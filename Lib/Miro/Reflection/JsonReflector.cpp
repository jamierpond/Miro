#include "JsonReflector.h"

#include <concepts>
#include <type_traits>

namespace Miro
{
namespace
{

ValueKind kindOf(const JSON& value)
{
    if (value.isNull())
        return ValueKind::Null;
    if (value.isBool())
        return ValueKind::Bool;
    if (value.isNumber())
        return ValueKind::Number;
    if (value.isString())
        return ValueKind::String;
    if (value.isArray())
        return ValueKind::Array;
    if (value.isObject())
        return ValueKind::Object;

    return ValueKind::Absent;
}

Json::Array& ensureArray(JSON& slot)
{
    if (!slot.isArray())
        slot.data.emplace<Json::Array>();

    return std::get<Json::Array>(slot.data);
}

Json::Object& ensureObject(JSON& slot)
{
    if (!slot.isObject())
        slot = JSON {Json::Object {}};

    return slot.asObject();
}

// Every alternative of PrimitiveRef has a Value constructor that stores
// it exactly — an integral one reaches the int64 alternative — so
// nothing is widened through double on the way out.
template <typename T>
void writeSlotFromPrimitive(JSON& slot, T* ptr)
{
    slot = JSON {*ptr};
}

template <typename T>
void readSlotIntoPrimitive(const JSON& slot, T* ptr)
{
    if constexpr (std::same_as<T, bool>)
    {
        if (slot.isBool())
            *ptr = slot.asBool();
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        if (slot.isString())
            *ptr = slot.asString();
    }
    else if constexpr (std::integral<T>)
    {
        // An integer slot is read exactly; a double one still converts,
        // so {"n": 5.0} keeps loading into an int field.
        if (slot.isInteger())
            *ptr = static_cast<T>(slot.asInteger());
        else if (slot.isNumber())
            *ptr = static_cast<T>(slot.asNumber());
    }
    else
    {
        if (slot.isNumber())
            *ptr = static_cast<T>(slot.asNumber());
    }
}

} // namespace

JsonReflector::JsonReflector(JSON& slotToUse, Options optsToUse)
    : JsonReflector(slotToUse, optsToUse, false)
{
}

JsonReflector::JsonReflector(JSON& slotToUse, Options optsToUse, bool absentToUse)
    : Reflector(optsToUse)
    , slot(&slotToUse)
    , absent(absentToUse)
{
    if (isSaving() && !absent)
        commitShape();
}

JsonReflector::~JsonReflector() = default;

void JsonReflector::commitShape()
{
    switch (opts.shape)
    {
        case Shape::Primitive:
            // visit() will write the value directly into the slot.
            break;
        case Shape::Object:
        case Shape::Map:
            ensureObject(*slot);
            break;
        case Shape::Array:
            ensureArray(*slot);
            break;
        case Shape::Raw:
            // The value decides, and it hasn't been written yet — every
            // other kind overwrites the slot on its way out (visit,
            // writeNull, resizeArray), but an empty object writes
            // nothing at all, so start as one and it survives.
            ensureObject(*slot);
            break;
    }
}

ValueKind JsonReflector::kind() const
{
    if (absent)
        return ValueKind::Absent;

    return kindOf(*slot);
}

bool JsonReflector::isIntegerNumber() const
{
    return !absent && slot->isInteger();
}

void JsonReflector::writeNull()
{
    *slot = JSON {nullptr};
}

void JsonReflector::markPresent()
{
    // Only an omittable child has an owner to claim a key from; anywhere
    // else (array element, map value already keyed, the root) the slot
    // exists unconditionally and there is nothing to do.
    if (owner == nullptr)
        return;

    slot = &owner->claimPendingKey();
    owner = nullptr;
}

// Creates the key this child was staged for and hands back the real
// slot. Whatever the child staged so far — at this point only its
// eagerly committed shape — moves across, so an engaged Omittable of an
// empty struct still writes {}.
JSON& JsonReflector::claimPendingKey()
{
    auto& target = ensureObject(*slot)[pendingKey];
    target = std::move(pendingSlot);
    pendingSlot = JSON {};
    return target;
}

void JsonReflector::visit(PrimitiveRef ref)
{
    if (isSaving())
        savePrimitive(ref);
    else
        loadPrimitive(ref);
}

void JsonReflector::savePrimitive(PrimitiveRef ref)
{
    std::visit([this](auto* ptr) { writeSlotFromPrimitive(*slot, ptr); }, ref.data);
}

void JsonReflector::loadPrimitive(PrimitiveRef ref)
{
    std::visit([this](auto* ptr) { readSlotIntoPrimitive(*slot, ptr); }, ref.data);
}

Reflector&
    JsonReflector::spawnChild(JSON& targetSlot, Options childOpts, bool absentToUse)
{
    // Destroy the previous child *before* constructing the new one so
    // any subclass with destructor side effects (e.g. emitting close
    // brackets) sees a strict open-then-close ordering.
    currentChild.reset();
    currentChild = new JsonReflector(targetSlot, childOpts, absentToUse);
    return *currentChild;
}

Reflector& JsonReflector::spawnMissingChild(Options childOpts)
{
    missingSlot = JSON {nullptr};
    return spawnChild(missingSlot, childOpts, true);
}

// Points the child at the staging slot instead of a real key. Nothing
// is written to the parent object unless the child claims it back via
// markPresent(); an abandoned staging slot is simply reused by the next
// omittable sibling.
Reflector& JsonReflector::spawnOmittableChild(std::string_view key,
                                              Options childOpts)
{
    pendingKey = std::string {key};
    pendingSlot = JSON {};

    spawnChild(pendingSlot, childOpts, false);
    currentChild->owner = this;
    return *currentChild;
}

Reflector& JsonReflector::atKey(std::string_view key, Options childOpts)
{
    if (isSaving())
        return atKeyForSave(key, childOpts);

    return atKeyForLoad(key, childOpts);
}

Reflector& JsonReflector::atKeyForSave(std::string_view key, Options childOpts)
{
    // The parent object is committed either way: a struct whose every
    // key is omitted still saves as {}.
    auto& obj = ensureObject(*slot);

    if (childOpts.omittable)
        return spawnOmittableChild(key, childOpts);

    return spawnChild(obj[std::string {key}], childOpts, false);
}

Reflector& JsonReflector::atKeyForLoad(std::string_view key, Options childOpts)
{
    if (!slot->isObject())
        return spawnMissingChild(childOpts);

    auto& obj = slot->asObject();
    auto it = obj.find(std::string {key});

    if (it == obj.end())
        return spawnMissingChild(childOpts);

    return spawnChild(it->second, childOpts, false);
}

Reflector& JsonReflector::atIndex(std::size_t index, Options childOpts)
{
    // No staging here: a JSON array has no way to express a hole, so an
    // omittable element just writes (or leaves) null.
    if (isSaving())
        return atIndexForSave(index, childOpts);

    return atIndexForLoad(index, childOpts);
}

Reflector& JsonReflector::atIndexForSave(std::size_t index, Options childOpts)
{
    auto& arr = ensureArray(*slot).getVector();

    if (arr.size() <= index)
        arr.resize(index + 1);

    return spawnChild(arr[index], childOpts, false);
}

Reflector& JsonReflector::atIndexForLoad(std::size_t index, Options childOpts)
{
    if (!slot->isArray())
        return spawnMissingChild(childOpts);

    auto& arr = std::get<Json::Array>(slot->data).getVector();

    if (index >= arr.size())
        return spawnMissingChild(childOpts);

    return spawnChild(arr[index], childOpts, false);
}

std::size_t JsonReflector::arraySize() const
{
    return slot->isArray() ? slot->asArray().getVector().size() : 0;
}

void JsonReflector::resizeArray(std::size_t newSize)
{
    ensureArray(*slot).getVector().resize(newSize);
}

Vector<std::string> JsonReflector::mapKeys() const
{
    auto keys = Vector<std::string> {};

    if (!slot->isObject())
        return keys;

    for (auto& [key, _]: slot->asObject())
        keys.add(key);

    return keys;
}

} // namespace Miro
