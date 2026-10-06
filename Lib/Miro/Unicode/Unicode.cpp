#include "Unicode.h"

#include "GeneralCategoryTable.h"

#include <algorithm>
#include <iterator>

namespace Miro::Unicode
{

namespace
{

constexpr auto categoryBits = 5;
constexpr auto categoryMask = std::uint32_t {(1u << categoryBits) - 1u};
constexpr auto maxCodePoint = char32_t {0x10FFFF};

bool inRange(GeneralCategory category, GeneralCategory first, GeneralCategory last)
{
    return category >= first && category <= last;
}

} // namespace

GeneralCategory generalCategory(char32_t codePoint)
{
    if (codePoint > maxCodePoint)
        return GeneralCategory::Unassigned;

    // Filling the low bits with the mask puts the key above every
    // entry sharing its code point, so upper_bound lands on the run
    // after the one we want. The first entry starts at U+0000, so
    // there is always a run to step back to.
    auto key = ((std::uint32_t) codePoint << categoryBits) | categoryMask;

    auto run = std::upper_bound(std::begin(Detail::generalCategoryRuns),
                                std::end(Detail::generalCategoryRuns),
                                key);

    return (GeneralCategory) (*(run - 1) & categoryMask);
}

std::string_view shortName(GeneralCategory category)
{
    constexpr std::string_view names[] = {
        "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl",
        "No", "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc",
        "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn"};

    return names[(std::size_t) category];
}

bool isLetter(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::UppercaseLetter,
                   GeneralCategory::OtherLetter);
}

bool isMark(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::NonspacingMark,
                   GeneralCategory::EnclosingMark);
}

bool isNumber(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::DecimalNumber,
                   GeneralCategory::OtherNumber);
}

bool isPunctuation(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::ConnectorPunctuation,
                   GeneralCategory::OtherPunctuation);
}

bool isSymbol(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::MathSymbol,
                   GeneralCategory::OtherSymbol);
}

bool isSeparator(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::SpaceSeparator,
                   GeneralCategory::ParagraphSeparator);
}

bool isOther(char32_t codePoint)
{
    return inRange(generalCategory(codePoint),
                   GeneralCategory::Control,
                   GeneralCategory::Unassigned);
}

bool isWhitespace(char32_t codePoint)
{
    if (codePoint < 0x80)
        return (codePoint >= 0x09 && codePoint <= 0x0D) || codePoint == 0x20;

    if (codePoint >= 0x2000 && codePoint <= 0x200A)
        return true;

    switch (codePoint)
    {
        case 0x0085:
        case 0x00A0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;

        default:
            return false;
    }
}

CodePoint decodeUtf8(std::string_view text, std::size_t position)
{
    if (position >= text.size())
        return {};

    auto lead = (unsigned char) text[position];

    if (lead < 0x80)
        return {lead, 1, true};

    // Every rejection reports the lead byte alone, so a caller
    // slicing input advances by one and keeps the byte verbatim.
    auto rejected = CodePoint {lead, 1, false};

    auto length = 0;
    auto value = char32_t {0};

    if ((lead & 0xE0) == 0xC0)
    {
        length = 2;
        value = lead & 0x1Fu;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        length = 3;
        value = lead & 0x0Fu;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        length = 4;
        value = lead & 0x07u;
    }
    else
    {
        return rejected;
    }

    if (position + (std::size_t) length > text.size())
        return rejected;

    for (auto index = 1; index < length; ++index)
    {
        auto continuation = (unsigned char) text[position + (std::size_t) index];

        if ((continuation & 0xC0) != 0x80)
            return rejected;

        value = (value << 6) | (continuation & 0x3Fu);
    }

    constexpr char32_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};

    if (value < smallest[length] || value > maxCodePoint)
        return rejected;

    if (value >= 0xD800 && value <= 0xDFFF)
        return rejected;

    return {value, length, true};
}

void appendUtf8(std::string& text, char32_t codePoint)
{
    auto append = [&text](char32_t byte) { text.push_back((char) (byte & 0xFF)); };

    if (codePoint < 0x80)
    {
        append(codePoint);
    }
    else if (codePoint < 0x800)
    {
        append(0xC0 | (codePoint >> 6));
        append(0x80 | (codePoint & 0x3F));
    }
    else if (codePoint < 0x10000)
    {
        append(0xE0 | (codePoint >> 12));
        append(0x80 | ((codePoint >> 6) & 0x3F));
        append(0x80 | (codePoint & 0x3F));
    }
    else
    {
        append(0xF0 | (codePoint >> 18));
        append(0x80 | ((codePoint >> 12) & 0x3F));
        append(0x80 | ((codePoint >> 6) & 0x3F));
        append(0x80 | (codePoint & 0x3F));
    }
}

} // namespace Miro::Unicode
