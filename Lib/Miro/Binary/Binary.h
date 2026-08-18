#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Miro binary wire format ("MIB1") — the compact, machine-oriented
// counterpart to the JSON layer. Optimized for encode/decode speed,
// wire size, and cheap random access, borrowing from protobuf and
// Cap'n Proto:
//
//   file      := "MIB1" | u32 keyTableOffset | value | keyTable
//   keyTable  := varint keyCount | { varint length | bytes }*
//   value     := tag byte | payload
//
//   Null / False / True   (no payload)
//   Int          zigzag varint
//   Double       8 raw little-endian IEEE-754 bytes
//   Float32      4 raw little-endian bytes — used whenever the double
//                round-trips through float exactly, so always lossless
//   String       varint byteLength | utf8 bytes
//   Object       u32 payloadSize | { varint keyId | value }*
//   Map          u32 payloadSize | { varint keyLength | bytes | value }*
//   Array        u32 payloadSize | u32 count | value*
//   PackedFloat  u32 count | count * 4 raw bytes
//   PackedDouble u32 count | count * 8 raw bytes
//   PackedInt32  u32 payloadSize | u32 count | zigzag varints
//   PackedInt64  u32 payloadSize | u32 count | zigzag varints
//
// Design notes:
// - Object keys are interned once into the trailing key table and
//   referenced by varint id, so an array of 10k identical records pays
//   for each field name once — protobuf's field numbers without the
//   .proto file. Map keys are data rather than schema and stay inline.
// - Every container is size-prefixed, so a reader can skip any value
//   in O(1) without parsing it. That is the index: View can jump to
//   doc["tracks"][7]["title"] touching only the bytes on that path.
// - payloadSize counts the bytes that follow the size field itself.
// - Multi-byte scalars are little-endian and floats store their raw
//   bits, so numeric round-trips are bit-exact.
//
// Journal ("MIBJ") — the append-friendly wrapper for map-shaped values,
// so inserting or removing one entry never rewrites the file:
//
//   journal := "MIBJ" | u32 baseSize | baseDoc (a full "MIB1" file) | frame*
//   frame   := u8 kind (1 = set, 2 = remove)
//              u32 payloadSize
//              payload
//              u32 fnv1a(payload)
//   payload(set)    := varint keyLength | key | valueDoc (a full "MIB1" file)
//   payload(remove) := varint keyLength | key
//
// Readers load the base and replay frames in order; later frames win.
// A torn append (crash mid-write) fails the length or checksum test and
// the tail from that point is dropped — the journal is always readable
// up to the last complete frame. fromBinary() handles journals
// transparently for std::map<std::string, V> targets; Document/View
// random access applies to plain "MIB1" files only (a journal's frames
// would go unseen, so it refuses them rather than serve stale reads).

namespace Miro::Binary
{

using Buffer = std::vector<std::uint8_t>;

enum class Tag : std::uint8_t
{
    Null = 0,
    False = 1,
    True = 2,
    Int = 3,
    Double = 4,
    Float32 = 5,
    String = 6,
    Object = 7,
    Map = 8,
    Array = 9,
    PackedFloat = 10,
    PackedDouble = 11,
    PackedInt32 = 12,
    PackedInt64 = 13,
};

inline constexpr auto npos = static_cast<std::size_t>(-1);
inline constexpr std::size_t headerSize = 8;
inline constexpr std::array<std::uint8_t, 4> magic {'M', 'I', 'B', '1'};
inline constexpr std::array<std::uint8_t, 4> journalMagic {'M', 'I', 'B', 'J'};

inline bool isDocument(std::span<const std::uint8_t> data)
{
    return data.size() >= magic.size()
           && std::memcmp(data.data(), magic.data(), magic.size()) == 0;
}

inline bool isJournal(std::span<const std::uint8_t> data)
{
    return data.size() >= journalMagic.size()
           && std::memcmp(data.data(), journalMagic.data(), journalMagic.size())
                  == 0;
}

// Either wire format — the "is this ours?" sniff for stores that also
// accept other encodings on the same path.
inline bool isEncoded(std::span<const std::uint8_t> data)
{
    return isDocument(data) || isJournal(data);
}

std::uint32_t fnv1a(std::span<const std::uint8_t> data);

enum class FrameKind : std::uint8_t
{
    Set = 1,
    Remove = 2,
};

// One decoded journal frame. `key` and `value` are views into the
// journal's bytes; `value` is a complete encoded document (Set only).
struct JournalFrame
{
    FrameKind kind = FrameKind::Set;
    std::string_view key;
    std::span<const std::uint8_t> value;
};

// Shape of a journal, walked and validated up to the first torn or
// corrupt frame. frameBytes/frameCount cover only the valid frames —
// they are what a store compares against baseSize to decide when the
// tail has outgrown the base and it is time to compact.
struct JournalInfo
{
    bool valid = false;
    bool truncated = false;
    std::size_t baseStart = 0;
    std::size_t baseSize = 0;
    std::size_t frameBytes = 0;
    std::size_t frameCount = 0;
};

JournalInfo inspectJournal(std::span<const std::uint8_t> data);
std::vector<JournalFrame> journalFrames(std::span<const std::uint8_t> data);

// Byte-level builders; the typed entry points (toBinaryJournal,
// binaryJournalSet / binaryJournalRemove) live in SerializeBinary.h.
Buffer makeJournal(std::span<const std::uint8_t> baseDoc);
Buffer makeSetFrame(std::string_view key, std::span<const std::uint8_t> valueDoc);
Buffer makeRemoveFrame(std::string_view key);

inline std::uint64_t zigzagEncode(std::int64_t value)
{
    return (static_cast<std::uint64_t>(value) << 1)
           ^ static_cast<std::uint64_t>(value >> 63);
}

inline std::int64_t zigzagDecode(std::uint64_t value)
{
    return static_cast<std::int64_t>(value >> 1)
           ^ -static_cast<std::int64_t>(value & 1);
}

// Append-only output buffer with byte-position patching (container
// sizes are back-filled once their payload is known) and object-key
// interning. Use through toBinary(); finish() seals the stream.
class Writer
{
public:
    Writer()
    {
        buffer.reserve(1024);
        buffer.insert(buffer.end(), magic.begin(), magic.end());
        writeU32(0); // key table offset, patched by finish()
    }

    std::size_t position() const { return buffer.size(); }

    void writeByte(std::uint8_t byte) { buffer.push_back(byte); }
    void writeTag(Tag tag) { writeByte(static_cast<std::uint8_t>(tag)); }

    void writeRaw(const void* data, std::size_t size)
    {
        auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer.insert(buffer.end(), bytes, bytes + size);
    }

    void writeU32(std::uint32_t value) { writeRaw(&value, sizeof(value)); }

    void patchU32(std::size_t pos, std::uint32_t value)
    {
        std::memcpy(buffer.data() + pos, &value, sizeof(value));
    }

    void writeVarint(std::uint64_t value)
    {
        while (value >= 0x80)
        {
            writeByte(static_cast<std::uint8_t>(value) | 0x80);
            value >>= 7;
        }

        writeByte(static_cast<std::uint8_t>(value));
    }

    void writeZigzag(std::int64_t value) { writeVarint(zigzagEncode(value)); }

    void writeString(std::string_view text)
    {
        writeVarint(text.size());
        writeRaw(text.data(), text.size());
    }

    std::uint32_t internKey(std::string_view key)
    {
        if (auto it = keyIds.find(key); it != keyIds.end())
            return it->second;

        auto id = static_cast<std::uint32_t>(keyOrder.size());
        auto [it, inserted] = keyIds.emplace(std::string {key}, id);
        keyOrder.push_back(it->first);
        return id;
    }

    void truncateTo(std::size_t pos) { buffer.resize(pos); }

    // Appends the key table and patches its offset into the header.
    // The writer is spent afterwards.
    Buffer finish()
    {
        auto tableOffset = static_cast<std::uint32_t>(buffer.size());
        writeVarint(keyOrder.size());

        for (auto key: keyOrder)
            writeString(key);

        patchU32(4, tableOffset);
        return std::move(buffer);
    }

private:
    struct StringHash
    {
        using is_transparent = void;

        std::size_t operator()(std::string_view text) const
        {
            return std::hash<std::string_view> {}(text);
        }
    };

    Buffer buffer;
    std::unordered_map<std::string, std::uint32_t, StringHash, std::equal_to<>>
        keyIds;

    // Views into keyIds' node-stable keys, in id order, for finish().
    std::vector<std::string_view> keyOrder;
};

namespace Detail
{

// Bounds-checked forward reader. Any out-of-range read flips ok to
// false and yields zeros; callers check ok once at the end instead of
// after every read.
struct ByteReader
{
    explicit ByteReader(std::span<const std::uint8_t> dataToUse,
                        std::size_t posToUse = 0)
        : data(dataToUse)
        , pos(posToUse)
    {
        if (pos > data.size())
            ok = false;
    }

    std::uint8_t readByte()
    {
        if (pos >= data.size())
        {
            ok = false;
            return 0;
        }

        return data[pos++];
    }

    std::uint64_t readVarint()
    {
        auto result = std::uint64_t {0};
        auto shift = 0;

        while (shift < 64)
        {
            auto byte = readByte();
            result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;

            if ((byte & 0x80) == 0)
                return result;

            shift += 7;
        }

        ok = false;
        return 0;
    }

    std::int64_t readZigzag() { return zigzagDecode(readVarint()); }

    std::uint32_t readU32()
    {
        auto value = std::uint32_t {0};
        readRaw(&value, sizeof(value));
        return value;
    }

    bool readRaw(void* out, std::size_t size)
    {
        if (size > data.size() - pos || pos > data.size())
        {
            ok = false;
            return false;
        }

        std::memcpy(out, data.data() + pos, size);
        pos += size;
        return true;
    }

    std::string_view readString(std::size_t size)
    {
        if (size > data.size() - pos || pos > data.size())
        {
            ok = false;
            return {};
        }

        auto view = std::string_view {
            reinterpret_cast<const char*>(data.data() + pos), size};
        pos += size;
        return view;
    }

    void skip(std::size_t size)
    {
        if (size > data.size() - pos || pos > data.size())
        {
            ok = false;
            return;
        }

        pos += size;
    }

    std::span<const std::uint8_t> data;
    std::size_t pos = 0;
    bool ok = true;
};

// Advances the reader past one whole value (tag included). O(1) for
// everything except varint-packed arrays, whose payload is still
// skipped as a single block thanks to the size prefix.
bool skipValue(ByteReader& reader);

// Parsed container header for one value. Leaves get tag + payloadStart
// (first byte after the tag); containers additionally get their end,
// count and fixed element size, all clamped to the buffer.
struct ValueInfo
{
    Tag tag = Tag::Null;
    bool valid = false;
    std::size_t payloadStart = 0;
    std::size_t payloadEnd = 0;
    std::size_t count = 0;
    std::size_t elemSize = 0;
};

ValueInfo parseValueHeader(std::span<const std::uint8_t> data, std::size_t pos);

} // namespace Detail

class View;

// Non-owning parsed handle on an encoded buffer: validates the header
// and indexes the key table. The underlying bytes must outlive the
// Document and any View into it.
class Document
{
public:
    Document() = default;

    explicit Document(std::span<const std::uint8_t> dataToUse);

    bool isValid() const { return valid; }
    std::span<const std::uint8_t> bytes() const { return data; }
    std::size_t rootOffset() const { return headerSize; }

    View root() const;

    std::string_view keyName(std::uint32_t id) const
    {
        return id < keyNames.size() ? keyNames[id] : std::string_view {};
    }

    std::optional<std::uint32_t> findKey(std::string_view name) const
    {
        if (auto it = keyLookup.find(name); it != keyLookup.end())
            return it->second;

        return std::nullopt;
    }

private:
    std::span<const std::uint8_t> data {};
    std::vector<std::string_view> keyNames;
    std::unordered_map<std::string_view, std::uint32_t> keyLookup;
    bool valid = false;
};

// Zero-parse cursor into an encoded buffer: navigation touches only
// the bytes on the path to the requested value, skipping siblings via
// their size prefixes — nothing is decoded until an as...() call.
// Stateless, so operator[](i) on a non-packed array costs O(i) skips;
// for bulk sequential access decode into a struct instead.
class View
{
public:
    View() = default;

    View(const Document& docToUse, std::size_t posToUse)
        : doc(&docToUse)
        , pos(posToUse)
    {
    }

    bool isValid() const;
    bool isNull() const { return tagIs(Tag::Null); }
    bool isBool() const { return tagIs(Tag::False) || tagIs(Tag::True); }
    bool isNumber() const;
    bool isString() const { return tagIs(Tag::String); }
    bool isObject() const { return tagIs(Tag::Object); }
    bool isMap() const { return tagIs(Tag::Map); }
    bool isArray() const;

    // Object / Map member lookup; invalid View if absent or not an
    // object/map.
    View operator[](std::string_view key) const;

    // Array element lookup (packed arrays included); invalid View if
    // out of range or not an array.
    View operator[](std::size_t index) const;

    // Array element count, or object/map entry count (scans entries).
    std::size_t size() const;

    bool asBool(bool fallback = false) const;
    std::int64_t asInt(std::int64_t fallback = 0) const;
    double asNumber(double fallback = 0.0) const;
    std::string_view asString() const;

    // Byte offset of this value in the encoded buffer — the "index"
    // hook: stash it, mmap the blob later, and resume from here with
    // View {doc, offset}.
    std::size_t offset() const { return pos; }

private:
    bool tagIs(Tag tag) const;

    // For elements of packed arrays, which have no tag byte of their
    // own: `packed` holds the array's tag and pos points at raw data.
    Tag packedTag() const { return static_cast<Tag>(packed); }
    bool isPackedElement() const { return packed != noPacked; }
    double packedElementValue() const;

    static constexpr std::uint8_t noPacked = 0xFF;

    friend class Document;

    const Document* doc = nullptr;
    std::size_t pos = npos;
    std::uint8_t packed = noPacked;
};

} // namespace Miro::Binary
