#include "Number.h"

#include "../Containers.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>

namespace Miro::Json::Detail
{

namespace
{

// std::to_chars / std::from_chars would do all of this in one call each,
// but libc++ marks their floating-point overloads unavailable before
// macOS 13.3, and an availability attribute is invisible to a
// feature-test macro. So the C library does the arithmetic, and the
// decimal point is translated on the way in and out.

std::optional<double> parseTerminated(const char* text, std::size_t size)
{
    char* end = nullptr;
    auto value = std::strtod(text, &end);

    if (end != text + size)
        return std::nullopt;

    return value;
}

// `text` with its '.' spelled the way the current LC_NUMERIC expects.
std::string localized(std::string_view text, std::string_view point)
{
    auto copy = std::string {};
    copy.reserve(text.size() + point.size());

    for (auto c: text)
    {
        if (c == '.')
            copy += point;
        else
            copy += c;
    }

    return copy;
}

bool isNumberChar(char c)
{
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e' || c == 'E';
}

// Appends a printf "%g" result as JSON. Whatever the locale spells the
// decimal point as, it is the one run of characters a "%g" result can
// contain that a JSON number cannot, so that run becomes '.'.
void appendNormalized(std::string& output, std::string_view text)
{
    auto begin = std::size_t {0};

    while (begin < text.size() && isNumberChar(text[begin]))
        ++begin;

    if (begin == text.size())
    {
        output += text;
        return;
    }

    auto end = begin;

    while (end < text.size() && !isNumberChar(text[end]))
        ++end;

    output += text.substr(0, begin);
    output += '.';
    output += text.substr(end);
}

// `terminated` is `text` with a NUL after it.
std::optional<double> parseSpelled(const char* terminated, std::string_view text)
{
    // First as spelled: in a '.' locale, which is nearly every process,
    // this is the whole job.
    if (auto value = parseTerminated(terminated, text.size()))
        return value;

    // Otherwise the locale may spell the decimal point differently, in
    // which case strtod stopped at the '.'.
    auto point = std::string_view(std::localeconv()->decimal_point);

    if (point == "." || text.find('.') == std::string_view::npos)
        return std::nullopt;

    auto copy = localized(text, point);
    return parseTerminated(copy.c_str(), copy.size());
}

} // namespace

// strtod has no length argument and runs on until the text stops
// looking like a number, so a view into longer storage is cut off by a
// NUL-terminated copy: on the stack when it fits, which is every number
// a sane document holds.
std::optional<double> parseDouble(std::string_view text)
{
    auto buffer = Miro::Array<char, 64> {};

    if (text.size() < static_cast<std::size_t>(buffer.size()))
    {
        text.copy(buffer.data(), text.size());
        buffer[static_cast<int>(text.size())] = '\0';
        return parseSpelled(buffer.data(), text);
    }

    auto copy = std::string(text);
    return parseSpelled(copy.c_str(), text);
}

// The shortest spelling that reads back as the same double, found by
// trying the three precisions that can produce one. 17 significant
// digits always round-trips, so the last attempt needs no check.
void appendShortestDouble(std::string& output, double number)
{
    auto buffer = Miro::Array<char, 64> {};
    auto size = static_cast<std::size_t>(buffer.size());
    auto mark = output.size();

    for (auto precision = 15; precision <= 17; ++precision)
    {
        std::snprintf(buffer.data(), size, "%.*g", precision, number);
        appendNormalized(output, buffer.data());

        auto written = std::string_view(output).substr(mark);

        if (precision == 17 || parseDouble(written) == number)
            return;

        output.resize(mark);
    }
}

} // namespace Miro::Json::Detail
