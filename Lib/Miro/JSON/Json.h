#pragma once

#include "../Containers.h"

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace Miro::Json
{

struct Value;

using Null = std::nullptr_t;
using Array = Vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

struct Value
{
    using Variant =
        std::variant<Null, bool, std::int64_t, double, std::string, Array, Object>;

    Value();
    Value(Null valueToUse);
    Value(bool valueToUse);
    Value(double valueToUse);
    Value(int valueToUse);
    Value(std::string valueToUse);
    Value(const char* valueToUse);
    Value(Array valueToUse);
    Value(Object valueToUse);

    // Every other integral width, stored exactly as an int64. An
    // unsigned value past INT64_MAX has no exact slot, so it widens to
    // double the way it did before there was an integer alternative.
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    Value(T valueToUse)
    {
        constexpr auto mayExceedInt64 =
            std::unsigned_integral<T> && sizeof(T) >= sizeof(std::int64_t);

        if constexpr (mayExceedInt64)
        {
            constexpr auto largest =
                static_cast<T>(std::numeric_limits<std::int64_t>::max());

            if (valueToUse > largest)
            {
                data = static_cast<double>(valueToUse);
                return;
            }
        }

        data = static_cast<std::int64_t>(valueToUse);
    }

    template <std::floating_point T>
    Value(T valueToUse)
        : data(static_cast<double>(valueToUse))
    {
    }

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isInteger() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    Object& toObject();

    bool asBool() const;
    double asNumber() const;
    std::int64_t asInteger() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;
    Object& asObject();

    operator bool() const;
    operator int() const;
    operator std::int64_t() const;
    operator double() const;
    operator float() const;
    operator std::string() const;

    Value& operator[](const std::string& keyToUse);
    const Value& operator[](const std::string& keyToUse) const;
    Value& operator[](const char* keyToUse);
    const Value& operator[](const char* keyToUse) const;
    Value& operator[](std::size_t indexToUse);
    const Value& operator[](std::size_t indexToUse) const;

    Value& operator[](int indexToUse);
    const Value& operator[](int indexToUse) const;

    // Numeric across the two storage kinds: 1 and 1.0 are the same
    // number however each of them got here. Everything else is
    // structural, so containers compare element by element.
    bool operator==(const Value& otherToUse) const;

    Variant data;
};

// Base of everything the JSON layer throws, so one handler can catch a
// malformed document and a bad access alike.
class Error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class ParseError : public Error
{
    using Error::Error;
};

// Thrown by every Value accessor — a missing key, an out-of-range index,
// or a type mismatch. The message names the key, the index and size, or
// the expected and actual type.
class AccessError : public Error
{
    using Error::Error;
};

Value* find(Object& object, std::string_view key);
const Value* find(const Object& object, std::string_view key);

// Helper used at the JSON-in seam of every command thunk. Lets handlers
// accept `null` as a stand-in for "no fields" — fromJSON would otherwise
// reject null when loading an object-shaped Req.
Value payloadOrEmpty(const Value& payload);

Value parse(std::string_view inputToUse);

// Like parse(), but never throws. On malformed input it catches the
// ParseError (and any other exception) internally and returns a null
// Value instead. Use this at boundaries where a default-constructed
// result is preferable to handling a thrown ParseError.
Value getParsedValue(std::string_view inputToUse);

std::string print(const Value& valueToUse, int indentToUse = 0);
void log(const Value& valueToUse, int indentToUse = 0);
} // namespace Miro::Json

namespace Miro
{
using JSON = Json::Value;
} // namespace Miro
