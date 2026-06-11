#pragma once

#include "../JSON/Json.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Miro
{
class Reflector;

struct Property
{
    template <typename T>
    constexpr void operator()(T& value);

    Reflector& reflector;
    std::string_view key;
};

struct Element
{
    template <typename T>
    constexpr void operator()(T& value);

    Reflector& reflector;
    std::size_t index;
};

enum class Mode
{
    Save,
    Load
};

enum class Shape
{
    Primitive,
    Object,
    Array,
    Map
};

enum class ValueKind
{
    Absent,
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

// Spawn-time configuration for a Reflector. Carried as a value member
// of the base, so most queries (mode, shape, schema, nullable) are
// non-virtual reads. When a parent spawns a child via atKey/atIndex,
// the dispatcher constructs the child's Options from the parent's —
// mode and schema propagate; shape and nullable are set per-child
// based on the value type being reflected.
struct Options
{
    Mode mode = Mode::Save;
    Shape shape = Shape::Primitive;
    bool nullable = false;
    bool schema = false;
};

// Identity of a named C++ type announced through reflection. `shortName`
// is the unqualified spelling (used as the default display name in
// generated output); `qualifiedName` is the compiler-derived full path
// from `__PRETTY_FUNCTION__`/`__FUNCSIG__` (e.g. "Ns::Inner::Foo"),
// stable per type and used as the dedup key when two types in different
// namespaces happen to share a short name.
struct TypeId
{
    std::string_view shortName;
    std::string_view qualifiedName;
};

// First-class primitive handle. Constructs implicitly from any built-in
// primitive — `ref.visit(myInt)` Just Works. To support a new primitive
// type, add another constructor + variant alternative below (or provide
// a free `makePrimitiveRef(T&)` overload and a templated constructor).
struct PrimitiveRef
{
    using Variant = std::variant<bool*, int*, double*, std::string*, std::int64_t*>;

    constexpr PrimitiveRef(bool& value)
        : data(&value)
    {
    }
    constexpr PrimitiveRef(int& value)
        : data(&value)
    {
    }
    constexpr PrimitiveRef(double& value)
        : data(&value)
    {
    }
    constexpr PrimitiveRef(std::string& value)
        : data(&value)
    {
    }
    constexpr PrimitiveRef(std::int64_t& value)
        : data(&value)
    {
    }

    Variant data;
};

// A Reflector represents exactly one slot in a tree. Configuration
// (mode, shape, schema, nullable) is committed at construction time
// via Options and stored in the base — most queries are non-virtual.
// Recursion happens via atKey/atIndex, which return a child reflector
// owned by this one.
//
// The non-virtual surface (and the defaults that a compile-time walk
// reaches) is constexpr so that a reflector whose overrides are
// themselves constexpr can drive a reflect() body during constant
// evaluation — see Reflection/ConstexprJson.h. Runtime reflectors are
// unaffected: an override need not be constexpr.
class Reflector
{
public:
    constexpr explicit Reflector(Options optsToUse)
        : opts(optsToUse)
    {
    }

    virtual ~Reflector() = default;

    constexpr Property operator[](std::string_view key) { return {*this, key}; }
    constexpr Element operator[](std::size_t index) { return {*this, index}; }

    constexpr const Options& options() const { return opts; }
    constexpr Mode mode() const { return opts.mode; }
    constexpr Shape shape() const { return opts.shape; }
    constexpr bool isSaving() const { return opts.mode == Mode::Save; }
    constexpr bool isLoading() const { return opts.mode == Mode::Load; }
    constexpr bool isSchema() const { return opts.schema; }
    constexpr bool isNullable() const { return opts.nullable; }

    // Per-slot operations that depend on the concrete reflector kind.
    virtual void visit(PrimitiveRef ref) = 0;
    virtual void writeNull() = 0;
    virtual ValueKind kind() const = 0;

    // Called by the dispatch right before invoking a reflectable type's
    // own reflect() body. `id` carries both the short and qualified C++
    // names. Reflectors that care about type identity (TypeScript
    // exporter, schema's $defs) override this. Returning false tells the
    // dispatcher to skip the body — used to break recursion when the
    // same type is already being walked further up the chain, or when
    // the reflector has emitted a name reference instead of inlining.
    constexpr virtual bool beginNamedType(TypeId /*id*/) { return true; }

    // Called by the enum dispatcher in schema mode with the enum's
    // identity and the ordered list of valid enumerator names.
    // Reflectors that want first-class enum output (e.g. the TypeScript
    // exporter) override this. The default falls back to a string slot,
    // which is what the JSON-Schema reflector has historically emitted.
    virtual void visitEnum(TypeId /*id*/, const Vector<std::string_view>& /*names*/)
    {
        auto placeholder = std::string {"enum"};
        visit(placeholder);
    }

    // Spawn a child reflector for a sub-slot. The returned reference is
    // owned by this reflector and remains valid only until the next
    // atKey/atIndex call on this reflector (or until this reflector is
    // destroyed). The child commits its own shape from childOpts at
    // construction.
    virtual Reflector& atKey(std::string_view key, Options childOpts) = 0;
    virtual Reflector& atIndex(std::size_t index, Options childOpts) = 0;

    // Iteration helpers — only meaningful inside an Array / Map slot.
    // Default to "empty" so reflectors with nothing useful to report
    // (e.g. SchemaReflector) need not override.
    virtual std::size_t arraySize() const { return 0; }
    virtual void resizeArray(std::size_t /*newSize*/) {}
    virtual Vector<std::string> mapKeys() const { return {}; }

    // Schema hint for fixed-size arrays. Called from the std::array /
    // EA::Array dispatcher with the compile-time N as both min and max.
    // Reflectors that don't care (the JSON reflector) keep the no-op
    // default; the schema reflector translates this into minItems/maxItems.
    virtual void setArrayBounds(std::size_t /*min*/, std::size_t /*max*/) {}

    // Gate called by reflectPolymorphic before it starts dispatching.
    // Data-walking reflectors (Json, Xml) override with a no-op; schema-
    // mode walkers (TypeReflector for Schema/TypeScript) keep the
    // default, which throws — emitting a partial shape from a default-
    // constructed polymorphic value would silently mislead callers, so
    // we surface the misuse immediately. `context` identifies the call
    // site for the error message.
    virtual void requirePolymorphicSupport(std::string_view context);

protected:
    Options opts;
};

} // namespace Miro
