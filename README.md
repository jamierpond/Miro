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

All functionality is exposed through a single public header: `#include <Miro/Miro.h>`. The library is built as a unity TU (one `.cpp` file) — implementation details live under `Lib/Miro/Detail/` and should not be included directly.

## The `Json` layer

The core type is `Miro::Json::Value`, a variant over `Null`, `bool`, `double`, `std::string`, `Array` (`std::vector<Value>`), and `Object` (`std::map<std::string, Value>`).

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

Accessors (`asBool`, `asNumber`, `asString`, `asArray`, `asObject`) throw `std::bad_variant_access` on type mismatch. Use the `is*` predicates to check first, or use `find(object, key)` to look up an optional value by key.

`ParseError` is thrown on malformed input.

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
- Any user type with a `reflect(Reflector&)` method (nested types compose)

Convenience functions:

- `Miro::toJSON(value)` / `Miro::fromJSON(value, json)`
- `Miro::createFromJSON<T>(json)`
- `Miro::toJSONString(value, indent = 0)` / `Miro::fromJSONString(value, str)`
- `Miro::createFromJSONString<T>(str)`

### Zero-annotation reflection (C++26 / P2996)

On a P2996-capable toolchain, plain aggregates serialize with **no `reflect()` method and no macro at all**. Build with `-DMIRO_ENABLE_REFLECTION=ON` (requires the [Bloomberg clang-p2996](https://github.com/bloomberg/clang-p2996) fork, which adds `-std=c++26 -freflection -fexpansion-statements`):

```cpp
struct Address
{
    std::string city;
    int zip = 0;
};

struct Profile
{
    std::string name;
    int age = 0;
    Address home;                 // nested aggregates compose
    std::vector<std::string> tags;
    std::vector<Address> visited; // and so do containers of them
};

auto json = Miro::toJSONString(profile);              // just works
auto back = Miro::createFromJSONString<Profile>(json);
```

Each non-static data member is walked via C++26 reflection, and **the C++ field name is used verbatim as the key** — rename a field and the wire format renames with it, with no second list to keep in sync. The same single dispatch powers every backend, so the same annotation-free type also flows through `toXML`, the JSON-Schema exporter, and codegen.

This path is **opt-in and additive**:

- It applies only to plain aggregates (public data, no user-declared constructors, no virtuals, no base classes). Types with invariants keep their explicit `reflect()`.
- A hand-written `reflect()` or any `MIRO_REFLECT*` macro always takes precedence, so existing code is unchanged.
- When `MIRO_ENABLE_REFLECTION` is off (the default), the feature is fully compiled out and ordinary C++20 builds on stock compilers behave exactly as before. The macros below remain fully supported.

### Reflection macros

The macros are no longer required for plain structs on a P2996 toolchain (see above), but remain the way to reflect types non-intrusively, remap keys, or run custom logic — and they are the only path on C++20 compilers.

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

## License

See `LICENSE.txt`.
