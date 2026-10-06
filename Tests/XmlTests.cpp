#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace Miro;
using namespace Miro::Xml;

auto printLeafSelfClosing = test("Print empty leaf as self-closing") = []
{
    auto node = Node {.name = "x"};
    check(print(node) == "<x/>");
};

auto printLeafWithText = test("Print leaf with text") = []
{
    auto node = Node {.name = "x", .text = "hello"};
    check(print(node) == "<x>hello</x>");
};

auto printAttributes = test("Print attributes") = []
{
    auto node = Node {.name = "Person"};
    node.attributes["name"] = "Alice";
    node.attributes["age"] = "30";
    check(print(node) == R"(<Person age="30" name="Alice"/>)");
};

auto printNestedFlat = test("Print nested children flat") = []
{
    auto child = Node {.name = "name", .text = "Alice"};
    auto root = Node {.name = "Person"};
    root.children.add(child);
    check(print(root) == "<Person><name>Alice</name></Person>");
};

auto printNestedIndented = test("Print nested children indented") = []
{
    auto child = Node {.name = "name", .text = "Alice"};
    auto root = Node {.name = "Person"};
    root.children.add(child);

    auto expected = std::string {"<Person>\n"
                                 "  <name>Alice</name>\n"
                                 "</Person>"};

    check(print(root, 2) == expected);
};

auto printEscapesText = test("Print escapes text content") = []
{
    auto node = Node {.name = "x", .text = "5 < 6 & ok"};
    check(print(node) == "<x>5 &lt; 6 &amp; ok</x>");
};

auto printEscapesAttributes = test("Print escapes attribute values") = []
{
    auto node = Node {.name = "x"};
    node.attributes["q"] = R"(He said "hi" & left)";
    check(print(node) == R"(<x q="He said &quot;hi&quot; &amp; left"/>)");
};

auto parseSelfClosing = test("Parse self-closing tag") = []
{
    auto node = parse("<x/>");
    check(node.name == "x");
    check(node.children.empty());
    check(node.text.empty());
    check(node.attributes.empty());
};

auto parseOpenClose = test("Parse open/close tag") = []
{
    auto node = parse("<x></x>");
    check(node.name == "x");
    check(node.text.empty());
};

auto parseLeafWithText = test("Parse leaf with text") = []
{
    auto node = parse("<x>hello</x>");
    check(node.text == "hello");
};

auto parseAttributes = test("Parse attributes (both quote styles)") = []
{
    auto node = parse(R"(<Person name="Alice" age='30'/>)");
    check(*findAttribute(node, "name") == "Alice");
    check(*findAttribute(node, "age") == "30");
};

auto parseEntities = test("Parse entity escapes") = []
{
    auto node = parse("<x>5 &lt; 6 &amp; &quot;ok&quot;</x>");
    check(node.text == R"(5 < 6 & "ok")");
};

auto parseEntitiesInAttribute = test("Parse entity escapes in attribute") = []
{
    auto node = parse(R"(<x q="&amp;&lt;"/>)");
    check(*findAttribute(node, "q") == "&<");
};

auto parseNested = test("Parse nested children") = []
{
    auto node = parse(R"(<Person><name>Alice</name><age>30</age></Person>)");
    check(node.children.size() == 2);
    check(node.children[0].name == "name");
    check(node.children[0].text == "Alice");
    check(node.children[1].name == "age");
    check(node.children[1].text == "30");
};

auto parseNestedWithWhitespace = test("Parse nested children with whitespace") = []
{
    auto input = std::string {"<Person>\n"
                              "  <name>Alice</name>\n"
                              "</Person>"};
    auto node = parse(input);
    check(node.children.size() == 1);
    check(node.children[0].name == "name");
    check(node.children[0].text == "Alice");
    check(node.text.empty());
};

auto roundTripIndented = test("Round-trip indented document") = []
{
    auto root = Node {.name = "Person"};
    root.attributes["id"] = "42";
    root.children.add(Node {.name = "name", .text = "Alice"});
    root.children.add(Node {.name = "note", .text = "5 < 6 & ok"});

    auto serialized = print(root, 2);
    auto reparsed = parse(serialized);
    check(reparsed == root);
};

auto parseRejectsMismatch = test("Parse rejects mismatched closing tag") = []
{
    auto threw = false;

    try
    {
        parse("<x></y>");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parseRejectsTrailing = test("Parse rejects trailing content") = []
{
    auto threw = false;

    try
    {
        parse("<x/>garbage");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parseRejectsUnterminated = test("Parse rejects unterminated tag") = []
{
    auto threw = false;

    try
    {
        parse("<x>");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parseXmlDeclaration = test("Parse skips XML declaration") = []
{
    auto node = parse(R"(<?xml version="1.0" encoding="UTF-8"?><x/>)");
    check(node.name == "x");
};

auto parseXmlDeclarationWithBlankLines =
    test("Parse skips XML declaration followed by blank lines") = []
{
    auto input = std::string {"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                              "\n"
                              "<x/>\n"};
    auto node = parse(input);
    check(node.name == "x");
};

auto parseProcessingInstructionInBody =
    test("Parse skips processing instructions in element body") = []
{
    auto node = parse("<x><?target data?><y/><?other?></x>");
    check(node.children.size() == 1);
    check(node.children[0].name == "y");
    check(node.text.empty());
};

auto parseProcessingInstructionAfterRoot =
    test("Parse skips processing instructions after root") = []
{
    auto node = parse("<x/><?pi after root?>");
    check(node.name == "x");
};

auto parseRejectsUnterminatedPI =
    test("Parse rejects unterminated processing instruction") = []
{
    auto threw = false;

    try
    {
        parse("<?xml version=\"1.0\"<x/>");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parseRejectsPIWithoutTarget =
    test("Parse rejects processing instruction without a target") = []
{
    auto threw = false;

    try
    {
        parse("<? ?><x/>");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parseCommentBeforeRoot = test("Parse skips comment before root") = []
{
    auto node = parse("<!-- a comment --><x/>");
    check(node.name == "x");
};

auto parseCommentAfterRoot = test("Parse skips comment after root") = []
{
    auto node = parse("<x/>\n<!-- trailing -->\n");
    check(node.name == "x");
};

auto parseCommentBetweenChildren = test("Parse skips comments between children") = []
{
    auto input = std::string {"<Person>\n"
                              "  <!-- the name -->\n"
                              "  <name>Alice</name>\n"
                              "  <!-- <age>99</age> disabled -->\n"
                              "  <age>30</age>\n"
                              "</Person>"};
    auto node = parse(input);
    check(node.children.size() == 2);
    check(node.children[0].name == "name");
    check(node.children[0].text == "Alice");
    check(node.children[1].name == "age");
    check(node.children[1].text == "30");
    check(node.text.empty());
};

auto parseCommentInsideText = test("Parse skips comment inside text") = []
{
    auto node = parse("<x>hel<!-- gap -->lo</x>");
    check(node.text == "hello");
    check(node.children.empty());
};

auto parseCommentWithDashes = test("Parse comment containing single dashes") = []
{
    auto node = parse("<x><!-- a - b -> c --></x>");
    check(node.name == "x");
    check(node.text.empty());
};

auto parseRejectsUnterminatedComment =
    test("Parse rejects unterminated comment") = []
{
    auto threw = false;

    try
    {
        parse("<x><!-- never closed </x>");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parseRejectsMalformedComment =
    test("Parse rejects malformed comment opener") = []
{
    auto threw = false;

    try
    {
        parse("<!-x--><x/>");
    }
    catch (const ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto parsePresetDocument = test("Parse plugin preset document") = []
{
    auto input = std::string {
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "\n"
        "<Root version=\"1\">\n"
        "  <Params>\n"
        "    <bassEnhance RealWorld=\"27%\"/>\n"
        "    <bassEnhanceEnabled RealWorld=\"On\"/>\n"
        "    <dbMax RealWorld=\"96.0000000\"/>\n"
        "    <inGain RealWorld=\"12.00 dB\"/>\n"
        "    <lowLatency/>\n"
        "    <output RealWorld=\"-12.00 dB\"/>\n"
        "    <thickTone RealWorld=\"0.7637834\"/>\n"
        "  </Params>\n"
        "  <Globals/>\n"
        "  <Meta name=\"Extremities\" category=\"BASS\" Author=\"Nahum\"/>\n"
        "</Root>\n"};

    auto root = parse(input);
    check(root.name == "Root");
    check(*findAttribute(root, "version") == "1");
    check(root.children.size() == 3);

    auto* params = findChild(root, "Params");
    check(params != nullptr);
    check(params->children.size() == 7);
    check(*findAttribute(params->children[0], "RealWorld") == "27%");
    check(*findAttribute(params->children[3], "RealWorld") == "12.00 dB");
    check(params->children[4].name == "lowLatency");
    check(params->children[4].attributes.empty());
    check(*findAttribute(params->children[5], "RealWorld") == "-12.00 dB");

    auto* globals = findChild(root, "Globals");
    check(globals != nullptr);
    check(globals->children.empty());

    auto* meta = findChild(root, "Meta");
    check(meta != nullptr);
    check(*findAttribute(*meta, "name") == "Extremities");
    check(*findAttribute(*meta, "category") == "BASS");
    check(*findAttribute(*meta, "Author") == "Nahum");
};

auto parseDecimalCharRef = test("Parse decimal character reference") = []
{
    auto node = parse("<x>caf&#233;</x>");
    check(node.text == "caf\xC3\xA9");
};

auto parseHexCharRef = test("Parse hex character reference") = []
{
    auto node = parse("<x>caf&#xE9;</x>");
    check(node.text == "caf\xC3\xA9");
};

auto parseHexCharRefUppercaseX =
    test("Parse hex character reference accepts uppercase X and digits") = []
{
    auto node = parse("<x>&#XE9;&#xe9;</x>");
    check(node.text == "\xC3\xA9\xC3\xA9");
};

auto parseNewlineCharRef = test("Parse newline character reference") = []
{
    auto node = parse("<x>a&#10;b</x>");
    check(node.text == "a\nb");
};

auto parseAstralCharRef = test("Parse character reference beyond the BMP") = []
{
    auto node = parse("<x>&#x1F3B5;</x>");
    check(node.text == "\xF0\x9F\x8E\xB5");
};

auto parseCharRefInAttribute = test("Parse character reference in attribute") = []
{
    auto node = parse(R"(<x name="Ren&#233;e&#x20;&#65;"/>)");
    check(*findAttribute(node, "name")
          == "Ren\xC3\xA9"
             "e A");
};

auto parseCharRefMixedWithEntities =
    test("Parse character references mixed with named entities") = []
{
    auto node = parse("<x>&lt;&#60;&#x3C;&gt;</x>");
    check(node.text == "<<<>");
};

auto parseCharRefRoundTrip =
    test("Character reference text survives a print/parse round trip") = []
{
    auto node = parse("<x>caf&#233; &#x1F3B5;</x>");
    auto reparsed = parse(print(node));
    check(reparsed == node);
};

static bool parseThrows(const char* input)
{
    try
    {
        parse(input);
    }
    catch (const ParseError&)
    {
        return true;
    }

    return false;
}

auto parseRejectsEmptyCharRef = test("Parse rejects empty character reference") = []
{
    check(parseThrows("<x>&#;</x>"));
    check(parseThrows("<x>&#x;</x>"));
};

auto parseRejectsBadDigitsCharRef =
    test("Parse rejects character reference with invalid digits") = []
{
    check(parseThrows("<x>&#12a;</x>"));
    check(parseThrows("<x>&#xZZ;</x>"));
    check(parseThrows("<x>&#-1;</x>"));
};

auto parseRejectsInvalidCodePoint =
    test("Parse rejects character reference to an invalid code point") = []
{
    check(parseThrows("<x>&#0;</x>"));
    check(parseThrows("<x>&#xD800;</x>"));
    check(parseThrows("<x>&#xDFFF;</x>"));
    check(parseThrows("<x>&#x110000;</x>"));
    check(parseThrows("<x>&#99999999999999999999;</x>"));
};

auto parseRejectsUnknownNamedEntity =
    test("Parse still rejects unknown named entity") = []
{ check(parseThrows("<x>&nbsp;</x>")); };
