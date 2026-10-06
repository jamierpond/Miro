// Schema evolution: data written by one version of a struct, read by the
// next. Every case runs through the binary wire and through JSON with the
// same expectations, so the two formats can't drift apart on what a
// changed struct means.
//
// The rules these tests pin, for both formats:
// - Fields are matched by name, never by position: reordering is free,
//   an unknown field is skipped, a missing field keeps the value the
//   reader started with (its default member initializer).
// - A renamed field is a missing field plus an unknown one, unless the
//   new reflect() reads the old name with Property::legacy().
// - A value whose type no longer fits the field is refused and the field
//   keeps its prior value: no wraparound, no out-of-range casts.
// - Truncated or corrupted bytes never crash or read out of bounds.

#include <Miro/Binary.h>
#include <Miro/Reflect.h>
#include <Miro/Xml.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace nano;
using namespace Miro;

namespace
{

template <typename To, typename From>
To viaBinary(const From& value)
{
    return createFromBinary<To>(toBinary(value));
}

template <typename To, typename From>
To viaJson(const From& value)
{
    return createFromJSONString<To>(toJSONString(value));
}

// Writes `value` as From, reads it back as To, in both formats, and
// hands each result to `expect`.
template <typename To, typename From, typename Expect>
void inBothFormats(const From& value, Expect expect)
{
    expect(viaBinary<To>(value));
    expect(viaJson<To>(value));
}

// --- The running example: a field renamed between versions ---

struct ProfileV1
{
    std::uint32_t field1 = 0;
    std::string field2;

    MIRO_REFLECT(field1, field2)
};

// Renamed with no migration: field2's data is simply orphaned.
struct ProfileV2Unmigrated
{
    std::uint32_t field1 = 0;
    std::string fieldTwo = "unset";

    MIRO_REFLECT(field1, fieldTwo)
};

// Renamed with a hand-written reflect that migrates the old name.
struct ProfileV2
{
    std::uint32_t field1 = 0;
    std::string fieldTwo = "unset";

    void reflect(Reflector& ref)
    {
        ref["field1"](field1);
        ref["fieldTwo"].legacy("field2")(fieldTwo);
    }

    bool operator==(const ProfileV2&) const = default;
};

// Writes both spellings at once, as a document touched by both versions
// might.
struct ProfileBothNames
{
    std::uint32_t field1 = 0;
    std::string field2;
    std::string fieldTwo;

    MIRO_REFLECT(field1, field2, fieldTwo)
};

ProfileV1 sampleV1()
{
    return {.field1 = 4000000000u, .field2 = "hello"};
}

auto renameUnmigrated =
    test("Evolution: renamed field without migration keeps its default") = []
{
    inBothFormats<ProfileV2Unmigrated>(sampleV1(),
                                       [](const ProfileV2Unmigrated& v2)
                                       {
                                           check(v2.field1 == 4000000000u);
                                           check(v2.fieldTwo == "unset");
                                       });
};

auto renameMigrated =
    test("Evolution: legacy() reads a renamed field from its old name") = []
{
    inBothFormats<ProfileV2>(sampleV1(),
                             [](const ProfileV2& v2)
                             {
                                 check(v2.field1 == 4000000000u);
                                 check(v2.fieldTwo == "hello");
                             });
};

auto renameNewKeyWins =
    test("Evolution: legacy() prefers the current name when both exist") = []
{
    auto both = ProfileBothNames {.field1 = 1, .field2 = "old", .fieldTwo = "new"};

    inBothFormats<ProfileV2>(
        both, [](const ProfileV2& v2) { check(v2.fieldTwo == "new"); });
};

auto renameNeitherKey =
    test("Evolution: legacy() with neither name present keeps the default") = []
{
    struct OnlyField1
    {
        std::uint32_t field1 = 3;
        MIRO_REFLECT(field1)
    };

    inBothFormats<ProfileV2>(OnlyField1 {},
                             [](const ProfileV2& v2)
                             {
                                 check(v2.field1 == 3);
                                 check(v2.fieldTwo == "unset");
                             });
};

auto renameSavesNewNameOnly =
    test("Evolution: legacy() saves under the current name only") = []
{
    auto v2 = ProfileV2 {.field1 = 1, .fieldTwo = "x"};

    auto bytes = toBinary(v2);
    auto doc = Binary::Document {bytes};
    check(doc.root()["fieldTwo"].asString() == "x");
    check(!doc.root()["field2"].isValid());

    auto json = toJSON(v2);
    check(json.asObject().contains("fieldTwo"));
    check(!json.asObject().contains("field2"));
};

auto renameStablePoint =
    test("Evolution: V1 -> V2 -> re-encode -> V2 is a fixed point") = []
{
    // Binary
    {
        auto migrated = viaBinary<ProfileV2>(sampleV1());
        auto reencoded = toBinary(migrated);
        auto again = createFromBinary<ProfileV2>(reencoded);

        check(again == migrated);
        check(again.fieldTwo == "hello");
        check(!Binary::Document {reencoded}.findKey("field2"));
        check(toBinary(again) == reencoded);
    }

    // JSON
    {
        auto migrated = viaJson<ProfileV2>(sampleV1());
        auto reencoded = toJSONString(migrated);
        auto again = createFromJSONString<ProfileV2>(reencoded);

        check(again == migrated);
        check(again.fieldTwo == "hello");
        check(reencoded.find("field2") == std::string::npos);
        check(toJSONString(again) == reencoded);
    }
};

// A container-typed field is the case a naive "read both keys" reflect
// gets wrong: reading a missing key into a vector used to clear it.
struct TagsV1
{
    std::vector<std::string> labels;
    MIRO_REFLECT(labels)
};

struct TagsV2
{
    std::vector<std::string> tags;

    void reflect(Reflector& ref) { ref["tags"].legacy("labels")(tags); }
};

auto renameContainer =
    test("Evolution: legacy() migrates a renamed vector field") = []
{
    inBothFormats<TagsV2>(
        TagsV1 {{"a", "b"}},
        [](const TagsV2& v2)
        { check(v2.tags == std::vector<std::string> {"a", "b"}); });
};

// --- Added, removed, reordered fields ---

struct AddedV2
{
    std::uint32_t field1 = 0;
    std::string field2;
    int added = 42;
    std::vector<int> addedList {1, 2};
    std::map<std::string, int> addedMap {{"k", 1}};
    std::optional<int> addedOptional;
    std::optional<int> addedOptionalWithValue = 5;

    MIRO_REFLECT(field1,
                 field2,
                 added,
                 addedList,
                 addedMap,
                 addedOptional,
                 addedOptionalWithValue)
};

auto fieldAdded = test("Evolution: fields added in V2 keep their defaults") = []
{
    inBothFormats<AddedV2>(sampleV1(),
                           [](const AddedV2& v2)
                           {
                               check(v2.field1 == 4000000000u);
                               check(v2.field2 == "hello");
                               check(v2.added == 42);
                               check(v2.addedList == std::vector<int> {1, 2});
                               check(v2.addedMap.size() == 1);
                               check(v2.addedMap.at("k") == 1);
                               check(!v2.addedOptional.has_value());
                               check(v2.addedOptionalWithValue == 5);
                           });
};

struct WideV1
{
    std::uint32_t field1 = 0;
    std::string field2;
    std::vector<double> dropped;
    std::optional<std::string> droppedOptional;
    std::map<std::string, ProfileV1> droppedMap;
    int tail = 0;

    MIRO_REFLECT(field1, field2, dropped, droppedOptional, droppedMap, tail)
};

struct NarrowV2
{
    std::uint32_t field1 = 0;
    std::string field2;
    int tail = 0;

    MIRO_REFLECT(field1, field2, tail)
};

auto fieldRemoved = test("Evolution: fields removed in V2 are skipped") = []
{
    auto v1 = WideV1 {};
    v1.field1 = 7;
    v1.field2 = "s";
    v1.dropped = {0.1, 2.0, 1e300};
    v1.droppedOptional = "gone";
    v1.droppedMap = {{"a", sampleV1()}};
    v1.tail = -3;

    inBothFormats<NarrowV2>(v1,
                            [](const NarrowV2& v2)
                            {
                                check(v2.field1 == 7);
                                check(v2.field2 == "s");
                                check(v2.tail == -3);
                            });
};

struct ReorderedV2
{
    int tail = 0;
    std::string field2;
    std::uint32_t field1 = 0;

    MIRO_REFLECT(tail, field2, field1)
};

auto fieldsReordered = test("Evolution: reordered fields load by name") = []
{
    auto v1 = NarrowV2 {.field1 = 11, .field2 = "x", .tail = 12};

    inBothFormats<ReorderedV2>(v1,
                               [](const ReorderedV2& v2)
                               {
                                   check(v2.field1 == 11);
                                   check(v2.field2 == "x");
                                   check(v2.tail == 12);
                               });
};

struct OptionalV1
{
    std::optional<int> maybe = 9;
    std::optional<int> nulled;
    int kept = 1;

    MIRO_REFLECT(maybe, nulled, kept)
};

struct OptionalRemovedV2
{
    int kept = 0;
    MIRO_REFLECT(kept)
};

struct OptionalToPlainV2
{
    int maybe = -1;
    int nulled = -1;
    int kept = 0;

    MIRO_REFLECT(maybe, nulled, kept)
};

struct PlainToOptionalV2
{
    std::optional<int> kept;
    MIRO_REFLECT(kept)
};

auto optionalRemoved =
    test("Evolution: optional fields removed in V2 are skipped") = []
{
    inBothFormats<OptionalRemovedV2>(
        OptionalV1 {}, [](const OptionalRemovedV2& v2) { check(v2.kept == 1); });
};

auto optionalUnwrapped =
    test("Evolution: optional<int> -> int takes the value, null keeps default") = []
{
    inBothFormats<OptionalToPlainV2>(OptionalV1 {},
                                     [](const OptionalToPlainV2& v2)
                                     {
                                         check(v2.maybe == 9);
                                         check(v2.nulled == -1);
                                         check(v2.kept == 1);
                                     });
};

auto optionalWrapped =
    test("Evolution: int -> optional<int> engages with the value") = []
{
    inBothFormats<PlainToOptionalV2>(
        OptionalV1 {}, [](const PlainToOptionalV2& v2) { check(v2.kept == 1); });
};

// --- Evolved types inside containers ---

struct OuterV1
{
    ProfileV1 profile;
    std::vector<ProfileV1> list;
    std::map<std::string, ProfileV1> byName;

    MIRO_REFLECT(profile, list, byName)
};

struct OuterV2
{
    ProfileV2 profile;
    std::vector<ProfileV2> list;
    std::map<std::string, ProfileV2> byName;

    MIRO_REFLECT(profile, list, byName)
};

struct AddedOuterV2
{
    AddedV2 profile;
    std::vector<AddedV2> list;
    std::map<std::string, AddedV2> byName;

    MIRO_REFLECT(profile, list, byName)
};

OuterV1 sampleOuterV1()
{
    auto v1 = OuterV1 {};
    v1.profile = {.field1 = 1, .field2 = "nested"};
    v1.list = {{.field1 = 2, .field2 = "first"}, {.field1 = 3, .field2 = "second"}};
    v1.byName = {{"a", {.field1 = 4, .field2 = "alpha"}},
                 {"b", {.field1 = 5, .field2 = "beta"}}};
    return v1;
}

auto nestedRenamed =
    test("Evolution: nested, vector and map elements migrate a rename") = []
{
    inBothFormats<OuterV2>(sampleOuterV1(),
                           [](const OuterV2& v2)
                           {
                               check(v2.profile.fieldTwo == "nested");
                               check(v2.list.size() == 2);

                               if (v2.list.size() == 2)
                               {
                                   check(v2.list[0].field1 == 2);
                                   check(v2.list[0].fieldTwo == "first");
                                   check(v2.list[1].fieldTwo == "second");
                               }
                               check(v2.byName.size() == 2);
                               check(v2.byName.at("a").fieldTwo == "alpha");
                               check(v2.byName.at("b").field1 == 5);
                               check(v2.byName.at("b").fieldTwo == "beta");
                           });
};

auto nestedAdded =
    test("Evolution: nested, vector and map elements default added fields") = []
{
    inBothFormats<AddedOuterV2>(sampleOuterV1(),
                                [](const AddedOuterV2& v2)
                                {
                                    check(v2.profile.added == 42);
                                    check(v2.profile.field2 == "nested");
                                    check(v2.list.size() == 2);

                                    for (const auto& element: v2.list)
                                    {
                                        check(element.added == 42);
                                        check(element.addedList.size() == 2);
                                    }

                                    check(v2.byName.at("a").added == 42);
                                    check(v2.byName.at("a").field2 == "alpha");
                                });
};

// --- Type changes ---

template <typename T>
struct Holder
{
    T value {};
    int after = 0;

    MIRO_REFLECT(value, after)
};

// Writes Holder<From>{from}, reads Holder<To> starting from `prior`.
template <typename To, typename From, typename Expect>
void typeChange(From from, To prior, Expect expect)
{
    auto written = Holder<From> {from, 77};

    {
        auto loaded = Holder<To> {prior, 0};
        fromBinary(loaded, toBinary(written));
        expect(loaded.value);
        check(loaded.after == 77);
    }

    {
        auto loaded = Holder<To> {prior, 0};
        fromJSONString(loaded, toJSONString(written));
        expect(loaded.value);
        check(loaded.after == 77);
    }
}

auto widenUnsigned = test("Evolution: uint32 -> uint64 widens") = []
{
    typeChange<std::uint64_t>(std::uint32_t {4000000000u},
                              std::uint64_t {1},
                              [](std::uint64_t v) { check(v == 4000000000u); });
};

auto narrowUnsignedFits =
    test("Evolution: uint64 -> uint32 converts when it fits") = []
{
    typeChange<std::uint32_t>(std::uint64_t {123},
                              std::uint32_t {1},
                              [](std::uint32_t v) { check(v == 123); });
};

auto narrowUnsignedOverflow =
    test("Evolution: uint64 -> uint32 out of range keeps prior") = []
{
    typeChange<std::uint32_t>(std::uint64_t {1} << 40,
                              std::uint32_t {1},
                              [](std::uint32_t v) { check(v == 1); });
};

auto narrowSignedOverflow =
    test("Evolution: int64 -> int32 out of range keeps prior") = []
{
    typeChange<std::int32_t>(std::int64_t {-5000000000LL},
                             std::int32_t {1},
                             [](std::int32_t v) { check(v == 1); });
};

auto signedToUnsigned = test("Evolution: negative int -> unsigned keeps prior") = []
{
    typeChange<std::uint32_t>(
        -1, std::uint32_t {1}, [](std::uint32_t v) { check(v == 1); });
    typeChange<std::uint16_t>(
        -1, std::uint16_t {1}, [](std::uint16_t v) { check(v == 1); });
};

auto uint64FullRange =
    test("Evolution: uint64 past INT64_MAX still round trips") = []
{
    auto big = std::numeric_limits<std::uint64_t>::max() - 5;
    typeChange<std::uint64_t>(
        big, std::uint64_t {1}, [big](std::uint64_t v) { check(v == big); });
};

auto intToDouble = test("Evolution: int -> double converts") = []
{ typeChange<double>(-7, 0.5, [](double v) { check(v == -7.0); }); };

auto int64ToDouble = test("Evolution: int64 -> double converts") = []
{
    typeChange<double>(std::int64_t {1} << 52,
                       0.5,
                       [](double v) { check(v == 4503599627370496.0); });
};

auto doubleToFloat = test("Evolution: double -> float narrows to nearest") = []
{ typeChange<float>(0.1, 1.0f, [](float v) { check(v == 0.1f); }); };

auto doubleToFloatOverflow =
    test("Evolution: double -> float out of range keeps prior") = []
{ typeChange<float>(1e300, 1.0f, [](float v) { check(v == 1.0f); }); };

auto doubleToInt = test("Evolution: double -> int truncates toward zero") = []
{
    typeChange<int>(3.9, 1, [](int v) { check(v == 3); });
    typeChange<int>(-3.9, 1, [](int v) { check(v == -3); });
};

auto doubleToIntOverflow =
    test("Evolution: double -> int out of range keeps prior") = []
{
    typeChange<int>(1e300, 1, [](int v) { check(v == 1); });
    typeChange<int>(-1e300, 1, [](int v) { check(v == 1); });
    typeChange<std::int64_t>(
        1e19, std::int64_t {1}, [](std::int64_t v) { check(v == 1); });
    typeChange<std::uint8_t>(
        300.0, std::uint8_t {1}, [](std::uint8_t v) { check(v == 1); });
};

auto nanToInt = test("Evolution: NaN -> int keeps prior (binary)") = []
{
    // JSON has no NaN, so this one is binary only.
    auto written = Holder<double> {std::numeric_limits<double>::quiet_NaN(), 77};
    auto loaded = Holder<int> {5, 0};
    fromBinary(loaded, toBinary(written));
    check(loaded.value == 5);
    check(loaded.after == 77);
};

auto stringToInt = test("Evolution: string -> int keeps prior") = []
{ typeChange<int>(std::string {"12"}, 1, [](int v) { check(v == 1); }); };

auto intToString = test("Evolution: int -> string keeps prior") = []
{
    typeChange<std::string>(12,
                            std::string {"prior"},
                            [](const std::string& v) { check(v == "prior"); });
};

auto scalarToVector = test("Evolution: scalar -> vector reads as empty") = []
{
    typeChange<std::vector<int>>(
        5, std::vector<int> {}, [](const std::vector<int>& v) { check(v.empty()); });
};

auto vectorToScalar = test("Evolution: vector -> scalar keeps prior") = []
{ typeChange<int>(std::vector<int> {1, 2, 3}, 1, [](int v) { check(v == 1); }); };

auto structToScalar = test("Evolution: struct -> scalar keeps prior") = []
{ typeChange<int>(sampleV1(), 1, [](int v) { check(v == 1); }); };

auto scalarToStruct = test("Evolution: scalar -> struct keeps prior fields") = []
{
    typeChange<ProfileV2>(5,
                          ProfileV2 {.field1 = 1, .fieldTwo = "prior"},
                          [](const ProfileV2& v)
                          {
                              check(v.field1 == 1);
                              check(v.fieldTwo == "prior");
                          });
};

auto packedElementChange =
    test("Evolution: vector<double> -> vector<int> converts per element") = []
{
    // Out-of-range elements keep their prior (zero) value.
    typeChange<std::vector<int>>(std::vector<double> {1.5, -2.5, 1e300, 7.0},
                                 std::vector<int> {},
                                 [](const std::vector<int>& v)
                                 { check(v == std::vector<int> {1, -2, 0, 7}); });
};

auto packedNarrowing = test(
    "Evolution: vector<int64> -> vector<int32> refuses overflow per element") = []
{
    typeChange<std::vector<std::int32_t>>(
        std::vector<std::int64_t> {1, std::int64_t {1} << 40, -3},
        std::vector<std::int32_t> {},
        [](const std::vector<std::int32_t>& v)
        { check(v == std::vector<std::int32_t> {1, 0, -3}); });
};

// --- Journal form ---

auto Bytes = [](std::initializer_list<Binary::Buffer> parts)
{
    auto out = Binary::Buffer {};

    for (const auto& part: parts)
        out.insert(out.end(), part.begin(), part.end());

    return out;
};

auto journalEvolved =
    test("Evolution: V1 journal base and frames load as map<string, V2>") = []
{
    auto base = std::map<std::string, ProfileV1> {
        {"a", {.field1 = 1, .field2 = "one"}},
        {"b", {.field1 = 2, .field2 = "two"}},
    };

    auto bytes = Bytes({
        toBinaryJournal(base),
        binaryJournalSet("c", ProfileV1 {.field1 = 3, .field2 = "three"}),
        binaryJournalSet("a", ProfileV1 {.field1 = 10, .field2 = "uno"}),
        binaryJournalRemove("b"),
    });

    auto loaded = createFromBinary<std::map<std::string, ProfileV2>>(bytes);
    check(loaded.size() == 2);
    check(loaded.at("a").field1 == 10);
    check(loaded.at("a").fieldTwo == "uno");
    check(loaded.at("c").fieldTwo == "three");
    check(!loaded.contains("b"));

    auto added = createFromBinary<std::map<std::string, AddedV2>>(bytes);
    check(added.at("c").added == 42);
    check(added.at("c").field2 == "three");
};

auto journalMixed = test("Evolution: V2 frames appended to a V1 journal") = []
{
    auto base =
        std::map<std::string, ProfileV1> {{"a", {.field1 = 1, .field2 = "one"}}};

    auto bytes = Bytes({
        toBinaryJournal(base),
        binaryJournalSet("b", ProfileV2 {.field1 = 2, .fieldTwo = "two"}),
    });

    auto loaded = createFromBinary<std::map<std::string, ProfileV2>>(bytes);
    check(loaded.at("a").fieldTwo == "one");
    check(loaded.at("b").fieldTwo == "two");

    // Compaction: re-encode as a fresh V2 journal, nothing old remains.
    auto compacted = toBinaryJournal(loaded);
    check(createFromBinary<std::map<std::string, ProfileV2>>(compacted) == loaded);
};

// --- Damaged input for an evolved struct ---

auto truncatedBinary =
    test("Evolution: every truncation of a V1 document reads as default V2") = []
{
    auto bytes = toBinary(sampleOuterV1());

    for (auto size = std::size_t {0}; size < bytes.size(); ++size)
    {
        auto prefix = std::span<const std::uint8_t> {bytes.data(), size};
        auto loaded = createFromBinary<OuterV2>(prefix);

        // The key table trails the document, so any truncation loses it
        // and the document is refused whole.
        check(loaded.list.empty());
        check(loaded.byName.empty());
        check(loaded.profile.fieldTwo == "unset");
    }
};

auto truncatedJournal =
    test("Evolution: every truncation of a V1 journal reads a clean prefix") = []
{
    auto base =
        std::map<std::string, ProfileV1> {{"a", {.field1 = 1, .field2 = "one"}}};
    auto bytes = Bytes({
        toBinaryJournal(base),
        binaryJournalSet("b", ProfileV1 {.field1 = 2, .field2 = "two"}),
    });

    for (auto size = std::size_t {0}; size < bytes.size(); ++size)
    {
        auto loaded = createFromBinary<std::map<std::string, ProfileV2>>(
            std::span<const std::uint8_t> {bytes.data(), size});

        // Either nothing (base torn), or the base alone (frame torn).
        check(loaded.empty()
              || (loaded.size() == 1 && loaded.at("a").fieldTwo == "one"));
    }
};

auto corruptedBinary =
    test("Evolution: corrupted V1 bytes never crash a V2 load") = []
{
    // Run under ASan/UBSan for this one to mean anything: it flips every
    // byte (and sets it to each interesting tag value) and only checks
    // that the load returns.
    auto bytes = toBinary(sampleOuterV1());

    for (auto i = std::size_t {0}; i < bytes.size(); ++i)
    {
        for (auto replacement:
             {std::uint8_t {0x00},
              std::uint8_t {0xFF},
              std::uint8_t {0x7F},
              std::uint8_t {0x80},
              static_cast<std::uint8_t>(bytes[i] ^ 0x01),
              std::uint8_t {static_cast<std::uint8_t>(Binary::Tag::Array)},
              std::uint8_t {static_cast<std::uint8_t>(Binary::Tag::PackedDouble)}})
        {
            auto damaged = bytes;
            damaged[i] = replacement;

            auto loaded = createFromBinary<OuterV2>(damaged);
            check(loaded.list.size() <= 1000000);
        }
    }
};

auto truncatedJson = test("Evolution: truncated V1 JSON reads as default V2") = []
{
    auto text = toJSONString(sampleOuterV1());

    for (auto size = std::size_t {0}; size < text.size(); ++size)
    {
        auto loaded =
            createFromJSONString<OuterV2>(std::string_view {text.data(), size});
        check(loaded.list.empty());
        check(loaded.profile.fieldTwo == "unset");
    }
};

// --- The missing-container rule outside binary/JSON ---

auto xmlEmptyVector =
    test("Evolution: XML empty vector over a non-empty default stays empty") = []
{
    // A missing key keeps a container's prior value; an empty one that
    // was written must still read back empty.
    struct WithList
    {
        std::vector<int> items {1, 2};
        MIRO_REFLECT(items)
    };

    auto value = WithList {};
    value.items.clear();

    check(createFromBinary<WithList>(toBinary(value)).items.empty());
    check(createFromJSONString<WithList>(toJSONString(value)).items.empty());
    check(createFromXMLString<WithList>(toXMLString(value)).items.empty());
};

} // namespace
