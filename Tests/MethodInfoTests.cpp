// Tests for MethodInfo deduction + makePmfHandler thunk behaviour
// against all four pmf shapes (Res(Req), Res(), void(Req), void()),
// including const variants. Validates that pmf-based handlers carry
// the same semantics as the existing free-function-based ones, so the
// upcoming ApiReflector::command<&Class::method>(name) entry point
// can be built on top.

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <string>
#include <type_traits>

using namespace nano;
using namespace Miro;

namespace
{

struct MIReq
{
    std::string text;

    MIRO_REFLECT(text)
};

struct MIRes
{
    std::string echoed;

    MIRO_REFLECT(echoed)
};

class TestApi
{
public:
    MIRes echo(const MIReq& req)
    {
        calls++;
        return {req.text + "!"};
    }

    MIRes status() const
    {
        return {lastLogged.empty() ? "idle" : "busy"};
    }

    void log(const MIReq& req) { lastLogged = req.text; }
    void tick() { ticks++; }

    std::string lastLogged;
    int ticks = 0;
    int calls = 0;
};

// makePmfHandler now yields a reply-based RawHandler (JSON in, result
// delivered through a continuation) so it can carry async commands too.
// These thunks all settle inline, so this helper drives one synchronously
// and returns its JSON — keeping the round-trip assertions below intact.
inline JSON callThunk(const CommandTable::RawHandler& handler, const JSON& payload)
{
    auto out = JSON {};
    handler(payload,
            [&out](const JSON& result, const std::string* error)
            {
                if (error != nullptr)
                    throw std::runtime_error(*error);
                out = result;
            });
    return out;
}

} // namespace

// ---------- Compile-time deductions ----------

static_assert(
    std::is_same_v<MethodInfo<decltype(&TestApi::echo)>::Class, TestApi>);
static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::echo)>::Req, MIReq>);
static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::echo)>::Res, MIRes>);
static_assert(MethodInfo<decltype(&TestApi::echo)>::hasReq);
static_assert(MethodInfo<decltype(&TestApi::echo)>::hasRes);
static_assert(!MethodInfo<decltype(&TestApi::echo)>::isConst);

static_assert(
    std::is_same_v<MethodInfo<decltype(&TestApi::status)>::Class, TestApi>);
static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::status)>::Req, void>);
static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::status)>::Res, MIRes>);
static_assert(!MethodInfo<decltype(&TestApi::status)>::hasReq);
static_assert(MethodInfo<decltype(&TestApi::status)>::hasRes);
static_assert(MethodInfo<decltype(&TestApi::status)>::isConst);

static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::log)>::Req, MIReq>);
static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::log)>::Res, void>);
static_assert(MethodInfo<decltype(&TestApi::log)>::hasReq);
static_assert(!MethodInfo<decltype(&TestApi::log)>::hasRes);

static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::tick)>::Req, void>);
static_assert(std::is_same_v<MethodInfo<decltype(&TestApi::tick)>::Res, void>);
static_assert(!MethodInfo<decltype(&TestApi::tick)>::hasReq);
static_assert(!MethodInfo<decltype(&TestApi::tick)>::hasRes);

// ---------- Thunk shape adaptation ----------

auto miThunkEcho = test("MethodInfo: Res(Req) thunk round-trips JSON") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler<&TestApi::echo>(api);

    auto payload = Json::parse(R"({"text":"hi"})");
    auto result = callThunk(handler, payload);

    check(result.isObject());
    check(result["echoed"].asString() == "hi!");
};

auto miThunkStatus =
    test("MethodInfo: Res() const thunk ignores payload, reads instance state") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler<&TestApi::status>(api);

    check(callThunk(handler, JSON {})["echoed"].asString() == "idle");

    api.lastLogged = "x";
    check(callThunk(handler, JSON {})["echoed"].asString() == "busy");
};

auto miThunkLog =
    test("MethodInfo: void(Req) thunk returns null, mutates instance") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler<&TestApi::log>(api);

    auto payload = Json::parse(R"({"text":"trace"})");
    auto result = callThunk(handler, payload);

    check(result.isNull());
    check(api.lastLogged == "trace");
};

auto miThunkTick =
    test("MethodInfo: void() thunk returns null, mutates instance") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler<&TestApi::tick>(api);

    auto result = callThunk(handler, JSON {});

    check(result.isNull());
    check(api.ticks == 1);
};

// ---------- Instance identity across calls ----------

auto miInstancePersists =
    test("MethodInfo: handler captures instance by address across calls") = []
{
    auto api = TestApi {};
    auto tick = makePmfHandler<&TestApi::tick>(api);

    callThunk(tick, JSON {});
    callThunk(tick, JSON {});
    callThunk(tick, JSON {});

    check(api.ticks == 3);
};

// ---------- Wire-up into CommandTable ----------
//
// Confirms makePmfHandler's return type matches CommandTable::on's
// RawHandler overload — i.e. the same table that today carries
// free-function-handler thunks can carry pmf-bound ones without any
// further adaptation.

auto miThroughCommandTable =
    test("MethodInfo: pmf handler installs into CommandTable") = []
{
    auto api = TestApi {};
    auto table = CommandTable {};
    table.on("echo", makePmfHandler<&TestApi::echo>(api));

    auto payload = Json::parse(R"({"text":"yo"})");
    auto result = table.dispatch("echo", payload);

    check(result["echoed"].asString() == "yo!");
};

// ---------- Runtime-pmf overload ----------
//
// Same semantics as the template-arg form, but the pmf is taken as a
// regular value argument. This is the entry point ApiReflector::command
// (pmf, name) will use — verifying that all four shapes produce
// identical results without `<>` at the call site.

auto miRuntimeEcho =
    test("MethodInfo (runtime pmf): Res(Req) thunk round-trips JSON") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler(&TestApi::echo, api);

    auto result = callThunk(handler, Json::parse(R"({"text":"hi"})"));

    check(result["echoed"].asString() == "hi!");
};

auto miRuntimeStatus =
    test("MethodInfo (runtime pmf): Res() const thunk reads instance state") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler(&TestApi::status, api);

    check(callThunk(handler, JSON {})["echoed"].asString() == "idle");

    api.lastLogged = "x";
    check(callThunk(handler, JSON {})["echoed"].asString() == "busy");
};

auto miRuntimeLog =
    test("MethodInfo (runtime pmf): void(Req) mutates instance, returns null") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler(&TestApi::log, api);

    auto result = callThunk(handler, Json::parse(R"({"text":"trace"})"));

    check(result.isNull());
    check(api.lastLogged == "trace");
};

auto miRuntimeTick =
    test("MethodInfo (runtime pmf): void() mutates instance, returns null") = []
{
    auto api = TestApi {};
    auto handler = makePmfHandler(&TestApi::tick, api);

    callThunk(handler, JSON {});
    callThunk(handler, JSON {});

    check(api.ticks == 2);
};

auto miRuntimeAgreesWithTemplate =
    test("MethodInfo (runtime pmf): result matches template-arg form") = []
{
    auto api = TestApi {};

    auto runtimeHandler = makePmfHandler(&TestApi::echo, api);
    auto templateHandler = makePmfHandler<&TestApi::echo>(api);

    auto payload = Json::parse(R"({"text":"same"})");

    auto fromRuntime = callThunk(runtimeHandler, payload);
    auto fromTemplate = callThunk(templateHandler, payload);

    check(fromRuntime["echoed"].asString() == fromTemplate["echoed"].asString());
    check(api.calls == 2);
};
