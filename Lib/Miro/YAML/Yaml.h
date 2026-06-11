#pragma once

#include "../JSON/Json.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace Miro::Yaml
{

// Minimal YAML document layer over the JSON value model.
//
// YAML's data model (null / bool / number / string / sequence /
// mapping) matches Miro::Json exactly, so the YAML backend reuses
// Json::Value as its document type — only the syntax layer here is
// YAML-specific. The reflection layer therefore also reuses
// JsonReflector; see toYAML / fromYAML in Reflection/Serialize.h.
//
// Parser scope is the subset Miro emits, plus common hand-written
// YAML: block mappings and sequences, compact "- key: value"
// sequence entries, single-line flow collections ([...] and {...}),
// plain / single-quoted / double-quoted scalars (with JSON-style
// escapes in double quotes), comments, and a leading "---" document
// marker. Block scalars (| and >), anchors / aliases / tags,
// multi-line flow collections, and multi-document streams are out
// of scope and produce a ParseError.
using Value = Json::Value;
using Null = Json::Null;
using Array = Json::Array;
using Object = Json::Object;

class ParseError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Resolves a plain (unquoted) scalar token the way the parser does:
// null / bool / number forms produce typed values, anything else a
// string. Exposed so the printer can decide when a string needs
// quoting to survive a round-trip.
Value resolvePlainScalar(std::string_view tokenToUse);

Value parse(std::string_view inputToUse);

// Block style by default; indentToUse <= 0 emits single-line flow
// style instead. Block style needs at least two columns per level,
// so smaller positive indents are clamped to 2.
std::string print(const Value& valueToUse, int indentToUse = 2);
void log(const Value& valueToUse, int indentToUse = 2);

} // namespace Miro::Yaml

namespace Miro
{
using YAML = Yaml::Value;
} // namespace Miro
