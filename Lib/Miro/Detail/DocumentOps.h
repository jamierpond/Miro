#pragma once

#include "../JSON/Json.h"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>

// Construction seams the generic document parsers (JSON/Generic.h,
// YAML/Generic.h) use to build a value tree without knowing the
// concrete model. These are the overloads for the runtime Json::Value
// (std::map-backed objects, EA::Vector arrays); the compile-time
// mirror in Reflection/ConstexprJson.h provides constexpr overloads
// with the same names, found through ADL. Object insertion is
// first-occurrence-wins, matching std::map::emplace.

namespace Miro::Json
{

inline void setEmptyArray(Value& value)
{
    value.data.emplace<Array>();
}

inline void setEmptyObject(Value& value)
{
    value = Value {Object {}};
}

inline void reserveArray(Value& value, std::size_t count)
{
    std::get<Array>(value.data).reserve(count);
}

inline void appendElement(Value& value, Value element)
{
    std::get<Array>(value.data).add(std::move(element));
}

inline void insertMember(Value& value, std::string key, Value member)
{
    value.asObject().emplace(std::move(key), std::move(member));
}

} // namespace Miro::Json
