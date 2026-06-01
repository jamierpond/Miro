#pragma once

#include "../Reflection/CommandTable.h"
#include "../Reflection/Serialize.h"
#include "BindReflector.h"

#include <ea_data_structures/Pointers/Broadcaster.h>
#include <ea_data_structures/Pointers/OwningPointer.h>
#include <ea_data_structures/Structures/Vector.h>

#include <functional>
#include <string>
#include <string_view>

namespace Miro
{

// Bridge is the runtime primitive transports plug into. It owns a
// CommandTable for incoming requests and an EA::Broadcaster (onEmit)
// for outgoing events; transports (eacp WebView, HTTP RPC, WebSocket,
// ...) are thin adapters that route their wire format through
// dispatch() and an EA::Listener attached to onEmit.
//
// One Bridge can serve multiple transports simultaneously — a typical
// app declares the Bridge once, binds its APIs via use(api), and hands
// a reference to each transport's constructor.
class Bridge
{
public:
    Bridge() = default;
    ~Bridge() = default;

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    Bridge(Bridge&&) = delete;
    Bridge& operator=(Bridge&&) = delete;

    template <typename Req, typename Res>
    void on(const std::string& command,
            const std::function<Res(const Req&)>& handler)
    {
        commands.on<Req, Res>(command, handler);
    }

    template <typename Req, typename Res>
    void on(const std::string& command, Res (*handler)(const Req&))
    {
        commands.on<Req, Res>(command, handler);
    }

    template <typename Res>
    void on(const std::string& command, Res (*handler)())
    {
        commands.on(command, handler);
    }

    template <typename Req>
    void on(const std::string& command, void (*handler)(const Req&))
    {
        commands.on(command, handler);
    }

    void on(const std::string& command, void (*handler)())
    {
        commands.on(command, handler);
    }

    template <typename T>
    void emit(const std::string& eventToUse, const T& payloadToUse)
    {
        emitJson(eventToUse, toJSON(payloadToUse));
    }

    void emit(const std::string& eventToUse) { emitJson(eventToUse, JSON {}); }

    // Async-capable dispatch — forwards to CommandTable::dispatch(reply).
    // The reply continuation fires inline for synchronous commands and on
    // a later turn for awaitable ones. Transports that can defer their
    // wire reply (the WebView bridge) use this form.
    void dispatch(std::string_view command,
                  const JSON& payloadToUse,
                  const CommandTable::Reply& reply) const;

    // Synchronous convenience — returns the result for sync handlers,
    // throws for asynchronous ones. Used by the HTTP RPC server and tests.
    JSON dispatch(std::string_view command, const JSON& payloadToUse) const;

    // Walks api.reflect(...) with a BindReflector — each command lands
    // in this bridge's CommandTable, each event subscribes a Listener
    // owned by the bridge (so subscriptions die with the bridge, not
    // with the API instance).
    //
    // ⚠ The API must outlive this Bridge. Installed handlers hold
    // &api, and bound listeners hold pointers into the API's event
    // broadcasters. EA::Broadcaster does not currently null-out
    // attached listeners on destruction, so if the API destructs
    // first, the Bridge's listener teardown will read freed memory.
    //
    // Practical rule: declare the API before the Bridge in your app
    // struct, so destruction happens Bridge → API. The same rule
    // applies in tests:
    //
    //     auto api    = Todos {};      // declared first → destructed last
    //     auto bridge = Bridge {};     // declared second → destructed first
    //     bridge.use(api);
    template <typename Api>
    void use(Api& api)
    {
        auto reflector = Detail::BindReflector {*this, &api};
        api.reflect(reflector);
    }

    // Pre-serialized variant of emit. The templated emit<T> goes
    // through toJSON; this one is for callers that already hold a JSON
    // payload (e.g. the bind reflector's event subscriptions, which
    // serialize once inside their listener body).
    void emitJson(const std::string& eventToUse, const JSON& payloadToUse);

    // Adopts a Listener so its subscription stays alive as long as
    // this bridge does. Called by BindReflector::eventImpl; rarely
    // called by user code directly.
    void attachListener(OwningPointer<EA::Listener> listener)
    {
        boundListeners.add(std::move(listener));
    }

    CommandTable& commandTable() { return commands; }

    // Fires on every emit. Transports attach an EA::Listener and read
    // the current event/payload via currentEvent()/currentPayload()
    // inside their callback.
    EA::Broadcaster onEmit;

    std::string_view currentEvent() const { return event; }
    const JSON& currentPayload() const { return *payload; }

private:
    CommandTable commands;
    std::string_view event;
    const JSON* payload = nullptr;
    Vector<OwningPointer<EA::Listener>> boundListeners;
};

} // namespace Miro
