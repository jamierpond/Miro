#pragma once

#include "../YAML/Yaml.h"
#include "ConstexprJson.h"
#include "ConstexprYaml.h"
#include "JsonReflector.h"
#include "ReflectContainers.h"
#include "ReflectDispatch.h"
#include "TypeName.h"
#include "XmlReflector.h"

#include <string>
#include <string_view>
#include <type_traits>

namespace Miro
{

template <typename T>
JSON toJSON(const T& value)
{
    auto json = JSON {};
    auto ref = JsonReflector {json, Detail::topLevelOptions<T>(Mode::Save)};
    Detail::reflectValue(ref, const_cast<T&>(value));
    return json;
}

template <typename T>
void fromJSON(T& value, const JSON& json)
{
    auto mutableJson = json;
    auto ref = JsonReflector {mutableJson, Detail::topLevelOptions<T>(Mode::Load)};
    Detail::reflectValue(ref, value);
}

template <typename T>
T createFromJSON(const JSON& json)
{
    auto value = T {};
    fromJSON(value, json);
    return value;
}

// toJSONString / fromJSONString / createFromJSONString are usable in
// constant expressions for types whose reflect() and constructor are
// constexpr and whose fields are constexpr-reflectable (primitives,
// std::string, std::vector / std::array / std::optional, enums, and
// nested such types). MIRO_REFLECT already generates a constexpr
// reflect(). At compile time the serde bypasses Json::Value (std::map
// can't run in a constant expression) and goes through the
// Detail::ConstexprJson mirror, which prints and parses identically;
// at runtime nothing changes.
template <typename T>
constexpr std::string toJSONString(const T& value, int indent = 0)
{
    if (std::is_constant_evaluated())
        return Detail::ConstexprJson::toText(value, indent);

    return Json::print(toJSON(value), indent);
}

template <typename T>
void logJSON(const T& value, int indent = 4)
{
    Json::log(toJSON(value), indent);
}

template <typename T>
constexpr void fromJSONString(T& value, std::string_view jsonString)
{
    if (std::is_constant_evaluated())
    {
        Detail::ConstexprJson::fromText(value, jsonString);
        return;
    }

    fromJSON(value, Json::parse(jsonString));
}

template <typename T>
constexpr T createFromJSONString(std::string_view jsonString)
{
    auto value = T {};
    fromJSONString(value, jsonString);
    return value;
}

template <typename T>
Xml::Node toXML(const T& value)
{
    auto root = Xml::Node {.name = std::string(Detail::typeNameOf<T>())};
    auto ref = XmlReflector {root, Detail::topLevelOptions<T>(Mode::Save)};
    Detail::reflectValue(ref, const_cast<T&>(value));
    return root;
}

template <typename T>
void fromXML(T& value, const Xml::Node& node)
{
    auto mutableNode = node;
    auto ref = XmlReflector {mutableNode, Detail::topLevelOptions<T>(Mode::Load)};
    Detail::reflectValue(ref, value);
}

template <typename T>
T createFromXML(const Xml::Node& node)
{
    auto value = T {};
    fromXML(value, node);
    return value;
}

template <typename T>
std::string toXMLString(const T& value, int indent = 0)
{
    return Xml::print(toXML(value), indent);
}

template <typename T>
void fromXMLString(T& value, std::string_view xmlString)
{
    fromXML(value, Xml::parse(xmlString));
}

template <typename T>
T createFromXMLString(std::string_view xmlString)
{
    return createFromXML<T>(Xml::parse(xmlString));
}

// YAML shares the JSON value model (Yaml::Value is Json::Value), so
// the reflection walk reuses JsonReflector — only the string layer
// (Yaml::parse / Yaml::print) is YAML-specific.
template <typename T>
YAML toYAML(const T& value)
{
    return toJSON(value);
}

template <typename T>
void fromYAML(T& value, const YAML& yaml)
{
    fromJSON(value, yaml);
}

template <typename T>
T createFromYAML(const YAML& yaml)
{
    return createFromJSON<T>(yaml);
}

// Like their JSON siblings, the YAML string functions are usable in
// constant expressions for constexpr-reflectable types; at compile
// time they route through the Detail::ConstexprYaml mirror, at
// runtime nothing changes.
template <typename T>
constexpr std::string toYAMLString(const T& value, int indent = 2)
{
    if (std::is_constant_evaluated())
        return Detail::ConstexprYaml::toText(value, indent);

    return Yaml::print(toYAML(value), indent);
}

template <typename T>
void logYAML(const T& value, int indent = 2)
{
    Yaml::log(toYAML(value), indent);
}

template <typename T>
constexpr void fromYAMLString(T& value, std::string_view yamlString)
{
    if (std::is_constant_evaluated())
    {
        Detail::ConstexprYaml::fromText(value, yamlString);
        return;
    }

    fromYAML(value, Yaml::parse(yamlString));
}

template <typename T>
constexpr T createFromYAMLString(std::string_view yamlString)
{
    auto value = T {};
    fromYAMLString(value, yamlString);
    return value;
}

} // namespace Miro
