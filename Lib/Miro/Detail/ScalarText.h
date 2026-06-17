#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

// Scalar <-> text primitives shared by the JSON and YAML layers, for
// both the runtime and compile-time backends. Number conversion is
// split on std::is_constant_evaluated(): at runtime it is exactly the
// historical ostringstream / strtod behavior; during constant
// evaluation it uses constexpr emulations of both. The emulations are
// exact for typical values (see the individual notes) and pinned
// against the runtime behavior by the parity tests in
// Tests/ConstexprTests.cpp.

namespace Miro::Detail
{

constexpr void appendInteger(std::string& output, long long valueToUse)
{
    if (valueToUse < 0)
    {
        output += '-';
        valueToUse = -valueToUse;
    }

    char digits[24] {};
    auto count = 0;

    do
    {
        digits[count++] = static_cast<char>('0' + valueToUse % 10);
        valueToUse /= 10;
    } while (valueToUse != 0);

    while (count > 0)
        output += digits[--count];
}

constexpr int hexDigitValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

constexpr void appendUtf8(std::string& result, unsigned codepoint)
{
    if (codepoint <= 0x7F)
    {
        result += static_cast<char>(codepoint);
    }
    else if (codepoint <= 0x7FF)
    {
        result += static_cast<char>(0xC0 | (codepoint >> 6));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else
    {
        result += static_cast<char>(0xE0 | (codepoint >> 12));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

// The shared double-quoted string form: JSON strings and quoted YAML
// scalars use the same escapes.
constexpr void printEscapedString(std::string& output, const std::string& text)
{
    constexpr auto hexDigits = std::string_view {"0123456789abcdef"};

    output += '"';

    for (auto c: text)
    {
        switch (c)
        {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    auto code = static_cast<unsigned>(static_cast<unsigned char>(c));
                    output += "\\u00";
                    output += hexDigits[(code >> 4) & 0xF];
                    output += hexDigits[code & 0xF];
                }
                else
                {
                    output += c;
                }
        }
    }

    output += '"';
}

// --- Number printing ---

// Emulates ostringstream's default double rendering (printf "%g",
// six significant digits). Digit extraction scales through binary
// floating point, so values sitting exactly on a rounding boundary
// can come out one final digit away from printf's result.
constexpr void printGeneralNumber(std::string& output, double valueToUse)
{
    // No sign for negative NaN: libc++'s ostringstream prints "nan"
    // either way (glibc would print "-nan" — NaN sign rendering is
    // platform-defined, so the emulation follows the platform Miro's
    // parity tests pin).
    if (valueToUse != valueToUse)
    {
        output += "nan";
        return;
    }

    if (valueToUse < 0)
    {
        output += '-';
        valueToUse = -valueToUse;
    }

    if (valueToUse == std::numeric_limits<double>::infinity())
    {
        output += "inf";
        return;
    }

    auto exponent = 0;

    while (valueToUse >= 10.0)
    {
        valueToUse /= 10.0;
        ++exponent;
    }

    while (valueToUse < 1.0)
    {
        valueToUse *= 10.0;
        --exponent;
    }

    auto scaled = valueToUse * 100000.0;
    auto digits = static_cast<long long>(scaled);

    if (scaled - static_cast<double>(digits) >= 0.5)
        ++digits;

    if (digits >= 1000000)
    {
        digits /= 10;
        ++exponent;
    }

    char digitChars[6] {};

    for (auto i = 5; i >= 0; --i)
    {
        digitChars[i] = static_cast<char>('0' + digits % 10);
        digits /= 10;
    }

    if (exponent < -4 || exponent >= 6)
    {
        output += digitChars[0];

        auto fracEnd = 6;

        while (fracEnd > 1 && digitChars[fracEnd - 1] == '0')
            --fracEnd;

        if (fracEnd > 1)
        {
            output += '.';

            for (auto i = 1; i < fracEnd; ++i)
                output += digitChars[i];
        }

        output += 'e';
        output += exponent < 0 ? '-' : '+';

        auto magnitude = exponent < 0 ? -exponent : exponent;

        if (magnitude < 10)
            output += '0';

        appendInteger(output, magnitude);
        return;
    }

    if (exponent >= 0)
    {
        for (auto i = 0; i <= exponent; ++i)
            output += digitChars[i];

        auto fracEnd = 6;

        while (fracEnd > exponent + 1 && digitChars[fracEnd - 1] == '0')
            --fracEnd;

        if (fracEnd > exponent + 1)
        {
            output += '.';

            for (auto i = exponent + 1; i < fracEnd; ++i)
                output += digitChars[i];
        }

        return;
    }

    output += "0.";

    for (auto i = 0; i < -exponent - 1; ++i)
        output += '0';

    auto fracEnd = 6;

    while (fracEnd > 1 && digitChars[fracEnd - 1] == '0')
        --fracEnd;

    for (auto i = 0; i < fracEnd; ++i)
        output += digitChars[i];
}

// The constexpr rendering: integral doubles below 1e15 as integers,
// anything else through the "%g" emulation. Exposed separately so the
// parity tests can exercise it at runtime against the real printer.
constexpr void printNumberEmulated(std::string& output, double number)
{
    auto integral = number == number && number < 1e15 && number > -1e15
                    && static_cast<double>(static_cast<long long>(number)) == number;

    if (integral)
        appendInteger(output, static_cast<long long>(number));
    else
        printGeneralNumber(output, number);
}

// Separate non-constexpr function: C++20 forbids a non-literal local
// (the ostringstream) anywhere in a constexpr body, even in a branch
// constant evaluation never takes.
inline void printNumberRuntime(std::string& output, double number)
{
    auto stream = std::ostringstream {};

    if (std::isfinite(number) && number == std::floor(number)
        && std::abs(number) < 1e15)
        stream << static_cast<long long>(number);
    else
        stream << number;

    output += stream.str();
}

// The number rendering used by both document printers: the historical
// ostringstream formatting at runtime, the emulation at compile time.
constexpr void printNumber(std::string& output, double number)
{
    if (std::is_constant_evaluated())
    {
        printNumberEmulated(output, number);
        return;
    }

    printNumberRuntime(output, number);
}

// --- Number parsing ---

constexpr char toLowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool isDecimalDigit(char c)
{
    return c >= '0' && c <= '9';
}

constexpr bool matchWordCaseInsensitive(std::string_view text,
                                        std::size_t& pos,
                                        std::string_view word)
{
    if (text.size() - pos < word.size())
        return false;

    for (auto i = std::size_t {0}; i < word.size(); ++i)
        if (toLowerAscii(text[pos + i]) != word[i])
            return false;

    pos += word.size();
    return true;
}

constexpr void accumulateDecimalDigit(std::uint64_t& mantissa,
                                      int& scale,
                                      char digitToUse,
                                      bool isFraction)
{
    constexpr auto limit = (std::numeric_limits<std::uint64_t>::max() - 9) / 10;

    if (mantissa <= limit)
    {
        mantissa = mantissa * 10 + static_cast<std::uint64_t>(digitToUse - '0');

        if (isFraction)
            --scale;
    }
    else if (!isFraction)
    {
        ++scale;
    }
}

// Clamped power-of-ten scaling. When the decimal mantissa fits
// exactly (<= 2^53) and the net exponent is within ±22 this is a
// single correctly-rounded multiply or divide — the strtod fast path
// — so typical values convert identically to strtod. Larger
// magnitudes scale in chunks and may drift by an ulp.
constexpr double scalePowerOfTen(double valueToUse, int exponentToUse)
{
    constexpr double powers[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                 1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

    constexpr auto maxValue = std::numeric_limits<double>::max();
    constexpr auto infinity = std::numeric_limits<double>::infinity();

    if (valueToUse == 0.0)
        return valueToUse;

    while (exponentToUse > 22)
    {
        if (valueToUse >= maxValue / 1e22)
            return infinity;

        valueToUse *= 1e22;
        exponentToUse -= 22;
    }

    while (exponentToUse < -22)
    {
        valueToUse /= 1e22;
        exponentToUse += 22;
    }

    if (exponentToUse > 0)
    {
        auto factor = powers[exponentToUse];

        if (valueToUse >= maxValue / factor)
            return infinity;

        return valueToUse * factor;
    }

    if (exponentToUse < 0)
        return valueToUse / powers[-exponentToUse];

    return valueToUse;
}

constexpr double scalePowerOfTwo(double valueToUse, int exponentToUse)
{
    constexpr auto maxValue = std::numeric_limits<double>::max();

    while (exponentToUse > 0)
    {
        if (valueToUse > maxValue / 2.0)
            return std::numeric_limits<double>::infinity();

        valueToUse *= 2.0;
        --exponentToUse;
    }

    while (exponentToUse < 0)
    {
        valueToUse /= 2.0;
        ++exponentToUse;
    }

    return valueToUse;
}

// Consumes an optional e/E (or p/P) exponent suffix, returning its
// signed value. The suffix only counts when at least one digit
// follows — otherwise pos is left on the marker, matching strtod.
constexpr int
    parseExponentSuffix(std::string_view token, std::size_t& pos, char lowerMarker)
{
    if (pos >= token.size() || toLowerAscii(token[pos]) != lowerMarker)
        return 0;

    auto probe = pos + 1;
    auto negative = false;

    if (probe < token.size() && (token[probe] == '+' || token[probe] == '-'))
    {
        negative = token[probe] == '-';
        ++probe;
    }

    if (probe >= token.size() || !isDecimalDigit(token[probe]))
        return 0;

    auto exponent = 0;

    while (probe < token.size() && isDecimalDigit(token[probe]))
    {
        if (exponent < 100000)
            exponent = exponent * 10 + (token[probe] - '0');

        ++probe;
    }

    pos = probe;
    return negative ? -exponent : exponent;
}

constexpr bool
    parseDecimalFloat(std::string_view token, std::size_t& pos, double& result)
{
    auto start = pos;
    auto mantissa = std::uint64_t {0};
    auto scale = 0;
    auto digitCount = 0;

    while (pos < token.size() && isDecimalDigit(token[pos]))
    {
        accumulateDecimalDigit(mantissa, scale, token[pos], false);
        ++digitCount;
        ++pos;
    }

    if (pos < token.size() && token[pos] == '.')
    {
        ++pos;

        while (pos < token.size() && isDecimalDigit(token[pos]))
        {
            accumulateDecimalDigit(mantissa, scale, token[pos], true);
            ++digitCount;
            ++pos;
        }
    }

    if (digitCount == 0)
    {
        pos = start;
        return false;
    }

    scale += parseExponentSuffix(token, pos, 'e');
    result = scalePowerOfTen(static_cast<double>(mantissa), scale);
    return true;
}

constexpr bool isHexFloatStart(std::string_view token, std::size_t pos)
{
    if (token.size() - pos < 3 || token[pos] != '0'
        || toLowerAscii(token[pos + 1]) != 'x')
        return false;

    if (hexDigitValue(token[pos + 2]) >= 0)
        return true;

    return token[pos + 2] == '.' && token.size() - pos >= 4
           && hexDigitValue(token[pos + 3]) >= 0;
}

constexpr double parseHexFloat(std::string_view token, std::size_t& pos)
{
    constexpr auto limit = (std::numeric_limits<std::uint64_t>::max() - 15) / 16;

    pos += 2;
    auto mantissa = std::uint64_t {0};
    auto binaryExponent = 0;

    while (pos < token.size() && hexDigitValue(token[pos]) >= 0)
    {
        if (mantissa <= limit)
            mantissa = mantissa * 16
                       + static_cast<std::uint64_t>(hexDigitValue(token[pos]));
        else
            binaryExponent += 4;

        ++pos;
    }

    if (pos < token.size() && token[pos] == '.')
    {
        ++pos;

        while (pos < token.size() && hexDigitValue(token[pos]) >= 0)
        {
            if (mantissa <= limit)
            {
                mantissa = mantissa * 16
                           + static_cast<std::uint64_t>(hexDigitValue(token[pos]));
                binaryExponent -= 4;
            }

            ++pos;
        }
    }

    binaryExponent += parseExponentSuffix(token, pos, 'p');
    return scalePowerOfTwo(static_cast<double>(mantissa), binaryExponent);
}

// Mirrors how many characters strtod would consume from `token` and
// the value it would produce; 0 when no conversion is possible.
// Recognizes decimal and hex floating forms plus signed
// inf / infinity / nan (the nan(payload) form is not recognized, and
// hex mantissas beyond 16 digits truncate rather than round — both
// vanishingly rare in real documents).
constexpr std::size_t strtodConsumed(std::string_view token, double& result)
{
    auto pos = std::size_t {0};
    auto negative = false;

    if (pos < token.size() && (token[pos] == '+' || token[pos] == '-'))
    {
        negative = token[pos] == '-';
        ++pos;
    }

    auto magnitude = 0.0;

    if (matchWordCaseInsensitive(token, pos, "infinity")
        || matchWordCaseInsensitive(token, pos, "inf"))
    {
        magnitude = std::numeric_limits<double>::infinity();
    }
    else if (matchWordCaseInsensitive(token, pos, "nan"))
    {
        magnitude = std::numeric_limits<double>::quiet_NaN();
    }
    else if (isHexFloatStart(token, pos))
    {
        magnitude = parseHexFloat(token, pos);
    }
    else if (!parseDecimalFloat(token, pos, magnitude))
    {
        return 0;
    }

    result = negative ? -magnitude : magnitude;
    return pos;
}

// The number conversion used by both document parsers: real strtod at
// runtime, the emulation at compile time. `consumed` reports how many
// characters converted so callers can keep their full-consumption
// checks.
constexpr double convertNumberText(std::string_view text, std::size_t& consumed)
{
    if (std::is_constant_evaluated())
    {
        auto value = 0.0;
        consumed = strtodConsumed(text, value);
        return value;
    }

    auto buffer = std::string(text);
    char* numEnd = nullptr;
    auto value = std::strtod(buffer.c_str(), &numEnd);
    consumed = static_cast<std::size_t>(numEnd - buffer.c_str());
    return value;
}

} // namespace Miro::Detail
