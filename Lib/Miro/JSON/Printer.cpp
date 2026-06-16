#include "Generic.h"
#include "Json.h"

#include <iostream>

// The printer lives in Generic.h, templated on the document model so
// the compile-time backend shares it. This TU instantiates it for the
// runtime Json::Value.

namespace Miro::Json
{

std::string print(const Value& valueToUse, int indentToUse)
{
    return Generic::print(valueToUse, indentToUse);
}

void log(const Value& valueToUse, int indentToUse)
{
    std::cout << print(valueToUse, indentToUse) << std::endl;
}

} // namespace Miro::Json
