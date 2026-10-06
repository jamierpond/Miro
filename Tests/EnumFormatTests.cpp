// Tests for Miro::EnumFormat — the opt-in that makes an enum travel as
// its integer value instead of its enumerator name. Covers the JSON and
// XML data walks, the nested positions (vector / optional / map value)
// and everything the schema-mode walk feeds: JSON Schema, TypeScript and
// the C++ header emitter.

#include "TestHelpers.h"

#include <Miro/Miro.h>

#include <NanoTest/NanoTest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace nano;
using namespace Miro;

namespace IntEnums
{

// Shaped after Discord's `{"type": 1}` convention — the reason the trait
// exists.
enum class ChannelType : int
{
    guildText = 0,
    dm = 1,
    guildVoice = 2
};

// Negative members, to prove the numbers survive rendering as literals.
enum class Delta : int
{
    down = -1,
    none = 0,
    up = 1
};

// Left on the default (name) format — the control for every "the default
// didn't move" assertion below.
enum class Flavour
{
    sweet,
    salty
};

} // namespace IntEnums

template <>
struct Miro::EnumFormat<IntEnums::ChannelType>
{
    static constexpr bool integer = true;
};

MIRO_ENUM_AS_INTEGER(IntEnums::Delta)

namespace IntEnums
{

struct Channel
{
    ChannelType type = ChannelType::dm;
    Flavour flavour = Flavour::salty;

    MIRO_REFLECT(type, flavour)
};

struct ChannelSet
{
    std::vector<ChannelType> types;
    std::optional<ChannelType> parent;
    std::map<std::string, ChannelType> byName;

    MIRO_REFLECT(types, parent, byName)
};

struct DeltaHolder
{
    Delta delta = Delta::down;

    MIRO_REFLECT(delta)
};

} // namespace IntEnums

using namespace IntEnums;

// ---------- The trait itself ----------

auto enumFormatDefaultIsName = test("EnumFormat: the default format is by name") = []
{
    check(!EnumFormat<Flavour>::integer);
    check(EnumFormat<ChannelType>::integer);
};

auto enumFormatMacroOptsIn =
    test("EnumFormat: MIRO_ENUM_AS_INTEGER opts a type in") = []
{ check(EnumFormat<Delta>::integer); };

auto enumFormatEntriesPairNamesWithValues =
    test("EnumFormat: enumEntries pairs each name with its value") = []
{
    auto entries = enumEntries<Delta>();

    check(entries.size() == 3);
    check(entries[0].name == "down");
    check(entries[0].value == -1);
    check(entries[2].name == "up");
    check(entries[2].value == 1);
};

// ---------- JSON save ----------

auto enumFormatSavesAsNumber =
    test("EnumFormat: integer enum saves as a number") = []
{
    auto json = toJSON(Channel {});

    check(json["type"].isNumber());
    check(json["type"].asNumber() == 1.0);
};

auto enumFormatLeavesDefaultAlone =
    test("EnumFormat: a plain enum in the same struct still saves as its name") = []
{
    auto json = toJSON(Channel {});

    check(json["flavour"].isString());
    check(json["flavour"].asString() == "salty");
};

auto enumFormatSavesNegativeValue =
    test("EnumFormat: integer enum saves a negative value") = []
{
    auto json = toJSON(DeltaHolder {});

    check(json["delta"].asNumber() == -1.0);
};

auto enumFormatSavesUnknownValue =
    test("EnumFormat: integer enum with no enumerator still saves its number") = []
{
    auto val = Channel {};
    val.type = static_cast<ChannelType>(77);

    check(toJSON(val)["type"].asNumber() == 77.0);
};

// ---------- JSON load ----------

auto enumFormatLoadsFromNumber =
    test("EnumFormat: integer enum loads from a number") = []
{
    auto val = createFromJSONString<Channel>(R"({"type": 2})");
    check(val.type == ChannelType::guildVoice);
};

auto enumFormatLoadsFromName =
    test("EnumFormat: integer enum still loads from an enumerator name") = []
{
    auto val = createFromJSONString<Channel>(R"({"type": "guildText"})");
    check(val.type == ChannelType::guildText);
};

auto enumFormatUnknownNameKeepsValue =
    test("EnumFormat: an unparseable string leaves the integer enum alone") = []
{
    auto val = Channel {};
    fromJSONString(val, R"({"type": "nonsense"})");

    check(val.type == ChannelType::dm);
};

auto enumFormatRoundTrips = test("EnumFormat: integer enum round-trips") = []
{
    auto original = Channel {ChannelType::guildVoice, Flavour::sweet};
    auto loaded = createFromJSON<Channel>(toJSON(original));

    check(loaded.type == ChannelType::guildVoice);
    check(loaded.flavour == Flavour::sweet);
};

// ---------- Nested positions ----------

auto enumFormatInVector =
    test("EnumFormat: vector elements follow the integer format") = []
{
    auto val = ChannelSet {};
    val.types = {ChannelType::dm, ChannelType::guildVoice};

    auto json = toJSON(val);
    check(json["types"][0].asNumber() == 1.0);
    check(json["types"][1].asNumber() == 2.0);
};

auto enumFormatInOptional =
    test("EnumFormat: optional payload follows the integer format") = []
{
    auto val = ChannelSet {};
    val.parent = ChannelType::guildText;

    auto json = toJSON(val);
    check(json["parent"].isNumber());
    check(json["parent"].asNumber() == 0.0);
};

auto enumFormatEmptyOptionalIsNull =
    test("EnumFormat: a disengaged optional integer enum still saves null") = []
{ check(toJSON(ChannelSet {})["parent"].isNull()); };

auto enumFormatInMapValue =
    test("EnumFormat: map values follow the integer format") = []
{
    auto val = ChannelSet {};
    val.byName["lobby"] = ChannelType::guildVoice;

    check(toJSON(val)["byName"]["lobby"].asNumber() == 2.0);
};

auto enumFormatNestedRoundTrip =
    test("EnumFormat: nested integer enums round-trip") = []
{
    auto original = ChannelSet {};
    original.types = {ChannelType::guildVoice, ChannelType::guildText};
    original.parent = ChannelType::dm;
    original.byName["lobby"] = ChannelType::guildVoice;

    auto loaded = createFromJSON<ChannelSet>(toJSON(original));

    check(loaded.types.size() == 2);
    check(loaded.types[0] == ChannelType::guildVoice);
    check(loaded.types[1] == ChannelType::guildText);
    check(loaded.parent.has_value());
    check(*loaded.parent == ChannelType::dm);
    check(loaded.byName["lobby"] == ChannelType::guildVoice);
};

// ---------- XML ----------

auto enumFormatXmlAttributeIsNumeric =
    test("EnumFormat: XML writes an integer enum as a numeric attribute") = []
{
    auto node = toXML(Channel {});

    check(*Xml::findAttribute(node, "type") == "1");
    check(*Xml::findAttribute(node, "flavour") == "salty");
};

auto enumFormatXmlRoundTrips =
    test("EnumFormat: XML round-trips an integer enum") = []
{
    auto original = Channel {ChannelType::guildVoice, Flavour::sweet};
    auto loaded = createFromXMLString<Channel>(toXMLString(original));

    check(loaded.type == ChannelType::guildVoice);
    check(loaded.flavour == Flavour::sweet);
};

auto enumFormatXmlRoundTripsNested =
    test("EnumFormat: XML round-trips nested integer enums") = []
{
    auto original = ChannelSet {};
    original.types = {ChannelType::guildText, ChannelType::guildVoice};
    original.parent = ChannelType::dm;

    auto loaded = createFromXMLString<ChannelSet>(toXMLString(original));

    check(loaded.types.size() == 2);
    check(loaded.types[0] == ChannelType::guildText);
    check(loaded.types[1] == ChannelType::guildVoice);
    check(loaded.parent.has_value());
    check(*loaded.parent == ChannelType::dm);
};

auto enumFormatXmlRoundTripsNegative =
    test("EnumFormat: XML round-trips a negative integer enum") = []
{
    auto original = DeltaHolder {Delta::down};
    auto loaded = createFromXMLString<DeltaHolder>(toXMLString(original));

    check(loaded.delta == Delta::down);
};

// ---------- JSON Schema ----------

auto enumFormatSchemaIsInteger =
    test("EnumFormat: schema types an integer enum as integer") = []
{
    auto schema = schemaOf<Channel>();
    auto& body = schema["$defs"]["ChannelType"];

    check(body["type"].asString() == "integer");
    check(body["enum"].isArray());

    auto& values = body["enum"].asArray();
    check(values.size() == 3);
    check(values[0].asNumber() == 0.0);
    check(values[1].asNumber() == 1.0);
    check(values[2].asNumber() == 2.0);
};

auto enumFormatSchemaKeepsNames =
    test("EnumFormat: schema keeps the enumerator names under x-enumNames") = []
{
    auto schema = schemaOf<Channel>();
    auto& names = schema["$defs"]["ChannelType"]["x-enumNames"];

    check(names.isArray());
    check(names.asArray().size() == 3);
    check(names[0].asString() == "guildText");
    check(names[2].asString() == "guildVoice");
};

auto enumFormatSchemaFieldIsARef =
    test("EnumFormat: schema field still points at the enum's $defs entry") = []
{
    auto schema = schemaOf<Channel>();
    auto& field = schema["$defs"]["Channel"]["properties"]["type"];

    check(field["$ref"].asString() == "#/$defs/ChannelType");
};

auto enumFormatSchemaLeavesNameEnums =
    test("EnumFormat: schema still types a name enum as string") = []
{
    auto schema = schemaOf<Channel>();
    auto& body = schema["$defs"]["Flavour"];

    check(body["type"].asString() == "string");
    check(body["enum"].asArray()[0].asString() == "sweet");
    check(!body.asObject().contains("x-enumNames"));
};

auto enumFormatSchemaNegativeValues =
    test("EnumFormat: schema carries negative enumerator values") = []
{
    auto schema = schemaOf<DeltaHolder>();
    auto& values = schema["$defs"]["Delta"]["enum"];

    check(values[0].asNumber() == -1.0);
    check(values[1].asNumber() == 0.0);
    check(values[2].asNumber() == 1.0);
};

// ---------- TypeScript ----------

auto enumFormatTsDeclaresNumericEnum =
    test("EnumFormat: TypeScript declares a numeric enum with its names") = []
{
    auto out = TypeScript::toTypes<Channel>();

    check(contains(out, "export enum ChannelType {"));
    check(contains(out, "    guildText = 0,"));
    check(contains(out, "    dm = 1,"));
    check(contains(out, "    guildVoice = 2,"));
};

auto enumFormatTsFieldRefersByName =
    test("EnumFormat: TypeScript field references the numeric enum") = []
{
    auto out = TypeScript::toTypes<Channel>();

    check(contains(out, "type: ChannelType;"));
    check(comesBefore(out, "export enum ChannelType", "export interface Channel"));
};

auto enumFormatTsLeavesNameEnums =
    test("EnumFormat: TypeScript still emits a string union for a name enum") = []
{
    auto out = TypeScript::toTypes<Channel>();
    check(contains(out, "export type Flavour = \"sweet\" | \"salty\";"));
};

auto enumFormatTsNegativeMembers =
    test("EnumFormat: TypeScript keeps negative enumerator values") = []
{
    auto out = TypeScript::toTypes<DeltaHolder>();
    check(contains(out, "    down = -1,"));
};

auto enumFormatZodIsLiteralUnion =
    test("EnumFormat: zod renders an integer enum as a union of literals") = []
{
    auto out = TypeScript::toZod<Channel>();

    check(contains(out,
                   "export const ChannelType = z.union([z.literal(0), z.literal(1), "
                   "z.literal(2)]);"));
    check(contains(out, "export type ChannelType = z.infer<typeof ChannelType>;"));
};

auto enumFormatZodLeavesNameEnums =
    test("EnumFormat: zod still emits z.enum for a name enum") = []
{
    auto out = TypeScript::toZod<Channel>();
    check(contains(out, "export const Flavour = z.enum([\"sweet\", \"salty\"]);"));
};

auto enumFormatZodOptionalIsNullish =
    test("EnumFormat: an optional integer enum is still nullish in zod") = []
{
    auto out = TypeScript::toZod<ChannelSet>();
    check(contains(out, "parent: ChannelType.nullish()"));
};

auto enumFormatTsVectorElement =
    test("EnumFormat: TypeScript renders a vector of integer enums") = []
{
    auto out = TypeScript::toTypes<ChannelSet>();
    check(contains(out, "types: ChannelType[];"));
};

// ---------- C++ header export ----------

auto enumFormatCppPinsValues =
    test("EnumFormat: C++ export pins the enumerator values") = []
{
    auto out = Cpp::toHeader<Channel>(Cpp::Modes::PureCPP);

    check(contains(out, "enum class ChannelType"));
    check(contains(out, "    guildText = 0,"));
    check(contains(out, "    guildVoice = 2"));
};

auto enumFormatCppLeavesNameEnums =
    test("EnumFormat: C++ export leaves a name enum unnumbered") = []
{
    auto out = Cpp::toHeader<Channel>(Cpp::Modes::PureCPP);

    check(contains(out, "    sweet,"));
    check(!contains(out, "sweet = 0"));
};

auto enumFormatCppMiroReDeclaresTheFormat =
    test("EnumFormat: cpp-miro export re-declares the integer format") = []
{
    auto out = Cpp::toHeader<Channel>(Cpp::Modes::Miro);

    check(contains(out, "MIRO_ENUM_AS_INTEGER(ChannelType)"));
    check(!contains(out, "MIRO_ENUM_AS_INTEGER(Flavour)"));
};
