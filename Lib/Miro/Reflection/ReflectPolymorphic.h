#pragma once

#include "../Containers.h"
#include "ReflectDispatch.h"
#include "Reflector.h"
#include "TypeName.h"

#include <string_view>
#include <variant>

// Polymorphic reflection: round-trips std::variant<Ts...> and inheritance
// hierarchies (e.g. OwningPointer<Base> holding a Derived) through JSON
// and XML reflectors. Wire format is externally tagged — `{"Circle":
// {...}}`. The outer object has exactly one key (the tag identifying
// the active alternative); its value is the alternative's body. This
// maps cleanly onto the existing atKey/mapKeys Reflector contract, so
// no new shape or slot kind is required.
//
// Three entry points, from low-level to high-level:
//
//   * `Miro::reflectPolymorphic(ref, value, callback)` — the baseline.
//     `callback` receives a PolymorphicDispatcher and registers each
//     alternative with `d.alt<T>(tag)`. Use this directly when the
//     storage type is custom or the tags need to come from somewhere
//     other than typeNameOf<T>(). User code can specialize
//     PolymorphicAccess<V> to plug in unusual storage.
//
//   * `reflectValue(ref, std::variant<...>&)` — zero-config built-in.
//     Each alternative's tag defaults to its short C++ type name.
//
//   * `Miro::Polymorphic<Base, Derived...>` — a thin OwningPointer<Base>
//     wrapper that auto-registers each Derived as an alternative. Drop
//     it into a struct as a member and reflect it like any other field.

namespace Miro::Detail
{

// Storage adapter for a polymorphic value. Specialize for new container
// types (e.g. std::shared_ptr<Base>, a tagged-pointer type) to reuse
// reflectPolymorphic with them.
//
// Contract:
//   * isActive<T>(value) — true if the value currently holds a T.
//   * get<T>(value)      — returns a mutable T& aliasing the active T.
//                          Only called when isActive<T>(value) is true.
//   * assign<T>(value)   — overwrite the value with a default-constructed
//                          T (or otherwise put it into a state where a T
//                          is now active) and return a mutable T&.
template <typename Value>
struct PolymorphicAccess;

template <typename... Ts>
struct PolymorphicAccess<std::variant<Ts...>>
{
    template <typename T>
    static bool isActive(const std::variant<Ts...>& value)
    {
        return std::holds_alternative<T>(value);
    }

    template <typename T>
    static T& get(std::variant<Ts...>& value)
    {
        return std::get<T>(value);
    }

    template <typename T>
    static T& assign(std::variant<Ts...>& value)
    {
        return value.template emplace<T>();
    }
};

template <typename Base>
struct PolymorphicAccess<OwningPointer<Base>>
{
    template <typename T>
    static bool isActive(const OwningPointer<Base>& value)
    {
        return value.get() != nullptr
               && dynamic_cast<const T*>(value.get()) != nullptr;
    }

    template <typename T>
    static T& get(OwningPointer<Base>& value)
    {
        return *dynamic_cast<T*>(value.get());
    }

    template <typename T>
    static T& assign(OwningPointer<Base>& value)
    {
        auto* derived = new T {};
        value.reset(derived);
        return *derived;
    }
};

} // namespace Miro::Detail

namespace Miro
{

// Injected into the user's callback by reflectPolymorphic. The user
// calls `d.alt<T>("tag")` once per alternative; dispatch happens eagerly
// inside alt() so the user's lambda doesn't need any state machine — the
// dispatcher locks itself after the first match.
template <typename Value>
class PolymorphicDispatcher
{
public:
    PolymorphicDispatcher(Reflector& refToUse, Value& valueToUse)
        : ref(refToUse)
        , value(valueToUse)
    {
        if (ref.isLoading())
        {
            auto keys = ref.mapKeys();
            if (!keys.empty())
                loadedTag = keys.front();
        }
    }

    template <typename T>
    void alt(std::string_view tag)
    {
        if (matched)
            return;

        auto childOpts = Detail::childOptionsFor<T>(ref.options());

        if (ref.isSaving())
        {
            if (Detail::PolymorphicAccess<Value>::template isActive<T>(value))
            {
                auto& active =
                    Detail::PolymorphicAccess<Value>::template get<T>(value);
                Detail::reflectValue(ref.atKey(tag, childOpts), active);
                matched = true;
            }
        }
        else
        {
            if (tag == loadedTag)
            {
                auto& slot =
                    Detail::PolymorphicAccess<Value>::template assign<T>(value);
                Detail::reflectValue(ref.atKey(tag, childOpts), slot);
                matched = true;
            }
        }
    }

    bool handled() const { return matched; }

private:
    Reflector& ref;
    Value& value;
    std::string loadedTag;
    bool matched = false;
};

template <typename Value, typename Callback>
void reflectPolymorphic(Reflector& ref, Value& value, Callback callback)
{
    ref.requirePolymorphicSupport("reflectPolymorphic");

    auto dispatcher = PolymorphicDispatcher<Value> {ref, value};
    callback(dispatcher);
}

// Thin OwningPointer<Base> holder that auto-registers each Derived as
// an alternative using its short type name as the tag. Use this when
// you want a polymorphic field with the default name-based tags and
// don't want to hand-write a reflect() body.
template <typename Base, typename... Derived>
struct Polymorphic
{
    OwningPointer<Base> value;

    Base* get() const { return value.get(); }
    Base* operator->() const { return value.get(); }
    Base& operator*() const { return *value; }
    explicit operator bool() const { return value.get() != nullptr; }

    void reflect(Reflector& ref)
    {
        reflectPolymorphic(
            ref,
            value,
            [](auto& d)
            { (d.template alt<Derived>(Detail::typeNameOf<Derived>()), ...); });
    }
};

} // namespace Miro

namespace Miro::Detail
{

// Zero-config std::variant overload: each alternative's tag is its
// short C++ type name. For variants of named user types the tags are
// clean ("Circle", "Square"); variants of primitives or std types get
// whatever typeNameOf produces. Users who care write a custom reflect()
// body that calls reflectPolymorphic with explicit tags.
template <typename... Ts>
void reflectValue(Reflector& ref, std::variant<Ts...>& value)
{
    Miro::reflectPolymorphic(
        ref, value, [](auto& d) { (d.template alt<Ts>(typeNameOf<Ts>()), ...); });
}

// EA::Variant derives from std::variant — delegate to the std::variant
// overload through the base so the tag rules live in exactly one place.
template <typename... Ts>
void reflectValue(Reflector& ref, EA::Variant<Ts...>& value)
{
    reflectValue(ref, static_cast<std::variant<Ts...>&>(value));
}

} // namespace Miro::Detail
