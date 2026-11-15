#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <string>
#include <vector>

namespace PackedStructTestTypes
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    bool operator==(const Vec3&) const = default;

    MIRO_REFLECT(x, y, z)
};

struct Quaternion
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    bool operator==(const Quaternion&) const = default;

    MIRO_REFLECT(x, y, z, w)
};

struct Keyframe
{
    float time = 0.0f;
    Quaternion value;
    Quaternion inTangent;

    bool operator==(const Keyframe&) const = default;

    MIRO_REFLECT(time, value, inTangent)
};

struct ReorderedFields
{
    std::int32_t first = 0;
    double second = 0.0;

    MIRO_REFLECT(second, first)
};

struct RenamedFields
{
    std::uint16_t width = 0;
    std::uint16_t height = 0;

    MIRO_REFLECT_MEMBERS(width, "w", height, "h")
};

struct Path3
{
    std::vector<Vec3> points;

    MIRO_REFLECT(points)
};

struct WithText
{
    float x = 0.0f;
    std::string label;

    MIRO_REFLECT(x, label)
};

struct WithFlag
{
    float x = 0.0f;
    bool isVisible = true;

    MIRO_REFLECT(x, isVisible)
};

struct DerivedWithoutOwnList : Vec3
{
    template <std::derived_from<Miro::Reflector> R>
    void reflect(R& ref)
    {
        Vec3::reflect(ref);
    }
};

struct HandWritten
{
    std::int64_t id = 0;
    double weight = 0.0;

    template <std::derived_from<Miro::Reflector> R>
    void reflect(R& ref)
    {
        ref["id"](id);
        ref["weight"](weight);
    }
};

} // namespace PackedStructTestTypes

MIRO_PACKED(PackedStructTestTypes::HandWritten, id, "id", weight, "weight")

namespace PackedStructTestTypes
{

struct HandWrittenNotDeclared
{
    float x = 0.0f;

    template <std::derived_from<Miro::Reflector> R>
    void reflect(R& ref)
    {
        ref["x"](x);
    }
};

} // namespace PackedStructTestTypes
