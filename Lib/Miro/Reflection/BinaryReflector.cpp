#include "BinaryReflector.h"
#include "NumericConvert.h"

#include <cstring>

namespace Miro
{

using Binary::Tag;
using Binary::Detail::ByteReader;
using Binary::Detail::parseValueHeader;
using Binary::Detail::skipValue;

namespace
{

// True when narrowing to float and widening back reproduces the exact
// double — the lossless case where the wire can spend 4 bytes instead
// of 8. NaN compares false and stays a full double, which is correct.
bool fitsFloat32(double value)
{
    return static_cast<double>(static_cast<float>(value)) == value;
}

} // namespace

// ---------------------------------------------------------------- Writer

BinaryWriterReflector::BinaryWriterReflector(Binary::Writer& writerToUse,
                                             Options optsToUse)
    : Reflector(optsToUse)
    , writer(writerToUse)
    , startPos(writerToUse.position())
{
    commitShape();
}

BinaryWriterReflector::~BinaryWriterReflector()
{
    currentChild.reset();

    if (pendingStart != Binary::npos)
    {
        writer.truncateTo(pendingStart);

        if (pendingIsElement)
            writer.writeTag(Tag::Null);

        return;
    }

    if (cancelled)
        return;

    if (countPos != Binary::npos)
        writer.patchU32(countPos, static_cast<std::uint32_t>(elementCount));

    if (sizePos != Binary::npos)
        writer.patchU32(
            sizePos, static_cast<std::uint32_t>(writer.position() - (sizePos + 4)));
}

void BinaryWriterReflector::commitShape()
{
    switch (opts.shape)
    {
        case Shape::Primitive:
            // visit() / writeNull() will write the value directly.
            break;

        case Shape::Object:
            writer.writeTag(Tag::Object);
            sizePos = writer.position();
            writer.writeU32(0);
            break;

        case Shape::Map:
            writer.writeTag(Tag::Map);
            sizePos = writer.position();
            writer.writeU32(0);
            break;

        case Shape::Array:
            writeArrayHeader();
            break;

        case Shape::Raw:
            // A raw JSON value: its kind is only known once the walk
            // starts. Commit to an object, the way the JSON layer does;
            // visit() / writeNull() / resizeArray() rewind and replace
            // it when the value turns out to be something else.
            writer.writeTag(Tag::Object);
            sizePos = writer.position();
            writer.writeU32(0);
            break;
    }
}

void BinaryWriterReflector::writeArrayHeader()
{
    writer.writeTag(Tag::Array);
    sizePos = writer.position();
    writer.writeU32(0);
    countPos = writer.position();
    writer.writeU32(0);
}

void BinaryWriterReflector::markPresent()
{
    pendingStart = Binary::npos;
}

void BinaryWriterReflector::visit(PrimitiveRef ref)
{
    if (opts.shape == Shape::Raw)
    {
        // A raw scalar replaces the speculative object header.
        writer.truncateTo(startPos);
        sizePos = Binary::npos;
    }

    std::visit(
        [this](auto* ptr)
        {
            using T = std::remove_pointer_t<decltype(ptr)>;

            if constexpr (std::same_as<T, bool>)
            {
                writer.writeTag(*ptr ? Tag::True : Tag::False);
            }
            else if constexpr (std::same_as<T, std::string>)
            {
                writer.writeTag(Tag::String);
                writer.writeString(*ptr);
            }
            else if constexpr (std::same_as<T, double>)
            {
                if (fitsFloat32(*ptr))
                {
                    auto narrow = static_cast<float>(*ptr);
                    writer.writeTag(Tag::Float32);
                    writer.writeRaw(&narrow, sizeof(narrow));
                }
                else
                {
                    writer.writeTag(Tag::Double);
                    writer.writeRaw(ptr, sizeof(double));
                }
            }
            else
            {
                writer.writeTag(Tag::Int);
                writer.writeZigzag(static_cast<std::int64_t>(*ptr));
            }
        },
        ref.data);
}

bool BinaryWriterReflector::visitPacked(PackedArrayRef ref)
{
    if (opts.shape != Shape::Array)
        return false;

    // Undo the generic Array header committed at construction and
    // replace the whole slot with a packed encoding.
    writer.truncateTo(startPos);
    cancelled = true;

    std::visit([this](auto values) { writePacked(values); }, ref.data);
    return true;
}

void BinaryWriterReflector::writePacked(std::span<float> values)
{
    writer.writeTag(Tag::PackedFloat);
    writer.writeU32(static_cast<std::uint32_t>(values.size()));
    writer.writeRaw(values.data(), values.size() * sizeof(float));
}

void BinaryWriterReflector::writePacked(std::span<double> values)
{
    auto allFitFloat = true;

    for (auto value: values)
    {
        if (!fitsFloat32(value))
        {
            allFitFloat = false;
            break;
        }
    }

    if (allFitFloat)
    {
        auto narrowed = std::vector<float>(values.begin(), values.end());
        writePacked(std::span<float> {narrowed});
        return;
    }

    writer.writeTag(Tag::PackedDouble);
    writer.writeU32(static_cast<std::uint32_t>(values.size()));
    writer.writeRaw(values.data(), values.size() * sizeof(double));
}

void BinaryWriterReflector::writePacked(std::span<std::int32_t> values)
{
    writer.writeTag(Tag::PackedInt32);
    auto packedSizePos = writer.position();
    writer.writeU32(0);
    writer.writeU32(static_cast<std::uint32_t>(values.size()));

    for (auto value: values)
        writer.writeZigzag(value);

    writer.patchU32(
        packedSizePos,
        static_cast<std::uint32_t>(writer.position() - (packedSizePos + 4)));
}

void BinaryWriterReflector::writePacked(std::span<std::int64_t> values)
{
    writer.writeTag(Tag::PackedInt64);
    auto packedSizePos = writer.position();
    writer.writeU32(0);
    writer.writeU32(static_cast<std::uint32_t>(values.size()));

    for (auto value: values)
        writer.writeZigzag(value);

    writer.patchU32(
        packedSizePos,
        static_cast<std::uint32_t>(writer.position() - (packedSizePos + 4)));
}

void BinaryWriterReflector::writeNull()
{
    // May arrive after a container header was committed (an empty
    // optional whose inner type is object/array-shaped) — nothing has
    // been written past this slot yet, so rewind and replace.
    writer.truncateTo(startPos);
    cancelled = true;
    writer.writeTag(Tag::Null);
}

ValueKind BinaryWriterReflector::kind() const
{
    return ValueKind::Absent;
}

Reflector& BinaryWriterReflector::spawnChild(Options childOpts)
{
    currentChild = new BinaryWriterReflector(writer, childOpts);
    return *currentChild;
}

Reflector& BinaryWriterReflector::atKey(std::string_view key, Options childOpts)
{
    // Destroy the previous child before appending the key: its size
    // patch reads writer.position() as "end of my payload", so nothing
    // may be appended until it has run.
    currentChild.reset();

    auto keyStart = writer.position();

    if (opts.shape == Shape::Map)
        writer.writeString(key);
    else
        writer.writeVarint(writer.internKey(key));

    spawnChild(childOpts);

    if (childOpts.omittable)
        currentChild->pendingStart = keyStart;

    return *currentChild;
}

Reflector& BinaryWriterReflector::atIndex(std::size_t index, Options childOpts)
{
    currentChild.reset();

    if (index + 1 > elementCount)
        elementCount = index + 1;

    auto elementStart = writer.position();
    spawnChild(childOpts);

    if (childOpts.omittable)
    {
        currentChild->pendingStart = elementStart;
        currentChild->pendingIsElement = true;
    }

    return *currentChild;
}

void BinaryWriterReflector::resizeArray(std::size_t newSize)
{
    if (opts.shape == Shape::Raw && countPos == Binary::npos)
    {
        // A raw array: swap the speculative object header for an array
        // one. Called before any element is written, so nothing is lost.
        writer.truncateTo(startPos);
        writeArrayHeader();
    }

    if (newSize > elementCount)
        elementCount = newSize;
}

// ---------------------------------------------------------------- Reader

BinaryReaderReflector::BinaryReaderReflector(const Binary::Document& docToUse,
                                             std::size_t posToUse,
                                             Options optsToUse)
    : BinaryReaderReflector(docToUse, posToUse, optsToUse, Tag::Null)
{
}

BinaryReaderReflector::BinaryReaderReflector(const Binary::Document& docToUse,
                                             std::size_t posToUse,
                                             Options optsToUse,
                                             Binary::Tag elemTagToUse)
    : Reflector(optsToUse)
    , doc(docToUse)
    , pos(posToUse)
    , elemTag(elemTagToUse)
{
    if (!absent() && !isPackedElement())
    {
        info = parseValueHeader(doc.bytes(), pos);

        if (!info.valid)
            pos = Binary::npos;

        cursorPos = info.payloadStart;
    }
}

BinaryReaderReflector::~BinaryReaderReflector() = default;

ValueKind BinaryReaderReflector::kind() const
{
    if (absent())
        return ValueKind::Absent;

    if (isPackedElement())
        return ValueKind::Number;

    switch (info.tag)
    {
        case Tag::Null:
            return ValueKind::Null;
        case Tag::False:
        case Tag::True:
            return ValueKind::Bool;
        case Tag::Int:
        case Tag::Double:
        case Tag::Float32:
            return ValueKind::Number;
        case Tag::String:
            return ValueKind::String;
        case Tag::Object:
        case Tag::Map:
            return ValueKind::Object;
        case Tag::Array:
        case Tag::PackedFloat:
        case Tag::PackedDouble:
        case Tag::PackedInt32:
        case Tag::PackedInt64:
            return ValueKind::Array;
    }

    return ValueKind::Absent;
}

bool BinaryReaderReflector::isIntegerNumber() const
{
    if (absent())
        return false;

    if (isPackedElement())
        return elemTag == Tag::PackedInt32 || elemTag == Tag::PackedInt64;

    return info.tag == Tag::Int;
}

double BinaryReaderReflector::packedElementNumber() const
{
    auto reader = ByteReader {doc.bytes(), pos};

    switch (elemTag)
    {
        case Tag::PackedFloat:
        {
            auto value = float {};
            reader.readRaw(&value, sizeof(value));
            return static_cast<double>(value);
        }

        case Tag::PackedDouble:
        {
            auto value = double {};
            reader.readRaw(&value, sizeof(value));
            return value;
        }

        default:
            return static_cast<double>(packedElementInteger());
    }
}

std::int64_t BinaryReaderReflector::packedElementInteger() const
{
    auto reader = ByteReader {doc.bytes(), pos};
    return reader.readZigzag();
}

void BinaryReaderReflector::visit(PrimitiveRef ref)
{
    if (absent())
        return;

    std::visit(
        [this](auto* ptr)
        {
            using T = std::remove_pointer_t<decltype(ptr)>;

            if (isPackedElement())
            {
                if constexpr (!std::same_as<T, bool>
                              && !std::same_as<T, std::string>)
                {
                    if (elemTag == Tag::PackedInt32 || elemTag == Tag::PackedInt64)
                        Detail::convertNumber(packedElementInteger(), *ptr);
                    else
                        Detail::convertNumber(packedElementNumber(), *ptr);
                }

                return;
            }

            auto reader = ByteReader {doc.bytes(), info.payloadStart};

            if constexpr (std::same_as<T, bool>)
            {
                if (info.tag == Tag::True)
                    *ptr = true;
                else if (info.tag == Tag::False)
                    *ptr = false;
            }
            else if constexpr (std::same_as<T, std::string>)
            {
                if (info.tag == Tag::String)
                {
                    auto length = reader.readVarint();
                    auto text = reader.readString(length);

                    if (reader.ok)
                        *ptr = std::string {text};
                }
            }
            else
            {
                if (info.tag == Tag::Int)
                {
                    auto value = reader.readZigzag();

                    if (reader.ok)
                        Detail::convertNumber(value, *ptr);
                }
                else if (info.tag == Tag::Double)
                {
                    auto value = double {};

                    if (reader.readRaw(&value, sizeof(value)))
                        Detail::convertNumber(value, *ptr);
                }
                else if (info.tag == Tag::Float32)
                {
                    auto value = float {};

                    if (reader.readRaw(&value, sizeof(value)))
                        Detail::convertNumber(static_cast<double>(value), *ptr);
                }
            }
        },
        ref.data);
}

bool BinaryReaderReflector::visitPacked(PackedArrayRef ref)
{
    if (absent() || isPackedElement())
        return false;

    if (info.tag != Tag::PackedFloat && info.tag != Tag::PackedDouble
        && info.tag != Tag::PackedInt32 && info.tag != Tag::PackedInt64)
        return false;

    std::visit(
        [this](auto dest)
        {
            using T = typename decltype(dest)::element_type;

            auto count = std::min(dest.size(), info.count);
            auto reader = ByteReader {doc.bytes(), info.payloadStart};

            if (info.tag == Tag::PackedFloat)
            {
                if constexpr (std::same_as<T, float>)
                {
                    reader.readRaw(dest.data(), count * sizeof(float));
                }
                else
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        auto value = float {};
                        reader.readRaw(&value, sizeof(value));
                        Detail::convertNumber(static_cast<double>(value), dest[i]);
                    }
                }
            }
            else if (info.tag == Tag::PackedDouble)
            {
                if constexpr (std::same_as<T, double>)
                {
                    reader.readRaw(dest.data(), count * sizeof(double));
                }
                else
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        auto value = double {};
                        reader.readRaw(&value, sizeof(value));
                        Detail::convertNumber(value, dest[i]);
                    }
                }
            }
            else
            {
                for (std::size_t i = 0; i < count; ++i)
                    Detail::convertNumber(reader.readZigzag(), dest[i]);
            }
        },
        ref.data);

    return true;
}

std::size_t BinaryReaderReflector::arraySize() const
{
    if (absent() || isPackedElement())
        return 0;

    switch (info.tag)
    {
        case Tag::Array:
        case Tag::PackedFloat:
        case Tag::PackedDouble:
        case Tag::PackedInt32:
        case Tag::PackedInt64:
            return info.count;

        default:
            return 0;
    }
}

Vector<std::string> BinaryReaderReflector::mapKeys() const
{
    auto keys = Vector<std::string> {};

    if (absent() || isPackedElement())
        return keys;

    if (info.tag != Tag::Object && info.tag != Tag::Map)
        return keys;

    auto reader = ByteReader {doc.bytes(), info.payloadStart};

    while (reader.ok && reader.pos < info.payloadEnd)
    {
        if (info.tag == Tag::Object)
        {
            auto id = reader.readVarint();

            if (skipValue(reader))
                keys.add(std::string {doc.keyName(static_cast<std::uint32_t>(id))});
        }
        else
        {
            auto length = reader.readVarint();
            auto name = reader.readString(length);

            if (skipValue(reader))
                keys.add(std::string {name});
        }
    }

    return keys;
}

std::size_t BinaryReaderReflector::findObjectField(std::uint32_t keyId)
{
    // Fields are usually requested in the order they were written, so
    // resume from the cursor and wrap around once for reordered or
    // missing keys.
    auto scan = [&](std::size_t from, std::size_t until) -> std::size_t
    {
        auto reader = ByteReader {doc.bytes(), from};

        while (reader.ok && reader.pos < until)
        {
            auto id = reader.readVarint();
            auto valueStart = reader.pos;

            if (!skipValue(reader))
                break;

            if (id == keyId)
            {
                cursorPos = reader.pos;
                return valueStart;
            }
        }

        return Binary::npos;
    };

    auto resumeFrom = cursorPos;

    if (auto found = scan(resumeFrom, info.payloadEnd); found != Binary::npos)
        return found;

    return scan(info.payloadStart, resumeFrom);
}

std::size_t BinaryReaderReflector::findMapEntry(std::string_view key)
{
    auto scan = [&](std::size_t from, std::size_t until) -> std::size_t
    {
        auto reader = ByteReader {doc.bytes(), from};

        while (reader.ok && reader.pos < until)
        {
            auto length = reader.readVarint();
            auto name = reader.readString(length);
            auto valueStart = reader.pos;

            if (!skipValue(reader))
                break;

            if (name == key)
            {
                cursorPos = reader.pos;
                return valueStart;
            }
        }

        return Binary::npos;
    };

    auto resumeFrom = cursorPos;

    if (auto found = scan(resumeFrom, info.payloadEnd); found != Binary::npos)
        return found;

    return scan(info.payloadStart, resumeFrom);
}

Reflector& BinaryReaderReflector::spawnChild(std::size_t childPos, Options childOpts)
{
    currentChild.reset();
    currentChild = new BinaryReaderReflector(doc, childPos, childOpts);
    return *currentChild;
}

Reflector& BinaryReaderReflector::spawnPackedChild(std::size_t childPos,
                                                   Options childOpts,
                                                   Binary::Tag elemTagToUse)
{
    currentChild.reset();
    currentChild = new BinaryReaderReflector(doc, childPos, childOpts, elemTagToUse);
    return *currentChild;
}

Reflector& BinaryReaderReflector::spawnMissingChild(Options childOpts)
{
    return spawnChild(Binary::npos, childOpts);
}

Reflector& BinaryReaderReflector::atKey(std::string_view key, Options childOpts)
{
    if (absent() || isPackedElement())
        return spawnMissingChild(childOpts);

    if (info.tag == Tag::Object)
    {
        auto keyId = doc.findKey(key);

        if (!keyId)
            return spawnMissingChild(childOpts);

        auto found = findObjectField(*keyId);

        if (found == Binary::npos)
            return spawnMissingChild(childOpts);

        return spawnChild(found, childOpts);
    }

    if (info.tag == Tag::Map)
    {
        auto found = findMapEntry(key);

        if (found == Binary::npos)
            return spawnMissingChild(childOpts);

        return spawnChild(found, childOpts);
    }

    return spawnMissingChild(childOpts);
}

Reflector& BinaryReaderReflector::atIndex(std::size_t index, Options childOpts)
{
    if (absent() || isPackedElement() || index >= info.count)
        return spawnMissingChild(childOpts);

    if (info.tag == Tag::PackedFloat || info.tag == Tag::PackedDouble)
        return spawnPackedChild(
            info.payloadStart + index * info.elemSize, childOpts, info.tag);

    if (info.tag == Tag::PackedInt32 || info.tag == Tag::PackedInt64)
    {
        if (index < cursorIndex)
        {
            cursorPos = info.payloadStart;
            cursorIndex = 0;
        }

        auto reader = ByteReader {doc.bytes(), cursorPos};

        while (cursorIndex < index && reader.ok)
        {
            reader.readVarint();
            ++cursorIndex;
            cursorPos = reader.pos;
        }

        if (!reader.ok)
            return spawnMissingChild(childOpts);

        return spawnPackedChild(cursorPos, childOpts, info.tag);
    }

    if (info.tag != Tag::Array)
        return spawnMissingChild(childOpts);

    if (index < cursorIndex)
    {
        cursorPos = info.payloadStart;
        cursorIndex = 0;
    }

    auto reader = ByteReader {doc.bytes(), cursorPos};

    while (cursorIndex < index && reader.ok)
    {
        if (!skipValue(reader))
            return spawnMissingChild(childOpts);

        ++cursorIndex;
        cursorPos = reader.pos;
    }

    if (!reader.ok || cursorPos >= info.payloadEnd)
        return spawnMissingChild(childOpts);

    auto elementPos = cursorPos;

    // Advance the cursor past this element so the next sequential
    // atIndex resumes in O(1).
    if (skipValue(reader))
    {
        cursorPos = reader.pos;
        cursorIndex = index + 1;
    }

    return spawnChild(elementPos, childOpts);
}

} // namespace Miro
