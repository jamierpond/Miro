// Coverage for internally tagged unions: Miro::reflectTagged, the
// TaggedDispatcher callback API, and the Miro::Tagged /
// Miro::TaggedVariant zero-config holders. The wire form is the one
// Discord and most JSON APIs use — the discriminator is an ordinary
// field of the object and the active alternative's fields sit beside
// it: {"type": 2, "style": 1, "label": "Click"}.

#include "TestHelpers.h"

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

// ----- int tags (the Discord component shape) -------------------------

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

using Component = TaggedVariant<"type", Button, SelectMenu>;

struct ActionRow
{
    std::vector<Component> components;

    MIRO_REFLECT(components)
};

struct Panel
{
    std::string title;
    Component component;

    MIRO_REFLECT(title, component)
};

// ----- string tags ----------------------------------------------------

struct TextEvent
{
    static constexpr auto miroTag = "text";

    std::string body;

    MIRO_REFLECT(body)
};

struct ImageEvent
{
    static constexpr auto miroTag = "image";

    std::string url;

    MIRO_REFLECT(url)
};

using Payload = TaggedVariant<"kind", TextEvent, ImageEvent>;

// ----- enum tags ------------------------------------------------------

enum class Opcode : int
{
    Ping = 1,
    Pong = 2
};

struct PingFrame
{
    static constexpr auto miroTag = Opcode::Ping;

    int seq = 0;

    MIRO_REFLECT(seq)
};

struct PongFrame
{
    static constexpr auto miroTag = Opcode::Pong;

    int seq = 0;

    MIRO_REFLECT(seq)
};

using Frame = TaggedVariant<"op", PingFrame, PongFrame>;

// ----- OwningPointer<Base> storage ------------------------------------

// Same contract as the Polymorphic<> tests: a virtual destructor so
// dynamic_cast works, and an empty reflect() to satisfy Reflectable —
// alternatives are always walked through their own derived reflect().
struct NodeBase
{
    virtual ~NodeBase() = default;
    void reflect(Reflector&) {}
};

struct TextNode : NodeBase
{
    static constexpr auto miroTag = 10;

    std::string text;

    MIRO_REFLECT(text)
};

struct ImageNode : NodeBase
{
    static constexpr auto miroTag = 11;

    std::string src;

    MIRO_REFLECT(src)
};

using Node = Tagged<"type", NodeBase, TextNode, ImageNode>;

// ----- hand-written reflectTagged bodies ------------------------------

// A named user type wrapping the union, so the schema / TypeScript
// walkers have a name to hoist the union declaration under.
struct NamedComponent
{
    std::variant<Button, SelectMenu> value;

    void reflect(Reflector& ref)
    {
        reflectTagged(ref,
                      "type",
                      value,
                      [](auto& d)
                      {
                          d.template alt<Button>(2);
                          d.template alt<SelectMenu>(3);
                      });
    }
};

// Plain keys written next to the discriminator by the same reflect()
// body — they belong to every arm.
struct Interaction
{
    std::string id;
    std::variant<Button, SelectMenu> data;

    void reflect(Reflector& ref)
    {
        ref["id"](id);

        reflectTagged(ref,
                      "type",
                      data,
                      [](auto& d)
                      {
                          d.template alt<Button>(2);
                          d.template alt<SelectMenu>(3);
                      });
    }
};

// Exposes the dispatcher's handled() result so the unknown-tag test can
// observe it without a side channel.
struct ComponentWithHandledFlag
{
    std::variant<Button, SelectMenu> value;
    bool lastLoadHandled = false;

    void reflect(Reflector& ref)
    {
        auto dispatcher =
            TaggedDispatcher<std::variant<Button, SelectMenu>> {ref, "type", value};

        dispatcher.alt<Button>(2);
        dispatcher.alt<SelectMenu>(3);
        lastLoadHandled = dispatcher.handled();
    }
};

// An alternative that declares the discriminator as a field of its own.
struct EchoArm
{
    static constexpr auto miroTag = 7;

    int type = 0;
    std::string body;

    MIRO_REFLECT(type, body)
};

using Echo = TaggedVariant<"type", EchoArm>;

} // namespace

auto taggedIntTagSavesButton =
    test("Tagged union: int tag saves the discriminator beside the fields") = []
{
    auto component = Component {Button {1, "Click"}};

    check(toJSONString(component) == R"({"label":"Click","style":1,"type":2})");
};

auto taggedIntTagLoadsButton =
    test("Tagged union: int tag loads the Button arm") = []
{
    auto loaded =
        createFromJSONString<Component>(R"({"type":2,"style":1,"label":"Click"})");

    check(loaded.holds<Button>());
    check(loaded.as<Button>().style == 1);
    check(loaded.as<Button>().label == "Click");
};

auto taggedIntTagSelectMenu =
    test("Tagged union: int tag round-trips the second alternative") = []
{
    auto component = Component {SelectMenu {"pick", {"a", "b"}}};
    auto json = toJSONString(component);

    check(json == R"({"customId":"pick","options":["a","b"],"type":3})");

    auto loaded = createFromJSONString<Component>(json);
    check(loaded.holds<SelectMenu>());
    check(loaded.as<SelectMenu>().customId == "pick");
    check(loaded.as<SelectMenu>().options.size() == 2);
};

auto taggedStringTagRoundTrip = test("Tagged union: string tag round-trips") = []
{
    auto payload = Payload {ImageEvent {"http://example.com/a.png"}};
    auto json = toJSONString(payload);

    check(json == R"({"kind":"image","url":"http://example.com/a.png"})");

    auto loaded = createFromJSONString<Payload>(json);
    check(loaded.holds<ImageEvent>());
    check(loaded.as<ImageEvent>().url == "http://example.com/a.png");
};

auto taggedStringTagPicksFirstArm =
    test("Tagged union: string tag selects the matching alternative") = []
{
    auto loaded = createFromJSONString<Payload>(R"({"kind":"text","body":"hi"})");

    check(loaded.holds<TextEvent>());
    check(loaded.as<TextEvent>().body == "hi");
};

auto taggedEnumTagRoundTrip =
    test("Tagged union: enum tag saves as its enumerator name") = []
{
    auto frame = Frame {PongFrame {9}};
    auto json = toJSONString(frame);

    check(json == R"({"op":"Pong","seq":9})");

    auto loaded = createFromJSONString<Frame>(json);
    check(loaded.holds<PongFrame>());
    check(loaded.as<PongFrame>().seq == 9);
};

auto taggedEnumTagLoadsFromNumber =
    test("Tagged union: enum tag also matches a numeric wire value") = []
{
    auto loaded = createFromJSONString<Frame>(R"({"op":1,"seq":4})");

    check(loaded.holds<PingFrame>());
    check(loaded.as<PingFrame>().seq == 4);
};

auto taggedOwningPointerStorage =
    test("Tagged union: OwningPointer<Base> storage round-trips a derived arm") = []
{
    auto node = Node {};
    node.value.create<ImageNode>()->src = "logo.svg";

    auto json = toJSONString(node);
    check(json == R"({"src":"logo.svg","type":11})");

    auto loaded = createFromJSONString<Node>(json);
    check(loaded.value.get() != nullptr);

    auto* image = dynamic_cast<ImageNode*>(loaded.value.get());
    check(image != nullptr);
    check(image->src == "logo.svg");
};

auto taggedOwningPointerOtherArm =
    test("Tagged union: OwningPointer<Base> storage picks the tag's arm") = []
{
    auto loaded = createFromJSONString<Node>(R"({"type":10,"text":"hello"})");

    auto* textNode = dynamic_cast<TextNode*>(loaded.value.get());
    check(textNode != nullptr);
    check(textNode->text == "hello");
};

auto taggedListInVector =
    test("Tagged union: a vector of tagged unions round-trips") = []
{
    auto row = ActionRow {};
    row.components.push_back(Component {Button {2, "Ok"}});
    row.components.push_back(Component {SelectMenu {"menu", {}}});

    auto json = toJSONString(row);
    check(json
          == R"({"components":[{"label":"Ok","style":2,"type":2},)"
             R"({"customId":"menu","options":[],"type":3}]})");

    auto loaded = createFromJSONString<ActionRow>(json);
    check(loaded.components.size() == 2);
    check(loaded.components[0].holds<Button>());
    check(loaded.components[0].as<Button>().label == "Ok");
    check(loaded.components[1].holds<SelectMenu>());
    check(loaded.components[1].as<SelectMenu>().customId == "menu");
};

auto taggedUnknownTagLeavesValueAlone =
    test("Tagged union: an unknown tag leaves the value untouched") = []
{
    auto holder = ComponentWithHandledFlag {.value = Button {5, "before"}};

    fromJSONString(holder, R"({"type":99,"style":1,"label":"after"})");

    check(!holder.lastLoadHandled);
    check(std::holds_alternative<Button>(holder.value));
    check(std::get<Button>(holder.value).style == 5);
    check(std::get<Button>(holder.value).label == "before");
};

auto taggedMissingTagLeavesValueAlone =
    test("Tagged union: a missing discriminator key matches nothing") = []
{
    auto holder = ComponentWithHandledFlag {.value = Button {5, "before"}};

    fromJSONString(holder, R"({"style":1,"label":"after"})");

    check(!holder.lastLoadHandled);
    check(std::get<Button>(holder.value).label == "before");
};

auto taggedHandWrittenBodyRoundTrip =
    test("Tagged union: hand-written reflectTagged body round-trips") = []
{
    auto named = NamedComponent {.value = SelectMenu {"id", {"one"}}};
    auto json = toJSONString(named);

    check(json == R"({"customId":"id","options":["one"],"type":3})");

    auto loaded = createFromJSONString<NamedComponent>(json);
    check(std::holds_alternative<SelectMenu>(loaded.value));
};

auto taggedCommonFieldsBesideDiscriminator =
    test("Tagged union: plain keys can sit beside the discriminator") = []
{
    auto interaction = Interaction {.id = "abc", .data = Button {1, "Go"}};
    auto json = toJSONString(interaction);

    check(json == R"({"id":"abc","label":"Go","style":1,"type":2})");

    auto loaded = createFromJSONString<Interaction>(json);
    check(loaded.id == "abc");
    check(std::holds_alternative<Button>(loaded.data));
    check(std::get<Button>(loaded.data).label == "Go");
};

auto taggedAlternativeDeclaringTheTagField =
    test("Tagged union: the registered tag wins over an arm's own tag field") = []
{
    // EchoArm declares `type` itself and leaves it at 0; the registered
    // tag is written last, so the object still loads back as an EchoArm.
    auto echo = Echo {EchoArm {0, "hi"}};
    auto json = toJSONString(echo);

    check(json == R"({"body":"hi","type":7})");

    auto loaded = createFromJSONString<Echo>(json);
    check(loaded.holds<EchoArm>());
    check(loaded.as<EchoArm>().body == "hi");

    // The arm's own field is still populated from the wire on load.
    check(loaded.as<EchoArm>().type == 7);
};

auto taggedXmlRoundTrip = test("Tagged union: round-trips through XML") = []
{
    auto panel =
        Panel {.title = "Row", .component = Component {Button {1, "Click"}}};
    auto xmlString = toXMLString(panel);

    // The discriminator is a primitive slot, so it lands as an attribute
    // on the same element as the alternative's own fields.
    check(contains(xmlString, "type=\"2\""));
    check(contains(xmlString, "label=\"Click\""));

    auto loaded = createFromXMLString<Panel>(xmlString);
    check(loaded.title == "Row");
    check(loaded.component.holds<Button>());
    check(loaded.component.as<Button>().style == 1);
    check(loaded.component.as<Button>().label == "Click");
};

auto taggedSchemaEmitsOneOf =
    test("Tagged union: JSON Schema describes a oneOf over const tags") = []
{
    auto schema = schemaOf<Component>();

    check(schema["oneOf"].asArray().size() == 2);

    auto& first = schema["oneOf"][0];
    check(first["allOf"][0]["properties"]["type"]["const"].asNumber() == 2.0);
    check(first["allOf"][0]["required"][0].asString() == "type");
    check(first["allOf"][1]["$ref"].asString() == "#/$defs/Button");

    auto& second = schema["oneOf"][1];
    check(second["allOf"][0]["properties"]["type"]["const"].asNumber() == 3.0);
    check(second["allOf"][1]["$ref"].asString() == "#/$defs/SelectMenu");

    // Both alternatives still get their own $defs entry.
    check(schema["$defs"]["Button"]["type"].asString() == "object");
    check(schema["$defs"]["SelectMenu"]["type"].asString() == "object");
};

auto taggedSchemaStringTagIsAConstString =
    test("Tagged union: a string tag becomes a string const in the schema") = []
{
    auto schema = schemaOf<Payload>();

    check(schema["oneOf"][0]["allOf"][0]["properties"]["kind"]["const"].asString()
          == "text");
};

auto taggedSchemaHoistsNamedUnions =
    test("Tagged union: a named union gets its own $defs entry") = []
{
    auto schema = schemaOf<NamedComponent>();

    check(schema["$ref"].asString() == "#/$defs/NamedComponent");
    check(
        schema["$defs"]["NamedComponent"]["oneOf"][1]["allOf"][1]["$ref"].asString()
        == "#/$defs/SelectMenu");
};

auto taggedSchemaKeepsCommonFields =
    test("Tagged union: common fields join the arms with allOf") = []
{
    auto schema = schemaOf<Interaction>();
    auto& body = schema["$defs"]["Interaction"];

    check(body["allOf"][0]["properties"]["id"]["type"].asString() == "string");
    check(body["allOf"][1]["oneOf"][0]["allOf"][1]["$ref"].asString()
          == "#/$defs/Button");
};

auto taggedTypeScriptDiscriminatedUnion =
    test("Tagged union: TypeScript emits a discriminated union") = []
{
    auto out = TypeScript::toTypes<Component>();

    check(contains(out, "export interface Button"));
    check(contains(out,
                   "export type Root = ({ type: 2 } & Button) "
                   "| ({ type: 3 } & SelectMenu);"));
};

auto taggedTypeScriptNamedUnion =
    test("Tagged union: a named union becomes its own TypeScript alias") = []
{
    auto out = TypeScript::toTypes<NamedComponent>();

    check(contains(out,
                   "export type NamedComponent = ({ type: 2 } & Button) "
                   "| ({ type: 3 } & SelectMenu);"));
    check(comesBefore(out, "export interface Button", "export type NamedComponent"));
};

auto taggedTypeScriptStringTag =
    test("Tagged union: a string tag becomes a quoted TypeScript literal") = []
{
    auto out = TypeScript::toTypes<Payload>();

    check(contains(out, "{ kind: \"text\" } & TextEvent"));
};

auto taggedTypeScriptCommonFields =
    test("Tagged union: common fields intersect the TypeScript arms") = []
{
    auto out = TypeScript::toTypes<Interaction>();

    check(contains(out, "export type Interaction = {"));
    check(contains(out, "id: string;"));
    check(
        contains(out, "} & (({ type: 2 } & Button) | ({ type: 3 } & SelectMenu));"));
};

auto taggedZodDiscriminatedUnion =
    test("Tagged union: Zod emits a union of tag intersections") = []
{
    auto out = TypeScript::toZod<Component>();

    check(contains(out,
                   "z.union([z.intersection(z.object({ type: z.literal(2) }), "
                   "Button), z.intersection(z.object({ type: z.literal(3) }), "
                   "SelectMenu)])"));
};

auto taggedZodStringTag = test("Tagged union: Zod quotes a string tag literal") = []
{
    auto out = TypeScript::toZod<Payload>();

    check(contains(out, "z.object({ kind: z.literal(\"text\") })"));
};

auto taggedCppEmitsVariant =
    test("Tagged union: the C++ emitter renders a std::variant alias") = []
{
    auto out = Cpp::toHeader<NamedComponent>();

    check(contains(out, "using NamedComponent = std::variant<Button, SelectMenu>;"));
};

auto taggedUnsupportedReflectorThrows =
    test("Tagged union: a reflector that can't describe unions throws") = []
{
    // JsonReflector never calls beginTaggedAlternative in a normal walk;
    // forcing schema mode on it exercises the base-class guard, which
    // must fail loudly rather than emit a partial shape.
    auto json = JSON {};
    auto ref = JsonReflector {
        json, Detail::topLevelOptions<Component>(Mode::Save, /*schema=*/true)};
    auto value = Component {};

    auto threw = false;
    try
    {
        Detail::reflectValue(ref, value);
    }
    catch (const std::logic_error&)
    {
        threw = true;
    }

    check(threw);
};
