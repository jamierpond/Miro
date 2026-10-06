#include "TypeTree.h"

#include "../Detail/StringUtilities.h"

#include <concepts>
#include <map>
#include <type_traits>
#include <utility>

namespace Miro::TypeTree
{

// ----- TypeReflector --------------------------------------------------

TypeReflector::TypeReflector(TypeNode& nodeToUse,
                             Options optsToUse,
                             TypeReflector* parentToUse)
    : Reflector(optsToUse)
    , node(nodeToUse)
    , parent(parentToUse)
{
    switch (opts.shape)
    {
        case Shape::Primitive:
            node.shape = TypeNode::Shape::Primitive;
            break;
        case Shape::Object:
            node.shape = TypeNode::Shape::Object;
            break;
        case Shape::Array:
            node.shape = TypeNode::Shape::Array;
            node.inner = EA::makeOwned<TypeNode>();
            break;
        case Shape::Map:
            node.shape = TypeNode::Shape::Map;
            node.inner = EA::makeOwned<TypeNode>();
            break;
        case Shape::Raw:
            node.shape = TypeNode::Shape::Any;
            break;
    }

    node.optional = opts.nullable;
    node.omittable = opts.omittable;
}

TypeReflector::~TypeReflector() = default;

ValueKind TypeReflector::kind() const
{
    return ValueKind::Absent;
}

void TypeReflector::writeNull() {}

bool TypeReflector::beginNamedType(TypeId id)
{
    // Set the names regardless: even when this is a back edge, the
    // renderer needs them to print the type by name.
    node.typeName = std::string {id.shortName};
    node.qualifiedName = std::string {id.qualifiedName};

    // Walk the parent chain — if any ancestor is currently inside a body
    // for the same C++ type, this slot is a back edge and we must not
    // descend or we'll infinite-recurse. The slot becomes a name
    // reference; collectNamed dedups against the outer walk.
    for (auto* p = parent; p != nullptr; p = p->parent)
    {
        if (p->activeQualifiedName == id.qualifiedName)
            return false;
    }

    activeQualifiedName = std::string {id.qualifiedName};
    return true;
}

void TypeReflector::beginEnum(TypeId id)
{
    node.shape = TypeNode::Shape::Enum;
    node.typeName = std::string {id.shortName};
    node.qualifiedName = std::string {id.qualifiedName};
    node.enumValues.clear();
    node.enumNumbers.clear();
}

void TypeReflector::visitEnum(TypeId id, const Vector<std::string_view>& names)
{
    beginEnum(id);
    node.enumValues.reserve(names.size());

    for (auto& name: names)
        node.enumValues.emplace_back(name);
}

void TypeReflector::visitIntegerEnum(TypeId id, const Vector<EnumEntry>& entries)
{
    beginEnum(id);
    node.enumIsInteger = true;
    node.enumValues.reserve(entries.size());
    node.enumNumbers.reserve(entries.size());

    for (auto& entry: entries)
    {
        node.enumValues.emplace_back(entry.name);
        node.enumNumbers.add(entry.value);
    }
}

Reflector& TypeReflector::beginTaggedAlternative(std::string_view tagKey,
                                                 const TagLiteral& tag,
                                                 Options childOpts)
{
    // The slot was committed as an Object by the constructor (a tagged
    // union is an object on the wire); the first alternative promotes
    // it. Any plain fields the same reflect() body already wrote stay
    // in `fields` and renderers intersect them with the arms.
    node.shape = TypeNode::Shape::Union;
    node.tagKey = std::string {tagKey};

    node.variants.add(
        TypeNode::Variant {tag.text, tag.isString, EA::makeOwned<TypeNode>()});

    return spawnChild(*node.variants.back().type, childOpts);
}

void TypeReflector::visit(PrimitiveRef ref)
{
    std::visit(
        [this](auto* ptr)
        {
            using T = std::remove_pointer_t<decltype(ptr)>;
            if constexpr (std::same_as<T, bool>)
                node.primitive = PrimitiveKind::Boolean;
            else if constexpr (std::same_as<T, std::string>)
                node.primitive = PrimitiveKind::String;
            else if constexpr (std::same_as<T, double>)
                node.primitive = PrimitiveKind::Number;
            else if constexpr (std::same_as<T, std::int64_t>)
                node.primitive = PrimitiveKind::Int64;
            else
                node.primitive = PrimitiveKind::Integer;
        },
        ref.data);
}

Reflector& TypeReflector::spawnChild(TypeNode& targetNode, Options childOpts)
{
    currentChild.reset();
    currentChild.create(targetNode, childOpts, this);
    return *currentChild;
}

Reflector& TypeReflector::atKey(std::string_view key, Options childOpts)
{
    if (node.shape == TypeNode::Shape::Map)
        return spawnChild(*node.inner, childOpts);

    auto field = TypeNode::Field {std::string {key}, EA::makeOwned<TypeNode>()};
    node.fields.add(std::move(field));
    return spawnChild(*node.fields.back().type, childOpts);
}

Reflector& TypeReflector::atIndex(std::size_t, Options childOpts)
{
    return spawnChild(*node.inner, childOpts);
}

void TypeReflector::setArrayBounds(std::size_t min, std::size_t max)
{
    node.minItems = min;
    node.maxItems = max;
}

// ----- prepareRoots and helpers ---------------------------------------

namespace
{

// Identity used to dedup a node in collectNamed. Prefers the raw
// qualified name (always unique per C++ type); falls back to the short
// name for the rare anonymous-object case so an unnamed slot still
// pairs with itself if encountered twice.
const std::string& dedupKey(const TypeNode& node)
{
    return node.qualifiedName.empty() ? node.typeName : node.qualifiedName;
}

// Shapes that become their own top-level declaration when they carry a
// name. Everything else is always rendered inline at its use site.
bool isDeclarable(const TypeNode& node)
{
    return node.shape == TypeNode::Shape::Object
           || node.shape == TypeNode::Shape::Enum
           || node.shape == TypeNode::Shape::Union;
}

void collectNamed(const TypeNode& node,
                  Vector<std::string>& seen,
                  Vector<const TypeNode*>& outOrdered);

// Descends into every sub-node a shape can own. Only one of the three
// is ever populated for a given shape (except a Union carrying plain
// fields), so walking all of them is cheap and keeps the traversal in
// one place.
void collectChildren(const TypeNode& node,
                     Vector<std::string>& seen,
                     Vector<const TypeNode*>& outOrdered)
{
    for (auto& field: node.fields)
        collectNamed(*field.type, seen, outOrdered);

    for (auto& variant: node.variants)
        collectNamed(*variant.type, seen, outOrdered);

    if (node.inner.get() != nullptr)
        collectNamed(*node.inner, seen, outOrdered);
}

// Collects every named (Object/Enum/Union) node reachable from `node` in
// post-order (deepest first). Re-encounters of the same C++ type (by
// qualified name) become inline name references in rendered output.
void collectNamed(const TypeNode& node,
                  Vector<std::string>& seen,
                  Vector<const TypeNode*>& outOrdered)
{
    if (!isDeclarable(node) || node.typeName.empty())
    {
        collectChildren(node, seen, outOrdered);
        return;
    }

    if (!seen.addIfNotThere(dedupKey(node)))
        return;

    collectChildren(node, seen, outOrdered);
    outOrdered.add(&node);
}

// Replaces every non-identifier run in `raw` with a single `_`, then
// strips leading underscores or digits so the result is a legal
// identifier. `Ns::Inner::Foo` → `Ns_Inner_Foo`. Strips a leading
// "(anonymous namespace)::" so test types in anonymous namespaces
// produce clean names.
std::string sanitizeIdentifier(std::string_view raw)
{
    constexpr auto anonPrefix = std::string_view {"(anonymous namespace)::"};
    auto trimmed = raw;
    while (trimmed.starts_with(anonPrefix))
        trimmed.remove_prefix(anonPrefix.size());

    auto out = std::string {};
    out.reserve(trimmed.size());

    for (auto c: trimmed)
    {
        if (Detail::isAsciiIdentPart(c))
            out += c;
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }

    while (!out.empty() && out.back() == '_')
        out.pop_back();
    while (!out.empty()
           && (out.front() == '_' || (out.front() >= '0' && out.front() <= '9')))
        out.erase(0, 1);

    return out;
}

using NameMap = std::map<std::string, std::string>;

// For each named type collected from the roots, decides what string
// will be emitted for it. When several distinct C++ types share an
// unqualified name (different namespaces), the colliding entries fall
// back to a sanitized qualified name so each declaration is unique.
NameMap chooseOutputNames(const Vector<const TypeNode*>& ordered)
{
    auto byShortName = std::map<std::string, Vector<const TypeNode*>> {};
    for (auto* n: ordered)
        byShortName[n->typeName].add(n);

    auto names = NameMap {};
    for (auto& [shortName, group]: byShortName)
    {
        if (group.size() == 1)
        {
            auto* n = group.front();
            if (!n->qualifiedName.empty())
                names[n->qualifiedName] = shortName;
            continue;
        }

        for (auto* n: group)
            names[n->qualifiedName] = sanitizeIdentifier(n->qualifiedName);
    }

    return names;
}

// Walks every TypeNode reachable from `root` and rewrites typeName to
// the chosen output name. References to a named type live as separate
// TypeNodes (Object/Enum with typeName but no fields), so the walk has
// to descend into fields, union arms and inner.
void applyOutputNames(TypeNode& root, const NameMap& names)
{
    if (!root.qualifiedName.empty())
    {
        auto it = names.find(root.qualifiedName);
        if (it != names.end())
            root.typeName = it->second;
    }

    for (auto& field: root.fields)
        applyOutputNames(*field.type, names);

    for (auto& variant: root.variants)
        applyOutputNames(*variant.type, names);

    if (root.inner.get() != nullptr)
        applyOutputNames(*root.inner, names);
}

} // namespace

Vector<const TypeNode*> prepareRoots(std::span<TypeNode> roots)
{
    auto seen = Vector<std::string> {};
    auto ordered = Vector<const TypeNode*> {};

    for (auto& root: roots)
        collectNamed(root, seen, ordered);

    auto names = chooseOutputNames(ordered);

    for (auto& root: roots)
        applyOutputNames(root, names);

    return ordered;
}

} // namespace Miro::TypeTree
