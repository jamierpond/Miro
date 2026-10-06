// Round-trip coverage for the Miro::Vector / Miro::Array / EA::MapVector /
// Miro::OwningPointer reflect overloads (Miro:: aliases come from
// Lib/Miro/Containers.h; EA:: spellings continue to work too).

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace Miro;

namespace
{

struct Inner
{
    int count = 0;
    std::string label;

    MIRO_REFLECT(count, label)
};

struct WithEAVector
{
    Vector<int> values;

    MIRO_REFLECT(values)
};

struct WithEAArray
{
    Array<double, 3> samples;

    MIRO_REFLECT(samples)
};

struct WithEAMapVector
{
    EA::MapVector<std::string, std::string> entries;

    MIRO_REFLECT(entries)
};

struct WithEAOwningPointer
{
    OwningPointer<Inner> child;

    MIRO_REFLECT(child)
};

struct WithEAOwnedVector
{
    OwnedVector<Inner> items;

    MIRO_REFLECT(items)
};

// A user-defined class inheriting from EA::Vector<T>. The dispatch and
// shape trait should recognize it as an array via its base class without
// any per-subclass registration.
class IntList : public Vector<int>
{
public:
    using Vector::Vector;
};

struct WithUserVectorSubclass
{
    IntList numbers;

    MIRO_REFLECT(numbers)
};

} // namespace

auto eaVectorRoundtrip = test("EA::Vector<int> round-trip") = []
{
    auto original = WithEAVector {};
    original.values.add(1);
    original.values.add(2);
    original.values.add(3);

    auto loaded = createFromJSON<WithEAVector>(toJSON(original));

    check(loaded.values.size() == 3);
    check(loaded.values[0] == 1);
    check(loaded.values[1] == 2);
    check(loaded.values[2] == 3);
};

auto eaVectorEmptyRoundtrip = test("EA::Vector empty round-trip") = []
{
    auto original = WithEAVector {};
    auto loaded = createFromJSON<WithEAVector>(toJSON(original));

    check(loaded.values.empty());
};

auto eaArrayRoundtrip = test("EA::Array<double, N> round-trip") = []
{
    auto original = WithEAArray {};
    original.samples[0] = 0.5;
    original.samples[1] = 1.5;
    original.samples[2] = 2.5;

    auto loaded = createFromJSON<WithEAArray>(toJSON(original));

    check(loaded.samples[0] == 0.5);
    check(loaded.samples[1] == 1.5);
    check(loaded.samples[2] == 2.5);
};

auto eaMapVectorRoundtrip = test("EA::MapVector<string, string> round-trip") = []
{
    auto original = WithEAMapVector {};
    original.entries["alpha"] = "one";
    original.entries["beta"] = "two";

    auto loaded = createFromJSON<WithEAMapVector>(toJSON(original));

    check(loaded.entries.size() == 2);
    check(*loaded.entries.getValue("alpha") == "one");
    check(*loaded.entries.getValue("beta") == "two");
};

auto eaOwningPointerSetRoundtrip =
    test("EA::OwningPointer<T> populated round-trip") = []
{
    auto original = WithEAOwningPointer {};
    original.child.create();
    original.child->count = 7;
    original.child->label = "ready";

    auto loaded = createFromJSON<WithEAOwningPointer>(toJSON(original));

    check(loaded.child.get() != nullptr);
    check(loaded.child->count == 7);
    check(loaded.child->label == "ready");
};

auto eaOwningPointerNullRoundtrip = test("EA::OwningPointer<T> null round-trip") = []
{
    auto original = WithEAOwningPointer {};
    auto loaded = createFromJSON<WithEAOwningPointer>(toJSON(original));

    check(loaded.child.get() == nullptr);
};

auto eaOwningPointerLoadFromNull =
    test("EA::OwningPointer<T> loads null payload as empty") = []
{
    auto val = WithEAOwningPointer {};
    val.child.create();
    val.child->count = 99;

    fromJSONString(val, R"({"child": null})");

    check(val.child.get() == nullptr);
};

auto eaOwnedVectorRoundtrip = test("EA::OwnedVector<T> round-trip") = []
{
    auto original = WithEAOwnedVector {};
    original.items.createNew().count = 1;
    original.items.back()->label = "first";
    original.items.createNew().count = 2;
    original.items.back()->label = "second";

    auto loaded = createFromJSON<WithEAOwnedVector>(toJSON(original));

    check(loaded.items.size() == 2);
    check(loaded.items[0].get() != nullptr);
    check(loaded.items[0]->count == 1);
    check(loaded.items[0]->label == "first");
    check(loaded.items[1].get() != nullptr);
    check(loaded.items[1]->count == 2);
    check(loaded.items[1]->label == "second");
};

auto eaOwnedVectorEmptyRoundtrip = test("EA::OwnedVector empty round-trip") = []
{
    auto original = WithEAOwnedVector {};
    auto loaded = createFromJSON<WithEAOwnedVector>(toJSON(original));

    check(loaded.items.empty());
};

auto eaOwnedVectorWithNullSlot =
    test("EA::OwnedVector loads null slots as empty entries") = []
{
    auto val = WithEAOwnedVector {};
    fromJSONString(val, R"({"items": [null, {"count": 5, "label": "ok"}]})");

    check(val.items.size() == 2);
    check(val.items[0].get() == nullptr);
    check(val.items[1].get() != nullptr);
    check(val.items[1]->count == 5);
    check(val.items[1]->label == "ok");
};

auto userVectorSubclassRoundtrip =
    test("class derived from EA::Vector<T> round-trips as an array") = []
{
    auto original = WithUserVectorSubclass {};
    original.numbers.add(10);
    original.numbers.add(20);
    original.numbers.add(30);

    auto json = toJSONString(original);
    check(json.find("[10,20,30]") != std::string::npos);

    auto loaded = createFromJSON<WithUserVectorSubclass>(toJSON(original));

    check(loaded.numbers.size() == 3);
    check(loaded.numbers[0] == 10);
    check(loaded.numbers[1] == 20);
    check(loaded.numbers[2] == 30);
};

namespace
{
struct Beat
{
    double position = 0.0;

    MIRO_REFLECT(position)
};

struct WithEAVariant
{
    EA::Variant<Inner, Beat> value;

    MIRO_REFLECT(value)
};
} // namespace

auto eaVariantRoundtripFirst =
    test("EA::Variant<Inner, Beat> round-trips the first alternative") = []
{
    auto original = WithEAVariant {};
    original.value = Inner {3, "x"};

    auto json = toJSONString(original);
    check(json == R"({"value":{"Inner":{"count":3,"label":"x"}}})");

    auto loaded = createFromJSONString<WithEAVariant>(json);
    auto* inner = loaded.value.get<Inner>();

    check(inner != nullptr);
    check(inner->count == 3);
    check(inner->label == "x");
};

auto eaVariantRoundtripSecond =
    test("EA::Variant<Inner, Beat> round-trips the second alternative") = []
{
    auto original = WithEAVariant {};
    original.value = Beat {1.5};

    auto json = toJSONString(original);
    check(json == R"({"value":{"Beat":{"position":1.5}}})");

    auto loaded = createFromJSONString<WithEAVariant>(json);
    auto* beat = loaded.value.get<Beat>();

    check(beat != nullptr);
    check(beat->position == 1.5);
};

auto eaVariantInVectorRoundtrip = test("Vector<EA::Variant> round-trips") = []
{
    auto original = Vector<EA::Variant<Inner, Beat>> {};
    original.add(Beat {0.25});
    original.add(Inner {7, "seven"});

    auto loaded = createFromJSON<Vector<EA::Variant<Inner, Beat>>>(toJSON(original));

    check(loaded.size() == 2);
    check(loaded[0].holds<Beat>());
    check(loaded[1].holds<Inner>());
    check(loaded[1].get<Inner>()->count == 7);
};
