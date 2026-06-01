#pragma once

#include "../IgnoreUnused.h"
#include "Reflector.h"
#include "Serialize.h"

#include <concepts>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Miro
{
struct EmptyValue
{
    static void reflect(Reflector&) {}
};

class UnknownCommandError : public std::runtime_error
{
public:
    explicit UnknownCommandError(const std::string& commandToUse);
};

namespace Detail
{

// One-shot Info struct for cases where the caller already has Req and
// Res as type arguments (CommandTable::on for std::function or
// function-pointer handlers). Distinct from MethodInfo / FunctionInfo
// in Bridge/Callable.h, which derive the same fields from a pmf or
// fn-ptr type. All three satisfy CallableInfo below.
template <typename ReqT, typename ResT>
struct InfoFor
{
    using Req = ReqT;
    using Res = ResT;
    static constexpr bool hasReq = !std::is_void_v<ReqT>;
    static constexpr bool hasRes = !std::is_void_v<ResT>;
};

template <typename T>
concept CallableInfo = requires {
    typename T::Req;
    typename T::Res;
    { T::hasReq } -> std::convertible_to<bool>;
    { T::hasRes } -> std::convertible_to<bool>;
};

// ---------- Optional-async command results ----------
//
// A command handler may return a value directly (the common, synchronous
// case) OR an awaitable that settles later — e.g. eacp::Threads::Async<T>,
// produced by a coroutine that co_awaits I/O. Miro stays event-loop
// agnostic: it never names a concrete async type, it just recognises the
// shape structurally. Anything exposing a ValueType, an isReady() probe,
// and a then(onValue, onError) continuation hook is treated as async.
template <typename T>
concept Awaitable = requires(const T& t) {
    typename T::ValueType;
    { t.isReady() } -> std::convertible_to<bool>;
} && requires { &T::then; };

// Unwraps Awaitable<T> to its ValueType; leaves any other type alone.
// Used both by the runtime adapter (to serialise the settled value) and
// by ApiReflector (so codegen emits the unwrapped wire type, not the
// awaitable wrapper).
template <typename T>
struct AwaitResult
{
    using type = T;
};

template <Awaitable T>
struct AwaitResult<T>
{
    using type = typename T::ValueType;
};

template <typename T>
using AwaitResultT = typename AwaitResult<T>::type;

// The reply continuation a handler invokes once its result is known:
// once with a JSON result, or with a non-null error message. Sync
// handlers call it inline before returning; async handlers call it from
// their continuation, possibly on a later event-loop turn.
using Reply = std::function<void(const JSON& result, const std::string* error)>;

// JSON-in / Reply-out handler. Replaces the old JSON(const JSON&) shape
// so a single handler type covers both sync and async commands without
// the transport having to branch.
using RawHandlerFn = std::function<void(const JSON& payload, const Reply& reply)>;

// Invokes `produce` (which calls the underlying handler) and routes its
// result to `reply`. Three shapes, picked at compile time off the raw
// return type Raw:
//   - Awaitable<Raw>: attach a then() continuation; reply fires when it
//     settles (immediately if already resolved). Async<void> replies null.
//   - void: call, then reply null.
//   - value: serialise and reply.
// Synchronous handlers that throw propagate out of `produce` — the
// caller (CommandTable::dispatch / the transport) decides how to surface
// that. Asynchronous failures arrive through the error continuation.
//
// `keepAlive` owns the deserialised request (or is null for nullary
// commands). It matters for async handlers written as coroutines that
// take `const Req&`: such a coroutine stores a *reference* to the request
// in its frame, so the request must outlive every co_await — not just the
// initial call. Capturing keepAlive in the then() continuation pins it
// until the awaitable settles. For sync handlers it's simply dropped when
// this returns, exactly as before.
template <typename Raw, typename Produce, typename KeepAlive>
void settleReply(const Reply& reply, Produce&& produce, KeepAlive keepAlive)
{
    if constexpr (Awaitable<Raw>)
    {
        using V = typename Raw::ValueType;
        Raw pending = produce();

        if constexpr (std::is_void_v<V>)
        {
            pending.then([reply, keepAlive] { reply(JSON {}, nullptr); },
                         [reply, keepAlive](const std::string& e)
                         { reply(JSON {}, &e); });
        }
        else
        {
            pending.then([reply, keepAlive](const V& value)
                         { reply(toJSON(value), nullptr); },
                         [reply, keepAlive](const std::string& e)
                         { reply(JSON {}, &e); });
        }
    }
    else if constexpr (std::is_void_v<Raw>)
    {
        produce();
        reply(JSON {}, nullptr);
    }
    else
    {
        reply(toJSON(produce()), nullptr);
    }
}

// Wraps any callable shaped like the underlying handler (Res(Req),
// Res(), void(Req), void()) into a RawHandlerFn. Res may be a plain
// value, void, or an Awaitable — settleReply handles all three. Info
// supplies the req shape / type; callable can be a function pointer,
// std::function, lambda, or any other invocable matching the implied
// signature. Used by:
//   - CommandTable::on for std::function + free-fn-ptr handlers
//   - CommandExport::Detail::registerCommand for static-init handlers
//   - makePmfHandler in Bridge/Callable.h for pmf handlers
//
// The request is heap-owned (shared_ptr) so it can outlive an async
// handler's first co_await — see settleReply's keepAlive note.
template <CallableInfo Info, typename Callable>
RawHandlerFn makeJsonAdapter(Callable callable)
{
    return [callable = std::move(callable)](const JSON& payload, const Reply& reply)
    {
        ignoreUnused(payload);
        if constexpr (Info::hasReq)
        {
            auto req = std::make_shared<typename Info::Req>();
            fromJSON(*req, Json::payloadOrEmpty(payload));
            settleReply<typename Info::Res>(
                reply, [&] { return callable(*req); }, req);
        }
        else
        {
            settleReply<typename Info::Res>(reply, [&] { return callable(); }, nullptr);
        }
    };
}

} // namespace Detail

class CommandTable
{
public:
    // A handler settles its result through this continuation — see
    // Detail::Reply. Exposed so transports can supply their own.
    using Reply = Detail::Reply;
    using RawHandler = Detail::RawHandlerFn;

    template <typename Req, typename Res>
    using TypedHandler = const std::function<Res(const Req&)>;

    template <typename Req, typename Res>
    static RawHandler createRawHandler(const TypedHandler<Req, Res>& handler)
    {
        return Detail::makeJsonAdapter<Detail::InfoFor<Req, Res>>(handler);
    }

    template <typename Req, typename Res>
    void on(const std::string& command, const TypedHandler<Req, Res>& handler)
    {
        registerHandler(command, createRawHandler(handler));
    }

    template <typename Req, typename Res>
    void on(const std::string& command, Res (*handler)(const Req&))
    {
        registerHandler(command,
                        Detail::makeJsonAdapter<Detail::InfoFor<Req, Res>>(handler));
    }

    template <typename Res>
    void on(const std::string& command, Res (*handler)())
    {
        registerHandler(
            command, Detail::makeJsonAdapter<Detail::InfoFor<void, Res>>(handler));
    }

    template <typename Req>
    void on(const std::string& command, void (*handler)(const Req&))
    {
        registerHandler(
            command, Detail::makeJsonAdapter<Detail::InfoFor<Req, void>>(handler));
    }

    void on(const std::string& command, void (*handler)())
    {
        registerHandler(
            command, Detail::makeJsonAdapter<Detail::InfoFor<void, void>>(handler));
    }

    void on(const std::string& command, const RawHandler& handler)
    {
        registerHandler(command, handler);
    }

    bool has(std::string_view command) const;

    // Async-capable dispatch: looks up the command and hands it `reply`,
    // which the handler invokes when its result is known — inline for a
    // synchronous handler, later for an awaitable one. Throws
    // UnknownCommandError if the command is not registered; a synchronous
    // handler that throws propagates out of this call. Transports that
    // can defer their wire reply (e.g. the WebView bridge) use this form.
    void dispatch(std::string_view command,
                  const JSON& payload,
                  const Reply& reply) const;

    // Synchronous convenience over the reply form, for transports/tests
    // that need the result as a return value. Works for handlers that
    // reply inline (every synchronous command); throws if the command
    // only settles asynchronously, since there is no value to return yet.
    JSON dispatch(std::string_view command, const JSON& payload) const;

private:
    void registerHandler(const std::string& command, const RawHandler& handler);

    std::unordered_map<std::string, RawHandler> handlers;
};
} // namespace Miro
