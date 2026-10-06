// Reflection of the exact-integer JSON alternative. The Json layer keeps
// an int64 that no double can name; these pin that the reflection layer
// does too — for a typed field, a raw JSON field, and the container
// forms — while doubles and integral doubles keep behaving as they did.

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

// 2^53 + 1: the smallest positive integer a double cannot represent.
constexpr auto beyondDouble = std::int64_t {9007199254740993};

struct WideField
{
    MIRO_REFLECT(offset)

    std::int64_t offset = beyondDouble;
};

struct MixedNumbers
{
    MIRO_REFLECT(n, ratio)

    int n = 5;
    double ratio = 2.5;
};

struct RawField
{
    MIRO_REFLECT(d)

    JSON d;
};

struct AnyField
{
    MIRO_REFLECT(d)

    Json::Any d;
};

struct OptionalWide
{
    MIRO_REFLECT(offset)

    std::optional<std::int64_t> offset;
};

struct WideList
{
    MIRO_REFLECT(offsets)

    std::vector<std::int64_t> offsets;
};

std::string wideFieldText(std::int64_t offset)
{
    return toJSONString(WideField {offset});
}

std::int64_t loadWideField(const std::string& text)
{
    return createFromJSONString<WideField>(text).offset;
}

// What a raw field carries out after carrying `text` in.
template <typename Holder>
std::string rawRoundTrip(const std::string& text)
{
    auto document = std::string {R"({"d":)"} + text + "}";
    return toJSONString(createFromJSONString<Holder>(document));
}

} // namespace

// --- A typed int64 field ---

auto reflectWideInteger = test("Reflected int64 keeps 2^53 + 1") = []
{
    check(wideFieldText(beyondDouble) == R"({"offset":9007199254740993})");
    check(loadWideField(wideFieldText(beyondDouble)) == beyondDouble);
};

auto reflectInt64Limits = test("Reflected int64 keeps both limits") = []
{
    constexpr auto lowest = std::numeric_limits<std::int64_t>::min();
    constexpr auto highest = std::numeric_limits<std::int64_t>::max();

    check(wideFieldText(lowest) == R"({"offset":-9223372036854775808})");
    check(wideFieldText(highest) == R"({"offset":9223372036854775807})");

    check(loadWideField(wideFieldText(lowest)) == lowest);
    check(loadWideField(wideFieldText(highest)) == highest);
};

auto reflectWideIntegerValue = test("Reflected int64 is an integer value") = []
{
    auto json = toJSON(WideField {beyondDouble});

    check(json["offset"].isInteger());
    check(json["offset"].asInteger() == beyondDouble);
};

// --- int and double are unchanged ---

auto reflectIntAndDouble = test("Reflected int and double print as before") = []
{ check(toJSONString(MixedNumbers {}) == R"({"n":5,"ratio":2.5})"); };

auto loadAcrossNumberKinds = test("Number kinds load into either target") = []
{
    auto value = createFromJSONString<MixedNumbers>(R"({"n":5.0,"ratio":5})");

    check(value.n == 5);
    check(value.ratio == 5.0);
};

// --- A raw JSON field ---

auto rawFieldKeepsWideInteger = test("Raw field keeps 2^53 + 1") = []
{
    check(rawRoundTrip<RawField>("9007199254740993") == R"({"d":9007199254740993})");
};

auto anyFieldKeepsWideInteger = test("Json::Any field keeps 2^53 + 1") = []
{
    check(rawRoundTrip<AnyField>("9007199254740993") == R"({"d":9007199254740993})");
};

auto rawFieldKeepsDoubles = test("Raw field keeps doubles") = []
{
    check(rawRoundTrip<RawField>("2.5") == R"({"d":2.5})");

    // 5.0 is stored as a double and printed by the shortest integral
    // spelling, the same as it is straight through the JSON layer.
    check(rawRoundTrip<RawField>("5.0") == R"({"d":5})");
    check(createFromJSONString<RawField>(R"({"d":2.5})").d.asNumber() == 2.5);
};

// --- The same numbers through XML ---

// XmlReflector never went through double — it spells primitives out as
// text — so a typed field was always exact. The raw walk is shared
// though, and an integer inside one used to reach XML as a double and
// print as 9.0072e+15.

auto wideIntegerThroughXml = test("Reflected int64 survives XML") = []
{
    auto xml = toXMLString(WideField {beyondDouble});

    check(xml == R"(<WideField offset="9007199254740993"/>)");
    check(createFromXMLString<WideField>(xml).offset == beyondDouble);
};

auto rawWideIntegerThroughXml = test("Raw field keeps 2^53 + 1 in XML") = []
{
    auto holder = createFromJSONString<RawField>(R"({"d":9007199254740993})");

    check(toXMLString(holder) == "<RawField><d>9007199254740993</d></RawField>");
};

// --- Optional and container forms ---

auto reflectOptionalWideInteger = test("optional<int64> keeps 2^53 + 1") = []
{
    auto text = toJSONString(OptionalWide {beyondDouble});

    check(text == R"({"offset":9007199254740993})");
    check(createFromJSONString<OptionalWide>(text).offset == beyondDouble);
};

auto reflectWideIntegerVector = test("vector<int64> keeps 2^53 + 1") = []
{
    auto text = toJSONString(WideList {{beyondDouble, -beyondDouble}});
    auto loaded = createFromJSONString<WideList>(text);

    check(text == R"({"offsets":[9007199254740993,-9007199254740993]})");
    check(loaded.offsets.size() == 2);
    check(loaded.offsets[0] == beyondDouble);
    check(loaded.offsets[1] == -beyondDouble);
};
