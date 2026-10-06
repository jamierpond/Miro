#include <Miro/Json.h>
#include <NanoTest/NanoTest.h>

#include <clocale>
#include <iostream>
#include <string>
#include <string_view>

using namespace nano;
using namespace Miro::Json;

namespace
{

// Selects a decimal-comma LC_NUMERIC for the scope and puts the previous
// one back on exit. The spelling of such a locale differs per platform,
// so a handful of candidates are tried; `active` says whether one took.
struct DecimalCommaLocale
{
    DecimalCommaLocale()
        : previous(std::setlocale(LC_NUMERIC, nullptr))
    {
        for (auto* candidate: {"de_DE.UTF-8",
                               "de_DE.utf8",
                               "de_DE",
                               "de-DE",
                               "German_Germany.1252",
                               "fr_FR.UTF-8",
                               "fr_FR.utf8",
                               "fr_FR",
                               "fr-FR"})
        {
            if (std::setlocale(LC_NUMERIC, candidate) == nullptr)
                continue;

            if (std::string_view(std::localeconv()->decimal_point) == ",")
            {
                active = true;
                return;
            }
        }

        std::setlocale(LC_NUMERIC, previous.c_str());
    }

    ~DecimalCommaLocale() { std::setlocale(LC_NUMERIC, previous.c_str()); }

    std::string previous;
    bool active = false;
};

// Parses only the first `size` bytes of `backing`, so the view ends
// before the backing storage does.
Value parsePrefix(std::string_view backing, std::size_t size)
{
    return parse(backing.substr(0, size));
}

} // namespace

// --- The parser stays inside the supplied view ---

auto parseBoundedFraction = test("Parse reads only the view, not its backing") = []
{
    check(parsePrefix("1.23", 3).asNumber() == 1.2);
    check(parsePrefix("1e55", 3).asNumber() == 1e5);
    check(parsePrefix("-0.5x", 4).asNumber() == -0.5);
};

auto parseLongToken = test("Parse a number too long for a stack buffer") = []
{
    auto text = "0." + std::string(80, '0') + "1";

    check(parse(text).asNumber() == 1e-81);
    check(parse(text + "e5").asNumber() == 1e-76);
};

auto parseBoundedWideInteger = test("Parse bounded integer above int64") = []
{
    auto value = parsePrefix("184467440737095516159", 20);

    check(!value.isInteger());
    check(value.asNumber() == 18446744073709551615.0);
};

// --- Numbers ignore LC_NUMERIC ---

auto parseUnderDecimalComma =
    test("Parse uses '.' under a decimal-comma locale") = []
{
    auto locale = DecimalCommaLocale {};

    if (!locale.active)
    {
        std::cout << "  (no decimal-comma locale installed, skipped)\n";
        return;
    }

    check(parse("1.5").asNumber() == 1.5);
    check(parse("-2.25e-3").asNumber() == -2.25e-3);
    check(parse("[0.1, 0.2]").asArray().size() == 2);
};

auto printUnderDecimalComma =
    test("Print uses '.' under a decimal-comma locale") = []
{
    auto locale = DecimalCommaLocale {};

    if (!locale.active)
    {
        std::cout << "  (no decimal-comma locale installed, skipped)\n";
        return;
    }

    auto array = Array {};
    array.add(1.5);

    check(print(Value {1.5}) == "1.5");
    check(print(Value {0.1}) == "0.1");
    check(print(Value {array}) == "[1.5]");
    check(parse(print(Value {3.14159})).asNumber() == 3.14159);
};
