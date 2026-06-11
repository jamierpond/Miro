#include "Reflector.h"

#include <stdexcept>
#include <string>

namespace Miro
{

void Reflector::requirePolymorphicSupport(std::string_view context)
{
    auto message = std::string {"Reflector does not support polymorphic dispatch ("};
    message += context;
    message += ").";
    throw std::logic_error(message);
}

} // namespace Miro
