#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace Miro;
using namespace Miro::Json;

namespace
{

bool parseThrows(std::string_view inputToUse)
{
    try
    {
        parse(inputToUse);
    }
    catch (const ParseError&)
    {
        return true;
    }
    catch (...)
    {
        return false;
    }

    return false;
}

const auto grinningFace = std::string("\xF0\x9F\x98\x80");

} // namespace

auto surrogatePairDecodes = test("Surrogate pair decodes to four UTF-8 bytes") = []
{
    auto value = parse(R"("\ud83d\ude00")");
    check(value.isString());
    check(value.asString().size() == 4);
    check(value.asString() == grinningFace);
};

auto surrogatePairInText = test("Surrogate pair embedded in text") = []
{
    auto value = parse(R"("a\ud83d\ude00b")");
    check(value.asString() == "a" + grinningFace + "b");
};

auto surrogatePairUpperCaseHex = test("Surrogate pair with upper-case hex") = []
{
    auto value = parse(R"("\uD83D\uDE00")");
    check(value.asString().size() == 4);
    check(value.asString() == grinningFace);
};

auto surrogatePairMaxCodepoint = test("Surrogate pair at U+10FFFF") = []
{
    auto value = parse(R"("\udbff\udfff")");
    check(value.asString() == std::string("\xF4\x8F\xBF\xBF"));
    check(value.asString().size() == 4);
};

auto highSurrogateThenNonSurrogateThrows =
    test("High surrogate followed by non-surrogate escape throws") = []
{ check(parseThrows(R"("\ud83d\u0041")")); };

auto loneHighSurrogateAtEndThrows =
    test("Lone high surrogate at end of string throws") = []
{ check(parseThrows(R"("\ud83d")")); };

auto loneLowSurrogateThrows =
    test("Lone low surrogate throws") = [] { check(parseThrows(R"("\ude00")")); };

auto highSurrogateThenLiteralThrows =
    test("High surrogate followed by a literal character throws") = []
{
    check(parseThrows(R"("\ud83dx")"));
    check(parseThrows(R"("\ud83dA")"));
};

auto invalidHexDigitThrows = test("Unicode escape with a non-hex digit throws") = []
{ check(parseThrows(R"("\u12G4")")); };

auto twoByteEscapeStillWorks = test("Two-byte unicode escape still decodes") = []
{
    auto value = parse(R"("\u00e9")");
    check(value.asString() == std::string("\xC3\xA9"));
    check(value.asString().size() == 2);
};

auto threeByteEscapeStillWorks = test("Three-byte unicode escape still decodes") = []
{
    auto value = parse(R"("\u4f60")");
    check(value.asString() == std::string("\xE4\xBD\xA0"));
    check(value.asString().size() == 3);
};

auto asciiEscapeStillWorks = test("ASCII unicode escape still decodes") = []
{
    auto value = parse(R"("\u0041")");
    check(value.asString() == "A");
};

auto surrogatePairRoundTrips = test("Surrogate pair round trips through print") = []
{
    auto printed = print(parse(R"("\ud83d\ude00")"));
    check(printed == "\"" + grinningFace + "\"");
};

auto surrogatePairAsObjectKey = test("Surrogate pair as an object key") = []
{
    auto value = parse(R"({"\ud83d\ude00": 1})");
    check(value.isObject());
    check(value.asObject().size() == 1);
    check(value.asObject().begin()->first == grinningFace);
    check(value.asObject().begin()->first.size() == 4);
    check(value[grinningFace].asNumber() == 1.0);
};
