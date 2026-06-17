#include "Generic.h"
#include "Yaml.h"

// The parser lives in Generic.h, templated on the document model so
// the compile-time backend shares it. This TU instantiates it for the
// runtime value type (Json::Value).

namespace Miro::Yaml
{

Value resolvePlainScalar(std::string_view tokenToUse)
{
    return Generic::resolvePlainScalar<Value>(tokenToUse);
}

Value parse(std::string_view inputToUse)
{
    return Generic::parse<Value>(inputToUse);
}

} // namespace Miro::Yaml
