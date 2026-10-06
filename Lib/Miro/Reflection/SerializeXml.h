#pragma once

#include "ReflectContainers.h"
#include "ReflectDispatch.h"
#include "ReflectJson.h"
#include "TypeName.h"
#include "XmlReflector.h"

#include <string>
#include <string_view>
#include <utility>

// XML counterparts of the JSON helpers in Serialize.h. Kept separate
// so JSON-only consumers don't compile the XML layer. Like the JSON
// helpers, each takes an optional CustomOptions drilled down to every
// nested reflector (see Serialize.h).
//
// ReflectJson.h comes along because raw-JSON fields classify as
// Shape::Raw, and shapeOf() has to agree about that in every TU — an
// <Miro/Xml.h>-only consumer must see the same classification a
// <Miro/Reflect.h> consumer does.

namespace Miro
{

template <typename T>
Xml::Node toXML(const T& value, CustomOptions custom = {})
{
    auto root = Xml::Node {.name = std::string(Detail::typeNameOf<T>())};
    auto ref = XmlReflector {
        root,
        Detail::topLevelOptions<T>(Mode::Save, /*schema=*/false, std::move(custom))};
    Detail::reflectValue(ref, const_cast<T&>(value));
    return root;
}

template <typename T>
void fromXML(T& value, const Xml::Node& node, CustomOptions custom = {})
{
    auto mutableNode = node;
    auto ref = XmlReflector {
        mutableNode,
        Detail::topLevelOptions<T>(Mode::Load, /*schema=*/false, std::move(custom))};
    Detail::reflectValue(ref, value);
}

template <typename T>
T createFromXML(const Xml::Node& node, CustomOptions custom = {})
{
    auto value = T {};
    fromXML(value, node, std::move(custom));
    return value;
}

template <typename T>
std::string toXMLString(const T& value, int indent = 0, CustomOptions custom = {})
{
    return Xml::print(toXML(value, std::move(custom)), indent);
}

template <typename T>
void fromXMLString(T& value, std::string_view xmlString, CustomOptions custom = {})
{
    fromXML(value, Xml::parse(xmlString), std::move(custom));
}

template <typename T>
T createFromXMLString(std::string_view xmlString, CustomOptions custom = {})
{
    return createFromXML<T>(Xml::parse(xmlString), std::move(custom));
}

} // namespace Miro
