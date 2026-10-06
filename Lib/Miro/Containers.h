#pragma once

// Single header that pulls in the EA::data_structures types Miro depends
// on and surfaces the most-used ones inside the Miro namespace as
// Miro::Vector / Miro::Array / Miro::OwningPointer / Miro::OwnedVector.
//
// Everywhere in Lib/Miro that needs an EA container should include this
// header (directly or transitively) instead of the individual EA headers,
// so user TUs aren't forced to know the full ea_data_structures include
// layout.

#include <ea_data_structures/Pointers/OwningPointer.h>
#include <ea_data_structures/Structures/Array.h>
#include <ea_data_structures/Structures/MapVector.h>
#include <ea_data_structures/Structures/OwnedVector.h>
#include <ea_data_structures/Structures/Vector.h>
#include <ea_data_structures/ValueWrapper/Variant.h>

namespace Miro
{
using EA::Array;
using EA::OwnedVector;
using EA::OwningPointer;
using EA::Variant;
using EA::Vector;
} // namespace Miro
