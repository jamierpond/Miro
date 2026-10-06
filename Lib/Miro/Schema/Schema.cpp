#include "Schema.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace Miro
{

using TypeTree::TypeNode;

namespace
{

std::string_view jsonSchemaPrimitive(TypeTree::PrimitiveKind kind)
{
    switch (kind)
    {
        case TypeTree::PrimitiveKind::Boolean:
            return "boolean";
        case TypeTree::PrimitiveKind::String:
            return "string";
        case TypeTree::PrimitiveKind::Number:
            return "number";
        case TypeTree::PrimitiveKind::Integer:
        case TypeTree::PrimitiveKind::Int64:
            return "integer";
    }
    return "string";
}

// Renders a single TypeNode as a JSON Schema fragment. Named types
// (Object, Enum) become {"$ref": "#/$defs/<typeName>"}; the bodies live
// in $defs and are emitted separately. Anonymous nodes (top-level
// primitives, vectors, maps with no named root) get an inline body.
JSON renderJsonNode(const TypeNode& node);

void writeRequired(const TypeNode& node, JSON& out)
{
    auto required = Json::Array {};

    // Nullable fields have always been left out; an omittable one may
    // legitimately not appear at all, which is what "required" is about.
    for (auto& field: node.fields)
        if (!field.type->optional && !field.type->omittable)
            required.emplace_back(field.name);

    if (!required.empty())
        out.asObject()["required"] = JSON {std::move(required)};
}

JSON renderJsonObjectBody(const TypeNode& node)
{
    auto out = JSON {Json::Object {}};
    out.asObject()["type"] = JSON {std::string {"object"}};

    auto props = JSON {Json::Object {}};
    for (auto& field: node.fields)
        props.asObject()[field.name] = renderJsonNode(*field.type);

    out.asObject()["properties"] = std::move(props);

    writeRequired(node, out);

    return out;
}

// Names of an integer enum's members. JSON Schema has no keyword for
// them, so they go out under the conventional "x-enumNames" extension —
// validators ignore it, code generators can pick the names back up.
void writeEnumNames(const TypeNode& node, Json::Object& obj)
{
    auto names = Json::Array {};
    names.reserve(node.enumValues.size());

    for (auto& name: node.enumValues)
        names.emplace_back(name);

    obj["x-enumNames"] = JSON {std::move(names)};
}

JSON renderJsonIntegerEnumBody(const TypeNode& node)
{
    auto out = JSON {Json::Object {}};
    auto& obj = out.asObject();
    obj["type"] = JSON {std::string {"integer"}};

    if (!node.enumNumbers.empty())
    {
        auto values = Json::Array {};
        values.reserve(node.enumNumbers.size());

        for (auto number: node.enumNumbers)
            values.emplace_back(static_cast<double>(number));

        obj["enum"] = JSON {std::move(values)};
        writeEnumNames(node, obj);
    }

    return out;
}

JSON renderJsonEnumBody(const TypeNode& node)
{
    if (node.enumIsInteger)
        return renderJsonIntegerEnumBody(node);

    auto out = JSON {Json::Object {}};
    auto& obj = out.asObject();
    obj["type"] = JSON {std::string {"string"}};

    if (!node.enumValues.empty())
    {
        auto values = Json::Array {};
        values.reserve(node.enumValues.size());
        for (auto& v: node.enumValues)
            values.emplace_back(v);
        obj["enum"] = JSON {std::move(values)};
    }

    return out;
}

void markNullable(JSON& out, bool optional)
{
    if (optional)
        out.asObject()["nullable"] = JSON {true};
}

JSON makeRefTo(const std::string& typeName)
{
    auto out = JSON {Json::Object {}};
    out.asObject()["$ref"] = JSON {std::string {"#/$defs/"} + typeName};
    return out;
}

JSON renderJsonPrimitive(const TypeNode& node)
{
    auto out = JSON {Json::Object {}};
    out.asObject()["type"] =
        JSON {std::string {jsonSchemaPrimitive(node.primitive)}};
    return out;
}

JSON renderJsonArrayBody(const TypeNode& node)
{
    auto out = JSON {Json::Object {}};
    auto& obj = out.asObject();

    obj["type"] = JSON {std::string {"array"}};
    obj["items"] = renderJsonNode(*node.inner);

    if (node.minItems)
        obj["minItems"] = JSON {static_cast<double>(*node.minItems)};
    if (node.maxItems)
        obj["maxItems"] = JSON {static_cast<double>(*node.maxItems)};

    return out;
}

JSON renderJsonMapBody(const TypeNode& node)
{
    auto out = JSON {Json::Object {}};
    auto& obj = out.asObject();

    obj["type"] = JSON {std::string {"object"}};
    obj["additionalProperties"] = renderJsonNode(*node.inner);

    return out;
}

// `{"properties": {"type": {"const": 2}}, "required": ["type"]}` — the
// half of a union arm that pins the discriminator.
JSON renderJsonTagConstraint(const TypeNode& node, const TypeNode::Variant& variant)
{
    auto constant = JSON {Json::Object {}};
    constant.asObject()["const"] =
        variant.tagIsString ? JSON {variant.tag}
                            : JSON {std::strtod(variant.tag.c_str(), nullptr)};

    auto props = JSON {Json::Object {}};
    props.asObject()[node.tagKey] = std::move(constant);

    auto required = Json::Array {};
    required.emplace_back(node.tagKey);

    auto out = JSON {Json::Object {}};
    auto& obj = out.asObject();
    obj["type"] = JSON {std::string {"object"}};
    obj["properties"] = std::move(props);
    obj["required"] = JSON {std::move(required)};

    return out;
}

JSON renderJsonUnionArm(const TypeNode& node, const TypeNode::Variant& variant)
{
    auto parts = Json::Array {};
    parts.emplace_back(renderJsonTagConstraint(node, variant));
    parts.emplace_back(renderJsonNode(*variant.type));

    auto out = JSON {Json::Object {}};
    out.asObject()["allOf"] = JSON {std::move(parts)};

    return out;
}

// A discriminated union is a oneOf over "tag pinned to this literal,
// and the alternative's own body". Plain keys the same reflect() body
// wrote next to the discriminator apply to every arm, so they join as
// an outer allOf.
JSON renderJsonUnionBody(const TypeNode& node)
{
    auto arms = Json::Array {};
    for (auto& variant: node.variants)
        arms.emplace_back(renderJsonUnionArm(node, variant));

    auto oneOf = JSON {Json::Object {}};
    oneOf.asObject()["oneOf"] = JSON {std::move(arms)};

    if (node.fields.empty())
        return oneOf;

    auto parts = Json::Array {};
    parts.emplace_back(renderJsonObjectBody(node));
    parts.emplace_back(std::move(oneOf));

    auto out = JSON {Json::Object {}};
    out.asObject()["allOf"] = JSON {std::move(parts)};

    return out;
}

JSON renderJsonNode(const TypeNode& node)
{
    auto out = [&]
    {
        switch (node.shape)
        {
            case TypeNode::Shape::Primitive:
                return renderJsonPrimitive(node);

            case TypeNode::Shape::Object:
                return node.typeName.empty() ? renderJsonObjectBody(node)
                                             : makeRefTo(node.typeName);

            case TypeNode::Shape::Array:
                return renderJsonArrayBody(node);

            case TypeNode::Shape::Map:
                return renderJsonMapBody(node);

            case TypeNode::Shape::Enum:
                // Anonymous enum shouldn't happen — visitEnum always sets
                // a name — but render inline if it does.
                return node.typeName.empty() ? renderJsonEnumBody(node)
                                             : makeRefTo(node.typeName);

            case TypeNode::Shape::Any:
                // The empty schema is JSON Schema's "anything goes".
                return JSON {Json::Object {}};
            case TypeNode::Shape::Union:
                return node.typeName.empty() ? renderJsonUnionBody(node)
                                             : makeRefTo(node.typeName);
        }

        return JSON {Json::Object {}};
    }();

    markNullable(out, node.optional);
    return out;
}

} // namespace

namespace
{

// Walks every named type reachable from the roots and returns a $defs
// object. Mutates roots in place via prepareRoots — the per-node
// disambiguation pass rewrites typeName when distinct types share a
// short name.
JSON buildDefs(std::span<TypeNode> roots)
{
    auto ordered = TypeTree::prepareRoots(roots);

    auto defs = JSON {Json::Object {}};
    for (auto* node: ordered)
    {
        auto& obj = defs.asObject()[node->typeName];

        if (node->shape == TypeNode::Shape::Enum)
            obj = renderJsonEnumBody(*node);
        else if (node->shape == TypeNode::Shape::Union)
            obj = renderJsonUnionBody(*node);
        else
            obj = renderJsonObjectBody(*node);
    }

    return defs;
}

} // namespace

// Multi-root variant — used by the type-export runner to bundle every
// registered type into a single document. No type is privileged as
// "the schema"; consumers point at one with {"$ref": "#/$defs/X"}.
JSON formatJsonSchema(std::span<TypeNode> roots)
{
    auto defs = buildDefs(roots);

    auto out = JSON {Json::Object {}};
    if (!defs.asObject().empty())
        out.asObject()["$defs"] = std::move(defs);

    return out;
}

// Single-root: render the root (a $ref for named types, an inline body
// for top-level vectors / primitives) and attach $defs alongside. Used
// by `schemaOf<T>()`.
JSON formatJsonSchema(TypeNode& root)
{
    // Build $defs first — prepareRoots rewrites typeName during this
    // pass, and we need the renamed names when we render the root.
    auto defs = buildDefs(std::span {&root, 1});

    auto out = renderJsonNode(root);

    if (!defs.asObject().empty())
    {
        if (!out.isObject())
            out = JSON {Json::Object {}};
        out.asObject()["$defs"] = std::move(defs);
    }

    return out;
}

} // namespace Miro
