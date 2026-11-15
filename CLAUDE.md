# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Miro is a lightweight C++20 JSON library. It provides two layers:

- `Miro::Json` — a `std::variant`-based `Value` type supporting null, bool, `std::int64_t`, double, string, Array (`std::vector<Value>`), and Object (`std::map<std::string, Value>`), plus `parse()` and `print()` (with optional indent). The two numeric alternatives are one JSON number: `isNumber()` and `asNumber()` answer for both, `isInteger()` / `asInteger()` for the exact one, and integers past 2^53 survive the parser, the printer and the reflection layer alike.
- `Miro` — a reflection layer (`Reflector`, `toJSON` / `fromJSON`, `toJSONString` / `fromJSONString`, `createFromJSON[String]`) that serializes user types via an intrusive `reflect(Miro::Reflector&)` method. Built-in support for primitives (`bool`, `int`, `double`, `std::string`), `std::vector<T>`, `std::array<T, N>`, `std::map<std::string, V>`, `std::optional<T>` (nullable), `Miro::Omittable<T>` (the key may be absent), enums, and `Miro::JSON` / `Miro::Json::Any` (a raw JSON value carried through verbatim).

## Build Commands

```bash
# Configure and build
cmake -B build
cmake --build build --config Release

# Run tests
ctest --test-dir build --config Release --output-on-failure

# Run a single test (by name regex)
ctest --test-dir build --config Release -R "TestName"
```

**Always configure with `-DMIRO_UNITY_BUILD=OFF`** so `compile_commands.json` has a per-file entry for each source and the LSP can resolve symbols. `MIRO_UNITY_BUILD` is ON by default (CMake's `UNITY_BUILD` on the `Miro` target) for faster CI/release builds, but it collapses per-file compile commands and breaks LSP navigation.

## Architecture

Each implementation `.cpp` file under `Lib/Miro/` is a separate translation unit, listed explicitly in `Lib/CMakeLists.txt`.

Public surface — the entry headers at `Lib/Miro/*.h`. Each is self-contained (enforced by the compile-check TUs in `Tests/PublicHeaders/`):
- `Miro/Miro.h` — umbrella, includes all entry headers below. Kept for backwards compatibility.
- `Miro/Json.h` — raw JSON layer (`Miro::Json::Value`, `parse()`, `print()`), no reflection.
- `Miro/Reflect.h` — reflection layer + JSON serialization (`MIRO_REFLECT`, `Reflector`, `toJSON` / `fromJSON`).
- `Miro/Xml.h` — XML value layer + XML serialization (`toXML` / `fromXML`).
- `Miro/Bridge.h` — runtime command/event bridge (`Bridge`, `ApiReflector`, `Event`, `CommandTable`).
- `Miro/Codegen.h` — type-export / codegen toolchain (`DescribeReflector`, `TypeTree`, TypeScript / schema / C++ emitters, `codegenMain()`).
- `Miro/OpenAI.h` — OpenAI Responses-API function tools over a reflected API (`OpenAI::ToolSet`, `FunctionCall`, `FunctionCallOutput`, `FunctionTool`).
- `Miro/Unicode.h` — Unicode character properties: `generalCategory()` (all 30 categories), the `\p{..}` class predicates, the `White_Space` property, and UTF-8 `decodeUtf8` / `appendUtf8`. The category table is generated from Python's `unicodedata` by `Tools/GenerateUnicodeTable.py` into `Unicode/GeneralCategoryTable.h` — regenerate it, never hand-edit it.

Headers in subdirectories (`Lib/Miro/Reflection/`, `Lib/Miro/JSON/`, `Lib/Miro/Bridge/`, ...) are implementation details — user code includes only the entry headers. Layering notes:
- `Reflection/Reflector.h` (the abstract `Reflector` base) is format-agnostic — it must not include the JSON layer. Neither does `Reflection/ReflectDispatch.h`: raw-JSON classification lives behind the `Detail::IsRawJson` trait, specialized in `Reflection/ReflectJson.h`.
- `JSON/Number.h` is the only place the JSON layer calls `strtod` / `snprintf`. Both honour `LC_NUMERIC`, and `strtod` has no length bound, so `parseDouble` works from a NUL-terminated copy and translates the decimal point, and `appendShortestDouble` normalises it back to `.`. `std::from_chars` / `std::to_chars` for doubles are off limits: libc++ marks them unavailable below macOS 13.3 and CI targets 10.13.
- `Reflection/Serialize.h` holds the JSON helpers only; the XML counterparts live in `Reflection/SerializeXml.h` so the bridge/JSON path never drags in XML.
- `Shape::Raw` is the slot shape for a raw `Miro::JSON` field — the structure comes from the value, not the C++ type. Every `Reflector` that switches on `Options::shape` has to answer for it, and `TypeTree::TypeNode::Shape::Any` is its schema-mode counterpart (`{}` / `unknown`).

Per-type customization points are struct templates in namespace `Miro` that a user specializes, not registries — `EnumRange<E>` (probed enumerator window) and `EnumFormat<E>` (`integer = true` makes the enum save as its underlying value instead of its name; `MIRO_ENUM_AS_INTEGER(Type)` is the one-line spelling). Schema mode announces the two enum flavours through separate `Reflector` hooks, `visitEnum(TypeId, names)` and `visitIntegerEnum(TypeId, entries)`, so the TypeTree keeps the numbers a numeric enum needs.
- `Reflection/PackedStruct.h`: a struct is packable when every reflected field is a `PackedArrayRef` number type or a packable struct, and it is standard-layout and trivially copyable. `MIRO_REFLECT` / `MIRO_REFLECT_MEMBERS` emit `describeMiroFields<Self>()` (field types, names, `offsetof` offsets) beside `reflect()`, so this is decided at compile time; a hand-written `reflect()` opts in with `MIRO_PACKED(Type, member, "key", ...)` at global scope, which specializes `PackedFields<T>`. `reflectElements` offers a `std::vector` / `std::array` of packable structs to `Reflector::visitPackedStructs(PackedStructArrayRef)` (bytes, stride, flattened fields with key paths in reflect order); the default declines, so JSON and XML still write objects.
- Tagged unions come in two flavours, one header each, sharing the `Detail::PolymorphicAccess` storage adapter (`std::variant<Ts...>`, `OwningPointer<Base>`, or a user specialization): `Reflection/ReflectPolymorphic.h` is externally tagged (`{"Circle": {...}}`, `reflectPolymorphic` / `Miro::Polymorphic`), `Reflection/ReflectTagged.h` is internally tagged (`{"type": 2, ...}`, `reflectTagged` / `Miro::Tagged` / `Miro::TaggedVariant`).
- Both dispatch through the abstract `Reflector`, which carries one hook per flavour: `requirePolymorphicSupport()` (external — the default throws, so schema-mode walkers reject it) and `beginTaggedAlternative()` (internal — the default throws, but `TypeTree::TypeReflector` overrides it and builds a `TypeNode::Shape::Union` node, which the TypeScript / Zod / JSON-Schema renderers emit as a discriminated union).
- `Reflection/ReflectMacro.h`: every body-generating macro (`MIRO_REFLECT`, `_MEMBERS`, `_POLY`, `_API`, and the `_EXTERNAL` forms) emits `reflect()` as a `std::derived_from<Reflector>`-constrained template, not a plain inline function, so the field dispatch chain is only instantiated in TUs that actually serialize the type (measured: a 30-struct model header costs an including TU ~0.4s eagerly vs ~0.1s lazily). Keep it that way. The two places a template can't go — local classes and virtual overrides — use a hand-written `void reflect(Miro::Reflector& ref) { MIRO_FIELDS(ref, ...); }`.
- `Reflection/Omittable.h` defines `Miro::Omittable<T>` — "this key may be absent", the counterpart to `std::optional<T>`'s "this key is null". The mechanism lives in the parent rather than the value: `childOptionsFor<Omittable<T>>` sets `Options::omittable`, a saving `atKey` then stages the child in a scratch slot instead of creating the key, and the key is created only when the `Omittable` dispatcher calls `Reflector::markPresent()` on an engaged value — at which point the child retargets onto the real slot and everything it writes lands there. Nothing is deferred to a destructor, and every non-omittable slot keeps the eager "shape committed at construction" contract. `Reflector::markPresent()` defaults to a no-op, so a reflector that doesn't implement staging just writes the key as before.
- On load, an `Omittable<T>` reads a `null` key as absent when `T` can't hold a null (not `std::optional`, not an owning pointer, not raw JSON) — strict schemas require every key and send `null` for "left out". `Omittable<std::optional<T>>` still reads `null` as engaged-but-empty.
- Descriptions: `ref["city"].doc("...")(city)` sets `Options::description` (a `string_view`, cleared per child by `childOptionsFor`), which `TypeTree` copies onto the slot's `TypeNode::description`. It belongs to the slot, not the type: renderers print it at the use site, never on a hoisted `$defs` / interface body. Data reflectors ignore it. Commands take a trailing `Miro::Doc {"..."}` on every `ApiReflector::command` overload; it reaches `DescribeReflector::CommandRecord::description` and `CommandEntry::description`, and `BindReflector` ignores it.
- `Schema/Schema.h`: `formatJsonSchema` takes a `Schema::Dialect` — `standard()` (the default, historical output) or `openAiStrict()` (closed objects, every key required with optionals as nullable type unions, unions as an `anyOf` of flattened closed arms, no `x-` keywords, a described non-nullable field of a named type inlined because OpenAI allows nothing beside a `$ref`; a map or raw JSON slot throws `std::invalid_argument`).
- `DescribeReflector::CommandRecord::reqRoot` / `resRoot` index the command's trees in `typeRoots`.
- `OpenAI/OpenAI.h`: `ToolSet` turns each command into a Responses-API function tool — `.` in a command name becomes `__`, `parameters` is the request's strict schema with the root definition inlined and the other `$defs` kept beside it — and `invoke` / `invokeAsync` route a `FunctionCall` through a `Bridge`, never throwing (errors come back as `{"error": ...}` output). The `openai-tools` codegen format emits the same array.

CMake target is `Miro` (static library). To add a new source file, add it to the `add_library(Miro ...)` list in `Lib/CMakeLists.txt`. When `MIRO_UNITY_BUILD=ON` (the default), CMake batches those sources into a unity TU via the `UNITY_BUILD` target property.

Tests use the [NanoTest](https://github.com/eyalamirmusic/NanoTest) framework, fetched automatically via CMake FetchContent. Test target is `MiroTests`. Benchmarks live in `Tests/Benchmark/` and compare against nlohmann/json. Both NanoTest and nlohmann/json are fetched in `Tests/CMakeLists.txt` (and `Tests/Benchmark/CMakeLists.txt` respectively); the root `CMakeLists.txt` only does `enable_testing()` + `add_subdirectory(Tests)` when the project is top-level or `MIRO_BUILD_TESTS=ON`.

## Code Style

- use auto for variables whenever possible
- use modern RAII style
- use explicit return types (not auto) for functions
- Allman brace style, 4-space indentation, 85-column limit, left-aligned pointers
- Enforced via `.clang-format` and `.clang-tidy` in repo root
- Use clang-format after every change in a source file or header file
- struct/class members go on last, below methods
- do not use m_ or _ prefixes. Instead use xToUse for input variables
