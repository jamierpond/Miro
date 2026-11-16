#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace Miro::Detail
{

// Extracts a compile-time, compiler-derived name for type T from
// __PRETTY_FUNCTION__ / __FUNCSIG__. Same trick as enumNameRaw in
// ReflectEnum.h, generalized to any T. Returns the spelling the
// compiler chose for the template argument (may include namespace
// qualifiers and template arguments).
template <typename T>
constexpr std::string_view rawTypeNameOf()
{
#if defined(__clang__) || defined(__GNUC__)
    constexpr auto prefix = std::string_view {"T = "};
    constexpr auto terminators = std::string_view {";]"};
    auto sig = std::string_view {__PRETTY_FUNCTION__};
    auto start = sig.find(prefix) + prefix.size();
    auto end = sig.find_first_of(terminators, start);
    return sig.substr(start, end - start);
#elif defined(_MSC_VER)
    constexpr auto prefix = std::string_view {"rawTypeNameOf<"};
    constexpr auto terminator = std::string_view {">("};
    auto sig = std::string_view {__FUNCSIG__};
    auto start = sig.find(prefix) + prefix.size();
    auto end = sig.find(terminator, start);
    return sig.substr(start, end - start);
#else
    return {};
#endif
}

// Like rawTypeNameOf but strips the leading C++ class-key keyword that
// MSVC's __FUNCSIG__ emits ("struct ", "class ", "enum "). All namespace
// qualifiers and template arguments are preserved. This is the canonical
// spelling to compare or hash across compilers — Clang/GCC don't emit
// the keyword in __PRETTY_FUNCTION__, so this is a no-op there.
template <typename T>
constexpr std::string_view parseQualifiedNameOf()
{
    auto raw = rawTypeNameOf<T>();
    constexpr auto prefixes = std::array {
        std::string_view {"struct "},
        std::string_view {"class "},
        std::string_view {"enum "},
    };
    for (auto p: prefixes)
        if (raw.starts_with(p))
            return raw.substr(p.size());
    return raw;
}

// Parsing __PRETTY_FUNCTION__ costs about 100 ns, so each type's name
// is parsed once, at compile time, instead of on every object reflected.
template <typename T>
inline constexpr auto qualifiedNameOfType = parseQualifiedNameOf<T>();

template <typename T>
constexpr std::string_view qualifiedNameOf()
{
    return qualifiedNameOfType<T>;
}

constexpr bool isScopeSeparatorAt(std::string_view nameToUse, std::size_t index)
{
    return nameToUse[index] == ':' && nameToUse[index + 1] == ':';
}

// The rightmost "::" that is not inside template angle brackets, or npos.
constexpr std::size_t
    findLastScopeSeparatorOutsideTemplateArguments(std::string_view nameToUse)
{
    auto depth = 0;
    auto lastSeparator = std::string_view::npos;

    for (auto i = std::size_t {0}; i + 1 < nameToUse.size(); ++i)
    {
        if (nameToUse[i] == '<')
            ++depth;
        else if (nameToUse[i] == '>' && depth > 0)
            --depth;
        else if (depth == 0 && isScopeSeparatorAt(nameToUse, i))
            lastSeparator = i;
    }

    return lastSeparator;
}

// Returns just the unqualified short name. Strips namespaces (incl.
// "(anonymous namespace)::"). Templates and STL types are deliberately
// left intact in the raw form — callers that want to filter them can
// inspect for '<' / "std::" themselves.
template <typename T>
constexpr std::string_view parseTypeNameOf()
{
    auto name = qualifiedNameOf<T>();
    auto lastSeparator = findLastScopeSeparatorOutsideTemplateArguments(name);

    if (lastSeparator != std::string_view::npos)
        name.remove_prefix(lastSeparator + 2);

    return name;
}

template <typename T>
inline constexpr auto typeNameOfType = parseTypeNameOf<T>();

template <typename T>
constexpr std::string_view typeNameOf()
{
    return typeNameOfType<T>;
}

// Compile-time extraction of a pointer-to-member's short name from
// __PRETTY_FUNCTION__ / __FUNCSIG__. Same trick as enumNameRaw, but
// with the NTTP rendered as a qualified `&Class::name` (or
// `&Class<T>::name`). Used by ApiReflector to derive command / event
// names from `&Foo::bar`-style template arguments so the user doesn't
// have to repeat the name as a string.
template <auto Member>
constexpr std::string_view memberNameOf()
{
#if defined(__clang__) || defined(__GNUC__)
    constexpr auto prefix = std::string_view {"Member = "};
    constexpr auto terminators = std::string_view {";]"};
    auto sig = std::string_view {__PRETTY_FUNCTION__};
    auto start = sig.find(prefix) + prefix.size();
    auto end = sig.find_first_of(terminators, start);
#elif defined(_MSC_VER)
    constexpr auto prefix = std::string_view {"memberNameOf<"};
    constexpr auto terminator = std::string_view {">("};
    auto sig = std::string_view {__FUNCSIG__};
    auto start = sig.find(prefix) + prefix.size();
    auto end = sig.find(terminator, start);
#else
    auto sig = std::string_view {};
    auto start = std::size_t {};
    auto end = std::size_t {};
#endif

    auto name = sig.substr(start, end - start);

    // Drop the leading '&' that compilers prepend for pmf / pmd NTTPs.
    if (!name.empty() && name.front() == '&')
        name.remove_prefix(1);

    // Find the rightmost "::" at zero depth (ignoring template-argument
    // angle brackets in the class qualifier and parens that wrap
    // "(anonymous namespace)") — that separates the class qualifier
    // from the actual member name.
    auto angleDepth = 0;
    auto parenDepth = 0;
    auto lastColon = std::string_view::npos;

    for (auto i = std::size_t {0}; i + 1 < name.size(); ++i)
    {
        auto c = name[i];
        if (c == '<')
            ++angleDepth;
        else if (c == '>' && angleDepth > 0)
            --angleDepth;
        else if (c == '(')
            ++parenDepth;
        else if (c == ')' && parenDepth > 0)
            --parenDepth;
        else if (angleDepth == 0 && parenDepth == 0 && c == ':'
                 && name[i + 1] == ':')
            lastColon = i;
    }

    if (lastColon != std::string_view::npos)
        name.remove_prefix(lastColon + 2);

    // MSVC renders a pmf NTTP as the full function signature, so what
    // remains after trimming the qualifier is "method(paramList) cv".
    // Strip the parameter list (and any trailing cv-quals) so the result
    // is just the unqualified member name, matching the GCC/Clang output.
    if (auto paren = name.find('('); paren != std::string_view::npos)
        name = name.substr(0, paren);

    return name;
}

// True if T is a "named" user-defined type — i.e. not a template
// instantiation and not in the std namespace. Used by the TypeScript
// exporter to decide whether to register T as a reusable type or
// inline its shape.
template <typename T>
constexpr bool isNamedUserType()
{
    auto raw = rawTypeNameOf<T>();

    if (raw.find('<') != std::string_view::npos)
        return false;
    if (raw.find("std::") != std::string_view::npos)
        return false;
    if (raw.find("(anonymous") != std::string_view::npos)
        return true; // anonymous-namespace user types are still user types
    return true;
}

} // namespace Miro::Detail
