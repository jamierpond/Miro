#pragma once

#include "../JSON/Json.h"
#include "Reflector.h"

#include <string>

namespace Miro
{

// A JsonReflector represents a single JSON slot. The shape
// (object/array/map/primitive) is fixed at construction via Options
// and committed eagerly when saving — atKey/atIndex spawn children
// that commit their own shape the same way. The child is owned by
// the parent and lives until the next atKey/atIndex call (or until
// the parent is destroyed).
//
// The one exception is an omittable child (the slot of an Omittable<T>
// field): its shape is committed into the parent's `pendingSlot`
// scratch area instead, and the key is created only when the child
// calls markPresent() — at which point the staged content moves into
// the real key and the child retargets onto it. A child that never
// claims leaves no trace, which is exactly "the key is absent".
// Everything else keeps the eager contract.
class JsonReflector final : public Reflector
{
public:
    JsonReflector(JSON& slotToUse, Options optsToUse);
    ~JsonReflector() override;

    void visit(PrimitiveRef ref) override;
    void writeNull() override;
    void markPresent() override;
    ValueKind kind() const override;
    bool isIntegerNumber() const override;

    Reflector& atKey(std::string_view key, Options childOpts) override;
    Reflector& atIndex(std::size_t index, Options childOpts) override;

    std::size_t arraySize() const override;
    void resizeArray(std::size_t newSize) override;
    Vector<std::string> mapKeys() const override;

    void requirePolymorphicSupport(std::string_view) override {}

private:
    // A pointer rather than a reference because an omittable child
    // retargets from the parent's staging slot onto the real key when
    // it turns out to be present. Never null.
    JSON* slot = nullptr;
    bool absent = false;

    // Sentinel used as the target slot when loading and the requested
    // key/index isn't present. Operations on a child pointing at it
    // become no-ops, matching the prior "skip body" semantics.
    JSON missingSlot;

    // Staging for an omittable child: the scratch slot it writes into
    // before claiming, and the key it would claim. Reset on each new
    // omittable spawn; simply abandoned when the child never claims.
    JSON pendingSlot;
    std::string pendingKey;

    // Set on an omittable child, and cleared the moment it claims its
    // key. Null on every other reflector, which is what makes
    // markPresent() a no-op where absence can't be expressed (array
    // elements, the root).
    JsonReflector* owner = nullptr;

    OwningPointer<JsonReflector> currentChild;

    JsonReflector(JSON& slotToUse, Options optsToUse, bool absentToUse);

    void commitShape();
    JSON& claimPendingKey();

    void savePrimitive(PrimitiveRef ref);
    void loadPrimitive(PrimitiveRef ref);

    Reflector& atKeyForSave(std::string_view key, Options childOpts);
    Reflector& atKeyForLoad(std::string_view key, Options childOpts);

    Reflector& atIndexForSave(std::size_t index, Options childOpts);
    Reflector& atIndexForLoad(std::size_t index, Options childOpts);

    Reflector& spawnChild(JSON& targetSlot, Options childOpts, bool absentToUse);
    Reflector& spawnMissingChild(Options childOpts);
    Reflector& spawnOmittableChild(std::string_view key, Options childOpts);
};

} // namespace Miro
