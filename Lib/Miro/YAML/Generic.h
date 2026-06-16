#pragma once

#include "../Detail/DocumentOps.h"
#include "../Detail/ScalarText.h"
#include "Yaml.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// YAML scalar resolution, printer and parser, generic over the
// document model — same arrangement as JSON/Generic.h: the runtime
// entry points instantiate with Json::Value, the compile-time backend
// (Reflection/ConstexprYaml.h) with the constexpr mirror. The value
// model requirements are the same as for JSON/Generic.h.

namespace Miro::Yaml::Generic
{

// Resolves a plain (unquoted) scalar token: null / bool / number
// forms produce typed values, anything else a string. Number
// recognition is strtod's (full-consumption), via
// Detail::convertNumberText.
template <typename V>
constexpr V resolvePlainScalar(std::string_view tokenToUse)
{
    if (tokenToUse.empty() || tokenToUse == "~" || tokenToUse == "null"
        || tokenToUse == "Null" || tokenToUse == "NULL")
        return {nullptr};

    if (tokenToUse == "true" || tokenToUse == "True" || tokenToUse == "TRUE")
        return {true};

    if (tokenToUse == "false" || tokenToUse == "False" || tokenToUse == "FALSE")
        return {false};

    auto first = tokenToUse.front();

    if (first == '-' || first == '+' || first == '.'
        || (first >= '0' && first <= '9'))
    {
        auto consumed = std::size_t {0};
        auto number = Detail::convertNumberText(tokenToUse, consumed);

        if (consumed == tokenToUse.size())
            return {number};
    }

    return {std::string(tokenToUse)};
}

// --- Printer ---

template <typename V>
constexpr void
    printBlockTo(std::string& output, const V& value, int indent, int depth);

template <typename V>
constexpr void printFlowTo(std::string& output, const V& value);

// A string can stay plain (unquoted) only when reading it back yields
// the same string: it must not resolve to null / bool / number, must
// not start with a YAML indicator, and must not contain syntax that
// would terminate or alter the scalar in block or flow context.
template <typename V>
constexpr bool needsQuoting(const std::string& text)
{
    if (text.empty())
        return true;

    if (!resolvePlainScalar<V>(text).isString())
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

template <typename V>
constexpr void printScalarString(std::string& output, const std::string& text)
{
    if (needsQuoting<V>(text))
        Detail::printEscapedString(output, text);
    else
        output += text;
}

// True for values that render on a single line within block style:
// scalars and empty containers.
template <typename V>
constexpr bool isInline(const V& value)
{
    if (value.isArray())
        return value.asArray().empty();

    if (value.isObject())
        return value.asObject().empty();

    return true;
}

template <typename V>
constexpr void printInlineTo(std::string& output, const V& value)
{
    if (value.isNull())
        output += "null";
    else if (value.isBool())
        output += value.asBool() ? "true" : "false";
    else if (value.isNumber())
        Detail::printNumber(output, value.asNumber());
    else if (value.isString())
        printScalarString<V>(output, value.asString());
    else if (value.isArray())
        output += "[]";
    else if (value.isObject())
        output += "{}";
}

template <typename V>
constexpr void
    printBlockMapping(std::string& output, const V& value, int indent, int depth)
{
    for (const auto& [key, member]: value.asObject())
    {
        output.append(static_cast<std::size_t>(indent * depth), ' ');
        printScalarString<V>(output, key);
        output += ':';

        if (isInline(member))
        {
            output += ' ';
            printInlineTo(output, member);
            output += '\n';
        }
        else
        {
            output += '\n';
            printBlockTo(output, member, indent, depth + 1);
        }
    }
}

template <typename V>
constexpr void
    printBlockSequence(std::string& output, const V& value, int indent, int depth)
{
    for (const auto& element: value.asArray())
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

template <typename V>
constexpr void
    printBlockTo(std::string& output, const V& value, int indent, int depth)
{
    if (value.isObject())
        printBlockMapping(output, value, indent, depth);
    else
        printBlockSequence(output, value, indent, depth);
}

template <typename V>
constexpr void printFlowMapping(std::string& output, const V& value)
{
    output += '{';
    auto first = true;

    for (const auto& [key, member]: value.asObject())
    {
        if (!first)
            output += ", ";

        first = false;
        printScalarString<V>(output, key);
        output += ": ";
        printFlowTo(output, member);
    }

    output += '}';
}

template <typename V>
constexpr void printFlowSequence(std::string& output, const V& value)
{
    output += '[';
    auto first = true;

    for (const auto& element: value.asArray())
    {
        if (!first)
            output += ", ";

        first = false;
        printFlowTo(output, element);
    }

    output += ']';
}

template <typename V>
constexpr void printFlowTo(std::string& output, const V& value)
{
    if (value.isArray() && !value.asArray().empty())
        printFlowSequence(output, value);
    else if (value.isObject() && !value.asObject().empty())
        printFlowMapping(output, value);
    else
        printInlineTo(output, value);
}

// Block style by default; indentToUse <= 0 emits single-line flow
// style instead. Block style needs at least two columns per level,
// so smaller positive indents are clamped to 2.
template <typename V>
constexpr std::string print(const V& valueToUse, int indentToUse)
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

// --- Parser ---

// Line-oriented parser. The input is split into (indent, content)
// lines up front — comments and blank lines are stripped during the
// split — then block structure is recovered from indentation alone.
// Inline ("flow") content never spans lines, so each line's content
// can be handed to a self-contained single-line flow parser.
template <typename V>
class Parser
{
public:
    constexpr explicit Parser(std::string_view inputToUse)
    {
        splitLines(inputToUse);
    }

    constexpr V parseDocument()
    {
        if (cur < lines.size() && lines[cur].content == "---")
            ++cur;

        if (cur >= lines.size())
            return {nullptr};

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

    constexpr V parseNode()
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

    constexpr V parseMapping(int indent)
    {
        auto result = V {};
        setEmptyObject(result);

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
                insertMember(result, std::move(key), std::move(value));
            }
            else
            {
                ++cur;
                insertMember(result, std::move(key), parseNestedValue(indent));
            }
        }

        return result;
    }

    // Value of a "key:" line with nothing after the colon: a nested
    // block at deeper indent, a sequence at the same indent (YAML
    // allows the dashes to align with the parent key), or null.
    constexpr V parseNestedValue(int indent)
    {
        if (cur >= lines.size())
            return {nullptr};

        if (lines[cur].indent > indent)
            return parseNode();

        if (lines[cur].indent == indent && isDashLine(lines[cur].content))
            return parseSequence(indent);

        return {nullptr};
    }

    constexpr V parseSequence(int indent)
    {
        auto result = V {};
        setEmptyArray(result);

        while (cur < lines.size() && lines[cur].indent == indent
               && isDashLine(lines[cur].content))
        {
            appendElement(result, parseSequenceElement(indent));
        }

        return result;
    }

    constexpr V parseSequenceElement(int indent)
    {
        auto content = lines[cur].content;

        if (content == "-")
        {
            ++cur;

            if (cur < lines.size() && lines[cur].indent > indent)
                return parseNode();

            return {nullptr};
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

    constexpr V parseFlowLine(std::string_view text)
    {
        auto pos = std::size_t {0};
        auto value = parseFlowValue(text, pos, false);
        skipSpaces(text, pos);

        if (pos != text.size())
            error("unexpected trailing content after value");

        return value;
    }

    constexpr V parseFlowValue(std::string_view text, std::size_t& pos, bool inFlow)
    {
        skipSpaces(text, pos);

        if (pos >= text.size())
            return {nullptr};

        auto c = text[pos];

        if (c == '[')
            return parseFlowArray(text, pos);
        if (c == '{')
            return parseFlowObject(text, pos);
        if (c == '"')
            return {parseDoubleQuoted(text, pos)};
        if (c == '\'')
            return {parseSingleQuoted(text, pos)};

        return parsePlainScalar(text, pos, inFlow);
    }

    constexpr V parseFlowArray(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto result = V {};
        setEmptyArray(result);
        skipSpaces(text, pos);

        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            return result;
        }

        while (true)
        {
            appendElement(result, parseFlowValue(text, pos, true));
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

    constexpr V parseFlowObject(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto result = V {};
        setEmptyObject(result);
        skipSpaces(text, pos);

        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            return result;
        }

        while (true)
        {
            parseFlowEntryInto(text, pos, result);
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

    constexpr void
        parseFlowEntryInto(std::string_view text, std::size_t& pos, V& entries)
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
        insertMember(entries, std::move(key), parseFlowValue(text, pos, true));
    }

    constexpr std::string parseFlowPlainKey(std::string_view text, std::size_t& pos)
    {
        auto start = pos;

        while (pos < text.size() && text[pos] != ':' && text[pos] != ','
               && text[pos] != '}')
            ++pos;

        return std::string(trim(text.substr(start, pos - start)));
    }

    constexpr V
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

        return resolvePlainScalar<V>(token);
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
            auto digit = Detail::hexDigitValue(text[pos + i]);

            if (digit < 0)
                error("invalid unicode escape");

            codepoint = codepoint * 16 + static_cast<unsigned>(digit);
        }

        pos += 4;
        Detail::appendUtf8(result, codepoint);
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
        throw ParseError("YAML parse error at line " + std::to_string(lineNumber)
                         + ": " + messageToUse);
    }

    std::vector<Line> lines;
    std::size_t cur = 0;
};

template <typename V>
constexpr V parse(std::string_view inputToUse)
{
    auto parser = Parser<V> {inputToUse};
    return parser.parseDocument();
}

} // namespace Miro::Yaml::Generic
