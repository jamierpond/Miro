#include "TestTypes.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace Miro;

// The YAML reflection layer reuses JsonReflector (YAML shares the
// JSON value model), so these tests focus on the string seam:
// toYAMLString / fromYAMLString / createFromYAMLString.

// --- Primitives ---

auto yamlSaveBool = test("YAML: save bool") = []
{
    auto val = ClassWithBool {};
    check(toYAMLString(val) == "active: true");
};

auto yamlSaveInt = test("YAML: save int") = []
{
    auto val = ClassWithInt {};
    check(toYAMLString(val) == "count: 42");
};

auto yamlSaveDouble = test("YAML: save double") = []
{
    auto val = ClassWithDouble {};
    check(toYAMLString(val) == "ratio: 3.14");
};

auto yamlSaveString = test("YAML: save string") = []
{
    auto val = ClassWithString {};
    check(toYAMLString(val) == "name: hello");
};

auto yamlLoadPrimitives = test("YAML: load primitives") = []
{
    auto val = createFromYAMLString<ClassWithInt>("count: 7");
    check(val.count == 7);

    auto s = createFromYAMLString<ClassWithString>("name: world");
    check(s.name == "world");
};

// --- Nested struct ---

auto yamlSaveNestedStruct = test("YAML: save nested struct") = []
{
    auto val = Outer {.a = 1, .nested = {5}, .label = "hi"};
    auto expected = std::string {"a: 1\n"
                                 "label: hi\n"
                                 "nested:\n"
                                 "  x: 5"};
    check(toYAMLString(val) == expected);
};

auto yamlLoadNestedStruct = test("YAML: load nested struct") = []
{
    auto val = createFromYAMLString<Outer>("a: 2\n"
                                           "label: hey\n"
                                           "nested:\n"
                                           "  x: 9\n");
    check(val.a == 2);
    check(val.label == "hey");
    check(val.nested.x == 9);
};

// --- Vectors ---

auto yamlSaveVectorOfInts = test("YAML: save vector<int>") = []
{
    auto val = ClassWithVectorOfInts {.nums = {1, 2, 3}};
    auto expected = std::string {"nums:\n"
                                 "  - 1\n"
                                 "  - 2\n"
                                 "  - 3"};
    check(toYAMLString(val) == expected);
};

auto yamlLoadVectorOfInts = test("YAML: load vector<int>") = []
{
    auto val = createFromYAMLString<ClassWithVectorOfInts>("nums:\n"
                                                           "  - 10\n"
                                                           "  - 20\n"
                                                           "  - 30\n");
    check(val.nums.size() == 3);
    check(val.nums[0] == 10);
    check(val.nums[1] == 20);
    check(val.nums[2] == 30);
};

auto yamlSaveVectorOfObjects = test("YAML: save vector<struct>") = []
{
    auto val = ClassWithVectorOfObjects {};
    auto expected = std::string {"items:\n"
                                 "  - x: 1\n"
                                 "  - x: 2\n"
                                 "  - x: 3"};
    check(toYAMLString(val) == expected);
};

auto yamlLoadVectorOfObjects = test("YAML: load vector<struct>") = []
{
    auto val = createFromYAMLString<ClassWithVectorOfObjects>("items:\n"
                                                              "  - x: 7\n"
                                                              "  - x: 8\n");
    check(val.items.size() == 2);
    check(val.items[0].x == 7);
    check(val.items[1].x == 8);
};

// --- Maps ---

auto yamlSaveStringMap = test("YAML: save map<string,string>") = []
{
    auto val = ClassWithStringMap {};
    auto expected = std::string {"data:\n"
                                 "  a: hello\n"
                                 "  b: world"};
    check(toYAMLString(val) == expected);
};

auto yamlLoadStringMap = test("YAML: load map<string,string>") = []
{
    auto val = createFromYAMLString<ClassWithStringMap>("data:\n"
                                                        "  a: x\n"
                                                        "  b: y\n"
                                                        "  c: z\n");
    check(val.data.size() == 3);
    check(val.data.at("a") == "x");
    check(val.data.at("b") == "y");
    check(val.data.at("c") == "z");
};

auto yamlSaveObjectMap = test("YAML: save map<string,struct>") = []
{
    auto val = ClassWithObjectMap {};
    val.items.emplace("foo", Inner {11});
    val.items.emplace("bar", Inner {22});

    auto expected = std::string {"items:\n"
                                 "  bar:\n"
                                 "    x: 22\n"
                                 "  foo:\n"
                                 "    x: 11"};
    check(toYAMLString(val) == expected);
};

auto yamlLoadObjectMap = test("YAML: load map<string,struct>") = []
{
    auto val = createFromYAMLString<ClassWithObjectMap>("items:\n"
                                                        "  alpha:\n"
                                                        "    x: 1\n"
                                                        "  beta:\n"
                                                        "    x: 2\n");
    check(val.items.size() == 2);
    check(val.items.at("alpha").x == 1);
    check(val.items.at("beta").x == 2);
};

// --- Optional ---

auto yamlLoadOptionalAbsent = test("YAML: load optional with missing keys") = []
{
    auto val = createFromYAMLString<ClassWithOptional>("{}");

    check(!val.maybeInt.has_value());
    check(!val.maybeInner.has_value());
};

auto yamlLoadOptionalPresent = test("YAML: load optional with present value") = []
{
    auto val = createFromYAMLString<ClassWithOptional>("maybeInt: 5\n"
                                                       "maybeInner:\n"
                                                       "  x: 8\n");
    check(val.maybeInt.has_value());
    check(*val.maybeInt == 5);
    check(val.maybeInner.has_value());
    check(val.maybeInner->x == 8);
};

auto yamlRoundTripOptional = test("YAML: round-trip optional") = []
{
    auto absent = ClassWithOptional {};
    auto reloadedAbsent =
        createFromYAMLString<ClassWithOptional>(toYAMLString(absent));
    check(!reloadedAbsent.maybeInt.has_value());
    check(!reloadedAbsent.maybeInner.has_value());

    auto present = ClassWithOptional {};
    present.maybeInt = 99;
    present.maybeInner = Inner {3};

    auto reloadedPresent =
        createFromYAMLString<ClassWithOptional>(toYAMLString(present));
    check(reloadedPresent.maybeInt.has_value());
    check(*reloadedPresent.maybeInt == 99);
    check(reloadedPresent.maybeInner.has_value());
    check(reloadedPresent.maybeInner->x == 3);
};

// --- Enum ---

auto yamlRoundTripEnum = test("YAML: round-trip enum") = []
{
    auto original = ClassWithEnum {};
    original.color = Color::Blue;
    original.signal = Signal::Stop;
    original.mode = ModeOn;

    auto reloaded = createFromYAMLString<ClassWithEnum>(toYAMLString(original));

    check(reloaded.color == Color::Blue);
    check(reloaded.signal == Signal::Stop);
    check(reloaded.mode == ModeOn);
};

// --- Round-trips ---

auto yamlRoundTripOuter = test("YAML: round-trip Outer") = []
{
    auto original = Outer {.a = 42, .nested = {123}, .label = "round"};
    auto reloaded = createFromYAMLString<Outer>(toYAMLString(original, 4));

    check(reloaded.a == original.a);
    check(reloaded.nested.x == original.nested.x);
    check(reloaded.label == original.label);
};

auto yamlRoundTripVectors = test("YAML: round-trip vectors") = []
{
    auto original =
        ClassWithVectorOfStrings {.tags = {"red", "5 < 6", "42", "true"}};
    auto reloaded =
        createFromYAMLString<ClassWithVectorOfStrings>(toYAMLString(original));

    check(reloaded.tags.size() == 4);
    check(reloaded.tags[0] == "red");
    check(reloaded.tags[1] == "5 < 6");
    check(reloaded.tags[2] == "42");
    check(reloaded.tags[3] == "true");
};

auto yamlRoundTripMaps = test("YAML: round-trip maps") = []
{
    auto original = ClassWithIntMap {.counts = {{"k1", 11}, {"k2", 22}}};
    auto reloaded = createFromYAMLString<ClassWithIntMap>(toYAMLString(original));

    check(reloaded.counts.size() == 2);
    check(reloaded.counts.at("k1") == 11);
    check(reloaded.counts.at("k2") == 22);
};

auto yamlRoundTripArrayOfDoubles = test("YAML: round-trip array<double, N>") = []
{
    auto original = ClassWithArrayOfDoubles {.vals = {1.5, 2.5, 3.5}};
    auto reloaded =
        createFromYAMLString<ClassWithArrayOfDoubles>(toYAMLString(original));

    check(reloaded.vals[0] == 1.5);
    check(reloaded.vals[1] == 2.5);
    check(reloaded.vals[2] == 3.5);
};

auto yamlRoundTripIntegrals = test("YAML: round-trip narrow integrals") = []
{
    auto original = ClassWithIntegrals {};
    auto reloaded = createFromYAMLString<ClassWithIntegrals>(toYAMLString(original));

    check(reloaded.u == original.u);
    check(reloaded.s == original.s);
    check(reloaded.ll == original.ll);
    check(reloaded.c == original.c);
};

auto yamlRoundTripUser = test("YAML: round-trip User") = []
{
    auto original = User {};
    original.name = "Alice";
    original.age = 30;
    original.active = false;
    original.address = {.street = "1 Main St", .zip = "12345"};
    original.tags = {"admin", "beta"};
    original.counters = {{"logins", 7}};
    original.note = "remember: be kind";
    original.shipping = Address {.street = "2 Side St", .zip = "67890"};
    original.color = Color::Blue;
    original.priority = Priority::High;

    auto reloaded = createFromYAMLString<User>(toYAMLString(original));

    check(reloaded.name == original.name);
    check(reloaded.age == original.age);
    check(reloaded.active == original.active);
    check(reloaded.address.street == original.address.street);
    check(reloaded.tags == original.tags);
    check(reloaded.counters == original.counters);
    check(reloaded.note == original.note);
    check(reloaded.shipping.has_value());
    check(reloaded.shipping->zip == original.shipping->zip);
    check(reloaded.color == original.color);
    check(reloaded.priority == original.priority);
    check(!reloaded.accent.has_value());
};

// --- Flow style ---

auto yamlSaveFlowStyle = test("YAML: save flow style") = []
{
    auto val = ClassWithVectorOfInts {.nums = {1, 2, 3}};
    check(toYAMLString(val, 0) == "{nums: [1, 2, 3]}");
};

// --- Load tolerance ---

auto yamlLoadIgnoresUnknownKeys = test("YAML: load ignores unknown keys") = []
{
    auto val = createFromYAMLString<ClassWithInt>("count: 3\n"
                                                  "unknown: ignored\n");
    check(val.count == 3);
};
