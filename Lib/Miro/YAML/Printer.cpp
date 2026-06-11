#include "Yaml.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>

namespace Miro::Yaml
{

void printBlockTo(std::string& output, const Value& value, int indent, int depth);
void printFlowTo(std::string& output, const Value& value);

// A string can stay plain (unquoted) only when reading it back yields
// the same string: it must not resolve to null / bool / number, must
// not start with a YAML indicator, and must not contain syntax that
// would terminate or alter the scalar in block or flow context.
bool needsQuoting(const std::string& text)
{
    if (text.empty())
        return true;

    if (!resolvePlainScalar(text).isString())
        return true;

    auto first = text.front();

    if (first == ' ' || first == '-' || first == '?' || first == ':' || first == '#'
        || first == '&' || first == '*' || first == '!' || first == '|'
        || first == '>' || first == '\'' || first == '"' || first == '%'
        || first == '@' || first == '`' || first == ',' || first == '['
        || first == ']' || first == '{' || first == '}')
        return true;

    if (text.back() == ' ')
        return true;

    for (auto i = std::size_t {0}; i < text.size(); ++i)
    {
        auto c = text[i];

        if (static_cast<unsigned char>(c) < 0x20)
            return true;

        if (c == ',' || c == '[' || c == ']' || c == '{' || c == '}')
            return true;

        if (c == ':' && (i + 1 == text.size() || text[i + 1] == ' '))
            return true;

        if (c == '#' && text[i - 1] == ' ')
            return true;
    }

    return false;
}

void printQuoted(std::string& output, const std::string& text)
{
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

void printScalarString(std::string& output, const std::string& text)
{
    if (needsQuoting(text))
        printQuoted(output, text);
    else
        output += text;
}

void printNumber(std::string& output, double number)
{
    auto stream = std::ostringstream {};

    if (std::isfinite(number) && number == std::floor(number)
        && std::abs(number) < 1e15)
        stream << static_cast<long long>(number);
    else
        stream << number;

    output += stream.str();
}

// True for values that render on a single line within block style:
// scalars and empty containers.
bool isInline(const Value& value)
{
    if (value.isArray())
        return value.asArray().empty();

    if (value.isObject())
        return value.asObject().empty();

    return true;
}

void printInlineTo(std::string& output, const Value& value)
{
    if (value.isNull())
        output += "null";
    else if (value.isBool())
        output += value.asBool() ? "true" : "false";
    else if (value.isNumber())
        printNumber(output, value.asNumber());
    else if (value.isString())
        printScalarString(output, value.asString());
    else if (value.isArray())
        output += "[]";
    else if (value.isObject())
        output += "{}";
}

void printBlockMapping(std::string& output,
                       const Object& object,
                       int indent,
                       int depth)
{
    for (const auto& [key, value]: object)
    {
        output.append(static_cast<std::size_t>(indent * depth), ' ');
        printScalarString(output, key);
        output += ':';

        if (isInline(value))
        {
            output += ' ';
            printInlineTo(output, value);
            output += '\n';
        }
        else
        {
            output += '\n';
            printBlockTo(output, value, indent, depth + 1);
        }
    }
}

void printBlockSequence(std::string& output,
                        const Array& array,
                        int indent,
                        int depth)
{
    for (const auto& element: array)
    {
        output.append(static_cast<std::size_t>(indent * depth), ' ');
        output += '-';

        if (isInline(element))
        {
            output += ' ';
            printInlineTo(output, element);
            output += '\n';
        }
        else
        {
            // Compact entry: the nested block's first line continues
            // on the dash line ("- x: 1"), padded so its content
            // lands exactly at the next indent level — the remaining
            // nested lines already align there.
            output.append(static_cast<std::size_t>(indent - 1), ' ');

            auto nested = std::string {};
            printBlockTo(nested, element, indent, depth + 1);
            output += nested.substr(static_cast<std::size_t>(indent)
                                    * static_cast<std::size_t>(depth + 1));
        }
    }
}

void printBlockTo(std::string& output, const Value& value, int indent, int depth)
{
    if (value.isObject())
        printBlockMapping(output, value.asObject(), indent, depth);
    else
        printBlockSequence(output, value.asArray(), indent, depth);
}

void printFlowMapping(std::string& output, const Object& object)
{
    output += '{';
    auto first = true;

    for (const auto& [key, value]: object)
    {
        if (!first)
            output += ", ";

        first = false;
        printScalarString(output, key);
        output += ": ";
        printFlowTo(output, value);
    }

    output += '}';
}

void printFlowSequence(std::string& output, const Array& array)
{
    output += '[';
    auto first = true;

    for (const auto& element: array)
    {
        if (!first)
            output += ", ";

        first = false;
        printFlowTo(output, element);
    }

    output += ']';
}

void printFlowTo(std::string& output, const Value& value)
{
    if (value.isArray() && !value.asArray().empty())
        printFlowSequence(output, value.asArray());
    else if (value.isObject() && !value.asObject().empty())
        printFlowMapping(output, value.asObject());
    else
        printInlineTo(output, value);
}

std::string print(const Value& valueToUse, int indentToUse)
{
    auto result = std::string {};

    if (indentToUse <= 0)
    {
        printFlowTo(result, valueToUse);
        return result;
    }

    if (isInline(valueToUse))
    {
        printInlineTo(result, valueToUse);
        return result;
    }

    auto indent = indentToUse < 2 ? 2 : indentToUse;
    printBlockTo(result, valueToUse, indent, 0);

    if (!result.empty() && result.back() == '\n')
        result.pop_back();

    return result;
}

void log(const Value& valueToUse, int indentToUse)
{
    // Qualified: Value is Json::Value, so an unqualified call would
    // also find Json::print through ADL and be ambiguous.
    std::cout << Miro::Yaml::print(valueToUse, indentToUse) << std::endl;
}

} // namespace Miro::Yaml
