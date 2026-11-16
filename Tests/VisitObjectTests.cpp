// Reflector::visitObject: the dispatch hands every reflectable object to
// the reflector as an erased ObjectView (type id, address, reflect
// function) before its body. Claiming it skips beginNamedType and the
// body; declining walks it exactly as before. The default declines, so
// JSON is unchanged.

#include <Miro/Miro.h>
#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

struct Point
{
    MIRO_REFLECT(x, y)

    int x = 1;
    int y = 2;
};

struct HandWritten
{
    void reflect(Reflector& ref)
    {
        ref["label"](label);
        ref["point"](point);
    }

    std::string label = "a";
    Point point;
};

struct Holder
{
    MIRO_REFLECT(id, point, written, points)

    int id = 7;
    Point point;
    HandWritten written;
    std::vector<Point> points {{3, 4}, {5, 6}};
};

struct ObjectLog
{
    std::vector<std::string> events;
    std::vector<const void*> claimedAddresses;
    std::vector<std::string_view> claimedTypes;
    std::string_view claimType;
    bool walkClaimed = false;
    int loadedX = 0;
};

struct ObjectRecorder : Reflector
{
    ObjectRecorder(ObjectLog& logToUse, Options optsToUse)
        : Reflector(optsToUse)
        , log(logToUse)
    {
    }

    bool visitObject(const ObjectView& object) override
    {
        log.events.push_back("object:" + std::string {object.type.shortName});

        if (object.type.qualifiedName != log.claimType)
            return false;

        log.claimedAddresses.push_back(object.instance);
        log.claimedTypes.push_back(object.type.qualifiedName);

        if (isLoading())
            if (auto* point = object.as<Point>())
                point->x = log.loadedX;

        if (log.walkClaimed)
            object.reflectInto(*this);

        return true;
    }

    bool beginNamedType(TypeId id) override
    {
        log.events.push_back("named:" + std::string {id.shortName});
        return true;
    }

    void visit(PrimitiveRef) override { log.events.push_back("prim"); }
    void writeNull() override {}
    ValueKind kind() const override
    {
        if (shape() == Shape::Array)
            return ValueKind::Array;

        return ValueKind::Object;
    }

    std::size_t arraySize() const override { return 2; }

    Reflector& atKey(std::string_view key, Options childOpts) override
    {
        log.events.push_back("key:" + std::string {key});
        return spawn(childOpts);
    }

    Reflector& atIndex(std::size_t index, Options childOpts) override
    {
        log.events.push_back("index:" + std::to_string(index));
        return spawn(childOpts);
    }

    Reflector& spawn(Options childOpts)
    {
        children.push_back(std::make_unique<ObjectRecorder>(log, childOpts));
        return *children.back();
    }

    ObjectLog& log;
    std::vector<std::unique_ptr<ObjectRecorder>> children;
};

std::vector<std::string> walk(Holder& holder, ObjectLog& log, Mode mode)
{
    auto root = ObjectRecorder {log, Detail::topLevelOptions<Holder>(mode)};
    Detail::reflectValue(root, holder);
    return log.events;
}

using Events = std::vector<std::string>;

const auto pointWalk = Events {"named:Point", "key:x", "prim", "key:y", "prim"};

Events concat(std::initializer_list<Events> parts)
{
    auto all = Events {};

    for (auto& part: parts)
        all.insert(all.end(), part.begin(), part.end());

    return all;
}

Events declinedHolderWalk()
{
    auto objectPoint = Events {"object:Point"};
    return concat({{"object:Holder", "named:Holder", "key:id", "prim", "key:point"},
                   objectPoint,
                   pointWalk,
                   {"key:written", "object:HandWritten", "named:HandWritten"},
                   {"key:label", "prim", "key:point"},
                   objectPoint,
                   pointWalk,
                   {"key:points", "index:0"},
                   objectPoint,
                   pointWalk,
                   {"index:1"},
                   objectPoint,
                   pointWalk});
}

} // namespace

auto declineWalksNormally = test("visitObject: declining walks as before") = []
{
    auto holder = Holder {};
    auto log = ObjectLog {};

    check(walk(holder, log, Mode::Save) == declinedHolderWalk());
    check(log.claimedAddresses.empty());
};

auto claimStopsWalk = test("visitObject: claiming skips the object's body") = []
{
    auto holder = Holder {};
    auto log = ObjectLog {.claimType = Detail::qualifiedNameOf<HandWritten>()};

    auto expected =
        Events {"key:written", "object:HandWritten", "key:points", "index:0"};
    auto events = walk(holder, log, Mode::Save);
    auto written = std::find(events.begin(), events.end(), "key:written");

    check(written != events.end());
    check(Events(written, written + 4) == expected);
    check(log.claimedAddresses.size() == 1);
    check(log.claimedAddresses.front() == &holder.written);
    check(log.claimedTypes.front() == typeIdOf<HandWritten>().qualifiedName);
};

auto claimMacroType = test("visitObject: claims a MIRO_REFLECT type") = []
{
    auto holder = Holder {};
    auto log = ObjectLog {.claimType = Detail::qualifiedNameOf<Point>()};
    auto events = walk(holder, log, Mode::Save);

    check(std::find(events.begin(), events.end(), "named:Point") == events.end());
    check(std::find(events.begin(), events.end(), "key:x") == events.end());
    check(log.claimedAddresses
          == std::vector<const void*> {&holder.point,
                                       &holder.written.point,
                                       &holder.points[0],
                                       &holder.points[1]});
};

auto firesForVectorElements = test("visitObject: fires for vector elements") = []
{
    auto points = std::vector<Point> {{1, 2}, {3, 4}};
    auto log = ObjectLog {.claimType = Detail::qualifiedNameOf<Point>()};
    auto root = ObjectRecorder {
        log, Detail::topLevelOptions<std::vector<Point>>(Mode::Save)};
    Detail::reflectValue(root, points);

    check(log.events
          == Events {"index:0", "object:Point", "index:1", "object:Point"});
    check(log.claimedAddresses == std::vector<const void*> {&points[0], &points[1]});
};

auto reflectClaimedObject =
    test("visitObject: ObjectView walks a claimed object") = []
{
    auto holder = Holder {};
    auto log = ObjectLog {.claimType = Detail::qualifiedNameOf<HandWritten>(),
                          .walkClaimed = true};
    auto declined = ObjectLog {};

    check(walk(holder, log, Mode::Save) == walk(holder, declined, Mode::Save));
    check(log.claimedAddresses.size() == 1);
};

auto firesInLoadMode = test("visitObject: fires in Load mode") = []
{
    auto holder = Holder {};
    auto log =
        ObjectLog {.claimType = Detail::qualifiedNameOf<Point>(), .loadedX = 42};
    walk(holder, log, Mode::Load);

    check(log.claimedAddresses.size() == 4);
    check(holder.point.x == 42);
    check(holder.written.point.x == 42);
    check(holder.points[0].x == 42);
    check(holder.points[1].x == 42);
};

auto jsonUnchanged = test("visitObject: JSON round trip is unchanged") = []
{
    auto holder = Holder {};
    auto json = toJSONString(holder);

    check(json
          == R"({"id":7,"point":{"x":1,"y":2},"points":[{"x":3,"y":4},)"
             R"({"x":5,"y":6}],"written":{"label":"a","point":{"x":1,"y":2}}})");

    auto loaded = createFromJSONString<Holder>(json);
    check(toJSONString(loaded) == json);
};
