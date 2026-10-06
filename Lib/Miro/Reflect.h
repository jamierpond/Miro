#pragma once

// Entry header: the reflection layer plus JSON serialization —
// MIRO_REFLECT, Reflector, toJSON / fromJSON / toJSONString and
// friends. XML serialization lives in <Miro/Xml.h>, the runtime
// command/event bridge in <Miro/Bridge.h>.

#include "Containers.h"
#include "IgnoreUnused.h"

#include "Reflection/Any.h"
#include "Reflection/JsonReflector.h"
#include "Reflection/Omittable.h"
#include "Reflection/ReflectContainers.h"
#include "Reflection/ReflectDispatch.h"
#include "Reflection/ReflectEnum.h"
#include "Reflection/ReflectJson.h"
#include "Reflection/ReflectMacro.h"
#include "Reflection/ReflectPolymorphic.h"
#include "Reflection/ReflectTagged.h"
#include "Reflection/Reflector.h"
#include "Reflection/Serialize.h"
#include "Reflection/TypeName.h"
