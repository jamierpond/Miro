#pragma once

#include "PackedStruct.h"
#include "ReflectDispatch.h"

#include <algorithm>
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

template <typename T>
void reflectElements(Reflector& ref, std::span<T> elements, const Options& childOpts)
{
    if constexpr (PackableElement<T>)
    {
        if (ref.visitPacked(PackedArrayRef {elements}))
            return;
    }
    else if constexpr (PackableStruct<T>)
    {
        if (ref.visitPackedStructs(makePackedStructArrayRef(elements)))
            return;
    }

    for (std::size_t i = 0; i < elements.size(); ++i)
        reflectValue(ref.atIndex(i, childOpts), elements[i]);
}

template <typename T>
void reflectArraySchema(Reflector& ref, const Options& childOpts)
{
    auto inner = T {};
    reflectValue(ref.atIndex(0, childOpts), inner);
}

template <typename T>
void reflectValue(Reflector& ref, std::vector<T>& value)
{
    auto childOpts = childOptionsFor<T>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            reflectArraySchema<T>(ref, childOpts);
            return;
        }

        ref.resizeArray(value.size());
        reflectElements(ref, std::span<T> {value}, childOpts);
        return;
    }

    // A missing key keeps the prior value, so a field added with a
    // non-empty default survives loading data written before it.
    if (ref.kind() == ValueKind::Absent)
        return;

    value.resize(ref.arraySize());
    reflectElements(ref, std::span<T> {value}, childOpts);
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
            reflectArraySchema<T>(ref, childOpts);
            return;
        }

        ref.resizeArray(N);
        reflectElements(ref, std::span<T> {value}, childOpts);
        return;
    }

    auto count = std::min(ref.arraySize(), N);
    reflectElements(ref, std::span<T> {value.data(), count}, childOpts);
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
        if (ref.kind() == ValueKind::Absent)
            return;

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
// untouched. A null key reads as absent too when T itself can't hold a
// null (Omittable<int>) — schema dialects that require every key, like
// OpenAI strict mode, send null for "left out". Where T can hold one
// (std::optional, OwningPointer, raw JSON), a null key engages, so
// Omittable<std::optional<T>> reads it back as engaged-but-empty.
template <typename T>
void reflectValue(Reflector& ref, Omittable<T>& value)
{
    constexpr auto nullMeansAbsent = !isOptional<T>() && shapeOf<T>() != Shape::Raw;

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
    else if (ref.kind() == ValueKind::Absent
             || (nullMeansAbsent && ref.kind() == ValueKind::Null))
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
        if (ref.kind() == ValueKind::Absent)
            return;

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
