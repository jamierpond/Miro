#pragma once

// Polymorphic reflector for declaring an API's commands and events.
//
// Mirrors the pattern Miro::Reflector uses for data: the user writes a
// non-static `void reflect(Miro::ApiReflector& r)` method on their API
// class; the library walks that method with a concrete reflector that
// either binds the API to a Bridge at runtime or records its shape for
// codegen.
//
//   class Todos
//   {
//   public:
//       void reflect(Miro::ApiReflector& r)
//       {
//           using T = Todos;
//           r.commands<&T::getTodos, &T::addTodo>();
//           r.events<&T::changes>();
//       }
//
//       TodoState getTodos() const;
//       void addTodo(const AddRequest& req);
//
//       Miro::Event<TodoState> changes;
//   };
//
// Names default to the unqualified member identifier — derived from the
// pmf itself via Detail::memberNameOf, so renames in the C++ source
// automatically rename on the wire. When the wire name must differ from
// the C++ identifier, drop back to the explicit overloads in the same
// reflect() body:
//
//       r.command(&T::oldName, "newWireName");
//       r.event  (&T::changes, "otherWireName");
//
// The templated entry points sit on this base (non-virtual). They use
// MethodInfo / EventMemberInfo to extract types, pack a type-erased
// descriptor, and dispatch through a virtual hook the concrete
// reflectors override. Same trick CommandTable already uses to route
// typed handlers through a runtime RawHandler.
//
// Sub-APIs: a reflect() body can defer to a member's own reflect()
// via r.use("subname", member) — every command/event the sub declares
// lands under "subname.<name>" on the wire and in DescribeReflector.
// r.use(member) (no key) does the same with no prefix; useful for
// splitting one API's reflect() across helper objects. Sub may
// declare reflect() intrusively (Sub::reflect(ApiReflector&)) or via
// a free reflect(Miro::ApiReflector&, Sub&) overload — same dual
// dispatch the data Reflector uses.

#include "../Reflection/CommandTable.h"
#include "../Reflection/TypeName.h"
#include "../TypeTree/TypeTree.h"
#include "Callable.h"

#include <ea_data_structures/Pointers/Broadcaster.h>
#include <ea_data_structures/Pointers/OwningPointer.h>

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Miro
{

class Bridge;
class ApiReflector;

namespace Detail
{

template <typename T>
concept HasApiReflectMember = requires(T& v, ApiReflector& r) { v.reflect(r); };

template <typename T>
concept HasApiExternalReflect = requires(T& v, ApiReflector& r) { reflect(r, v); };

template <typename T>
concept ApiReflectable = HasApiReflectMember<T> || HasApiExternalReflect<T>;

// Sibling of MethodInfo<Pmf> for pmds — extracts owning class + value
// type from a pointer-to-data-member. ApiReflector::api<...> uses it to
// recover the field type from each `&T::field` template argument so it
// can branch on EventLike<T> vs ApiReflectable<T>.
template <typename>
struct PmdTraits;

template <typename C, typename T>
struct PmdTraits<T C::*>
{
    using Class = C;
    using Type = T;
};

} // namespace Detail

// In-place variant of TypeTree::buildTree<T>(). The descriptors carry
// these as `void(TypeNode&)` factories so DescribeReflector can build
// trees directly into an emplace_back'd slot — avoiding any move of a
// TypeNode (which owns its children via OwningPointer<TypeNode> and
// is safer left in place once built).
template <typename T>
void buildTreeInto(TypeTree::TypeNode& root)
{
    auto opts = Detail::topLevelOptions<T>(Mode::Save, /*schema=*/true);
    auto reflector = TypeTree::TypeReflector {root, opts};
    auto value = T {};
    Detail::reflectValue(reflector, value);
}

namespace Detail
{
using NodeFunc = std::function<void(TypeTree::TypeNode&)>;

// One reflected type as seen from a user's reflect() body: the Req
// side of a command, the Res side, or the payload of an Event<T>.
// `buildTree` is empty when the type is elided (e.g. void return,
// nullary command) — operator bool() lets consumers gate cleanly.
struct TypeInfo
{
    std::string_view name;
    std::string_view qualifiedName;

    // In-place TypeNode factory; see buildTreeInto<T> for the contract.
    // Caller controls where the node lives — TypeNode owns its
    // children via OwningPointer<TypeNode> and is safer left in place
    // once built.
    NodeFunc buildTree;

    explicit operator bool() const { return bool(buildTree); }
};

template <typename T>
TypeInfo makeTypeInfo()
{
    return {typeNameOf<T>(),
            qualifiedNameOf<T>(),
            [](TypeTree::TypeNode& root) { buildTreeInto<T>(root); }};
}

// Type-erased descriptor of one command declared from a user's
// reflect() body. Carries enough metadata for the describe-mode
// reflector to feed codegen plus a factory closure that, given the
// API-instance pointer, returns a RawHandler ready to install on a
// CommandTable. The pmf itself is captured inside `makeHandler` —
// the descriptor stays POD-like.
struct CommandDescriptor
{
    using HandlerFunc = std::function<CommandTable::RawHandler(void* apiInstance)>;

    std::string_view name;

    // Empty TypeInfo (operator bool == false) when the shape elides
    // that side: Res() / void(Req) / void().
    TypeInfo req;
    TypeInfo res;

    HandlerFunc makeHandler;
};

// Sibling descriptor for an Event<T> member. `makeListener` builds an
// owning Listener subscribed to the event's broadcaster; the listener
// closes over the supplied `emit` callback, which the binding side
// wires to its bridge's emit channel.
struct EventDescriptor
{
    using PayloadFunc = std::function<void(const JSON& payload)>;
    using ListenerFunc =
        std::function<OwningPointer<EA::Listener>(void*, PayloadFunc)>;

    std::string_view name;

    // Always populated — Event<T> always has a payload type.
    TypeInfo payload;

    ListenerFunc makeListener;

    // Returns toJSON(T{}) — i.e. the wire representation of a
    // default-constructed payload. Hook codegen uses this as the
    // initial value for useFoo() React hooks before the first real
    // emit arrives. Always populated.
    std::function<JSON()> defaultPayloadJson;

    // Keyed-collection metadata, used by React-hook codegen to emit
    // useXxx/useXxxIds/useXxxItem instead of a flat store. Populated
    // only by ApiReflector::keyedEvent — plain event() leaves these
    // empty and downstream formats treat the event as non-keyed.
    bool isKeyed = false;
    std::string_view collectionField;
    std::string_view keyField;
};
} // namespace Detail

class ApiReflector
{
public:
    virtual ~ApiReflector() = default;

    template <typename Pmf>
        requires std::is_member_function_pointer_v<Pmf>
    void command(Pmf method, std::string_view name)
    {
        using Info = MethodInfo<Pmf>;

        auto fullName = joinedName(name);

        auto d = Detail::CommandDescriptor {};
        d.name = fullName;

        if constexpr (Info::hasReq)
            d.req = Detail::makeTypeInfo<typename Info::Req>();
        // Unwrap awaitable results (e.g. Async<T>) so codegen emits the
        // settled wire type T — the bridge serialises the resolved value,
        // never the wrapper. Async<void> unwraps to void and elides the
        // response side, exactly like a synchronous void command.
        using Res = Detail::AwaitResultT<typename Info::Res>;
        if constexpr (!std::is_void_v<Res>)
            d.res = Detail::makeTypeInfo<Res>();

        d.makeHandler = [method](void* apiInstance) -> CommandTable::RawHandler
        {
            auto& api = *static_cast<typename Info::Class*>(apiInstance);
            return makePmfHandler(method, api);
        };

        commandImpl(d);
    }

    // Name-free overload: derives the command name from the pmf itself
    // via Detail::memberNameOf<Pmf>(). Prefer this in fresh reflect()
    // bodies — the string overload above stays available for renames.
    template <auto Pmf>
        requires std::is_member_function_pointer_v<decltype(Pmf)>
    void command()
    {
        command(Pmf, Detail::memberNameOf<Pmf>());
    }

    // Free-function command: same wiring as the pmf overload, but with
    // no instance to bind — the descriptor's makeHandler ignores the
    // apiInstance pointer and routes the call straight through the
    // function pointer. FunctionInfo<F*> supplies the same hasReq /
    // hasRes / Req / Res shape MethodInfo provides for pmfs.
    template <auto Func>
        requires std::is_function_v<std::remove_pointer_t<decltype(Func)>>
    void command()
    {
        using Info = FunctionInfo<decltype(Func)>;

        auto fullName = joinedName(Detail::memberNameOf<Func>());

        auto d = Detail::CommandDescriptor {};
        d.name = fullName;

        if constexpr (Info::hasReq)
            d.req = Detail::makeTypeInfo<typename Info::Req>();
        // Unwrap awaitable results (e.g. Async<T>) so codegen emits the
        // settled wire type T — the bridge serialises the resolved value,
        // never the wrapper. Async<void> unwraps to void and elides the
        // response side, exactly like a synchronous void command.
        using Res = Detail::AwaitResultT<typename Info::Res>;
        if constexpr (!std::is_void_v<Res>)
            d.res = Detail::makeTypeInfo<Res>();

        d.makeHandler = [](void*) -> CommandTable::RawHandler
        {
            return Detail::makeJsonAdapter<Info>(
                [](auto&&... args) -> decltype(auto)
                { return Func(std::forward<decltype(args)>(args)...); });
        };

        commandImpl(d);
    }

    // Callable-object command: accepts lambdas (capturing and
    // captureless), std::function, and other invocables with a single
    // operator(). Req/Res are recovered from MethodInfo<&Callable::
    // operator()>, so any of the four shapes the pmf path supports
    // (Res(Req) / Res() / void(Req) / void()) work here too.
    //
    // Explicit name only — lambda types have no source-derived name
    // for memberNameOf to extract, so the caller must supply one.
    template <typename Callable>
        requires(!std::is_member_function_pointer_v<Callable>
                 && !std::is_function_v<std::remove_pointer_t<Callable>>
                 && requires { &Callable::operator(); })
    void command(Callable callable, std::string_view name)
    {
        using Pmf = decltype(&Callable::operator());
        using Info = MethodInfo<Pmf>;

        auto fullName = joinedName(name);

        auto d = Detail::CommandDescriptor {};
        d.name = fullName;

        if constexpr (Info::hasReq)
            d.req = Detail::makeTypeInfo<typename Info::Req>();
        // Unwrap awaitable results (e.g. Async<T>) so codegen emits the
        // settled wire type T — the bridge serialises the resolved value,
        // never the wrapper. Async<void> unwraps to void and elides the
        // response side, exactly like a synchronous void command.
        using Res = Detail::AwaitResultT<typename Info::Res>;
        if constexpr (!std::is_void_v<Res>)
            d.res = Detail::makeTypeInfo<Res>();

        d.makeHandler = [c = std::move(callable)](void*) mutable
            -> CommandTable::RawHandler
        { return Detail::makeJsonAdapter<Info>(std::move(c)); };

        commandImpl(d);
    }

    // Variadic sugar so a whole API can be declared in one statement:
    //   r.commands<&Clock::getCurrentTick, &Clock::reset>();
    template <auto... Pmfs>
        requires(sizeof...(Pmfs) > 0)
    void commands()
    {
        (command<Pmfs>(), ...);
    }

    template <typename Pmd>
    void event(Pmd member, std::string_view name)
    {
        auto fullName = joinedName(name);
        eventImpl(makeEventDescriptor(member, fullName));
    }

    // Name-free overload: derives the event name from the pmd itself.
    template <auto Pmd>
    void event()
    {
        event(Pmd, Detail::memberNameOf<Pmd>());
    }

    // Variadic sugar — see commands<...>().
    template <auto... Pmds>
        requires(sizeof...(Pmds) > 0)
    void events()
    {
        (event<Pmds>(), ...);
    }

    // Same wiring as event(), plus marks the payload as a keyed
    // collection — `collectionField` names a vector-typed field on
    // Payload, `keyField` names the id-like field on each item.
    // React-hook codegen uses this metadata to emit per-id selector
    // hooks (useXxx / useXxxIds / useXxxItem) instead of one flat
    // store. Replaces EACP_KEYED_STATE on the static-init path.
    template <typename Pmd>
    void keyedEvent(Pmd member,
                    std::string_view name,
                    std::string_view collectionField,
                    std::string_view keyField)
    {
        auto fullName = joinedName(name);
        auto d = makeEventDescriptor(member, fullName);
        d.isKeyed = true;
        d.collectionField = collectionField;
        d.keyField = keyField;
        eventImpl(d);
    }

    // Unified dispatcher: classifies each Member at compile time and
    // forwards to the matching overload. A pmf becomes a command
    // (auto-named); a pmd to Event<T> / RefEvent<T> becomes an event
    // (auto-named); a pmd to an ApiReflectable type becomes a sub-API
    // installed under the field identifier as prefix (so the field
    // name doubles as the wire-name prefix).
    //
    // `self` is passed explicitly so the dispatcher can deref pmds
    // (`self.*Member`) without consulting currentApiInstance() — that
    // path works in both bind and describe modes without requiring the
    // concrete reflector to push an initial frame.
    //
    // Mostly invoked through MIRO_REFLECT_API. Hand-written reflect()
    // bodies can call it directly: r.api<&T::a, &T::b>(*this).
    template <auto... Members, typename Self>
        requires(sizeof...(Members) > 0)
    void api(Self& self)
    {
        (apiOne<Members>(self), ...);
    }

    // Recurse into a sub-API: every command/event the sub declares is
    // emitted with "key." prepended on the wire and in
    // DescribeReflector. The current API-instance pointer is swapped
    // for &sub for the duration of the recursion so concrete
    // reflectors that install pmf handlers cast against the right
    // type. Sub may declare reflect() intrusively or as a free
    // reflect(Miro::ApiReflector&, Sub&) overload (ADL).
    template <typename Sub>
        requires Detail::ApiReflectable<Sub>
    void use(std::string_view key, Sub& sub)
    {
        frames.add(Frame {static_cast<void*>(&sub), std::string {key}});
        dispatchSubReflect(sub);
        frames.pop_back();
    }

    // Same as use(key, sub) but flat — no prefix contribution. Useful
    // for splitting one API's reflect() across helper objects that
    // share the wire namespace.
    template <typename Sub>
        requires Detail::ApiReflectable<Sub>
    void use(Sub& sub)
    {
        frames.add(Frame {static_cast<void*>(&sub), std::string {}});
        dispatchSubReflect(sub);
        frames.pop_back();
    }

protected:
    explicit ApiReflector(void* initialInstance = nullptr)
    {
        if (initialInstance != nullptr)
            frames.add(Frame {initialInstance, std::string {}});
    }

    // Concrete reflectors call this from commandImpl/eventImpl to get
    // the instance the descriptor's makeHandler / makeListener should
    // bind against. Tracks the use<>() recursion stack — top frame is
    // the deepest sub-API currently being walked.
    void* currentApiInstance() const
    {
        return frames.empty() ? nullptr : frames.back().api;
    }

    virtual void commandImpl(const Detail::CommandDescriptor&) = 0;
    virtual void eventImpl(const Detail::EventDescriptor&) = 0;

private:
    // One frame per active use<>() recursion (plus the initial frame
    // pushed by concrete reflectors that need to bind handlers). The
    // prefix is empty for frames pushed via the no-key use(sub)
    // overload — joinedName skips them.
    struct Frame
    {
        void* api = nullptr;
        std::string prefix;
    };

    template <typename Sub>
    void dispatchSubReflect(Sub& sub)
    {
        if constexpr (Detail::HasApiReflectMember<Sub>)
            sub.reflect(*this);
        else
            reflect(*this, sub);
    }

    // One step of the pack expansion driven by api<...>. Kept private
    // so the variadic entry is the only public surface — the dispatch
    // shape (pmf / free fn / event / sub) is an implementation detail.
    template <auto Member, typename Self>
    void apiOne(Self& self)
    {
        using M = decltype(Member);

        if constexpr (std::is_member_function_pointer_v<M>
                      || std::is_function_v<std::remove_pointer_t<M>>)
        {
            command<Member>();
        }
        else
        {
            using Type = typename Detail::PmdTraits<M>::Type;

            if constexpr (EventLike<Type>)
                event<Member>();
            else if constexpr (Detail::ApiReflectable<Type>)
                use(Detail::memberNameOf<Member>(), self.*Member);
            else
                static_assert(sizeof(Type) == 0,
                              "ApiReflector::api: member is neither a "
                              "callable, an Event<T>, nor an "
                              "ApiReflectable sub-API.");
        }
    }

    // Joins active prefixes with '.' and appends `local`. Empty prefix
    // frames (from no-key use) contribute nothing. Result is a fresh
    // std::string the caller keeps alive for the duration of
    // commandImpl/eventImpl, which copies the name immediately.
    std::string joinedName(std::string_view local) const
    {
        auto out = std::string {};
        for (auto& f: frames)
        {
            if (f.prefix.empty())
                continue;
            out += f.prefix;
            out += '.';
        }
        out += local;
        return out;
    }

    // Shared construction shared between event() and keyedEvent().
    // Captures Pmd-dependent type info via EventMemberInfo, populates
    // every Pmd-derived field — keyed metadata is filled in by the
    // public keyedEvent() wrapper after this returns.
    template <typename Pmd>
    static Detail::EventDescriptor makeEventDescriptor(Pmd member,
                                                       std::string_view name)
    {
        using Info = EventMemberInfo<Pmd>;
        using Payload = typename Info::Payload;

        auto d = Detail::EventDescriptor {};
        d.name = name;
        d.payload = Detail::makeTypeInfo<Payload>();
        d.defaultPayloadJson = [] { return toJSON(Payload {}); };

        d.makeListener = [member](void* apiInstance,
                                  std::function<void(const JSON&)> emit)
            -> OwningPointer<EA::Listener>
        {
            auto& api = *static_cast<typename Info::Class*>(apiInstance);
            auto& event = api.*member;

            return EA::makeOwned<EA::Listener>(
                event.broadcaster(),
                [&event, emit = std::move(emit)] { emit(toJSON(event.snapshot())); },
                EA::Listener::Modes::TriggerOnEvent);
        };

        return d;
    }

    Vector<Frame> frames;
};

} // namespace Miro
