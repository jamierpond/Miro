#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

// Converts a loaded number into the field's type only if it fits; otherwise
// returns false and leaves the field alone, like a missing key.

namespace Miro::Detail
{

// 2^digits as a double: one past the largest value of T, exact for
// every integer width (static_cast<double>(max) would have to round).
template <std::integral T>
constexpr double calculateIntegerUpperBound()
{
    auto bound = 1.0;

    for (auto i = 0; i < std::numeric_limits<T>::digits; ++i)
        bound *= 2.0;

    return bound;
}

template <std::integral T>
bool isDoubleInIntegerRange(double value)
{
    constexpr auto upper = calculateIntegerUpperBound<T>();
    constexpr auto lower = std::is_signed_v<T> ? -upper : 0.0;

    // NaN fails both comparisons.
    return value >= lower && value < upper;
}

template <std::floating_point T>
bool isDoubleInFloatingRange(double value)
{
    if constexpr (sizeof(T) < sizeof(double))
    {
        constexpr auto largest = static_cast<double>(std::numeric_limits<T>::max());
        return !std::isfinite(value) || std::fabs(value) <= largest;
    }
    else
    {
        return true;
    }
}

// std::in_range refuses char types, which are valid fields.
template <std::integral T>
bool isInt64InIntegerRange(std::int64_t value)
{
    using Limits = std::numeric_limits<T>;

    if constexpr (std::is_signed_v<T>)
    {
        return value >= static_cast<std::int64_t>(Limits::min())
               && value <= static_cast<std::int64_t>(Limits::max());
    }
    else
    {
        const auto unsignedValue = static_cast<std::uint64_t>(value);
        const auto largest = static_cast<std::uint64_t>(Limits::max());
        return value >= 0 && unsignedValue <= largest;
    }
}

template <typename T>
bool convertNumber(double value, T& out)
{
    if constexpr (std::integral<T>)
    {
        if (!isDoubleInIntegerRange<T>(value))
            return false;
    }
    else if (!isDoubleInFloatingRange<T>(value))
    {
        return false;
    }

    out = static_cast<T>(value);
    return true;
}

template <typename T>
bool convertNumber(std::int64_t value, T& out)
{
    if constexpr (std::integral<T>)
    {
        if (!isInt64InIntegerRange<T>(value))
            return false;
    }

    out = static_cast<T>(value);
    return true;
}

} // namespace Miro::Detail
