#include <Miro/Json.h>
#include <NanoTest/NanoTest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace nano;
using namespace Miro;
using namespace Miro::Json;

namespace
{

template <typename ExceptionType, typename FunctionType>
std::string messageFrom(FunctionType&& functionToUse)
{
    try
    {
        functionToUse();
    }
    catch (const ExceptionType& error)
    {
        return error.what();
    }
    catch (...)
    {
        return "<threw some other exception type>";
    }

    return "<nothing was thrown>";
}

bool mentions(std::string_view messageToUse, std::string_view partToUse)
{
    return messageToUse.find(partToUse) != std::string_view::npos;
}

const Value& asConst(const Value& valueToUse)
{
    return valueToUse;
}

} // namespace

auto asStringOnANumberThrows = test("asString() on a number throws AccessError") = []
{
    auto value = parse("42");

    check(messageFrom<AccessError>([&] { (void) value.asString(); })
          == "expected string but value is number");
};

auto asNumberOnAStringThrows = test("asNumber() on a string throws AccessError") = []
{
    auto value = parse(R"("hello")");

    check(messageFrom<AccessError>([&] { (void) value.asNumber(); })
          == "expected number but value is string");
};

auto asBoolOnNullThrows = test("asBool() on null throws AccessError") = []
{
    auto value = parse("null");

    check(messageFrom<AccessError>([&] { (void) value.asBool(); })
          == "expected bool but value is null");
};

auto asArrayOnAnObjectThrows = test("asArray() on an object throws AccessError") = []
{
    auto value = parse(R"({"a":1})");

    check(messageFrom<AccessError>([&] { (void) value.asArray(); })
          == "expected array but value is object");
};

auto asObjectOnAnArrayThrows =
    test("asObject() on an array throws AccessError, const and non-const") = []
{
    auto value = parse("[1,2,3]");

    check(messageFrom<AccessError>([&] { (void) value.asObject(); })
          == "expected object but value is array");
    check(messageFrom<AccessError>([&] { (void) asConst(value).asObject(); })
          == "expected object but value is array");
};

auto implicitIntConversionThrows =
    test("Implicit int conversion from a string throws AccessError") = []
{
    auto value = parse(R"("hello")");

    check(messageFrom<AccessError>(
              [&]
              {
                  int converted = value;
                  (void) converted;
              })
          == "expected number but value is string");
};

auto implicitStringConversionThrows =
    test("Implicit std::string conversion from a number throws AccessError") = []
{
    auto value = parse("42");

    check(messageFrom<AccessError>(
              [&]
              {
                  std::string converted = value;
                  (void) converted;
              })
          == "expected string but value is number");
};

auto implicitBoolConversionThrows =
    test("Implicit bool conversion from a number throws AccessError") = []
{
    auto value = parse("42");

    check(messageFrom<AccessError>(
              [&]
              {
                  bool converted = value;
                  (void) converted;
              })
          == "expected bool but value is number");
};

auto parseFailureCaughtThroughSharedBase =
    test("A parse failure is caught as Miro::Json::Error") = []
{
    auto message = messageFrom<Error>([] { (void) parse("{oops"); });

    check(mentions(message, "parse error"));
};

auto accessFailureCaughtThroughSharedBase =
    test("An access failure is caught as Miro::Json::Error") = []
{
    auto value = parse(R"({"a":1})");

    check(mentions(messageFrom<Error>([&] { (void) value["a"].asString(); }),
                   "expected string but value is number"));
};

auto parseFailureStillCaughtAsParseError =
    test("A parse failure is still caught as ParseError") = []
{
    auto message = messageFrom<ParseError>([] { (void) parse("{oops"); });

    check(mentions(message, "parse error"));
};

auto anAccessFailureIsNotAParseError =
    test("An access failure is not a ParseError") = []
{
    auto value = parse(R"({"a":1})");

    check(messageFrom<ParseError>([&] { (void) value["a"].asString(); })
          == "<threw some other exception type>");
};

auto bothKindsCaughtAsRuntimeError =
    test("Both error kinds are caught as std::runtime_error") = []
{
    auto value = parse(R"({"a":1})");

    check(mentions(messageFrom<std::runtime_error>([] { (void) parse("{oops"); }),
                   "parse error"));
    check(mentions(
        messageFrom<std::runtime_error>([&] { (void) value["a"].asString(); }),
        "expected string"));
};

auto successfulAccessIsUnchanged = test("Successful access is unchanged") = []
{
    auto value = parse(R"({"name":"Miro","items":[10,20,30],"on":true})");

    check(value["name"].asString() == "Miro");
    check(asConst(value)["name"].asString() == "Miro");
    check(value["items"][0].asNumber() == 10.0);
    check(value["items"][2].asNumber() == 30.0);
    check(asConst(value)["items"][1].asNumber() == 20.0);
    check(value["on"].asBool());
    check(value["items"].asArray().size() == 3);
    check(value.asObject().size() == 3);

    int count = value["items"][1];
    std::string name = value["name"];

    check(count == 20);
    check(name == "Miro");
};

auto toObjectStillWrites = test("toObject() is still the way to write a key") = []
{
    auto value = Value {};

    value.toObject()["added"] = 1;

    check(value["added"].asNumber() == 1.0);
};
