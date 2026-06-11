#include "Yaml.h"

#include <cstdlib>

namespace Miro::Yaml
{

Value resolvePlainScalar(std::string_view tokenToUse)
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
        auto buffer = std::string(tokenToUse);
        char* numEnd = nullptr;
        auto number = std::strtod(buffer.c_str(), &numEnd);

        if (numEnd == buffer.c_str() + buffer.size())
            return {number};
    }

    return {std::string(tokenToUse)};
}

// Line-oriented parser. The input is split into (indent, content)
// lines up front — comments and blank lines are stripped during the
// split — then block structure is recovered from indentation alone.
// Inline ("flow") content never spans lines, so each line's content
// can be handed to a self-contained single-line flow parser.
class Parser
{
public:
    explicit Parser(std::string_view inputToUse) { splitLines(inputToUse); }

    Value parseDocument()
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

    Value parseNode()
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

    Value parseMapping(int indent)
    {
        auto entries = Object {};

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
                entries.emplace(std::move(key), std::move(value));
            }
            else
            {
                ++cur;
                entries.emplace(std::move(key), parseNestedValue(indent));
            }
        }

        return {std::move(entries)};
    }

    // Value of a "key:" line with nothing after the colon: a nested
    // block at deeper indent, a sequence at the same indent (YAML
    // allows the dashes to align with the parent key), or null.
    Value parseNestedValue(int indent)
    {
        if (cur >= lines.size())
            return {nullptr};

        if (lines[cur].indent > indent)
            return parseNode();

        if (lines[cur].indent == indent && isDashLine(lines[cur].content))
            return parseSequence(indent);

        return {nullptr};
    }

    Value parseSequence(int indent)
    {
        auto elements = Array {};

        while (cur < lines.size() && lines[cur].indent == indent
               && isDashLine(lines[cur].content))
        {
            elements.add(parseSequenceElement(indent));
        }

        return {std::move(elements)};
    }

    Value parseSequenceElement(int indent)
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

    static bool isDashLine(std::string_view content)
    {
        return content == "-" || content.substr(0, 2) == "- ";
    }

    // Position of the colon that splits "key: value" (or "key:") at
    // the top level of a line — outside quotes and flow brackets,
    // followed by a space or end of line. npos when the line is not
    // a mapping entry.
    std::size_t mappingColonPos(std::string_view content) const
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

    std::string parseKey(std::string_view keyPart)
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

    Value parseFlowLine(std::string_view text)
    {
        auto pos = std::size_t {0};
        auto value = parseFlowValue(text, pos, false);
        skipSpaces(text, pos);

        if (pos != text.size())
            error("unexpected trailing content after value");

        return value;
    }

    Value parseFlowValue(std::string_view text, std::size_t& pos, bool inFlow)
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

    Value parseFlowArray(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto elements = Array {};
        skipSpaces(text, pos);

        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            return {std::move(elements)};
        }

        while (true)
        {
            elements.add(parseFlowValue(text, pos, true));
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
                return {std::move(elements)};
            }

            error("expected ',' or ']' in flow sequence");
        }
    }

    Value parseFlowObject(std::string_view text, std::size_t& pos)
    {
        ++pos;
        auto entries = Object {};
        skipSpaces(text, pos);

        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            return {std::move(entries)};
        }

        while (true)
        {
            parseFlowEntryInto(text, pos, entries);
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
                return {std::move(entries)};
            }

            error("expected ',' or '}' in flow mapping");
        }
    }

    void parseFlowEntryInto(std::string_view text, std::size_t& pos, Object& entries)
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
        entries.emplace(std::move(key), parseFlowValue(text, pos, true));
    }

    std::string parseFlowPlainKey(std::string_view text, std::size_t& pos)
    {
        auto start = pos;

        while (pos < text.size() && text[pos] != ':' && text[pos] != ','
               && text[pos] != '}')
            ++pos;

        return std::string(trim(text.substr(start, pos - start)));
    }

    Value parsePlainScalar(std::string_view text, std::size_t& pos, bool inFlow)
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

    void checkUnsupportedIndicator(char c) const
    {
        if (c == '|' || c == '>')
            error("block scalars ('|' and '>') are not supported");

        if (c == '&' || c == '*' || c == '!' || c == '%' || c == '@' || c == '`')
            error("unsupported YAML indicator '" + std::string(1, c) + "'");
    }

    // --- Quoted strings ---

    std::string parseDoubleQuoted(std::string_view text, std::size_t& pos)
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

    std::string parseSingleQuoted(std::string_view text, std::size_t& pos)
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

    void parseEscapeSequence(std::string_view text,
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

    void parseUnicodeEscape(std::string_view text,
                            std::size_t& pos,
                            std::string& result)
    {
        if (text.size() - pos < 4)
            error("unexpected end of unicode escape");

        auto codepoint = unsigned {0};

        for (auto i = std::size_t {0}; i < 4; ++i)
        {
            auto digit = hexDigit(text[pos + i]);

            if (digit < 0)
                error("invalid unicode escape");

            codepoint = codepoint * 16 + static_cast<unsigned>(digit);
        }

        pos += 4;
        appendUtf8(result, codepoint);
    }

    static int hexDigit(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;

        return -1;
    }

    static void appendUtf8(std::string& result, unsigned codepoint)
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

    // Index just past the closing quote of the quoted scalar starting
    // at `start`, or npos when unterminated. Mirrors the real quote
    // parsers but only skips — used for comment stripping and colon
    // scanning.
    static std::size_t skipQuoted(std::string_view text, std::size_t start)
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

    void splitLines(std::string_view input)
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

    void addLine(std::string_view rawLine, std::size_t numberToUse)
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

        lines.add(Line {static_cast<int>(indent), content, numberToUse});
    }

    // Cuts the line at an unquoted '#' that starts the line or
    // follows whitespace. An unterminated quote is left intact so the
    // value parser reports it with a proper message.
    static std::string_view stripComment(std::string_view content)
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

    static std::string_view trim(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);

        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.remove_suffix(1);

        return text;
    }

    static void skipSpaces(std::string_view text, std::size_t& pos)
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
    }

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

    Vector<Line> lines;
    int cur = 0;
};

Value parse(std::string_view inputToUse)
{
    auto parser = Parser(inputToUse);
    return parser.parseDocument();
}

} // namespace Miro::Yaml
