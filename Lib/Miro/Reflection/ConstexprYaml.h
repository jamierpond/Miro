#pragma once

#include "../YAML/Yaml.h"
#include "ConstexprJson.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Compile-time YAML serde backend.
//
// YAML shares the JSON value model, so this layer reuses the
// ConstexprJson mirror (Value tree and reflector) and only ports the
// YAML-specific string layer: plain-scalar resolution, the printer
// and the line-oriented parser, each mirroring its runtime sibling in
// Lib/Miro/YAML so compile-time output is byte-identical.
//
// Plain-scalar number recognition mirrors strtod: decimal and hex
// floating forms plus signed inf / infinity / nan (the nan(payload)
// form is not recognized, and hex mantissas beyond 16 digits truncate
// rather than round — both vanishingly rare in real documents).

namespace Miro::Detail::ConstexprYaml
{

using ConstexprJson::Value;

// --- Plain-scalar resolution (mirrors Yaml::resolvePlainScalar) ---

constexpr char toLowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
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

constexpr bool isDecimalDigit(char c)
{
    return c >= '0' && c <= '9';
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
    result = ConstexprJson::scalePowerOfTen(static_cast<double>(mantissa), scale);
    return true;
}

constexpr bool isHexFloatStart(std::string_view token, std::size_t pos)
{
    if (token.size() - pos < 3 || token[pos] != '0'
        || toLowerAscii(token[pos + 1]) != 'x')
        return false;

    if (ConstexprJson::hexDigitValue(token[pos + 2]) >= 0)
        return true;

    return token[pos + 2] == '.' && token.size() - pos >= 4
           && ConstexprJson::hexDigitValue(token[pos + 3]) >= 0;
}

constexpr double parseHexFloat(std::string_view token, std::size_t& pos)
{
    constexpr auto limit = (std::numeric_limits<std::uint64_t>::max() - 15) / 16;

    pos += 2;
    auto mantissa = std::uint64_t {0};
    auto binaryExponent = 0;

    while (pos < token.size() && ConstexprJson::hexDigitValue(token[pos]) >= 0)
    {
        if (mantissa <= limit)
            mantissa = mantissa * 16
                       + static_cast<std::uint64_t>(
                           ConstexprJson::hexDigitValue(token[pos]));
        else
            binaryExponent += 4;

        ++pos;
    }

    if (pos < token.size() && token[pos] == '.')
    {
        ++pos;

        while (pos < token.size() && ConstexprJson::hexDigitValue(token[pos]) >= 0)
        {
            if (mantissa <= limit)
            {
                mantissa = mantissa * 16
                           + static_cast<std::uint64_t>(
                               ConstexprJson::hexDigitValue(token[pos]));
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

constexpr Value makeString(std::string textToUse)
{
    auto result = Value {};
    result.kind = Value::Kind::String;
    result.stringValue = std::move(textToUse);
    return result;
}

constexpr Value makeNumber(double numberToUse)
{
    auto result = Value {};
    result.kind = Value::Kind::Number;
    result.numberValue = numberToUse;
    return result;
}

constexpr Value makeBool(bool valueToUse)
{
    auto result = Value {};
    result.kind = Value::Kind::Bool;
    result.boolValue = valueToUse;
    return result;
}

constexpr Value resolvePlainScalar(std::string_view tokenToUse)
{
    if (tokenToUse.empty() || tokenToUse == "~" || tokenToUse == "null"
        || tokenToUse == "Null" || tokenToUse == "NULL")
        return {};

    if (tokenToUse == "true" || tokenToUse == "True" || tokenToUse == "TRUE")
        return makeBool(true);

    if (tokenToUse == "false" || tokenToUse == "False" || tokenToUse == "FALSE")
        return makeBool(false);

    auto first = tokenToUse.front();

    if (first == '-' || first == '+' || first == '.'
        || (first >= '0' && first <= '9'))
    {
        auto number = 0.0;

        if (strtodConsumed(tokenToUse, number) == tokenToUse.size())
            return makeNumber(number);
    }

    return makeString(std::string(tokenToUse));
}

// --- Printer (mirrors YAML/Printer.cpp byte for byte) ---

constexpr void
    printBlockTo(std::string& output, const Value& value, int indent, int depth);
constexpr void printFlowTo(std::string& output, const Value& value);

constexpr bool needsQuoting(const std::string& text)
{
    if (text.empty())
        return true;

    if (resolvePlainScalar(text).kind != Value::Kind::String)
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

constexpr void printQuoted(std::string& output, const std::string& text)
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

constexpr void printScalarString(std::string& output, const std::string& text)
{
    if (needsQuoting(text))
        printQuoted(output, text);
    else
        output += text;
}

// True for values that render on a single line within block style:
// scalars and empty containers.
constexpr bool isInline(const Value& value)
{
    if (value.isArray())
        return value.arrayValue.empty();

    if (value.isObject())
        return value.objectValue.empty();

    return true;
}

constexpr void printInlineTo(std::string& output, const Value& value)
{
    switch (value.kind)
    {
        case Value::Kind::Null:
            output += "null";
            break;
        case Value::Kind::Bool:
            output += value.boolValue ? "true" : "false";
            break;
        case Value::Kind::Number:
            ConstexprJson::printNumber(output, value.numberValue);
            break;
        case Value::Kind::String:
            printScalarString(output, value.stringValue);
            break;
        case Value::Kind::Array:
            output += "[]";
            break;
        case Value::Kind::Object:
            output += "{}";
            break;
    }
}

constexpr void printBlockMapping(std::string& output,
                                 const std::vector<ConstexprJson::Member>& object,
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

constexpr void printBlockSequence(std::string& output,
                                  const std::vector<Value>& array,
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

constexpr void
    printBlockTo(std::string& output, const Value& value, int indent, int depth)
{
    if (value.isObject())
        printBlockMapping(output, value.objectValue, indent, depth);
    else
        printBlockSequence(output, value.arrayValue, indent, depth);
}

constexpr void printFlowMapping(std::string& output,
                                const std::vector<ConstexprJson::Member>& object)
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

constexpr void printFlowSequence(std::string& output,
                                 const std::vector<Value>& array)
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

constexpr void printFlowTo(std::string& output, const Value& value)
{
    if (value.isArray() && !value.arrayValue.empty())
        printFlowSequence(output, value.arrayValue);
    else if (value.isObject() && !value.objectValue.empty())
        printFlowMapping(output, value.objectValue);
    else
        printInlineTo(output, value);
}

constexpr std::string print(const Value& valueToUse, int indentToUse)
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

// --- Parser (mirrors YAML/Parser.cpp) ---

class Parser
{
public:
    constexpr explicit Parser(std::string_view inputToUse)
    {
        splitLines(inputToUse);
    }

    constexpr Value parseDocument()
    {
        if (cur < lines.size() && lines[cur].content == "---")
            ++cur;

        if (cur >= lines.size())
            return {};

        auto result = parseNode();

        if (cur < lines.size())
            error("unexpected trailing content");

        return result;
    }

private:
    struct Line
    {
        int indent = 0;
        std::string_view content;
        std::size_t number = 0;
    };

    static constexpr auto npos = std::string_view::npos;

    // --- Block structure ---

    constexpr Value parseNode()
    {
        const auto& line = lines[cur];

        if (isDashLine(line.content))
            return parseSequence(line.indent);

        if (mappingColonPos(line.content) != npos)
            return parseMapping(line.indent);

        auto value = parseFlowLine(line.content);
        ++cur;
        return value;
    }

    constexpr Value parseMapping(int indent)
    {
        auto result = Value {};
        result.kind = Value::Kind::Object;

        while (cur < lines.size() && lines[cur].indent == indent
               && !isDashLine(lines[cur].content))
        {
            auto content = lines[cur].content;
            auto colon = mappingColonPos(content);

            if (colon == npos)
                error("expected 'key: value'");

            auto key = parseKey(content.substr(0, colon));
            auto rest = trim(content.substr(colon + 1));

            if (!rest.empty())
            {
                auto value = parseFlowLine(rest);
                ++cur;
                ConstexprJson::insertIfAbsent(
                    result.objectValue, std::move(key), std::move(value));
            }
            else
            {
                ++cur;
                ConstexprJson::insertIfAbsent(
                    result.objectValue, std::move(key), parseNestedValue(indent));
            }
        }

        return result;
    }

    // Value of a "key:" line with nothing after the colon: a nested
    // block at deeper indent, a sequence at the same indent (YAML
    // allows the dashes to align with the parent key), or null.
    constexpr Value parseNestedValue(int indent)
    {
        if (cur >= lines.size())
            return {};

        if (lines[cur].indent > indent)
            return parseNode();

        if (lines[cur].indent == indent && isDashLine(lines[cur].content))
            return parseSequence(indent);

        return {};
    }

    constexpr Value parseSequence(int indent)
    {
        auto result = Value {};
        result.kind = Value::Kind::Array;

        while (cur < lines.size() && lines[cur].indent == indent
               && isDashLine(lines[cur].content))
        {
            result.arrayValue.push_back(parseSequenceElement(indent));
        }

        return result;
    }

    constexpr Value parseSequenceElement(int indent)
    {
        auto content = lines[cur].content;

        if (content == "-")
        {
            ++cur;

            if (cur < lines.size() && lines[cur].indent > indent)
                return parseNode();

            return {};
        }

        // "- rest": re-enter at the rest's column, so compact nested
        // collections ("- x: 1" continued by aligned "y: 2" lines, or
        // "- - a" nested sequences) parse as if rest were its own line.
        auto restOffset = std::size_t {1};

        while (restOffset < content.size() && content[restOffset] == ' ')
            ++restOffset;

        lines[cur].indent += static_cast<int>(restOffset);
        lines[cur].content = content.substr(restOffset);
        return parseNode();
    }

    static constexpr bool isDashLine(std::string_view content)
    {
        return content == "-" || content.substr(0, 2) == "- ";
    }

    // Position of the colon that splits "key: value" (or "key:") at
    // the top level of a line — outside quotes and flow brackets,
    // followed by a space or end of line. npos when the line is not
    // a mapping entry.
    constexpr std::size_t mappingColonPos(std::string_view content) const
    {
        auto depth = 0;

        for (auto i = std::size_t {0}; i < content.size(); ++i)
        {
            auto c = content[i];

            if (c == '"' || c == '\'')
            {
                auto after = skipQuoted(content, i);

                if (after == npos)
                    return npos;

                i = after - 1;
            }
            else if (c == '[' || c == '{')
            {
                ++depth;
            }
            else if (c == ']' || c == '}')
            {
                --depth;
            }
            else if (c == ':' && depth == 0
                     && (i + 1 == content.size() || content[i + 1] == ' '))
            {
                return i;
            }
        }

        return npos;
    }

    constexpr std::string parseKey(std::string_view keyPart)
    {
        auto trimmed = trim(keyPart);

        if (trimmed.empty())
            error("empty mapping key");

        if (trimmed.front() == '"' || trimmed.front() == '\'')
        {
            auto pos = std::size_t {0};
            auto key = trimmed.front() == '"' ? parseDoubleQuoted(trimmed, pos)
                                              : parseSingleQuoted(trimmed, pos);

            if (pos != trimmed.size())
                error("unexpected characters after quoted key");

            return key;
        }

        return std::string(trimmed);
    }

    // --- Single-line flow values ---

    constexpr Value parseFlowLine(std::string_view text)
    {
        auto pos = std::size_t {0};
        auto value = parseFlowValue(text, pos, false);
        skipSpaces(text, pos);

        if (pos != text.size())
            error("unexpected trailing content after value");

        return value;
    }

    constexpr Value
        parseFlowValue(std::string_view text, std::size_t& pos, bool inFlow)
    {
        skipSpaces(text, pos);

        if (pos >= text.size())
            return {};

        auto c = text[pos];

        if (c == '[')
            return parseFlowArray(text, pos);
        if (c == '{')
            return parseFlowObject(text, pos);
        if (c == '"')
            return makeString(parseDoubleQuoted(text, pos));
        if (c == '\'')
            return makeString(parseSingleQuoted(text, pos));

        return parsePlainScalar(text, pos, inFlow);
    }

    constexpr Value parseFlowArray(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto result = Value {};
        result.kind = Value::Kind::Array;
        skipSpaces(text, pos);

        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            return result;
        }

        while (true)
        {
            result.arrayValue.push_back(parseFlowValue(text, pos, true));
            skipSpaces(text, pos);

            if (pos >= text.size())
                error("unterminated flow sequence");

            if (text[pos] == ',')
            {
                ++pos;
                continue;
            }

            if (text[pos] == ']')
            {
                ++pos;
                return result;
            }

            error("expected ',' or ']' in flow sequence");
        }
    }

    constexpr Value parseFlowObject(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto result = Value {};
        result.kind = Value::Kind::Object;
        skipSpaces(text, pos);

        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            return result;
        }

        while (true)
        {
            parseFlowEntryInto(text, pos, result.objectValue);
            skipSpaces(text, pos);

            if (pos >= text.size())
                error("unterminated flow mapping");

            if (text[pos] == ',')
            {
                ++pos;
                continue;
            }

            if (text[pos] == '}')
            {
                ++pos;
                return result;
            }

            error("expected ',' or '}' in flow mapping");
        }
    }

    constexpr void parseFlowEntryInto(std::string_view text,
                                      std::size_t& pos,
                                      std::vector<ConstexprJson::Member>& entries)
    {
        skipSpaces(text, pos);

        auto key = std::string {};

        if (pos < text.size() && (text[pos] == '"' || text[pos] == '\''))
            key = text[pos] == '"' ? parseDoubleQuoted(text, pos)
                                   : parseSingleQuoted(text, pos);
        else
            key = parseFlowPlainKey(text, pos);

        skipSpaces(text, pos);

        if (pos >= text.size() || text[pos] != ':')
            error("expected ':' in flow mapping");

        ++pos;
        ConstexprJson::insertIfAbsent(
            entries, std::move(key), parseFlowValue(text, pos, true));
    }

    constexpr std::string parseFlowPlainKey(std::string_view text, std::size_t& pos)
    {
        auto start = pos;

        while (pos < text.size() && text[pos] != ':' && text[pos] != ','
               && text[pos] != '}')
            ++pos;

        return std::string(trim(text.substr(start, pos - start)));
    }

    constexpr Value
        parsePlainScalar(std::string_view text, std::size_t& pos, bool inFlow)
    {
        auto start = pos;

        while (pos < text.size())
        {
            auto c = text[pos];

            if (inFlow && (c == ',' || c == ']' || c == '}'))
                break;

            ++pos;
        }

        auto token = trim(text.substr(start, pos - start));

        if (!token.empty())
            checkUnsupportedIndicator(token.front());

        return resolvePlainScalar(token);
    }

    constexpr void checkUnsupportedIndicator(char c) const
    {
        if (c == '|' || c == '>')
            error("block scalars ('|' and '>') are not supported");

        if (c == '&' || c == '*' || c == '!' || c == '%' || c == '@' || c == '`')
            error("unsupported YAML indicator '" + std::string(1, c) + "'");
    }

    // --- Quoted strings ---

    constexpr std::string parseDoubleQuoted(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto result = std::string {};

        while (pos < text.size() && text[pos] != '"')
        {
            if (text[pos] == '\\')
                parseEscapeSequence(text, pos, result);
            else
                result += text[pos++];
        }

        if (pos >= text.size())
            error("unterminated double-quoted string");

        ++pos;
        return result;
    }

    constexpr std::string parseSingleQuoted(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto result = std::string {};

        while (pos < text.size())
        {
            if (text[pos] == '\'')
            {
                if (pos + 1 < text.size() && text[pos + 1] == '\'')
                {
                    result += '\'';
                    pos += 2;
                    continue;
                }

                ++pos;
                return result;
            }

            result += text[pos++];
        }

        error("unterminated single-quoted string");
    }

    constexpr void parseEscapeSequence(std::string_view text,
                                       std::size_t& pos,
                                       std::string& result)
    {
        ++pos;

        if (pos >= text.size())
            error("unexpected end of string escape");

        auto escaped = text[pos];
        ++pos;

        switch (escaped)
        {
            case '"':
                result += '"';
                break;
            case '\\':
                result += '\\';
                break;
            case '/':
                result += '/';
                break;
            case 'b':
                result += '\b';
                break;
            case 'f':
                result += '\f';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            case 'u':
                parseUnicodeEscape(text, pos, result);
                break;
            default:
                error("invalid escape character '" + std::string(1, escaped) + "'");
        }
    }

    constexpr void parseUnicodeEscape(std::string_view text,
                                      std::size_t& pos,
                                      std::string& result)
    {
        if (text.size() - pos < 4)
            error("unexpected end of unicode escape");

        auto codepoint = unsigned {0};

        for (auto i = std::size_t {0}; i < 4; ++i)
        {
            auto digit = ConstexprJson::hexDigitValue(text[pos + i]);

            if (digit < 0)
                error("invalid unicode escape");

            codepoint = codepoint * 16 + static_cast<unsigned>(digit);
        }

        pos += 4;
        ConstexprJson::appendUtf8(result, codepoint);
    }

    // Index just past the closing quote of the quoted scalar starting
    // at `start`, or npos when unterminated. Mirrors the real quote
    // parsers but only skips — used for comment stripping and colon
    // scanning.
    static constexpr std::size_t skipQuoted(std::string_view text, std::size_t start)
    {
        auto quote = text[start];
        auto i = start + 1;

        while (i < text.size())
        {
            if (quote == '"' && text[i] == '\\')
            {
                i += 2;
                continue;
            }

            if (text[i] == quote)
            {
                if (quote == '\'' && i + 1 < text.size() && text[i + 1] == '\'')
                {
                    i += 2;
                    continue;
                }

                return i + 1;
            }

            ++i;
        }

        return npos;
    }

    // --- Line splitting ---

    constexpr void splitLines(std::string_view input)
    {
        auto lineStart = std::size_t {0};
        auto lineNumber = std::size_t {1};

        while (lineStart <= input.size())
        {
            auto lineEnd = input.find('\n', lineStart);
            auto rawLine = lineEnd == npos
                               ? input.substr(lineStart)
                               : input.substr(lineStart, lineEnd - lineStart);

            addLine(rawLine, lineNumber);
            ++lineNumber;

            if (lineEnd == npos)
                break;

            lineStart = lineEnd + 1;
        }
    }

    constexpr void addLine(std::string_view rawLine, std::size_t numberToUse)
    {
        if (!rawLine.empty() && rawLine.back() == '\r')
            rawLine.remove_suffix(1);

        auto indent = std::size_t {0};

        while (indent < rawLine.size() && rawLine[indent] == ' ')
            ++indent;

        if (indent < rawLine.size() && rawLine[indent] == '\t')
            errorAtLine(numberToUse,
                        "tab characters are not allowed in indentation");

        auto content = stripComment(rawLine.substr(indent));

        while (!content.empty() && (content.back() == ' ' || content.back() == '\t'))
            content.remove_suffix(1);

        if (content.empty())
            return;

        lines.push_back(Line {static_cast<int>(indent), content, numberToUse});
    }

    // Cuts the line at an unquoted '#' that starts the line or
    // follows whitespace. An unterminated quote is left intact so the
    // value parser reports it with a proper message.
    static constexpr std::string_view stripComment(std::string_view content)
    {
        for (auto i = std::size_t {0}; i < content.size(); ++i)
        {
            auto c = content[i];

            if (c == '"' || c == '\'')
            {
                auto after = skipQuoted(content, i);

                if (after == npos)
                    return content;

                i = after - 1;
            }
            else if (c == '#'
                     && (i == 0 || content[i - 1] == ' ' || content[i - 1] == '\t'))
            {
                return content.substr(0, i);
            }
        }

        return content;
    }

    // --- Helpers ---

    static constexpr std::string_view trim(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);

        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.remove_suffix(1);

        return text;
    }

    static constexpr void skipSpaces(std::string_view text, std::size_t& pos)
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
    }

    // Always throw. Deliberately not constexpr: reaching either
    // during constant evaluation fails compilation right here, with
    // this frame at the top of the evaluation trace.
    [[noreturn]] void error(const std::string& messageToUse) const
    {
        auto lineNumber = std::size_t {1};

        if (cur < lines.size())
            lineNumber = lines[cur].number;
        else if (!lines.empty())
            lineNumber = lines[lines.size() - 1].number;

        errorAtLine(lineNumber, messageToUse);
    }

    [[noreturn]] static void errorAtLine(std::size_t lineNumber,
                                         const std::string& messageToUse)
    {
        auto message = std::string {"YAML parse error at line "};
        ConstexprJson::appendInteger(message, static_cast<long long>(lineNumber));
        message += ": ";
        message += messageToUse;
        throw Yaml::ParseError(message);
    }

    std::vector<Line> lines;
    std::size_t cur = 0;
};

constexpr Value parse(std::string_view inputToUse)
{
    auto parser = Parser(inputToUse);
    return parser.parseDocument();
}

// --- Serde entry points (called from Serialize.h during constant
// evaluation) ---

template <typename T>
constexpr std::string toText(const T& value, int indent)
{
    auto root = Value {};
    auto ref = ConstexprJson::ValueReflector {root, topLevelOptions<T>(Mode::Save)};

    using Detail::reflectValue;
    reflectValue(ref, const_cast<T&>(value));

    // Qualified: ADL on ConstexprJson::Value would also find
    // ConstexprJson::print and be ambiguous.
    return ConstexprYaml::print(root, indent);
}

template <typename T>
constexpr void fromText(T& value, std::string_view text)
{
    auto root = parse(text);
    auto ref = ConstexprJson::ValueReflector {root, topLevelOptions<T>(Mode::Load)};

    using Detail::reflectValue;
    reflectValue(ref, value);
}

} // namespace Miro::Detail::ConstexprYaml
