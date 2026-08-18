#pragma once

#include "../Containers.h"

#include <cstddef>
#include <cstdint>
#include <span>
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
    void operator()(T& value);

    Reflector& reflector;
    std::string_view key;
};

struct Element
{
    template <typename T>
    void operator()(T& value);

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

// User-supplied, format-agnostic payload threaded verbatim through an
// entire reflection walk. Seeded at the top-level toJSON/fromJSON (or
// toXML/fromXML) call and copied unchanged from a parent reflector to
// every child — so, unlike the per-child fields of Options, a value set
// at the root is visible to every nested type's reflect() body via
// reflector.customOptions(). Types that care can branch on it (e.g.
// carry a "session" tag down the tree and serialize differently for it);
// types that don't simply ignore it. Currently just a free-form string,
// but more fields can be added later without touching the propagation
// machinery.
struct CustomOptions
{
    std::string tag {};
};

// Spawn-time configuration for a Reflector. Carried as a value member
// of the base, so most queries (mode, shape, schema, nullable) are
// non-virtual reads. When a parent spawns a child via atKey/atIndex,
// the dispatcher constructs the child's Options from the parent's —
// mode, schema and custom propagate; shape and nullable are set per-child
// based on the value type being reflected.
struct Options
{
    Mode mode = Mode::Save;
    Shape shape = Shape::Primitive;
    bool nullable = false;
    bool schema = false;

    // Propagated parent -> child unchanged (see CustomOptions).
    CustomOptions custom {};
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

    PrimitiveRef(bool& value)
        : data(&value)
    {
    }
    PrimitiveRef(int& value)
        : data(&value)
    {
    }
    PrimitiveRef(double& value)
        : data(&value)
    {
    }
    PrimitiveRef(std::string& value)
        : data(&value)
    {
    }
    PrimitiveRef(std::int64_t& value)
        : data(&value)
    {
    }

    Variant data;
};

// A contiguous numeric array offered to the reflector as one unit. The
// container dispatch tries visitPacked() before walking elements one
// by one; a reflector that can bulk-encode (the binary backend) takes
// it and returns true, everyone else inherits the default false and
// gets the classic per-element walk. On Save the span is read; on Load
// the caller has already resized to arraySize() and the span is filled.
struct PackedArrayRef
{
    using Variant = std::variant<std::span<std::int32_t>,
                                 std::span<std::int64_t>,
                                 std::span<float>,
                                 std::span<double>>;

    Variant data;
};

// A Reflector represents exactly one slot in a tree. Configuration
// (mode, shape, schema, nullable) is committed at construction time
// via Options and stored in the base — most queries are non-virtual.
// Recursion happens via atKey/atIndex, which return a child reflector
// owned by this one.
class Reflector
{
public:
    explicit Reflector(Options optsToUse)
        : opts(optsToUse)
    {
    }

    virtual ~Reflector() = default;

    Property operator[](std::string_view key);
    Element operator[](std::size_t index);

    const Options& options() const { return opts; }
    const CustomOptions& customOptions() const { return opts.custom; }
    Mode mode() const { return opts.mode; }
    Shape shape() const { return opts.shape; }
    bool isSaving() const { return opts.mode == Mode::Save; }
    bool isLoading() const { return opts.mode == Mode::Load; }
    bool isSchema() const { return opts.schema; }
    bool isNullable() const { return opts.nullable; }

    // Per-slot operations that depend on the concrete reflector kind.
    virtual void visit(PrimitiveRef ref) = 0;
    virtual void writeNull() = 0;
    virtual ValueKind kind() const = 0;

    // Bulk fast path for contiguous numeric arrays (see PackedArrayRef).
    virtual bool visitPacked(PackedArrayRef /*ref*/) { return false; }

    // Called by the dispatch right before invoking a reflectable type's
    // own reflect() body. `id` carries both the short and qualified C++
    // names. Reflectors that care about type identity (TypeScript
    // exporter, schema's $defs) override this. Returning false tells the
    // dispatcher to skip the body — used to break recursion when the
    // same type is already being walked further up the chain, or when
    // the reflector has emitted a name reference instead of inlining.
    virtual bool beginNamedType(TypeId /*id*/) { return true; }

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
