#include "PackedStructTestTypes.h"

#include <Miro/Reflect.h>
#include <Miro/Xml.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

using namespace PackedStructTestTypes;

static_assert(Detail::PackableStruct<Vec3>);
static_assert(Detail::PackableStruct<Keyframe>);
static_assert(Detail::PackableStruct<ReorderedFields>);
static_assert(Detail::PackableStruct<RenamedFields>);
static_assert(Detail::PackableStruct<HandWritten>);
static_assert(!Detail::PackableStruct<WithText>);
static_assert(!Detail::PackableStruct<WithFlag>);
static_assert(!Detail::PackableStruct<DerivedWithoutOwnList>);
static_assert(!Detail::PackableStruct<HandWrittenNotDeclared>);
static_assert(!Detail::PackableStruct<float>);
static_assert(Detail::PackableStruct<ExternalVec3>);
static_assert(Detail::PackableStruct<ExternalRenamedSize>);
static_assert(!Detail::PackableStruct<ExternalLabelledPoint>);

constexpr auto keyframeFieldCount = std::size_t {9};
static_assert(Detail::PackedStructLayout<Keyframe>::fields.size()
              == keyframeFieldCount);

struct PackedStructProbeLog
{
    std::size_t packedStructCalls = 0;
    std::size_t elementCount = 0;
    std::size_t stride = 0;
    std::vector<std::vector<std::string>> fieldPaths;
    std::vector<std::size_t> fieldOffsets;
    std::size_t objectVisits = 0;
};

// One array slot that records what visitPackedStructs is offered. When it
// accepts, a load writes `loadedFirstFieldValue` into the first field of
// every element.
struct PackedStructProbeReflector : Reflector
{
    PackedStructProbeReflector(PackedStructProbeLog& logToUse,
                               Options optsToUse,
                               bool acceptsPackedToUse)
        : Reflector(std::move(optsToUse))
        , log(logToUse)
        , acceptsPacked(acceptsPackedToUse)
    {
    }

    static constexpr auto loadedArraySize = std::size_t {3};
    static constexpr auto loadedFirstFieldValue = 7;

    void visit(PrimitiveRef /*ref*/) override {}
    void writeNull() override {}
    ValueKind kind() const override { return ValueKind::Array; }

    bool visitPackedStructs(PackedStructArrayRef ref) override
    {
        if (!acceptsPacked)
            return false;

        ++log.packedStructCalls;
        log.elementCount = ref.getElementCount();
        log.stride = ref.stride;

        for (const auto& field: ref.fields)
        {
            log.fieldPaths.emplace_back(field.path.begin(), field.path.end());
            log.fieldOffsets.push_back(field.offset);
        }

        if (isLoading())
            fillFirstFieldOfEveryElement(ref);

        return true;
    }

    static void fillFirstFieldOfEveryElement(const PackedStructArrayRef& ref)
    {
        const auto& field = ref.fields.front();

        for (auto i = std::size_t {0}; i < ref.getElementCount(); ++i)
        {
            auto* fieldBytes =
                ref.elementBytes.data() + i * ref.stride + field.offset;
            std::visit(
                [fieldBytes](auto type)
                {
                    using Number = typename decltype(type)::type;
                    auto value = static_cast<Number>(loadedFirstFieldValue);
                    std::memcpy(fieldBytes, &value, sizeof(value));
                },
                field.type);
        }
    }

    std::size_t arraySize() const override { return loadedArraySize; }

    Reflector& atKey(std::string_view /*key*/, Options childOpts) override
    {
        return createChild(childOpts);
    }

    Reflector& atIndex(std::size_t /*index*/, Options childOpts) override
    {
        if (childOpts.shape == Shape::Object)
            ++log.objectVisits;

        return createChild(childOpts);
    }

    Reflector& createChild(const Options& childOpts)
    {
        child = std::make_unique<PackedStructProbeReflector>(
            log, childOpts, acceptsPacked);
        return *child;
    }

    PackedStructProbeLog& log;
    bool acceptsPacked = true;
    std::unique_ptr<PackedStructProbeReflector> child;
};

template <typename T>
PackedStructProbeLog reflectThroughProbe(T& value, Mode mode, bool acceptsPacked)
{
    auto log = PackedStructProbeLog {};
    auto probe = PackedStructProbeReflector {
        log, Detail::topLevelOptions<T>(mode), acceptsPacked};
    Detail::reflectValue(probe, value);
    return log;
}

using Path = std::vector<std::string>;
using Offsets = std::vector<std::size_t>;

const auto vec3Offsets =
    Offsets {offsetof(Vec3, x), offsetof(Vec3, y), offsetof(Vec3, z)};
const auto keyframeValueXOffset =
    offsetof(Keyframe, value) + offsetof(Quaternion, x);
const auto keyframeInTangentWOffset =
    offsetof(Keyframe, inTangent) + offsetof(Quaternion, w);
const auto reorderedOffsets =
    Offsets {offsetof(ReorderedFields, second), offsetof(ReorderedFields, first)};

auto vectorOfStructsGoesWhole = test(
    "Packed structs: a vector of Vec3 is offered as one block with its layout") = []
{
    auto points = std::vector<Vec3> {{1, 2, 3}, {4, 5, 6}};
    auto log = reflectThroughProbe(points, Mode::Save, true);

    check(log.packedStructCalls == 1);
    check(log.elementCount == points.size());
    check(log.stride == sizeof(Vec3));
    check(log.fieldPaths == std::vector<Path> {{"x"}, {"y"}, {"z"}});
    check(log.fieldOffsets == vec3Offsets);
    check(log.objectVisits == 0);
};

auto nestedStructsFlatten =
    test("Packed structs: a nested packable struct flattens into dotted paths") = []
{
    auto keyframes = std::vector<Keyframe>(2);
    auto log = reflectThroughProbe(keyframes, Mode::Save, true);

    check(log.packedStructCalls == 1);
    check(log.fieldPaths.size() == keyframeFieldCount);
    check(log.fieldPaths[0] == Path {"time"});
    check(log.fieldPaths[1] == Path {"value", "x"});
    check(log.fieldPaths[8] == Path {"inTangent", "w"});
    check(log.fieldOffsets[1] == keyframeValueXOffset);
    check(log.fieldOffsets[8] == keyframeInTangentWOffset);
};

auto reflectOrderIsKept =
    test("Packed structs: fields come in reflect order, not memory order") = []
{
    auto values = std::vector<ReorderedFields>(1);
    auto renamed = std::vector<RenamedFields>(1);
    auto log = reflectThroughProbe(values, Mode::Save, true);
    auto renamedLog = reflectThroughProbe(renamed, Mode::Save, true);

    check(log.fieldPaths == std::vector<Path> {{"second"}, {"first"}});
    check(log.fieldOffsets == reorderedOffsets);
    check(renamedLog.fieldPaths == std::vector<Path> {{"w"}, {"h"}});
};

auto loadFillsResizedVector =
    test("Packed structs: a load resizes the vector, then fills it in place") = []
{
    auto points = std::vector<Vec3> {};
    auto log = reflectThroughProbe(points, Mode::Load, true);

    check(log.packedStructCalls == 1);
    check(points
          == std::vector<Vec3>(PackedStructProbeReflector::loadedArraySize,
                               Vec3 {7, 0, 0}));
};

auto fixedArrayOfStructs =
    test("Packed structs: a std::array of structs is offered whole") = []
{
    auto points = std::array<Vec3, PackedStructProbeReflector::loadedArraySize> {};
    auto log = reflectThroughProbe(points, Mode::Load, true);

    check(log.packedStructCalls == 1);
    check(log.elementCount == points.size());
    check(points[2] == Vec3 {7, 0, 0});
};

auto declinedWalksObjects = test(
    "Packed structs: a reflector that declines gets one object per element") = []
{
    auto points = std::vector<Vec3>(4);
    auto log = reflectThroughProbe(points, Mode::Save, false);

    check(log.packedStructCalls == 0);
    check(log.objectVisits == points.size());
};

auto unpackableStructsWalk =
    test("Packed structs: a struct with a string, bool or own reflect() walks") = []
{
    auto labelled = std::vector<WithText>(2);
    auto flagged = std::vector<WithFlag>(2);
    auto undeclared = std::vector<HandWrittenNotDeclared>(2);

    check(reflectThroughProbe(labelled, Mode::Save, true).packedStructCalls == 0);
    check(reflectThroughProbe(flagged, Mode::Save, true).packedStructCalls == 0);
    check(reflectThroughProbe(undeclared, Mode::Save, true).packedStructCalls == 0);
};

auto declaredHandWrittenPacks =
    test("Packed structs: MIRO_PACKED lets a hand-written reflect() pack") = []
{
    auto values = std::vector<HandWritten>(2);
    auto log = reflectThroughProbe(values, Mode::Save, true);

    check(log.packedStructCalls == 1);
    check(log.fieldPaths == std::vector<Path> {{"id"}, {"weight"}});
};

auto externalStructsPack =
    test("Packed structs: MIRO_REFLECT_EXTERNAL number structs pack undeclared") = []
{
    auto points = std::vector<ExternalVec3>(2);
    auto log = reflectThroughProbe(points, Mode::Save, true);

    check(log.packedStructCalls == 1);
    check(log.stride == sizeof(ExternalVec3));
    check(log.fieldPaths == std::vector<Path> {{"x"}, {"y"}, {"z"}});
    check(log.fieldOffsets
          == Offsets {offsetof(ExternalVec3, x),
                      offsetof(ExternalVec3, y),
                      offsetof(ExternalVec3, z)});
};

auto externalRenamedStructsPack =
    test("Packed structs: MIRO_REFLECT_EXTERNAL_MEMBERS packs under its keys") = []
{
    auto sizes = std::vector<ExternalRenamedSize>(2);
    auto log = reflectThroughProbe(sizes, Mode::Save, true);

    check(log.packedStructCalls == 1);
    check(log.fieldPaths == std::vector<Path> {{"w"}, {"h"}});
};

auto externalStructWithTextWalks =
    test("Packed structs: an external struct with a string walks") = []
{
    auto labelled = std::vector<ExternalLabelledPoint>(2);
    auto log = reflectThroughProbe(labelled, Mode::Save, true);

    check(log.packedStructCalls == 0);
    check(log.objectVisits == labelled.size());
};

auto jsonIsUnchanged =
    test("Packed structs: JSON writes and reads an array of objects as before") = []
{
    auto keyframes = std::vector<Keyframe> {
        {0.5f, {1, 2, 3, 4}, {}},
        {1.0f, {}, {5, 6, 7, 8}},
    };
    auto text = toJSONString(keyframes);

    check(text
          == R"([{"inTangent":{"w":1,"x":0,"y":0,"z":0},"time":0.5,)"
             R"("value":{"w":4,"x":1,"y":2,"z":3}},)"
             R"({"inTangent":{"w":8,"x":5,"y":6,"z":7},"time":1,)"
             R"("value":{"w":1,"x":0,"y":0,"z":0}}])");
    check(createFromJSONString<std::vector<Keyframe>>(text) == keyframes);
};

auto xmlIsUnchanged =
    test("Packed structs: XML round-trips an array of packable structs") = []
{
    auto path = Path3 {{{1, 2, 3}, {4, 5, 6}}};

    check(createFromXMLString<Path3>(toXMLString(path)).points == path.points);
};

} // namespace
