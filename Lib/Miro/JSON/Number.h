#pragma once

#include <optional>
#include <string>
#include <string_view>

// The one place the JSON layer talks to the C library's floating-point
// conversions. Both strtod and printf spell the decimal point the way
// LC_NUMERIC says, and JSON only ever uses '.', so the translation lives
// here and the parser and printer stay locale-blind.

namespace Miro::Json::Detail
{

// Converts `text`, a JSON-spelled number with a '.' decimal point, and
// reads nothing past it. Returns nothing unless the whole of `text` is
// consumed.
std::optional<double> parseDouble(std::string_view text);

// Appends `number` as the shortest JSON spelling that reads back as the
// same double. `number` must be finite.
void appendShortestDouble(std::string& output, double number);

} // namespace Miro::Json::Detail
