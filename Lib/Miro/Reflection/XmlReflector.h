#pragma once

#include "../XML/Xml.h"
#include "Reflector.h"

#include <string>
#include <variant>

namespace Miro
{

// An XmlReflector targets one of four "slot" kinds, picked at spawn
// time from the parent's atKey/atIndex call and the child's
// childOpts.shape:
//
//   * ElementSlot   — a real <element> node. Created by atKey when
//                     the child is Object/Map-shaped, by atIndex
//                     under an ArraySlot, or supplied at the root.
//                     visit() reads/writes the node's text content.
//   * AttributeSlot — an attribute on a parent node. Created by atKey
//                     when the child is Primitive-shaped. visit()
//                     reads/writes the parent's attributes[name].
//   * ArraySlot     — "repeated siblings named X under parent". Created
//                     by atKey when the child is Array-shaped (or by
//                     atIndex when the array element type is itself
//                     array-shaped, using "item" as the fallback name).
//                     atIndex(i) targets the i-th such sibling.
//   * MissingSlot   — load-time sentinel for an absent attribute /
//                     child / array sibling. All operations are no-ops.
//
// A Raw-shaped child — a Miro::JSON field, whose structure is known
// only at run time — saves as an ElementSlot and, on load, resolves to
// whichever real slot kind the document supports: an attribute if one
// exists under that name, repeated siblings if there is more than one,
// otherwise the single child element. XML records no types, so such a
// value comes back with every leaf as a string; see ReflectJson.h.
//
// Like JsonReflector, the parent owns its currently-active child via
// OwningPointer; the child is destroyed when the parent spawns a new
// one (or when the parent is destroyed).
//
// "Absent" (a disengaged Omittable<T> field) means no attribute and no
// element. Attributes and array siblings are already created lazily, so
// they get it for free; an omittable Object/Map child is staged in
// `pendingNode` and appended only when the child claims it through
// markPresent(). XML has no null, so writeNull() still can't be told
// apart from absent on a primitive — Omittable<std::optional<T>> is
// only fully three-state in JSON.
class XmlReflector final : public Reflector
{
public:
    XmlReflector(Xml::Node& rootToUse, Options optsToUse);
    ~XmlReflector() override;

    void visit(PrimitiveRef ref) override;
    void writeNull() override;
    void markPresent() override;
    ValueKind kind() const override;

    Reflector& atKey(std::string_view key, Options childOpts) override;
    Reflector& atIndex(std::size_t index, Options childOpts) override;

    std::size_t arraySize() const override;
    void resizeArray(std::size_t newSize) override;
    Vector<std::string> mapKeys() const override;

    void requirePolymorphicSupport(std::string_view) override {}

private:
    struct ElementSlot
    {
        Xml::Node* node;
    };

    struct AttributeSlot
    {
        Xml::Node* parent;
        std::string name;
    };

    struct ArraySlot
    {
        Xml::Node* parent;
        std::string elementName;
    };

    struct MissingSlot
    {
    };

    using Slot = std::variant<ElementSlot, AttributeSlot, ArraySlot, MissingSlot>;

    Slot slot;

    // Staging for an omittable Object/Map child — the only XML slot kind
    // that would otherwise create its node up front. The node is built
    // here and appended to this element's children only when the child
    // claims it via markPresent(). Attribute and array children need no
    // staging: they touch the parent solely on an actual write.
    Xml::Node pendingNode;

    // Set on an omittable child, cleared the moment it claims its node.
    // Null everywhere else, which makes markPresent() a no-op wherever
    // absence needs no help (attributes, array siblings, the root).
    XmlReflector* owner = nullptr;

    OwningPointer<XmlReflector> currentChild;

    XmlReflector(Slot slotToUse, Options optsToUse);

    Xml::Node& claimPendingElement();

    Reflector& spawnElement(Xml::Node& node, Options childOpts);
    Reflector&
        spawnAttribute(Xml::Node& parent, std::string name, Options childOpts);
    Reflector& spawnArray(Xml::Node& parent, std::string name, Options childOpts);
    Reflector& spawnMissing(Options childOpts);
    Reflector& spawnPendingElement(std::string name, Options childOpts);

    Reflector&
        atKeyOnElement(Xml::Node& parent, std::string_view key, Options childOpts);
    Reflector& atIndexOnArray(Xml::Node& parent,
                              const std::string& elementName,
                              std::size_t index,
                              Options childOpts);
};

} // namespace Miro
