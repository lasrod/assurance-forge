#include "sacm/compat/preserve.h"

#include "model/access.h"

#include <string>
#include <unordered_map>

namespace sacm::compat {

namespace {

using detail::Access;
using model::SACMElement;

}  // namespace

std::size_t adopt_preserved_content(model::Document& target, const model::Document& source) {
    // Index the source's carriers first. Walking the source once and the target
    // once keeps this linear; the alternative (a find() per source element) is
    // the same complexity only because the index is a hash map, and this way
    // the intent reads directly.
    std::unordered_map<std::string, const SACMElement*> carriers;
    source.for_each_element([&carriers](const SACMElement& element) {
        if (!element.preserved_content().empty() || !element.preserved_attributes().empty()) {
            carriers.emplace(element.id().value(), &element);
        }
    });

    std::size_t adopted = 0;
    if (!carriers.empty()) {
        target.for_each_element([&carriers, &adopted](const SACMElement& element) {
            const auto found = carriers.find(element.id().value());
            if (found == carriers.end()) {
                return;
            }
            // for_each_element is const-only by design (navigation never
            // mutates). Writing goes through Access, the same gateway the
            // reader and the command layer use, so "documents change only
            // through apply or load" still holds mechanically -- this function
            // is part of the load-side story, restoring what a load produced.
            SACMElement& mutable_element = const_cast<SACMElement&>(element);
            bool received = false;
            if (mutable_element.preserved_content().empty() &&
                !found->second->preserved_content().empty()) {
                Access::preserved_content(mutable_element) = found->second->preserved_content();
                received = true;
            }
            if (mutable_element.preserved_attributes().empty() &&
                !found->second->preserved_attributes().empty()) {
                Access::preserved_attributes(mutable_element) =
                    found->second->preserved_attributes();
                received = true;
            }
            if (received) {
                ++adopted;
            }
        });
    }

    // The declarations travel with the fragments whether or not any element
    // matched: a fragment re-emitted under an undeclared prefix is not
    // namespace-well-formed, and the next load cannot classify a preserved
    // attribute at all (SACM23-COMPAT-001). Prefixes the target already binds
    // win, so a rebuild cannot be made to rebind one.
    for (const auto& [prefix, uri] : source.foreign_namespaces()) {
        Access::foreign_namespaces(target).emplace(prefix, uri);
    }

    // Preserved-element identity travels too, or the bytes come back without the
    // knowledge that makes them legible. These ids are what let validation call a
    // reference into compatibility content *untyped* (SACM-REF-003) rather than
    // *missing* (SACM-REF-001). Restoring the fragment but not the id left a
    // rebuilt document reporting a hard SACM-REF-001 Error against an element its
    // own output still carries -- reintroducing, through the back door, the exact
    // false "structurally broken" verdict SACM-REF-003 exists to prevent.
    //
    // Only ids the target does not already resolve: one the rebuild produced as a
    // real typed element must stay typed, not be demoted to opaque.
    for (const model::ElementId& id : source.preserved_element_ids()) {
        if (target.find(id) == nullptr) {
            Access::preserved_element_ids(target).insert(id);
        }
    }

    return adopted;
}

}  // namespace sacm::compat
