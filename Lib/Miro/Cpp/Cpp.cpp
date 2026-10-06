#include "Cpp.h"

#include <sstream>
#include <string>
#include <string_view>

namespace Miro::Cpp
{

using TypeTree::TypeNode;

namespace
{

std::string_view cppPrimitive(TypeTree::PrimitiveKind kind)
{
    switch (kind)
    {
        case TypeTree::PrimitiveKind::Boolean:
            return "bool";
        case TypeTree::PrimitiveKind::String:
            return "std::string";
        case TypeTree::PrimitiveKind::Number:
            return "double";
        case TypeTree::PrimitiveKind::Integer:
            return "int";
        case TypeTree::PrimitiveKind::Int64:
            return "std::int64_t";
    }
    return "int";
}

// Default initializer for primitive fields so the generated structs
// produce predictable values without explicit construction. Containers,
// optionals and omittables are already empty by default; named types
// fall back to the user's own default constructor.
std::string defaultInitFor(const TypeNode& field)
{
    if (field.optional || field.omittable)
        return {};

    if (field.shape != TypeNode::Shape::Primitive)
        return {};

    switch (field.primitive)
    {
        case TypeTree::PrimitiveKind::Boolean:
            return " = false";
        case TypeTree::PrimitiveKind::Number:
        case TypeTree::PrimitiveKind::Integer:
        case TypeTree::PrimitiveKind::Int64:
            return " = 0";
        case TypeTree::PrimitiveKind::String:
            return {};
    }

    return {};
}

std::string renderType(const TypeNode& node, Modes mode);

// Wraps `renderType(node)` in the C++ spelling of the node's
// optionality: `std::optional<...>` when the value may be null,
// `Miro::Omittable<...>` when the key may be missing. Used for fields
// and inner element types.
std::string renderTypeWithOptional(const TypeNode& node, Modes mode)
{
    auto base = renderType(node, mode);

    if (node.optional)
        base = "std::optional<" + base + ">";

    if (node.omittable)
    {
        // The pure header must not depend on Miro, so "may be absent"
        // collapses onto std::optional there — lossy on the wire, but it
        // compiles standalone. The Miro header keeps the distinction.
        if (mode == Modes::Miro)
            base = "Miro::Omittable<" + base + ">";
        else if (!node.optional)
            base = "std::optional<" + base + ">";
    }

    return base;
}

// A discriminated union has no dedicated C++ spelling in plain-struct
// output; std::variant lists the same alternatives, which is also what
// a Miro-side tagged union stores. The discriminator itself is dropped
// — it is derived from the active alternative, not a field.
std::string renderVariantType(const TypeNode& node, Modes mode)
{
    if (node.variants.empty())
        return "std::monostate";

    auto out = std::string {"std::variant<"};
    auto first = true;

    for (auto& variant: node.variants)
    {
        if (!first)
            out += ", ";
        first = false;
        out += renderType(*variant.type, mode);
    }

    return out + ">";
}

std::string renderType(const TypeNode& node, Modes mode)
{
    switch (node.shape)
    {
        case TypeNode::Shape::Primitive:
            return std::string {cppPrimitive(node.primitive)};
        case TypeNode::Shape::Object:
        case TypeNode::Shape::Enum:
            return node.typeName;
        case TypeNode::Shape::Union:
            return node.typeName.empty() ? renderVariantType(node, mode)
                                         : node.typeName;
        case TypeNode::Shape::Array:
            return "std::vector<" + renderTypeWithOptional(*node.inner, mode) + ">";
        case TypeNode::Shape::Map:
            return "std::map<std::string, "
                   + renderTypeWithOptional(*node.inner, mode) + ">";
        case TypeNode::Shape::Any:
            // Plain C++ has no spelling for "any JSON value", so both
            // flavours name Miro's own — a PureCPP header that carries
            // one of these does depend on Miro after all.
            return "Miro::JSON";
    }
    return "auto";
}

void emitStructFields(std::ostringstream& out, const TypeNode& node, Modes mode)
{
    for (auto& field: node.fields)
        out << "    " << renderTypeWithOptional(*field.type, mode) << " "
            << field.name << defaultInitFor(*field.type) << ";\n";
}

void emitReflectMacro(std::ostringstream& out, const TypeNode& node)
{
    if (!node.fields.empty())
        out << "\n";

    out << "    MIRO_REFLECT(";
    auto first = true;
    for (auto& field: node.fields)
    {
        if (!first)
            out << ", ";
        first = false;
        out << field.name;
    }
    out << ")\n";
}

std::string emitStruct(const TypeNode& node, Modes mode)
{
    auto out = std::ostringstream {};
    out << "struct " << node.typeName << "\n{\n";

    emitStructFields(out, node, mode);

    if (mode == Modes::Miro)
        emitReflectMacro(out, node);

    out << "};\n";
    return out.str();
}

std::string emitUnionAlias(const TypeNode& node, Modes mode)
{
    auto out = std::ostringstream {};
    out << "using " << node.typeName << " = " << renderVariantType(node, mode)
        << ";\n";
    return out.str();
}

std::string emitEnum(const TypeNode& node, Modes mode)
{
    auto out = std::ostringstream {};
    out << "enum class " << node.typeName << "\n{\n";

    for (auto i = 0; i < node.enumValues.size(); ++i)
    {
        if (i > 0)
            out << ",\n";

        out << "    " << node.enumValues[i];

        // Only integer-format enums put their numbers on the wire, so
        // only they need them pinned in the regenerated declaration.
        if (node.enumIsInteger)
            out << " = " << node.enumNumbers[i];
    }
    out << "\n};\n";

    // The wire format is part of the type: without this the regenerated
    // enum would save as names and no longer match the API it came from.
    if (node.enumIsInteger && mode == Modes::Miro)
        out << "\nMIRO_ENUM_AS_INTEGER(" << node.typeName << ")\n";

    return out.str();
}

} // namespace

std::string formatHeader(std::span<TypeNode> roots, Modes mode)
{
    auto ordered = TypeTree::prepareRoots(roots);

    auto out = std::ostringstream {};
    out << "#pragma once\n\n";
    out << "#include <cstdint>\n";
    out << "#include <map>\n";
    out << "#include <optional>\n";
    out << "#include <string>\n";
    out << "#include <variant>\n";
    out << "#include <vector>\n";

    if (mode == Modes::Miro)
        out << "\n#include <Miro/Miro.h>\n";

    out << "\n";

    for (auto* node: ordered)
    {
        if (node->shape == TypeNode::Shape::Enum)
            out << emitEnum(*node, mode) << "\n";
        else if (node->shape == TypeNode::Shape::Union)
            out << emitUnionAlias(*node, mode) << "\n";
        else
            out << emitStruct(*node, mode) << "\n";
    }

    return out.str();
}

std::string formatHeader(TypeNode& root, Modes mode)
{
    return formatHeader(std::span<TypeNode> {&root, 1}, mode);
}

} // namespace Miro::Cpp
