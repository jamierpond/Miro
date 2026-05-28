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
    void command(Pmf method, std::string_view name)
    {
        using Info = MethodInfo<Pmf>;

        auto fullName = joinedName(name);

        auto d = Detail::CommandDescriptor {};
        d.name = fullName;

        if constexpr (Info::hasReq)
            d.req = Detail::makeTypeInfo<typename Info::Req>();
        if constexpr (Info::hasRes)
            d.res = Detail::makeTypeInfo<typename Info::Res>();

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
    void command()
    {
        command(Pmf, Detail::memberNameOf<Pmf>());
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

    // Recurse into a sub-API: every command/event the sub declares is
    // emitted with "key::" prepended on the wire and in
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

    // Joins active prefixes with "::" and appends `local`. Empty prefix
    // frames (from no-key use) contribute nothing. Result is a fresh
    // std::string the caller keeps alive for the duration of
    // commandImpl/eventImpl, which copies the name immediately.
    //
    // "::" (not ".") is the namespace separator the codegen splits on to
    // build nested objects — see CommandExport ("Commands whose names
    // contain \"::\" become nested objects") and CppClient ("::" -> "_").
    // A "." here produces a single un-split segment, so codegen emits the
    // dotted name as one bare JS key (invalid TS).
    std::string joinedName(std::string_view local) const
    {
        auto out = std::string {};
        for (auto& f: frames)
        {
            if (f.prefix.empty())
                continue;
            out += f.prefix;
            out += "::";
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
