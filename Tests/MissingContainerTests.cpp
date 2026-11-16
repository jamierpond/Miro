// Loading data saved before a list or map field existed: the missing key
// leaves the field's default alone instead of emptying it.

#include <Miro/Reflect.h>
#include <Miro/Xml.h>

#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

template <typename To, typename From>
To convertThroughJson(const From& value)
{
    return createFromJSONString<To>(toJSONString(value));
}

// Writes `value` as From, reads it back as To, and hands the result to
// `expect`.
template <typename To, typename From, typename Expect>
void checkThroughJson(const From& value, Expect expect)
{
    expect(convertThroughJson<To>(value));
}

struct ProfileV1
{
    std::uint32_t field1 = 0;
    std::string field2;

    MIRO_REFLECT(field1, field2)
};

ProfileV1 sampleV1()
{
    return {.field1 = 4000000000u, .field2 = "hello"};
}

struct AddedV2
{
    std::uint32_t field1 = 0;
    std::string field2;
    int added = 42;
    std::vector<int> addedList {1, 2};
    std::map<std::string, int> addedMap {{"k", 1}};
    std::optional<int> addedOptional;
    std::optional<int> addedOptionalWithValue = 5;

    MIRO_REFLECT(field1,
                 field2,
                 added,
                 addedList,
                 addedMap,
                 addedOptional,
                 addedOptionalWithValue)
};

auto fieldAdded =
    test("Missing containers: fields added in V2 keep their defaults") = []
{
    checkThroughJson<AddedV2>(sampleV1(),
                              [](const AddedV2& v2)
                              {
                                  check(v2.field1 == 4000000000u);
                                  check(v2.field2 == "hello");
                                  check(v2.added == 42);
                                  check(v2.addedList == std::vector<int> {1, 2});
                                  check(v2.addedMap.size() == 1);
                                  check(v2.addedMap.at("k") == 1);
                                  check(!v2.addedOptional.has_value());
                                  check(v2.addedOptionalWithValue == 5);
                              });
};

struct OuterV1
{
    ProfileV1 profile;
    std::vector<ProfileV1> list;
    std::map<std::string, ProfileV1> byName;

    MIRO_REFLECT(profile, list, byName)
};

struct AddedOuterV2
{
    AddedV2 profile;
    std::vector<AddedV2> list;
    std::map<std::string, AddedV2> byName;

    MIRO_REFLECT(profile, list, byName)
};

OuterV1 sampleOuterV1()
{
    auto v1 = OuterV1 {};
    v1.profile = {.field1 = 1, .field2 = "nested"};
    v1.list = {{.field1 = 2, .field2 = "first"}, {.field1 = 3, .field2 = "second"}};
    v1.byName = {{"a", {.field1 = 4, .field2 = "alpha"}},
                 {"b", {.field1 = 5, .field2 = "beta"}}};
    return v1;
}

auto nestedAdded = test(
    "Missing containers: nested, vector and map elements default added fields") = []
{
    checkThroughJson<AddedOuterV2>(sampleOuterV1(),
                                   [](const AddedOuterV2& v2)
                                   {
                                       check(v2.profile.added == 42);
                                       check(v2.profile.field2 == "nested");
                                       check(v2.list.size() == 2);

                                       for (const auto& element: v2.list)
                                       {
                                           check(element.added == 42);
                                           check(element.addedList.size() == 2);
                                       }

                                       check(v2.byName.at("a").added == 42);
                                       check(v2.byName.at("a").field2 == "alpha");
                                   });
};

auto xmlEmptyVector = test(
    "Missing containers: XML empty vector over a non-empty default stays empty") = []
{
    // A missing key keeps a container's prior value; an empty one that
    // was written must still read back empty.
    struct WithList
    {
        std::vector<int> items {1, 2};
        void reflect(Miro::Reflector& ref) { MIRO_FIELDS(ref, items); }
    };

    auto value = WithList {};
    value.items.clear();

    check(createFromJSONString<WithList>(toJSONString(value)).items.empty());
    check(createFromXMLString<WithList>(toXMLString(value)).items.empty());
};

} // namespace
