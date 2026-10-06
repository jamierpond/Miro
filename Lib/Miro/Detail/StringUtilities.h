#pragma once

#include "../Containers.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Miro::Detail
{

bool isAsciiIdentStart(char c);
bool isAsciiIdentPart(char c);
bool isAsciiIdentifier(std::string_view name);

bool isJsIdentStart(char c);
bool isJsIdentPart(char c);
bool isJsIdentifier(std::string_view name);

Vector<std::string> splitOn(std::string_view input, std::string_view delimiter);

std::string
    replaceAll(std::string_view input, std::string_view from, std::string_view to);

std::string trimAsciiWhitespace(std::string_view input);

std::string makeIndent(int width, int depth);

// Parses the whole of `text` as a decimal integer: no sign slop, no
// trailing characters. Formats with no number kind hand integers over
// as text (an XML attribute is always a string), so an integer-format
// enum or an integral union tag still has to recognise "2".
std::optional<std::int64_t> parseWholeInteger(std::string_view text);

} // namespace Miro::Detail
