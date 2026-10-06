#pragma once

#include "../Binary/Binary.h"
#include "Reflector.h"

namespace Miro
{

// Streams a reflection walk straight into a Binary::Writer — no
// intermediate value tree. Container slots write their tag plus a
// placeholder size at construction and back-patch it on destruction;
// the strict open-then-close child ordering (previous child destroyed
// before the next one spawns) guarantees the payload is complete by
// then. Save-only.
class BinaryWriterReflector final : public Reflector
{
public:
    BinaryWriterReflector(Binary::Writer& writerToUse, Options optsToUse);
    ~BinaryWriterReflector() override;

    void visit(PrimitiveRef ref) override;
    bool visitPacked(PackedArrayRef ref) override;
    void writeNull() override;
    ValueKind kind() const override;

    Reflector& atKey(std::string_view key, Options childOpts) override;
    Reflector& atIndex(std::size_t index, Options childOpts) override;

    void resizeArray(std::size_t newSize) override;

    void markPresent() override;

    void requirePolymorphicSupport(std::string_view) override {}

private:
    Reflector& spawnChild(Options childOpts);
    void commitShape();
    void writeArrayHeader();

    void writePacked(std::span<float> values);
    void writePacked(std::span<double> values);
    void writePacked(std::span<std::int32_t> values);
    void writePacked(std::span<std::int64_t> values);

    Binary::Writer& writer;
    std::size_t startPos;
    std::size_t sizePos = Binary::npos;
    std::size_t countPos = Binary::npos;
    std::size_t elementCount = 0;
    bool cancelled = false;

    // Set on the child slot of an Omittable<T> until it calls
    // markPresent(): where this slot's bytes (an object field's key
    // included) begin. Never claimed, the destructor rewinds to here so
    // the key vanishes — or, for an array element, which can't be
    // absent, leaves a Null in its place so the count stays right.
    std::size_t pendingStart = Binary::npos;
    bool pendingIsElement = false;

    OwningPointer<BinaryWriterReflector> currentChild;
};

// Walks a reflection load directly over the encoded bytes via a
// Binary::Document. Object fields keep a resume cursor, so loads that
// ask for keys in the written order (the common same-struct round
// trip) cost O(1) per field, with a wrap-around scan as the fallback
// for reordered or missing keys. Sequential array access is O(1) per
// element the same way. Load-only; never throws — malformed input
// degrades to absent slots, leaving fields at their prior values.
class BinaryReaderReflector final : public Reflector
{
public:
    BinaryReaderReflector(const Binary::Document& docToUse,
                          std::size_t posToUse,
                          Options optsToUse);

    ~BinaryReaderReflector() override;

    void visit(PrimitiveRef ref) override;
    bool visitPacked(PackedArrayRef ref) override;
    void writeNull() override {}
    ValueKind kind() const override;
    bool isIntegerNumber() const override;

    Reflector& atKey(std::string_view key, Options childOpts) override;
    Reflector& atIndex(std::size_t index, Options childOpts) override;

    std::size_t arraySize() const override;
    Vector<std::string> mapKeys() const override;

    void requirePolymorphicSupport(std::string_view) override {}

private:
    // Spawns either a regular child at an absolute value offset, an
    // absent child (pos == npos), or a packed-element child (elemTag
    // set, pos pointing at raw element bytes).
    BinaryReaderReflector(const Binary::Document& docToUse,
                          std::size_t posToUse,
                          Options optsToUse,
                          Binary::Tag elemTagToUse);

    Reflector& spawnChild(std::size_t childPos, Options childOpts);
    Reflector& spawnPackedChild(std::size_t childPos,
                                Options childOpts,
                                Binary::Tag elemTagToUse);
    Reflector& spawnMissingChild(Options childOpts);

    bool absent() const { return pos == Binary::npos; }
    bool isPackedElement() const { return elemTag != Binary::Tag::Null; }

    double packedElementNumber() const;
    std::int64_t packedElementInteger() const;
    std::size_t findObjectField(std::uint32_t keyId);
    std::size_t findMapEntry(std::string_view key);

    const Binary::Document& doc;
    std::size_t pos;
    Binary::Tag elemTag = Binary::Tag::Null;
    Binary::Detail::ValueInfo info {};

    // Resume cursor for object/map field scans and sequential array
    // element access.
    std::size_t cursorPos = 0;
    std::size_t cursorIndex = 0;

    OwningPointer<BinaryReaderReflector> currentChild;
};

} // namespace Miro
