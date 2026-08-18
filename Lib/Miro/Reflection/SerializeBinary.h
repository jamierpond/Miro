#pragma once

#include "BinaryReflector.h"
#include "ReflectContainers.h"
#include "ReflectDispatch.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// Binary serialization helpers — the compact counterparts of toJSON /
// fromJSON in Serialize.h. Same CustomOptions plumbing, same
// non-throwing load semantics: missing keys and mismatched types leave
// fields at their prior values, corrupt input degrades to defaults.
//
// The journal entry points (toBinaryJournal, binaryJournalSet /
// binaryJournalRemove) produce the append-friendly "MIBJ" encoding: a
// full base document plus per-entry frames a store can append to a
// file without rewriting it. fromBinary reads journals transparently —
// base first, then frames replayed in order for map-shaped targets.

namespace Miro
{
namespace Detail
{

// The types journal frames can key into. Frames replay only into
// std::map<std::string, V>; every other target reads the base document
// and ignores the tail.
template <typename T>
struct JournalMapOf : std::false_type
{
};

template <typename V>
struct JournalMapOf<std::map<std::string, V>> : std::true_type
{
    using Value = V;
};

// Loads one plain encoded document into `value`. May throw on
// allocation failure — fromBinary wraps it.
template <typename T>
void loadBinaryDocument(T& value,
                        std::span<const std::uint8_t> data,
                        const CustomOptions& custom)
{
    auto doc = Binary::Document {data};

    if (!doc.isValid())
        return;

    auto ref = BinaryReaderReflector {
        doc,
        doc.rootOffset(),
        Detail::topLevelOptions<T>(Mode::Load, /*schema=*/false, custom)};
    Detail::reflectValue(ref, value);
}

} // namespace Detail

template <typename T>
Binary::Buffer toBinary(const T& value, CustomOptions custom = {})
{
    auto writer = Binary::Writer {};

    {
        auto ref = BinaryWriterReflector {
            writer,
            Detail::topLevelOptions<T>(
                Mode::Save, /*schema=*/false, std::move(custom))};
        Detail::reflectValue(ref, const_cast<T&>(value));
    }

    return writer.finish();
}

// Never throws (see fromJSON): on any fault `value` keeps whatever was
// populated before it. Reads both wire formats: a plain document loads
// directly; a journal loads its base and then replays the frames (for
// map-shaped T — anything else gets the base alone).
template <typename T>
void fromBinary(T& value,
                std::span<const std::uint8_t> data,
                CustomOptions custom = {})
{
    try
    {
        if (Binary::isJournal(data))
        {
            const auto info = Binary::inspectJournal(data);

            if (!info.valid)
                return;

            Detail::loadBinaryDocument(
                value, data.subspan(info.baseStart, info.baseSize), custom);

            if constexpr (Detail::JournalMapOf<T>::value)
            {
                using Value = typename Detail::JournalMapOf<T>::Value;

                for (const auto& frame: Binary::journalFrames(data))
                {
                    auto key = std::string {frame.key};

                    if (frame.kind == Binary::FrameKind::Set)
                    {
                        auto element = Value {};
                        Detail::loadBinaryDocument(element, frame.value, custom);
                        value[key] = std::move(element);
                    }
                    else
                    {
                        value.erase(key);
                    }
                }
            }

            return;
        }

        Detail::loadBinaryDocument(value, data, custom);
    }
    catch (...)
    {
        // Intentionally swallowed — this function never throws.
    }
}

// Non-throwing: a fault leaves the returned value with whatever was
// populated before it, starting from a default T {}.
template <typename T>
T createFromBinary(std::span<const std::uint8_t> data, CustomOptions custom = {})
{
    auto value = T {};
    fromBinary(value, data, std::move(custom));
    return value;
}

// The value as a fresh journal: a full base document with no frames
// yet. Append binaryJournalSet / binaryJournalRemove frames to it (or
// to the file holding it) and fromBinary replays them in order.
template <typename T>
Binary::Buffer toBinaryJournal(const T& value, CustomOptions custom = {})
{
    return Binary::makeJournal(toBinary(value, std::move(custom)));
}

// One "map[key] = value" insert as an appendable frame.
template <typename V>
Binary::Buffer
    binaryJournalSet(std::string_view key, const V& value, CustomOptions custom = {})
{
    return Binary::makeSetFrame(key, toBinary(value, std::move(custom)));
}

// One "map.erase(key)" tombstone as an appendable frame.
inline Binary::Buffer binaryJournalRemove(std::string_view key)
{
    return Binary::makeRemoveFrame(key);
}

} // namespace Miro
