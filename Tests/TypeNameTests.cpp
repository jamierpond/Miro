#include <Miro/Reflect.h>

#include <NanoTest/NanoTest.h>

#include <string_view>

using namespace nano;
using namespace Miro;

namespace TypeNameTypes
{

struct Point
{
    int x = 0;
};

template <typename T>
struct Box
{
    T value {};
};

enum class Colour
{
    red,
    green
};

} // namespace TypeNameTypes

static_assert(Detail::typeNameOf<TypeNameTypes::Point>() == "Point");
static_assert(Detail::qualifiedNameOf<TypeNameTypes::Point>()
              == "TypeNameTypes::Point");

auto typeNamesMatchTheCompilerSpelling =
    test("TypeName: short and qualified names of a struct, template and enum") = []
{
    check(Detail::typeNameOf<TypeNameTypes::Point>() == "Point");
    check(Detail::qualifiedNameOf<TypeNameTypes::Point>() == "TypeNameTypes::Point");
    check(Detail::typeNameOf<TypeNameTypes::Box<int>>() == "Box<int>");
    check(Detail::qualifiedNameOf<TypeNameTypes::Box<int>>()
          == "TypeNameTypes::Box<int>");
    check(Detail::typeNameOf<TypeNameTypes::Colour>() == "Colour");
    check(Detail::qualifiedNameOf<TypeNameTypes::Colour>()
          == "TypeNameTypes::Colour");
};
