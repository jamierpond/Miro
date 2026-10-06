#include "Json.h"
#include "Number.h"

#include <charconv>
#include <optional>

namespace Miro::Json
{

class Parser
{
public:
    explicit Parser(std::string_view inputToUse)
        : input(inputToUse.data())
        , end(inputToUse.data() + inputToUse.size())
        , pos(inputToUse.data())
    {
    }

    Value parseValue()
    {
        skipWhitespaceAndComments();

        if (atEnd())
            error("unexpected end of input");

        auto c = *pos;

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
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();

        error("unexpected character '" + std::string(1, c) + "'");
    }

    bool atEnd() const { return pos >= end; }

    void skipWhitespaceAndComments()
    {
        while (pos < end)
        {
            auto c = *pos;

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos;
            else if (c == '/')
                skipComment();
            else
                break;
        }
    }

    void skipComment()
    {
        if (remaining() < 2 || (pos[1] != '/' && pos[1] != '*'))
            error("unexpected character '/'");

        if (pos[1] == '/')
            skipLineComment();
        else
            skipBlockComment();
    }

    void skipLineComment()
    {
        pos += 2;

        while (pos < end && *pos != '\n' && *pos != '\r')
            ++pos;
    }

    void skipBlockComment()
    {
        auto start = pos;
        pos += 2;

        while (pos + 1 < end)
        {
            if (pos[0] == '*' && pos[1] == '/')
            {
                pos += 2;
                return;
            }
            ++pos;
        }

        pos = start;
        error("unterminated block comment");
    }

    [[noreturn]] void error(const std::string& messageToUse) const
    {
        throw ParseError("JSON parse error at position "
                         + std::to_string(pos - input) + ": " + messageToUse);
    }

private:
    // --- Primitives ---

    Value parseNull()
    {
        expectKeyword("null", 4);
        return {nullptr};
    }

    Value parseBool()
    {
        if (remaining() >= 4 && pos[0] == 't' && pos[1] == 'r' && pos[2] == 'u'
            && pos[3] == 'e')
        {
            pos += 4;
            return {true};
        }

        expectKeyword("false", 5);
        return {false};
    }

    Value parseNumber()
    {
        auto start = pos;

        parseNumberSign();
        parseNumberIntegerPart();

        auto hasFraction = parseNumberFractionPart();
        auto hasExponent = parseNumberExponentPart();

        if (!hasFraction && !hasExponent)
        {
            if (auto integer = integerFrom(start))
                return {*integer};
        }

        auto token = std::string_view(start, static_cast<std::size_t>(pos - start));
        auto value = Detail::parseDouble(token);

        if (!value)
            error("failed to parse number");

        return {*value};
    }

    // Digits only, so the spelling names an integer — unless it is wider
    // than int64, in which case from_chars reports the overflow and the
    // caller falls back to the double path.
    std::optional<std::int64_t> integerFrom(const char* start) const
    {
        auto integer = std::int64_t {};
        auto [ptr, ec] = std::from_chars(start, pos, integer);

        if (ec != std::errc {} || ptr != pos)
            return std::nullopt;

        return integer;
    }

    void parseNumberSign()
    {
        if (pos < end && *pos == '-')
            ++pos;

        if (pos >= end)
            error("unexpected end of number");
    }

    void parseNumberIntegerPart()
    {
        if (*pos == '0')
            ++pos;
        else if (*pos >= '1' && *pos <= '9')
            skipDigits();
        else
            error("invalid number");
    }

    bool parseNumberFractionPart()
    {
        if (pos >= end || *pos != '.')
            return false;

        ++pos;

        if (pos >= end || *pos < '0' || *pos > '9')
            error("expected digit after decimal point");

        skipDigits();
        return true;
    }

    bool parseNumberExponentPart()
    {
        if (pos >= end || (*pos != 'e' && *pos != 'E'))
            return false;

        ++pos;

        if (pos < end && (*pos == '+' || *pos == '-'))
            ++pos;

        if (pos >= end || *pos < '0' || *pos > '9')
            error("expected digit in exponent");

        skipDigits();
        return true;
    }

    // --- Strings ---

    Value parseString() { return {parseStringRaw()}; }

    std::string parseStringRaw()
    {
        expect('"');

        auto start = pos;
        skipToStringBreak();

        if (pos < end && *pos == '"')
        {
            auto result = std::string(start, pos - start);
            ++pos;
            return result;
        }

        auto result = std::string(start, pos - start);
        parseStringEscapes(result);
        return result;
    }

    void parseStringEscapes(std::string& result)
    {
        while (pos < end && *pos != '"')
        {
            if (*pos == '\\')
            {
                parseEscapeSequence(result);
            }
            else
            {
                auto start = pos;
                skipToStringBreak();
                result.append(start, pos - start);
            }
        }

        if (pos >= end)
            error("expected '\"' but reached end of "
                  "input");

        ++pos;
    }

    void parseEscapeSequence(std::string& result)
    {
        ++pos;

        if (pos >= end)
            error("unexpected end of string escape");

        auto escaped = *pos;
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

    void parseUnicodeEscape(std::string& result)
    {
        auto codepoint = parseHexCodeUnit();

        if (isLowSurrogate(codepoint))
            error("unpaired low surrogate in unicode escape");

        if (isHighSurrogate(codepoint))
            codepoint = combineSurrogates(codepoint, parseLowSurrogateEscape());

        appendUtf8(result, codepoint);
    }

    unsigned parseHexCodeUnit()
    {
        if (remaining() < 4)
            error("unexpected end of unicode escape");

        auto hex = Miro::Array<char, 4> {pos[0], pos[1], pos[2], pos[3]};
        pos += 4;

        auto codeUnit = unsigned {};
        auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + 4, codeUnit, 16);

        if (ec != std::errc {} || ptr != hex.data() + 4)
            error("invalid unicode escape");

        return codeUnit;
    }

    unsigned parseLowSurrogateEscape()
    {
        if (remaining() < 2 || pos[0] != '\\' || pos[1] != 'u')
            error("expected a low surrogate escape after a high surrogate");

        pos += 2;
        auto codeUnit = parseHexCodeUnit();

        if (!isLowSurrogate(codeUnit))
            error("expected a low surrogate after a high surrogate");

        return codeUnit;
    }

    // --- Containers ---

    Value parseArray()
    {
        expect('[');
        skipWhitespaceAndComments();

        auto elements = Array {};

        if (pos < end && *pos != ']')
        {
            elements.reserve(16);
            elements.add(parseValue());
            skipWhitespaceAndComments();

            while (pos < end && *pos == ',')
            {
                ++pos;
                elements.add(parseValue());
                skipWhitespaceAndComments();
            }
        }

        expect(']');
        return {std::move(elements)};
    }

    Value parseObject()
    {
        expect('{');
        skipWhitespaceAndComments();

        auto entries = Object {};

        if (pos < end && *pos != '}')
        {
            parseKeyValueInto(entries);

            while (pos < end && *pos == ',')
            {
                ++pos;
                parseKeyValueInto(entries);
            }
        }

        expect('}');
        return {std::move(entries)};
    }

    void parseKeyValueInto(Object& entries)
    {
        skipWhitespaceAndComments();
        auto key = parseStringRaw();
        skipWhitespaceAndComments();
        expect(':');
        auto value = parseValue();
        entries.emplace(std::move(key), std::move(value));
        skipWhitespaceAndComments();
    }

    // --- Helpers ---

    static bool isHighSurrogate(unsigned codeUnit)
    {
        return codeUnit >= 0xD800 && codeUnit <= 0xDBFF;
    }

    static bool isLowSurrogate(unsigned codeUnit)
    {
        return codeUnit >= 0xDC00 && codeUnit <= 0xDFFF;
    }

    static unsigned combineSurrogates(unsigned high, unsigned low)
    {
        return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
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
        else if (codepoint <= 0xFFFF)
        {
            result += static_cast<char>(0xE0 | (codepoint >> 12));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            result += static_cast<char>(0xF0 | (codepoint >> 18));
            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    void skipDigits()
    {
        while (pos < end && *pos >= '0' && *pos <= '9')
            ++pos;
    }

    void skipToStringBreak()
    {
        while (pos < end && *pos != '"' && *pos != '\\')
            ++pos;
    }

    std::ptrdiff_t remaining() const { return end - pos; }

    void expectKeyword(const char* keywordToUse, int lengthToUse)
    {
        if (remaining() < lengthToUse)
            error("expected '" + std::string(keywordToUse) + "'");

        for (auto i = 0; i < lengthToUse; ++i)
        {
            if (pos[i] != keywordToUse[i])
                error("expected '" + std::string(keywordToUse) + "'");
        }

        pos += lengthToUse;
    }

    void expect(char charToUse)
    {
        if (pos >= end || *pos != charToUse)
        {
            error("expected '" + std::string(1, charToUse) + "'"
                  + (pos >= end ? " but reached end of input"
                                : " but got '" + std::string(1, *pos) + "'"));
        }
        ++pos;
    }

    const char* input;
    const char* end;
    const char* pos;
};

Value parse(std::string_view inputToUse)
{
    auto parser = Parser(inputToUse);
    auto result = parser.parseValue();
    parser.skipWhitespaceAndComments();

    if (!parser.atEnd())
        parser.error("unexpected trailing content");

    return result;
}

Value getParsedValue(std::string_view inputToUse)
{
    try
    {
        return parse(inputToUse);
    }
    catch (...)
    {
        return Value {};
    }
}

} // namespace Miro::Json
