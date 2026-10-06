// Tests for Miro::Omittable — "this key is not in the document" as
// opposed to std::optional's "this key is present and null".
//
// The save-side assertions deliberately check for the *absence of a
// member* rather than for a null value: an omitted key must leave no
// trace in the parent object at all.

#include "TestHelpers.h"
#include "TestTypes.h"

#include <NanoTest/NanoTest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

struct Patch
{
    Omittable<std::string> name;
    Omittable<int> count;

    MIRO_REFLECT(name, count)
};

struct TriState
{
    Omittable<std::optional<int>> value;

    MIRO_REFLECT(value)
};

struct OmittableStruct
{
    Omittable<Inner> inner;
    int trailing = 7;

    MIRO_REFLECT(inner, trailing)
};

struct OmittableEmptyStruct
{
    Omittable<MacroEmpty> body;

    MIRO_REFLECT(body)
};

struct OmittableVectorField
{
    Omittable<std::vector<int>> nums;

    MIRO_REFLECT(nums)
};

struct OmittableListItem
{
    int id = 0;
    Omittable<std::string> label;

    MIRO_REFLECT(id, label)
};

struct OmittableList
{
    std::vector<OmittableListItem> items;

    MIRO_REFLECT(items)
};

struct OmittableOuter
{
    Patch patch;
    int trailing = 3;

    MIRO_REFLECT(patch, trailing)
};

struct OmittableMapField
{
    std::map<std::string, Omittable<int>> entries;

    MIRO_REFLECT(entries)
};

struct OmittableArrayElements
{
    std::vector<Omittable<int>> slots;

    MIRO_REFLECT(slots)
};

bool hasKey(const JSON& json, const char* key)
{
    return json.isObject() && json.asObject().find(key) != json.asObject().end();
}

bool isRequired(const JSON& schema, const char* typeName, const char* field)
{
    auto& body = schema["$defs"][typeName].asObject();
    auto it = body.find("required");

    if (it == body.end())
        return false;

    for (auto& entry: it->second.asArray())
        if (entry.asString() == field)
            return true;

    return false;
}

} // namespace

// ---------- The value type itself ----------

auto omittableValueSemantics = test("Omittable: value semantics") = []
{
    auto empty = Omittable<int> {};
    check(!empty.has_value());
    check(!static_cast<bool>(empty));

    auto engaged = Omittable<int> {5};
    check(engaged.has_value());
    check(static_cast<bool>(engaged));
    check(*engaged == 5);

    engaged.reset();
    check(!engaged.has_value());

    check(engaged.emplace(9) == 9);
    check(*engaged == 9);

    auto structured = Omittable<Inner> {Inner {3}};
    check(structured->x == 3);
};

auto omittableComparison = test("Omittable: comparison") = []
{
    check(Omittable<int> {} == Omittable<int> {});
    check(Omittable<int> {1} == Omittable<int> {1});
    check(Omittable<int> {1} != Omittable<int> {2});
    check(Omittable<int> {} != Omittable<int> {0});

    // The implicit T -> Omittable<T> conversion works on either side.
    check(Omittable<int> {4} == 4);
    check(4 == Omittable<int> {4});
};

// ---------- Save ----------

auto omittableSaveEngagedWritesKey = test("Omittable: engaged saves the key") = []
{
    auto patch = Patch {};
    patch.name = std::string {"ada"};
    patch.count = 3;

    auto json = toJSON(patch);

    check(json["name"].asString() == "ada");
    check(json["count"].asNumber() == 3);
};

auto omittableSaveDisengagedOmitsKey =
    test("Omittable: disengaged omits the key entirely") = []
{
    auto json = toJSON(Patch {});

    check(json.isObject());
    check(json.asObject().empty());
    check(!hasKey(json, "name"));
    check(!hasKey(json, "count"));
    check(toJSONString(Patch {}) == "{}");
};

auto omittableCommitsBeforeDestruction =
    test("Omittable: the key lands before the reflector is destroyed") = []
{
    auto patch = Patch {};
    patch.name = std::string {"live"};

    auto json = JSON {};
    auto ref = JsonReflector {json, Detail::topLevelOptions<Patch>(Mode::Save)};
    Detail::reflectValue(ref, patch);

    // Nothing is deferred to ~JsonReflector — the staged key is claimed
    // the moment the engaged Omittable is reflected, so the object is
    // already complete while the reflector is alive.
    check(hasKey(json, "name"));
    check(json["name"].asString() == "live");
    check(!hasKey(json, "count"));
};

auto omittableSaveMixed =
    test("Omittable: only the engaged one of two keys is written") = []
{
    auto patch = Patch {};
    patch.count = 12;

    auto json = toJSON(patch);

    check(!hasKey(json, "name"));
    check(hasKey(json, "count"));
    check(json["count"].asNumber() == 12);

    auto other = Patch {};
    other.name = std::string {"only"};

    auto otherJson = toJSON(other);

    check(hasKey(otherJson, "name"));
    check(!hasKey(otherJson, "count"));
};

auto omittableSaveNestedStruct =
    test("Omittable: nested struct key appears only when engaged") = []
{
    auto omitted = toJSON(OmittableStruct {});

    check(!hasKey(omitted, "inner"));
    check(omitted["trailing"].asNumber() == 7);

    auto value = OmittableStruct {};
    value.inner = Inner {42};

    auto json = toJSON(value);

    check(json["inner"]["x"].asNumber() == 42);
    check(json["trailing"].asNumber() == 7);
};

auto omittableSaveEmptyEngagedObject =
    test("Omittable: engaged empty struct still writes the key") = []
{
    auto value = OmittableEmptyStruct {};
    value.body = MacroEmpty {};

    auto json = toJSON(value);

    check(hasKey(json, "body"));
    check(json["body"].isObject());
    check(json["body"].asObject().empty());

    check(!hasKey(toJSON(OmittableEmptyStruct {}), "body"));
};

auto omittableSaveVectorField = test(
    "Omittable: engaged empty vector writes [] - disengaged writes nothing") = []
{
    auto value = OmittableVectorField {};
    value.nums = std::vector<int> {};

    auto json = toJSON(value);

    check(hasKey(json, "nums"));
    check(json["nums"].isArray());
    check(json["nums"].asArray().empty());

    check(!hasKey(toJSON(OmittableVectorField {}), "nums"));
};

auto omittableSaveInsideNestedStruct =
    test("Omittable: omitted key inside a nested struct") = []
{
    auto outer = OmittableOuter {};
    outer.patch.name = std::string {"deep"};

    auto json = toJSON(outer);

    check(json["patch"]["name"].asString() == "deep");
    check(!hasKey(json["patch"], "count"));
    check(json["trailing"].asNumber() == 3);

    auto allOmitted = toJSON(OmittableOuter {});

    check(allOmitted["patch"].isObject());
    check(allOmitted["patch"].asObject().empty());
    check(allOmitted["trailing"].asNumber() == 3);
};

auto omittableSaveInsideVectorOfStructs =
    test("Omittable: omitted key inside a vector of structs") = []
{
    auto list = OmittableList {};
    list.items.push_back({1, std::string {"first"}});
    list.items.push_back({2, {}});
    list.items.push_back({3, std::string {"third"}});

    auto json = toJSON(list);
    auto& items = json["items"].asArray();

    check(items.size() == 3);
    check(items[0]["label"].asString() == "first");
    check(!hasKey(items[1], "label"));
    check(items[1]["id"].asNumber() == 2);
    check(items[2]["label"].asString() == "third");
};

auto omittableSaveAsMapValue =
    test("Omittable: disengaged map value drops its entry") = []
{
    auto value = OmittableMapField {};
    value.entries["a"] = 1;
    value.entries["b"] = Omittable<int> {};
    value.entries["c"] = 3;

    auto json = toJSON(value);

    check(json["entries"].asObject().size() == 2);
    check(json["entries"]["a"].asNumber() == 1);
    check(!hasKey(json["entries"], "b"));
    check(json["entries"]["c"].asNumber() == 3);
};

auto omittableInArrayElementIsNull =
    test("Omittable: an array element has no absent form - it saves null") = []
{
    auto value = OmittableArrayElements {};
    value.slots.push_back(Omittable<int> {});
    value.slots.push_back(Omittable<int> {5});

    auto json = toJSON(value);

    check(json["slots"].asArray().size() == 2);
    check(json["slots"][0].isNull());
    check(json["slots"][1].asNumber() == 5);
};

// ---------- Load ----------

auto omittableLoadMissingKeyResets =
    test("Omittable: a missing key loads as disengaged") = []
{
    auto patch = Patch {};
    patch.name = std::string {"stale"};
    patch.count = 99;

    fromJSONString(patch, "{}");

    check(!patch.name.has_value());
    check(!patch.count.has_value());
};

auto omittableLoadPresentEngages =
    test("Omittable: a present key loads as engaged") = []
{
    auto patch = createFromJSONString<Patch>(R"({"name": "bob", "count": 4})");

    check(patch.name.has_value());
    check(*patch.name == "bob");
    check(patch.count == 4);
};

auto omittableLoadPartial = test("Omittable: one key present, one missing") = []
{
    auto patch = createFromJSONString<Patch>(R"({"count": 8})");

    check(!patch.name.has_value());
    check(patch.count == 8);
};

auto omittableLoadNestedStruct =
    test("Omittable: nested struct loads through the key") = []
{
    auto present = createFromJSONString<OmittableStruct>(R"({"inner": {"x": 6}})");

    check(present.inner.has_value());
    check(present.inner->x == 6);

    auto missing = createFromJSONString<OmittableStruct>("{}");

    check(!missing.inner.has_value());
};

auto omittableLoadInsideVectorOfStructs =
    test("Omittable: vector of structs loads per-element absence") = []
{
    auto list = createFromJSONString<OmittableList>(
        R"({"items": [{"id": 1, "label": "a"}, {"id": 2}]})");

    check(list.items.size() == 2);
    check(list.items[0].label == std::string {"a"});
    check(!list.items[1].label.has_value());
    check(list.items[1].id == 2);
};

auto omittableRoundTrip = test("Omittable: JSON round trip preserves absence") = []
{
    auto list = OmittableList {};
    list.items.push_back({1, std::string {"kept"}});
    list.items.push_back({2, {}});

    auto restored = createFromJSONString<OmittableList>(toJSONString(list));

    check(restored.items.size() == 2);
    check(restored.items[0].label == std::string {"kept"});
    check(!restored.items[1].label.has_value());
};

// ---------- The three-state model ----------

auto omittableTriStateSave =
    test("Omittable: absent / null / value all save differently") = []
{
    auto absent = toJSON(TriState {});
    check(!hasKey(absent, "value"));

    auto nulled = TriState {};
    nulled.value = std::optional<int> {};

    auto nulledJson = toJSON(nulled);
    check(hasKey(nulledJson, "value"));
    check(nulledJson["value"].isNull());

    auto set = TriState {};
    set.value = std::optional<int> {11};

    check(toJSON(set)["value"].asNumber() == 11);
};

auto omittableTriStateLoad =
    test("Omittable: absent / null / value all load differently") = []
{
    auto absent = createFromJSONString<TriState>("{}");
    check(!absent.value.has_value());

    auto nulled = createFromJSONString<TriState>(R"({"value": null})");
    check(nulled.value.has_value());
    check(!nulled.value->has_value());

    auto set = createFromJSONString<TriState>(R"({"value": 11})");
    check(set.value.has_value());
    check(set.value->has_value());
    check(**set.value == 11);
};

auto omittableTriStateRoundTrip = test("Omittable: three-state round trip") = []
{
    auto states = std::vector<TriState> {};
    states.push_back(TriState {});
    states.push_back(TriState {std::optional<int> {}});
    states.push_back(TriState {std::optional<int> {2}});

    for (auto& state: states)
    {
        auto restored = createFromJSONString<TriState>(toJSONString(state));
        check(restored.value == state.value);
    }
};

auto omittableOptionalUnchanged =
    test("Omittable: std::optional still saves null and ignores missing keys") = []
{
    // The reason Omittable exists as its own type — none of this moved.
    auto json = toJSON(ClassWithOptional {});

    check(hasKey(json, "maybeInt"));
    check(json["maybeInt"].isNull());

    auto value = ClassWithOptional {};
    value.maybeInt = 5;
    fromJSONString(value, "{}");

    check(value.maybeInt.has_value());
    check(*value.maybeInt == 5);
};

// ---------- XML ----------

auto omittableXmlAttribute =
    test("Omittable XML: engaged writes an attribute, disengaged writes none") = []
{
    auto patch = Patch {};
    patch.count = 4;

    auto node = toXML(patch);

    check(Xml::findAttribute(node, "name") == nullptr);
    check(*Xml::findAttribute(node, "count") == "4");
};

auto omittableXmlElement = test(
    "Omittable XML: engaged writes a child element, disengaged writes none") = []
{
    auto omitted = toXML(OmittableStruct {});

    check(Xml::findChild(omitted, "inner") == nullptr);
    check(omitted.children.empty());
    check(*Xml::findAttribute(omitted, "trailing") == "7");

    auto value = OmittableStruct {};
    value.inner = Inner {8};

    auto node = toXML(value);

    check(node.children.size() == 1);
    check(*Xml::findAttribute(*Xml::findChild(node, "inner"), "x") == "8");
};

auto omittableXmlElementOrdering =
    test("Omittable XML: an omitted element doesn't disturb its siblings") = []
{
    auto outer = OmittableOuter {};
    outer.patch.name = std::string {"deep"};

    auto node = toXML(outer);

    check(node.children.size() == 1);
    check(node.children[0].name == "patch");
    check(*Xml::findAttribute(node.children[0], "name") == "deep");
    check(Xml::findAttribute(node.children[0], "count") == nullptr);
};

auto omittableXmlLoad = test("Omittable XML: absence loads as disengaged") = []
{
    auto present = createFromXMLString<Patch>(R"(<Patch name="ada" count="3"/>)");

    check(present.name == std::string {"ada"});
    check(present.count == 3);

    auto partial = createFromXMLString<Patch>(R"(<Patch count="3"/>)");

    check(!partial.name.has_value());
    check(partial.count == 3);

    auto element = createFromXMLString<OmittableStruct>(R"(<OmittableStruct/>)");

    check(!element.inner.has_value());
};

auto omittableXmlRoundTrip = test("Omittable XML: round trip preserves absence") = []
{
    auto value = OmittableOuter {};
    value.patch.count = 5;

    auto restored = createFromXMLString<OmittableOuter>(toXMLString(value));

    check(!restored.patch.name.has_value());
    check(restored.patch.count == 5);
    check(restored.trailing == 3);
};

auto omittableXmlVector =
    test("Omittable XML: an omitted vector key loads back as disengaged") = []
{
    auto restored =
        createFromXMLString<OmittableVectorField>(R"(<OmittableVectorField/>)");

    check(!restored.nums.has_value());

    auto value = OmittableVectorField {};
    value.nums = std::vector<int> {1, 2};

    auto roundTripped =
        createFromXMLString<OmittableVectorField>(toXMLString(value));

    check(roundTripped.nums.has_value());
    check(roundTripped.nums->size() == 2);
};

// ---------- Schema ----------

auto omittableSchemaNotRequired =
    test("Schema: an omittable field is not listed in 'required'") = []
{
    auto schema = schemaOf<OmittableStruct>();

    check(!isRequired(schema, "OmittableStruct", "inner"));
    check(isRequired(schema, "OmittableStruct", "trailing"));
};

auto omittableSchemaKeepsInnerShape =
    test("Schema: an omittable field keeps the inner type's shape") = []
{
    auto schema = schemaOf<Patch>();
    auto& body = schema["$defs"]["Patch"];

    check(body["properties"]["name"]["type"].asString() == "string");
    check(body["properties"]["count"]["type"].asString() == "integer");

    // Absent is not null: an Omittable<T> alone is not nullable.
    check(!hasKey(body["properties"]["name"], "nullable"));
    check(body.asObject().find("required") == body.asObject().end());
};

auto omittableSchemaNestedRef =
    test("Schema: an omittable struct field is still a $ref") = []
{
    auto schema = schemaOf<OmittableStruct>();

    check(
        schema["$defs"]["OmittableStruct"]["properties"]["inner"]["$ref"].asString()
        == "#/$defs/Inner");
};

auto omittableSchemaTriState =
    test("Schema: Omittable<optional<T>> is nullable and not required") = []
{
    auto schema = schemaOf<TriState>();
    auto& value = schema["$defs"]["TriState"]["properties"]["value"];

    check(value["type"].asString() == "integer");
    check(value["nullable"].asBool() == true);
    check(!isRequired(schema, "TriState", "value"));
};

// ---------- TypeScript ----------

auto omittableTsTypes = test("TypeScript types: omittable field uses `key?:`") = []
{
    auto out = TypeScript::toTypes<Patch>();

    check(contains(out, "name?: string;"));
    check(contains(out, "count?: number;"));
};

auto omittableTsTypesTriState =
    test("TypeScript types: Omittable<optional<T>> is `key?: T | null`") = []
{
    auto out = TypeScript::toTypes<TriState>();
    check(contains(out, "value?: number | null;"));
};

auto omittableTsTypesRequiredSibling =
    test("TypeScript types: a non-omittable sibling keeps `key:`") = []
{
    auto out = TypeScript::toTypes<OmittableStruct>();

    check(contains(out, "inner?: Inner;"));
    check(contains(out, "trailing: number;"));
};

auto omittableTsZod = test("TypeScript zod: omittable field uses .optional()") = []
{
    auto out = TypeScript::toZod<Patch>();

    check(contains(out, "name: z.string().optional()"));
    check(contains(out, "count: z.number().int().optional()"));
};

auto omittableTsZodTriState =
    test("TypeScript zod: Omittable<optional<T>> uses .nullish()") = []
{
    auto out = TypeScript::toZod<TriState>();
    check(contains(out, "value: z.number().int().nullish()"));
};

// ---------- C++ export ----------

auto omittableCppMiroHeader =
    test("C++ export: the Miro header spells omittable fields Omittable") = []
{
    auto out = Cpp::toHeader<Patch>(Cpp::Modes::Miro);

    check(contains(out, "Miro::Omittable<std::string> name;"));
    check(contains(out, "Miro::Omittable<int> count;"));
};

auto omittableCppPureHeader =
    test("C++ export: the pure header falls back to std::optional") = []
{
    auto out = Cpp::toHeader<Patch>(Cpp::Modes::PureCPP);

    check(contains(out, "std::optional<std::string> name;"));
    check(!contains(out, "Miro::Omittable"));
};

auto omittableCppTriState =
    test("C++ export: Omittable<optional<T>> keeps both layers in Miro mode") = []
{
    auto miro = Cpp::toHeader<TriState>(Cpp::Modes::Miro);
    check(contains(miro, "Miro::Omittable<std::optional<int>> value;"));

    auto pure = Cpp::toHeader<TriState>(Cpp::Modes::PureCPP);
    check(contains(pure, "std::optional<int> value;"));
};
