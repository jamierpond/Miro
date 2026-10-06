#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

using namespace nano;
using namespace Miro;
using namespace Miro::Json;

namespace
{

enum Legacy
{
    one = 1
};

struct NumberFields
{
    void reflect(Miro::Reflector& ref)
    {
        ref["n"](n);
        ref["ratio"](ratio);
    }

    int n = 5;
    double ratio = 0.25;
};

bool throwsOnAsInteger(const Value& value)
{
    try
    {
        value.asInteger();
    }
    catch (const std::runtime_error&)
    {
        return true;
    }

    return false;
}

} // namespace

// --- Parsing integers exactly ---

auto parseBeyondDoublePrecision = test("Parse integer beyond 2^53") = []
{
    auto value = parse("9007199254740993");

    check(value.isNumber());
    check(value.isInteger());
    check(value.asInteger() == 9007199254740993LL);
    check(print(value) == "9007199254740993");
};

auto parseInt64Bounds = test("Parse int64 bounds exactly") = []
{
    auto lowest = parse("-9223372036854775808");
    auto largest = parse("9223372036854775807");

    check(lowest.asInteger() == std::numeric_limits<std::int64_t>::min());
    check(largest.asInteger() == std::numeric_limits<std::int64_t>::max());
    check(print(lowest) == "-9223372036854775808");
    check(print(largest) == "9223372036854775807");
};

auto parseAboveInt64 = test("Parse integer above int64 falls back to double") = []
{
    auto value = parse("18446744073709551615");

    check(value.isNumber());
    check(!value.isInteger());

    auto expected = 1.8446744073709552e19;
    check(std::abs(value.asNumber() - expected)
          <= std::nextafter(expected, std::numeric_limits<double>::infinity())
                 - expected);
};

auto parseIntegerVersusDouble = test("Parse distinguishes integer spellings") = []
{
    check(parse("42").isInteger());
    check(!parse("42.0").isInteger());
    check(!parse("4e1").isInteger());
    check(!parse("-0.0").isInteger());

    check(parse("42").asNumber() == 42.0);
    check(parse("42.0").asNumber() == 42.0);
    check(parse("4.2e1").asNumber() == 42.0);
    check(parse("4e1").asNumber() == 40.0);

    check(parse("42.0").isNumber());
    check(parse("4e1").isNumber());
    check(parse("-0.0").isNumber());
};

// A digits-only spelling has no fraction and no exponent, so `-0` takes
// the integer path and the sign is lost: it is the integer 0.
auto parseNegativeZero = test("Parse -0 as the integer zero") = []
{
    check(parse("-0").isInteger());
    check(parse("-0").asInteger() == 0);
    check(print(parse("-0")) == "0");
};

// --- Constructing from every integral width ---

auto constructFromIntegers = test("Construct from integral types") = []
{
    check(Value {42}.isInteger());
    check(Value {42LL}.isInteger());
    check(Value {std::int64_t {1} << 62}.isInteger());
    check(Value {std::size_t {42}}.isInteger());
    check(Value {7u}.isInteger());
    check(Value {short {3}}.isInteger());

    check(print(Value {42}) == "42");
    check(print(Value {42LL}) == "42");
    check(print(Value {std::int64_t {1} << 62}) == "4611686018427387904");
    check(print(Value {std::size_t {42}}) == "42");
    check(print(Value {7u}) == "7");
    check(print(Value {short {3}}) == "3");
};

auto constructFromUnsignedAboveInt64 =
    test("Construct from unsigned past int64 stores a double") = []
{
    auto value = Value {std::numeric_limits<std::uint64_t>::max()};

    check(value.isNumber());
    check(!value.isInteger());
    check(value.asNumber() == 1.8446744073709552e19);
};

auto constructFromFloatingPoint = test("Construct from floating point types") = []
{
    check(Value {42.0}.isNumber());
    check(!Value {42.0}.isInteger());
    check(print(Value {42.0}) == "42");

    check(Value {1.5f}.isNumber());
    check(print(Value {1.5f}) == "1.5");
};

auto constructFromBoolStaysBool = test("Construct from bool stays a bool") = []
{
    check(Value {true}.isBool());
    check(!Value {true}.isNumber());
    check(!Value {true}.isInteger());
    check(print(Value {true}) == "true");
};

auto constructFromUnscopedEnum = test("Construct from an unscoped enum") = []
{
    Value value = one;

    check(value.isInteger());
    check(value.asInteger() == 1);
};

// --- Equality across the two storage kinds ---

auto equalityAcrossStorage = test("Equality is numeric across storage kinds") = []
{
    check(Value {1} == Value {1.0});
    check(parse("1") == parse("1.0"));
    check(Value {1} != Value {2});
    check(Value {1} != Value {true});
    check(Value {1} != Value {"1"});
};

auto equalityInsideContainers = test("Equality descends into containers") = []
{
    check(parse(R"([1, {"a": 2}])") == parse(R"([1.0, {"a": 2.0}])"));
    check(parse(R"([1, {"a": 2}])") != parse(R"([1.0, {"a": 3.0}])"));
};

auto equalityKeepsPrecision = test("Equality does not round to double") = []
{
    check(parse("9007199254740993") != Value {9007199254740992.0});
    check(parse("9007199254740992") == Value {9007199254740992.0});
};

// --- asInteger ---

auto asIntegerFromDouble = test("asInteger reads an integral double") = []
{
    check(parse("42.0").asInteger() == 42);
    check(parse("4e1").asInteger() == 40);
    check(parse("-0.0").asInteger() == 0);
};

auto asIntegerRejects = test("asInteger rejects what it cannot represent") = []
{
    check(throwsOnAsInteger(parse("1.5")));
    check(throwsOnAsInteger(parse("1e300")));
    check(throwsOnAsInteger(parse("18446744073709551615")));
    check(throwsOnAsInteger(parse(R"("42")")));
    check(throwsOnAsInteger(parse("true")));
    check(throwsOnAsInteger(parse("null")));
};

// --- Implicit conversions ---

auto implicitConversionsFromInteger =
    test("Implicit conversions from an integer value") = []
{
    int asInt = Value {42};
    double asDouble = Value {42};
    float asFloat = Value {42};

    check(asInt == 42);
    check(asDouble == 42.0);
    check(asFloat == 42.0f);
};

// The conversion operator is declared for std::int64_t exactly, so
// only that spelling resolves — it is `long` on LP64 and `long long`
// on the Apple/Windows models. Every other integer type, `long` and
// `std::size_t` included, stays ambiguous as it already was.
auto implicitConversionToInt64 = test("Implicit conversion to int64") = []
{
    std::int64_t wide = Value {std::int64_t {1} << 62};
    std::int64_t alsoWide = Value {9007199254740993LL};

    check(wide == std::int64_t {1} << 62);
    check(alsoWide == 9007199254740993LL);
};

// --- Printing doubles ---

auto printDoublesRoundTrip = test("Print doubles round-trip") = []
{
    check(print(parse("0.1")) == "0.1");
    check(print(parse("0.1234567891")) == "0.1234567891");
    check(print(parse("3.141592653589793")) == "3.141592653589793");
    check(parse(print(Value {1.0 / 3.0})).asNumber() == 1.0 / 3.0);
};

auto printLargeDoubles = test("Print large doubles") = []
{
    check(parse(print(parse("1e300"))).asNumber() == 1e300);
    check(print(parse("1e15")) == "1000000000000000");
    check(print(parse("1e16")) == "10000000000000000");

    // Past int64 there is no integer spelling left, so the shortest
    // round-trip form is what comes out.
    check(print(Value {1e20}) == "1e+20");
};

// 0.1 + 0.2 is the classic case that needs all 17 digits: "%.16g" spells
// it 0.3, which reads back as a different double.
auto printSeventeenDigitDouble = test("Print a double needing 17 digits") = []
{
    check(print(parse("0.30000000000000004")) == "0.30000000000000004");
    check(parse(print(Value {0.1 + 0.2})).asNumber() == 0.1 + 0.2);
};

auto printSmallAndFractionalDoubles = test("Print small and fractional doubles") = []
{
    check(parse(print(Value {1e-7})).asNumber() == 1e-7);
    check(print(Value {123456.789}) == "123456.789");
};

auto printDoubleExtremes = test("Print the extremes of the double range") = []
{
    auto smallestDenormal = 5e-324;
    auto largestFinite = 1.7976931348623157e308;

    check(parse(print(Value {smallestDenormal})).asNumber() == smallestDenormal);
    check(parse(print(Value {largestFinite})).asNumber() == largestFinite);
};

auto printNonFinite = test("Print non-finite doubles as null") = []
{
    check(print(Value {std::numeric_limits<double>::infinity()}) == "null");
    check(print(Value {-std::numeric_limits<double>::infinity()}) == "null");
    check(print(Value {std::numeric_limits<double>::quiet_NaN()}) == "null");
};

// --- Reflection ---

auto reflectNumberFields = test("Reflection round-trips int and double") = []
{
    auto value = NumberFields {7, 1.5};
    auto json = toJSON(value);

    check(json["n"].isNumber());
    check(json["ratio"].isNumber());

    auto loaded = createFromJSON<NumberFields>(json);

    check(loaded.n == 7);
    check(loaded.ratio == 1.5);
};

auto reflectIntPrintsAsInteger = test("Reflected int prints without a point") = []
{ check(toJSONString(NumberFields {}) == R"({"n":5,"ratio":0.25})"); };
