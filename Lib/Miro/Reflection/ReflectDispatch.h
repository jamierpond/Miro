#pragma once

#include "NumericConvert.h"
#include "Omittable.h"
#include "Reflector.h"
#include "TypeName.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Miro::Json
{
struct Value;
}

namespace Miro::Detail
{

template <typename T>
concept HasReflectMember = requires(T& v, Reflector& r) { v.reflect(r); };

template <typename T>
concept HasExternalReflect = requires(T& v, Reflector& r) { reflect(r, v); };

template <typename T>
concept AbleToReflect = HasReflectMember<T> || HasExternalReflect<T>;

// Type traits for shape classification.

template <typename T>
struct IsOptional : std::false_type
{
};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type
{
};

// OwningPointer behaves like std::optional for reflection — nullable
// slot whose inner shape is decided by T.
template <typename T>
struct IsOptional<OwningPointer<T>> : std::true_type
{
};

template <typename T>
struct InnerOf
{
    using type = T;
};

template <typename T>
struct InnerOf<std::optional<T>>
{
    using type = T;
};

template <typename T>
struct InnerOf<OwningPointer<T>>
{
    using type = T;
};

// Omittable<T> decides whether the *key* exists; whether the value can
// be null is still up to what it wraps. So Omittable<int> is not
// nullable, Omittable<std::optional<int>> is, and the shape comes from
// unwrapping both layers.
template <typename T>
struct IsOptional<Omittable<T>> : IsOptional<T>
{
};

template <typename T>
struct InnerOf<Omittable<T>>
{
    using type = typename InnerOf<T>::type;
};

template <typename T>
struct IsOmittable : std::false_type
{
};

template <typename T>
struct IsOmittable<Omittable<T>> : std::true_type
{
};

// Detects whether T derives (directly or indirectly) from some
// EA::Vector<U, A>. Used so anything inheriting from EA::Vector — e.g.
// EA::OwnedVector or a user's own subclass — is classified as array-
// shaped without each one needing its own IsArrayLike specialization.
template <typename U, typename A>
auto vectorDerivedProbe(const Vector<U, A>*) -> std::true_type;
auto vectorDerivedProbe(...) -> std::false_type;

template <typename T>
constexpr bool inheritsFromVector =
    decltype(vectorDerivedProbe(std::declval<T*>()))::value;

template <typename T, typename = void>
struct IsArrayLike : std::false_type
{
};

template <typename T>
struct IsArrayLike<T, std::enable_if_t<inheritsFromVector<T>>> : std::true_type
{
};

template <typename T>
struct IsArrayLike<std::vector<T>> : std::true_type
{
};

template <typename T, std::size_t N>
struct IsArrayLike<std::array<T, N>> : std::true_type
{
};

template <typename T, int N>
struct IsArrayLike<Array<T, N>> : std::true_type
{
};

template <typename T>
struct IsMapLike : std::false_type
{
};

template <typename V, typename Compare>
struct IsMapLike<std::map<std::string, V, Compare>> : std::true_type
{
};

template <typename V>
struct IsMapLike<EA::MapVector<std::string, V>> : std::true_type
{
};

// Variant slots serialize as externally-tagged objects: `{"Tag": {...}}`,
// one property whose key is the active alternative's tag. The slot is
// genuinely Object-shaped, so the JsonReflector commits ensureObject()
// before reflectPolymorphic writes the tag.
template <typename T>
struct IsVariant : std::false_type
{
};

template <typename... Ts>
struct IsVariant<std::variant<Ts...>> : std::true_type
{
};

template <typename... Ts>
struct IsVariant<EA::Variant<Ts...>> : std::true_type
{
};

// Raw JSON slots carry their structure in the value, not in the C++
// type, so they classify as Shape::Raw. Specialized in ReflectJson.h
// (for Miro::Json::Value and anything derived from it, e.g. Json::Any)
// where Value is a complete type — this header only forward-declares
// it, so it stays free of the JSON layer.
template <typename T, typename = void>
struct IsRawJson : std::false_type
{
};

template <typename T>
constexpr bool isOptional()
{
    return IsOptional<T>::value;
}

template <typename T>
constexpr bool isOmittable()
{
    return IsOmittable<T>::value;
}

template <typename T>
consteval Shape shapeOf()
{
    using U = typename InnerOf<T>::type;

    if constexpr (IsRawJson<U>::value)
        return Shape::Raw;
    else if constexpr (IsArrayLike<U>::value)
        return Shape::Array;
    else if constexpr (IsMapLike<U>::value)
        return Shape::Map;
    else if constexpr (IsVariant<U>::value
                       || (AbleToReflect<U> && !std::is_arithmetic_v<U>
                           && !std::is_enum_v<U>) )
        return Shape::Object;
    else
        return Shape::Primitive;
}

// Build child Options from a parent's Options. mode, schema and the
// custom payload inherit (the plain copy of `parent` carries them);
// shape, nullable and omittable are decided by the value type T. Not
// constexpr: Options now holds a std::string (CustomOptions), and this
// is only ever evaluated at runtime while spawning a child reflector.
template <typename T>
Options childOptionsFor(const Options& parent)
{
    auto opts = parent;
    opts.shape = shapeOf<T>();
    opts.nullable = isOptional<T>();
    opts.omittable = isOmittable<T>();
    return opts;
}

// Build top-level Options for a fresh root reflector reflecting T. The
// custom payload defaults to empty; the JSON/XML entry points pass the
// caller-supplied CustomOptions here so it drills down to every child.
template <typename T>
Options
    topLevelOptions(Mode mode, bool schema = false, const CustomOptions& custom = {})
{
    return Options {
        .mode = mode,
        .shape = shapeOf<T>(),
        .nullable = isOptional<T>(),
        .omittable = isOmittable<T>(),
        .schema = schema,
        .custom = custom,
    };
}

// Forward declarations for container, optional and enum overloads. The
// definitions live in ReflectContainers.h / ReflectEnum.h, but the
// declarations must be visible here so Phase-1 lookup inside Property's
// templated operator() can see them as candidates.
template <typename T>
void reflectValue(Reflector& ref, std::vector<T>& value);

template <typename T, std::size_t N>
void reflectValue(Reflector& ref, std::array<T, N>& value);

template <typename V, typename Compare>
void reflectValue(Reflector& ref, std::map<std::string, V, Compare>& value);

template <typename T>
void reflectValue(Reflector& ref, std::optional<T>& value);

template <typename T>
void reflectValue(Reflector& ref, Omittable<T>& value);

template <typename T, typename Allocator>
void reflectValue(Reflector& ref, Vector<T, Allocator>& value);

template <typename T, int N>
void reflectValue(Reflector& ref, Array<T, N>& value);

template <typename V>
void reflectValue(Reflector& ref, EA::MapVector<std::string, V>& value);

template <typename T>
void reflectValue(Reflector& ref, OwningPointer<T>& value);

template <typename... Ts>
void reflectValue(Reflector& ref, std::variant<Ts...>& value);

template <typename... Ts>
void reflectValue(Reflector& ref, EA::Variant<Ts...>& value);

template <typename T>
    requires std::is_enum_v<T>
void reflectValue(Reflector& ref, T& value);

// Raw JSON value — declared in ReflectJson.h, defined in ReflectJson.cpp.
// Json::Any binds to this overload through its Value base.
void reflectValue(Reflector& ref, Json::Value& value);

inline void reflectValue(Reflector& ref, bool& value)
{
    ref.visit(value);
}

inline void reflectValue(Reflector& ref, int& value)
{
    ref.visit(value);
}

inline void reflectValue(Reflector& ref, double& value)
{
    ref.visit(value);
}

inline void reflectValue(Reflector& ref, std::string& value)
{
    ref.visit(value);
}

inline void reflectValue(Reflector& ref, std::int64_t& value)
{
    ref.visit(value);
}

template <std::integral T>
    requires(!std::same_as<T, bool> && !std::same_as<T, int>
             && !std::same_as<T, std::int64_t>)
void reflectValue(Reflector& ref, T& value)
{
    auto wide = static_cast<std::int64_t>(value);
    ref.visit(wide);

    if (!ref.isLoading())
        return;

    // A uint64 travels as its int64 bit pattern, so a value past
    // INT64_MAX arrives negative and must be reinterpreted, not refused.
    // Every narrower type takes the loaded value only if it fits.
    if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(std::int64_t))
        value = static_cast<T>(wide);
    else if (!convertNumber(wide, value))
        ref.reportRefusedValue();
}

template <std::floating_point T>
    requires(!std::same_as<T, double>)
void reflectValue(Reflector& ref, T& value)
{
    auto wide = static_cast<double>(value);
    ref.visit(wide);

    if (ref.isLoading() && !convertNumber(wide, value))
        ref.reportRefusedValue();
}

// A reflectable struct's walk once visitObject has declined it: the
// beginNamedType announcement, then the user's own reflect.
template <typename T>
void reflectObjectBody(Reflector& ref, T& value)
{
    if constexpr (isNamedUserType<T>())
        if (!ref.beginNamedType(typeIdOf<T>()))
            return;

    if constexpr (HasReflectMember<T>)
        value.reflect(ref);
    else
        reflect(ref, value);
}

// The typed half of ObjectView::reflectInto.
template <typename T>
void reflectObjectErased(Reflector& ref, void* object)
{
    reflectObjectBody(ref, *static_cast<T*>(object));
}

// Default fallback: a reflectable struct (member reflect() or external
// `Miro::reflect(Reflector&, T&)` free function). The slot is already
// committed as Object by the parent's atKey/atIndex via Options.shape.
// The reflector may claim the whole object through visitObject;
// otherwise the dispatcher runs the user's reflect.
template <typename T>
    requires AbleToReflect<T> && (!std::is_arithmetic_v<T>) && (!std::is_enum_v<T>)
void reflectValue(Reflector& ref, T& value)
{
    if (ref.visitObject(ObjectView::of(value)))
        return;

    reflectObjectBody(ref, value);
}

// "The dispatcher knows how to reflect a T." Spelled unqualified from
// inside Detail for the same reason Property::operator() is: ordinary
// lookup sees every built-in overload above, and ADL at the point of use
// also sees a user's late `reflectValue(Reflector&, T&)` in namespace Miro
// or in T's own namespace. A qualified Detail::reflectValue would say
// "no" for exactly the types the dispatcher does handle.
template <typename T>
concept HasReflectValue =
    requires(Reflector& ref, T& value) { reflectValue(ref, value); };

// The typed half of AttributeRef::reflectInto. Reflects a copy, so the
// attribute itself stays const whatever the parent's mode.
template <typename A>
void reflectAttributeInto(Reflector& parent,
                          std::string_view key,
                          const void* attribute)
{
    auto copy = *static_cast<const A*>(attribute);
    parent[key](copy);
}

// Compile-time checks on one attribute A of a field of type T. A
// function so a failure names A and T in the instantiation trace.
template <typename T, typename A>
consteval bool checkAttribute()
{
    static_assert(std::is_trivially_destructible_v<A>,
                  "Miro field attributes must be trivially destructible, so "
                  "passing one can never allocate. Hold text as a "
                  "std::string_view, not a std::string.");
    static_assert(AttributeAppliesTo<A, T>::value,
                  "This attribute does not apply to this field type "
                  "(see the Miro::AttributeAppliesTo specialization).");
    return true;
}

} // namespace Miro::Detail

namespace Miro
{

// Property/Element dispatch the value through an unqualified call to
// `reflectValue`, with `using Detail::reflectValue` bringing the built-in
// overloads into scope. Two-phase lookup for unqualified dependent calls
// re-runs ADL at instantiation on the argument types, so users can teach
// Miro about a new primitive (e.g. juce::String) by adding a free
// `reflectValue(Reflector&, T&)` overload in namespace `Miro` or in `T`'s
// own namespace, even after <Miro/Miro.h> has been included.
template <typename A>
AttributeRef AttributeRef::of(const A& attribute)
{
    auto ref = AttributeRef {Detail::qualifiedNameOf<A>(), &attribute};

    if constexpr (Detail::HasReflectValue<A> && std::is_copy_constructible_v<A>)
        ref.reflectFn = &Detail::reflectAttributeInto<A>;

    return ref;
}

template <typename T>
ObjectView ObjectView::of(T& object)
{
    return {typeIdOf<T>(), &object, &Detail::reflectObjectErased<T>};
}

// Attributes cost nothing unless present: with an empty pack and no
// doc() text this is exactly the old atKey + reflectValue. Otherwise
// the views live in a stack array (doc() text first, as a Doc) and the
// child slot gets one visitAttributes call before its value.
template <typename T, typename... As>
void Property::operator()(T& value, const As&... attributes)
{
    static_assert((Detail::checkAttribute<T, As>() && ...));

    using Detail::reflectValue;
    auto& child =
        reflector.atKey(key, Detail::childOptionsFor<T>(reflector.options()));

    if constexpr (sizeof...(As) == 0)
    {
        if (!description.empty())
        {
            auto doc = Doc {description};
            auto refs = std::array {AttributeRef::of(doc)};
            child.visitAttributes(refs);
        }
    }
    else
    {
        auto doc = Doc {description};
        auto refs =
            std::array {AttributeRef::of(doc), AttributeRef::of(attributes)...};
        auto view = std::span<const AttributeRef> {refs};

        if (description.empty())
            view = view.subspan(1);

        child.visitAttributes(view);
    }

    reflectValue(child, value);
}

template <typename T>
void Element::operator()(T& value)
{
    using Detail::reflectValue;
    reflectValue(
        reflector.atIndex(index, Detail::childOptionsFor<T>(reflector.options())),
        value);
}

template <typename T>
concept Reflectable = Detail::HasReflectValue<T>;

} // namespace Miro
