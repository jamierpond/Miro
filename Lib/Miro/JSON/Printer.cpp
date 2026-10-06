#include "Json.h"
#include "Number.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace Miro::Json
{

void printTo(std::string& output, const Value& value, int indent, int depth);

void printString(std::string& output, const std::string& str)
{
    output += '"';

    for (auto c: str)
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
                    auto buf = Miro::Array<char, 8> {};
                    std::snprintf(
                        buf.data(),
                        static_cast<std::size_t>(buf.size()),
                        "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
                    output += buf.data();
                }
                else
                {
                    output += c;
                }
        }
    }

    output += '"';
}

template <typename T>
void printWithToChars(std::string& output, T number)
{
    auto buffer = Miro::Array<char, 48> {};
    auto* first = buffer.data();
    auto [ptr, ec] = std::to_chars(first, first + buffer.size(), number);
    output.append(first, ptr);
}

void printInteger(std::string& output, std::int64_t number)
{
    printWithToChars(output, number);
}

// Every finite number round-trips. Infinity and NaN have no JSON
// spelling at all, so they go out as null.
void printDouble(std::string& output, double number)
{
    if (!std::isfinite(number))
    {
        output += "null";
        return;
    }

    constexpr auto limit = 9223372036854775808.0;

    if (number == std::trunc(number) && number >= -limit && number < limit)
    {
        printInteger(output, static_cast<std::int64_t>(number));
        return;
    }

    Detail::appendShortestDouble(output, number);
}

void writeIndent(std::string& output, int indent, int depth)
{
    output += '\n';
    output.append(static_cast<std::size_t>(indent * depth), ' ');
}

void printArray(std::string& output, const Array& array, int indent, int depth)
{
    output += '[';

    if (array.empty())
    {
        output += ']';
        return;
    }

    auto first = true;

    for (const auto& element: array)
    {
        if (!first)
            output += ',';

        first = false;

        if (indent > 0)
            writeIndent(output, indent, depth + 1);

        printTo(output, element, indent, depth + 1);
    }

    if (indent > 0)
        writeIndent(output, indent, depth);

    output += ']';
}

void printObject(std::string& output, const Object& object, int indent, int depth)
{
    output += '{';

    if (object.empty())
    {
        output += '}';
        return;
    }

    auto first = true;

    for (const auto& [key, value]: object)
    {
        if (!first)
            output += ',';

        first = false;

        if (indent > 0)
            writeIndent(output, indent, depth + 1);

        printString(output, key);
        output += ':';

        if (indent > 0)
            output += ' ';

        printTo(output, value, indent, depth + 1);
    }

    if (indent > 0)
        writeIndent(output, indent, depth);

    output += '}';
}

void printTo(std::string& output, const Value& value, int indent, int depth)
{
    if (value.isNull())
    {
        output += "null";
    }
    else if (value.isBool())
    {
        output += value.asBool() ? "true" : "false";
    }
    else if (value.isInteger())
    {
        printInteger(output, value.asInteger());
    }
    else if (value.isNumber())
    {
        printDouble(output, value.asNumber());
    }
    else if (value.isString())
    {
        printString(output, value.asString());
    }
    else if (value.isArray())
    {
        printArray(output, value.asArray(), indent, depth);
    }
    else if (value.isObject())
    {
        printObject(output, value.asObject(), indent, depth);
    }
}

std::string print(const Value& valueToUse, int indentToUse)
{
    auto result = std::string {};
    printTo(result, valueToUse, indentToUse, 0);
    return result;
}

void log(const Value& valueToUse, int indentToUse)
{
    std::cout << print(valueToUse, indentToUse) << std::endl;
}

} // namespace Miro::Json
