#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace Miro;

// --- Scalar parsing ---

auto yamlParseNull = test("YAML: parse null") = []
{
    check(Yaml::parse("null").isNull());
    check(Yaml::parse("~").isNull());
};

auto yamlParseEmptyDocument = test("YAML: parse empty document as null") = []
{
    check(Yaml::parse("").isNull());
    check(Yaml::parse("# just a comment\n").isNull());
    check(Yaml::parse("---\n").isNull());
};

auto yamlParseBoolTrue = test("YAML: parse bool true") = []
{
    auto value = Yaml::parse("true");
    check(value.isBool());
    check(value.asBool() == true);
};

auto yamlParseBoolFalse = test("YAML: parse bool false") = []
{
    auto value = Yaml::parse("false");
    check(value.isBool());
    check(value.asBool() == false);
};

auto yamlParseInteger = test("YAML: parse integer") = []
{
    auto value = Yaml::parse("42");
    check(value.isNumber());
    check(value.asNumber() == 42.0);
};

auto yamlParseNegativeNumber = test("YAML: parse negative number") = []
{
    auto value = Yaml::parse("-3.14");
    check(value.isNumber());
    check(value.asNumber() == -3.14);
};

auto yamlParseExponent = test("YAML: parse exponent") = []
{
    auto value = Yaml::parse("1e10");
    check(value.isNumber());
    check(value.asNumber() == 1e10);
};

auto yamlParsePlainString = test("YAML: parse plain string") = []
{
    auto value = Yaml::parse("hello");
    check(value.isString());
    check(value.asString() == "hello");
};

auto yamlParsePlainStringWithPunctuation =
    test("YAML: parse plain string with punctuation") = []
{
    check(Yaml::parse("5 < 6 ok").asString() == "5 < 6 ok");
    check(Yaml::parse("1.2.3").asString() == "1.2.3");
};

auto yamlParseDoubleQuotedEscapes =
    test("YAML: parse double-quoted string with escapes") = []
{
    auto value = Yaml::parse(R"("line1\nline2\ttab")");
    check(value.asString() == "line1\nline2\ttab");
};

auto yamlParseSingleQuoted = test("YAML: parse single-quoted string") = []
{
    auto value = Yaml::parse("'it''s here'");
    check(value.asString() == "it's here");
};

auto yamlParseUnicodeEscape = test("YAML: parse unicode escape") = []
{
    auto value = Yaml::parse(R"("A")");
    check(value.asString() == "A");
};

auto yamlParseQuotedNumberIsString =
    test("YAML: parse quoted number stays a string") = []
{
    auto value = Yaml::parse(R"("42")");
    check(value.isString());
    check(value.asString() == "42");
};

// --- Flow collections ---

auto yamlParseEmptyFlowSequence = test("YAML: parse empty flow sequence") = []
{
    auto value = Yaml::parse("[]");
    check(value.isArray());
    check(value.asArray().empty());
};

auto yamlParseFlowSequence = test("YAML: parse flow sequence") = []
{
    auto value = Yaml::parse("[1, two, true, null]");
    check(value.isArray());
    check(value.asArray().size() == 4);
    check(value[0].asNumber() == 1.0);
    check(value[1].asString() == "two");
    check(value[2].asBool() == true);
    check(value[3].isNull());
};

auto yamlParseEmptyFlowMapping = test("YAML: parse empty flow mapping") = []
{
    auto value = Yaml::parse("{}");
    check(value.isObject());
    check(value.asObject().empty());
};

auto yamlParseFlowMapping = test("YAML: parse flow mapping") = []
{
    auto value = Yaml::parse("{name: Miro, version: 1.0, active: true}");
    check(value["name"].asString() == "Miro");
    check(value["version"].asNumber() == 1.0);
    check(value["active"].asBool() == true);
};

auto yamlParseNestedFlow = test("YAML: parse nested flow collections") = []
{
    auto value = Yaml::parse("{a: {b: [1, 2]}}");
    check(value["a"]["b"][0].asNumber() == 1.0);
    check(value["a"]["b"][1].asNumber() == 2.0);
};

// --- Block collections ---

auto yamlParseBlockSequence = test("YAML: parse block sequence") = []
{
    auto value = Yaml::parse("- 1\n"
                             "- two\n"
                             "- true\n"
                             "- null\n");

    check(value.isArray());
    check(value.asArray().size() == 4);
    check(value[0].asNumber() == 1.0);
    check(value[1].asString() == "two");
    check(value[2].asBool() == true);
    check(value[3].isNull());
};

auto yamlParseBlockMapping = test("YAML: parse block mapping") = []
{
    auto value = Yaml::parse("name: Miro\n"
                             "version: 1.0\n"
                             "features:\n"
                             "  - json\n"
                             "  - yaml\n"
                             "active: true\n"
                             "metadata: null\n");

    check(value.isObject());
    check(value["name"].asString() == "Miro");
    check(value["version"].asNumber() == 1.0);
    check(value["active"].asBool() == true);
    check(value["metadata"].isNull());
    check(value["features"].isArray());
    check(value["features"][0].asString() == "json");
    check(value["features"][1].asString() == "yaml");
};

auto yamlParseEmptyValueIsNull = test("YAML: parse key with no value as null") = []
{
    auto value = Yaml::parse("a:\n"
                             "b: 1\n");

    check(value["a"].isNull());
    check(value["b"].asNumber() == 1.0);
};

auto yamlParseNestedMappings = test("YAML: parse nested mappings") = []
{
    auto value = Yaml::parse("a:\n"
                             "  b:\n"
                             "    c: 42\n");

    check(value["a"]["b"]["c"].asNumber() == 42.0);
};

auto yamlParseSequenceOfMappings =
    test("YAML: parse sequence of compact mappings") = []
{
    auto value = Yaml::parse("items:\n"
                             "  - x: 1\n"
                             "    y: 2\n"
                             "  - x: 3\n");

    check(value["items"].asArray().size() == 2);
    check(value["items"][0]["x"].asNumber() == 1.0);
    check(value["items"][0]["y"].asNumber() == 2.0);
    check(value["items"][1]["x"].asNumber() == 3.0);
};

auto yamlParseSequenceAtParentIndent =
    test("YAML: parse sequence aligned with parent key") = []
{
    auto value = Yaml::parse("items:\n"
                             "- 1\n"
                             "- 2\n");

    check(value["items"].asArray().size() == 2);
    check(value["items"][0].asNumber() == 1.0);
    check(value["items"][1].asNumber() == 2.0);
};

auto yamlParseNestedSequences = test("YAML: parse nested sequences") = []
{
    auto value = Yaml::parse("- - a\n"
                             "  - b\n"
                             "- c\n");

    check(value.asArray().size() == 2);
    check(value[0].asArray().size() == 2);
    check(value[0][0].asString() == "a");
    check(value[0][1].asString() == "b");
    check(value[1].asString() == "c");
};

auto yamlParseQuotedKeys = test("YAML: parse quoted keys") = []
{
    auto value = Yaml::parse("\"my key\": 1\n"
                             "'other key': 2\n");

    check(value["my key"].asNumber() == 1.0);
    check(value["other key"].asNumber() == 2.0);
};

auto yamlParseComments = test("YAML: parse comments") = []
{
    auto value = Yaml::parse("# leading comment\n"
                             "a: 1 # trailing comment\n"
                             "# dangling comment\n"
                             "b: 2\n");

    check(value["a"].asNumber() == 1.0);
    check(value["b"].asNumber() == 2.0);
};

auto yamlParseHashInsideQuotes =
    test("YAML: parse '#' inside quotes is not a comment") = []
{
    auto value = Yaml::parse(R"(a: "color #1")");
    check(value["a"].asString() == "color #1");
};

auto yamlParseDocumentStartMarker = test("YAML: parse document start marker") = []
{
    auto value = Yaml::parse("---\n"
                             "a: 1\n");

    check(value["a"].asNumber() == 1.0);
};

auto yamlValueEquality = test("YAML: value equality") = []
{
    check(Yaml::parse("42") == Yaml::parse("42"));
    check(Yaml::parse("hello") == Yaml::parse("hello"));
    check(Yaml::parse("true") == Yaml::parse("true"));
    check(Yaml::parse("null") == Yaml::parse("null"));
    check(Yaml::parse("{a: [1, 2]}") == Yaml::parse("a:\n  - 1\n  - 2\n"));
};

// --- Parse errors ---

auto yamlParseAnchorThrows = test("YAML: parse anchor throws") = []
{
    auto threw = false;

    try
    {
        Yaml::parse("a: &anchor 1");
    }
    catch (const Yaml::ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto yamlParseBlockScalarThrows = test("YAML: parse block scalar throws") = []
{
    auto threw = false;

    try
    {
        Yaml::parse("a: |\n  text\n");
    }
    catch (const Yaml::ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto yamlParseTabIndentThrows = test("YAML: parse tab indentation throws") = []
{
    auto threw = false;

    try
    {
        Yaml::parse("\ta: 1");
    }
    catch (const Yaml::ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto yamlParseTrailingContentThrows =
    test("YAML: parse trailing content throws") = []
{
    auto threw = false;

    try
    {
        Yaml::parse("42\nmore");
    }
    catch (const Yaml::ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto yamlParseUnterminatedFlowThrows =
    test("YAML: parse unterminated flow sequence throws") = []
{
    auto threw = false;

    try
    {
        Yaml::parse("[1, 2");
    }
    catch (const Yaml::ParseError&)
    {
        threw = true;
    }

    check(threw);
};

auto yamlParseUnterminatedStringThrows =
    test("YAML: parse unterminated string throws") = []
{
    auto threw = false;

    try
    {
        Yaml::parse("\"abc");
    }
    catch (const Yaml::ParseError&)
    {
        threw = true;
    }

    check(threw);
};

// --- Print tests ---

auto yamlPrintScalars = test("YAML: print scalars") = []
{
    check(Yaml::print(Yaml::parse("null")) == "null");
    check(Yaml::print(Yaml::parse("true")) == "true");
    check(Yaml::print(Yaml::parse("false")) == "false");
    check(Yaml::print(Yaml::parse("42")) == "42");
    check(Yaml::print(Yaml::parse("-7")) == "-7");
    check(Yaml::print(Yaml::parse("3.14")) == "3.14");
    check(Yaml::print(Yaml::parse("hello")) == "hello");
};

auto yamlPrintEmptyContainers = test("YAML: print empty containers") = []
{
    check(Yaml::print(Yaml::parse("[]")) == "[]");
    check(Yaml::print(Yaml::parse("{}")) == "{}");
};

auto yamlPrintQuotesAmbiguousStrings =
    test("YAML: print quotes ambiguous strings") = []
{
    check(Yaml::print(YAML {"true"}) == R"("true")");
    check(Yaml::print(YAML {"42"}) == R"("42")");
    check(Yaml::print(YAML {"a: b"}) == R"("a: b")");
    check(Yaml::print(YAML {""}) == R"("")");
    check(Yaml::print(YAML {"-dash"}) == R"("-dash")");
    check(Yaml::print(YAML {"line1\nline2"}) == R"("line1\nline2")");
    check(Yaml::print(YAML {"hello world"}) == "hello world");
};

auto yamlPrintBlockMapping = test("YAML: print block mapping") = []
{
    auto value = Yaml::parse("{a: 1, b: two}");
    check(Yaml::print(value) == "a: 1\nb: two");
};

auto yamlPrintBlockSequence = test("YAML: print block sequence") = []
{
    auto value = Yaml::parse("[1, 2, 3]");
    check(Yaml::print(value) == "- 1\n- 2\n- 3");
};

auto yamlPrintNested = test("YAML: print nested collections") = []
{
    auto value = Yaml::parse("{a: {b: 1}, c: [2, 3]}");
    auto expected = std::string {"a:\n"
                                 "  b: 1\n"
                                 "c:\n"
                                 "  - 2\n"
                                 "  - 3"};
    check(Yaml::print(value) == expected);
};

auto yamlPrintSequenceOfMappings =
    test("YAML: print sequence of mappings compactly") = []
{
    auto value = Yaml::parse("{items: [{x: 1, y: 2}, {x: 3}]}");
    auto expected = std::string {"items:\n"
                                 "  - x: 1\n"
                                 "    y: 2\n"
                                 "  - x: 3"};
    check(Yaml::print(value) == expected);
};

auto yamlPrintIndentFour = test("YAML: print with indent 4") = []
{
    auto value = Yaml::parse("{items: [{x: 1, y: 2}, {x: 3}]}");
    auto expected = std::string {"items:\n"
                                 "    -   x: 1\n"
                                 "        y: 2\n"
                                 "    -   x: 3"};
    check(Yaml::print(value, 4) == expected);
};

auto yamlPrintFlowStyle = test("YAML: print flow style") = []
{
    auto value = Yaml::parse("a: 1\n"
                             "b:\n"
                             "  - 1\n"
                             "  - two\n");
    check(Yaml::print(value, 0) == "{a: 1, b: [1, two]}");
};

auto yamlPrintRoundTrip = test("YAML: print round-trip") = []
{
    auto original = Yaml::parse("name: Miro\n"
                                "version: \"42\"\n"
                                "note: 5 < 6 ok\n"
                                "multi: \"line1\\nline2\"\n"
                                "items:\n"
                                "  - 1\n"
                                "  - two\n"
                                "  - true\n"
                                "  - null\n"
                                "nested:\n"
                                "  x: 1\n"
                                "empty: []\n"
                                "emptyMap: {}\n");

    check(Yaml::parse(Yaml::print(original)) == original);
    check(Yaml::parse(Yaml::print(original, 0)) == original);
    check(Yaml::parse(Yaml::print(original, 4)) == original);
};

// --- Hostile real-world documents ---

bool parseThrows(std::string_view textToUse)
{
    try
    {
        Yaml::parse(textToUse);
    }
    catch (const Yaml::ParseError&)
    {
        return true;
    }

    return false;
}

auto yamlParseDockerCompose = test("YAML: parse hostile docker-compose.yml") = []
{
    auto compose = Yaml::parse(R"(
---
# docker-compose.yml — production stack
version: "3.9"

services:
  web:
    image: nginx:1.25-alpine
    container_name: web_frontend
    restart: unless-stopped
    ports:
      - "80:80"
      - "443:443"
      - 8080:8080
    environment:
      NGINX_HOST: example.com
      NGINX_PORT: 443
      DEBUG: "false"
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
      - static_data:/var/www/static
    depends_on:
      api:
        condition: service_healthy
    networks: [frontend, backend]
    labels:
      com.example.description: "Front # not a comment"
      com.example.scale: 3

  api:
    image: example/api:2.4.1
    command: ["./server", "--port", "9000", "--verbose"]
    environment:
      - DATABASE_URL=postgres://user:pass@db:5432/app
      - SECRET_KEY=s3cr3t#notacomment
    healthcheck:
      test: [CMD, curl, -f, "http://localhost:9000/health"]
      interval: 30s
      timeout: 10s
      retries: 5
    deploy:
      resources:
        limits: {cpus: "0.50", memory: 512M}

  db:
    image: postgres:16
    environment:
      POSTGRES_USER: app
      POSTGRES_PASSWORD: "p@ss: w0rd!"
      POSTGRES_DB: app
    volumes:
      - db_data:/var/lib/postgresql/data

volumes:
  db_data: {}
  static_data:

networks:
  frontend:
    driver: bridge
  backend:
    driver: bridge
    ipam:
      config:
        - subnet: 172.28.0.0/16
)");

    check(compose["version"].asString() == "3.9");
    check(compose["services"].asObject().size() == 3);

    const auto& web = compose["services"]["web"];
    check(web["image"].asString() == "nginx:1.25-alpine");
    check(web["restart"].asString() == "unless-stopped");
    check(web["ports"][0].asString() == "80:80");
    check(web["ports"][2].asString() == "8080:8080");
    check(web["environment"]["NGINX_PORT"].asNumber() == 443.0);
    check(web["environment"]["DEBUG"].asString() == "false");
    check(web["volumes"][0].asString() == "./nginx.conf:/etc/nginx/nginx.conf:ro");
    check(web["depends_on"]["api"]["condition"].asString() == "service_healthy");
    check(web["networks"][1].asString() == "backend");
    check(web["labels"]["com.example.description"].asString()
          == "Front # not a comment");
    check(web["labels"]["com.example.scale"].asNumber() == 3.0);

    const auto& api = compose["services"]["api"];
    check(api["command"].asArray().size() == 4);
    check(api["command"][1].asString() == "--port");
    check(api["environment"][0].asString()
          == "DATABASE_URL=postgres://user:pass@db:5432/app");
    check(api["environment"][1].asString() == "SECRET_KEY=s3cr3t#notacomment");
    check(api["healthcheck"]["test"][2].asString() == "-f");
    check(api["healthcheck"]["test"][3].asString()
          == "http://localhost:9000/health");
    check(api["healthcheck"]["interval"].asString() == "30s");
    check(api["healthcheck"]["retries"].asNumber() == 5.0);
    check(api["deploy"]["resources"]["limits"]["cpus"].asString() == "0.50");
    check(api["deploy"]["resources"]["limits"]["memory"].asString() == "512M");

    const auto& db = compose["services"]["db"];
    check(db["environment"]["POSTGRES_PASSWORD"].asString() == "p@ss: w0rd!");

    check(compose["volumes"]["db_data"].isObject());
    check(compose["volumes"]["db_data"].asObject().empty());
    check(compose["volumes"]["static_data"].isNull());
    check(compose["networks"]["backend"]["ipam"]["config"][0]["subnet"].asString()
          == "172.28.0.0/16");

    check(Yaml::parse(Yaml::print(compose)) == compose);
    check(Yaml::parse(Yaml::print(compose, 0)) == compose);
};

auto yamlParseGithubWorkflow = test("YAML: parse hostile GitHub workflow") = []
{
    auto workflow = Yaml::parse(R"(
name: CI
on:
  push:
    branches: [main]
  schedule:
    - cron: "*/15 * * * *"
jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        os: [ubuntu-latest, macos-14]
        config: [Debug, Release]
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -B build -DMIRO_UNITY_BUILD=OFF
      - name: Test
        run: ctest --test-dir build -R "YAML: parse"
        env:
          CTEST_OUTPUT_ON_FAILURE: 1
)");

    check(workflow["name"].asString() == "CI");
    check(workflow["on"]["push"]["branches"][0].asString() == "main");
    check(workflow["on"]["schedule"][0]["cron"].asString() == "*/15 * * * *");

    const auto& build = workflow["jobs"]["build"];
    check(build["runs-on"].asString() == "ubuntu-latest");
    check(build["strategy"]["matrix"]["os"][1].asString() == "macos-14");
    check(build["strategy"]["matrix"]["config"].asArray().size() == 2);
    check(build["steps"].asArray().size() == 3);
    check(build["steps"][0]["uses"].asString() == "actions/checkout@v4");
    check(build["steps"][1]["name"].asString() == "Configure");
    check(build["steps"][1]["run"].asString()
          == "cmake -B build -DMIRO_UNITY_BUILD=OFF");
    check(build["steps"][2]["run"].asString()
          == "ctest --test-dir build -R \"YAML: parse\"");
    check(build["steps"][2]["env"]["CTEST_OUTPUT_ON_FAILURE"].asNumber() == 1.0);

    check(Yaml::parse(Yaml::print(workflow)) == workflow);
    check(Yaml::parse(Yaml::print(workflow, 0)) == workflow);
};

auto yamlParseScalarTorture = test("YAML: parse hostile scalars") = []
{
    auto value = Yaml::parse(R"(
tilde: ~
empty:
yes: no
dot-leading: .hidden
semver: 1.2.3
plus: +42
zero-padded: 0755
ratio: 16:9
windows-path: C:\Users\test
url: https://example.com:8443/a?b=c&d=e#frag
colons: ::
spaces:     lots   of   space
percent: 100%
emoji: 🚀 to the moon
empty-quoted: ''
escaped: "tab\there \"quoted\" A"
apostrophe: 'it''s ''quoted'''
)");

    check(value["tilde"].isNull());
    check(value["empty"].isNull());
    check(value["yes"].asString() == "no");
    check(value["dot-leading"].asString() == ".hidden");
    check(value["semver"].asString() == "1.2.3");
    check(value["plus"].asNumber() == 42.0);
    check(value["zero-padded"].asNumber() == 755.0);
    check(value["ratio"].asString() == "16:9");
    check(value["windows-path"].asString() == R"(C:\Users\test)");
    check(value["url"].asString() == "https://example.com:8443/a?b=c&d=e#frag");
    check(value["colons"].asString() == "::");
    check(value["spaces"].asString() == "lots   of   space");
    check(value["percent"].asString() == "100%");
    check(value["emoji"].asString() == "🚀 to the moon");
    check(value["empty-quoted"].asString() == "");
    check(value["escaped"].asString() == "tab\there \"quoted\" A");
    check(value["apostrophe"].asString() == "it's 'quoted'");

    check(Yaml::parse(Yaml::print(value)) == value);
    check(Yaml::parse(Yaml::print(value, 0)) == value);
};

auto yamlParseHostileKeys = test("YAML: parse hostile keys") = []
{
    auto value = Yaml::parse(R"(
key with spaces: 1
"quoted: colon": 2
'single # hash': 3
dotted.key/slash: 4
question?: 5
<<: 6
)");

    check(value["key with spaces"].asNumber() == 1.0);
    check(value["quoted: colon"].asNumber() == 2.0);
    check(value["single # hash"].asNumber() == 3.0);
    check(value["dotted.key/slash"].asNumber() == 4.0);
    check(value["question?"].asNumber() == 5.0);
    check(value["<<"].asNumber() == 6.0);

    check(Yaml::parse(Yaml::print(value)) == value);
    check(Yaml::parse(Yaml::print(value, 0)) == value);
};

auto yamlParseCrlfDocument = test("YAML: parse CRLF document") = []
{
    auto value = Yaml::parse("version: \"3.9\"\r\n"
                             "# comment\r\n"
                             "services:\r\n"
                             "  app:\r\n"
                             "    image: alpine:3.20\r\n");

    check(value["version"].asString() == "3.9");
    check(value["services"]["app"]["image"].asString() == "alpine:3.20");
};

auto yamlParseCommentChaos = test("YAML: parse comment chaos") = []
{
    auto value = Yaml::parse(R"(
# header comment
a: 1 # trailing
        # floating comment, deeply indented
b:
  # comment inside block
  - 1 # one
      # misaligned comment between elements
  - 2
c: 3
)");

    check(value["a"].asNumber() == 1.0);
    check(value["b"].asArray().size() == 2);
    check(value["b"][1].asNumber() == 2.0);
    check(value["c"].asNumber() == 3.0);
};

auto yamlParseDeepNesting = test("YAML: parse deeply nested hostile mix") = []
{
    auto value = Yaml::parse(R"(
matrix:
  - - - leaf: [{x: [1, [2, {y: 'z, with comma'}]]}]
)");

    const auto& leaf = value["matrix"][0][0][0]["leaf"];
    check(leaf[0]["x"][0].asNumber() == 1.0);
    check(leaf[0]["x"][1][0].asNumber() == 2.0);
    check(leaf[0]["x"][1][1]["y"].asString() == "z, with comma");

    check(Yaml::parse(Yaml::print(value)) == value);
    check(Yaml::parse(Yaml::print(value, 0)) == value);
};

auto yamlParseHostileRejects =
    test("YAML: hostile documents reject unsupported syntax") = []
{
    check(parseThrows("x-defaults: &defaults\n"
                      "  restart: always\n"));

    check(parseThrows("services:\n"
                      "  web:\n"
                      "    <<: *defaults\n"));

    check(parseThrows("services:\n"
                      "  app:\n"
                      "    command: |\n"
                      "      echo hello\n"));

    check(parseThrows("---\na: 1\n---\nb: 2\n"));
    check(parseThrows("%YAML 1.2\n---\na: 1\n"));
    check(parseThrows("a: !!str 5\n"));
};

auto yamlJsonCrossRoundTrip =
    test("YAML: round-trip yaml -> json string -> yaml string") = []
{
    auto original = Yaml::parse(R"(
service:
  image: nginx:1.25-alpine
  ports:
    - "80:80"
    - 8080:8080
  command: ["./server", "--port", "9000"]
  limits: {cpus: "0.50", memory: 512M}
  note: "Front # not a comment"
  password: "p@ss: w0rd!"
  cron: "*/15 * * * *"
  emoji: 🚀 to the moon
  retries: 5
  enabled: true
  extra: ~
)");

    auto jsonText = Json::print(original);
    auto fromJson = Json::parse(jsonText);
    check(fromJson == original);

    auto yamlText = Yaml::print(fromJson);
    check(Yaml::parse(yamlText) == original);

    auto indentedJson = Json::print(original, 2);
    check(Yaml::parse(Yaml::print(Json::parse(indentedJson), 0)) == original);
};
