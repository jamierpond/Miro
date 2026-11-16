#pragma once

#include "../Containers.h"
#include "TypeName.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Miro
{
class Reflector;

// Human-readable description. Two uses, one type:
//  - the first field attribute: ref["city"](city, Miro::Doc {"..."})
//    (Property::doc() is the older spelling of the same thing);
//  - the trailing argument of any ApiReflector::command overload:
//    r.command<&T::getWeather>(Miro::Doc {"Current weather for a city"});
// Held by view. As a field attribute it lives only for the call; the
// command descriptor copies it. Data reflectors ignore it; TypeTree
// records a field's Doc as the slot's TypeNode::description.
struct Doc
{
    std::string_view text;
};

// A non-owning, type-erased view of one field attribute, handed to
// Reflector::visitAttributes and valid only for the duration of that
// call. Attributes are arbitrary user types passed beside the value:
//   ref["speed"](speed, Range {0.f, 10.f}, Unit::hertz);
//
// Identity is the attribute type's qualified name, compared as text —
// never the address of a per-type static — so a host and a hot-reloaded
// library, each with its own copy of any static, still agree on what a
// Range is. (The flip side: two distinct types that print the same
// qualified name, e.g. same-named types in anonymous namespaces of two
// TUs, are indistinguishable.)
struct AttributeRef
{
    // Defined in ReflectDispatch.h, which knows how to reflect A.
    template <typename A>
    static AttributeRef of(const A& attribute);

    // The attribute as an A, or null when it is some other type.
    template <typename A>
    const A* as() const
    {
        if (typeName != Detail::qualifiedNameOf<A>())
            return nullptr;

        return static_cast<const A*>(value);
    }

    // False for an attribute Miro can't reflect (e.g. one holding a
    // function pointer): a live reflector can still read it with as<>,
    // but it can't be written out.
    bool isReflectable() const { return reflectFn != nullptr; }

    // Reflects a copy of the attribute as member `key` of `parent`,
    // which must be an Object slot — the child's shape then comes from
    // the attribute's own type, which only the typed side knows. A
    // no-op when the attribute isn't reflectable.
    void reflectInto(Reflector& parent, std::string_view key) const
    {
        if (reflectFn != nullptr)
            reflectFn(parent, key, value);
    }

    std::string_view typeName;
    const void* value = nullptr;
    void (*reflectFn)(Reflector&, std::string_view, const void*) = nullptr;
};

// Compile-time customization point: may attribute A be attached to a
// field of type T? Every combination is allowed by default. Specialize
// it to reject a misuse at the call site, e.g.
//   template <typename T>
//   struct Miro::AttributeAppliesTo<Range, T>
//   {
//       static constexpr bool value = std::is_arithmetic_v<T>;
//   };
// T is the field's declared type (std::optional<float>, not float).
template <typename A, typename T>
struct AttributeAppliesTo
{
    static constexpr bool value = true;
};

struct Property
{
    // Attaches a human-readable description to this field:
    //   ref["city"].doc("City and country")(city);
    // Same as passing Miro::Doc {"City and country"} as an attribute:
    // operator() delivers it to the child slot as the first Doc
    // attribute. Held by view, so it must outlive the reflect() call —
    // normally a string literal.
    Property doc(std::string_view text) const { return {reflector, key, text}; }

    // Reflects `value` as member `key`. Any trailing arguments are
    // attributes: the child slot sees them, in order, through one
    // visitAttributes() call before the value itself is visited. They
    // must be trivially destructible (so building them never allocates)
    // and pass AttributeAppliesTo<A, T>.
    template <typename T, typename... As>
    void operator()(T& value, const As&... attributes);

    Reflector& reflector;
    std::string_view key;
    std::string_view description {};
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
    Map,

    // A slot whose structure is decided at runtime by the value being
    // reflected rather than by its C++ type — a raw Miro::Json::Value
    // (or Json::Any) field, which can hold any of the kinds above.
    // Reflectors that commit a shape eagerly must pick something that
    // survives being overwritten by whatever the value turns out to be;
    // see JsonReflector::commitShape.
    Raw
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
// mode, schema and custom propagate; shape, nullable and omittable are
// set per-child based on the value type being reflected.
struct Options
{
    Mode mode = Mode::Save;
    Shape shape = Shape::Primitive;
    bool nullable = false;

    // Set for the child slot of an Omittable<T>: this child may decline
    // to exist at all, so a saving parent must not create the key until
    // the child calls markPresent(). Per-child like `shape` — it never
    // propagates further down. A reflector that doesn't implement
    // staging simply writes the key as it always did.
    bool omittable = false;

    bool schema = false;

    // Propagated parent -> child unchanged (see CustomOptions).
    CustomOptions custom {};
};

// The discriminator value of one alternative of an internally tagged
// union, in the format-neutral spelling schema walkers need. `text` is
// the literal ("2", "primary"); `isString` says whether it is quoted on
// the wire. Enum tags follow the normal enum path, so a named
// enumerator arrives here as its name with `isString` set.
struct TagLiteral
{
    std::string text;
    bool isString = false;
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

// The TypeId Miro announces for T before reflecting its body. A custom
// reflectValue that calls ref.beginNamedType itself uses this, so its
// type is identified exactly as a MIRO_REFLECT type would be.
template <typename T>
constexpr TypeId typeIdOf()
{
    return {Detail::typeNameOf<T>(), Detail::qualifiedNameOf<T>()};
}

// A non-owning, type-erased view of one reflectable object, handed to
// Reflector::visitObject and valid only for the duration of that call.
// `instance` points at the live T (mutable in both modes: a loading
// reflector may write through it). `reflect` walks it exactly as the
// dispatch would have had visitObject declined — beginNamedType, then
// the type's own reflect() body — into whatever reflector it is given,
// which must be an Object-shaped slot. It never calls visitObject on
// that reflector for the object itself, so a reflector can claim an
// object and still walk it from inside its own visitObject.
struct ObjectView
{
    // Defined in ReflectDispatch.h, which knows how to reflect T.
    template <typename T>
    static ObjectView of(T& object);

    // The object as a T, or null when it is some other type. Compared
    // by qualified name, like AttributeRef::as.
    template <typename T>
    T* as() const
    {
        if (type.qualifiedName != Detail::qualifiedNameOf<T>())
            return nullptr;

        return static_cast<T*>(instance);
    }

    void reflectInto(Reflector& target) const { reflect(target, instance); }

    TypeId type;
    void* instance = nullptr;
    void (*reflect)(Reflector&, void*) = nullptr;
};

// One enumerator of an integer-valued enum (see Miro::EnumFormat): the
// C++ spelling plus the number that actually travels on the wire.
// Name-valued enums announce names only, so they use a plain
// Vector<std::string_view> instead.
struct EnumEntry
{
    std::string_view name;
    std::int64_t value = 0;
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

// A contiguous array of numbers handed to a reflector in one call.
// Saving: the span holds the elements to write. Loading: the container
// was already resized to arraySize(), and the span is filled in place.
struct PackedArrayRef
{
    using Variant = std::variant<std::span<std::int8_t>,
                                 std::span<std::uint8_t>,
                                 std::span<std::int16_t>,
                                 std::span<std::uint16_t>,
                                 std::span<std::int32_t>,
                                 std::span<std::uint32_t>,
                                 std::span<std::int64_t>,
                                 std::span<std::uint64_t>,
                                 std::span<float>,
                                 std::span<double>>;

    Variant data;
};

namespace Detail
{

template <typename Spans>
struct NumberTypesOfSpans;

template <typename... Numbers>
struct NumberTypesOfSpans<std::variant<std::span<Numbers>...>>
{
    using Type = std::variant<std::type_identity<Numbers>...>;
};

} // namespace Detail

// One of the number types PackedArrayRef can hold, named by type rather
// than by value: std::visit gives back the C++ type.
using PackedNumberType = Detail::NumberTypesOfSpans<PackedArrayRef::Variant>::Type;

// One number inside a packable struct. A field of a nested struct is
// flattened: Keyframe's `value.x` has the path {"value", "x"} and the
// offset of x from the start of the Keyframe.
struct PackedStructField
{
    std::span<const std::string_view> path;
    PackedNumberType type;
    std::size_t offset = 0;
};

// A contiguous array of structs made only of fixed-width numbers, handed
// to a reflector in one call. Fields are listed in reflect order, which
// need not be memory order. Saving: the bytes hold the elements to
// write. Loading: the container was already resized to arraySize() and
// default-constructed, so a field the reflector does not fill keeps its
// default.
struct PackedStructArrayRef
{
    std::size_t getElementCount() const { return elementBytes.size() / stride; }

    std::span<std::byte> elementBytes;
    std::size_t stride = 0;
    std::span<const PackedStructField> fields;
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
        : opts(std::move(optsToUse))
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
    bool isOmittable() const { return opts.omittable; }

    // Per-slot operations that depend on the concrete reflector kind.
    virtual void visit(PrimitiveRef ref) = 0;
    virtual void writeNull() = 0;
    virtual ValueKind kind() const = 0;

    // Return true to take a whole array of numbers at once; the default
    // false walks the elements one by one.
    virtual bool visitPacked(PackedArrayRef /*ref*/) { return false; }

    // Return true to take a whole array of packable structs at once; the
    // default false reflects each element as an object.
    virtual bool visitPackedStructs(PackedStructArrayRef /*ref*/) { return false; }

    // Refines ValueKind::Number for a format that stores an exact
    // integer apart from a double, the way the JSON layer does. Only a
    // Shape::Raw walk has to ask — a typed field already knows which C++
    // type it is reading into — so a format with a single numeric kind
    // keeps the default and its raw numbers stay doubles.
    virtual bool isIntegerNumber() const { return false; }

    // Called by the Omittable dispatcher on a save, for an engaged value
    // only, before the inner T is reflected. A reflector that staged
    // this slot (because Options::omittable was set) commits it to the
    // parent here; one that didn't ignores the call. Never calling it is
    // exactly how "this key is absent" is expressed.
    virtual void markPresent();

    // Called by Property::operator() on the field's child slot, after
    // the slot is spawned and before its value is visited, with the
    // field's attributes in call order (a Property::doc() text arrives
    // first, as a Doc). Never called for a field without attributes.
    // The views are valid only during the call. The default ignores
    // them — which is how a reflector says it doesn't support
    // attributes; TypeTree keeps Doc as the description and every
    // reflectable attribute as JSON.
    virtual void visitAttributes(std::span<const AttributeRef> /*attributes*/) {}

    // Called by the dispatch right before invoking a reflectable type's
    // own reflect() body. `id` carries both the short and qualified C++
    // names. Reflectors that care about type identity (TypeScript
    // exporter, schema's $defs) override this. Returning false tells the
    // dispatcher to skip the body — used to break recursion when the
    // same type is already being walked further up the chain, or when
    // the reflector has emitted a name reference instead of inlining.
    virtual bool beginNamedType(TypeId /*id*/) { return true; }

    // Called by the dispatch for every reflectable object (a type with a
    // member, external, MIRO_REFLECT or hand-written reflect — templates
    // included), in Save and Load modes alike, before anything else is
    // done with it: before beginNamedType and before its body. Returning
    // true claims the object — the dispatch then does nothing more with
    // it, so neither beginNamedType nor the body runs; the reflector has
    // handled it (and can still walk it with ObjectView::reflectInto).
    // Returning false carries on exactly as if the hook didn't exist.
    // Not called for primitives, enums, variants, raw JSON, types with
    // their own reflectValue, or containers themselves — but it is
    // called for each reflectable element of a container. The default
    // declines, so a reflector that doesn't override it pays one virtual
    // call per object and nothing else.
    virtual bool visitObject(const ObjectView& /*object*/) { return false; }

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

    // Sibling of visitEnum for enums that go on the wire as their
    // integer value (`Miro::EnumFormat<E>::integer`). Entries carry the
    // enumerator names alongside those numbers so a renderer can emit a
    // numeric enum rather than throwing the names away. The default
    // reports the slot as a plain int64 — the shape the data really
    // has for a reflector that doesn't model enums at all.
    virtual void visitIntegerEnum(TypeId id, const Vector<EnumEntry>& entries);

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

    // Asked on a load before a container allocates for its stored
    // elements: a vector before it resizes, a map before it adds its
    // entries, an OwningPointer before it creates its object. Returning
    // false leaves the container as it was. A binary format can store an
    // element in one byte that becomes kilobytes in memory, so a
    // reflector reading untrusted data refuses here to cap a load's
    // memory. The default approves everything.
    virtual bool approveElementAllocation(std::size_t /*elementCount*/,
                                          std::size_t /*elementSizeInBytes*/)
    {
        return true;
    }

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

    // Schema-mode hook for internally tagged unions (reflectTagged).
    // Data walkers serialize only the alternative that is actually
    // active, so they never call this; a schema walker needs every arm,
    // so the dispatcher announces each registered alternative here and
    // reflects its body into the returned child slot. `tagKey` is the
    // discriminator's field name and `tag` this arm's literal value.
    // The default throws for the same reason requirePolymorphicSupport
    // does — a reflector that can't describe a union should say so
    // rather than emit a partial shape.
    virtual Reflector& beginTaggedAlternative(std::string_view tagKey,
                                              const TagLiteral& tag,
                                              Options childOpts);

protected:
    Options opts;
};

} // namespace Miro
