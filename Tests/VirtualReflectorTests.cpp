// Examples + extensibility tests for the virtual Reflector base.
//
// These tests show two things:
//   1. User code is unchanged: `void reflect(Reflector&)` with `ref["x"](x)`
//      syntax, no templates, no virtuals visible.
//   2. A new reflector kind is implemented as a small Reflector subclass.
//      Each reflector instance represents one slot; recursion happens by
//      asking the reflector for a child via atKey/atIndex. No internal
//      stack, no cursor — the position is implicit in which instance
//      you have.

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <sstream>

using namespace nano;
using namespace Miro;

// ─── Example user types — same shape as today's user code ─────────────

namespace
{

struct Point
{
    double x = 0.0;
    double y = 0.0;

    MIRO_REFLECT(x, y)
};

struct Polygon
{
    std::string name;
    std::vector<Point> vertices;

    MIRO_REFLECT(name, vertices)
};

struct Settings
{
    bool enabled = true;
    int maxRetries = 3;
    std::optional<std::string> note;
    std::map<std::string, int> counters;

    MIRO_REFLECT(enabled, maxRetries, note, counters)
};

struct HandWritten
{
    int count = 0;
    std::string label;

    void reflect(Reflector& ref)
    {
        ref["count"](count);
        ref["label"](label);
    }
};

} // namespace

// ─── Round-trip examples — user-facing API unchanged ───────────────────

auto roundtripStruct = test("Virtual API: struct round-trip") = []
{
    auto original = Polygon {"triangle", {{0, 0}, {1, 0}, {0, 1}}};
    auto restored =
        createFromJSONString<Polygon>(toJSONString(original));

    check(restored.name == "triangle");
    check(restored.vertices.size() == 3);
    check(restored.vertices[2].x == 0.0);
    check(restored.vertices[2].y == 1.0);
};

auto roundtripContainersAndOptional =
    test("Virtual API: containers + optional round-trip") = []
{
    auto original = Settings {.enabled = false,
                              .maxRetries = 7,
                              .note = "ready",
                              .counters = {{"a", 1}, {"b", 2}}};

    auto restored =
        createFromJSONString<Settings>(toJSONString(original));

    check(restored.enabled == false);
    check(restored.maxRetries == 7);
    check(restored.note.has_value());
    check(*restored.note == "ready");
    check(restored.counters.at("a") == 1);
    check(restored.counters.at("b") == 2);
};

auto handWrittenReflect = test("Virtual API: hand-written reflect method") = []
{
    auto original = HandWritten {42, "hello"};
    auto restored =
        createFromJSONString<HandWritten>(toJSONString(original));

    check(restored.count == 42);
    check(restored.label == "hello");
};

auto rootLevelVector = test("Virtual API: top-level vector serializes as array") = []
{
    auto original = std::vector<int> {1, 2, 3};
    auto restored =
        createFromJSONString<std::vector<int>>(toJSONString(original));

    check(restored.size() == 3);
    check(restored[0] == 1);
    check(restored[2] == 3);
};

auto optionalNullRoundtrip = test("Virtual API: optional null round-trip") = []
{
    auto original = Settings {};
    original.note.reset();

    auto restored =
        createFromJSONString<Settings>(toJSONString(original));

    check(!restored.note.has_value());
};

// ─── Custom reflector — implemented entirely as a Reflector subclass ───
//
// TracingReflector records every reflection step as text. Each instance
// carries a label (its position in the tree) and a shared output stream.
// The shape (committed via Options at construction) decides the open
// bracket and the matching closer; the closer is emitted in the
// destructor, with currentChild destroyed first so closes nest
// correctly.

namespace
{

struct TracingReflector : Reflector
{
    std::ostringstream& out;
    std::string label;
    std::string closer;
    std::unique_ptr<TracingReflector> currentChild;

    TracingReflector(std::ostringstream& outToUse,
                     Options optsToUse,
                     std::string labelToUse = {})
        : Reflector(optsToUse)
        , out(outToUse)
        , label(std::move(labelToUse))
    {
        switch (optsToUse.shape)
        {
            case Shape::Primitive:
                break;
            case Shape::Object:
            case Shape::Map:
                out << label << "={";
                closer = "}";
                break;
            case Shape::Array:
                out << label << "=[";
                closer = "]";
                break;
            case Shape::Raw:
                out << label << "=<raw>";
                break;
        }
    }

    ~TracingReflector() override
    {
        currentChild.reset();
        out << closer;
    }

    void visit(PrimitiveRef ref) override
    {
        out << label << "=";
        std::visit(
            [this](auto* ptr)
            {
                using T = std::remove_pointer_t<decltype(ptr)>;
                if constexpr (std::is_same_v<T, bool>)
                    out << (*ptr ? "true" : "false");
                else if constexpr (std::is_same_v<T, std::string>)
                    out << "\"" << *ptr << "\"";
                else
                    out << *ptr;
            },
            ref.data);
        out << ";";
    }

    void writeNull() override { out << label << "=null;"; }
    ValueKind kind() const override { return ValueKind::Absent; }

    Reflector& atKey(std::string_view key, Options childOpts) override
    {
        currentChild.reset();
        currentChild =
            std::make_unique<TracingReflector>(out, childOpts, std::string {key});
        return *currentChild;
    }

    Reflector& atIndex(std::size_t index, Options childOpts) override
    {
        currentChild.reset();
        currentChild = std::make_unique<TracingReflector>(
            out, childOpts, "[" + std::to_string(index) + "]");
        return *currentChild;
    }
};

} // namespace

auto customReflectorOnStruct =
    test("Custom reflector: traces struct reflection") = []
{
    auto trace = std::ostringstream {};
    auto reflector = TracingReflector {trace, {.shape = Shape::Primitive}};
    auto value = Point {3.0, 4.0};

    value.reflect(reflector);

    check(trace.str() == "x=3;y=4;");
};

auto customReflectorOnNested =
    test("Custom reflector: traces nested + array structure") = []
{
    auto trace = std::ostringstream {};
    {
        auto reflector = TracingReflector {trace, {.shape = Shape::Primitive}};
        auto poly = Polygon {"L", {{1, 0}, {0, 1}}};
        poly.reflect(reflector);
    }

    check(trace.str() == "name=\"L\";vertices=[[0]={x=1;y=0;}[1]={x=0;y=1;}]");
};

auto customReflectorTopLevelVector =
    test("Custom reflector: traces top-level vector") = []
{
    auto trace = std::ostringstream {};
    {
        auto reflector = TracingReflector {
            trace,
            Detail::topLevelOptions<std::vector<int>>(Mode::Save)};
        auto vec = std::vector<int> {10, 20, 30};
        Detail::reflectValue(reflector, vec);
    }

    check(trace.str() == "=[[0]=10;[1]=20;[2]=30;]");
};
