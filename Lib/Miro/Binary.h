#pragma once

// Entry header: the binary serialization layer — the compact "MIB1"
// wire format (Miro::Binary::Writer / Document / View) plus the
// reflection serializers toBinary / fromBinary / createFromBinary.

#include "Binary/Binary.h"
#include "Reflection/ReflectEnum.h"
#include "Reflection/ReflectMacro.h"
#include "Reflection/ReflectPolymorphic.h"
#include "Reflection/SerializeBinary.h"
