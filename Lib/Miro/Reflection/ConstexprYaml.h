#pragma once

#include "../YAML/Generic.h"
#include "ConstexprJson.h"

#include <string>
#include <string_view>

// Compile-time YAML serde backend: the generic YAML parser / printer
// (YAML/Generic.h) instantiated with the constexpr document mirror
// from ConstexprJson.h, plus the reflect plumbing shared with the
// JSON backend. Same algorithms as the runtime path, identical bytes
// by construction.

namespace Miro::Detail::ConstexprYaml
{

using ConstexprJson::Value;

constexpr Value resolvePlainScalar(std::string_view tokenToUse)
{
    return Yaml::Generic::resolvePlainScalar<Value>(tokenToUse);
}

constexpr Value parse(std::string_view inputToUse)
{
    return Yaml::Generic::parse<Value>(inputToUse);
}

constexpr std::string print(const Value& valueToUse, int indentToUse)
{
    return Yaml::Generic::print(valueToUse, indentToUse);
}

// --- Entry points (called from Serialize.h during constant
// evaluation) ---

template <typename T>
constexpr std::string toText(const T& value, int indent)
{
    return Yaml::Generic::print(ConstexprJson::reflectToValue(value), indent);
}

template <typename T>
constexpr void fromText(T& value, std::string_view text)
{
    ConstexprJson::loadFromValue(value, parse(text));
}

} // namespace Miro::Detail::ConstexprYaml
