#pragma once

#include "../Containers.h"
#include "../Detail/StringUtilities.h"
#include "ReflectDispatch.h"
#include "ReflectEnum.h"
#include "ReflectPolymorphic.h"
#include "Reflector.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

// Internally tagged unions — the counterpart to ReflectPolymorphic.h.
//
// Where reflectPolymorphic is externally tagged (`{"Circle": {...}}`,
// one key naming the alternative), an internally tagged union puts the
// discriminator in an ordinary field of the object and lets the active
// alternative's own fields sit beside it:
//
//   {"type": 2, "style": 1, "label": "Click"}   -> Button
//   {"type": 3, "options": [...]}               -> SelectMenu
//
// That is the shape Discord and most JSON APIs use. Storage is shared
// with reflectPolymorphic — the same Detail::PolymorphicAccess adapter
// backs std::variant<Ts...>, OwningPointer<Base>, and anything a user
// specializes it for.
//
// Three entry points, low-level to high-level:
//
//   * `Miro::reflectTagged(ref, key, value, callback)` — the baseline.
//     `callback` receives a TaggedDispatcher and registers each
//     alternative with `d.alt<T>(tag)`. The tag's C++ type is deduced
//     per call and may be an integer, a string, or an enum.
//
//   * `Miro::Tagged<"type", Base, Derived...>` — OwningPointer<Base>
//     holder that reads `static constexpr auto miroTag` off each Derived.
//
//   * `Miro::TaggedVariant<"type", Ts...>` — the same, over
//     std::variant<Ts...> so the union is a value type (copyable, and
//     therefore usable as a vector element).
//
// Alternatives must be object-shaped: they are reflected into the same
// slot as the tag, so their reflect() body adds keys next to it.

namespace Miro::Detail
{

// The C++ type a tag is stored and compared as. Anything convertible to
// a string_view (a string literal, std::string_view, std::string) is
// kept as an owned std::string; integers and enums stay as they are.
template <typename Tag>
using TagStorage =
    std::conditional_t<std::convertible_to<Tag, std::string_view>, std::string, Tag>;

template <typename Stored>
std::int64_t tagAsInteger(const Stored& tag)
{
    if constexpr (std::is_enum_v<Stored>)
        return static_cast<std::int64_t>(
            static_cast<std::underlying_type_t<Stored>>(tag));
    else
        return static_cast<std::int64_t>(tag);
}

// The wire spelling of one alternative's tag, handed to schema walkers
// through Reflector::beginTaggedAlternative. Enums follow the normal
// enum path: the enumerator name when it has one, its numeric value
// otherwise.
template <typename Stored>
TagLiteral tagLiteralOf(const Stored& tag)
{
    if constexpr (std::same_as<Stored, std::string>)
    {
        return TagLiteral {tag, true};
    }
    else if constexpr (std::is_enum_v<Stored>)
    {
        if (auto name = std::string {enumToString(tag)}; !name.empty())
            return TagLiteral {std::move(name), true};

        return TagLiteral {std::to_string(tagAsInteger(tag)), false};
    }
    else
    {
        return TagLiteral {std::to_string(tagAsInteger(tag)), false};
    }
}

// Compile-time string usable as a non-type template parameter, so a
// tagged holder can carry its discriminator key in its own type:
// `Tagged<"type", Base, Button>`. `data` is public because a structural
// NTTP type may not have private members.
template <std::size_t N>
struct TagKey
{
    constexpr TagKey(const char (&textToUse)[N])
    {
        for (auto i = std::size_t {0}; i < N; ++i)
            data[i] = textToUse[i];
    }

    constexpr std::string_view view() const { return {data, N - 1}; }

    char data[N] {};
};

template <typename T>
using MiroTagType = TagStorage<std::decay_t<decltype(T::miroTag)>>;

// True when every alternative's miroTag has the same C++ type. Mixing an
// int tag with a string tag in one union is a declaration bug, so the
// zero-config holders reject it at compile time.
template <typename... Ts>
struct TagTypesAgree : std::true_type
{
};

template <typename First, typename... Rest>
struct TagTypesAgree<First, Rest...>
    : std::bool_constant<(std::same_as<MiroTagType<First>, MiroTagType<Rest>>
                          && ...)>
{
};

} // namespace Miro::Detail

namespace Miro
{

// Injected into the user's callback by reflectTagged. The user calls
// `d.alt<T>(tag)` once per alternative; like PolymorphicDispatcher,
// dispatch happens eagerly inside alt() and the dispatcher locks itself
// after the first match, so the callback needs no state machine.
//
// On load the tag slot is read once, up front, into its wire form; each
// alt() then compares its own literal against that. On save the active
// alternative is reflected into the same slot and the registered tag is
// written last, so it stays authoritative even if an alternative
// declares the discriminator as a field of its own. In schema mode
// nothing is read or written — every registered alternative is
// announced through beginTaggedAlternative.
template <typename Value>
class TaggedDispatcher
{
public:
    TaggedDispatcher(Reflector& refToUse,
                     std::string_view keyToUse,
                     Value& valueToUse)
        : ref(refToUse)
        , key(keyToUse)
        , value(valueToUse)
    {
        if (ref.isLoading())
            readTag();
    }

    template <typename T, typename Tag>
    void alt(Tag tag)
    {
        using Stored = Detail::TagStorage<Tag>;
        auto expected = Stored(tag);

        if (ref.isSchema())
        {
            describe<T>(Detail::tagLiteralOf(expected));
            return;
        }

        if (matched)
            return;

        if (ref.isSaving())
            saveIfActive<T>(expected);
        else if (matches(expected))
            load<T>();
    }

    bool handled() const { return matched; }

private:
    // What the tag slot carried on load. Reflectors with no number kind
    // (XML, where the tag is an attribute) report every scalar as a
    // String, so an integral tag is matched against the text too.
    enum class Form
    {
        None,
        Number,
        String
    };

    Options tagOptions() const
    {
        auto opts = ref.options();
        opts.shape = Shape::Primitive;
        opts.nullable = false;
        return opts;
    }

    void readTag()
    {
        auto& tagRef = ref.atKey(key, tagOptions());

        if (auto kind = tagRef.kind(); kind == ValueKind::Number)
        {
            tagRef.visit(number);
            form = Form::Number;
        }
        else if (kind == ValueKind::String)
        {
            tagRef.visit(text);
            form = Form::String;
        }
    }

    template <typename Stored>
    bool matches(const Stored& expected) const
    {
        if constexpr (std::same_as<Stored, std::string>)
        {
            return form == Form::String && text == expected;
        }
        else
        {
            auto wanted = Detail::tagAsInteger(expected);

            if (form == Form::Number)
                return number == wanted;

            if (form != Form::String)
                return false;

            if constexpr (std::is_enum_v<Stored>)
            {
                if (auto parsed = enumFromString<Stored>(text))
                    return *parsed == expected;
            }

            auto parsed = Detail::parseWholeInteger(text);
            return parsed.has_value() && *parsed == wanted;
        }
    }

    template <typename Stored>
    void writeTag(const Stored& tagToWrite)
    {
        auto copy = tagToWrite;
        Detail::reflectValue(ref.atKey(key, tagOptions()), copy);
    }

    template <typename T, typename Stored>
    void saveIfActive(const Stored& expected)
    {
        using Access = Detail::PolymorphicAccess<Value>;

        if (!Access::template isActive<T>(value))
            return;

        // Body first, tag last: an alternative that also declares the
        // discriminator as one of its own fields would otherwise
        // overwrite the tag with whatever that field happens to hold,
        // and the object would no longer load back as itself.
        Detail::reflectValue(ref, Access::template get<T>(value));
        writeTag(expected);
        matched = true;
    }

    template <typename T>
    void load()
    {
        auto& slot = Detail::PolymorphicAccess<Value>::template assign<T>(value);
        Detail::reflectValue(ref, slot);
        matched = true;
    }

    template <typename T>
    void describe(const TagLiteral& tag)
    {
        auto& child = ref.beginTaggedAlternative(
            key, tag, Detail::childOptionsFor<T>(ref.options()));

        auto probe = T {};
        Detail::reflectValue(child, probe);
    }

    Reflector& ref;
    std::string_view key;
    Value& value;

    std::string text;
    std::int64_t number = 0;
    Form form = Form::None;
    bool matched = false;
};

template <typename Value, typename Callback>
void reflectTagged(Reflector& ref,
                   std::string_view key,
                   Value& value,
                   Callback callback)
{
    auto dispatcher = TaggedDispatcher<Value> {ref, key, value};
    callback(dispatcher);
}

// Zero-config OwningPointer<Base> holder — the internally tagged
// sibling of Miro::Polymorphic. Each alternative declares its own
// discriminator as `static constexpr auto miroTag = 2;` and the key is
// baked into the type: `Tagged<"type", Component, Button, SelectMenu>`.
template <Detail::TagKey Key, typename Base, typename... Derived>
struct Tagged
{
    static_assert(Detail::TagTypesAgree<Derived...>::value,
                  "All alternatives of a Tagged<> must declare miroTag with "
                  "the same type.");

    Base* get() const { return value.get(); }
    Base* operator->() const { return value.get(); }
    Base& operator*() const { return *value; }
    explicit operator bool() const { return value.get() != nullptr; }

    void reflect(Reflector& ref)
    {
        reflectTagged(ref,
                      Key.view(),
                      value,
                      [](auto& d)
                      { (d.template alt<Derived>(Derived::miroTag), ...); });
    }

    OwningPointer<Base> value;
};

// std::variant flavour of Tagged: a copyable value type, so a list of
// alternatives is just a std::vector<TaggedVariant<...>>.
template <Detail::TagKey Key, typename... Ts>
struct TaggedVariant
{
    static_assert(Detail::TagTypesAgree<Ts...>::value,
                  "All alternatives of a TaggedVariant<> must declare miroTag "
                  "with the same type.");

    template <typename T>
    bool holds() const
    {
        return std::holds_alternative<T>(value);
    }

    template <typename T>
    T& as()
    {
        return std::get<T>(value);
    }

    template <typename T>
    const T& as() const
    {
        return std::get<T>(value);
    }

    void reflect(Reflector& ref)
    {
        reflectTagged(ref,
                      Key.view(),
                      value,
                      [](auto& d) { (d.template alt<Ts>(Ts::miroTag), ...); });
    }

    std::variant<Ts...> value;
};

} // namespace Miro
