#pragma once

// Umbrella header — pulls in every public Miro entry point. Kept for
// backwards compatibility; new code can include just the layers it
// needs:
//
//   <Miro/Json.h>    — raw JSON value type, parse / print
//   <Miro/Reflect.h> — reflection + JSON serialization (toJSON / fromJSON)
//   <Miro/Binary.h>  — compact binary serialization (toBinary / fromBinary)
//   <Miro/Xml.h>     — XML value type + XML serialization
//   <Miro/Bridge.h>  — runtime command/event bridge
//   <Miro/Codegen.h> — type-export / codegen toolchain

#include "Binary.h"
#include "Bridge.h"
#include "Codegen.h"
#include "Json.h"
#include "Reflect.h"
#include "Xml.h"
