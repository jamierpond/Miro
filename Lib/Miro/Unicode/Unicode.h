#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Miro::Unicode
{

// The 30 Unicode general categories, grouped by major class so every
// class predicate below is a range test on the enum value. The
// comments carry the standard two-letter abbreviations.
enum class GeneralCategory
{
    UppercaseLetter, // Lu
    LowercaseLetter, // Ll
    TitlecaseLetter, // Lt
    ModifierLetter, // Lm
    OtherLetter, // Lo
    NonspacingMark, // Mn
    SpacingMark, // Mc
    EnclosingMark, // Me
    DecimalNumber, // Nd
    LetterNumber, // Nl
    OtherNumber, // No
    ConnectorPunctuation, // Pc
    DashPunctuation, // Pd
    OpenPunctuation, // Ps
    ClosePunctuation, // Pe
    InitialPunctuation, // Pi
    FinalPunctuation, // Pf
    OtherPunctuation, // Po
    MathSymbol, // Sm
    CurrencySymbol, // Sc
    ModifierSymbol, // Sk
    OtherSymbol, // So
    SpaceSeparator, // Zs
    LineSeparator, // Zl
    ParagraphSeparator, // Zp
    Control, // Cc
    Format, // Cf
    Surrogate, // Cs
    PrivateUse, // Co
    Unassigned // Cn
};

GeneralCategory generalCategory(char32_t codePoint);
std::string_view shortName(GeneralCategory category);

bool isLetter(char32_t codePoint);
bool isMark(char32_t codePoint);
bool isNumber(char32_t codePoint);
bool isPunctuation(char32_t codePoint);
bool isSymbol(char32_t codePoint);
bool isSeparator(char32_t codePoint);
bool isOther(char32_t codePoint);

// The White_Space property, which is what \s means in a
// Unicode-aware regex engine. Not the same set as Z*: it takes in
// U+0009..U+000D and U+0085, and leaves out U+200B.
bool isWhitespace(char32_t codePoint);

struct CodePoint
{
    char32_t value = 0;
    int byteLength = 1;
    bool valid = false;
};

// Decodes one UTF-8 sequence at position. On an invalid, truncated,
// overlong, surrogate-encoding or > U+10FFFF sequence, returns the
// single lead byte as value with byteLength 1 and valid false, so a
// caller slicing input can keep it verbatim.
CodePoint decodeUtf8(std::string_view text, std::size_t position);

void appendUtf8(std::string& text, char32_t codePoint);

} // namespace Miro::Unicode
