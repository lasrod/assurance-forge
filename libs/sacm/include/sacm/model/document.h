#pragma once

#include "sacm/commands/mutation.h"
#include "sacm/commands/operations.h"
#include "sacm/model/assurance_case.h"
#include "sacm/model/element.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sacm::model {

// A loaded or created SACM 2.3 document: the source of truth for one
// assurance case interchange unit.
//
// Ownership: the document owns its root packages; containment owns children.
// References between elements are stored as ElementIds and resolved through
// the document's index.
//
// Mutation of the TYPED MODEL: exclusively through preview/apply. Public
// mutations either succeed and leave the document valid for the supported
// slice, or fail leaving it unchanged with diagnostics (SACM23-VAL-002).
// Navigation is const-only.
//
// One narrow exception, and it does not touch the typed model:
// `sacm::compat::adopt_preserved_content` moves opaque compatibility content
// (preserved fragments, vendor attributes, foreign namespace declarations,
// preserved-element ids) from one document onto another. No operation can
// produce that content and none reads it, so it cannot affect what an
// operation sees or decides -- which is why it is revision-neutral: `apply`'s
// `expected_revision` guards the typed model, and adoption cannot change it.
// It CAN change save behaviour (a document that would strict-save may
// afterwards refuse with SACM-XMI-006), because that is the point.
class Document {
  public:
    Document();
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Strict interchange roots (AssuranceCasePackages, clause 2.4).
    const std::vector<std::unique_ptr<AssuranceCasePackage>>& roots() const { return roots_; }

    // Roots of other kinds accepted by tolerant loads (bare ArgumentPackage/
    // ArtifactPackage/TerminologyPackage interchange units, clauses 2.2/2.3).
    const std::vector<std::unique_ptr<SACMElement>>& other_roots() const { return other_roots_; }

    // Namespace declarations carried by the source document whose URIs this
    // library does not itself emit, keyed by prefix. Preserved compatibility
    // fragments (SACM23-COMPAT-001) are stored as opaque text, so the prefixes
    // they use are only meaningful while their declarations survive with them:
    // a compatibility save re-declares these so the output stays
    // namespace-well-formed and the fragments can be read back. Empty for
    // documents created through the editing API.
    const std::map<std::string, std::string>& foreign_namespaces() const {
        return foreign_namespaces_;
    }

    // Ids carried by elements that only preserved compatibility content
    // represents -- they are in the source document but not in the index,
    // because the reader could not type them (SACM23-COMPAT-002).
    //
    // A reference to one of these is not dangling: the target exists, it is
    // merely untyped. Validation needs the distinction so that a file whose
    // argument is intact is not reported as structurally broken.
    bool has_preserved_element(const ElementId& id) const {
        return preserved_element_ids_.contains(id);
    }

    const std::unordered_set<ElementId>& preserved_element_ids() const {
        return preserved_element_ids_;
    }

    // Element lookup by id; nullptr when absent.
    const SACMElement* find(const ElementId& id) const;

    template <typename T>
    const T* find_as(const ElementId& id) const {
        return dynamic_cast<const T*>(find(id));
    }

    bool contains(const ElementId& id) const { return find(id) != nullptr; }

    // Number of elements in the document (all kinds, including utility
    // elements).
    std::size_t element_count() const { return index_.size(); }

    // Visits every element (roots and descendants) in document order.
    void for_each_element(const std::function<void(const SACMElement&)>& fn) const;

    // Monotonic revision; bumped by every successful apply.
    std::uint64_t revision() const { return revision_; }

    // Computes what applying `operation` would do, without mutating.
    commands::OperationPreview preview(const commands::Operation& operation) const;

    // Applies `operation` atomically. When `expected_revision` is given
    // (from a preview) and the document has changed since, fails with
    // SACM-CMD-003 and no mutation.
    commands::MutationResult apply(const commands::Operation& operation,
                                   std::optional<std::uint64_t> expected_revision = {});

  private:
    friend struct sacm::detail::Access;

    std::vector<std::unique_ptr<AssuranceCasePackage>> roots_;
    std::vector<std::unique_ptr<SACMElement>> other_roots_;
    std::unordered_map<ElementId, SACMElement*> index_;
    std::unordered_set<ElementId> preserved_element_ids_;
    std::map<ElementKind, std::uint64_t> id_counters_;
    std::map<std::string, std::string> foreign_namespaces_;
    std::uint64_t revision_ = 0;
};

}  // namespace sacm::model
