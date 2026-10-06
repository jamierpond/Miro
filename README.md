# Miro

A lightweight C++20 JSON library with a reflection-based (de)serialization layer.

Miro provides two layers you can use independently:

- **`Miro::Json`** — a `std::variant`-based `Value` type, plus `parse()` and `print()`.
- **`Miro`** — a reflection layer (`toJSON` / `fromJSON`) that serializes your own structs via an intrusive `reflect()` method.

## Requirements

- C++20
- CMake 3.21+

## Building

```bash
cmake -B build
cmake --build build --config Release
```

## Running the tests

Tests are built when Miro is the top-level project (or when `MIRO_BUILD_TESTS=ON`). The test target is `MiroTests` and uses the [NanoTest](https://github.com/eyalamirmusic/NanoTest) framework, fetched automatically.

```bash
ctest --test-dir build --config Release --output-on-failure

# Run a single test by name regex
ctest --test-dir build --config Release -R "Parse object"
```

A benchmark target compares Miro against nlohmann/json and lives in `Tests/Benchmark/`.

## Using Miro in your project

With CMake FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(Miro GIT_REPOSITORY <url> GIT_TAG main)
FetchContent_MakeAvailable(Miro)

target_link_libraries(YourTarget PRIVATE Miro)
```

`#include <Miro/Miro.h>` is the umbrella header that exposes everything. If you only need part of the library, include just the layer you use:

| Header | Contents |
|---|---|
| `<Miro/Json.h>` | Raw JSON value type, `parse()` / `print()` — no reflection |
| `<Miro/Reflect.h>` | Reflection layer + JSON serialization (`MIRO_REFLECT`, `toJSON` / `fromJSON`) |
| `<Miro/Xml.h>` | XML value type + XML serialization (`toXML` / `fromXML`) |
| `<Miro/Bridge.h>` | Runtime command/event bridge (`Bridge`, `ApiReflector`, `Event`) |
| `<Miro/Codegen.h>` | Type-export / codegen toolchain (`codegenMain()`, TypeScript / schema / C++ emitters) |
| `<Miro/Unicode.h>` | Unicode character properties (`generalCategory()`, `isLetter()`, ...) and UTF-8 decode / encode |

Each entry header is self-contained. Headers in subdirectories (`Miro/Reflection/...`, `Miro/JSON/...`, ...) are implementation details and should not be included directly.

## The `Json` layer

The core type is `Miro::Json::Value`, a variant over `Null`, `bool`, `std::int64_t`, `double`, `std::string`, `Array` (`std::vector<Value>`), and `Object` (`std::map<std::string, Value>`).

```cpp
#include <Miro/Miro.h>

using namespace Miro::Json;

auto value = parse(R"({
    "name": "Miro",
    "version": 1.0,
    "features": ["json", "reflection"],
    "active": true
})");

auto name = value["name"].asString();       // "Miro"
auto first = value["features"][0].asString(); // "json"

auto compact = print(value);       // minified
auto pretty = print(value, 4);     // indented with 4 spaces
```

The typed accessors (`asBool` / `asNumber` / `asInteger` / `asString` / `asArray` / `asObject`, and the implicit conversions) throw `Miro::Json::AccessError` on a type mismatch, with a message that names both sides: `expected string but value is number`. Use the `is*` predicates to check first.

`operator[]` is unchecked, like the standard containers it forwards to: a key that is not there or an index past the end is the caller's error, not a checked condition. Use `find(object, key)` to look up an optional value by key, and write through `toObject()`.

`ParseError` is thrown on malformed input. Both it and `AccessError` derive from `Miro::Json::Error`, so one `catch (const Miro::Json::Error&)` covers everything this layer throws; `Error` itself derives from `std::runtime_error`.

### Numbers

A number is stored either as a `std::int64_t` or as a `double`, and `isNumber()` is true for both. A digits-only spelling with no fraction and no exponent parses as an integer, so a 64-bit byte offset or a snowflake ID survives a round trip that `double` would round away; anything wider than `int64` (or carrying a fraction or an exponent) is a `double`. `Value` constructs from every integral width — `int`, `unsigned`, `short`, `long long`, `std::size_t` — with an unsigned value past `INT64_MAX` widening to `double`, the only spelling left for it.

```cpp
auto id = parse("9007199254740993");   // 2^53 + 1

id.isNumber();    // true
id.isInteger();   // true — stored as an int64
id.asInteger();   // 9007199254740993
id.asNumber();    // 9007199254740992.0, the nearest double
print(id);        // "9007199254740993"
```

`isInteger()` reports the storage; `asInteger()` reports the value, so it also reads a `double` that names an exact integer (`parse("42.0").asInteger()` is `42`) and throws when the value has a fractional part, falls outside `int64`, or is not a number at all. Equality is numeric across the two: `Value{1} == Value{1.0}`.

Printing round-trips. Integers print in full, and a `double` prints in the shortest form that reads back as the same `double` — `print(parse("3.141592653589793"))` is `3.141592653589793`, not a six-digit approximation. A `double` with no fractional part that fits in `int64` prints without a decimal point, and infinity and NaN print as `null`, having no JSON spelling of their own.

## The reflection layer

Define a `reflect(Miro::Reflector&)` method on your type, binding each field to a key. The same function handles both saving and loading.

```cpp
#include <Miro/Miro.h>

struct Settings
{
    void reflect(Miro::Reflector& ref)
    {
        ref["name"](name);
        ref["count"](count);
        ref["tags"](tags);
    }

    std::string name;
    int count = 0;
    std::vector<std::string> tags;
};

auto s = Settings {"hello", 3, {"a", "b"}};

auto json = Miro::toJSONString(s, 4);
auto loaded = Miro::createFromJSONString<Settings>(json);
```

Built-in reflection is provided for:

- Primitives: `bool`, `int`, `double`, `std::string`
- All other integral types (`unsigned`, `short`, `long`, `long long`, `char`, ...) — serialized as JSON numbers
- `std::vector<T>` and `std::array<T, N>`
- `std::map<std::string, V>`
- `std::optional<T>` — empty optionals serialize as JSON `null`; on load, `null` clears the optional and a missing key leaves it untouched
- `enum` and `enum class` — saved as the enumerator's name, loaded from a name or a number (see [Enums](#enums))
- `Miro::JSON` and `Miro::Json::Any` — a raw JSON value, carried through verbatim (see [Raw JSON fields](#raw-json-fields))
- `Miro::Omittable<T>` — an empty omittable writes no key at all; see [Absent keys](#absent-keys) below
- Any user type with a `reflect(Reflector&)` method (nested types compose)

Convenience functions:

- `Miro::toJSON(value)` / `Miro::fromJSON(value, json)`
- `Miro::createFromJSON<T>(json)`
- `Miro::toJSONString(value, indent = 0)` / `Miro::fromJSONString(value, str)`
- `Miro::createFromJSONString<T>(str)`

### Enums

Enums reflect with nothing to register — the enumerator names are read off the compiler's own signature strings. By default an enum saves as its name, which keeps a hand-edited file readable, and loads from either a name or a number:

```cpp
enum class Color { Red, Green, Blue };

struct Paint
{
    Color color = Color::Green;

    MIRO_REFLECT(color)
};

Miro::toJSONString(Paint {});   // {"color":"Green"}
```

A value with no matching enumerator falls back to its number, so it still round-trips. The names are also available on their own through `Miro::enumToString(value)`, `Miro::enumFromString<E>(name)` and `Miro::enumNames<E>()`.

Enumerators are probed over `[-128, 127]`. Specialize `Miro::EnumRange` for an enum that lives outside that window:

```cpp
template <>
struct Miro::EnumRange<Http::StatusCode>
{
    static constexpr int minValue = 100;
    static constexpr int maxValue = 599;
};
```

#### Enums as integers

Plenty of wire formats spell enums as numbers instead — Discord's `{"type": 1}`, for one, where the name a C++ enumerator happens to carry means nothing to the server. Specialize `Miro::EnumFormat` and the type saves as its underlying value:

```cpp
namespace Discord
{
enum class ChannelType : int { guildText = 0, dm = 1, guildVoice = 2 };
}

template <>
struct Miro::EnumFormat<Discord::ChannelType>
{
    static constexpr bool integer = true;
};
```

`MIRO_ENUM_AS_INTEGER` is the same thing as a one-liner, at global scope after the enum is declared:

```cpp
MIRO_ENUM_AS_INTEGER(Discord::ChannelType)
```

Either spelling applies everywhere the type appears — on its own, as a `std::vector` element, inside a `std::optional`, as a map value:

```cpp
struct Channel
{
    Discord::ChannelType type = Discord::ChannelType::dm;

    MIRO_REFLECT(type)
};

Miro::toJSONString(Channel {});   // {"type":1}
```

Loading doesn't change: a number and an enumerator name are both accepted whichever way the type saves, so an API that sends one spelling and documents the other still loads. Enums without the trait keep saving as names.

The export formats follow the trait as well. An integer-format enum becomes `{"type": "integer", "enum": [0, 1, 2]}` in JSON Schema — with the names kept alongside under `x-enumNames` — a real `enum ChannelType { guildText = 0, ... }` in the generated TypeScript module, and a union of numeric literals in the Zod one.
### Absent keys

`std::optional<T>` covers one axis: the value may be *null*. Some APIs need the other one — the key may not be in the document at all. In a REST `PATCH` body an absent field means "leave this as it is" while a null field means "clear it"; Discord's type notation spells the two `field?` and `?type`, and they compose as `field?: ?type`.

`Miro::Omittable<T>` is that second axis. In C++ it behaves like an optional (`has_value()`, `operator*`, `operator->`, `explicit operator bool`, `reset()`, `emplace()`, comparison, implicit construction from a `T`). On the wire:

- **Save** — disengaged writes no key; the parent object simply has no such member. Engaged reflects the inner `T` into the key as usual.
- **Load** — a missing key resets it to disengaged. This is the one place where absence means something rather than being ignored. Any key that *is* present — `null` included — engages it and loads the inner `T`.

Wrapping an optional therefore gives the full three-state model of absent / null / value:

```cpp
struct ChannelPatch
{
    Miro::Omittable<std::string> name;                    // absent | value
    Miro::Omittable<std::optional<std::string>> topic;    // absent | null | value

    MIRO_REFLECT(name, topic)
};

auto patch = ChannelPatch {};
patch.topic = std::optional<std::string> {};   // engaged, but empty

Miro::toJSONString(patch);   // {"topic":null} — `name` is not there at all
```

An `Omittable<T>` field works anywhere a field can appear: at the top level, inside a nested struct, inside structs held in a vector, and as a `std::map` value (a disengaged value drops its entry). Elsewhere there is no "absent" to express — an `Omittable` used as an array element saves as `null`, since a JSON array cannot have a hole in it.

In XML, absent means no attribute and no child element. XML has no null of its own, so `Omittable<std::optional<T>>` is only two-state there.

For the export formats an omittable member is an optional property: it is left out of JSON Schema's `required`, and rendered `key?: T` in TypeScript (`key?: T | null` when it also wraps an optional). The generated `cpp-miro` header spells it `Miro::Omittable<T>`; the dependency-free `cpp` header falls back to `std::optional<T>`.

### Reflection macros

Four macros cover the common cases. Pick based on two axes: does your type support adding a member function (intrusive vs. non-intrusive), and do you want the JSON key to match the C++ identifier or to be an arbitrary string?

| | JSON key = field name | JSON key is an explicit string |
| --- | --- | --- |
| **Intrusive** (inside your struct) | `MIRO_REFLECT` | `MIRO_REFLECT_MEMBERS` |
| **Non-intrusive** (for types you don't own) | `MIRO_REFLECT_EXTERNAL` | `MIRO_REFLECT_EXTERNAL_MEMBERS` |

#### `MIRO_REFLECT` — intrusive, field name as key

List the fields inside the struct; each becomes `ref["field"](field)`:

```cpp
struct Settings
{
    std::string name;
    int count = 0;
    std::vector<std::string> tags;

    MIRO_REFLECT(name, count, tags)
};
```

Equivalent to the hand-written `reflect()` method above.

#### `MIRO_REFLECT_MEMBERS` — intrusive, explicit JSON keys

Takes `(field, "key")` pairs when the JSON key needs to differ from the C++ identifier (for example, keys with spaces or hyphens, or keys that are C++ reserved words):

```cpp
struct Product
{
    std::string name;
    double unitPrice = 0.0;

    MIRO_REFLECT_MEMBERS(name, "Full Name", unitPrice, "Unit Price")
};
```

#### `MIRO_REFLECT_EXTERNAL` — non-intrusive, field name as key

For third-party types you can't modify. Place at **file / global scope**, after the type is fully declared, with the fully-qualified type name:

```cpp
namespace ThirdParty { struct Point { int x; int y; }; }

MIRO_REFLECT_EXTERNAL(ThirdParty::Point, x, y)
```

This defines a free-function `reflect` overload in namespace `Miro`, so the type composes with the rest of the reflection layer (nesting, `std::vector`, `std::map`, etc.).

#### `MIRO_REFLECT_EXTERNAL_MEMBERS` — non-intrusive, explicit JSON keys

The external-type variant with custom key strings:

```cpp
MIRO_REFLECT_EXTERNAL_MEMBERS(ThirdParty::Point, x, "X Coord", y, "Y Coord")
```

#### `MIRO_FIELDS` — inside a hand-written `reflect()` body

The four macros above each generate a complete `reflect()` method. When you need custom logic alongside the field list — running a broadcaster, branching on `ref.isLoading()`, reading a version field before the rest — write `reflect()` by hand and drop `MIRO_FIELDS(ref, ...)` in to list the routine fields without repeating each name as a string:

```cpp
struct Settings
{
    void reflect(Miro::Reflector& ref)
    {
        MIRO_FIELDS(ref, name, count)

        if (ref.isLoading())
            onLoaded.trigger();
    }

    std::string name;
    int count = 0;
    Broadcaster onLoaded;
};
```

### Raw JSON fields

A field of type `Miro::JSON` (or `Miro::Json::Any`) reflects as-is: whatever it holds is written out unchanged, and on load the slot's tree is copied straight into it. Reach for it when a payload's type only becomes known once another field has been read:

```cpp
struct Frame
{
    int op = 0;
    Miro::JSON d;      // shape depends on op
    std::string t;

    MIRO_REFLECT(op, d, t)
};

auto frame = Miro::createFromJSONString<Frame>(text);

if (frame.op == 10)
{
    auto hello = Miro::createFromJSON<Hello>(frame.d);
}
```

Every JSON kind survives the round trip, including the difference between an empty `{}` and an empty `[]`. A missing key leaves the field at its previous value, as with any other type, and an explicit `null` sets it to null. Raw values nest inside the other built-ins too — `std::vector<Miro::JSON>`, `std::map<std::string, Miro::JSON>` and `std::optional<Miro::JSON>` all work.

The exporters describe a raw field as "anything": `{}` in JSON Schema, `unknown` in TypeScript, `z.unknown()` in Zod.

`toXML` / `fromXML` write a raw value the way a typed field of the same shape would be written — primitives as attributes, nested values as elements, arrays as repeated siblings. XML records no types, though, so reading one back yields a tree whose leaves are all strings, and a one-element array is indistinguishable from a single value.
### Tagged unions

A field whose type is decided at runtime — one of several alternatives — is
reflected as a *tagged union*. Miro supports both wire conventions.

**Externally tagged** (`Miro::reflectPolymorphic`, `Miro::Polymorphic`) wraps the
alternative in a one-key object naming it: `{"Circle": {"radius": 1}}`. A bare
`std::variant<Ts...>` reflects this way out of the box, with each alternative's
short C++ type name as the key.

**Internally tagged** (`Miro::reflectTagged`) is what Discord and most JSON APIs
use: the discriminator is an ordinary field of the object and the active
alternative's own fields sit beside it.

```json
{"type": 2, "style": 1, "label": "Click"}
{"type": 3, "customId": "pick", "options": ["a", "b"]}
```

Give each alternative a `static constexpr auto miroTag` and hold it in a
`Miro::TaggedVariant<"key", Ts...>` — a `std::variant<Ts...>` that knows how to
read and write its own discriminator:

```cpp
struct Button
{
    static constexpr auto miroTag = 2;

    int style = 0;
    std::string label;

    MIRO_REFLECT(style, label)
};

struct SelectMenu
{
    static constexpr auto miroTag = 3;

    std::string customId;
    std::vector<std::string> options;

    MIRO_REFLECT(customId, options)
};

using Component = Miro::TaggedVariant<"type", Button, SelectMenu>;

struct ActionRow
{
    std::vector<Component> components;

    MIRO_REFLECT(components)
};
```

`Component` is a plain value type — copyable, so it drops into a `std::vector`
like any other field. Reach for `Miro::Tagged<"type", Base, Derived...>` instead
when the alternatives share a base class and you want `OwningPointer<Base>`
storage; it is the internally tagged sibling of `Miro::Polymorphic`.

A tag may be an `int`, a string, or an enum. Enum tags go through the normal
enum path, so they save as the enumerator's name and load from either the name
or its numeric value. All alternatives of one union must tag with the same type.

When the tags don't belong on the alternatives — third-party types, or values
that aren't constant expressions — call `reflectTagged` directly and register
each alternative in the callback:

```cpp
struct Interaction
{
    std::string id;
    std::variant<Button, SelectMenu> data;

    void reflect(Miro::Reflector& ref)
    {
        ref["id"](id);

        Miro::reflectTagged(ref,
                            "type",
                            data,
                            [](auto& d)
                            {
                                d.template alt<Button>(2);
                                d.template alt<SelectMenu>(3);
                            });
    }
};
```

As shown, plain keys may sit beside the discriminator — they are written to the
same object and belong to every alternative.

Notes on the semantics:

- The tag key does **not** have to be a field of the alternative structs. If an
  alternative declares it anyway, the registered tag still wins on save (it is
  written after the body) and the field is populated from the wire on load.
- An unrecognised tag on load leaves the value untouched, like every other Miro
  load path. `Miro::TaggedDispatcher::handled()` reports whether an alternative
  matched, for reflect bodies that want to know.
- Alternatives must be object-shaped: they share the slot with the tag.
- XML follows the reflector's usual rules — the tag is a primitive, so it lands
  as an attribute on the same element as the alternative's own fields.
- The exporters describe an internally tagged union as a discriminated union:
  TypeScript emits `({ type: 2 } & Button) | ({ type: 3 } & SelectMenu)`, Zod a
  `z.union` of `z.literal` intersections, and JSON Schema a `oneOf` of `const`
  tags. Externally tagged unions are still rejected by the schema walkers.

## The API reflection layer

Beyond serializing types, Miro can describe an entire API surface — its **commands** (request/response methods) and **events** (typed push notifications) — via a `reflect(Miro::ApiReflector&)` method on an API class. The same declaration both binds the API to a runtime `Miro::Bridge` and feeds codegen for typed TypeScript clients, server-side handlers, event subscriptions, and matching C++ headers.

```cpp
#include <Miro/Miro.h>

class Todos
{
public:
    TodoState getTodos() const;
    void addTodo(const AddRequest& req);
    Miro::Event<TodoState> changes;

    MIRO_REFLECT_API(getTodos, addTodo, changes)
};
```

`MIRO_REFLECT_API` classifies each listed member at compile time:

- **Method** → registered as a command. The request type, response type, and shape (`Res(Req)` / `Res()` / `void(Req)` / `void()`) are inferred from the signature.
- **`Event<T>` / `RefEvent<T>` data member** → registered as an event with payload type `T`.
- **Data member of a type that itself has `reflect(ApiReflector&)`** → recursively walked as a sub-API, prefixed by the field identifier on the wire.

Each command's request/response payload and each event's payload type must already have data-reflection (one of the `MIRO_REFLECT*` macros or a hand-written `reflect()`).

### Sub-APIs and prefixed naming

A reflect body can defer to a member API; every command and event the sub declares is namespaced under the field identifier:

```cpp
class FilesSubApi
{
public:
    FileList list() const;
    Miro::Event<FileList> changed;

    MIRO_REFLECT_API(list, changed)
};

class App
{
public:
    FilesSubApi files;
    UsersSubApi users;

    MIRO_REFLECT_API(files, users)
};
```

Wire names become `files.list`, `files.changed`, `users.<name>`, and so on. Prefixes accumulate when sub-APIs nest further (`outer.inner.deepCmd`).

For sub-APIs you don't own (third-party types), define a free `reflect(ApiReflector&, T&)` overload — picked up via ADL when a parent reflects the field:

```cpp
namespace Miro {
inline void reflect(ApiReflector& r, ThirdParty::Service& obj)
{
    r.api<&ThirdParty::Service::query>(obj);
}
}
```

### Free functions and lambdas

When a command isn't a method on the API class, hand-write the reflect body and reach for the per-callable overloads:

```cpp
ARRes pure(const ARReq& req);  // free function

class App
{
public:
    Miro::Event<ARRes> beat;

    void reflect(Miro::ApiReflector& r)
    {
        r.api<&App::beat>(*this);

        // Free function — name auto-derived from the pointer
        r.command<&pure>();

        // Lambda — name supplied explicitly (no source-derived name)
        r.command(
            [](const ARReq& req) -> ARRes { return {"lambda:" + req.text}; },
            "lambdaCmd");
    }
};
```

`r.command(callable, name)` accepts capturing lambdas, captureless lambdas, free functions, and `std::function` — the request/response shape is recovered from the callable's `operator()`.

### Binding to a Bridge

`Miro::Bridge` is a transport-agnostic command + event hub. `bridge.use(api)` walks `api.reflect(...)` once, installing each command on the bridge's command table and a listener on each event:

```cpp
auto bridge = Miro::Bridge {};
bridge.use(api);

// Request/response dispatch
auto response = bridge.dispatch("addTodo", payloadJson);

// Publish-side emit — bridge.onEmit fires under the wire name "changes"
api.changes.publish({/* ... */});
```

`bridge.onEmit` is a broadcaster; subscribe a transport listener to it and forward the (name, payload) pair to your wire of choice. The same API class drives both ends — there is no separate "schema" file.

## Exporting to other languages

Once an API class is declared, Miro can emit matching definitions for other languages and tools. The supported formats are:

| Format       | Output extension | What it produces                                                |
| ------------ | ---------------- | --------------------------------------------------------------- |
| `ts`         | `.ts`            | TypeScript interfaces for every reflected payload               |
| `zod`        | `.zod.ts`        | Zod runtime validators (TypeScript)                             |
| `backend`    | `.backend.ts`    | Typed client — `makeBackend(invoke)` factory, one fn per command |
| `ts-server`  | `.handlers.ts`   | Server-side `Handlers` interface + `dispatch(handlers, …)`      |
| `bridge`     | `.bridge.ts`     | Transport-agnostic bridge runtime (Transport interface, glue)   |
| `events`     | `.events.ts`     | Typed `Events` map + `EventBus { subscribe<K extends … > … }`   |
| `jsonschema` | `.schema.json`   | JSON Schema (Draft 2020-12)                                     |
| `cpp`        | `.types.h`       | Plain C++ structs (no Miro dependency)                          |
| `cpp-miro`   | `.miro.h`        | C++ structs with `MIRO_REFLECT(...)` baked in                   |
| `cpp-client` | `.client.h`      | Typed C++ client header                                         |

### Setting up an export target

```cmake
miro_export(MySchema
    API_HEADER Api/Todos.h               # header(s) declaring the API class(es)
    API        Api::Todos                # fully-qualified API class name(s)
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
    FORMATS    ts backend ts-server events  # optional; defaults to all
)
```

`miro_export()` is registered by Miro's top-level `CMakeLists.txt`, so it is available anywhere downstream of `FetchContent_MakeAvailable(Miro)` — no extra `include()` needed.

Arguments:

- **`NAME`** (positional, required) — name of the schema target. Also the default output basename. An `INTERFACE` library with this name carries the generated header include directory.
- **`API_HEADER`** (required) — header file(s) declaring the API class(es). The generated stub `#include`s each one verbatim before calling into Miro's codegen entry point.
- **`API`** (required) — fully-qualified API class name(s). Each must have a `void reflect(Miro::ApiReflector&)` method.
- **`OUTPUT_DIR`** — directory the generated files are written to. Omit to declare the schema without an initial emit; call `miro_export_emit(NAME ...)` afterwards for one or more emits.
- **`OUTPUT_NAME`** — output filename stem (defaults to `NAME`).
- **`FORMATS`** — subset of the table above (defaults to all).

For additional emits (e.g. the TypeScript bundle into your web app's source tree *and* `cpp-client` into your C++ tree), call `miro_export_emit(NAME OUTPUT_DIR ... FORMATS ...)` after `miro_export(...)`.

### What happens at build time

Each `miro_export_emit(...)` attaches a `POST_BUILD` step to the schema's codegen executable. When the executable is rebuilt — i.e. whenever the API class or any reflected payload changes — every emit reruns and writes one file per requested format to `OUTPUT_DIR`, named `<OUTPUT_NAME>.<extension>`.

Consumers link the `${NAME}` INTERFACE library (which carries the include directory) and add a dependency on `${NAME}_Codegen` so the generated files exist before the consumer compiles:

```cmake
target_link_libraries(MyApp PRIVATE MySchema)
add_dependencies(MyApp MySchema_Codegen)
```

When cross-compiling, `miro_export()` creates the INTERFACE library but skips the codegen executable (a foreign-arch binary can't run on the build host). Consumers continue to consume committed generated files.

## The `Unicode` layer

C++ has no way to ask a code point for its Unicode general category, so anything doing text layout, IME handling, or a `\p{L}` / `\p{N}` style pre-tokenizer ends up generating a private table. `<Miro/Unicode.h>` answers it once, with all 30 categories rather than a two-class approximation.

```cpp
#include <Miro/Unicode.h>

using namespace Miro::Unicode;

generalCategory(U'A');      // GeneralCategory::UppercaseLetter
shortName(generalCategory(U'7'));  // "Nd"

isLetter(0x4F60);  // true  — Lo
isNumber(0x2160);  // true  — Nl (Roman numeral one)
isSymbol(0x1F600); // true  — So
```

`isLetter`, `isMark`, `isNumber`, `isPunctuation`, `isSymbol`, `isSeparator` and `isOther` are the `\p{..}` major classes. `isWhitespace` is the `White_Space` property — what `\s` means in a Unicode-aware regex — which is not the same set as `Z*`: it includes `U+0009..U+000D` and `U+0085`, and excludes `U+200B`.

UTF-8 conversion comes in the same header. `decodeUtf8` reports invalid, truncated, overlong, surrogate-encoded and out-of-range sequences by returning the lead byte with `byteLength == 1` and `valid == false`, so a caller slicing input can pass the byte through verbatim instead of losing it:

```cpp
auto text = std::string_view {"h\xC3\xA9llo"};

for (auto position = std::size_t {0}; position < text.size();)
{
    auto decoded = decodeUtf8(text, position);
    position += (std::size_t) decoded.byteLength;
}

auto out = std::string {};
appendUtf8(out, 0x1F600);
```

The category table is generated from Python's `unicodedata` by `Tools/GenerateUnicodeTable.py`. It is a sorted array of run starts, each packing `(codePoint << 5) | category` into one `std::uint32_t`; a lookup is a single binary search. Regenerate it rather than editing it by hand:

```bash
python3 Tools/GenerateUnicodeTable.py
```

## License

See `LICENSE.txt`.
