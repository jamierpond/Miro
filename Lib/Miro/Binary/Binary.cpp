#include "Binary.h"

namespace Miro::Binary
{
namespace
{

void appendRaw(Buffer& out, const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void appendU32(Buffer& out, std::uint32_t value)
{
    appendRaw(out, &value, sizeof(value));
}

void appendVarint(Buffer& out, std::uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<std::uint8_t>(value) | 0x80);
        value >>= 7;
    }

    out.push_back(static_cast<std::uint8_t>(value));
}

// frame := kind (1) | payloadSize (4) | payload | checksum (4)
constexpr std::size_t frameOverhead = 9;

Buffer makeFrame(FrameKind kind,
                 std::string_view key,
                 std::span<const std::uint8_t> valueDoc)
{
    auto payload = Buffer {};
    payload.reserve(key.size() + valueDoc.size() + 4);
    appendVarint(payload, key.size());
    appendRaw(payload, key.data(), key.size());
    appendRaw(payload, valueDoc.data(), valueDoc.size());

    auto out = Buffer {};
    out.reserve(frameOverhead + payload.size());
    out.push_back(static_cast<std::uint8_t>(kind));
    appendU32(out, static_cast<std::uint32_t>(payload.size()));
    appendRaw(out, payload.data(), payload.size());
    appendU32(out, fnv1a(payload));
    return out;
}

// Walks the journal, validating each frame; stops (and flags truncated)
// at the first torn or corrupt one. Frames are appended to `out` when a
// collector is given.
JournalInfo walkJournal(std::span<const std::uint8_t> data,
                        std::vector<JournalFrame>* out)
{
    auto info = JournalInfo {};

    if (!isJournal(data) || data.size() < headerSize)
        return info;

    auto baseSize = std::uint32_t {};
    std::memcpy(&baseSize, data.data() + 4, sizeof(baseSize));

    if (baseSize > data.size() - headerSize)
        return info;

    info.valid = true;
    info.baseStart = headerSize;
    info.baseSize = baseSize;

    auto pos = headerSize + baseSize;

    while (pos < data.size())
    {
        if (data.size() - pos < frameOverhead)
        {
            info.truncated = true;
            break;
        }

        auto kind = data[pos];
        auto payloadSize = std::uint32_t {};
        std::memcpy(&payloadSize, data.data() + pos + 1, sizeof(payloadSize));

        if ((kind != static_cast<std::uint8_t>(FrameKind::Set)
             && kind != static_cast<std::uint8_t>(FrameKind::Remove))
            || payloadSize > data.size() - pos - frameOverhead)
        {
            info.truncated = true;
            break;
        }

        auto payload = data.subspan(pos + 5, payloadSize);
        auto storedChecksum = std::uint32_t {};
        std::memcpy(&storedChecksum,
                    data.data() + pos + 5 + payloadSize,
                    sizeof(storedChecksum));

        if (storedChecksum != fnv1a(payload))
        {
            info.truncated = true;
            break;
        }

        auto reader = Detail::ByteReader {payload};
        auto keyLength = reader.readVarint();
        auto key = reader.readString(keyLength);

        if (!reader.ok)
        {
            info.truncated = true;
            break;
        }

        if (out != nullptr)
            out->push_back(JournalFrame {
                static_cast<FrameKind>(kind), key, payload.subspan(reader.pos)});

        pos += frameOverhead + payloadSize;
        info.frameBytes += frameOverhead + payloadSize;
        ++info.frameCount;
    }

    return info;
}

} // namespace

std::uint32_t fnv1a(std::span<const std::uint8_t> data)
{
    auto hash = std::uint32_t {2166136261U};

    for (auto byte: data)
    {
        hash ^= byte;
        hash *= 16777619U;
    }

    return hash;
}

JournalInfo inspectJournal(std::span<const std::uint8_t> data)
{
    return walkJournal(data, nullptr);
}

std::vector<JournalFrame> journalFrames(std::span<const std::uint8_t> data)
{
    auto frames = std::vector<JournalFrame> {};
    walkJournal(data, &frames);
    return frames;
}

Buffer makeJournal(std::span<const std::uint8_t> baseDoc)
{
    auto out = Buffer {};
    out.reserve(headerSize + baseDoc.size());
    appendRaw(out, journalMagic.data(), journalMagic.size());
    appendU32(out, static_cast<std::uint32_t>(baseDoc.size()));
    appendRaw(out, baseDoc.data(), baseDoc.size());
    return out;
}

Buffer makeSetFrame(std::string_view key, std::span<const std::uint8_t> valueDoc)
{
    return makeFrame(FrameKind::Set, key, valueDoc);
}

Buffer makeRemoveFrame(std::string_view key)
{
    return makeFrame(FrameKind::Remove, key, {});
}

namespace Detail
{

bool skipValue(ByteReader& reader)
{
    auto tag = static_cast<Tag>(reader.readByte());

    switch (tag)
    {
        case Tag::Null:
        case Tag::False:
        case Tag::True:
            break;

        case Tag::Int:
            reader.readVarint();
            break;

        case Tag::Double:
            reader.skip(8);
            break;

        case Tag::Float32:
            reader.skip(4);
            break;

        case Tag::String:
            reader.skip(reader.readVarint());
            break;

        case Tag::Object:
        case Tag::Map:
        case Tag::Array:
        case Tag::PackedInt32:
        case Tag::PackedInt64:
            reader.skip(reader.readU32());
            break;

        case Tag::PackedFloat:
            reader.skip(std::size_t {reader.readU32()} * 4);
            break;

        case Tag::PackedDouble:
            reader.skip(std::size_t {reader.readU32()} * 8);
            break;

        default:
            reader.ok = false;
            break;
    }

    return reader.ok;
}

ValueInfo parseValueHeader(std::span<const std::uint8_t> data, std::size_t pos)
{
    auto info = ValueInfo {};
    auto reader = ByteReader {data, pos};
    info.tag = static_cast<Tag>(reader.readByte());

    if (!reader.ok
        || static_cast<std::uint8_t>(info.tag)
               > static_cast<std::uint8_t>(Tag::PackedInt64))
        return info;

    switch (info.tag)
    {
        case Tag::Object:
        case Tag::Map:
        {
            auto size = reader.readU32();
            info.payloadStart = reader.pos;
            info.payloadEnd = std::min(reader.pos + size, data.size());
            break;
        }

        case Tag::Array:
        {
            auto size = reader.readU32();
            auto end = std::min(reader.pos + size, data.size());
            info.count = reader.readU32();
            info.payloadStart = reader.pos;
            info.payloadEnd = end;

            // An element is at least one byte — clamp a corrupt count
            // so callers can size containers from it safely.
            if (info.payloadEnd > info.payloadStart)
                info.count =
                    std::min(info.count, info.payloadEnd - info.payloadStart);
            else
                info.count = 0;

            break;
        }

        case Tag::PackedFloat:
        case Tag::PackedDouble:
        {
            info.elemSize = info.tag == Tag::PackedFloat ? 4 : 8;
            info.count = reader.readU32();
            info.payloadStart = reader.pos;

            auto available = data.size() > reader.pos ? data.size() - reader.pos
                                                      : std::size_t {0};
            info.count =
                std::min(std::size_t {info.count}, available / info.elemSize);
            info.payloadEnd = info.payloadStart + info.count * info.elemSize;
            break;
        }

        case Tag::PackedInt32:
        case Tag::PackedInt64:
        {
            auto size = reader.readU32();
            auto end = std::min(reader.pos + size, data.size());
            info.count = reader.readU32();
            info.payloadStart = reader.pos;
            info.payloadEnd = end;

            if (info.payloadEnd > info.payloadStart)
                info.count =
                    std::min(info.count, info.payloadEnd - info.payloadStart);
            else
                info.count = 0;

            break;
        }

        default:
            info.payloadStart = reader.pos;
            info.payloadEnd = data.size();
            break;
    }

    info.valid = reader.ok;
    return info;
}

} // namespace Detail

Document::Document(std::span<const std::uint8_t> dataToUse)
    : data(dataToUse)
{
    if (data.size() < headerSize)
        return;

    if (std::memcmp(data.data(), magic.data(), magic.size()) != 0)
        return;

    auto tableOffset = std::uint32_t {};
    std::memcpy(&tableOffset, data.data() + 4, sizeof(tableOffset));

    if (tableOffset < headerSize || tableOffset > data.size())
        return;

    auto reader = Detail::ByteReader {data, tableOffset};
    auto count = reader.readVarint();

    // Each key costs at least one length byte, so a valid count can't
    // exceed the remaining bytes — reject corrupt tables early.
    if (count > data.size() - std::min(reader.pos, data.size()))
        return;

    keyNames.reserve(count);
    keyLookup.reserve(count);

    for (auto i = std::uint64_t {0}; i < count; ++i)
    {
        auto length = reader.readVarint();
        auto name = reader.readString(length);

        if (!reader.ok)
            return;

        keyNames.push_back(name);
        keyLookup.emplace(name, static_cast<std::uint32_t>(i));
    }

    valid = reader.ok;
}

View Document::root() const
{
    if (!valid)
        return {};

    return View {*this, rootOffset()};
}

bool View::isValid() const
{
    return doc != nullptr && pos != npos;
}

bool View::tagIs(Tag tag) const
{
    if (!isValid() || isPackedElement())
        return false;

    return Detail::parseValueHeader(doc->bytes(), pos).tag == tag;
}

bool View::isNumber() const
{
    if (!isValid())
        return false;

    if (isPackedElement())
        return true;

    auto tag = Detail::parseValueHeader(doc->bytes(), pos).tag;
    return tag == Tag::Int || tag == Tag::Double || tag == Tag::Float32;
}

bool View::isArray() const
{
    if (!isValid() || isPackedElement())
        return false;

    auto tag = Detail::parseValueHeader(doc->bytes(), pos).tag;
    return tag == Tag::Array || tag == Tag::PackedFloat || tag == Tag::PackedDouble
           || tag == Tag::PackedInt32 || tag == Tag::PackedInt64;
}

View View::operator[](std::string_view key) const
{
    if (!isValid() || isPackedElement())
        return {};

    auto info = Detail::parseValueHeader(doc->bytes(), pos);

    if (info.tag == Tag::Object)
    {
        auto wanted = doc->findKey(key);

        if (!wanted)
            return {};

        auto reader = Detail::ByteReader {doc->bytes(), info.payloadStart};

        while (reader.ok && reader.pos < info.payloadEnd)
        {
            auto id = reader.readVarint();
            auto valueStart = reader.pos;

            if (!Detail::skipValue(reader))
                break;

            if (id == *wanted)
                return View {*doc, valueStart};
        }

        return {};
    }

    if (info.tag == Tag::Map)
    {
        auto reader = Detail::ByteReader {doc->bytes(), info.payloadStart};

        while (reader.ok && reader.pos < info.payloadEnd)
        {
            auto length = reader.readVarint();
            auto name = reader.readString(length);
            auto valueStart = reader.pos;

            if (!Detail::skipValue(reader))
                break;

            if (name == key)
                return View {*doc, valueStart};
        }
    }

    return {};
}

View View::operator[](std::size_t index) const
{
    if (!isValid() || isPackedElement())
        return {};

    auto info = Detail::parseValueHeader(doc->bytes(), pos);

    if (index >= info.count)
        return {};

    if (info.tag == Tag::PackedFloat || info.tag == Tag::PackedDouble)
    {
        auto view = View {*doc, info.payloadStart + index * info.elemSize};
        view.packed = static_cast<std::uint8_t>(info.tag);
        return view;
    }

    if (info.tag == Tag::PackedInt32 || info.tag == Tag::PackedInt64)
    {
        auto reader = Detail::ByteReader {doc->bytes(), info.payloadStart};

        for (auto i = std::size_t {0}; i < index; ++i)
            reader.readVarint();

        if (!reader.ok)
            return {};

        auto view = View {*doc, reader.pos};
        view.packed = static_cast<std::uint8_t>(info.tag);
        return view;
    }

    if (info.tag == Tag::Array)
    {
        auto reader = Detail::ByteReader {doc->bytes(), info.payloadStart};

        for (auto i = std::size_t {0}; i < index; ++i)
            if (!Detail::skipValue(reader))
                return {};

        if (reader.pos >= info.payloadEnd)
            return {};

        return View {*doc, reader.pos};
    }

    return {};
}

std::size_t View::size() const
{
    if (!isValid() || isPackedElement())
        return 0;

    auto info = Detail::parseValueHeader(doc->bytes(), pos);

    if (info.tag == Tag::Array || info.elemSize != 0 || info.tag == Tag::PackedInt32
        || info.tag == Tag::PackedInt64)
        return info.count;

    if (info.tag == Tag::Object || info.tag == Tag::Map)
    {
        auto reader = Detail::ByteReader {doc->bytes(), info.payloadStart};
        auto count = std::size_t {0};

        while (reader.ok && reader.pos < info.payloadEnd)
        {
            if (info.tag == Tag::Object)
            {
                reader.readVarint();
            }
            else
            {
                auto length = reader.readVarint();
                reader.readString(length);
            }

            if (!Detail::skipValue(reader))
                break;

            ++count;
        }

        return count;
    }

    return 0;
}

double View::packedElementValue() const
{
    auto reader = Detail::ByteReader {doc->bytes(), pos};

    switch (packedTag())
    {
        case Tag::PackedFloat:
        {
            auto value = float {};
            reader.readRaw(&value, sizeof(value));
            return reader.ok ? static_cast<double>(value) : 0.0;
        }

        case Tag::PackedDouble:
        {
            auto value = double {};
            reader.readRaw(&value, sizeof(value));
            return reader.ok ? value : 0.0;
        }

        case Tag::PackedInt32:
        case Tag::PackedInt64:
            return static_cast<double>(reader.readZigzag());

        default:
            return 0.0;
    }
}

bool View::asBool(bool fallback) const
{
    if (!isValid() || isPackedElement())
        return fallback;

    auto tag = Detail::parseValueHeader(doc->bytes(), pos).tag;

    if (tag == Tag::True)
        return true;

    if (tag == Tag::False)
        return false;

    return fallback;
}

std::int64_t View::asInt(std::int64_t fallback) const
{
    if (!isValid())
        return fallback;

    if (isPackedElement())
        return static_cast<std::int64_t>(packedElementValue());

    auto reader = Detail::ByteReader {doc->bytes(), pos};
    auto tag = static_cast<Tag>(reader.readByte());

    switch (tag)
    {
        case Tag::Int:
            return reader.readZigzag();

        case Tag::Double:
        {
            auto value = double {};
            reader.readRaw(&value, sizeof(value));
            return static_cast<std::int64_t>(value);
        }

        case Tag::Float32:
        {
            auto value = float {};
            reader.readRaw(&value, sizeof(value));
            return static_cast<std::int64_t>(value);
        }

        default:
            return fallback;
    }
}

double View::asNumber(double fallback) const
{
    if (!isValid())
        return fallback;

    if (isPackedElement())
        return packedElementValue();

    auto reader = Detail::ByteReader {doc->bytes(), pos};
    auto tag = static_cast<Tag>(reader.readByte());

    switch (tag)
    {
        case Tag::Int:
            return static_cast<double>(reader.readZigzag());

        case Tag::Double:
        {
            auto value = double {};
            reader.readRaw(&value, sizeof(value));
            return reader.ok ? value : fallback;
        }

        case Tag::Float32:
        {
            auto value = float {};
            reader.readRaw(&value, sizeof(value));
            return reader.ok ? static_cast<double>(value) : fallback;
        }

        default:
            return fallback;
    }
}

std::string_view View::asString() const
{
    if (!isValid() || isPackedElement())
        return {};

    auto reader = Detail::ByteReader {doc->bytes(), pos};

    if (static_cast<Tag>(reader.readByte()) != Tag::String)
        return {};

    return reader.readString(reader.readVarint());
}

} // namespace Miro::Binary
