#pragma once

#include "../JSON/Generic.h"
#include "ReflectDispatch.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Compile-time JSON serde backend.
//
// Json::Value can't exist in a constant expression (std::map is not
// constexpr), so this provides a constexpr mirror of the document
// model — object members are kept key-sorted, matching std::map
// iteration order — plus a constexpr port of JsonReflector. The
// parser and printer are NOT duplicated here: the generic templates
// in JSON/Generic.h are instantiated with the mirror Value, so the
// compile-time path runs the same algorithms as the runtime path and
// produces identical bytes by construction. The public serde entry
// points (toJSONString / fromJSONString / createFromJSONString in
// Serialize.h) route here when constant-evaluated.

namespace Miro::Detail::ConstexprJson
{

struct Member;

// Constexpr mirror of Json::Value: same read accessors and scalar
// constructors (so generic code compiles identically against both),
// with public alternative fields instead of a variant.
struct Value
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    // Defined below, after Member is complete: a user-provided
    // constructor odr-uses the vector<Member> member's destructor for
    // cleanup, which needs the complete type.
    Value() = default;
    constexpr Value(std::nullptr_t);
    constexpr Value(bool valueToUse);
    constexpr Value(double valueToUse);
    constexpr Value(std::string valueToUse);

    constexpr bool isNull() const { return kind == Kind::Null; }
    constexpr bool isBool() const { return kind == Kind::Bool; }
    constexpr bool isNumber() const { return kind == Kind::Number; }
    constexpr bool isString() const { return kind == Kind::String; }
    constexpr bool isArray() const { return kind == Kind::Array; }
    constexpr bool isObject() const { return kind == Kind::Object; }

    constexpr bool asBool() const { return boolValue; }
    constexpr double asNumber() const { return numberValue; }
    constexpr const std::string& asString() const { return stringValue; }
    constexpr const std::vector<Value>& asArray() const { return arrayValue; }
    constexpr const std::vector<Member>& asObject() const { return objectValue; }

    Kind kind = Kind::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<Value> arrayValue;
    std::vector<Member> objectValue;
};

struct Member
{
    std::string key;
    Value value;
};

constexpr Value::Value(std::nullptr_t) {}

constexpr Value::Value(bool valueToUse)
    : kind(Kind::Bool)
    , boolValue(valueToUse)
{
}

constexpr Value::Value(double valueToUse)
    : kind(Kind::Number)
    , numberValue(valueToUse)
{
}

constexpr Value::Value(std::string valueToUse)
    : kind(Kind::String)
    , stringValue(std::move(valueToUse))
{
}

using Array = std::vector<Value>;
using Object = std::vector<Member>;

constexpr void resetTo(Value& slot, Value::Kind kindToUse)
{
    slot = Value {};
    slot.kind = kindToUse;
}

constexpr Value* find(Object& members, std::string_view key)
{
    for (auto& member: members)
        if (member.key == key)
            return &member.value;

    return nullptr;
}

// Sorted insert returning the slot for `key` — the existing one when
// present (std::map::operator[] semantics for the save path).
constexpr Value& findOrInsert(Object& members, std::string_view key)
{
    auto index = std::size_t {0};

    while (index < members.size() && members[index].key < key)
        ++index;

    if (index >= members.size() || members[index].key != key)
        members.insert(members.begin() + static_cast<std::ptrdiff_t>(index),
                       Member {std::string {key}, Value {}});

    return members[index].value;
}

// Construction seams used by the generic parsers (see
// Detail/DocumentOps.h for the runtime Json::Value overloads).
// Insertion is sorted and first-occurrence-wins, matching
// std::map::emplace iteration order and semantics.

constexpr void setEmptyArray(Value& value)
{
    resetTo(value, Value::Kind::Array);
}

constexpr void setEmptyObject(Value& value)
{
    resetTo(value, Value::Kind::Object);
}

constexpr void reserveArray(Value& value, std::size_t count)
{
    value.arrayValue.reserve(count);
}

constexpr void appendElement(Value& value, Value element)
{
    value.arrayValue.push_back(std::move(element));
}

constexpr void insertMember(Value& value, std::string key, Value member)
{
    auto& members = value.objectValue;
    auto index = std::size_t {0};

    while (index < members.size() && members[index].key < key)
        ++index;

    if (index < members.size() && members[index].key == key)
        return;

    members.insert(members.begin() + static_cast<std::ptrdiff_t>(index),
                   Member {std::move(key), std::move(member)});
}

// --- Reflector ---

template <typename T>
constexpr void writeSlotFromPrimitive(Value& slot, const T& valueToUse)
{
    if constexpr (std::same_as<T, bool>)
    {
        resetTo(slot, Value::Kind::Bool);
        slot.boolValue = valueToUse;
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        resetTo(slot, Value::Kind::String);
        slot.stringValue = valueToUse;
    }
    else
    {
        resetTo(slot, Value::Kind::Number);
        slot.numberValue = static_cast<double>(valueToUse);
    }
}

template <typename T>
constexpr void readSlotIntoPrimitive(const Value& slot, T& valueToUse)
{
    if constexpr (std::same_as<T, bool>)
    {
        if (slot.kind == Value::Kind::Bool)
            valueToUse = slot.boolValue;
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        if (slot.kind == Value::Kind::String)
            valueToUse = slot.stringValue;
    }
    else
    {
        if (slot.kind == Value::Kind::Number)
            valueToUse = static_cast<T>(slot.numberValue);
    }
}

// Constexpr port of JsonReflector: one slot per reflector, shape
// committed eagerly when saving, children spawned via atKey/atIndex
// and owned until the next spawn. Loading from a missing key/index
// goes through an `absent` child whose operations no-op.
//
// Deliberately a class template (always instantiated as
// BasicValueReflector<Value>): a plain class would have its member
// bodies compiled at header-parse time, leaving the template
// specializations they reference (get_if, string ops, the visit
// helpers) in clang's pending-instantiation queue until end of TU —
// which a static_assert in user code evaluates before. As a template
// instantiated from toText/fromText, the whole body — including the
// virtuals, via the vtable — is instantiated at the point of use,
// inside the constant-evaluation context, which instantiates the
// helpers eagerly. std::visit is avoided for the same reason: its
// function-pointer dispatch matrix hides the callee from on-demand
// instantiation entirely.
template <typename SlotValue>
class BasicValueReflector final : public Reflector
{
public:
    using Kind = typename SlotValue::Kind;

    constexpr BasicValueReflector(SlotValue& slotToUse, Options optsToUse)
        : BasicValueReflector(slotToUse, optsToUse, false)
    {
    }

    constexpr ~BasicValueReflector() override { delete currentChild; }

    constexpr void visit(PrimitiveRef ref) override
    {
        visitAlternative<bool>(ref) || visitAlternative<int>(ref)
            || visitAlternative<double>(ref) || visitAlternative<std::string>(ref)
            || visitAlternative<std::int64_t>(ref);
    }

    constexpr void writeNull() override { resetTo(slot, Kind::Null); }

    constexpr ValueKind kind() const override
    {
        if (absent)
            return ValueKind::Absent;

        switch (slot.kind)
        {
            case Kind::Null:
                return ValueKind::Null;
            case Kind::Bool:
                return ValueKind::Bool;
            case Kind::Number:
                return ValueKind::Number;
            case Kind::String:
                return ValueKind::String;
            case Kind::Array:
                return ValueKind::Array;
            case Kind::Object:
                return ValueKind::Object;
        }

        return ValueKind::Absent;
    }

    constexpr Reflector& atKey(std::string_view key, Options childOpts) override
    {
        if (isSaving())
        {
            if (!slot.isObject())
                resetTo(slot, Kind::Object);

            return spawnChild(findOrInsert(slot.objectValue, key), childOpts, false);
        }

        if (!slot.isObject())
            return spawnMissingChild(childOpts);

        if (auto* found = find(slot.objectValue, key))
            return spawnChild(*found, childOpts, false);

        return spawnMissingChild(childOpts);
    }

    constexpr Reflector& atIndex(std::size_t index, Options childOpts) override
    {
        if (isSaving())
        {
            if (!slot.isArray())
                resetTo(slot, Kind::Array);

            if (slot.arrayValue.size() <= index)
                slot.arrayValue.resize(index + 1);

            return spawnChild(slot.arrayValue[index], childOpts, false);
        }

        if (!slot.isArray() || index >= slot.arrayValue.size())
            return spawnMissingChild(childOpts);

        return spawnChild(slot.arrayValue[index], childOpts, false);
    }

    constexpr std::size_t arraySize() const override
    {
        return slot.isArray() ? slot.arrayValue.size() : 0;
    }

    constexpr void resizeArray(std::size_t newSize) override
    {
        if (!slot.isArray())
            resetTo(slot, Kind::Array);

        slot.arrayValue.resize(newSize);
    }

private:
    constexpr BasicValueReflector(SlotValue& slotToUse,
                                  Options optsToUse,
                                  bool absentToUse)
        : Reflector(optsToUse)
        , slot(slotToUse)
        , absent(absentToUse)
    {
        if (isSaving() && !absent)
            commitShape();
    }

    constexpr void commitShape()
    {
        switch (opts.shape)
        {
            case Shape::Primitive:
                break;
            case Shape::Object:
            case Shape::Map:
                if (!slot.isObject())
                    resetTo(slot, Kind::Object);
                break;
            case Shape::Array:
                if (!slot.isArray())
                    resetTo(slot, Kind::Array);
                break;
        }
    }

    template <typename T>
    constexpr bool visitAlternative(PrimitiveRef& ref)
    {
        auto* pointer = std::get_if<T*>(&ref.data);

        if (pointer == nullptr)
            return false;

        if (isSaving())
            writeSlotFromPrimitive(slot, **pointer);
        else
            readSlotIntoPrimitive(slot, **pointer);

        return true;
    }

    constexpr Reflector&
        spawnChild(SlotValue& targetSlot, Options childOpts, bool absentToUse)
    {
        delete currentChild;
        currentChild = nullptr;
        currentChild = new BasicValueReflector(targetSlot, childOpts, absentToUse);
        return *currentChild;
    }

    constexpr Reflector& spawnMissingChild(Options childOpts)
    {
        missingSlot = SlotValue {};
        return spawnChild(missingSlot, childOpts, true);
    }

    SlotValue& slot;
    bool absent = false;
    SlotValue missingSlot;
    BasicValueReflector* currentChild = nullptr;
};

using ValueReflector = BasicValueReflector<Value>;

// --- Document text via the shared generic JSON parser / printer ---

constexpr Value parse(std::string_view inputToUse)
{
    return Json::Generic::parse<Value>(inputToUse);
}

constexpr std::string print(const Value& valueToUse, int indentToUse)
{
    return Json::Generic::print(valueToUse, indentToUse);
}

// --- Serde plumbing (shared with the YAML backend) ---

template <typename T>
constexpr Value reflectToValue(const T& value)
{
    auto root = Value {};
    auto ref = ValueReflector {root, topLevelOptions<T>(Mode::Save)};

    using Detail::reflectValue;
    reflectValue(ref, const_cast<T&>(value));

    return root;
}

template <typename T>
constexpr void loadFromValue(T& value, Value root)
{
    auto ref = ValueReflector {root, topLevelOptions<T>(Mode::Load)};

    using Detail::reflectValue;
    reflectValue(ref, value);
}

// --- Entry points (called from Serialize.h during constant
// evaluation) ---

template <typename T>
constexpr std::string toText(const T& value, int indent)
{
    return Json::Generic::print(reflectToValue(value), indent);
}

template <typename T>
constexpr void fromText(T& value, std::string_view text)
{
    loadFromValue(value, parse(text));
}

} // namespace Miro::Detail::ConstexprJson
