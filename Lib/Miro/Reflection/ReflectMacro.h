#pragma once

#include "PackedStruct.h"
#include "Reflector.h"

#include <concepts>

namespace Miro
{
class ApiReflector;
}

// Macro-based reflect() generator.
//
// Usage (intrusive, for types you own):
//   struct Foo
//   {
//       int x = 0;
//       std::string name;
//       MIRO_REFLECT(x, name)
//   };
//
// Usage (non-intrusive, for types you don't own):
//   // at global scope, after Foo is fully declared
//   MIRO_REFLECT_EXTERNAL(Foo, x, name)
//
// Each listed member becomes ref["member"](member). The field name is used
// as the JSON key. Supports up to ~256 fields.
//
// Usage (custom JSON keys, intrusive):
//   MIRO_REFLECT_MEMBERS(x, "X Coord", name, "Full Name")
//
// Usage (custom JSON keys, non-intrusive):
//   MIRO_REFLECT_EXTERNAL_MEMBERS(Foo, x, "X Coord", name, "Full Name")
//
// The ..._MEMBERS variants take (field, keyString) pairs instead of bare
// field names, so the JSON key can differ from the C++ identifier.
//
// Usage (hand-written reflect body with auto-named fields):
//   void reflect(Miro::Reflector& ref)
//   {
//       if (ref.isLoading())
//           onAboutToLoad.trigger();
//
//       MIRO_FIELDS(ref, x, name);
//
//       if (ref.isLoading())
//           onLoaded.trigger();
//   }
//
// MIRO_FIELDS is the building block MIRO_REFLECT itself uses. Reach for it
// when you need custom logic inside reflect() but still want to avoid
// hand-typing each field name as both an identifier and a string.
//
// Compile-time cost: every body-generating macro here (MIRO_REFLECT,
// MIRO_REFLECT_MEMBERS, MIRO_REFLECT_POLY, MIRO_REFLECT_API and their
// _EXTERNAL siblings) emits reflect() as a constrained member / free
// function *template* rather than a plain function:
//
//   template <std::derived_from<Miro::Reflector> R>
//   void reflect(R& ref) { ... }
//
// A plain inline body is semantically analysed in every translation
// unit that includes the class, which instantiates the whole
// reflectValue dispatch chain for every field type whether or not that
// TU ever serializes anything. A template body is only instantiated in
// the TUs that actually reflect the type, so a model header costs its
// includers nothing beyond parsing. Call sites are unaffected: any
// Reflector-derived object binds R by deduction, the HasReflectMember /
// Reflectable concepts still hold, and Base::reflect(ref) still works
// from a derived class's hand-written body.
//
// Two places a template cannot go, and where you write the body by
// hand instead (MIRO_FIELDS keeps that to one line):
//
//   * A local class (a struct defined inside a function) — C++ forbids
//     member templates there:
//         void reflect(Miro::Reflector& ref) { MIRO_FIELDS(ref, x, y); }
//   * A virtual reflect() you intend to override — templates can't be
//     virtual, and a macro-generated template would silently hide the
//     base's virtual rather than override it.

#define MIRO_PARENS ()

#define MIRO_EXPAND(...)                                                            \
    MIRO_EXPAND4(MIRO_EXPAND4(MIRO_EXPAND4(MIRO_EXPAND4(__VA_ARGS__))))
#define MIRO_EXPAND4(...)                                                           \
    MIRO_EXPAND3(MIRO_EXPAND3(MIRO_EXPAND3(MIRO_EXPAND3(__VA_ARGS__))))
#define MIRO_EXPAND3(...)                                                           \
    MIRO_EXPAND2(MIRO_EXPAND2(MIRO_EXPAND2(MIRO_EXPAND2(__VA_ARGS__))))
#define MIRO_EXPAND2(...)                                                           \
    MIRO_EXPAND1(MIRO_EXPAND1(MIRO_EXPAND1(MIRO_EXPAND1(__VA_ARGS__))))
#define MIRO_EXPAND1(...) __VA_ARGS__

#define MIRO_FOR_EACH(macro, ...)                                                   \
    __VA_OPT__(MIRO_EXPAND(MIRO_FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define MIRO_FOR_EACH_HELPER(macro, a, ...)                                         \
    macro(a) __VA_OPT__(MIRO_FOR_EACH_AGAIN MIRO_PARENS(macro, __VA_ARGS__))
#define MIRO_FOR_EACH_AGAIN() MIRO_FOR_EACH_HELPER

#define MIRO_FOR_EACH_PAIR(macro, ...)                                              \
    __VA_OPT__(MIRO_EXPAND(MIRO_FOR_EACH_PAIR_HELPER(macro, __VA_ARGS__)))
#define MIRO_FOR_EACH_PAIR_HELPER(macro, a, b, ...)                                 \
    macro(a, b) __VA_OPT__(MIRO_FOR_EACH_PAIR_AGAIN MIRO_PARENS(macro, __VA_ARGS__))
#define MIRO_FOR_EACH_PAIR_AGAIN() MIRO_FOR_EACH_PAIR_HELPER

// MIRO_FOR_EACH_PAIR with a fixed `extra` first argument:
// macro(extra, a, b) macro(extra, c, d) ...
#define MIRO_FOR_EACH_PAIR_WITH(macro, extra, ...)                                  \
    __VA_OPT__(                                                                     \
        MIRO_EXPAND(MIRO_FOR_EACH_PAIR_WITH_HELPER(macro, extra, __VA_ARGS__)))
#define MIRO_FOR_EACH_PAIR_WITH_HELPER(macro, extra, a, b, ...)                     \
    macro(extra, a, b) __VA_OPT__(                                                  \
        MIRO_FOR_EACH_PAIR_WITH_AGAIN MIRO_PARENS(macro, extra, __VA_ARGS__))
#define MIRO_FOR_EACH_PAIR_WITH_AGAIN() MIRO_FOR_EACH_PAIR_WITH_HELPER

// Like MIRO_FOR_EACH, but threads a fixed `extra` argument through every
// invocation: macro(extra, a) macro(extra, b) ... — used by MIRO_FIELDS
// to bind the user-supplied reflector expression into each field call.
#define MIRO_FOR_EACH_WITH(macro, extra, ...)                                       \
    __VA_OPT__(MIRO_EXPAND(MIRO_FOR_EACH_WITH_HELPER(macro, extra, __VA_ARGS__)))
#define MIRO_FOR_EACH_WITH_HELPER(macro, extra, a, ...)                             \
    macro(extra, a)                                                                 \
        __VA_OPT__(MIRO_FOR_EACH_WITH_AGAIN MIRO_PARENS(macro, extra, __VA_ARGS__))
#define MIRO_FOR_EACH_WITH_AGAIN() MIRO_FOR_EACH_WITH_HELPER

#define MIRO_FIELDS_FIELD(refExpr, field) refExpr[#field](field);

// Public: drop into a hand-written reflect() body to reflect a list of
// fields without re-typing their names as strings. `refExpr` is evaluated
// once per field (typically just the bare reflector parameter name).
#define MIRO_FIELDS(refExpr, ...)                                                   \
    MIRO_FOR_EACH_WITH(MIRO_FIELDS_FIELD, refExpr, __VA_ARGS__)

#define MIRO_API_FIELD(refExpr, field) refExpr.use(#field, field);

// ApiReflector sibling of MIRO_FIELDS: inside an ApiReflector reflect()
// body, defer to a list of sub-API members using each one's identifier
// as the wire-name prefix. Expands `MIRO_API(r, files, users)` to
// `r.use("files", files); r.use("users", users);` — each sub's
// commands/events land under "files.<name>" and "users.<name>".
//
// Requires <Miro/Miro.h> (or Bridge/ApiReflector.h directly) in scope
// at expansion — the macro does not include it.
#define MIRO_API(refExpr, ...)                                                      \
    MIRO_FOR_EACH_WITH(MIRO_API_FIELD, refExpr, __VA_ARGS__)

#define MIRO_REFLECT_API_FIELD(refExpr, field)                                      \
    refExpr.template api<&MiroReflectApiSelf::field>(*this);

// ApiReflector sibling of MIRO_REFLECT: generates a reflect() body that
// hands each listed member to ApiReflector::api<...>(*this). Each
// member is auto-classified — a method becomes a command, an Event<T>
// data member becomes an event, and an ApiReflectable data member
// becomes a sub-API installed under its identifier as prefix.
//
// Usage:
//   class Todos
//   {
//   public:
//       TodoState getTodos() const;
//       void addTodo(const AddRequest& req);
//       Miro::Event<TodoState> changes;
//       FilesSubApi files;
//
//       MIRO_REFLECT_API(getTodos, addTodo, changes, files)
//   };
//
// Requires <Miro/Miro.h> (or Bridge/ApiReflector.h + <type_traits>) in
// scope at expansion.
#define MIRO_REFLECT_API(...)                                                       \
    template <std::derived_from<Miro::ApiReflector> MiroApiReflectorType>           \
    void reflect(MiroApiReflectorType& __VA_OPT__(r))                               \
    {                                                                               \
        __VA_OPT__(using MiroReflectApiSelf =                                       \
                       std::remove_cvref_t<decltype(*this)>;)                       \
        MIRO_FOR_EACH_WITH(MIRO_REFLECT_API_FIELD, r, __VA_ARGS__)                  \
    }

// The field list MIRO_REFLECT emits beside reflect(), from which
// PackedStruct.h decides at compile time whether the type is packable.
// getMiroFieldListOwner() names the class that wrote the list, so a
// derived class doesn't pass for its base.
#define MIRO_FIELD_LIST_FROM(describedFields)                                       \
    template <typename MiroSelf>                                                    \
    static constexpr auto describeMiroFields()                                      \
    {                                                                               \
        return std::tuple_cat(std::tuple<> {} describedFields);                     \
    }                                                                               \
    auto getMiroFieldListOwner() const -> std::remove_cvref_t<decltype(*this)>*;

#define MIRO_FIELD_LIST(...)                                                        \
    MIRO_FIELD_LIST_FROM(                                                           \
        MIRO_FOR_EACH_WITH(MIRO_DESCRIBE_FIELD, MiroSelf, __VA_ARGS__))

#define MIRO_FIELD_LIST_NAMED(...)                                                  \
    MIRO_FIELD_LIST_FROM(                                                           \
        MIRO_FOR_EACH_PAIR_WITH(MIRO_DESCRIBE_FIELD_NAMED, MiroSelf, __VA_ARGS__))

#define MIRO_REFLECT(...)                                                           \
    template <std::derived_from<Miro::Reflector> MiroReflectorType>                 \
    void reflect(MiroReflectorType& __VA_OPT__(ref)) {                              \
        MIRO_FIELDS(ref, __VA_ARGS__)} MIRO_FIELD_LIST(__VA_ARGS__)

#define MIRO_REFLECT_EXTERNAL_FIELD(field) ref[#field](valueToUse.field);

// The external counterpart of MIRO_FIELD_LIST. describeFields() is a
// template so the offsetof of a non-standard-layout Type sits in a
// discarded branch that is never instantiated.
#define MIRO_EXTERNAL_FIELD_LIST_FROM(Type, describedFields)                        \
    template <>                                                                     \
    struct ExternalFields<Type>                                                     \
    {                                                                               \
        static constexpr bool isDeclared = true;                                    \
                                                                                    \
        template <typename MiroSelf>                                                \
        static constexpr auto describeFields()                                      \
        {                                                                           \
            return std::tuple_cat(std::tuple<> {} describedFields);                 \
        }                                                                           \
    };

#define MIRO_EXTERNAL_FIELD_LIST(Type, ...)                                         \
    MIRO_EXTERNAL_FIELD_LIST_FROM(                                                  \
        Type, MIRO_FOR_EACH_WITH(MIRO_DESCRIBE_FIELD, MiroSelf, __VA_ARGS__))

#define MIRO_EXTERNAL_FIELD_LIST_NAMED(Type, ...)                                   \
    MIRO_EXTERNAL_FIELD_LIST_FROM(                                                  \
        Type,                                                                       \
        MIRO_FOR_EACH_PAIR_WITH(MIRO_DESCRIBE_FIELD_NAMED, MiroSelf, __VA_ARGS__))

#define MIRO_REFLECT_EXTERNAL(Type, ...)                                            \
    namespace Miro                                                                  \
    {                                                                               \
    MIRO_EXTERNAL_FIELD_LIST(Type, __VA_ARGS__)                                     \
                                                                                    \
    template <std::derived_from<Miro::Reflector> MiroReflectorType>                 \
    void reflect(MiroReflectorType& __VA_OPT__(ref), Type& __VA_OPT__(valueToUse))  \
    {                                                                               \
        MIRO_FOR_EACH(MIRO_REFLECT_EXTERNAL_FIELD, __VA_ARGS__)                     \
    }                                                                               \
    }

#define MIRO_REFLECT_NAMED_FIELD(field, key) ref[key](field);

#define MIRO_REFLECT_MEMBERS(...)                                                   \
    template <std::derived_from<Miro::Reflector> MiroReflectorType>                 \
    void reflect(MiroReflectorType& __VA_OPT__(ref)) {MIRO_FOR_EACH_PAIR(           \
        MIRO_REFLECT_NAMED_FIELD, __VA_ARGS__)} MIRO_FIELD_LIST_NAMED(__VA_ARGS__)

#define MIRO_REFLECT_EXTERNAL_NAMED_FIELD(field, key) ref[key](valueToUse.field);

#define MIRO_REFLECT_EXTERNAL_MEMBERS(Type, ...)                                    \
    namespace Miro                                                                  \
    {                                                                               \
    MIRO_EXTERNAL_FIELD_LIST_NAMED(Type, __VA_ARGS__)                               \
                                                                                    \
    template <std::derived_from<Miro::Reflector> MiroReflectorType>                 \
    void reflect(MiroReflectorType& __VA_OPT__(ref), Type& __VA_OPT__(valueToUse))  \
    {                                                                               \
        MIRO_FOR_EACH_PAIR(MIRO_REFLECT_EXTERNAL_NAMED_FIELD, __VA_ARGS__)          \
    }                                                                               \
    }

// Polymorphic reflect: `field` is a std::variant or OwningPointer<Base>
// holder; pairs are (DerivedType, "tag") sequences. Generates a reflect()
// body that delegates to Miro::reflectPolymorphic with the listed
// alternatives. The lambda captures `d` from its enclosing scope, so
// the per-pair expansion doesn't need to thread it.
//
// Usage:
//   struct Shape
//   {
//       std::variant<Circle, Square> value;
//       MIRO_REFLECT_POLY(value, Circle, "circle", Square, "square")
//   };

#define MIRO_POLY_ALT_PAIR(type, tag) d.template alt<type>(tag);

#define MIRO_REFLECT_POLY(field, ...)                                               \
    template <std::derived_from<Miro::Reflector> MiroReflectorType>                 \
    void reflect(MiroReflectorType& ref)                                            \
    {                                                                               \
        Miro::reflectPolymorphic(                                                   \
            ref,                                                                    \
            field,                                                                  \
            [&](auto& __VA_OPT__(d))                                                \
            { MIRO_FOR_EACH_PAIR(MIRO_POLY_ALT_PAIR, __VA_ARGS__) });               \
    }

#define MIRO_REFLECT_EXTERNAL_POLY(Type, field, ...)                                \
    namespace Miro                                                                  \
    {                                                                               \
    template <std::derived_from<Miro::Reflector> MiroReflectorType>                 \
    void reflect(MiroReflectorType& ref, Type& valueToUse)                          \
    {                                                                               \
        reflectPolymorphic(                                                         \
            ref,                                                                    \
            valueToUse.field,                                                       \
            [&](auto& __VA_OPT__(d))                                                \
            { MIRO_FOR_EACH_PAIR(MIRO_POLY_ALT_PAIR, __VA_ARGS__) });               \
    }                                                                               \
    }

#define MIRO_PACKED_FIELD(Type, field, key)                                         \
    , Miro::Detail::describeField<decltype(Type::field)>(key, offsetof(Type, field))

// Lets a type whose reflect() is written by hand pack in arrays, given
// (member, "key") pairs that must match what its reflect() writes:
//   MIRO_PACKED(Game::Vec3, x, "x", y, "y", z, "z")
// At global scope, after the type is complete, like MIRO_ENUM_AS_INTEGER.
// Every member must be a fixed-width number or a packable struct.
#define MIRO_PACKED(Type, ...)                                                      \
    namespace Miro                                                                  \
    {                                                                               \
    template <>                                                                     \
    struct PackedFields<Type>                                                       \
    {                                                                               \
        static constexpr bool isDeclared = true;                                    \
                                                                                    \
        static constexpr auto describeFields()                                      \
        {                                                                           \
            static_assert(std::is_standard_layout_v<Type>                           \
                              && std::is_trivially_copyable_v<Type>,                \
                          "MIRO_PACKED needs a standard-layout, trivially "         \
                          "copyable type");                                         \
            return std::tuple_cat(std::tuple<> {                                    \
            } MIRO_FOR_EACH_PAIR_WITH(MIRO_PACKED_FIELD, Type, __VA_ARGS__));       \
        }                                                                           \
    };                                                                              \
    }
