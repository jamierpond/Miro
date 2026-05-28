#include "TypeScript.h"

#include "../Detail/StringUtilities.h"

#include <ResEmbed/ResEmbed.h>

#include <sstream>
#include <string>
#include <string_view>

namespace Miro::TypeScript
{

using TypeTree::TypeNode;

// Bare identifier when `name` is a valid JS identifier, otherwise a
// JSON-quoted string with `\` and `"` escaped. See the header.
std::string formatPropertyKey(std::string_view name)
{
    if (Detail::isJsIdentifier(name))
        return std::string {name};

    auto out = std::string {"\""};
    for (auto c: name)
    {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

namespace
{

std::string_view zodPrimitive(TypeTree::PrimitiveKind kind)
{
    switch (kind)
    {
        case TypeTree::PrimitiveKind::Boolean:
            return "z.boolean()";
        case TypeTree::PrimitiveKind::String:
            return "z.string()";
        case TypeTree::PrimitiveKind::Number:
            return "z.number()";
        case TypeTree::PrimitiveKind::Integer:
        case TypeTree::PrimitiveKind::Int64:
            return "z.number().int()";
    }
    return "z.unknown()";
}

std::string_view tsPrimitive(TypeTree::PrimitiveKind kind)
{
    switch (kind)
    {
        case TypeTree::PrimitiveKind::Boolean:
            return "boolean";
        case TypeTree::PrimitiveKind::String:
            return "string";
        case TypeTree::PrimitiveKind::Number:
        case TypeTree::PrimitiveKind::Integer:
        case TypeTree::PrimitiveKind::Int64:
            return "number";
    }
    return "unknown";
}

// ---------- Zod renderer ----------

std::string renderZod(const TypeNode& node);

std::string renderZodObjectInline(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "z.object({\n";

    for (auto& field: node.fields)
        out << "    " << formatPropertyKey(field.name) << ": "
            << renderZod(*field.type) << ",\n";

    out << "})";
    return out.str();
}

std::string renderZodEnumInline(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "z.enum([";

    auto first = true;
    for (auto& value: node.enumValues)
    {
        if (!first)
            out << ", ";
        first = false;
        out << "\"" << value << "\"";
    }

    out << "])";
    return out.str();
}

std::string renderZod(const TypeNode& node)
{
    auto base = std::string {};

    switch (node.shape)
    {
        case TypeNode::Shape::Primitive:
            base = std::string {zodPrimitive(node.primitive)};
            break;
        case TypeNode::Shape::Object:
            base =
                node.typeName.empty() ? renderZodObjectInline(node) : node.typeName;
            break;
        case TypeNode::Shape::Array:
            base = "z.array(" + renderZod(*node.inner) + ")";
            break;
        case TypeNode::Shape::Map:
            base = "z.record(z.string(), " + renderZod(*node.inner) + ")";
            break;
        case TypeNode::Shape::Enum:
            base = node.typeName.empty() ? renderZodEnumInline(node) : node.typeName;
            break;
    }

    if (node.optional)
        base += ".optional()";

    return base;
}

std::string declareZodNamed(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export const " << node.typeName << " = " << renderZodObjectInline(node)
        << ";\n";
    out << "export type " << node.typeName << " = z.infer<typeof " << node.typeName
        << ">;\n";
    return out.str();
}

std::string declareZodEnum(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export const " << node.typeName << " = " << renderZodEnumInline(node)
        << ";\n";
    out << "export type " << node.typeName << " = z.infer<typeof " << node.typeName
        << ">;\n";
    return out.str();
}

// ---------- Plain TypeScript renderer ----------

// Renders a node as a TypeScript type expression (no `?` — optionality
// at field-level is handled by the field separator). For non-field
// optional contexts we wrap with ` | undefined`.
std::string renderType(const TypeNode& node, bool fieldContext);

std::string renderTypeObjectInline(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "{\n";

    for (auto& field: node.fields)
    {
        auto separator = field.type->optional ? "?: " : ": ";
        out << "    " << formatPropertyKey(field.name) << separator
            << renderType(*field.type, /*fieldContext=*/true) << ";\n";
    }

    out << "}";
    return out.str();
}

std::string renderTypeEnumInline(const TypeNode& node)
{
    auto out = std::string {};
    auto first = true;

    for (auto& value: node.enumValues)
    {
        if (!first)
            out += " | ";
        first = false;
        out += "\"";
        out += value;
        out += "\"";
    }

    return out;
}

std::string renderType(const TypeNode& node, bool fieldContext)
{
    auto base = std::string {};

    switch (node.shape)
    {
        case TypeNode::Shape::Primitive:
            base = std::string {tsPrimitive(node.primitive)};
            break;
        case TypeNode::Shape::Object:
            base =
                node.typeName.empty() ? renderTypeObjectInline(node) : node.typeName;
            break;
        case TypeNode::Shape::Array:
            base = renderType(*node.inner, /*fieldContext=*/false) + "[]";
            break;
        case TypeNode::Shape::Map:
            base = "Record<string, " + renderType(*node.inner, false) + ">";
            break;
        case TypeNode::Shape::Enum:
            base =
                node.typeName.empty() ? renderTypeEnumInline(node) : node.typeName;
            break;
    }

    // In field context, optionality is conveyed by `field?:`. Outside a
    // field (e.g. the inner of an array), optional must be expressed as
    // a union with undefined.
    if (node.optional && !fieldContext)
        base = "(" + base + " | undefined)";

    return base;
}

std::string declareTypeNamed(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export interface " << node.typeName << " "
        << renderTypeObjectInline(node) << "\n";
    return out.str();
}

std::string declareTypeEnum(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export type " << node.typeName << " = " << renderTypeEnumInline(node)
        << ";\n";
    return out.str();
}

// True if the root node was emitted as its own top-level declaration by
// collectNamed — in that case we skip the default-export / Root-alias
// fallback to avoid a redundant second name for the same shape.
bool rootIsHoisted(const TypeNode& root)
{
    if (root.typeName.empty())
        return false;

    return root.shape == TypeNode::Shape::Object
           || root.shape == TypeNode::Shape::Enum;
}

} // namespace

std::string formatZodModule(std::span<TypeNode> roots)
{
    auto ordered = TypeTree::prepareRoots(roots);

    auto out = std::ostringstream {};
    out << "import { z } from \"zod\";\n\n";

    for (auto* node: ordered)
    {
        if (node->shape == TypeNode::Shape::Enum)
            out << declareZodEnum(*node) << "\n";
        else
            out << declareZodNamed(*node) << "\n";
    }

    return out.str();
}

std::string formatTypesModule(std::span<TypeNode> roots)
{
    auto ordered = TypeTree::prepareRoots(roots);

    auto out = std::ostringstream {};

    for (auto* node: ordered)
    {
        if (node->shape == TypeNode::Shape::Enum)
            out << declareTypeEnum(*node) << "\n";
        else
            out << declareTypeNamed(*node) << "\n";
    }

    return out.str();
}

std::string formatZodModule(TypeNode& root)
{
    auto out = formatZodModule(std::span<TypeNode> {&root, 1});

    // The bundled overload skips default exports (one-per-module rule);
    // the single-root path adds one for anonymous roots like top-level
    // vectors so the module isn't pointless.
    if (!rootIsHoisted(root))
        out += "export default " + renderZod(root) + ";\n";

    return out;
}

std::string formatTypesModule(TypeNode& root)
{
    auto out = formatTypesModule(std::span<TypeNode> {&root, 1});

    if (!rootIsHoisted(root))
        out +=
            "export type Root = " + renderType(root, /*fieldContext=*/false) + ";\n";

    return out;
}

std::string formatBridgeRuntime()
{
    return ResEmbed::get("BridgeRuntime.ts", "MiroResources").toString();
}

} // namespace Miro::TypeScript
