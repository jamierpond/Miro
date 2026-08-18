// Benchmark-style tests comparing the binary backend against the JSON
// backend on a database-shaped payload: many uniform records mixing
// strings, scalars and a packed float embedding. Timing ratios are
// printed for inspection; the assertions stick to what must always
// hold — bit-exact round trips and a strictly smaller wire size.

#include "TestTypes.h"

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>

using namespace nano;
using namespace Miro;

namespace
{

struct BenchTrack
{
    std::int64_t id = 0;
    std::string title;
    std::string artist;
    double duration = 0.0;
    double bpm = 0.0;
    int playCount = 0;
    bool favorite = false;
    std::vector<std::string> tags;
    std::vector<float> embedding;

    MIRO_REFLECT(
        id, title, artist, duration, bpm, playCount, favorite, tags, embedding)
};

struct BenchLibrary
{
    std::vector<BenchTrack> tracks;

    MIRO_REFLECT(tracks)
};

BenchLibrary makeLibrary(int trackCount)
{
    auto library = BenchLibrary {};
    library.tracks.reserve(static_cast<std::size_t>(trackCount));

    for (auto i = 0; i < trackCount; ++i)
    {
        auto track = BenchTrack {};
        track.id = 1000000000LL + i;
        track.title = "Track number " + std::to_string(i);
        track.artist = "Artist " + std::to_string(i % 37);
        track.duration = 180.0 + i * 0.377; // not float-representable
        track.bpm = 60.0 + (i % 120) * 0.5; // float-representable
        track.playCount = i * 7 % 1000;
        track.favorite = i % 5 == 0;
        track.tags = {"genre" + std::to_string(i % 11), i % 2 == 0 ? "even" : "odd"};

        track.embedding.reserve(32);

        for (auto d = 0; d < 32; ++d)
            track.embedding.push_back(
                std::sin(static_cast<float>(i * 32 + d) * 0.01F));

        library.tracks.push_back(std::move(track));
    }

    return library;
}

template <typename Fn>
double measureMs(int iterations, Fn&& fn)
{
    auto best = std::numeric_limits<double>::max();

    for (auto i = 0; i < iterations; ++i)
    {
        auto start = std::chrono::steady_clock::now();
        fn();
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start);
        best = std::min(best, elapsed.count());
    }

    return best;
}

bool tracksEqual(const BenchTrack& a, const BenchTrack& b)
{
    return a.id == b.id && a.title == b.title && a.artist == b.artist
           && a.duration == b.duration && a.bpm == b.bpm
           && a.playCount == b.playCount && a.favorite == b.favorite
           && a.tags == b.tags && a.embedding == b.embedding;
}

} // namespace

auto binaryBenchmark = test("Benchmark: binary vs JSON on a track library") = []
{
    constexpr auto trackCount = 1000;
    constexpr auto iterations = 5;

    auto library = makeLibrary(trackCount);

    // --- Wire size ---
    auto binary = toBinary(library);
    auto json = toJSONString(library);

    // --- Encode ---
    auto binaryEncodeMs =
        measureMs(iterations, [&] { auto buffer = toBinary(library); });
    auto jsonEncodeMs =
        measureMs(iterations, [&] { auto text = toJSONString(library); });

    // --- Decode ---
    auto binaryDecodeMs = measureMs(
        iterations, [&] { auto lib = createFromBinary<BenchLibrary>(binary); });
    auto jsonDecodeMs = measureMs(
        iterations, [&] { auto lib = createFromJSONString<BenchLibrary>(json); });

    std::cout << "Binary vs JSON, " << trackCount << " tracks (best of "
              << iterations << "):\n"
              << "  wire size:  binary " << binary.size() / 1024 << " KB, json "
              << json.size() / 1024 << " KB  ("
              << static_cast<double>(json.size())
                     / static_cast<double>(binary.size())
              << "x)\n"
              << "  encode:     binary " << binaryEncodeMs << " ms, json "
              << jsonEncodeMs << " ms  (" << jsonEncodeMs / binaryEncodeMs << "x)\n"
              << "  decode:     binary " << binaryDecodeMs << " ms, json "
              << jsonDecodeMs << " ms  (" << jsonDecodeMs / binaryDecodeMs << "x)\n";

    // Wire size must always win on this payload: interned keys plus
    // packed float32 embeddings against repeated JSON keys and 17-digit
    // decimal doubles.
    check(binary.size() < json.size());

    // Binary round trip is bit-exact — JSON can't promise this for the
    // float embeddings without printing exact decimals.
    auto decoded = createFromBinary<BenchLibrary>(binary);
    check(decoded.tracks.size() == library.tracks.size());

    for (std::size_t i = 0; i < decoded.tracks.size(); ++i)
        check(tracksEqual(decoded.tracks[i], library.tracks[i]));
};

auto binaryRandomAccess = test("Benchmark: View random access vs full decode") = []
{
    constexpr auto trackCount = 1000;

    auto library = makeLibrary(trackCount);
    auto binary = toBinary(library);
    auto json = toJSONString(library);

    // Point lookup via View: touch only the bytes on the path.
    auto doc = Binary::Document {std::span<const std::uint8_t> {binary}};
    check(doc.isValid());

    auto viewMs = measureMs(5,
                            [&]
                            {
                                auto title =
                                    doc.root()["tracks"][742]["title"].asString();
                                check(title == "Track number 742");
                            });

    // The JSON equivalent has to parse the whole document first.
    auto jsonMs = measureMs(5,
                            [&]
                            {
                                auto parsed = Json::getParsedValue(json);
                                check(parsed["tracks"][742]["title"].asString()
                                      == "Track number 742");
                            });

    std::cout << "Point lookup tracks[742].title, " << trackCount << " tracks:\n"
              << "  View (no decode): " << viewMs << " ms\n"
              << "  JSON full parse:  " << jsonMs << " ms\n";

    // Navigating by size-prefix skips must beat parsing the whole
    // document by a wide margin; assert a conservative bound.
    check(viewMs < jsonMs);
};
