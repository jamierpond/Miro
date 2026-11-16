// Reflector::reportRefusedValue: when Miro refuses a loaded value (a number
// that doesn't fit its field or an enum's underlying type, an enum name with
// no enumerator), the field keeps its prior value and the reflector is told.

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

using namespace nano;
using namespace Miro;

namespace
{

using StoredScalar = std::variant<std::int64_t, double, std::string>;

// A loading slot holding one stored scalar, counting the refusals it hears of.
class StoredScalarReflector final : public Reflector
{
public:
    explicit StoredScalarReflector(StoredScalar storedToUse)
        : Reflector({.mode = Mode::Load})
        , stored(std::move(storedToUse))
    {
    }

    void visit(PrimitiveRef ref) override
    {
        std::visit([this](auto* field) { copyStoredInto(*field); }, ref.data);
    }

    void writeNull() override {}

    ValueKind kind() const override
    {
        if (std::holds_alternative<std::string>(stored))
            return ValueKind::String;

        return ValueKind::Number;
    }

    Reflector& atKey(std::string_view, Options) override { return *this; }
    Reflector& atIndex(std::size_t, Options) override { return *this; }

    void reportRefusedValue() override { ++refusedValueCount; }

    int refusedValueCount = 0;

private:
    template <typename Field>
    void copyStoredInto(Field& field)
    {
        if (auto* integer = std::get_if<std::int64_t>(&stored))
        {
            if constexpr (std::is_arithmetic_v<Field>)
                field = static_cast<Field>(*integer);
        }
        else if (auto* number = std::get_if<double>(&stored))
        {
            if constexpr (std::is_arithmetic_v<Field>)
                field = static_cast<Field>(*number);
        }
        else if constexpr (std::same_as<Field, std::string>)
        {
            field = std::get<std::string>(stored);
        }
    }

    StoredScalar stored;
};

enum class Weapon : std::uint8_t
{
    sword = 1,
    bow = 2
};

template <typename T>
int loadAndCountRefusals(T& value, StoredScalar stored)
{
    auto reflector = StoredScalarReflector {std::move(stored)};
    Detail::reflectValue(reflector, value);
    return reflector.refusedValueCount;
}

} // namespace

auto narrowIntegerRefusalIsReported =
    test("reportRefusedValue: 1000 into an int8 field is reported") = []
{
    constexpr auto prior = std::int8_t {3};
    auto field = prior;

    check(loadAndCountRefusals(field, std::int64_t {1000}) == 1);
    check(field == prior);
};

auto fittingIntegerIsNotReported =
    test("reportRefusedValue: 100 into an int8 field loads unreported") = []
{
    auto field = std::int8_t {};

    check(loadAndCountRefusals(field, std::int64_t {100}) == 0);
    check(field == 100);
};

auto floatOverflowIsReported =
    test("reportRefusedValue: 1e300 into a float field is reported") = []
{
    constexpr auto prior = 2.5f;
    auto field = prior;

    check(loadAndCountRefusals(field, 1e300) == 1);
    check(field == prior);
};

auto enumNumberPastUnderlyingTypeIsRefused =
    test("reportRefusedValue: an enum number past its underlying type is "
         "refused") = []
{
    auto weapon = Weapon::bow;

    check(loadAndCountRefusals(weapon, std::int64_t {257}) == 1);
    check(weapon == Weapon::bow);
};

auto enumNumberOfEnumeratorLoads =
    test("reportRefusedValue: an enum number naming an enumerator loads") = []
{
    auto weapon = Weapon::bow;

    check(loadAndCountRefusals(weapon, std::int64_t {1}) == 0);
    check(weapon == Weapon::sword);
};

auto unknownEnumNameIsRefused =
    test("reportRefusedValue: an enum name with no enumerator is refused") = []
{
    auto weapon = Weapon::bow;

    check(loadAndCountRefusals(weapon, std::string {"axe"}) == 1);
    check(weapon == Weapon::bow);
};
