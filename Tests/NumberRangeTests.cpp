// Loading a number into a field whose type no longer holds it (int64 into
// int32, 1e300 into int, -1 into unsigned) refuses it and keeps the field's
// prior value, instead of casting it to garbage.

#include <Miro/Reflect.h>

#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <limits>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

template <typename T>
struct SingleFieldHolder
{
    T value {};
    int after = 0;

    MIRO_REFLECT(value, after)
};

// Writes SingleFieldHolder<From>{from}, reads SingleFieldHolder<To> starting from `prior`.
template <typename To, typename From, typename Expect>
void checkFieldTypeChange(From from, To prior, Expect expect)
{
    auto written = SingleFieldHolder<From> {from, 77};

    auto loaded = SingleFieldHolder<To> {prior, 0};
    fromJSONString(loaded, toJSONString(written));
    expect(loaded.value);
    check(loaded.after == 77);
}

auto widenUnsigned = test("Number range: uint32 -> uint64 widens") = []
{
    checkFieldTypeChange<std::uint64_t>(std::uint32_t {4000000000u},
                                        std::uint64_t {1},
                                        [](std::uint64_t v)
                                        { check(v == 4000000000u); });
};

auto narrowUnsignedFits =
    test("Number range: uint64 -> uint32 converts when it fits") = []
{
    checkFieldTypeChange<std::uint32_t>(std::uint64_t {123},
                                        std::uint32_t {1},
                                        [](std::uint32_t v) { check(v == 123); });
};

auto narrowUnsignedOverflow =
    test("Number range: uint64 -> uint32 out of range keeps prior") = []
{
    checkFieldTypeChange<std::uint32_t>(std::uint64_t {1} << 40,
                                        std::uint32_t {1},
                                        [](std::uint32_t v) { check(v == 1); });
};

auto narrowSignedOverflow =
    test("Number range: int64 -> int32 out of range keeps prior") = []
{
    checkFieldTypeChange<std::int32_t>(std::int64_t {-5000000000LL},
                                       std::int32_t {1},
                                       [](std::int32_t v) { check(v == 1); });
};

auto signedToUnsigned =
    test("Number range: negative int -> unsigned keeps prior") = []
{
    checkFieldTypeChange<std::uint32_t>(
        -1, std::uint32_t {1}, [](std::uint32_t v) { check(v == 1); });
    checkFieldTypeChange<std::uint16_t>(
        -1, std::uint16_t {1}, [](std::uint16_t v) { check(v == 1); });
};

auto uint64FullRange =
    test("Number range: uint64 past INT64_MAX still round trips") = []
{
    auto big = std::numeric_limits<std::uint64_t>::max() - 5;
    checkFieldTypeChange<std::uint64_t>(
        big, std::uint64_t {1}, [big](std::uint64_t v) { check(v == big); });
};

auto intToDouble = test("Number range: int -> double converts") = []
{ checkFieldTypeChange<double>(-7, 0.5, [](double v) { check(v == -7.0); }); };

auto int64ToDouble = test("Number range: int64 -> double converts") = []
{
    checkFieldTypeChange<double>(std::int64_t {1} << 52,
                                 0.5,
                                 [](double v) { check(v == 4503599627370496.0); });
};

auto doubleToFloat = test("Number range: double -> float narrows to nearest") = []
{ checkFieldTypeChange<float>(0.1, 1.0f, [](float v) { check(v == 0.1f); }); };

auto doubleToFloatOverflow =
    test("Number range: double -> float out of range keeps prior") = []
{ checkFieldTypeChange<float>(1e300, 1.0f, [](float v) { check(v == 1.0f); }); };

auto doubleToInt = test("Number range: double -> int truncates toward zero") = []
{
    checkFieldTypeChange<int>(3.9, 1, [](int v) { check(v == 3); });
    checkFieldTypeChange<int>(-3.9, 1, [](int v) { check(v == -3); });
};

auto doubleToIntOverflow =
    test("Number range: double -> int out of range keeps prior") = []
{
    checkFieldTypeChange<int>(1e300, 1, [](int v) { check(v == 1); });
    checkFieldTypeChange<int>(-1e300, 1, [](int v) { check(v == 1); });
    checkFieldTypeChange<std::int64_t>(
        1e19, std::int64_t {1}, [](std::int64_t v) { check(v == 1); });
    checkFieldTypeChange<std::uint8_t>(
        300.0, std::uint8_t {1}, [](std::uint8_t v) { check(v == 1); });
};

auto vectorElementChange =
    test("Number range: vector<double> -> vector<int> converts per element") = []
{
    // Out-of-range elements keep their prior (zero) value.
    checkFieldTypeChange<std::vector<int>>(
        std::vector<double> {1.5, -2.5, 1e300, 7.0},
        std::vector<int> {},
        [](const std::vector<int>& v)
        { check(v == std::vector<int> {1, -2, 0, 7}); });
};

constexpr auto wrapsToFiveInInt32 = (std::int64_t {1} << 40) + 5;

auto vectorElementNarrowing = test(
    "Number range: vector<int64> -> vector<int32> refuses overflow per element") = []
{
    checkFieldTypeChange<std::vector<std::int32_t>>(
        std::vector<std::int64_t> {1, wrapsToFiveInInt32, -3},
        std::vector<std::int32_t> {},
        [](const std::vector<std::int32_t>& v)
        { check(v == std::vector<std::int32_t> {1, 0, -3}); });
};

} // namespace
