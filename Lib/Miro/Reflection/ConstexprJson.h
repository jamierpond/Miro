#pragma once

#include "../JSON/Generic.h"
#include "ReflectDispatch.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Compile-time JSON serde backend.
//
// Json::Value can't exist in a constant expression (std::map is not
// constexpr), so this provides a constexpr mirror of the document
// model — object members are kept key-sorted, matching std::map
// iteration order — plus a constexpr port of JsonReflector. The
// parser and printer are NOT duplicated here: the generic templates
// in JSON/Generic.h are instantiated with the mirror Value, so the
// compile-time path runs the same algorithms as the runtime path and
// produces identical bytes by construction. The public serde entry
// points (toJSONString / fromJSONString / createFromJSONString in
// Serialize.h) route here when constant-evaluated.

namespace Miro::Detail::ConstexprJson
{

struct Member;

// Constexpr mirror of Json::Value: same read accessors and scalar
// constructors (so generic code compiles identically against both),
// with public alternative fields instead of a variant.
struct Value
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    // Defined below, after Member is complete: a user-provided
    // constructor odr-uses the vector<Member> member's destructor for
    // cleanup, which needs the complete type.
    Value() = default;
    constexpr Value(std::nullptr_t);
    constexpr Value(bool valueToUse);
    constexpr Value(double valueToUse);
    constexpr Value(std::string valueToUse);

    constexpr bool isNull() const { return kind == Kind::Null; }
    constexpr bool isBool() const { return kind == Kind::Bool; }
    constexpr bool isNumber() const { return kind == Kind::Number; }
    constexpr bool isString() const { return kind == Kind::String; }
    constexpr bool isArray() const { return kind == Kind::Array; }
    constexpr bool isObject() const { return kind == Kind::Object; }

    constexpr bool asBool() const { return boolValue; }
    constexpr double asNumber() const { return numberValue; }
    constexpr const std::string& asString() const { return stringValue; }
    constexpr const std::vector<Value>& asArray() const { return arrayValue; }
    constexpr const std::vector<Member>& asObject() const { return objectValue; }

    Kind kind = Kind::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<Value> arrayValue;
    std::vector<Member> objectValue;
};

struct Member
{
    std::string key;
    Value value;
};

constexpr Value::Value(std::nullptr_t) {}

constexpr Value::Value(bool valueToUse)
    : kind(Kind::Bool)
    , boolValue(valueToUse)
{
}

constexpr Value::Value(double valueToUse)
    : kind(Kind::Number)
    , numberValue(valueToUse)
{
}

constexpr Value::Value(std::string valueToUse)
    : kind(Kind::String)
    , stringValue(std::move(valueToUse))
{
}

using Array = std::vector<Value>;
using Object = std::vector<Member>;

constexpr void resetTo(Value& slot, Value::Kind kindToUse)
{
    slot = Value {};
    slot.kind = kindToUse;
}

constexpr Value* find(Object& members, std::string_view key)
{
    for (auto& member: members)
        if (member.key == key)
            return &member.value;

    return nullptr;
}

// Sorted insert returning the slot for `key` — the existing one when
// present (std::map::operator[] semantics for the save path).
constexpr Value& findOrInsert(Object& members, std::string_view key)
{
    auto index = std::size_t {0};

    while (index < members.size() && members[index].key < key)
        ++index;

    if (index >= members.size() || members[index].key != key)
        members.insert(members.begin() + static_cast<std::ptrdiff_t>(index),
                       Member {std::string {key}, Value {}});

    return members[index].value;
}

// Construction seams used by the generic parsers (see
// Detail/DocumentOps.h for the runtime Json::Value overloads).
// Insertion is sorted and first-occurrence-wins, matching
// std::map::emplace iteration order and semantics.

constexpr void setEmptyArray(Value& value)
{
    resetTo(value, Value::Kind::Array);
}

constexpr void setEmptyObject(Value& value)
{
    resetTo(value, Value::Kind::Object);
}

constexpr void reserveArray(Value& value, std::size_t count)
{
    value.arrayValue.reserve(count);
}

constexpr void appendElement(Value& value, Value element)
{
    value.arrayValue.push_back(std::move(element));
}

constexpr void insertMember(Value& value, std::string key, Value member)
{
    auto& members = value.objectValue;
    auto index = std::size_t {0};

    while (index < members.size() && members[index].key < key)
        ++index;

    if (index < members.size() && members[index].key == key)
        return;

    members.insert(members.begin() + static_cast<std::ptrdiff_t>(index),
                   Member {std::move(key), std::move(member)});
}

// Sort parsed members by key (matching std::map iteration order, so the
// printer is byte-identical to the runtime Json::Value path) and drop
// later duplicates, mirroring std::map::emplace's first-occurrence-wins.
//
// The parser appends members in document order and calls this once per
// object — O(n log n). It replaces a per-member sorted insert that was
// O(n^2): each insert shifted every later Member (a string + two
// vectors) down one slot, so a 50k-entry vocab.json move-constructed
// ~1.3 billion of them in the constexpr interpreter — minutes of
// compile time. std::sort is constexpr in C++20; std::stable_sort is
// not, so the comparator breaks key ties by original index to keep the
// first occurrence.
constexpr void sortObjectMembers(std::vector<Member>& members)
{
    const auto count = members.size();

    auto order = std::vector<std::size_t>(count);
    for (auto i = std::size_t {0}; i < count; ++i)
        order[i] = i;

    std::sort(order.begin(),
              order.end(),
              [&](std::size_t a, std::size_t b)
              {
                  if (members[a].key != members[b].key)
                      return members[a].key < members[b].key;
                  return a < b;
              });

    auto sorted = std::vector<Member> {};
    sorted.reserve(count);
    for (const auto index: order)
    {
        if (!sorted.empty() && sorted.back().key == members[index].key)
            continue;
        sorted.push_back(std::move(members[index]));
    }

    members = std::move(sorted);
}

// --- Reflector ---

template <typename T>
constexpr void writeSlotFromPrimitive(Value& slot, const T& valueToUse)
{
    if constexpr (std::same_as<T, bool>)
    {
        resetTo(slot, Value::Kind::Bool);
        slot.boolValue = valueToUse;
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        resetTo(slot, Value::Kind::String);
        slot.stringValue = valueToUse;
    }
    else
    {
        resetTo(slot, Value::Kind::Number);
        slot.numberValue = static_cast<double>(valueToUse);
    }
}

template <typename T>
constexpr void readSlotIntoPrimitive(const Value& slot, T& valueToUse)
{
    if constexpr (std::same_as<T, bool>)
    {
        if (slot.kind == Value::Kind::Bool)
            valueToUse = slot.boolValue;
    }
    else if constexpr (std::same_as<T, std::string>)
    {
        if (slot.kind == Value::Kind::String)
            valueToUse = slot.stringValue;
    }
    else
    {
        if (slot.kind == Value::Kind::Number)
            valueToUse = static_cast<T>(slot.numberValue);
    }
}

// Constexpr port of JsonReflector: one slot per reflector, shape
// committed eagerly when saving, children spawned via atKey/atIndex
// and owned until the next spawn. Loading from a missing key/index
// goes through an `absent` child whose operations no-op.
//
// Deliberately a class template (always instantiated as
// BasicValueReflector<Value>): a plain class would have its member
// bodies compiled at header-parse time, leaving the template
// specializations they reference (get_if, string ops, the visit
// helpers) in clang's pending-instantiation queue until end of TU —
// which a static_assert in user code evaluates before. As a template
// instantiated from toText/fromText, the whole body — including the
// virtuals, via the vtable — is instantiated at the point of use,
// inside the constant-evaluation context, which instantiates the
// helpers eagerly. std::visit is avoided for the same reason: its
// function-pointer dispatch matrix hides the callee from on-demand
// instantiation entirely.
template <typename SlotValue>
class BasicValueReflector final : public Reflector
{
public:
    using Kind = typename SlotValue::Kind;

    constexpr BasicValueReflector(SlotValue& slotToUse, Options optsToUse)
        : BasicValueReflector(slotToUse, optsToUse, false)
    {
    }

    constexpr ~BasicValueReflector() override { delete currentChild; }

    constexpr void visit(PrimitiveRef ref) override
    {
        visitAlternative<bool>(ref) || visitAlternative<int>(ref)
            || visitAlternative<double>(ref) || visitAlternative<std::string>(ref)
            || visitAlternative<std::int64_t>(ref);
    }

    constexpr void writeNull() override { resetTo(slot, Kind::Null); }

    constexpr ValueKind kind() const override
    {
        if (absent)
            return ValueKind::Absent;

        switch (slot.kind)
        {
            case Kind::Null:
                return ValueKind::Null;
            case Kind::Bool:
                return ValueKind::Bool;
            case Kind::Number:
                return ValueKind::Number;
            case Kind::String:
                return ValueKind::String;
            case Kind::Array:
                return ValueKind::Array;
            case Kind::Object:
                return ValueKind::Object;
        }

        return ValueKind::Absent;
    }

    constexpr Reflector& atKey(std::string_view key, Options childOpts) override
    {
        if (isSaving())
        {
            if (!slot.isObject())
                resetTo(slot, Kind::Object);

            return spawnChild(findOrInsert(slot.objectValue, key), childOpts, false);
        }

        if (!slot.isObject())
            return spawnMissingChild(childOpts);

        if (auto* found = find(slot.objectValue, key))
            return spawnChild(*found, childOpts, false);

        return spawnMissingChild(childOpts);
    }

    constexpr Reflector& atIndex(std::size_t index, Options childOpts) override
    {
        if (isSaving())
        {
            if (!slot.isArray())
                resetTo(slot, Kind::Array);

            if (slot.arrayValue.size() <= index)
                slot.arrayValue.resize(index + 1);

            return spawnChild(slot.arrayValue[index], childOpts, false);
        }

        if (!slot.isArray() || index >= slot.arrayValue.size())
            return spawnMissingChild(childOpts);

        return spawnChild(slot.arrayValue[index], childOpts, false);
    }

    constexpr std::size_t arraySize() const override
    {
        return slot.isArray() ? slot.arrayValue.size() : 0;
    }

    constexpr void resizeArray(std::size_t newSize) override
    {
        if (!slot.isArray())
            resetTo(slot, Kind::Array);

        slot.arrayValue.resize(newSize);
    }

private:
    constexpr BasicValueReflector(SlotValue& slotToUse,
                                  Options optsToUse,
                                  bool absentToUse)
        : Reflector(optsToUse)
        , slot(slotToUse)
        , absent(absentToUse)
    {
        if (isSaving() && !absent)
            commitShape();
    }

    constexpr void commitShape()
    {
        switch (opts.shape)
        {
            case Shape::Primitive:
                break;
            case Shape::Object:
            case Shape::Map:
                if (!slot.isObject())
                    resetTo(slot, Kind::Object);
                break;
            case Shape::Array:
                if (!slot.isArray())
                    resetTo(slot, Kind::Array);
                break;
        }
    }

    template <typename T>
    constexpr bool visitAlternative(PrimitiveRef& ref)
    {
        auto* pointer = std::get_if<T*>(&ref.data);

        if (pointer == nullptr)
            return false;

        if (isSaving())
            writeSlotFromPrimitive(slot, **pointer);
        else
            readSlotIntoPrimitive(slot, **pointer);

        return true;
    }

    constexpr Reflector&
        spawnChild(SlotValue& targetSlot, Options childOpts, bool absentToUse)
    {
        delete currentChild;
        currentChild = nullptr;
        currentChild = new BasicValueReflector(targetSlot, childOpts, absentToUse);
        return *currentChild;
    }

    constexpr Reflector& spawnMissingChild(Options childOpts)
    {
        missingSlot = SlotValue {};
        return spawnChild(missingSlot, childOpts, true);
    }

    SlotValue& slot;
    bool absent = false;
    SlotValue missingSlot;
    BasicValueReflector* currentChild = nullptr;
};

using ValueReflector = BasicValueReflector<Value>;

// --- Document text via the shared generic JSON parser / printer ---

constexpr Value parse(std::string_view inputToUse)
{
    return Json::Generic::parse<Value>(inputToUse);
}

constexpr std::string print(const Value& valueToUse, int indentToUse)
{
    return Json::Generic::print(valueToUse, indentToUse);
}

// --- Parser (mirrors JSON/Parser.cpp) ---

// Clamped power-of-ten scaling for number conversion. When the
// decimal mantissa fits exactly (≤ 2^53) and the net exponent is
// within ±22 this is a single correctly-rounded multiply or divide —
// the strtod fast path — so typical values convert identically to
// the runtime parser. Larger magnitudes scale in chunks and may
// drift by an ulp.
constexpr double scalePowerOfTen(double valueToUse, int exponentToUse)
{
    constexpr double powers[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                 1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

    constexpr auto maxValue = std::numeric_limits<double>::max();
    constexpr auto infinity = std::numeric_limits<double>::infinity();

    if (valueToUse == 0.0)
        return valueToUse;

    while (exponentToUse > 22)
    {
        if (valueToUse >= maxValue / 1e22)
            return infinity;

        valueToUse *= 1e22;
        exponentToUse -= 22;
    }

    while (exponentToUse < -22)
    {
        valueToUse /= 1e22;
        exponentToUse += 22;
    }

    if (exponentToUse > 0)
    {
        auto factor = powers[exponentToUse];

        if (valueToUse >= maxValue / factor)
            return infinity;

        return valueToUse * factor;
    }

    if (exponentToUse < 0)
        return valueToUse / powers[-exponentToUse];

    return valueToUse;
}

constexpr int hexDigitValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

constexpr void appendUtf8(std::string& result, unsigned codepoint)
{
    if (codepoint <= 0x7F)
    {
        result += static_cast<char>(codepoint);
    }
    else if (codepoint <= 0x7FF)
    {
        result += static_cast<char>(0xC0 | (codepoint >> 6));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else
    {
        result += static_cast<char>(0xE0 | (codepoint >> 12));
        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

class Parser
{
public:
    constexpr explicit Parser(std::string_view inputToUse)
        : input(inputToUse)
    {
    }

    constexpr Value parseValue()
    {
        skipWhitespaceAndComments();

        if (atEnd())
            error("unexpected end of input");

        auto c = peek();

        if (c == '"')
            return parseString();
        if (c == '{')
            return parseObject();
        if (c == '[')
            return parseArray();
        if (c == 't' || c == 'f')
            return parseBool();
        if (c == 'n')
            return parseNull();
        if (c == '-' || isDigit(c))
            return parseNumber();

        error("unexpected character");
    }

    constexpr bool atEnd() const { return pos >= input.size(); }

    constexpr void skipWhitespaceAndComments()
    {
        while (!atEnd())
        {
            auto c = peek();

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos;
            else if (c == '/')
                skipComment();
            else
                break;
        }
    }

    // Always throws. Deliberately not constexpr: reaching it during
    // constant evaluation fails compilation right here, with this
    // frame at the top of the evaluation trace.
    [[noreturn]] void error(std::string_view messageToUse) const
    {
        auto message = std::string {"JSON parse error at position "};
        appendInteger(message, static_cast<long long>(pos));
        message += ": ";
        message += messageToUse;
        throw Json::ParseError(message);
    }

private:
    static constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }

    constexpr char peek() const { return input[pos]; }

    constexpr void expect(char charToUse)
    {
        if (atEnd() || peek() != charToUse)
            error("expected different character");

        ++pos;
    }

    constexpr void expectKeyword(std::string_view keywordToUse)
    {
        if (input.size() - pos < keywordToUse.size()
            || input.substr(pos, keywordToUse.size()) != keywordToUse)
            error("expected keyword");

        pos += keywordToUse.size();
    }

    constexpr void skipComment()
    {
        if (input.size() - pos < 2
            || (input[pos + 1] != '/' && input[pos + 1] != '*'))
            error("unexpected character '/'");

        if (input[pos + 1] == '/')
        {
            pos += 2;

            while (!atEnd() && peek() != '\n' && peek() != '\r')
                ++pos;

            return;
        }

        pos += 2;

        while (pos + 1 < input.size())
        {
            if (input[pos] == '*' && input[pos + 1] == '/')
            {
                pos += 2;
                return;
            }

            ++pos;
        }

        error("unterminated block comment");
    }

    // --- Primitives ---

    constexpr Value parseNull()
    {
        expectKeyword("null");
        return {};
    }

    constexpr Value parseBool()
    {
        auto result = Value {};
        result.kind = Value::Kind::Bool;

        if (peek() == 't')
        {
            expectKeyword("true");
            result.boolValue = true;
        }
        else
        {
            expectKeyword("false");
            result.boolValue = false;
        }

        return result;
    }

    constexpr Value parseNumber()
    {
        auto negative = false;

        if (peek() == '-')
        {
            negative = true;
            ++pos;
        }

        if (atEnd())
            error("unexpected end of number");

        auto mantissa = std::uint64_t {0};
        auto scale = 0;

        if (peek() == '0')
        {
            ++pos;
        }
        else if (isDigit(peek()))
        {
            while (!atEnd() && isDigit(peek()))
            {
                accumulateDigit(mantissa, scale, peek(), false);
                ++pos;
            }
        }
        else
        {
            error("invalid number");
        }

        if (!atEnd() && peek() == '.')
        {
            ++pos;

            if (atEnd() || !isDigit(peek()))
                error("expected digit after decimal point");

            while (!atEnd() && isDigit(peek()))
            {
                accumulateDigit(mantissa, scale, peek(), true);
                ++pos;
            }
        }

        auto explicitExponent = 0;

        if (!atEnd() && (peek() == 'e' || peek() == 'E'))
        {
            ++pos;
            auto exponentNegative = false;

            if (!atEnd() && (peek() == '+' || peek() == '-'))
            {
                exponentNegative = peek() == '-';
                ++pos;
            }

            if (atEnd() || !isDigit(peek()))
                error("expected digit in exponent");

            while (!atEnd() && isDigit(peek()))
            {
                if (explicitExponent < 100000)
                    explicitExponent = explicitExponent * 10 + (peek() - '0');

                ++pos;
            }

            if (exponentNegative)
                explicitExponent = -explicitExponent;
        }

        auto number =
            scalePowerOfTen(static_cast<double>(mantissa), scale + explicitExponent);

        auto result = Value {};
        result.kind = Value::Kind::Number;
        result.numberValue = negative ? -number : number;
        return result;
    }

    // Accumulates one decimal digit into the mantissa, tracking the
    // power of ten carried by digits beyond 64-bit range (kept as
    // exponent bumps) and by fraction digits (exponent drops).
    static constexpr void accumulateDigit(std::uint64_t& mantissa,
                                          int& scale,
                                          char digitToUse,
                                          bool isFraction)
    {
        constexpr auto limit = (std::numeric_limits<std::uint64_t>::max() - 9) / 10;

        if (mantissa <= limit)
        {
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(digitToUse - '0');

            if (isFraction)
                --scale;
        }
        else if (!isFraction)
        {
            ++scale;
        }
    }

    // --- Strings ---

    constexpr Value parseString()
    {
        auto result = Value {};
        result.kind = Value::Kind::String;
        result.stringValue = parseStringRaw();
        return result;
    }

    constexpr std::string parseStringRaw()
    {
        expect('"');

        auto result = std::string {};

        while (!atEnd() && peek() != '"')
        {
            if (peek() == '\\')
            {
                parseEscapeSequence(result);
            }
            else
            {
                result += peek();
                ++pos;
            }
        }

        if (atEnd())
            error("expected '\"' but reached end of input");

        ++pos;
        return result;
    }

    constexpr void parseEscapeSequence(std::string& result)
    {
        ++pos;

        if (atEnd())
            error("unexpected end of string escape");

        auto escaped = peek();
        ++pos;

        switch (escaped)
        {
            case '"':
                result += '"';
                break;
            case '\\':
                result += '\\';
                break;
            case '/':
                result += '/';
                break;
            case 'b':
                result += '\b';
                break;
            case 'f':
                result += '\f';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                result += '\r';
                break;
            case 't':
                result += '\t';
                break;
            case 'u':
                parseUnicodeEscape(result);
                break;
            default:
                error("invalid escape character");
        }
    }

    constexpr void parseUnicodeEscape(std::string& result)
    {
        if (input.size() - pos < 4)
            error("unexpected end of unicode escape");

        auto codepoint = unsigned {0};

        for (auto i = std::size_t {0}; i < 4; ++i)
        {
            auto digit = hexDigitValue(input[pos + i]);

            if (digit < 0)
                error("invalid unicode escape");

            codepoint = codepoint * 16 + static_cast<unsigned>(digit);
        }

        pos += 4;
        appendUtf8(result, codepoint);
    }

    // --- Containers ---

    constexpr Value parseArray()
    {
        expect('[');
        skipWhitespaceAndComments();

        auto result = Value {};
        result.kind = Value::Kind::Array;

        if (!atEnd() && peek() != ']')
        {
            result.arrayValue.push_back(parseValue());
            skipWhitespaceAndComments();

            while (!atEnd() && peek() == ',')
            {
                ++pos;
                result.arrayValue.push_back(parseValue());
                skipWhitespaceAndComments();
            }
        }

        expect(']');
        return result;
    }

    constexpr Value parseObject()
    {
        expect('{');
        skipWhitespaceAndComments();

        auto result = Value {};
        result.kind = Value::Kind::Object;

        if (!atEnd() && peek() != '}')
        {
            parseMemberInto(result.objectValue);

            while (!atEnd() && peek() == ',')
            {
                ++pos;
                parseMemberInto(result.objectValue);
            }
        }

        expect('}');
        sortObjectMembers(result.objectValue);
        return result;
    }

    // Append in document order — O(1) amortized. The object is sorted and
    // deduplicated once in parseObject via sortObjectMembers; doing the
    // sorted insert here instead was O(n^2) and made large objects (a
    // 50k-entry vocab.json) take many minutes to parse in a constant
    // evaluation.
    constexpr void parseMemberInto(std::vector<Member>& members)
    {
        skipWhitespaceAndComments();
        auto key = parseStringRaw();
        skipWhitespaceAndComments();
        expect(':');
        auto value = parseValue();
        members.push_back(Member {std::move(key), std::move(value)});
        skipWhitespaceAndComments();
    }

    std::string_view input;
    std::size_t pos = 0;
};


template <typename T>
constexpr Value reflectToValue(const T& value)
{
    auto root = Value {};
    auto ref = ValueReflector {root, topLevelOptions<T>(Mode::Save)};

    using Detail::reflectValue;
    reflectValue(ref, const_cast<T&>(value));

    return root;
}

template <typename T>
constexpr void loadFromValue(T& value, Value root)
{
    auto ref = ValueReflector {root, topLevelOptions<T>(Mode::Load)};

    using Detail::reflectValue;
    reflectValue(ref, value);
}

// --- Entry points (called from Serialize.h during constant
// evaluation) ---

template <typename T>
constexpr std::string toText(const T& value, int indent)
{
    return Json::Generic::print(reflectToValue(value), indent);
}

template <typename T>
constexpr void fromText(T& value, std::string_view text)
{
    loadFromValue(value, parse(text));
}

} // namespace Miro::Detail::ConstexprJson
