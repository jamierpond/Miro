#pragma once

#include "ReflectDispatch.h"

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Miro::Detail
{

template <typename T>
constexpr void reflectValue(Reflector& ref, std::vector<T>& value)
{
    auto childOpts = childOptionsFor<T>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            auto inner = T {};
            reflectValue(ref.atIndex(0, childOpts), inner);
        }
        else
        {
            ref.resizeArray(value.size());
            for (std::size_t i = 0; i < value.size(); ++i)
                reflectValue(ref.atIndex(i, childOpts), value[i]);
        }
    }
    else
    {
        value.resize(ref.arraySize());
        for (std::size_t i = 0; i < value.size(); ++i)
            reflectValue(ref.atIndex(i, childOpts), value[i]);
    }
}

template <typename T, std::size_t N>
constexpr void reflectValue(Reflector& ref, std::array<T, N>& value)
{
    auto childOpts = childOptionsFor<T>(ref.options());

    if (ref.isSaving())
    {
        if (ref.isSchema())
        {
            ref.setArrayBounds(N, N);
            auto inner = T {};
            reflectValue(ref.atIndex(0, childOpts), inner);
        }
        else
        {
            ref.resizeArray(N);
            for (std::size_t i = 0; i < N; ++i)
                reflectValue(ref.atIndex(i, childOpts), value[i]);
        }
    }
    else
    {
        auto size = ref.arraySize();
        auto count = size < N ? size : N;
        for (std::size_t i = 0; i < count; ++i)
            reflectValue(ref.atIndex(i, childOpts), value[i]);
    }
}

template <typename V>
void reflectValue(Reflector& ref, std::map<std::string, V>& value)
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
constexpr void reflectValue(Reflector& ref, std::optional<T>& value)
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
