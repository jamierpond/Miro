#pragma once

#include "ReflectDispatch.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Miro::Detail
{

// Element types eligible for the bulk visitPacked() fast path (see
// PackedArrayRef in Reflector.h). Deliberately the exact fixed-width
// set the wire formats know how to pack — enums and odd integer
// widths keep the per-element walk.
template <typename T>
concept PackableElement =
    std::same_as<T, std::int32_t> || std::same_as<T, std::int64_t>
    || std::same_as<T, float> || std::same_as<T, double>;

template <typename T>
void reflectValue(Reflector& ref, std::vector<T>& value)
{
    auto childOpts = childOptionsFor<T>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = T {};
            reflectValue(ref.atIndex(0, childOpts), inner);
            return;
        }

        if constexpr (PackableElement<T>)
            if (ref.visitPacked(PackedArrayRef {std::span<T> {value}}))
                return;

        ref.resizeArray(value.size());
        for (std::size_t i = 0; i < value.size(); ++i)
            reflectValue(ref.atIndex(i, childOpts), value[i]);
    }
    else
    {
        value.resize(ref.arraySize());

        if constexpr (PackableElement<T>)
            if (ref.visitPacked(PackedArrayRef {std::span<T> {value}}))
                return;

        for (std::size_t i = 0; i < value.size(); ++i)
            reflectValue(ref.atIndex(i, childOpts), value[i]);
    }
}

template <typename T, std::size_t N>
void reflectValue(Reflector& ref, std::array<T, N>& value)
{
    auto childOpts = childOptionsFor<T>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            ref.setArrayBounds(N, N);
            auto inner = T {};
            reflectValue(ref.atIndex(0, childOpts), inner);
            return;
        }

        if constexpr (PackableElement<T>)
            if (ref.visitPacked(PackedArrayRef {std::span<T> {value}}))
                return;

        ref.resizeArray(N);
        for (std::size_t i = 0; i < N; ++i)
            reflectValue(ref.atIndex(i, childOpts), value[i]);
    }
    else
    {
        auto size = ref.arraySize();
        auto count = size < N ? size : N;

        if constexpr (PackableElement<T>)
            if (ref.visitPacked(PackedArrayRef {std::span<T> {value.data(), count}}))
                return;

        for (std::size_t i = 0; i < count; ++i)
            reflectValue(ref.atIndex(i, childOpts), value[i]);
    }
}

template <typename V, typename Compare>
void reflectValue(Reflector& ref, std::map<std::string, V, Compare>& value)
{
    auto childOpts = childOptionsFor<V>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = V {};
            reflectValue(ref.atKey("*", childOpts), inner);
        }
        else
        {
            for (auto& [key, element]: value)
                reflectValue(ref.atKey(key, childOpts), element);
        }
    }
    else
    {
        value.clear();
        for (auto& key: ref.mapKeys())
        {
            auto element = V {};
            reflectValue(ref.atKey(key, childOpts), element);
            value.emplace(key, std::move(element));
        }
    }
}

template <typename T>
void reflectValue(Reflector& ref, std::optional<T>& value)
{
    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = T {};
            reflectValue(ref, inner);
        }
        else if (value)
        {
            reflectValue(ref, *value);
        }
        else
        {
            ref.writeNull();
        }
    }
    else
    {
        auto k = ref.kind();

        if (k == ValueKind::Null)
        {
            value.reset();
        }
        else if (k != ValueKind::Absent)
        {
            auto inner = T {};
            reflectValue(ref, inner);
            value = std::move(inner);
        }
    }
}

// Omittable<T> controls whether the key exists at all — the mirror
// image of std::optional, which controls whether the value is null.
//
// Saving, disengaged: we touch nothing, so the slot the parent staged
// for us is never claimed and the key never appears. Engaged: markPresent()
// tells the parent to keep the key (even when the inner reflect body
// writes nothing, as for an empty struct), then the inner T is reflected
// into the same slot as usual.
//
// Loading is the one place where "absent" carries meaning rather than
// being ignored: a missing key resets the value instead of leaving it
// untouched. Anything present — null included — engages, so
// Omittable<std::optional<T>> reads a null key back as engaged-but-empty.
template <typename T>
void reflectValue(Reflector& ref, Omittable<T>& value)
{
    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = T {};
            reflectValue(ref, inner);
        }
        else if (value)
        {
            ref.markPresent();
            reflectValue(ref, *value);
        }
    }
    else if (ref.kind() == ValueKind::Absent)
    {
        value.reset();
    }
    else
    {
        auto inner = T {};
        reflectValue(ref, inner);
        value = std::move(inner);
    }
}

// Vector wraps std::vector — delegate to the std::vector overload via
// getVector() so dispatch logic lives in exactly one place.
template <typename T, typename Allocator>
void reflectValue(Reflector& ref, Vector<T, Allocator>& value)
{
    reflectValue(ref, value.getVector());
}

// Array wraps std::array (with int Size → size_t under the hood) —
// delegate the same way.
template <typename T, int N>
void reflectValue(Reflector& ref, Array<T, N>& value)
{
    reflectValue(ref, value.getArray());
}

// EA::MapVector is a vector-of-pairs, not a std::map wrapper, so we walk
// it directly. Iteration order is insertion order.
template <typename V>
void reflectValue(Reflector& ref, EA::MapVector<std::string, V>& value)
{
    auto childOpts = childOptionsFor<V>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = V {};
            reflectValue(ref.atKey("*", childOpts), inner);
        }
        else
        {
            for (auto& pair: value)
                reflectValue(ref.atKey(pair.first, childOpts), pair.second);
        }
    }
    else
    {
        value.clear();
        for (auto& key: ref.mapKeys())
        {
            auto& slot = value[key];
            reflectValue(ref.atKey(key, childOpts), slot);
        }
    }
}

// OwningPointer is a unique_ptr-like with a raw T* underneath — reflect
// as a nullable slot (mirrors std::optional shape).
template <typename T>
void reflectValue(Reflector& ref, OwningPointer<T>& value)
{
    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = T {};
            reflectValue(ref, inner);
        }
        else if (value.get() != nullptr)
        {
            reflectValue(ref, *value);
        }
        else
        {
            ref.writeNull();
        }
    }
    else
    {
        auto k = ref.kind();

        if (k == ValueKind::Null)
        {
            value.reset();
        }
        else if (k != ValueKind::Absent)
        {
            value.create();
            reflectValue(ref, *value);
        }
    }
}

} // namespace Miro::Detail
