#pragma once

#include "../TypeTree/TypeTree.h"

#include <span>
#include <string>
#include <string_view>

namespace Miro::TypeScript
{

// Formats `name` for use as a property key in a JS object literal or TS
// interface: a bare identifier when `name` is a valid JS identifier,
// otherwise a JSON-quoted string (with `\` and `"` escaped). Names that
// carry a sub-API namespace separator ("a::b") aren't bare identifiers,
// so quoting them keeps the emitted module valid. Shared so other
// codegen layers (e.g. eacp's events module) key the same way.
std::string formatPropertyKey(std::string_view name);

// The format functions take their roots by mutable reference because
// emission may rewrite per-node `typeName` to disambiguate types from
// different namespaces that share an unqualified name. Callers that
// don't want their trees touched should hand over a copy.
std::string formatZodModule(TypeTree::TypeNode& root);
std::string formatTypesModule(TypeTree::TypeNode& root);

// Multi-root variants — emit one self-contained module that declares
// every named (object or enum) type reachable from any of the roots,
// deduped by qualified name. Used by the type-export runner to bundle
// all registered types into a single .zod.ts / .ts file. The single-
// root versions above add a default export for anonymous roots; the
// bundled versions skip that because a module only allows one default
// export.
std::string formatZodModule(std::span<TypeTree::TypeNode> roots);
std::string formatTypesModule(std::span<TypeTree::TypeNode> roots);

// Static, schema-independent runtime emitted as the `bridge` format —
// the transport-agnostic glue (Transport interface, makeBridge factory)
// that command factories from <baseName>.backend bind to. Pair with a
// transport adapter (e.g. eacp's webViewTransport, an HTTP fetch
// transport, a WebSocket transport) to get a typed client.
std::string formatBridgeRuntime();

// Public entry points.
template <typename T>
std::string toZod()
{
    auto tree = TypeTree::buildTree<T>();
    return formatZodModule(tree);
}

template <typename T>
std::string toTypes()
{
    auto tree = TypeTree::buildTree<T>();
    return formatTypesModule(tree);
}

} // namespace Miro::TypeScript
