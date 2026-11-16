// Reflector::approveElementAllocation: a load asks the reflector before a
// container allocates for its stored elements, and a refusal leaves the
// container as it was. The default approves, so JSON and XML are unchanged.

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

struct LargeElement
{
    MIRO_REFLECT(samples)

    std::array<double, 512> samples {};
};

struct ShapeBase
{
    virtual ~ShapeBase() = default;
    void reflect(Reflector&) {}
};

struct CircleShape : ShapeBase
{
    MIRO_REFLECT(radius)

    double radius = 0.0;
};

struct AllocationRequest
{
    std::size_t elementCount = 0;
    std::size_t elementSizeInBytes = 0;
};

struct AllocationLog
{
    bool isApproving = false;
    std::vector<AllocationRequest> requests;
};

constexpr auto storedElementCount = std::size_t {100'000};

// A loading slot that claims to hold `storedCount` elements (or map keys,
// or one object key), each of them null.
class StoredElementsReflector final : public Reflector
{
public:
    StoredElementsReflector(AllocationLog& logToUse,
                            ValueKind kindToUse,
                            std::size_t storedCountToUse,
                            Options optsToUse)
        : Reflector(std::move(optsToUse))
        , log(logToUse)
        , storedKind(kindToUse)
        , storedCount(storedCountToUse)
    {
    }

    void visit(PrimitiveRef) override {}
    void writeNull() override {}
    ValueKind kind() const override { return storedKind; }

    Reflector& atKey(std::string_view, Options childOpts) override
    {
        return spawnNullChild(childOpts);
    }

    Reflector& atIndex(std::size_t, Options childOpts) override
    {
        return spawnNullChild(childOpts);
    }

    std::size_t arraySize() const override { return storedCount; }

    Vector<std::string> mapKeys() const override
    {
        auto keys = Vector<std::string> {};

        for (std::size_t i = 0; i < storedCount; ++i)
            keys.add("key" + std::to_string(i));

        return keys;
    }

    bool approveElementAllocation(std::size_t elementCount,
                                  std::size_t elementSizeInBytes) override
    {
        log.requests.push_back({elementCount, elementSizeInBytes});
        return log.isApproving;
    }

    void requirePolymorphicSupport(std::string_view) override {}

private:
    Reflector& spawnNullChild(Options childOpts)
    {
        childOpts.mode = Mode::Load;
        child = std::make_unique<StoredElementsReflector>(
            log, ValueKind::Null, 0, childOpts);
        return *child;
    }

    AllocationLog& log;
    ValueKind storedKind;
    std::size_t storedCount;
    std::unique_ptr<StoredElementsReflector> child;
};

template <typename T>
AllocationLog loadStoredElements(T& value,
                                 ValueKind storedKind,
                                 std::size_t storedCount,
                                 bool isApproving)
{
    auto log = AllocationLog {isApproving, {}};
    auto reflector = StoredElementsReflector {
        log, storedKind, storedCount, Detail::topLevelOptions<T>(Mode::Load)};
    Detail::reflectValue(reflector, value);
    return log;
}

bool wasAskedOnceFor(const AllocationLog& log,
                     std::size_t elementCount,
                     std::size_t elementSizeInBytes)
{
    return log.requests.size() == 1
           && log.requests.front().elementCount == elementCount
           && log.requests.front().elementSizeInBytes == elementSizeInBytes;
}

} // namespace

auto refusedVectorKeepsItsElements =
    test("approveElementAllocation: a refused vector keeps its elements") = []
{
    auto elements = std::vector<LargeElement>(2);
    auto log =
        loadStoredElements(elements, ValueKind::Array, storedElementCount, false);

    check(elements.size() == 2);
    check(wasAskedOnceFor(log, storedElementCount, sizeof(LargeElement)));
};

auto approvedVectorLoadsEveryElement =
    test("approveElementAllocation: an approved vector loads every element") = []
{
    constexpr auto smallCount = std::size_t {3};
    auto elements = std::vector<int> {};
    loadStoredElements(elements, ValueKind::Array, smallCount, true);

    check(elements.size() == smallCount);
};

auto refusedMapKeepsItsEntries =
    test("approveElementAllocation: a refused map keeps its entries") = []
{
    using Map = std::map<std::string, LargeElement>;
    auto entries = Map {{"kept", {}}};
    auto log =
        loadStoredElements(entries, ValueKind::Object, storedElementCount, false);

    check(entries.size() == 1 && entries.contains("kept"));
    check(wasAskedOnceFor(log, storedElementCount, sizeof(Map::value_type)));
};

auto refusedMapVectorKeepsItsEntries =
    test("approveElementAllocation: a refused EA::MapVector keeps its entries") = []
{
    auto entries = EA::MapVector<std::string, LargeElement> {};
    entries["kept"] = {};
    auto log =
        loadStoredElements(entries, ValueKind::Object, storedElementCount, false);

    check(entries.size() == 1);
    check(log.requests.size() == 1);
};

auto refusedOwningPointerStaysEmpty =
    test("approveElementAllocation: a refused OwningPointer creates nothing") = []
{
    auto pointer = OwningPointer<LargeElement> {};
    auto log = loadStoredElements(pointer, ValueKind::Object, 0, false);

    check(pointer.get() == nullptr);
    check(wasAskedOnceFor(log, 1, sizeof(LargeElement)));
};

auto refusedPolymorphicPointerStaysEmpty =
    test("approveElementAllocation: a refused polymorphic alternative creates "
         "nothing") = []
{
    auto pointer = OwningPointer<ShapeBase> {};
    auto log = AllocationLog {false, {}};
    auto reflector =
        StoredElementsReflector {log, ValueKind::Object, 1, {.mode = Mode::Load}};
    reflectPolymorphic(reflector,
                       pointer,
                       [](auto& dispatcher)
                       { dispatcher.template alt<CircleShape>("key0"); });

    check(pointer.get() == nullptr);
    check(wasAskedOnceFor(log, 1, sizeof(CircleShape)));
};
