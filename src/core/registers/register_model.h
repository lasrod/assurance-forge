#pragma once

// The Claim-Support-Evidence (CSE) and Evidence registers.
//
// A register row is two things joined: structure *derived* from the argument
// (which claims rest on which evidence) and assessment fields the user *enters*
// (owners, criteria, status, notes). Only the second kind needs storing, and
// only the second kind can be lost.
//
// This lives in core rather than in the panel because it is domain logic with
// no UI dependency, and because data a user typed must be testable and
// serializable independently of whether a window is open.

#include "core/sacm_model.h"

#include <map>
#include <string>
#include <vector>

namespace core::registers {

// A claim/evidence pairing derived from an AssertedEvidence relationship.
struct CseLink {
    std::string claim_id;
    std::string evidence_id;
};

bool operator==(const CseLink& a, const CseLink& b);

// Assessment fields for one CSE row. Not derivable from the argument: this is
// the reviewer's judgement about it.
struct CseMetadata {
    std::string claim_owner;
    std::string evidence_owner;
    std::string safety_case_owner;
    std::string claim_criteria;
    std::string evidence_criteria;
    std::string assessment_status = "Not Assessed";
    std::string notes;
};

// Assessment fields for one evidence item.
struct EvidenceMetadata {
    std::string evidence_owner;
    std::string type;
    std::string recency;
    std::string maturity;
    std::string controlled_environment;
    std::string notes;
};

// Everything a user typed into the two registers. `cse` is keyed by CSE id
// (see MakeCseId), `evidence` by element id. Ordered maps so serialization is
// byte-stable, which matters for diffs and for the audit trail.
struct RegisterStore {
    std::map<std::string, CseMetadata> cse;
    std::map<std::string, EvidenceMetadata> evidence;
};

// Stable identity for a claim/evidence pairing. Changing this format orphans
// every stored assessment, so it is fixed and covered by a test.
std::string MakeCseId(const std::string& claim_id, const std::string& evidence_id);

// Every claim/evidence pairing in the argument, sorted and deduplicated so the
// register does not reorder when the document does.
std::vector<CseLink> DeriveCseLinks(const parser::AssuranceCase& model);

// Every evidence element, whether or not a claim currently rests on it. Sorted.
// Unlinked evidence is included deliberately: evidence nobody uses is a finding,
// not something to hide. An ArtifactReference attached by an AssertedContext is
// a GSN Context, not evidence, and is left out; so is an Artifact that an
// ArtifactReference cites, which is that evidence's record rather than more
// evidence.
std::vector<std::string> DeriveEvidenceIds(const parser::AssuranceCase& model);

// How many CSE rows cite this evidence.
int CountCseUses(const std::vector<CseLink>& links, const std::string& evidence_id);

// A claim resting on a piece of evidence, with the AssertedEvidence that
// carries it. `shared` when that relationship also carries other sources, so
// removing it would withdraw more than this one link.
struct EvidenceCitation {
    std::string claim_id;
    std::string relationship_id;
    bool shared = false;
};

// The claims resting on `evidence_id`, sorted by claim id then relationship id.
std::vector<EvidenceCitation> DeriveEvidenceCitations(const parser::AssuranceCase& model,
                                                      const std::string& evidence_id);

// What evidence can be attached under, sorted: the Goals and Strategies GSN
// lets `SupportedBy` reach a Solution from. An Assumption and a Justification
// are SACM Claims too but are leaves in GSN, so offering them would only earn
// the refusal `core::CanAddChildElement` already gives.
std::vector<std::string> DeriveEvidenceSupportTargets(const parser::AssuranceCase& model);

// Display text for an element, falling back through name -> content ->
// description -> id so a row is never blank.
std::string DisplayTextFor(const parser::SacmElement* element);

std::string SerializeRegisterStore(const RegisterStore& store);
bool DeserializeRegisterStore(const std::string& content, RegisterStore& store, std::string& error);

// Stored assessments whose subject is no longer in the argument -- the element
// was deleted or its id changed.
struct OrphanedMetadata {
    std::vector<std::string> cse_ids;
    std::vector<std::string> evidence_ids;
};

// Reports orphans; does NOT remove them. Deleting a reviewer's assessment
// because an id moved would be silent data loss, and the caller (a human, via
// the UI) is the only party entitled to make that call.
OrphanedMetadata FindOrphanedMetadata(const RegisterStore& store,
                                      const std::vector<CseLink>& links,
                                      const std::vector<std::string>& evidence_ids);

} // namespace core::registers
