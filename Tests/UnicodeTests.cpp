#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <set>
#include <string>
#include <string_view>
#include <utility>

using namespace nano;
using namespace Miro::Unicode;

using C = GeneralCategory;

namespace
{

std::string encoded(char32_t codePoint)
{
    auto text = std::string {};
    appendUtf8(text, codePoint);
    return text;
}

} // namespace

auto categoryAscii = test("generalCategory: ASCII representatives") = []
{
    check(generalCategory(U'A') == C::UppercaseLetter);
    check(generalCategory(U'a') == C::LowercaseLetter);
    check(generalCategory(U'7') == C::DecimalNumber);
    check(generalCategory(U' ') == C::SpaceSeparator);
    check(generalCategory(U'\n') == C::Control);
    check(generalCategory(U'!') == C::OtherPunctuation);
    check(generalCategory(U'$') == C::CurrencySymbol);
    check(generalCategory(U'+') == C::MathSymbol);
    check(generalCategory(U'^') == C::ModifierSymbol);
    check(generalCategory(U'_') == C::ConnectorPunctuation);
    check(generalCategory(U'-') == C::DashPunctuation);
    check(generalCategory(U'(') == C::OpenPunctuation);
    check(generalCategory(U')') == C::ClosePunctuation);
};

auto categoryLetters = test("generalCategory: letter subcategories") = []
{
    check(generalCategory(0x00E9) == C::LowercaseLetter);
    check(generalCategory(0x01C5) == C::TitlecaseLetter);
    check(generalCategory(0x02B0) == C::ModifierLetter);
    check(generalCategory(0x4F60) == C::OtherLetter);
};

auto categoryMarks = test("generalCategory: mark subcategories") = []
{
    check(generalCategory(0x0301) == C::NonspacingMark);
    check(generalCategory(0x0903) == C::SpacingMark);
    check(generalCategory(0x20DD) == C::EnclosingMark);
    check(generalCategory(0xE01EF) == C::NonspacingMark);
};

auto categoryNumbers = test("generalCategory: number subcategories") = []
{
    check(generalCategory(0x0663) == C::DecimalNumber);
    check(generalCategory(0x2160) == C::LetterNumber);
    check(generalCategory(0x00B2) == C::OtherNumber);
};

auto categoryQuotes = test("generalCategory: initial and final punctuation") = []
{
    check(generalCategory(0x00AB) == C::InitialPunctuation);
    check(generalCategory(0x00BB) == C::FinalPunctuation);
};

auto categorySeparators = test("generalCategory: line and paragraph separators") = []
{
    check(generalCategory(0x2028) == C::LineSeparator);
    check(generalCategory(0x2029) == C::ParagraphSeparator);
};

auto categoryOther = test("generalCategory: the C* categories") = []
{
    check(generalCategory(0x200B) == C::Format);
    check(generalCategory(0xFEFF) == C::Format);
    check(generalCategory(0xD800) == C::Surrogate);
    check(generalCategory(0xE000) == C::PrivateUse);
    check(generalCategory(0x0378) == C::Unassigned);
    check(generalCategory(0x10FFFF) == C::Unassigned);
};

auto categoryEmoji = test("generalCategory: emoji is So") = []
{ check(generalCategory(0x1F600) == C::OtherSymbol); };

// Garay (U+10D40..) and Todhri (U+105C0..) are blocks added in Unicode
// 16.0; all three were Cn before it. They fail against a table built
// from an older UCD, which is the point of testing them.
auto categoryUnicode16 =
    test("generalCategory: code points new in Unicode 16.0") = []
{
    check(generalCategory(0x10D40) == C::DecimalNumber);
    check(generalCategory(0x10D50) == C::UppercaseLetter);
    check(generalCategory(0x105C0) == C::OtherLetter);
};

auto shortNameRoundTrip = test("shortName: all 30 enumerators") = []
{
    constexpr std::pair<GeneralCategory, std::string_view> expected[] = {
        {C::UppercaseLetter, "Lu"},
        {C::LowercaseLetter, "Ll"},
        {C::TitlecaseLetter, "Lt"},
        {C::ModifierLetter, "Lm"},
        {C::OtherLetter, "Lo"},
        {C::NonspacingMark, "Mn"},
        {C::SpacingMark, "Mc"},
        {C::EnclosingMark, "Me"},
        {C::DecimalNumber, "Nd"},
        {C::LetterNumber, "Nl"},
        {C::OtherNumber, "No"},
        {C::ConnectorPunctuation, "Pc"},
        {C::DashPunctuation, "Pd"},
        {C::OpenPunctuation, "Ps"},
        {C::ClosePunctuation, "Pe"},
        {C::InitialPunctuation, "Pi"},
        {C::FinalPunctuation, "Pf"},
        {C::OtherPunctuation, "Po"},
        {C::MathSymbol, "Sm"},
        {C::CurrencySymbol, "Sc"},
        {C::ModifierSymbol, "Sk"},
        {C::OtherSymbol, "So"},
        {C::SpaceSeparator, "Zs"},
        {C::LineSeparator, "Zl"},
        {C::ParagraphSeparator, "Zp"},
        {C::Control, "Cc"},
        {C::Format, "Cf"},
        {C::Surrogate, "Cs"},
        {C::PrivateUse, "Co"},
        {C::Unassigned, "Cn"},
    };

    auto seen = std::set<std::string_view> {};

    for (auto [category, name]: expected)
    {
        check(shortName(category) == name);
        seen.insert(name);
    }

    check(seen.size() == 30);
};

auto letterClass = test("isLetter over every letter subcategory") = []
{
    check(isLetter(U'A'));
    check(isLetter(U'a'));
    check(isLetter(0x01C5));
    check(isLetter(0x02B0));
    check(isLetter(0x4F60));
    check(!isLetter(U'7'));
    check(!isLetter(U' '));
};

auto markClass = test("isMark over every mark subcategory") = []
{
    check(isMark(0x0301));
    check(isMark(0x0903));
    check(isMark(0x20DD));
    check(!isMark(U'a'));
};

auto numberClass = test("isNumber over every number subcategory") = []
{
    check(isNumber(U'7'));
    check(isNumber(0x0663));
    check(isNumber(0x2160));
    check(isNumber(0x00B2));
    check(!isNumber(U'A'));
};

auto punctuationClass = test("isPunctuation over the P* categories") = []
{
    check(isPunctuation(U'_'));
    check(isPunctuation(U'-'));
    check(isPunctuation(U'('));
    check(isPunctuation(U')'));
    check(isPunctuation(0x00AB));
    check(isPunctuation(0x00BB));
    check(isPunctuation(U'!'));
    check(!isPunctuation(U'+'));
};

auto symbolClass = test("isSymbol over the S* categories") = []
{
    check(isSymbol(U'+'));
    check(isSymbol(U'$'));
    check(isSymbol(U'^'));
    check(isSymbol(0x1F600));
    check(!isSymbol(U'!'));
};

auto separatorClass = test("isSeparator over the Z* categories") = []
{
    check(isSeparator(U' '));
    check(isSeparator(0x2028));
    check(isSeparator(0x2029));
    check(!isSeparator(U'a'));
};

auto otherClass = test("isOther over the C* categories") = []
{
    check(isOther(U'\n'));
    check(isOther(0xFEFF));
    check(isOther(0xD800));
    check(isOther(0xE000));
    check(isOther(0x0378));
    check(!isOther(U'a'));
};

auto whitespaceProperty = test("isWhitespace follows White_Space, not Z*") = []
{
    for (auto codePoint = char32_t {0x0009}; codePoint <= 0x000D; ++codePoint)
        check(isWhitespace(codePoint));

    check(isWhitespace(U' '));
    check(isWhitespace(0x0085));
    check(isWhitespace(0x00A0));
    check(isWhitespace(0x1680));
    check(isWhitespace(0x2028));
    check(isWhitespace(0x2029));
    check(isWhitespace(0x202F));
    check(isWhitespace(0x205F));
    check(isWhitespace(0x3000));

    check(!isWhitespace(0x200B));
    check(!isWhitespace(0xFEFF));
    check(!isWhitespace(U'a'));
};

auto whitespaceVersusSeparator = test("Tab is White_Space but not a separator") = []
{
    check(!isSeparator(0x0009));
    check(isWhitespace(0x0009));
    check(isSeparator(0x00A0));
    check(isWhitespace(0x00A0));
};

auto whitespaceCount = test("White_Space has exactly 25 code points") = []
{
    auto count = 0;

    for (auto codePoint = char32_t {0}; codePoint <= 0x10FFFF; ++codePoint)
        if (isWhitespace(codePoint))
            ++count;

    check(count == 25);
};

auto decodeOneByte = test("decodeUtf8: one-byte sequence") = []
{
    auto decoded = decodeUtf8("a", 0);
    check(decoded.valid);
    check(decoded.value == U'a');
    check(decoded.byteLength == 1);
};

auto decodeTwoBytes = test("decodeUtf8: two-byte sequence") = []
{
    auto decoded = decodeUtf8("\xC3\xA9", 0);
    check(decoded.valid);
    check(decoded.value == 0xE9);
    check(decoded.byteLength == 2);
};

auto decodeThreeBytes = test("decodeUtf8: three-byte sequence") = []
{
    auto decoded = decodeUtf8("\xE4\xBD\xA0", 0);
    check(decoded.valid);
    check(decoded.value == 0x4F60);
    check(decoded.byteLength == 3);
};

auto decodeFourBytes = test("decodeUtf8: four-byte sequence") = []
{
    auto decoded = decodeUtf8("\xF0\x9F\x98\x80", 0);
    check(decoded.valid);
    check(decoded.value == 0x1F600);
    check(decoded.byteLength == 4);
};

auto decodeLoneContinuation = test("decodeUtf8: lone continuation byte") = []
{
    auto decoded = decodeUtf8("\x80", 0);
    check(!decoded.valid);
    check(decoded.value == 0x80);
    check(decoded.byteLength == 1);
};

auto decodeTruncated = test("decodeUtf8: truncated sequence") = []
{
    auto decoded = decodeUtf8("\xE4\xBD", 0);
    check(!decoded.valid);
    check(decoded.value == 0xE4);
    check(decoded.byteLength == 1);
};

auto decodeOverlong = test("decodeUtf8: overlong encoding") = []
{
    auto decoded = decodeUtf8("\xC0\x80", 0);
    check(!decoded.valid);
    check(decoded.value == 0xC0);
    check(decoded.byteLength == 1);
};

auto decodeEncodedSurrogate = test("decodeUtf8: surrogate encoded as UTF-8") = []
{
    auto decoded = decodeUtf8("\xED\xA0\x80", 0);
    check(!decoded.valid);
    check(decoded.value == 0xED);
    check(decoded.byteLength == 1);
};

auto decodeAboveMax = test("decodeUtf8: above U+10FFFF") = []
{
    auto decoded = decodeUtf8("\xF4\x90\x80\x80", 0);
    check(!decoded.valid);
    check(decoded.value == 0xF4);
    check(decoded.byteLength == 1);
};

auto decodeAtPosition = test("decodeUtf8: decodes at an interior position") = []
{
    auto text = std::string_view {"ab\xE4\xBD\xA0z"};

    auto first = decodeUtf8(text, 1);
    check(first.valid);
    check(first.value == U'b');
    check(first.byteLength == 1);

    auto middle = decodeUtf8(text, 2);
    check(middle.valid);
    check(middle.value == 0x4F60);
    check(middle.byteLength == 3);

    auto last = decodeUtf8(text, 5);
    check(last.valid);
    check(last.value == U'z');
    check(last.byteLength == 1);
};

auto decodePastEnd = test("decodeUtf8: position at or past the end") = []
{
    auto decoded = decodeUtf8("abc", 3);
    check(!decoded.valid);
    check(decoded.byteLength == 1);
};

auto appendLengths = test("appendUtf8 produces 1, 2, 3 and 4 byte encodings") = []
{
    check(encoded(U'a') == "a");
    check(encoded(0xE9) == "\xC3\xA9");
    check(encoded(0x4F60) == "\xE4\xBD\xA0");
    check(encoded(0x1F600) == "\xF0\x9F\x98\x80");
};

auto appendAppends = test("appendUtf8 appends rather than replaces") = []
{
    auto text = std::string {"x"};
    appendUtf8(text, 0xE9);
    appendUtf8(text, U'y');
    check(text == "x\xC3\xA9y");
};

auto roundTripSweep =
    test("appendUtf8 / decodeUtf8 round trip over all code points") = []
{
    auto text = std::string {};
    auto failures = 0;

    for (auto codePoint = char32_t {0}; codePoint <= 0x10FFFF; ++codePoint)
    {
        if (codePoint >= 0xD800 && codePoint <= 0xDFFF)
            continue;

        text.clear();
        appendUtf8(text, codePoint);

        auto decoded = decodeUtf8(text, 0);

        auto expectedLength = codePoint < 0x80      ? 1
                              : codePoint < 0x800   ? 2
                              : codePoint < 0x10000 ? 3
                                                    : 4;

        if (!decoded.valid || decoded.value != codePoint
            || decoded.byteLength != expectedLength
            || text.size() != (std::size_t) expectedLength)
            ++failures;
    }

    check(failures == 0);
};

auto categorySweep = test("generalCategory answers for every code point") = []
{
    auto letters = 0;

    for (auto codePoint = char32_t {0}; codePoint <= 0x10FFFF; ++codePoint)
        if (isLetter(codePoint))
            ++letters;

    check(letters > 0);
    check(letters < 0x110000);
};

auto surrogatesAreCs = test("the whole surrogate range is Cs") = []
{
    for (auto codePoint = char32_t {0xD800}; codePoint <= 0xDFFF; ++codePoint)
        check(generalCategory(codePoint) == C::Surrogate);
};

auto asciiTable = test("generalCategory over the whole of ASCII") = []
{
    constexpr std::string_view expected[128] = {
        "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc",
        "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Cc",
        "Cc", "Cc", "Cc", "Cc", "Cc", "Cc", "Zs", "Po", "Po", "Po", "Sc", "Po", "Po",
        "Po", "Ps", "Pe", "Po", "Sm", "Po", "Pd", "Po", "Po", "Nd", "Nd", "Nd", "Nd",
        "Nd", "Nd", "Nd", "Nd", "Nd", "Nd", "Po", "Po", "Sm", "Sm", "Sm", "Po", "Po",
        "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu",
        "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu", "Lu",
        "Ps", "Po", "Pe", "Sk", "Pc", "Sk", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll",
        "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ll",
        "Ll", "Ll", "Ll", "Ll", "Ll", "Ll", "Ps", "Sm", "Pe", "Sm", "Cc",
    };

    for (auto codePoint = 0; codePoint < 128; ++codePoint)
        check(shortName(generalCategory((char32_t) codePoint))
              == expected[codePoint]);
};
