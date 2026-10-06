#pragma once

#include "../Detail/StringUtilities.h"
#include "Reflector.h"
#include "TypeName.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Miro
{

// Specialize this to shrink or extend the probed enumerator range for a
// given enum type. The default covers [-128, 127].
//
// Note: an unscoped enum without a fixed underlying type
// (e.g. `enum Foo { ... }`) has a value range determined by its
// enumerators, and casting out-of-range values in a constant expression
// is UB. Give such enums an explicit base (`enum Foo : int { ... }`) or
// use `enum class` to reflect them.
template <typename E>
struct EnumRange
{
    static constexpr int minValue = -128;
    static constexpr int maxValue = 127;
};

// Specialize this to change how a given enum type is written. By
// default an enum saves as its enumerator name (a JSON string), which
// is the readable choice for a format you own. Set `integer` to true
// for wire formats that spell enums as numbers — Discord's
// `{"type": 1}`, most REST APIs with numeric kind fields — and the enum
// saves as its underlying value instead:
//
//   template <>
//   struct Miro::EnumFormat<Discord::ChannelType>
//   {
//       static constexpr bool integer = true;
//   };
//
// Loading is unaffected either way: a number and an enumerator name are
// both accepted whatever the save format is.
template <typename E>
struct EnumFormat
{
    static constexpr bool integer = false;
};

namespace Detail
{

template <auto V>
constexpr std::string_view enumNameRaw()
{
#if defined(__clang__) || defined(__GNUC__)
    constexpr auto prefix = std::string_view {"V = "};
    constexpr auto terminators = std::string_view {";]"};
    auto sig = std::string_view {__PRETTY_FUNCTION__};
    auto start = sig.find(prefix) + prefix.size();
    auto end = sig.find_first_of(terminators, start);
#elif defined(_MSC_VER)
    constexpr auto prefix = std::string_view {"enumNameRaw<"};
    constexpr auto terminator = std::string_view {">("};
    auto sig = std::string_view {__FUNCSIG__};
    auto start = sig.find(prefix) + prefix.size();
    auto end = sig.find(terminator, start);
#else
    auto sig = std::string_view {};
    auto start = std::size_t {};
    auto end = std::size_t {};
#endif

    auto name = sig.substr(start, end - start);

    // Find the rightmost "::" at paren-depth 0 — i.e. the separator
    // between the qualifier and the enumerator. Counting depth lets us
    // ignore the `::` inside an "(anonymous namespace)::Foo" qualifier,
    // and lets us recognize "((anonymous namespace)::Foo)42" cast
    // spellings (their inner `::` lives at depth 1, so it's skipped and
    // the leading `(` survives the front-paren check below).
    auto depth = 0;
    auto lastColon = std::string_view::npos;

    for (auto i = std::size_t {0}; i + 1 < name.size(); ++i)
    {
        if (name[i] == '(')
            ++depth;
        else if (name[i] == ')' && depth > 0)
            --depth;
        else if (depth == 0 && name[i] == ':' && name[i + 1] == ':')
            lastColon = i;
    }

    if (lastColon != std::string_view::npos)
        name.remove_prefix(lastColon + 2);

    if (!name.empty() && name.front() == '(')
        return {};

    return name;
}

template <typename E, int Min, std::size_t... Is>
constexpr auto buildEnumTable(std::index_sequence<Is...>)
{
    return std::array<std::pair<E, std::string_view>, sizeof...(Is)> {
        std::pair {static_cast<E>(Min + static_cast<int>(Is)),
                   enumNameRaw<static_cast<E>(Min + static_cast<int>(Is))>()}...};
}

template <typename E>
    requires std::is_enum_v<E>
inline constexpr auto enumTable = buildEnumTable<E, EnumRange<E>::minValue>(
    std::make_index_sequence<static_cast<std::size_t>(
        EnumRange<E>::maxValue - EnumRange<E>::minValue + 1)> {});

} // namespace Detail

template <typename E>
    requires std::is_enum_v<E>
constexpr std::string_view enumToString(E value)
{
    for (auto& [entryValue, name]: Detail::enumTable<E>)
        if (entryValue == value && !name.empty())
            return name;

    return {};
}

template <typename E>
    requires std::is_enum_v<E>
constexpr std::optional<E> enumFromString(std::string_view str)
{
    for (auto& [entryValue, name]: Detail::enumTable<E>)
        if (!name.empty() && name == str)
            return entryValue;

    return std::nullopt;
}

template <typename E>
    requires std::is_enum_v<E>
Vector<std::string_view> enumNames()
{
    auto names = Vector<std::string_view> {};

    for (auto& [_, name]: Detail::enumTable<E>)
        if (!name.empty())
            names.add(name);

    return names;
}

// enumNames() plus the number behind each name. Used by the schema walk
// for integer-format enums, where a renderer needs both halves to emit
// something like `enum ChannelType { guildText = 0, dm = 1 }`.
template <typename E>
    requires std::is_enum_v<E>
Vector<EnumEntry> enumEntries()
{
    using Underlying = std::underlying_type_t<E>;

    auto entries = Vector<EnumEntry> {};

    for (auto& [entryValue, name]: Detail::enumTable<E>)
        if (!name.empty())
            entries.add(EnumEntry {
                name,
                static_cast<std::int64_t>(static_cast<Underlying>(entryValue))});

    return entries;
}

namespace Detail
{

template <typename T>
    requires std::is_enum_v<T>
std::int64_t enumToNumber(T value)
{
    return static_cast<std::int64_t>(static_cast<std::underlying_type_t<T>>(value));
}

template <typename T>
    requires std::is_enum_v<T>
T enumFromNumber(std::int64_t numeric)
{
    return static_cast<T>(static_cast<std::underlying_type_t<T>>(numeric));
}

template <typename T>
    requires std::is_enum_v<T>
void saveEnum(Reflector& ref, T& value)
{
    auto id = TypeId {typeNameOf<T>(), qualifiedNameOf<T>()};

    if (ref.isSchema())
    {
        if constexpr (EnumFormat<T>::integer)
            ref.visitIntegerEnum(id, enumEntries<T>());
        else
            ref.visitEnum(id, enumNames<T>());

        return;
    }

    // Name format also falls through to the numeric write below when the
    // value has no enumerator — a number round-trips, an empty string
    // wouldn't.
    if constexpr (!EnumFormat<T>::integer)
    {
        if (auto name = std::string {enumToString(value)}; !name.empty())
        {
            ref.visit(name);
            return;
        }
    }

    auto numeric = enumToNumber(value);
    ref.visit(numeric);
}

template <typename T>
    requires std::is_enum_v<T>
void loadEnum(Reflector& ref, T& value)
{
    auto kind = ref.kind();

    if (kind == ValueKind::String)
    {
        auto text = std::string {};
        ref.visit(text);

        if (auto parsed = enumFromString<T>(text))
        {
            value = *parsed;
            return;
        }

        if constexpr (EnumFormat<T>::integer)
        {
            if (auto numeric = parseWholeInteger(text))
                value = enumFromNumber<T>(*numeric);
        }
    }
    else if (kind == ValueKind::Number)
    {
        auto numeric = std::int64_t {};
        ref.visit(numeric);
        value = enumFromNumber<T>(numeric);
    }
}

template <typename T>
    requires std::is_enum_v<T>
void reflectValue(Reflector& ref, T& value)
{
    if (ref.isSaving())
        saveEnum(ref, value);
    else
        loadEnum(ref, value);
}

} // namespace Detail
} // namespace Miro

// Non-intrusive one-liner for the EnumFormat specialization above, in
// the spirit of MIRO_REFLECT_EXTERNAL: place it at global scope once the
// enum is declared, and the type saves as its integer value everywhere
// it appears — on its own, inside a vector, an optional or a map value.
//
//   MIRO_ENUM_AS_INTEGER(Discord::ChannelType)
#define MIRO_ENUM_AS_INTEGER(Type)                                                  \
    namespace Miro                                                                  \
    {                                                                               \
    template <>                                                                     \
    struct EnumFormat<Type>                                                         \
    {                                                                               \
        static constexpr bool integer = true;                                       \
    };                                                                              \
    }
