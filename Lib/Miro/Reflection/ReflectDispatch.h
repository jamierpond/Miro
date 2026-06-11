#pragma once

#include "Reflector.h"
#include "TypeName.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace Miro::Detail
{

template <typename T>
concept HasReflectMember = requires(T& v, Reflector& r) { v.reflect(r); };

template <typename T>
concept HasExternalReflect = requires(T& v, Reflector& r) { reflect(r, v); };

template <typename T>
concept Reflectable = HasReflectMember<T> || HasExternalReflect<T>;

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

template <typename V>
struct IsMapLike<std::map<std::string, V>> : std::true_type
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

template <typename T>
constexpr bool isOptional()
{
    return IsOptional<T>::value;
}

template <typename T>
consteval Shape shapeOf()
{
    using U = typename InnerOf<T>::type;

    if constexpr (IsArrayLike<U>::value)
        return Shape::Array;
    else if constexpr (IsMapLike<U>::value)
        return Shape::Map;
    else if constexpr (IsVariant<U>::value
                       || (Reflectable<U> && !std::is_arithmetic_v<U>
                           && !std::is_enum_v<U>) )
        return Shape::Object;
    else
        return Shape::Primitive;
}

// Build child Options from a parent's Options. mode and schema
// inherit; shape and nullable are decided by the value type T.
template <typename T>
constexpr Options childOptionsFor(const Options& parent)
{
    auto opts = parent;
    opts.shape = shapeOf<T>();
    opts.nullable = isOptional<T>();
    return opts;
}

// Build top-level Options for a fresh root reflector reflecting T.
template <typename T>
constexpr Options topLevelOptions(Mode mode, bool schema = false)
{
    return Options {
        .mode = mode,
        .shape = shapeOf<T>(),
        .nullable = isOptional<T>(),
        .schema = schema,
    };
}

// Forward declarations for container, optional and enum overloads. The
// definitions live in ReflectContainers.h / ReflectEnum.h, but the
// declarations must be visible here so Phase-1 lookup inside Property's
// templated operator() can see them as candidates.
template <typename T>
constexpr void reflectValue(Reflector& ref, std::vector<T>& value);

template <typename T, std::size_t N>
constexpr void reflectValue(Reflector& ref, std::array<T, N>& value);

template <typename V>
void reflectValue(Reflector& ref, std::map<std::string, V>& value);

template <typename T>
constexpr void reflectValue(Reflector& ref, std::optional<T>& value);

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

template <typename T>
    requires std::is_enum_v<T>
constexpr void reflectValue(Reflector& ref, T& value);

constexpr void reflectValue(Reflector& ref, bool& value)
{
    ref.visit(value);
}

constexpr void reflectValue(Reflector& ref, int& value)
{
    ref.visit(value);
}

constexpr void reflectValue(Reflector& ref, double& value)
{
    ref.visit(value);
}

constexpr void reflectValue(Reflector& ref, std::string& value)
{
    ref.visit(value);
}

constexpr void reflectValue(Reflector& ref, std::int64_t& value)
{
    ref.visit(value);
}

// The widened copy is only written back when loading — saving never
// changes the value, and skipping the writeback lets a constant
// evaluation serialize a const object (writing through the
// const_cast in toJSON/toJSONString would otherwise be rejected).
template <std::integral T>
    requires(!std::same_as<T, bool> && !std::same_as<T, int>
             && !std::same_as<T, std::int64_t>)
constexpr void reflectValue(Reflector& ref, T& value)
{
    auto wide = static_cast<std::int64_t>(value);
    ref.visit(wide);

    if (ref.isLoading())
        value = static_cast<T>(wide);
}

template <std::floating_point T>
    requires(!std::same_as<T, double>)
constexpr void reflectValue(Reflector& ref, T& value)
{
    auto wide = static_cast<double>(value);
    ref.visit(wide);

    if (ref.isLoading())
        value = static_cast<T>(wide);
}

// Default fallback: a reflectable struct (member reflect() or external
// `Miro::reflect(Reflector&, T&)` free function). The slot is already
// committed as Object by the parent's atKey/atIndex via Options.shape,
// so the dispatcher just runs the user's reflect.
template <typename T>
    requires Reflectable<T> && (!std::is_arithmetic_v<T>) && (!std::is_enum_v<T>)
constexpr void reflectValue(Reflector& ref, T& value)
{
    if constexpr (isNamedUserType<T>())
        if (!ref.beginNamedType(TypeId {typeNameOf<T>(), qualifiedNameOf<T>()}))
            return;

    if constexpr (HasReflectMember<T>)
        value.reflect(ref);
    else
        reflect(ref, value);
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
template <typename T>
constexpr void Property::operator()(T& value)
{
    using Detail::reflectValue;
    reflectValue(
        reflector.atKey(key, Detail::childOptionsFor<T>(reflector.options())),
        value);
}

template <typename T>
constexpr void Element::operator()(T& value)
{
    using Detail::reflectValue;
    reflectValue(
        reflector.atIndex(index, Detail::childOptionsFor<T>(reflector.options())),
        value);
}

} // namespace Miro
