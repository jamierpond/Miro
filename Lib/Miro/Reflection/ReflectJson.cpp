#include "ReflectJson.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace Miro::Detail
{

namespace
{
// The shape a child slot has to be spawned with to carry `value`. Only
// the save side can answer this, and it matters beyond JSON: XmlReflector
// picks its slot kind (attribute / element / repeated siblings) from the
// child's shape, so a number inside a raw value lands in an attribute
// exactly as a typed int field would.
Shape rawJsonShapeOf(const Json::Value& value)
{
    if (value.isObject())
        return Shape::Object;

    if (value.isArray())
        return Shape::Array;

    return Shape::Primitive;
}

// The slot's own nullable and omittable flags describe the raw field, not
// what it contains: a child of a raw value is always present in it, so it
// is spawned as a plain slot. Left on, `omittable` would stage every
// object child as a key that nothing ever claims, and it would vanish.
Options rawJsonChildOptions(const Reflector& ref, Shape shapeToUse)
{
    auto opts = ref.options();
    opts.shape = shapeToUse;
    opts.nullable = false;
    opts.omittable = false;
    return opts;
}

void saveRawJson(Reflector& ref, Json::Value& value)
{
    if (value.isBool())
    {
        auto data = value.asBool();
        ref.visit(data);
    }
    else if (value.isInteger())
    {
        auto data = value.asInteger();
        ref.visit(data);
    }
    else if (value.isNumber())
    {
        auto data = value.asNumber();
        ref.visit(data);
    }
    else if (value.isString())
    {
        auto data = value.asString();
        ref.visit(data);
    }
    else if (value.isArray())
    {
        auto& items = std::get<Json::Array>(value.data).getVector();

        // Also what keeps an empty array an array: the slot was spawned
        // as Shape::Raw, which commits to an object, and this is the
        // only call that turns it back into an array.
        ref.resizeArray(items.size());

        for (auto i = std::size_t {0}; i < items.size(); ++i)
        {
            auto opts = rawJsonChildOptions(ref, rawJsonShapeOf(items[i]));
            saveRawJson(ref.atIndex(i, opts), items[i]);
        }
    }
    else if (value.isObject())
    {
        // Nothing to do for an empty object — the slot was already
        // committed as one when it was spawned.
        for (auto& [key, element]: value.asObject())
        {
            auto opts = rawJsonChildOptions(ref, rawJsonShapeOf(element));
            saveRawJson(ref.atKey(key, opts), element);
        }
    }
    else
    {
        ref.writeNull();
    }
}

void loadRawJson(Reflector& ref, Json::Value& value)
{
    // Children are spawned as Shape::Raw in turn: the document, not the
    // C++ type, says what each one is.
    auto childOpts = rawJsonChildOptions(ref, Shape::Raw);

    switch (ref.kind())
    {
        case ValueKind::Absent:
            // An absent key leaves the existing value alone, as with
            // every other type.
            return;

        case ValueKind::Null:
            value = Json::Value {};
            return;

        case ValueKind::Bool:
        {
            auto data = false;
            ref.visit(data);
            value = data;
            return;
        }

        case ValueKind::Number:
        {
            // The slot decides which of the two numeric kinds comes
            // back, exactly as it decides object against array above.
            if (ref.isIntegerNumber())
            {
                auto integer = std::int64_t {0};
                ref.visit(integer);
                value = integer;
                return;
            }

            auto data = 0.0;
            ref.visit(data);
            value = data;
            return;
        }

        case ValueKind::String:
        {
            auto data = std::string {};
            ref.visit(data);
            value = std::move(data);
            return;
        }

        case ValueKind::Array:
        {
            auto array = Json::Array {};
            auto& items = array.getVector();
            items.resize(ref.arraySize());

            for (auto i = std::size_t {0}; i < items.size(); ++i)
                loadRawJson(ref.atIndex(i, childOpts), items[i]);

            value = std::move(array);
            return;
        }

        case ValueKind::Object:
        {
            auto object = Json::Object {};

            for (auto& key: ref.mapKeys())
                loadRawJson(ref.atKey(key, childOpts), object[key]);

            value = std::move(object);
            return;
        }
    }
}
} // namespace

void reflectValue(Reflector& ref, Json::Value& value)
{
    // Schema mode describes the slot as "anything" — the walker already
    // recorded that from Shape::Raw. Walking the value here would
    // describe whatever the default-constructed instance happens to
    // hold (null), which says nothing about what the field accepts.
    if (ref.isSchema())
        return;

    if (ref.isSaving())
        saveRawJson(ref, value);
    else
        loadRawJson(ref, value);
}

} // namespace Miro::Detail
