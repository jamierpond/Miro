#pragma once

#include <optional>
#include <utility>

namespace Miro
{

// An optional-like wrapper whose disengaged state means "this key is
// not in the document at all", where std::optional's disengaged state
// means "this key is present and null".
//
// REST APIs routinely need both: in a PATCH body an absent field means
// "leave it alone" while a null field means "clear it". Discord's type
// notation spells them `field?` and `?type`, and they compose —
// `Omittable<std::optional<T>>` is the full three-state model of
// absent / null / value.
//
// Deliberately not std::optional: the reflection layer keys off the C++
// type to decide whether a slot is allowed to vanish, so reusing
// std::optional would change what every existing optional means on the
// wire. The interface is the slice of std::optional Miro needs; T must
// be default-constructible, as it must be everywhere else in the
// reflection layer.
template <typename T>
class Omittable
{
public:
    using ValueType = T;

    Omittable() = default;

    Omittable(const T& valueToUse)
        : storage(valueToUse)
    {
    }

    Omittable(T&& valueToUse)
        : storage(std::move(valueToUse))
    {
    }

    bool has_value() const { return storage.has_value(); }
    explicit operator bool() const { return storage.has_value(); }

    T& operator*() { return *storage; }
    const T& operator*() const { return *storage; }

    T* operator->() { return &*storage; }
    const T* operator->() const { return &*storage; }

    void reset() { storage.reset(); }

    template <typename... Args>
    T& emplace(Args&&... args)
    {
        return storage.emplace(std::forward<Args>(args)...);
    }

    // Hidden friend rather than a member so the implicit T -> Omittable
    // conversion applies to either operand: both `slot == 5` and
    // `5 == slot` compile for an Omittable<int>.
    friend bool operator==(const Omittable& lhs, const Omittable& rhs) = default;

private:
    std::optional<T> storage;
};

} // namespace Miro
