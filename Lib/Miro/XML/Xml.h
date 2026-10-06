#pragma once

#include "../Containers.h"

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Miro::Xml
{

// Minimal XML DOM. Each Node is one element:
//   <name attr="...">text<child .../></name>
// Mixed content (text interleaved with children) is not represented —
// `text` is meaningful for leaf elements only.
//
// Parser scope is the subset Miro emits: elements, attributes
// (single- and double-quoted), self-closing tags, text content, and
// the five standard entity escapes (& < > " ') plus numeric character
// references (&#233; / &#xE9;, decoded to UTF-8). Comments and
// processing instructions (including the <?xml ...?> declaration)
// are accepted and skipped — they are not represented in the tree.
// CDATA, namespaces, DOCTYPE, and validation are out of scope.
struct Node
{
    std::string name;
    std::map<std::string, std::string> attributes {};
    Vector<Node> children {};
    std::string text {};

    bool operator==(const Node& otherToUse) const = default;
};

class ParseError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

const std::string* findAttribute(const Node& node, std::string_view key);
Node* findChild(Node& node, std::string_view name);
const Node* findChild(const Node& node, std::string_view name);

Node parse(std::string_view inputToUse);
std::string print(const Node& nodeToUse, int indentToUse = 0);
void log(const Node& nodeToUse, int indentToUse = 0);

} // namespace Miro::Xml

namespace Miro
{
using XML = Xml::Node;
} // namespace Miro
