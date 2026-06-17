#include "Generic.h"
#include "Json.h"

// The parser lives in Generic.h, templated on the document model so
// the compile-time backend shares it. This TU instantiates it for the
// runtime Json::Value.

namespace Miro::Json
{

Value parse(std::string_view inputToUse)
{
    return Generic::parse<Value>(inputToUse);
}

} // namespace Miro::Json
