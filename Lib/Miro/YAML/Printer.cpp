#include "Generic.h"
#include "Yaml.h"

#include <iostream>

// The printer lives in Generic.h, templated on the document model so
// the compile-time backend shares it. This TU instantiates it for the
// runtime value type (Json::Value).

namespace Miro::Yaml
{

std::string print(const Value& valueToUse, int indentToUse)
{
    return Generic::print(valueToUse, indentToUse);
}

void log(const Value& valueToUse, int indentToUse)
{
    // Qualified: Value is Json::Value, so an unqualified call would
    // also find Json::print through ADL and be ambiguous.
    std::cout << Miro::Yaml::print(valueToUse, indentToUse) << std::endl;
}

} // namespace Miro::Yaml
