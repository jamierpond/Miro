// Self-containment check: <Miro/Unicode.h> must compile as the first
// and only Miro include. The test body is a minimal smoke of the
// character-property layer; the real assertion is that this TU
// compiles at all.

#include <Miro/Unicode.h>

#include <NanoTest/NanoTest.h>

using namespace nano;

auto unicodeEntryHeader = test("Entry header: Miro/Unicode.h is self-contained") = []
{
    check(Miro::Unicode::generalCategory(U'A')
          == Miro::Unicode::GeneralCategory::UppercaseLetter);
    check(Miro::Unicode::isLetter(U'A'));
};
