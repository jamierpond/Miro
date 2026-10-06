#include <Miro/Miro.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct BenchmarkResult
{
    double average() const { return total / iterations; }

    std::string name;
    double total = 0.0;
    int iterations = 1;
};

BenchmarkResult measure(const std::string& nameToUse,
                        int iterationsToUse,
                        const std::function<void()>& fnToUse)
{
    auto start = std::chrono::high_resolution_clock::now();

    for (auto i = 0; i < iterationsToUse; ++i)
        fnToUse();

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(end - start);

    return {nameToUse, elapsed.count(), iterationsToUse};
}

void report(const BenchmarkResult& miroToUse, const BenchmarkResult& nlohmannToUse)
{
    auto ratio = nlohmannToUse.average() / miroToUse.average();

    std::cout << "  Miro:     " << std::fixed << std::setprecision(2)
              << miroToUse.average() << " ms avg (" << miroToUse.iterations
              << " iterations)\n";
    std::cout << "  nlohmann: " << std::fixed << std::setprecision(2)
              << nlohmannToUse.average() << " ms avg (" << nlohmannToUse.iterations
              << " iterations)\n";
    std::cout << "  Ratio:    " << std::fixed << std::setprecision(2) << ratio
              << "x (>1 = Miro faster)\n\n";
}

std::string generateLargeArray(int countToUse)
{
    auto stream = std::ostringstream {};
    stream << "[";

    for (auto i = 0; i < countToUse; ++i)
    {
        if (i > 0)
            stream << ",";

        stream << i << "." << (i % 100);
    }

    stream << "]";
    return stream.str();
}

std::string generateObjectArray(int countToUse)
{
    auto stream = std::ostringstream {};
    stream << "[";

    for (auto i = 0; i < countToUse; ++i)
    {
        if (i > 0)
            stream << ",";

        stream << R"({"id":)" << i << R"(,"name":"item_)" << i << R"(","value":)"
               << (i * 1.5) << R"(,"active":)" << (i % 2 == 0 ? "true" : "false")
               << R"(,"tags":["a","b","c"]})";
    }

    stream << "]";
    return stream.str();
}

std::string generateDeepNested(int depthToUse)
{
    auto stream = std::ostringstream {};

    for (auto i = 0; i < depthToUse; ++i)
        stream << R"({"level)" << i << R"(":)";

    stream << "42";

    for (auto i = 0; i < depthToUse; ++i)
        stream << "}";

    return stream.str();
}

std::string generateStringHeavy(int countToUse)
{
    auto stream = std::ostringstream {};
    stream << "{";

    for (auto i = 0; i < countToUse; ++i)
    {
        if (i > 0)
            stream << ",";

        stream << R"("key_)" << i << R"(":"This is a longer string value )"
               << R"(with some content for key number )" << i
               << R"( and it includes special chars )"
               << R"(like \n newlines and \t tabs")";
    }

    stream << "}";
    return stream.str();
}

// Byte-level BPE tokens the way a HuggingFace tokenizer.json spells them:
// short, mostly ASCII, and about a third carrying the leading U+0120
// ("Ġ", the visible stand-in for a space) that makes them multi-byte.
std::string generateVocabularyKey(int indexToUse)
{
    static constexpr auto letters = std::string_view {"abcdefghijklmnopqrstuvwxyz"};

    auto key = std::string {};

    if (indexToUse % 3 == 0)
        key += "\xC4\xA0";

    auto remainder = indexToUse;

    do
    {
        key += letters[static_cast<std::size_t>(remainder % 26)];
        remainder /= 26;
    } while (remainder > 0);

    if (indexToUse % 5 == 0)
        key += "\xC3\xA4";

    return key;
}

std::string generateVocabulary(int countToUse)
{
    auto stream = std::ostringstream {};
    stream << "{";

    for (auto i = 0; i < countToUse; ++i)
    {
        if (i > 0)
            stream << ",";

        stream << '"' << generateVocabularyKey(i) << "\":" << i;
    }

    stream << "}";
    return stream.str();
}

std::vector<std::string> generateVocabularyKeys(int countToUse)
{
    auto keys = std::vector<std::string> {};
    keys.reserve(static_cast<std::size_t>(countToUse));

    for (auto i = 0; i < countToUse; ++i)
        keys.push_back(generateVocabularyKey(i));

    return keys;
}

void runBenchmark(const std::string& nameToUse,
                  const std::string& jsonToUse,
                  int iterationsToUse)
{
    std::cout << nameToUse << " (" << (jsonToUse.size() / 1024) << " KB):\n";

    auto miro = measure(
        "Miro", iterationsToUse, [&] { auto v = Miro::Json::parse(jsonToUse); });

    auto nlohmann = measure("nlohmann",
                            iterationsToUse,
                            [&] { auto v = nlohmann::json::parse(jsonToUse); });

    report(miro, nlohmann);
}

void runLookupBenchmark(const std::string& nameToUse,
                        const std::string& jsonToUse,
                        const std::vector<std::string>& keysToUse,
                        int iterationsToUse)
{
    std::cout << nameToUse << " (" << keysToUse.size() << " keys):\n";

    auto miroValue = Miro::Json::parse(jsonToUse);
    auto& miroObject = miroValue.asObject();
    auto nlohmannValue = nlohmann::json::parse(jsonToUse);

    auto found = std::size_t {};

    auto miro = measure("Miro",
                        iterationsToUse,
                        [&]
                        {
                            for (const auto& key: keysToUse)
                            {
                                auto view = std::string_view {key};

                                if (Miro::Json::find(miroObject, view) != nullptr)
                                    ++found;
                            }
                        });

    auto nlohmann =
        measure("nlohmann",
                iterationsToUse,
                [&]
                {
                    for (const auto& key: keysToUse)
                    {
                        auto view = std::string_view {key};

                        if (nlohmannValue.find(view) != nlohmannValue.end())
                            ++found;
                    }
                });

    report(miro, nlohmann);

    if (found != keysToUse.size() * static_cast<std::size_t>(iterationsToUse) * 2)
        std::cout << "  WARNING: lookups did not all hit\n";
}

int main()
{
    std::cout << "=== JSON Parse Benchmark ===\n\n";

    auto largeArray = generateLargeArray(100000);
    runBenchmark("Large number array", largeArray, 20);

    auto objectArray = generateObjectArray(10000);
    runBenchmark("Array of objects", objectArray, 20);

    auto deepNested = generateDeepNested(500);
    runBenchmark("Deeply nested object", deepNested, 1000);

    auto stringHeavy = generateStringHeavy(10000);
    runBenchmark("String-heavy object", stringHeavy, 20);

    auto vocabulary = generateVocabulary(50000);
    runBenchmark("Large flat object", vocabulary, 20);

    auto vocabularyKeys = generateVocabularyKeys(50000);
    runLookupBenchmark("Key lookup", vocabulary, vocabularyKeys, 20);

    return 0;
}
