#include <Miro/Miro.h>

#include <NanoTest/NanoTest.h>

// Zero-annotation structural reflection (C++26 / P2996). Every type in
// this file is a plain aggregate: no reflect() method, no MIRO_REFLECT
// macro. The tests only build their assertions when the reflection path
// is active (MIRO_HAS_REFLECTION, set by ReflectDispatch.h when the
// experimental <meta> header is available and the build passes
// -freflection -fexpansion-statements). On an ordinary C++20 build the
// header is absent, the macro is undefined, and this TU compiles to
// nothing — so it never disturbs the existing toolchains.

#ifdef MIRO_HAS_REFLECTION

using namespace nano;
using namespace Miro;

namespace
{
struct Address
{
    std::string city;
    int zip = 0;
};

struct Profile
{
    std::string name;
    int age = 0;
    bool admin = false;
    Address home; // nested aggregate
    std::vector<std::string> tags;
    std::vector<Address> visited; // vector of aggregates
};
} // namespace

auto aggregateSaveUsesFieldNames = test("Aggregate save uses field names") = []
{
    auto json = toJSON(Address {"London", 11});

    check(json["city"].isString());
    check(json["city"].asString() == "London");
    check(json["zip"].isNumber());
    check(json["zip"].asNumber() == 11.0);
};

auto aggregateRoundTrip = test("Aggregate round-trips with no macro") = []
{
    auto original = Profile {"Ada",
                             37,
                             true,
                             Address {"London", 11},
                             {"x", "y"},
                             {Address {"Paris", 75}, Address {"Berlin", 10}}};

    auto back = createFromJSONString<Profile>(toJSONString(original));

    check(back.name == "Ada");
    check(back.age == 37);
    check(back.admin == true);
    check(back.home.city == "London");
    check(back.home.zip == 11);
    check(back.tags.size() == 2);
    check(back.tags[1] == "y");
    check(back.visited.size() == 2);
    check(back.visited[0].zip == 75);
    check(back.visited[1].city == "Berlin");
};

auto aggregateFlowsThroughXml = test("Aggregate flows through XML reflector") = []
{
    auto back = createFromXMLString<Address>(toXMLString(Address {"Paris", 75}));

    check(back.city == "Paris");
    check(back.zip == 75);
};

#endif // MIRO_HAS_REFLECTION
