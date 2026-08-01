#pragma once

// Internal traversal helpers (not installed): containment children,
// outgoing references, and reference cleanup. Single source of truth for
// "what does each kind contain / reference"; keep in sync with
// docs/sacm/sacm-2.3-metamodel-inventory.md (asserted by
// test_metamodel_coverage.cpp).

#include "sacm/model/document.h"
#include "sacm/model/element.h"

#include <functional>
#include <string_view>
#include <unordered_set>

namespace sacm::model::traverse {

// Visits every directly contained child of `element` in document order.
void for_each_child(const SACMElement& element, const std::function<void(const SACMElement&)>& fn);
void for_each_child(SACMElement& element, const std::function<void(SACMElement&)>& fn);

// Visits `element` and all descendants (pre-order, document order).
void for_each_descendant(const SACMElement& element, const std::function<void(const SACMElement&)>& fn);

// One outgoing reference of an element: containment-role name and target id.
struct ReferenceUse {
    std::string_view role;
    const ElementId* target;
};

// Visits every outgoing reference of `element` (not of its descendants),
// including ExpressionLangString expression references inside the name,
// descriptions, and utility content.
void for_each_reference(const SACMElement& element, const std::function<void(const ReferenceUse&)>& fn);

// Removes every reference from `referrer` whose target is in `doomed`
// (membership entries, citations, interface lists, ...). Returns the number
// of removed/cleared references. Used by delete cascades so no reference is
// ever left dangling.
std::size_t remove_references_to(SACMElement& referrer, const std::unordered_set<ElementId>& doomed);

// Nearest ancestor (including `element` itself) that is a package kind;
// nullptr when the element is not inside any package.
const SACMElement* nearest_package(const SACMElement& element);

} // namespace sacm::model::traverse
