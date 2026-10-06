#pragma once

#include "../JSON/Json.h"
#include "ReflectDispatch.h"
#include "Reflector.h"

#include <type_traits>

// Built-in reflection for a raw JSON value. A field of type Miro::JSON
// (Miro::Json::Value) — or Miro::Json::Any, which derives from it — is
// written out and read back verbatim, whatever it happens to hold.
//
// The motivating case is an envelope whose payload type is only known
// after another field has been read. Discord's gateway frame is
// {"op": 10, "d": {...}, "s": null, "t": null}, where the shape of "d"
// depends on "op" and "t": a client reflects "d" into a Miro::JSON,
// reads the discriminator, and then decodes the payload a second time
// with Miro::createFromJSON<T>(frame.d).
//
// Because the structure lives in the value and not in the C++ type, the
// slot is Shape::Raw and the walk in ReflectJson.cpp drives the reflector
// from the value's runtime kind. It only uses the abstract Reflector interface,
// so it works through any reflector — see XmlReflector for what a
// format that can't represent JSON's kinds makes of it.

namespace Miro::Detail
{

template <typename T>
struct IsRawJson<T, std::enable_if_t<std::is_base_of_v<Json::Value, T>>>
    : std::true_type
{
};

// Json::Any binds to this overload through its Value base.
void reflectValue(Reflector& ref, Json::Value& value);

} // namespace Miro::Detail
