#include <Miro/Json.h>
#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

#include <string>
#include <string_view>

using namespace nano;
using namespace Miro;
using namespace Miro::Json;

auto objectComparatorIsTransparent = test("Object comparator is transparent") = []
{ check(requires { typename Object::key_compare::is_transparent; }); };

auto objectFindsByStringView = test("Object::find takes a string_view") = []
{
    auto object = Object {};
    object.emplace("k", Value {42});

    auto found = object.find(std::string_view {"k"});

    check(found != object.end());
    check(found->second.asNumber() == 42.0);
};

// A view into the middle of a longer buffer has no terminator after its
// last character. Anything that reaches for key.data() reads past the
// end; the lookup has to honour the view's size.
auto findTakesAnUnterminatedView = test("find takes an unterminated view") = []
{
    auto object = Object {};
    object.emplace("ab", Value {1});
    object.emplace("abc", Value {2});
    object.emplace("abcdef", Value {3});

    auto haystack = std::string {"abcdef"};
    auto key = std::string_view {haystack}.substr(0, 3);

    auto found = Miro::Json::find(object, key);

    check(found != nullptr);
    check(found->asNumber() == 2.0);
};

auto findReturnsNullForMissingKey = test("find returns null for a missing key") = []
{
    auto object = Object {};
    object.emplace("a", Value {1});

    check(Miro::Json::find(object, "b") == nullptr);
};

auto findGivesAMutableValue = test("find gives a mutable value") = []
{
    auto object = Object {};
    object.emplace("a", Value {1});

    Miro::Json::find(object, std::string_view {"a"})->data = 2.0;

    check(object.at("a").asNumber() == 2.0);
};

auto duplicateKeysKeepTheFirstValue =
    test("Duplicate keys keep the first value") = []
{
    auto value = parse(R"({"a":1,"b":2,"a":3})");
    auto& object = value.asObject();

    check(object.size() == 2);
    check(object.at("a").asNumber() == 1.0);
};

auto objectKeepsKeysSorted = test("Object keeps its keys sorted") = []
{
    auto value = parse(R"({"c":1,"a":2,"b":3})");
    auto keys = std::string {};

    for (const auto& [key, entry]: value.asObject())
        keys += key;

    check(keys == "abc");
    check(print(value) == R"({"a":2,"b":3,"c":1})");
};

// The reflection layer classifies an Object through IsMapLike, which has
// to admit the comparator or a field spelled Object stops compiling.
struct ClassWithJsonObject
{
    void reflect(Miro::Reflector& ref) { ref["obj"](obj); }

    Object obj;
};

auto objectStaysReflectable = test("An Object field stays reflectable") = []
{
    auto value = ClassWithJsonObject {};
    value.obj.emplace("a", Value {1});
    value.obj.emplace("b", Value {"two"});

    check(toJSONString(value) == R"({"obj":{"a":1,"b":"two"}})");
};
