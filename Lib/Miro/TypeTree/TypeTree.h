#pragma once

#include "../Reflection/ReflectDispatch.h"
#include "../Reflection/Reflector.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// A format-neutral description of a reflected C++ type. The TypeReflector
// walks any MIRO_REFLECT-enabled value and builds a TypeNode tree;
// downstream renderers (Zod, plain TypeScript, JSON Schema, future
// formats) consume the same tree, so the structural reflection logic
// is written once and the per-format work shrinks to "walk this tree
// and emit text/JSON".

namespace Miro::TypeTree
{

// Primitive flavour. Renderers map this to format-specific spellings
// (e.g. Boolean → "z.boolean()" / "boolean" / "{type:'boolean'}").
enum class PrimitiveKind
{
    Boolean,
    String,
    Number,
    Integer,
    Int64,
};

struct TypeNode
{
    // Nested so it doesn't shadow Miro::Shape (which lives on the
    // parent Reflector's Options). The IR superset adds Enum and Union
    // cases — both are object/primitive slots as far as the data
    // reflectors are concerned, but renderers need them apart — plus
    // Any for a raw JSON slot (a Miro::JSON field): it accepts any
    // value, so renderers spell it out as the format's "anything"
    // — {} in JSON Schema, `unknown` in TypeScript.
    enum class Shape
    {
        Primitive,
        Object,
        Array,
        Map,
        Enum,
        Any,
        Union,
    };

    Shape shape = Shape::Primitive;

    // The value may be null (std::optional / OwningPointer).
    bool optional = false;

    // The key may be missing entirely (Miro::Omittable). Independent of
    // `optional` — Omittable<std::optional<T>> sets both — and renders
    // as an optional property: `key?:` in TypeScript, left out of JSON
    // Schema's "required".
    bool omittable = false;

    // Primitive only.
    PrimitiveKind primitive = PrimitiveKind::String;

    // Object / Enum only: the display name emitted in generated source.
    // Defaults to the unqualified C++ type name; on collision the
    // disambiguation pass rewrites it to a sanitized qualified form.
    // Empty if anonymous (only meaningful for objects — enums always
    // come with a name from the dispatcher).
    std::string typeName;

    // Object / Enum only: the raw qualified C++ name from the compiler
    // (e.g. "Ns::Inner::Foo"). Stable per type and used as the dedup
    // key so two types in different namespaces with the same short
    // name don't silently collapse into one declaration.
    std::string qualifiedName;

    // Object only: ordered fields. A Union node may carry fields too,
    // when the same reflect() body wrote plain keys next to the
    // discriminator — renderers intersect them with the arms.
    struct Field
    {
        std::string name;
        OwningPointer<TypeNode> type;
    };
    Vector<Field> fields;

    // Union only: the discriminator's field name, and one arm per
    // registered alternative. `tag` is the arm's literal spelling and
    // `tagIsString` whether it is quoted on the wire.
    struct Variant
    {
        std::string tag;
        bool tagIsString = false;
        OwningPointer<TypeNode> type;
    };
    std::string tagKey;
    Vector<Variant> variants;

    // Array / Map only: the inner element type.
    OwningPointer<TypeNode> inner;

    // Enum only: ordered enumerator names.
    Vector<std::string> enumValues;

    // Enum only: true when the enum travels as its integer value rather
    // than its name (Miro::EnumFormat<E>::integer). Renderers emit a
    // numeric enum instead of a string one.
    bool enumIsInteger = false;

    // Integer-format enums only: the wire number of each enumerator,
    // parallel to enumValues. Empty for name-format enums, whose numbers
    // never reach the wire.
    Vector<std::int64_t> enumNumbers;

    // Array only, and only when fixed-size (std::array<T, N> or
    // EA::Array<T, N>): both bounds get N. nullopt for resizable
    // arrays (std::vector / EA::Vector). Schema renderer uses these
    // to emit minItems/maxItems.
    std::optional<std::size_t> minItems;
    std::optional<std::size_t> maxItems;
};

// Walks a default-constructed value of T via reflection and produces
// the structural TypeNode tree. Used by both the TypeScript and Schema
// renderers.
class TypeReflector final : public Reflector
{
public:
    TypeReflector(TypeNode& nodeToUse,
                  Options optsToUse,
                  TypeReflector* parentToUse = nullptr);
    ~TypeReflector() override;

    void visit(PrimitiveRef ref) override;
    void writeNull() override;
    ValueKind kind() const override;
    bool beginNamedType(TypeId id) override;
    void visitEnum(TypeId id, const Vector<std::string_view>& names) override;
    void visitIntegerEnum(TypeId id, const Vector<EnumEntry>& entries) override;
    Reflector& beginTaggedAlternative(std::string_view tagKey,
                                      const TagLiteral& tag,
                                      Options childOpts) override;

    Reflector& atKey(std::string_view key, Options childOpts) override;
    Reflector& atIndex(std::size_t index, Options childOpts) override;

    void setArrayBounds(std::size_t min, std::size_t max) override;

private:
    TypeNode& node;
    OwningPointer<TypeReflector> currentChild;

    // Parent in the spawn chain; nullptr at the root. Used by
    // beginNamedType to detect a recursive descent into the same C++
    // type — the slot becomes a name reference and the body is skipped.
    TypeReflector* parent;

    // Qualified name being walked at this slot. Set in beginNamedType
    // when the body is going to run; descendants check the parent chain
    // against this to detect cycles.
    std::string activeQualifiedName;

    Reflector& spawnChild(TypeNode& targetNode, Options childOpts);

    // Shared by the two visitEnum overrides: turns this slot into an
    // Enum node carrying the type's identity, with the value lists reset.
    void beginEnum(TypeId id);
};

template <typename T>
TypeNode buildTree()
{
    auto root = TypeNode {};
    auto opts = Detail::topLevelOptions<T>(Mode::Save, /*schema=*/true);
    auto reflector = TypeReflector {root, opts};
    auto value = T {};
    Detail::reflectValue(reflector, value);
    return root;
}

// Post-walk pass shared by every renderer: collects the named-type
// nodes reachable from any root in dependency order, and resolves
// display-name collisions by rewriting `typeName` on each affected
// TypeNode in place. The returned pointers reference nodes inside
// `roots` (or their subtrees) — valid for as long as `roots` is alive.
//
// Roots are passed mutably because the rewrite happens in place; if a
// caller doesn't want their trees touched, hand over a copy.
Vector<const TypeNode*> prepareRoots(std::span<TypeNode> roots);

} // namespace Miro::TypeTree
