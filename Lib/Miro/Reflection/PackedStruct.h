#pragma once

#include "Reflector.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

// A struct is packable when every reflected field is a fixed-width number
// (a PackedArrayRef element type) or another packable struct. An array of
// them can then go to a reflector as one block (visitPackedStructs).
// Everything here is decided at compile time from the field list that
// MIRO_REFLECT emits, or that MIRO_PACKED declares for a type with a
// hand-written reflect().

namespace Miro
{

// Specialize through MIRO_PACKED, the way EnumFormat is specialized
// through MIRO_ENUM_AS_INTEGER.
template <typename T>
struct PackedFields
{
    static constexpr bool isDeclared = false;
};

// The field list of a type reflected outside its class. Specialized by
// MIRO_REFLECT_EXTERNAL and MIRO_REFLECT_EXTERNAL_MEMBERS.
template <typename T>
struct ExternalFields
{
    static constexpr bool isDeclared = false;
};

namespace Detail
{

// The element types PackedArrayRef holds; everything else walks element
// by element.
template <typename T>
concept PackableElement =
    std::constructible_from<PackedArrayRef::Variant, std::span<T>>;

template <typename Member>
concept CanBePackedMember = PackableElement<Member> || std::is_class_v<Member>;

// `offset` is empty for a field that cannot be packed, such as a string
// or a static member; its type is recorded either way.
template <typename Member>
struct FieldDescription
{
    using Type = Member;

    std::string_view name;
    std::optional<std::size_t> offset;
};

template <typename Member>
constexpr std::tuple<FieldDescription<Member>>
    describeField(std::string_view name, std::optional<std::size_t> offset)
{
    return {FieldDescription<Member> {name, offset}};
}

template <typename T>
concept HasMacroFieldList = requires(const T& value) {
    { value.getMiroFieldListOwner() } -> std::same_as<T*>;
    T::template describeMiroFields<T>();
};

template <typename T>
concept HasDeclaredFieldList = PackedFields<T>::isDeclared;

template <typename T>
concept HasExternalFieldList = ExternalFields<T>::isDeclared;

template <typename T>
concept HasFieldList =
    HasMacroFieldList<T> || HasDeclaredFieldList<T> || HasExternalFieldList<T>;

template <typename T>
constexpr auto getFieldDescriptions()
{
    if constexpr (HasDeclaredFieldList<T>)
        return PackedFields<T>::describeFields();
    else if constexpr (HasExternalFieldList<T>)
        return ExternalFields<T>::template describeFields<T>();
    else
        return T::template describeMiroFields<T>();
}

template <typename T>
constexpr bool isPackableStruct();

template <typename Member>
constexpr bool isPackableField(const FieldDescription<Member>& field)
{
    if constexpr (PackableElement<Member>)
        return field.offset.has_value();
    else
        return field.offset.has_value() && isPackableStruct<Member>();
}

template <typename T>
constexpr bool areAllFieldsPackable()
{
    constexpr auto fields = getFieldDescriptions<T>();

    if constexpr (std::tuple_size_v<decltype(fields)> == 0)
        return false;
    else
        return std::apply([](const auto&... field)
                          { return (isPackableField(field) && ...); },
                          fields);
}

template <typename T>
constexpr bool isPackableStruct()
{
    if constexpr (!HasFieldList<T>)
        return false;
    else if constexpr (!std::is_standard_layout_v<T>
                       || !std::is_trivially_copyable_v<T>)
        return false;
    else
        return areAllFieldsPackable<T>();
}

template <typename T>
concept PackableStruct = isPackableStruct<T>();

template <typename T>
constexpr std::size_t countFlattenedFields();

template <typename Member>
constexpr std::size_t countFlattenedFieldsOf(const FieldDescription<Member>&)
{
    if constexpr (PackableElement<Member>)
        return 1;
    else
        return countFlattenedFields<Member>();
}

template <typename T>
constexpr std::size_t countFlattenedFields()
{
    return std::apply([](const auto&... field)
                      { return (countFlattenedFieldsOf(field) + ...); },
                      getFieldDescriptions<T>());
}

inline constexpr auto maxPackedStructNestingDepth = std::size_t {8};

struct FlattenedFieldPath
{
    std::array<std::string_view, maxPackedStructNestingDepth> segments {};
    std::size_t depth = 0;
};

struct FlattenedField
{
    FlattenedFieldPath path;
    PackedNumberType type;
    std::size_t offset = 0;
};

template <std::size_t Count>
struct FlattenedFieldList
{
    std::array<FlattenedField, Count> fields {};
    std::size_t size = 0;
};

template <typename T, typename List>
constexpr void appendFlattenedFields(List& list,
                                     FlattenedFieldPath& prefix,
                                     std::size_t baseOffset);

template <typename Member, typename List>
constexpr void appendFlattenedField(List& list,
                                    FlattenedFieldPath& prefix,
                                    std::size_t baseOffset,
                                    const FieldDescription<Member>& field)
{
    prefix.segments.at(prefix.depth++) = field.name;
    auto offset = baseOffset + *field.offset;

    if constexpr (PackableElement<Member>)
        list.fields[list.size++] =
            FlattenedField {prefix, std::type_identity<Member> {}, offset};
    else
        appendFlattenedFields<Member>(list, prefix, offset);

    --prefix.depth;
}

template <typename T, typename List>
constexpr void appendFlattenedFields(List& list,
                                     FlattenedFieldPath& prefix,
                                     std::size_t baseOffset)
{
    std::apply([&](const auto&... field)
               { (appendFlattenedField(list, prefix, baseOffset, field), ...); },
               getFieldDescriptions<T>());
}

template <typename T>
constexpr auto flattenFields()
{
    auto list = FlattenedFieldList<countFlattenedFields<T>()> {};
    auto prefix = FlattenedFieldPath {};
    appendFlattenedFields<T>(list, prefix, 0);
    return list.fields;
}

// The flattened field table of T, built once at compile time. `fields`
// views the paths in `flattened`, so both are static members.
template <PackableStruct T>
struct PackedStructLayout
{
    static constexpr auto flattened = flattenFields<T>();

    static constexpr auto fields = []
    {
        auto result = std::array<PackedStructField, flattened.size()> {};

        for (auto i = std::size_t {0}; i < flattened.size(); ++i)
        {
            const auto& path = flattened[i].path;
            result[i] = PackedStructField {
                std::span<const std::string_view> {path.segments.data(), path.depth},
                flattened[i].type,
                flattened[i].offset};
        }

        return result;
    }();
};

template <PackableStruct T>
PackedStructArrayRef makePackedStructArrayRef(std::span<T> elements)
{
    return {
        std::as_writable_bytes(elements), sizeof(T), PackedStructLayout<T>::fields};
}

} // namespace Detail
} // namespace Miro

// The byte offset of `field` in `Self`, or empty when the field cannot be
// packed. offsetof is only defined for a standard-layout Self, so any
// other Self gets no offsets.
#define MIRO_PACKED_FIELD_OFFSET(Self, field)                                       \
    []() -> std::optional<std::size_t>                                              \
    {                                                                               \
        if constexpr (Miro::Detail::CanBePackedMember<decltype(Self::field)>        \
                      && std::is_standard_layout_v<Self>)                           \
        {                                                                           \
            if constexpr (std::is_member_object_pointer_v<decltype(&Self::field)>)  \
                return offsetof(Self, field);                                       \
            else                                                                    \
                return std::nullopt;                                                \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            return std::nullopt;                                                    \
        }                                                                           \
    }()

#define MIRO_DESCRIBE_FIELD_NAMED(Self, field, key)                                 \
    , Miro::Detail::describeField<decltype(Self::field)>(                           \
          key, MIRO_PACKED_FIELD_OFFSET(Self, field))

#define MIRO_DESCRIBE_FIELD(Self, field)                                            \
    MIRO_DESCRIBE_FIELD_NAMED(Self, field, #field)
