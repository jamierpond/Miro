#pragma once

#include "Reflector.h"

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
// commands/events land under "files::<name>" and "users::<name>"
// ("::" is the namespace separator the codegen nests on; see
// ApiReflector::joinedName).
//
// Requires <Miro/Miro.h> (or Bridge/ApiReflector.h directly) in scope
// at expansion — the macro does not include it.
#define MIRO_API(refExpr, ...)                                                      \
    MIRO_FOR_EACH_WITH(MIRO_API_FIELD, refExpr, __VA_ARGS__)

#define MIRO_REFLECT(...)                                                           \
    void reflect([[maybe_unused]] Miro::Reflector& ref)                             \
    {                                                                               \
        MIRO_FIELDS(ref, __VA_ARGS__)                                               \
    }

#define MIRO_REFLECT_EXTERNAL_FIELD(field) ref[#field](valueToUse.field);

#define MIRO_REFLECT_EXTERNAL(Type, ...)                                            \
    namespace Miro                                                                  \
    {                                                                               \
    inline void reflect([[maybe_unused]] Miro::Reflector& ref,                      \
                        [[maybe_unused]] Type& valueToUse)                          \
    {                                                                               \
        MIRO_FOR_EACH(MIRO_REFLECT_EXTERNAL_FIELD, __VA_ARGS__)                     \
    }                                                                               \
    }

#define MIRO_REFLECT_NAMED_FIELD(field, key) ref[key](field);

#define MIRO_REFLECT_MEMBERS(...)                                                   \
    void reflect([[maybe_unused]] Miro::Reflector& ref)                             \
    {                                                                               \
        MIRO_FOR_EACH_PAIR(MIRO_REFLECT_NAMED_FIELD, __VA_ARGS__)                   \
    }

#define MIRO_REFLECT_EXTERNAL_NAMED_FIELD(field, key) ref[key](valueToUse.field);

#define MIRO_REFLECT_EXTERNAL_MEMBERS(Type, ...)                                    \
    namespace Miro                                                                  \
    {                                                                               \
    inline void reflect([[maybe_unused]] Miro::Reflector& ref,                      \
                        [[maybe_unused]] Type& valueToUse)                          \
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
    void reflect([[maybe_unused]] Miro::Reflector& ref)                             \
    {                                                                               \
        Miro::reflectPolymorphic(                                                   \
            ref,                                                                    \
            field,                                                                  \
            [&]([[maybe_unused]] auto& d)                                           \
            { MIRO_FOR_EACH_PAIR(MIRO_POLY_ALT_PAIR, __VA_ARGS__) });               \
    }

#define MIRO_REFLECT_EXTERNAL_POLY(Type, field, ...)                                \
    namespace Miro                                                                  \
    {                                                                               \
    inline void reflect([[maybe_unused]] Miro::Reflector& ref,                      \
                        [[maybe_unused]] Type& valueToUse)                          \
    {                                                                               \
        Miro::reflectPolymorphic(                                                   \
            ref,                                                                    \
            valueToUse.field,                                                       \
            [&]([[maybe_unused]] auto& d)                                           \
            { MIRO_FOR_EACH_PAIR(MIRO_POLY_ALT_PAIR, __VA_ARGS__) });               \
    }                                                                               \
    }
