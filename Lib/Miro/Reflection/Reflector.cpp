#include "Reflector.h"

#include <stdexcept>
#include <string>

namespace Miro
{

Property Reflector::operator[](std::string_view key)
{
    return {*this, key};
}

Element Reflector::operator[](std::size_t index)
{
    return {*this, index};
}

void Reflector::requirePolymorphicSupport(std::string_view context)
{
    auto message = std::string {"Reflector does not support polymorphic dispatch ("};
    message += context;
    message += ").";
    throw std::logic_error(message);
}

Reflector&
    Reflector::beginTaggedAlternative(std::string_view, const TagLiteral&, Options)
{
    throw std::logic_error(
        "Reflector does not support internally tagged unions (reflectTagged).");
}

void Reflector::markPresent() {}

void Reflector::visitIntegerEnum(TypeId, const Vector<EnumEntry>&)
{
    auto placeholder = std::int64_t {};
    visit(placeholder);
}

} // namespace Miro
