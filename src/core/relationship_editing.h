#pragma once

#include "parser/xml_parser.h"
#include "legacy_sacm/sacm_model.h"

#include <string>

namespace core {

// Edits that act on a relationship rather than on a node.
//
// `element_factory`'s removal path is node-shaped by construction: `PlanRemoval`
// works over the GSN tree and deliberately excludes relationship ids, and
// `RemoveElement` reparents structural children. Neither is meaningful for an
// edge. Until these existed the tool could report that a relationship was wrong
// and offer no way to correct it, which is the worst position a validator can
// leave a user in.
//
// Each is a pure mutator over (model, package) with the same signature the
// audited commands bridge onto the library, so a live edit and its replay
// converge by construction.

// Removes one relationship, leaving both of its endpoints in place.
//
// The endpoints survive deliberately: a relationship is the claim that two
// elements are related, and withdrawing that claim is not the same as deleting
// what it related. A node left with no remaining parent becomes a visible
// orphan rather than disappearing, so nothing is lost without being seen.
//
// Removing an inference that carried a Strategy in its `reasoning` slot detaches
// that Strategy along with anything it reasoned to. That is a large consequence
// for one click, so a caller that is not offering undo should confirm first.
bool RemoveRelationship(parser::AssuranceCase& ac,
                        sacm::AssuranceCasePackage* pkg,
                        const std::string& relationship_id,
                        std::string& out_error);

// Drops a single reference from a relationship's sources, targets or reasoning
// slot -- the repair for an endpoint that names an element the case does not
// contain.
//
// Scrubbing can leave the relationship structurally invalid (an inference with
// no source and no reasoning, or anything with no target). The relationship is
// then removed instead, and `out_removed_relationship` says so, because "your
// broken reference is gone" and "your relationship is gone" are different
// outcomes and the caller has to be able to tell the user which happened.
bool DropRelationshipReference(parser::AssuranceCase& ac,
                               sacm::AssuranceCasePackage* pkg,
                               const std::string& relationship_id,
                               const std::string& reference,
                               bool& out_removed_relationship,
                               std::string& out_error);

// Moves a Strategy out of an inference's source list and into its `reasoning`
// slot -- the repair for a Strategy wired as an end of the inference.
//
// This preserves the argument rather than deleting part of it: the author's
// intent ("this strategy explains this inference") is what the correct encoding
// expresses, so the fix is a re-wiring, not a removal. Refused when the
// inference already has a reasoning, because choosing which of two strategies
// explains the step is a judgement the tool does not get to make.
bool MoveStrategyToReasoning(parser::AssuranceCase& ac,
                             sacm::AssuranceCasePackage* pkg,
                             const std::string& relationship_id,
                             const std::string& strategy_id,
                             std::string& out_error);

} // namespace core
