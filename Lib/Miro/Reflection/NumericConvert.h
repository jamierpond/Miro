#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

// Range-checked conversion of a decoded number into the C++ type of the
// field being loaded. A plain static_cast is undefined for a double
// outside the target's range (or NaN) and silently wraps a too-wide
// integer, so a field whose type changed between writer and reader
// (int64 -> int32, double -> int, ...) could load garbage. Here a value
// that doesn't fit is refused: the function returns false and leaves
// `out` alone, the same "keep the prior value" rule the loaders apply to
// a missing key or a mismatched type. A fractional double that is in
// range truncates toward zero, as it always has.

namespace Miro::Detail
{

// 2^digits as a double: one past the largest value of T, exact for
// every integer width (static_cast<double>(max) would have to round).
template <std::integral T>
constexpr double integerUpperBound()
{
    auto bound = 1.0;

    for (auto i = 0; i < std::numeric_limits<T>::digits; ++i)
        bound *= 2.0;

    return bound;
}

template <typename T>
bool convertNumber(double value, T& out)
{
    if constexpr (std::integral<T>)
    {
        constexpr auto upper = integerUpperBound<T>();
        constexpr auto lower = std::is_signed_v<T> ? -upper : 0.0;

        // NaN fails both comparisons.
        if (!(value >= lower && value < upper))
            return false;

        out = static_cast<T>(value);
        return true;
    }
    else
    {
        if constexpr (sizeof(T) < sizeof(double))
            if (std::isfinite(value)
                && std::fabs(value)
                       > static_cast<double>(std::numeric_limits<T>::max()))
                return false;

        out = static_cast<T>(value);
        return true;
    }
}

template <typename T>
bool convertNumber(std::int64_t value, T& out)
{
    if constexpr (std::integral<T>)
    {
        // std::in_range refuses char types, which are valid fields.
        using Limits = std::numeric_limits<T>;

        if constexpr (std::is_signed_v<T>)
        {
            if (value < static_cast<std::int64_t>(Limits::min())
                || value > static_cast<std::int64_t>(Limits::max()))
                return false;
        }
        else
        {
            if (value < 0
                || static_cast<std::uint64_t>(value)
                       > static_cast<std::uint64_t>(Limits::max()))
                return false;
        }
    }

    out = static_cast<T>(value);
    return true;
}

} // namespace Miro::Detail
