#include "TypeScript.h"

#include "../CommandExport/ResolvedTypes.h"
#include "../Detail/StringUtilities.h"

#include <ResEmbed/ResEmbed.h>

#include <sstream>
#include <string>
#include <string_view>

namespace Miro::TypeScript
{

using TypeTree::TypeNode;

namespace
{

// A JS string literal with `\` and `"` escaped.
std::string quoteJsString(std::string_view text)
{
    auto out = std::string {"\""};
    for (auto c: text)
    {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Returns `name` ready to drop into a JS object literal or TS interface
// as a property key. Bare identifier when possible; otherwise a JSON-
// quoted string.
std::string formatPropertyKey(std::string_view name)
{
    if (Detail::isJsIdentifier(name))
        return std::string {name};

    return quoteJsString(name);
}

// A union arm's discriminator as a TypeScript literal type — bare for a
// numeric tag, quoted for a string or enum-name tag.
std::string tagLiteral(const TypeNode::Variant& variant)
{
    return variant.tagIsString ? quoteJsString(variant.tag) : variant.tag;
}

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

std::string renderZod(const TypeNode& node, bool fieldContext);

std::string renderZodObjectInline(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "z.object({\n";

    for (auto& field: node.fields)
        out << "    " << formatPropertyKey(field.name) << ": "
            << renderZod(*field.type, /*fieldContext=*/true) << ",\n";

    out << "})";
    return out.str();
}

// z.enum only accepts strings, so an integer-format enum becomes a union
// of numeric literals — z.infer then yields the same `0 | 1 | 2` the
// plain-types renderer produces. z.union needs at least two members, so
// the degenerate sizes get their own spellings.
std::string renderZodIntegerEnumInline(const TypeNode& node)
{
    if (node.enumNumbers.empty())
        return "z.number().int()";

    if (node.enumNumbers.size() == 1)
        return "z.literal(" + std::to_string(node.enumNumbers.front()) + ")";

    auto out = std::ostringstream {};
    out << "z.union([";

    auto first = true;
    for (auto number: node.enumNumbers)
    {
        if (!first)
            out << ", ";
        first = false;
        out << "z.literal(" << number << ")";
    }

    out << "])";
    return out.str();
}

std::string renderZodEnumInline(const TypeNode& node)
{
    if (node.enumIsInteger)
        return renderZodIntegerEnumInline(node);

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

// One arm of a discriminated union: the discriminator narrowed to this
// alternative's literal, intersected with the alternative's own shape.
// z.discriminatedUnion would be tighter but needs every option to be a
// literal ZodObject, which a $ref-style named arm isn't.
std::string renderZodUnionArm(const TypeNode& node, const TypeNode::Variant& variant)
{
    return "z.intersection(z.object({ " + formatPropertyKey(node.tagKey)
           + ": z.literal(" + tagLiteral(variant) + ") }), "
           + renderZod(*variant.type, /*fieldContext=*/false) + ")";
}

std::string renderZodUnionInline(const TypeNode& node)
{
    if (node.variants.empty())
        return "z.never()";

    auto arms = std::string {};
    auto first = true;
    for (auto& variant: node.variants)
    {
        if (!first)
            arms += ", ";
        first = false;
        arms += renderZodUnionArm(node, variant);
    }

    auto out = node.variants.size() == 1 ? arms : "z.union([" + arms + "])";

    // A reflect() body may write plain keys next to the discriminator;
    // those belong to every arm.
    if (!node.fields.empty())
        out = "z.intersection(" + renderZodObjectInline(node) + ", " + out + ")";

    return out;
}

std::string renderZod(const TypeNode& node, bool fieldContext)
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
        case TypeNode::Shape::Union:
            base =
                node.typeName.empty() ? renderZodUnionInline(node) : node.typeName;
            break;
        case TypeNode::Shape::Array:
            base = "z.array(" + renderZod(*node.inner, /*fieldContext=*/false) + ")";
            break;
        case TypeNode::Shape::Map:
            base = "z.record(z.string(), "
                   + renderZod(*node.inner, /*fieldContext=*/false) + ")";
            break;
        case TypeNode::Shape::Enum:
            base = node.typeName.empty() ? renderZodEnumInline(node) : node.typeName;
            break;
        case TypeNode::Shape::Any:
            base = "z.unknown()";
            break;
    }

    // Disengaged optional serializes to null. Field: .nullish() (null|undefined,
    // key optional); non-field (array/map value): .nullable(), no undefined.
    // A disengaged Omittable has no key at all, so on its own it is
    // .optional() (undefined, never null) — and .nullish() already covers
    // both when the two compose.
    if (node.optional)
        base += fieldContext ? ".nullish()" : ".nullable()";
    else if (node.omittable && fieldContext)
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

std::string declareZodUnion(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export const " << node.typeName << " = " << renderZodUnionInline(node)
        << ";\n";
    out << "export type " << node.typeName << " = z.infer<typeof " << node.typeName
        << ">;\n";
    return out.str();
}

// ---------- Plain TypeScript renderer ----------

// Renders a node as a TypeScript type expression.
std::string renderType(const TypeNode& node, bool fieldContext);

std::string renderTypeObjectInline(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "{\n";

    for (auto& field: node.fields)
    {
        auto mayBeMissing = field.type->optional || field.type->omittable;
        auto separator = mayBeMissing ? "?: " : ": ";
        out << "    " << formatPropertyKey(field.name) << separator
            << renderType(*field.type, /*fieldContext=*/true) << ";\n";
    }

    out << "}";
    return out.str();
}

std::string renderTypeIntegerEnumInline(const TypeNode& node)
{
    if (node.enumNumbers.empty())
        return "number";

    auto out = std::string {};
    auto first = true;

    for (auto number: node.enumNumbers)
    {
        if (!first)
            out += " | ";
        first = false;
        out += std::to_string(number);
    }

    return out;
}

std::string renderTypeEnumInline(const TypeNode& node)
{
    if (node.enumIsInteger)
        return renderTypeIntegerEnumInline(node);

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

// `{ type: 2 } & Button` — the discriminator narrowed to this arm's
// literal, intersected with the alternative's own shape. Narrowing on
// the tag then works the way it does for a hand-written TS union.
std::string renderTypeUnionArm(const TypeNode& node,
                               const TypeNode::Variant& variant)
{
    return "{ " + formatPropertyKey(node.tagKey) + ": " + tagLiteral(variant)
           + " } & " + renderType(*variant.type, /*fieldContext=*/false);
}

std::string renderTypeUnionInline(const TypeNode& node)
{
    if (node.variants.empty())
        return "never";

    auto out = std::string {};
    auto first = true;
    for (auto& variant: node.variants)
    {
        if (!first)
            out += " | ";
        first = false;
        out += "(" + renderTypeUnionArm(node, variant) + ")";
    }

    // A reflect() body may write plain keys next to the discriminator;
    // those belong to every arm.
    if (!node.fields.empty())
        out = renderTypeObjectInline(node) + " & (" + out + ")";

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
        case TypeNode::Shape::Union:
            base =
                node.typeName.empty() ? renderTypeUnionInline(node) : node.typeName;
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
        case TypeNode::Shape::Any:
            base = "unknown";
            break;
    }

    // Disengaged optional serializes to null. Field: `?:` carries absent, add
    // `| null`; non-field: wrap `(T | null)`, no `?:` to carry it.
    if (node.optional)
        base = fieldContext ? base + " | null" : "(" + base + " | null)";

    return base;
}

std::string declareTypeNamed(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export interface " << node.typeName << " "
        << renderTypeObjectInline(node) << "\n";
    return out.str();
}

// A named integer-format enum becomes a real TypeScript enum rather than
// a bare `0 | 1` alias: the numbers on the wire are meaningless on their
// own, so callers want `ChannelType.guildText` to write with. It still
// serves as a type in every position the string alias did.
std::string declareTypeIntegerEnum(const TypeNode& node)
{
    if (node.enumNumbers.empty())
        return "export type " + node.typeName + " = number;\n";

    auto out = std::ostringstream {};
    out << "export enum " << node.typeName << " {\n";

    for (auto i = 0; i < node.enumNumbers.size(); ++i)
        out << "    " << node.enumValues[i] << " = " << node.enumNumbers[i] << ",\n";

    out << "}\n";
    return out.str();
}

std::string declareTypeEnum(const TypeNode& node)
{
    if (node.enumIsInteger)
        return declareTypeIntegerEnum(node);

    auto out = std::ostringstream {};
    out << "export type " << node.typeName << " = " << renderTypeEnumInline(node)
        << ";\n";
    return out.str();
}

std::string declareTypeUnion(const TypeNode& node)
{
    auto out = std::ostringstream {};
    out << "export type " << node.typeName << " = " << renderTypeUnionInline(node)
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
           || root.shape == TypeNode::Shape::Enum
           || root.shape == TypeNode::Shape::Union;
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
        else if (node->shape == TypeNode::Shape::Union)
            out << declareZodUnion(*node) << "\n";
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
        else if (node->shape == TypeNode::Shape::Union)
            out << declareTypeUnion(*node) << "\n";
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
        out += "export default " + renderZod(root, /*fieldContext=*/false) + ";\n";

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

std::string formatEventsModule(std::span<TypeNode> typeRoots,
                               std::span<const TypeExport::EventInfo> events,
                               std::string_view baseName)
{
    auto resolved = CommandExport::resolveTypes(typeRoots);

    auto out = std::ostringstream {};

    // Only the event payloads below name `T`, so an API with no events would
    // import a module it never references — which fails any tsc running
    // noUnusedLocals.
    if (!events.empty())
        out << "import type * as T from './" << baseName << "';\n\n";

    out << "export interface Events\n{\n";
    for (auto& ev: events)
    {
        auto payload = resolved.nameFor(ev.payloadQualifiedName, ev.payloadTypeName);
        out << "    '" << ev.name << "': T." << payload << ";\n";
    }
    out << "}\n\n";

    out << "export type EventName = keyof Events;\n\n";

    out << "export interface EventBus\n"
           "{\n"
           "    subscribe<K extends EventName>(\n"
           "        name: K,\n"
           "        handler: (payload: Events[K]) => void,\n"
           "    ): () => void;\n"
           "}\n";

    return out.str();
}

} // namespace Miro::TypeScript
