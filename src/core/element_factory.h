#pragma once

#include "parser/xml_parser.h"
#include "sacm/sacm_model.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace core {

enum class NewElementKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
};

// Add a new element of the given kind as a child of parent_id.
// Updates both the parser model (drives UI) and the sacm package (drives save).
// Returns true on success and writes the new element id to out_new_id.
// On failure, out_error contains a human-readable reason.
bool AddChildElement(parser::AssuranceCase& ac,
                     sacm::AssuranceCasePackage* pkg,
                     const std::string& parent_id,
                     NewElementKind kind,
                     std::string& out_new_id,
                     std::string& out_error);

// Overload that also reports the generated relationship element id. Required
// by the audited command bus so the AuditEvent payload can carry every id the
// replayer needs to reproduce the operation deterministically.
bool AddChildElement(parser::AssuranceCase& ac,
                     sacm::AssuranceCasePackage* pkg,
                     const std::string& parent_id,
                     NewElementKind kind,
                     std::string& out_new_id,
                     std::string& out_new_relationship_id,
                     std::string& out_error);

// Replay-only entrypoint: install a new child element using the supplied
// element + relationship ids verbatim instead of generating fresh ones. Used
// by the audit replayer; not for normal UI dispatch.
bool AddChildElementWithIds(parser::AssuranceCase& ac,
                            sacm::AssuranceCasePackage* pkg,
                            const std::string& parent_id,
                            NewElementKind kind,
                            const std::string& element_id,
                            const std::string& relationship_id,
                            std::string& out_error);

// Add a new top-level Goal (root claim) without creating a relationship.
// Useful for starting a fresh argument from the canvas background.
bool AddTopGoal(parser::AssuranceCase& ac,
                sacm::AssuranceCasePackage* pkg,
                std::string& out_new_id,
                std::string& out_error);

// Replay-only entrypoint: install a top-level goal with the supplied id.
bool AddTopGoalWithId(parser::AssuranceCase& ac,
                      sacm::AssuranceCasePackage* pkg,
                      const std::string& element_id,
                      std::string& out_error);

// Count the number of descendant elements (children, grandchildren, ...) of
// the element with id `id`. Descendants are derived by walking relationship
// elements: an element X is a child of Y if some relationship has Y in its
// target_refs and X in its source_refs (or as the reasoning_ref). Relationship
// elements themselves are NOT counted as descendants.
int CountDescendants(const parser::AssuranceCase& ac, const std::string& id);

// How a remove operation should expand from the selected element.
//   NodeOnly            - just the element + its Group2 attachments
//                         (Context, Assumption, Justification). Structural
//                         children are reparented to the element's parent.
//   NodeAndDescendants  - the element and every descendant in its subtree.
enum class RemoveMode {
    NodeOnly,
    NodeAndDescendants,
};

// Compute the closed set of NODE ids that would be deleted by RemoveElement
// with the given mode. Pure: does not mutate the model. Used by the menu to
// label items with the count, and by the UI to highlight nodes pending
// confirmation. Relationship element ids are NOT included.
std::unordered_set<std::string> PlanRemoval(const parser::AssuranceCase& ac, const std::string& id, RemoveMode mode);

// Remove the element with id `id` from both the parser and sacm models, plus
// any relationship elements that become structurally empty as a result.
// Behavior depends on `mode` (see RemoveMode above). For NodeOnly, structural
// children of `id` are reparented to its structural parent before deletion.
// Returns true on success; on failure writes a human-readable reason into
// out_error and leaves the models unchanged.
bool RemoveElement(parser::AssuranceCase& ac,
                   sacm::AssuranceCasePackage* pkg,
                   const std::string& id,
                   RemoveMode mode,
                   std::string& out_error);

// Text field on an element that can be updated through the audited command
// bus. The wire-level token (used in audit event payloads) is the lower-case
// name of the enum value.
enum class ElementTextField {
    Name,
    Description,
    Content,
};

const char* ElementTextFieldToToken(ElementTextField field);
bool        ElementTextFieldFromToken(const std::string& token, ElementTextField& out);

// Update one localized text field on the element with id `element_id`. The
// language code is typically "en" (the primary editor language) but the
// helper handles secondary languages too by writing only the corresponding
// `*_langs[language]` entry (and the canonical `description`/`name`/`content`
// scalar when `language == "en"`). On success returns true and writes the
// previous value into `out_old_value`. On failure leaves the model unchanged
// and sets `out_error`. Used by both the audited command and the replayer.
bool SetElementTextField(parser::AssuranceCase& ac,
                         sacm::AssuranceCasePackage* pkg,
                         const std::string& element_id,
                         ElementTextField field,
                         const std::string& language,
                         const std::string& new_value,
                         std::string& out_old_value,
                         std::string& out_error);

} // namespace core
