// Tests for raw-JSON reflection — a Miro::JSON (or Json::Any) field
// carried through a reflect() body verbatim, for envelopes whose
// payload type is only known once another field has been read.

#include "TestHelpers.h"
#include "TestTypes.h"

#include <Miro/Cpp/Cpp.h>
#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

struct RawHolder
{
    JSON value;

    MIRO_REFLECT(value)
};

// A Discord-shaped gateway frame: what "d" holds depends on "op"/"t",
// so the client keeps it raw and decodes it again once it knows.
struct Frame
{
    int op = 0;
    JSON d;
    std::optional<int> s;
    std::string t;

    MIRO_REFLECT(op, d, s, t)
};

struct AnyFrame
{
    int op = 0;
    Json::Any d;

    MIRO_REFLECT(op, d)
};

// The payload "d" carries for op == 10.
struct HelloPayload
{
    int heartbeatInterval = 0;
    std::string sessionId;

    MIRO_REFLECT(heartbeatInterval, sessionId)
};

struct RawContainers
{
    std::vector<JSON> items;
    std::map<std::string, JSON> byName;

    MIRO_REFLECT(items, byName)
};

struct OptionalRaw
{
    std::optional<JSON> value;

    MIRO_REFLECT(value)
};

// The Discord shape for a field that may be missing, and when present is
// whatever the server chose to send - an audit-log change's new_value.
struct OmittableRaw
{
    Omittable<JSON> value;

    MIRO_REFLECT(value)
};

// Round-trips `text` (any JSON value) through a raw field and returns
// what the field ended up holding.
JSON loadRaw(std::string_view text)
{
    auto json = std::string {"{\"value\": "} + std::string {text} + "}";
    return createFromJSONString<RawHolder>(json).value;
}

// Saves a raw field holding `value` and returns the printed field.
std::string saveRaw(const JSON& value)
{
    return Json::print(toJSON(RawHolder {value})["value"]);
}

} // namespace

// --- Save: every kind is written out as itself ---

auto rawJsonSaveNull =
    test("Raw JSON: save null") = [] { check(saveRaw(JSON {}) == "null"); };

auto rawJsonSaveBool = test("Raw JSON: save bool") = []
{
    check(saveRaw(JSON {true}) == "true");
    check(saveRaw(JSON {false}) == "false");
};

auto rawJsonSaveNumber = test("Raw JSON: save number") = []
{
    check(saveRaw(JSON {42}) == "42");
    check(saveRaw(JSON {1.5}) == "1.5");
};

auto rawJsonSaveString =
    test("Raw JSON: save string") = [] { check(saveRaw(JSON {"hi"}) == "\"hi\""); };

auto rawJsonSaveArray = test("Raw JSON: save array") = []
{
    check(saveRaw(Json::parse(R"([1,"two",false,null])"))
          == R"([1,"two",false,null])");
};

auto rawJsonSaveObject = test("Raw JSON: save object") = []
{ check(saveRaw(Json::parse(R"({"a":1,"b":"x"})")) == R"({"a":1,"b":"x"})"); };

auto rawJsonSaveNested = test("Raw JSON: save nested containers") = []
{
    constexpr auto text = R"({"a":[{"b":[1,2]},{"c":{"d":true}}]})";

    check(saveRaw(Json::parse(text)) == text);
};

auto rawJsonSaveEmptyObject = test("Raw JSON: an empty object stays an object") = []
{ check(saveRaw(JSON {Json::Object {}}) == "{}"); };

auto rawJsonSaveEmptyArray = test("Raw JSON: an empty array stays an array") = []
{ check(saveRaw(JSON {Json::Array {}}) == "[]"); };

auto rawJsonSaveNestedEmpties =
    test("Raw JSON: nested empty containers keep their kind") = []
{
    constexpr auto text = R"({"a":{},"b":[],"c":[{}],"d":[[]]})";

    check(saveRaw(Json::parse(text)) == text);
};

auto rawJsonSaveLeavesSourceAlone =
    test("Raw JSON: saving does not touch the value") = []
{
    auto holder = RawHolder {Json::parse(R"({"a":[1,2]})")};
    auto before = holder.value;
    auto saved = toJSON(holder);

    check(saved["value"] == before);
    check(holder.value == before);
};

// --- Load: the slot's tree is copied in as-is ---

auto rawJsonLoadNull =
    test("Raw JSON: load null") = [] { check(loadRaw("null").isNull()); };

auto rawJsonLoadBool = test("Raw JSON: load bool") = []
{
    check(loadRaw("true").asBool() == true);
    check(loadRaw("false").asBool() == false);
};

auto rawJsonLoadNumber = test("Raw JSON: load number") = []
{
    check(loadRaw("42").asNumber() == 42.0);
    check(loadRaw("-1.25").asNumber() == -1.25);
};

auto rawJsonLoadString = test("Raw JSON: load string") = []
{ check(loadRaw("\"hi\"").asString() == "hi"); };

auto rawJsonLoadEmptyContainers =
    test("Raw JSON: load keeps empty objects and arrays apart") = []
{
    check(loadRaw("{}").isObject());
    check(loadRaw("{}").asObject().empty());
    check(loadRaw("[]").isArray());
    check(loadRaw("[]").asArray().empty());
};

auto rawJsonLoadNested = test("Raw JSON: load copies a nested tree verbatim") = []
{
    auto original = Json::parse(R"({"a":[{"b":[1,2]},{"c":{"d":true}}],"e":null})");

    check(loadRaw(Json::print(original)) == original);
};

auto rawJsonRoundTrip = test("Raw JSON: round trip through string") = []
{
    auto original = Json::parse(R"({"kinds":[null,true,1.5,"s",[],{}]})");
    auto holder = RawHolder {original};
    auto loaded = createFromJSONString<RawHolder>(toJSONString(holder));

    check(loaded.value == original);
};

auto rawJsonAbsentKeyKeepsValue =
    test("Raw JSON: an absent key leaves the value untouched") = []
{
    auto holder = RawHolder {JSON {42}};

    fromJSONString(holder, "{}");

    check(holder.value.asNumber() == 42.0);
};

auto rawJsonExplicitNullClearsValue =
    test("Raw JSON: an explicit null overwrites the value") = []
{
    auto holder = RawHolder {JSON {42}};

    fromJSONString(holder, R"({"value":null})");

    check(holder.value.isNull());
};

auto rawJsonLoadReplacesDifferentKind =
    test("Raw JSON: loading replaces a value of another kind") = []
{
    auto holder = RawHolder {Json::parse(R"({"a":1})")};

    fromJSONString(holder, R"({"value":[1,2]})");

    check(holder.value.isArray());
    check(holder.value.asArray().size() == 2);
};

// --- As a field next to typed ones ---

auto rawJsonAlongsideTypedFields =
    test("Raw JSON: a raw field sits next to typed fields") = []
{
    constexpr auto text = R"({"op":0,"d":{"guild_id":"123"},"s":42,"t":"READY"})";
    auto frame = createFromJSONString<Frame>(text);

    check(frame.op == 0);
    check(frame.s.value() == 42);
    check(frame.t == "READY");
    check(frame.d["guild_id"].asString() == "123");
    check(toJSON(frame) == Json::parse(text));
};

auto rawJsonDecodesIntoTypedStruct =
    test("Raw JSON: the kept payload decodes into a typed struct") = []
{
    auto frame = createFromJSONString<Frame>(
        R"({"op":10,"d":{"heartbeatInterval":41250,"sessionId":"abc"}})");

    check(frame.op == 10);

    auto hello = createFromJSON<HelloPayload>(frame.d);

    check(hello.heartbeatInterval == 41250);
    check(hello.sessionId == "abc");
};

auto rawJsonAnyFieldType = test("Raw JSON: Json::Any works as the field type") = []
{
    auto frame = AnyFrame {10, Json::Any {HelloPayload {41250, "abc"}}};
    auto json = toJSON(frame);

    check(json["d"]["heartbeatInterval"].asNumber() == 41250.0);

    auto loaded = createFromJSON<AnyFrame>(json);

    check(loaded.d["sessionId"].asString() == "abc");
    check(createFromJSON<HelloPayload>(loaded.d).sessionId == "abc");
};

auto rawJsonTopLevel = test("Raw JSON: Miro::JSON round-trips as the root type") = []
{
    auto original = Json::parse(R"({"a":[1,{},[]],"b":null})");

    check(toJSON(original) == original);
    check(createFromJSON<JSON>(original) == original);
    check(createFromJSONString<JSON>(R"([1,2])").asArray().size() == 2);
};

auto rawJsonInsideContainers =
    test("Raw JSON: raw values inside typed containers") = []
{
    auto original =
        RawContainers {{JSON {1}, Json::parse("{}")}, {{"k", Json::parse("[1]")}}};
    auto text = toJSONString(original);

    check(Json::parse(text)
          == Json::parse(R"({"items":[1,{}],"byName":{"k":[1]}})"));

    auto loaded = createFromJSONString<RawContainers>(text);

    check(loaded.items.size() == 2);
    check(loaded.items[1].isObject());
    check(loaded.byName.at("k").asArray().size() == 1);
};

auto rawJsonOptionalField = test("Raw JSON: optional<Miro::JSON> field") = []
{
    check(toJSONString(OptionalRaw {}) == R"({"value":null})");

    auto engaged = OptionalRaw {Json::parse(R"({"a":1})")};

    check(toJSONString(engaged) == R"({"value":{"a":1}})");

    auto loaded = createFromJSONString<OptionalRaw>(R"({"value":[1,2]})");

    check(loaded.value.has_value());
    check(loaded.value->asArray().size() == 2);
};

auto rawJsonOmittableField = test("Raw JSON: Omittable<Miro::JSON> field") = []
{
    check(toJSONString(OmittableRaw {}) == "{}");

    // Every kind of nesting: an object, an array of objects, and empties.
    // The omittable flag of the slot must not leak onto the raw children,
    // or an object child is staged as omittable and never claimed.
    auto text = std::string {R"({"value":{"a":[{"b":1},{}],"o":{"k":"v"},"e":[]}})"};
    auto engaged = createFromJSONString<OmittableRaw>(text);

    check(engaged.value.has_value());
    check(toJSON(engaged) == Json::parse(text));

    auto null = createFromJSONString<OmittableRaw>(R"({"value":null})");

    check(null.value.has_value());
    check(null.value->isNull());
    check(toJSONString(null) == R"({"value":null})");
};

// --- Through the XML reflector ---
//
// XML records no types, so a raw value survives as a tree of objects
// and string leaves: numbers and bools come back as their spelling.

auto rawJsonXmlSavesLikeTypedFields =
    test("Raw JSON: XML writes primitives as attributes") = []
{
    auto holder = RawHolder {Json::parse(R"({"a":1,"b":"x","n":{"deep":true}})")};
    auto node = toXML(holder);
    auto* raw = Xml::findChild(node, "value");

    check(raw != nullptr);
    check(*Xml::findAttribute(*raw, "a") == "1");
    check(*Xml::findAttribute(*raw, "b") == "x");
    check(*Xml::findAttribute(*Xml::findChild(*raw, "n"), "deep") == "true");
};

auto rawJsonXmlRoundTrip = test("Raw JSON: XML round trip of a simple object") = []
{
    auto holder = RawHolder {Json::parse(R"({"a":1,"b":"x","c":true})")};
    auto loaded = createFromXMLString<RawHolder>(toXMLString(holder));

    check(loaded.value.isObject());
    check(loaded.value.asObject().size() == 3);
    check(loaded.value["a"].asString() == "1");
    check(loaded.value["b"].asString() == "x");
    check(loaded.value["c"].asString() == "true");
};

auto rawJsonXmlNestedRoundTrip =
    test("Raw JSON: XML round trip keeps nested objects") = []
{
    auto holder = RawHolder {Json::parse(R"({"n":{"deep":"yes"}})")};
    auto loaded = createFromXMLString<RawHolder>(toXMLString(holder));

    check(loaded.value["n"].isObject());
    check(loaded.value["n"]["deep"].asString() == "yes");
};

auto rawJsonXmlRepeatedElements =
    test("Raw JSON: XML repeated elements load back as an array") = []
{
    auto holder = RawHolder {Json::parse(R"({"tags":["a","b"]})")};
    auto loaded = createFromXMLString<RawHolder>(toXMLString(holder));

    check(loaded.value["tags"].isArray());
    check(loaded.value["tags"].asArray().size() == 2);
    check(loaded.value["tags"][0].asString() == "a");
    check(loaded.value["tags"][1].asString() == "b");
};

auto rawJsonXmlAbsentKeepsValue =
    test("Raw JSON: XML load of an absent element keeps the value") = []
{
    auto holder = RawHolder {JSON {42}};

    fromXMLString(holder, "<RawHolder/>");

    check(holder.value.asNumber() == 42.0);
};

// --- Schema / TypeScript export ---

auto rawJsonSchemaIsAnything =
    test("Raw JSON schema: a raw field accepts anything") = []
{
    auto schema = schemaOf<Frame>();
    auto& body = schema["$defs"]["Frame"];
    auto& raw = body["properties"]["d"];

    check(raw.isObject());
    check(raw.asObject().empty());
    check(body["properties"]["op"]["type"].asString() == "integer");
};

auto rawJsonSchemaFieldIsRequired =
    test("Raw JSON schema: a raw field is still required") = []
{
    auto schema = schemaOf<Frame>();
    auto& required = schema["$defs"]["Frame"]["required"];
    auto names = std::vector<std::string> {};

    for (auto& entry: required.asArray())
        names.push_back(entry.asString());

    check(std::find(names.begin(), names.end(), "d") != names.end());
    check(std::find(names.begin(), names.end(), "s") == names.end());
};

auto rawJsonSchemaInsideContainers =
    test("Raw JSON schema: raw values inside typed containers") = []
{
    auto schema = schemaOf<RawContainers>();
    auto& body = schema["$defs"]["RawContainers"];

    check(body["properties"]["items"]["items"].asObject().empty());
    check(body["properties"]["byName"]["additionalProperties"].asObject().empty());
};

auto rawJsonTypeScriptUnknown =
    test("Raw JSON TypeScript: a raw field is unknown") = []
{
    auto out = TypeScript::toTypes<Frame>();

    check(contains(out, "d: unknown;"));
    check(contains(out, "op: number;"));
};

auto rawJsonZodUnknown = test("Raw JSON Zod: a raw field is z.unknown()") = []
{
    auto out = TypeScript::toZod<Frame>();

    check(contains(out, "d: z.unknown()"));
};

auto rawJsonCppExport = test("Raw JSON C++ export: a raw field is Miro::JSON") = []
{
    auto out = Cpp::toHeader<Frame>(Cpp::Modes::Miro);

    check(contains(out, "Miro::JSON d;"));
};

auto rawJsonTypeScriptOptional =
    test("Raw JSON TypeScript: an optional raw field is unknown | null") = []
{
    auto types = TypeScript::toTypes<OptionalRaw>();
    auto zod = TypeScript::toZod<OptionalRaw>();

    check(contains(types, "value?: unknown | null;"));
    check(contains(zod, "value: z.unknown().nullish()"));
};
