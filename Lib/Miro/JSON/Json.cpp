#include "Json.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace Miro::Json
{

namespace
{

// One overload per Value::Variant alternative, and the only place a
// JSON type is spelled — a new alternative needs one line here.
struct TypeNames
{
    static std::string_view of(Null) { return "null"; }
    static std::string_view of(bool) { return "bool"; }
    static std::string_view of(std::int64_t) { return "number"; }
    static std::string_view of(double) { return "number"; }
    static std::string_view of(const std::string&) { return "string"; }
    static std::string_view of(const Array&) { return "array"; }
    static std::string_view of(const Object&) { return "object"; }
};

std::string_view typeNameOf(const Value& valueToUse)
{
    return std::visit([](const auto& heldToUse) { return TypeNames::of(heldToUse); },
                      valueToUse.data);
}

template <typename T, typename ValueType>
std::conditional_t<std::is_const_v<ValueType>, const T, T>&
    checkedGet(ValueType& valueToUse)
{
    if (auto* held = std::get_if<T>(&valueToUse.data))
        return *held;

    throw AccessError("expected " + std::string(TypeNames::of(T {}))
                      + " but value is " + std::string(typeNameOf(valueToUse)));
}

// A double names an int64 only when it is finite, has no fractional
// part, and lands inside [-2^63, 2^63) — the half-open range int64
// actually covers, both bounds exactly representable as doubles.
std::optional<std::int64_t> exactInteger(double number)
{
    constexpr auto limit = 9223372036854775808.0;

    if (!std::isfinite(number) || number != std::trunc(number))
        return std::nullopt;

    if (number < -limit || number >= limit)
        return std::nullopt;

    return static_cast<std::int64_t>(number);
}

bool sameNumber(const Value& left, const Value& right)
{
    if (left.isInteger() && right.isInteger())
        return left.asInteger() == right.asInteger();

    if (!left.isInteger() && !right.isInteger())
        return left.asNumber() == right.asNumber();

    const auto& integer = left.isInteger() ? left : right;
    const auto& real = left.isInteger() ? right : left;

    auto exact = exactInteger(real.asNumber());
    return exact.has_value() && *exact == integer.asInteger();
}

} // namespace

Value::Value()
    : data(nullptr)
{
}

Value::Value(Null /*valueToUse*/)
    : data(nullptr)
{
}

Value::Value(bool valueToUse)
    : data(valueToUse)
{
}

Value::Value(double valueToUse)
    : data(valueToUse)
{
}

Value::Value(int valueToUse)
    : data(static_cast<std::int64_t>(valueToUse))
{
}

Value::Value(std::string valueToUse)
    : data(std::move(valueToUse))
{
}

Value::Value(const char* valueToUse)
    : data(std::string(valueToUse))
{
}

Value::Value(Array valueToUse)
    : data(std::move(valueToUse))
{
}

Value::Value(Object valueToUse)
    : data(std::move(valueToUse))
{
}

bool Value::isNull() const
{
    return std::holds_alternative<Null>(data);
}

bool Value::isBool() const
{
    return std::holds_alternative<bool>(data);
}

bool Value::isNumber() const
{
    return isInteger() || std::holds_alternative<double>(data);
}

bool Value::isInteger() const
{
    return std::holds_alternative<std::int64_t>(data);
}

bool Value::isString() const
{
    return std::holds_alternative<std::string>(data);
}

bool Value::isArray() const
{
    return std::holds_alternative<Array>(data);
}

bool Value::isObject() const
{
    return std::holds_alternative<Object>(data);
}

Object& Value::toObject()
{
    if (!isObject())
        data = Object();

    return asObject();
}

bool Value::asBool() const
{
    return checkedGet<bool>(*this);
}

double Value::asNumber() const
{
    if (auto* integer = std::get_if<std::int64_t>(&data))
        return static_cast<double>(*integer);

    return checkedGet<double>(*this);
}

std::int64_t Value::asInteger() const
{
    if (auto* integer = std::get_if<std::int64_t>(&data))
        return *integer;

    if (auto exact = exactInteger(checkedGet<double>(*this)))
        return *exact;

    throw AccessError("expected integer but value is a number with no exact "
                      "64-bit integer representation");
}

const std::string& Value::asString() const
{
    return checkedGet<std::string>(*this);
}

const Array& Value::asArray() const
{
    return checkedGet<Array>(*this);
}

const Object& Value::asObject() const
{
    return checkedGet<Object>(*this);
}

Object& Value::asObject()
{
    return checkedGet<Object>(*this);
}

Value::operator bool() const
{
    return asBool();
}
Value::operator int() const
{
    return static_cast<int>(asNumber());
}
Value::operator std::int64_t() const
{
    return asInteger();
}
Value::operator double() const
{
    return asNumber();
}
Value::operator float() const
{
    return static_cast<float>(asNumber());
}
Value::operator std::string() const
{
    return asString();
}

Value& Value::operator[](const std::string& keyToUse)
{
    return std::get<Object>(data).at(keyToUse);
}

const Value& Value::operator[](const std::string& keyToUse) const
{
    return std::get<Object>(data).at(keyToUse);
}

Value& Value::operator[](const char* keyToUse)
{
    return operator[](std::string(keyToUse));
}

const Value& Value::operator[](const char* keyToUse) const
{
    return operator[](std::string(keyToUse));
}

Value& Value::operator[](std::size_t indexToUse)
{
    return std::get<Array>(data)[indexToUse];
}

const Value& Value::operator[](std::size_t indexToUse) const
{
    return std::get<Array>(data)[indexToUse];
}
Value& Value::operator[](int indexToUse)
{
    return std::get<Array>(data)[indexToUse];
}

const Value& Value::operator[](int indexToUse) const
{
    return operator[]((size_t) indexToUse);
}

bool Value::operator==(const Value& otherToUse) const
{
    if (isNumber() && otherToUse.isNumber())
        return sameNumber(*this, otherToUse);

    return data == otherToUse.data;
}

Value* find(Object& object, std::string_view key)
{
    auto it = object.find(key);

    if (it != object.end())
        return &it->second;

    return nullptr;
}

const Value* find(const Object& object, std::string_view key)
{
    auto it = object.find(key);

    if (it != object.end())
        return &it->second;

    return nullptr;
}

Value payloadOrEmpty(const Value& payload)
{
    if (payload.isNull())
        return Value {Object {}};

    return payload;
}

} // namespace Miro::Json
