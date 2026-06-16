#pragma once

#include "../Detail/DocumentOps.h"
#include "../Detail/ScalarText.h"
#include "Json.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

// JSON parser and printer, generic over the document model. The
// runtime entry points (Parser.cpp / Printer.cpp) instantiate these
// with Json::Value; the compile-time backend
// (Reflection/ConstexprJson.h) instantiates them with its constexpr
// mirror — same algorithms, same bytes, by construction.
//
// A value model V provides: default/null/bool/double/std::string
// construction, the isX()/asX() read accessors, and the construction
// seams from Detail/DocumentOps.h (setEmptyArray / setEmptyObject /
// reserveArray / appendElement / insertMember), found through ADL.

namespace Miro::Json::Generic
{

// --- Printer ---

template <typename V>
constexpr void printTo(std::string& output, const V& value, int indent, int depth);

constexpr void writeIndent(std::string& output, int indent, int depth)
{
    output += '\n';
    output.append(static_cast<std::size_t>(indent * depth), ' ');
}

template <typename V>
constexpr void printArray(std::string& output, const V& value, int indent, int depth)
{
    const auto& array = value.asArray();
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

template <typename V>
constexpr void
    printObject(std::string& output, const V& value, int indent, int depth)
{
    const auto& object = value.asObject();
    output += '{';

    if (object.empty())
    {
        output += '}';
        return;
    }

    auto first = true;

    for (const auto& [key, member]: object)
    {
        if (!first)
            output += ',';

        first = false;

        if (indent > 0)
            writeIndent(output, indent, depth + 1);

        Detail::printEscapedString(output, key);
        output += ':';

        if (indent > 0)
            output += ' ';

        printTo(output, member, indent, depth + 1);
    }

    if (indent > 0)
        writeIndent(output, indent, depth);

    output += '}';
}

template <typename V>
constexpr void printTo(std::string& output, const V& value, int indent, int depth)
{
    if (value.isNull())
        output += "null";
    else if (value.isBool())
        output += value.asBool() ? "true" : "false";
    else if (value.isNumber())
        Detail::printNumber(output, value.asNumber());
    else if (value.isString())
        Detail::printEscapedString(output, value.asString());
    else if (value.isArray())
        printArray(output, value, indent, depth);
    else if (value.isObject())
        printObject(output, value, indent, depth);
}

template <typename V>
constexpr std::string print(const V& valueToUse, int indentToUse)
{
    auto result = std::string {};
    printTo(result, valueToUse, indentToUse, 0);
    return result;
}

// --- Parser ---

template <typename V>
class Parser
{
public:
    constexpr explicit Parser(std::string_view inputToUse)
        : input(inputToUse)
    {
    }

    constexpr V parseValue()
    {
        skipWhitespaceAndComments();

        if (atEnd())
            error("unexpected end of input");

        auto c = peek();

        if (c == '"')
            return parseString();
        if (c == '{')
            return parseObject();
        if (c == '[')
            return parseArray();
        if (c == 't' || c == 'f')
            return parseBool();
        if (c == 'n')
            return parseNull();
        if (c == '-' || Detail::isDecimalDigit(c))
            return parseNumber();

        error("unexpected character '" + std::string(1, c) + "'");
    }

    constexpr bool atEnd() const { return pos >= input.size(); }

    constexpr void skipWhitespaceAndComments()
    {
        while (!atEnd())
        {
            auto c = peek();

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos;
            else if (c == '/')
                skipComment();
            else
                break;
        }
    }

    // Always throws. Deliberately not constexpr: reaching it during
    // constant evaluation fails compilation right here, with this
    // frame at the top of the evaluation trace.
    [[noreturn]] void error(const std::string& messageToUse) const
    {
        throw ParseError("JSON parse error at position " + std::to_string(pos) + ": "
                         + messageToUse);
    }

private:
    constexpr char peek() const { return input[pos]; }

    constexpr std::size_t remaining() const { return input.size() - pos; }

    constexpr void skipComment()
    {
        if (remaining() < 2 || (input[pos + 1] != '/' && input[pos + 1] != '*'))
            error("unexpected character '/'");

        if (input[pos + 1] == '/')
            skipLineComment();
        else
            skipBlockComment();
    }

    constexpr void skipLineComment()
    {
        pos += 2;

        while (!atEnd() && peek() != '\n' && peek() != '\r')
            ++pos;
    }

    constexpr void skipBlockComment()
    {
        auto start = pos;
        pos += 2;

        while (pos + 1 < input.size())
        {
            if (input[pos] == '*' && input[pos + 1] == '/')
            {
                pos += 2;
                return;
            }

            ++pos;
        }

        pos = start;
        error("unterminated block comment");
    }

    // --- Primitives ---

    constexpr V parseNull()
    {
        expectKeyword("null");
        return {nullptr};
    }

    constexpr V parseBool()
    {
        if (remaining() >= 4 && input[pos] == 't' && input[pos + 1] == 'r'
            && input[pos + 2] == 'u' && input[pos + 3] == 'e')
        {
            pos += 4;
            return {true};
        }

        expectKeyword("false");
        return {false};
    }

    constexpr V parseNumber()
    {
        auto start = pos;

        parseNumberSign();
        parseNumberIntegerPart();
        parseNumberFractionPart();
        parseNumberExponentPart();

        auto span = input.substr(start, pos - start);
        auto consumed = std::size_t {0};
        auto value = Detail::convertNumberText(span, consumed);

        if (consumed != span.size())
            error("failed to parse number");

        return {value};
    }

    constexpr void parseNumberSign()
    {
        if (!atEnd() && peek() == '-')
            ++pos;

        if (atEnd())
            error("unexpected end of number");
    }

    constexpr void parseNumberIntegerPart()
    {
        if (peek() == '0')
            ++pos;
        else if (peek() >= '1' && peek() <= '9')
            skipDigits();
        else
            error("invalid number");
    }

    constexpr void parseNumberFractionPart()
    {
        if (atEnd() || peek() != '.')
            return;

        ++pos;

        if (atEnd() || !Detail::isDecimalDigit(peek()))
            error("expected digit after decimal point");

        skipDigits();
    }

    constexpr void parseNumberExponentPart()
    {
        if (atEnd() || (peek() != 'e' && peek() != 'E'))
            return;

        ++pos;

        if (!atEnd() && (peek() == '+' || peek() == '-'))
            ++pos;

        if (atEnd() || !Detail::isDecimalDigit(peek()))
            error("expected digit in exponent");

        skipDigits();
    }

    // --- Strings ---

    constexpr V parseString() { return {parseStringRaw()}; }

    constexpr std::string parseStringRaw()
    {
        expect('"');

        auto start = pos;
        skipToStringBreak();

        if (!atEnd() && peek() == '"')
        {
            auto result = std::string(input.substr(start, pos - start));
            ++pos;
            return result;
        }

        auto result = std::string(input.substr(start, pos - start));
        parseStringEscapes(result);
        return result;
    }

    constexpr void parseStringEscapes(std::string& result)
    {
        while (!atEnd() && peek() != '"')
        {
            if (peek() == '\\')
            {
                parseEscapeSequence(result);
            }
            else
            {
                auto start = pos;
                skipToStringBreak();
                result += input.substr(start, pos - start);
            }
        }

        if (atEnd())
            error("expected '\"' but reached end of "
                  "input");

        ++pos;
    }

    constexpr void parseEscapeSequence(std::string& result)
    {
        ++pos;

        if (atEnd())
            error("unexpected end of string escape");

        auto escaped = peek();
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
                parseUnicodeEscape(result);
                break;
            default:
                error("invalid escape character '" + std::string(1, escaped) + "'");
        }
    }

    constexpr void parseUnicodeEscape(std::string& result)
    {
        if (remaining() < 4)
            error("unexpected end of unicode escape");

        auto codepoint = unsigned {0};

        for (auto i = std::size_t {0}; i < 4; ++i)
        {
            auto digit = Detail::hexDigitValue(input[pos + i]);

            if (digit < 0)
                error("invalid unicode escape");

            codepoint = codepoint * 16 + static_cast<unsigned>(digit);
        }

        pos += 4;
        Detail::appendUtf8(result, codepoint);
    }

    // --- Containers ---

    constexpr V parseArray()
    {
        expect('[');
        skipWhitespaceAndComments();

        auto result = V {};
        setEmptyArray(result);

        if (!atEnd() && peek() != ']')
        {
            reserveArray(result, 16);
            appendElement(result, parseValue());
            skipWhitespaceAndComments();

            while (!atEnd() && peek() == ',')
            {
                ++pos;
                appendElement(result, parseValue());
                skipWhitespaceAndComments();
            }
        }

        expect(']');
        return result;
    }

    constexpr V parseObject()
    {
        expect('{');
        skipWhitespaceAndComments();

        auto result = V {};
        setEmptyObject(result);

        if (!atEnd() && peek() != '}')
        {
            parseKeyValueInto(result);

            while (!atEnd() && peek() == ',')
            {
                ++pos;
                parseKeyValueInto(result);
            }
        }

        expect('}');
        return result;
    }

    constexpr void parseKeyValueInto(V& object)
    {
        skipWhitespaceAndComments();
        auto key = parseStringRaw();
        skipWhitespaceAndComments();
        expect(':');
        auto value = parseValue();
        insertMember(object, std::move(key), std::move(value));
        skipWhitespaceAndComments();
    }

    // --- Helpers ---

    constexpr void skipDigits()
    {
        while (!atEnd() && Detail::isDecimalDigit(peek()))
            ++pos;
    }

    constexpr void skipToStringBreak()
    {
        while (!atEnd() && peek() != '"' && peek() != '\\')
            ++pos;
    }

    constexpr void expectKeyword(std::string_view keywordToUse)
    {
        if (remaining() < keywordToUse.size()
            || input.substr(pos, keywordToUse.size()) != keywordToUse)
            error("expected '" + std::string(keywordToUse) + "'");

        pos += keywordToUse.size();
    }

    constexpr void expect(char charToUse)
    {
        if (atEnd() || peek() != charToUse)
        {
            error("expected '" + std::string(1, charToUse) + "'"
                  + (atEnd() ? " but reached end of input"
                             : " but got '" + std::string(1, peek()) + "'"));
        }
        ++pos;
    }

    std::string_view input;
    std::size_t pos = 0;
};

template <typename V>
constexpr V parse(std::string_view inputToUse)
{
    auto parser = Parser<V> {inputToUse};
    auto result = parser.parseValue();
    parser.skipWhitespaceAndComments();

    if (!parser.atEnd())
        parser.error("unexpected trailing content");

    return result;
}

} // namespace Miro::Json::Generic
