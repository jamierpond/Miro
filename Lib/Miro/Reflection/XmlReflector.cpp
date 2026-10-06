#include "XmlReflector.h"

#include <cmath>
#include <concepts>
#include <cstdlib>
#include <sstream>
#include <type_traits>

namespace Miro
{
namespace
{

template <typename T>
std::string primitiveToString(T* ptr)
{
    if constexpr (std::same_as<T, bool>)
    {
        return *ptr ? "true" : "false";
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        return *ptr;
    }
    else if constexpr (std::same_as<T, double>)
    {
        // Match JSON printer behavior — emit integers without a decimal
        // point so round-trips through int-backed reflect targets don't
        // pick up "5.0" strings.
        if (std::isfinite(*ptr) && *ptr == std::floor(*ptr) && std::abs(*ptr) < 1e15)
        {
            auto stream = std::ostringstream {};
            stream << static_cast<long long>(*ptr);
            return stream.str();
        }

        auto stream = std::ostringstream {};
        stream << *ptr;
        return stream.str();
    }
    else
    {
        return std::to_string(*ptr);
    }
}

template <typename T>
void primitiveFromString(const std::string& text, T* ptr)
{
    if constexpr (std::same_as<T, bool>)
    {
        *ptr = (text == "true" || text == "1");
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        *ptr = text;
    }
    else if constexpr (std::same_as<T, double>)
    {
        *ptr = std::strtod(text.c_str(), nullptr);
    }
    else
    {
        *ptr = static_cast<T>(std::strtoll(text.c_str(), nullptr, 10));
    }
}

std::size_t countSiblings(const Xml::Node& parent, std::string_view name)
{
    auto count = std::size_t {0};

    for (const auto& child: parent.children)
        if (child.name == name)
            ++count;

    return count;
}

Xml::Node* findNthSibling(Xml::Node& parent, std::string_view name, std::size_t i)
{
    auto count = std::size_t {0};

    for (auto& child: parent.children)
    {
        if (child.name != name)
            continue;

        if (count == i)
            return &child;

        ++count;
    }

    return nullptr;
}

} // namespace

XmlReflector::XmlReflector(Xml::Node& rootToUse, Options optsToUse)
    : XmlReflector(ElementSlot {&rootToUse}, optsToUse)
{
}

XmlReflector::XmlReflector(Slot slotToUse, Options optsToUse)
    : Reflector(optsToUse)
    , slot(std::move(slotToUse))
{
}

XmlReflector::~XmlReflector() = default;

void XmlReflector::markPresent()
{
    if (owner == nullptr)
        return;

    slot = ElementSlot {&owner->claimPendingElement()};
    owner = nullptr;
}

// Appends the node this child was staged for, in document order — the
// staged element goes in exactly where the field was reflected — and
// hands back the real node for the child to retarget onto.
Xml::Node& XmlReflector::claimPendingElement()
{
    auto* node = std::get<ElementSlot>(slot).node;
    node->children.add(std::move(pendingNode));
    pendingNode = Xml::Node {};
    return node->children.back();
}

ValueKind XmlReflector::kind() const
{
    // We never report Null — XML has no native null representation.
    // Optional dispatch sees Absent (slot missing) vs. any other kind
    // (present, load the value).
    if (std::holds_alternative<MissingSlot>(slot))
        return ValueKind::Absent;

    if (std::holds_alternative<AttributeSlot>(slot))
        return ValueKind::String;

    if (std::holds_alternative<ArraySlot>(slot))
        return ValueKind::Array;

    // An element with no attributes and no children carries nothing but
    // its text, so it reports as a string exactly like an attribute
    // does. That's what lets a value-shaped element — an array item
    // holding an enum, say — be read back from its text rather than
    // mistaken for an object with no readable content. A raw-JSON slot
    // has to recover the value's kind from the document alone, and an
    // element with no text either is the most XML can say about an
    // empty object, so only a leaf that actually has text is a string
    // there.
    const auto& element = *std::get<ElementSlot>(slot).node;

    if (element.attributes.empty() && element.children.empty())
    {
        if (shape() != Shape::Raw || !element.text.empty())
            return ValueKind::String;
    }

    return ValueKind::Object;
}

void XmlReflector::writeNull()
{
    // Lazy by design: AttributeSlot only writes on visit, so a writeNull
    // means "skip the attribute". ElementSlot's node already exists (was
    // pushed by atKey) — leaving it empty is the closest XML analogue of
    // null. ArraySlot creates siblings only on atIndex, so writeNull
    // leaves the parent untouched.
}

void XmlReflector::visit(PrimitiveRef ref)
{
    std::visit(
        [this, ref](auto& s)
        {
            using S = std::decay_t<decltype(s)>;

            if constexpr (std::same_as<S, ElementSlot>)
            {
                std::visit(
                    [this, &s](auto* ptr)
                    {
                        if (isSaving())
                            s.node->text = primitiveToString(ptr);
                        else
                            primitiveFromString(s.node->text, ptr);
                    },
                    ref.data);
            }
            else if constexpr (std::same_as<S, AttributeSlot>)
            {
                std::visit(
                    [this, &s](auto* ptr)
                    {
                        if (isSaving())
                            s.parent->attributes[s.name] = primitiveToString(ptr);
                        else if (auto it = s.parent->attributes.find(s.name);
                                 it != s.parent->attributes.end())
                            primitiveFromString(it->second, ptr);
                    },
                    ref.data);
            }
            // ArraySlot / MissingSlot: visit is a no-op.
        },
        slot);
}

Reflector& XmlReflector::atKey(std::string_view key, Options childOpts)
{
    return std::visit(
        [&](auto& s) -> Reflector&
        {
            using S = std::decay_t<decltype(s)>;

            if constexpr (std::same_as<S, ElementSlot>)
                return atKeyOnElement(*s.node, key, childOpts);
            else
                return spawnMissing(childOpts);
        },
        slot);
}

Reflector& XmlReflector::atKeyOnElement(Xml::Node& parent,
                                        std::string_view key,
                                        Options childOpts)
{
    auto keyStr = std::string(key);

    switch (childOpts.shape)
    {
        case Shape::Primitive:
            if (isLoading() && !parent.attributes.contains(keyStr))
                return spawnMissing(childOpts);

            return spawnAttribute(parent, std::move(keyStr), childOpts);

        case Shape::Array:
            // An omitted array key left no siblings behind. XML can't
            // tell that from an engaged-but-empty vector, and "absent"
            // is the reading that round-trips, so prefer it.
            if (isLoading() && childOpts.omittable
                && countSiblings(parent, keyStr) == 0)
                return spawnMissing(childOpts);

            return spawnArray(parent, std::move(keyStr), childOpts);

        case Shape::Raw:
            // A raw JSON value has no static shape, so saving always
            // makes an element and lets the value's own kind decide
            // what goes inside it (its children are spawned with the
            // shape read off the value, so primitives still become
            // attributes). Loading has to guess from the document: an
            // attribute is a primitive the save side wrote, repeated
            // siblings are an array, a single child is a nested value.
            if (isLoading())
            {
                if (parent.attributes.contains(keyStr))
                    return spawnAttribute(parent, std::move(keyStr), childOpts);

                if (countSiblings(parent, keyStr) > 1)
                    return spawnArray(parent, std::move(keyStr), childOpts);
            }

            [[fallthrough]];

        case Shape::Object:
        case Shape::Map:
            if (isSaving())
            {
                if (childOpts.omittable)
                    return spawnPendingElement(std::move(keyStr), childOpts);

                parent.children.add(Xml::Node {.name = keyStr});
                return spawnElement(parent.children.back(), childOpts);
            }

            if (auto* child = Xml::findChild(parent, keyStr))
                return spawnElement(*child, childOpts);

            return spawnMissing(childOpts);
    }

    return spawnMissing(childOpts);
}

Reflector& XmlReflector::atIndex(std::size_t index, Options childOpts)
{
    // No staging here: an omittable array element has no absent form —
    // repeated siblings can't have a hole in them.
    return std::visit(
        [&](auto& s) -> Reflector&
        {
            using S = std::decay_t<decltype(s)>;

            if constexpr (std::same_as<S, ArraySlot>)
                return atIndexOnArray(*s.parent, s.elementName, index, childOpts);
            else
                return spawnMissing(childOpts);
        },
        slot);
}

Reflector& XmlReflector::atIndexOnArray(Xml::Node& parent,
                                        const std::string& elementName,
                                        std::size_t index,
                                        Options childOpts)
{
    if (isSaving())
    {
        auto count = countSiblings(parent, elementName);

        while (count <= index)
        {
            parent.children.add(Xml::Node {.name = elementName});
            ++count;
        }
    }

    auto* node = findNthSibling(parent, elementName, index);

    if (node == nullptr)
        return spawnMissing(childOpts);

    // Nested arrays (vector-of-vector) have no field name for the inner
    // axis — fall back to "item" so we can still emit/read repeated
    // children inside the per-outer-element wrapper.
    if (childOpts.shape == Shape::Array)
        return spawnArray(*node, "item", childOpts);

    return spawnElement(*node, childOpts);
}

std::size_t XmlReflector::arraySize() const
{
    if (const auto* s = std::get_if<ArraySlot>(&slot))
        return countSiblings(*s->parent, s->elementName);

    return 0;
}

void XmlReflector::resizeArray(std::size_t newSize)
{
    auto* s = std::get_if<ArraySlot>(&slot);

    if (s == nullptr)
        return;

    auto& children = s->parent->children;
    auto count = countSiblings(*s->parent, s->elementName);

    while (count > newSize)
    {
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            if (it->name == s->elementName)
            {
                children.erase(it);
                --count;
                break;
            }
        }
    }

    while (count < newSize)
    {
        children.add(Xml::Node {.name = s->elementName});
        ++count;
    }
}

Vector<std::string> XmlReflector::mapKeys() const
{
    auto keys = Vector<std::string> {};

    const auto* s = std::get_if<ElementSlot>(&slot);

    if (s == nullptr)
        return keys;

    for (const auto& [key, _]: s->node->attributes)
        keys.add(key);

    for (const auto& child: s->node->children)
    {
        // Dedup against attribute keys — a Map<string, V> in XML mode
        // could in principle have both an attribute and a child named
        // the same; we don't want to walk the key twice.
        auto seen = false;

        for (const auto& existing: keys)
        {
            if (existing == child.name)
            {
                seen = true;
                break;
            }
        }

        if (!seen)
            keys.add(child.name);
    }

    return keys;
}

Reflector& XmlReflector::spawnElement(Xml::Node& node, Options childOpts)
{
    currentChild.reset();
    currentChild = new XmlReflector(ElementSlot {&node}, childOpts);
    return *currentChild;
}

Reflector& XmlReflector::spawnAttribute(Xml::Node& parent,
                                        std::string name,
                                        Options childOpts)
{
    currentChild.reset();
    currentChild =
        new XmlReflector(AttributeSlot {&parent, std::move(name)}, childOpts);
    return *currentChild;
}

Reflector&
    XmlReflector::spawnArray(Xml::Node& parent, std::string name, Options childOpts)
{
    currentChild.reset();
    currentChild = new XmlReflector(ArraySlot {&parent, std::move(name)}, childOpts);
    return *currentChild;
}

Reflector& XmlReflector::spawnMissing(Options childOpts)
{
    currentChild.reset();
    currentChild = new XmlReflector(MissingSlot {}, childOpts);
    return *currentChild;
}

// Points the child at the staged node instead of a real child element.
// Nothing is appended unless the child claims it back via markPresent();
// an abandoned staged node is simply overwritten by the next omittable
// sibling.
Reflector& XmlReflector::spawnPendingElement(std::string name, Options childOpts)
{
    pendingNode = Xml::Node {.name = std::move(name)};

    spawnElement(pendingNode, childOpts);
    currentChild->owner = this;
    return *currentChild;
}

} // namespace Miro
