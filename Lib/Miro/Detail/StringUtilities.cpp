#include "StringUtilities.h"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace Miro::Detail
{

bool isAsciiIdentStart(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool isAsciiIdentPart(char c)
{
    return isAsciiIdentStart(c) || (c >= '0' && c <= '9');
}

bool isAsciiIdentifier(std::string_view name)
{
    if (name.empty())
        return false;

    if (!isAsciiIdentStart(name.front()))
        return false;

    for (auto c: name.substr(1))
        if (!isAsciiIdentPart(c))
            return false;

    return true;
}

bool isJsIdentStart(char c)
{
    return isAsciiIdentStart(c) || c == '$';
}

bool isJsIdentPart(char c)
{
    return isAsciiIdentPart(c) || c == '$';
}

bool isJsIdentifier(std::string_view name)
{
    if (name.empty())
        return false;

    if (!isJsIdentStart(name.front()))
        return false;

    for (auto c: name.substr(1))
        if (!isJsIdentPart(c))
            return false;

    return true;
}

Vector<std::string> splitOn(std::string_view input, std::string_view delimiter)
{
    auto out = Vector<std::string> {};

    if (delimiter.empty())
    {
        out.add(std::string {input});
        return out;
    }

    auto start = std::size_t {0};

    while (start <= input.size())
    {
        auto pos = input.find(delimiter, start);
        auto segment = pos == std::string_view::npos
                           ? input.substr(start)
                           : input.substr(start, pos - start);

        out.add(std::string {segment});

        if (pos == std::string_view::npos)
            break;

        start = pos + delimiter.size();
    }

    return out;
}

std::string
    replaceAll(std::string_view input, std::string_view from, std::string_view to)
{
    if (from.empty())
        return std::string {input};

    auto out = std::string {};
    out.reserve(input.size());

    auto pos = std::size_t {0};

    while (pos < input.size())
    {
        auto match = input.find(from, pos);

        if (match == std::string_view::npos)
        {
            out.append(input.substr(pos));
            break;
        }

        out.append(input.substr(pos, match - pos));
        out.append(to);
        pos = match + from.size();
    }

    return out;
}

std::string trimAsciiWhitespace(std::string_view input)
{
    auto isSpace = [](char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    auto first = std::size_t {0};
    while (first < input.size() && isSpace(input[first]))
        ++first;

    auto last = input.size();
    while (last > first && isSpace(input[last - 1]))
        --last;

    return std::string {input.substr(first, last - first)};
}

std::string makeIndent(int width, int depth)
{
    auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(depth);
    // Braced init-list would bind to initializer_list<char>, not the
    // (size_type, char) constructor we want.
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::string(count, ' ');
}

std::optional<std::int64_t> parseWholeInteger(std::string_view text)
{
    auto parsed = std::int64_t {};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    auto result = std::from_chars(begin, end, parsed);

    if (result.ec != std::errc {} || result.ptr != end)
        return std::nullopt;

    return parsed;
}

} // namespace Miro::Detail
