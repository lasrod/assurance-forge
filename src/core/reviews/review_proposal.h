#pragma once

#include <nlohmann/json.hpp>

#include "parser/xml_parser.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace core::reviews {

constexpr const char* kReviewProposalSchema = "assurance-forge.review-proposal.v1";
constexpr const char* kReviewProposalRemoveModeNodeOnly = "node_only";
constexpr const char* kReviewProposalRemoveModeNodeAndDescendants = "node_and_descendants";

enum class PatchOperationType {
    CreateClaim,
    CreateStrategy,
    CreateSolution,
    CreateContext,
    CreateAssumption,
    CreateJustification,
    UpdateElementText,
    UpdateElementName,
    SetUndeveloped,
    ClearUndeveloped,
    AddSupportedBy,
    RemoveSupportedBy,
    AddInContextOf,
    RemoveInContextOf,
    RemoveElement,
    // Where a piece of evidence is: the location of the Resource the
    // ArtifactReference in `element` cites, carried in `new_value`. An empty
    // value clears it. See sacm_adapter::apply_set_evidence_location.
    SetEvidenceLocation,
    // One of the evidence register's columns on the Artifact the reference in
    // `element` cites: `field` names the column by its core::EvidenceAttribute
    // token ("owner", "type", "version", "date", "maturity",
    // "controlled_environment", "notes"), `new_value` the text; empty clears.
    SetEvidenceAttribute,
    // One of the CSE register's columns on the AssertedEvidence in `element`:
    // `field` names the column by its core::CseAttribute token ("claim_owner",
    // "assessment_status", ...), `new_value` the text; empty clears.
    SetCseAttribute,
    // Terminology (SACM clause 10). A term is an element of type "term" in the
    // flat model: its value (the word or phrase being defined) lives in
    // `content` and its definition in `description`, mirroring how
    // `sacm_adapter::project_case` projects a library Term.
    CreateTerm,
    UpdateTerm,
    RemoveTerm,
    // A Category (clause 10.8) classifies terms. It is an element of type
    // "category" whose `name` and `description` are its only content.
    CreateCategory,
    UpdateCategory,
};

// The `field` vocabulary UpdateTerm accepts. These are term-domain names an
// agent can discover from the schema; the patch service maps them onto the flat
// element's content/description/name and its terminology fields.
constexpr const char* kTermFieldValue = "value";
constexpr const char* kTermFieldDefinition = "definition";
constexpr const char* kTermFieldName = "name";
constexpr const char* kTermFieldCategory = "category";
constexpr const char* kTermFieldExternalReference = "external_reference";
constexpr const char* kTermFieldOrigin = "origin";

// The `field` vocabulary UpdateCategory accepts.
constexpr const char* kCategoryFieldName = "name";
constexpr const char* kCategoryFieldDescription = "description";

struct ElementRef {
    std::optional<std::string> existing_id;
    std::optional<std::string> create_ref;
};

// The language of `text` and `new_value`. SACM stores a primary LangString plus
// translations, and the model's scalar `name`/`content`/`description` mirror the
// primary; keeping the primary in its own field rather than inside
// `translations` is what stops a proposal from creating an element that has a
// Japanese statement and no English one.
constexpr const char* kPatchPrimaryLanguage = "en";

struct PatchOperation {
    PatchOperationType type = PatchOperationType::UpdateElementText;
    std::optional<ElementRef> element;
    std::optional<ElementRef> source;
    std::optional<ElementRef> target;
    std::optional<std::string> create_ref;
    std::string field;
    std::string old_value;
    std::string new_value;
    std::string text;
    // Secondary-language text for whichever of `text` or `new_value` this
    // operation carries, keyed by language code ("ja"). One operation therefore
    // states a claim in every language at once: a reviewer accepts a bilingual
    // claim or none of it, and no group can be promoted half-translated.
    //
    // A "en" entry here is ignored -- the primary language has a field.
    std::map<std::string, std::string> translations;
};

struct ReviewProposal {
    std::string schema = kReviewProposalSchema;
    std::string id;
    std::string review_item_id;
    std::string title;
    std::string summary;
    std::string author_name;
    std::string created_utc;
    std::string anchor_element_id;
    std::vector<std::string> affected_existing_element_ids;
    std::string base_model_hash;
    std::map<std::string, std::string> base_element_hashes;
    std::vector<PatchOperation> operations;
};

enum class ProposalValidity {
    Valid,
    Broken,
};

struct ProposalValidityResult {
    ProposalValidity validity = ProposalValidity::Broken;
    std::string reason;
};

struct ReviewProposalSummary {
    std::string id;
    std::string title;
    std::string summary;
    std::string review_item_id;
    std::string anchor_element_id;
    std::filesystem::path relative_path;
    ProposalValidityResult validity;
};

const char* PatchOperationTypeToString(PatchOperationType type);
bool PatchOperationTypeFromString(const std::string& value, PatchOperationType& type);

// Reads one operation from the JSON an AI writes. One parser rather than one
// per caller: an external client staging over MCP and a SCCG review proposing a
// repair describe the same edit, and two readers of one wire vocabulary are how
// the two surfaces come to accept different things.
//
// `error` is worded for the model that produced the JSON, since it is what has
// to correct it.
bool ParsePatchOperationJson(const nlohmann::json& source, PatchOperation& out, std::string& error);

// Whether this operation brings a new element into being and therefore needs a
// patch-local create_ref and a pinned identity. One definition, used by the
// validity gate, the patch service and the draft materializer alike: three
// private copies of this list is how a new create operation gets staged with an
// id one of them never pins.
bool IsCreateOperation(PatchOperationType type);

std::string SerializeReviewProposal(const ReviewProposal& proposal);
bool DeserializeReviewProposal(const std::string& content, ReviewProposal& proposal, std::string& error);

std::string ComputeModelSemanticHash(const parser::AssuranceCase& model);
// The same hash over only the elements named, for asking whether the part of a
// model somebody actually read has changed. A review is stale when its own
// scope moved; hashing the whole model instead makes an edit anywhere -- by the
// user, or by another contributor to the same draft -- discard a completed
// review of an untouched branch (ADR 0013).
//
// Ids naming nothing are folded in as absent, so an element disappearing from
// the model changes the hash rather than being silently skipped.
std::string ComputeScopeSemanticHash(const parser::AssuranceCase& model, const std::vector<std::string>& element_ids);
std::string ComputeElementSemanticHash(const parser::SacmElement& element);
ProposalValidityResult EvaluateReviewProposalValidity(const ReviewProposal& proposal,
                                                      const parser::AssuranceCase& current_model);

} // namespace core::reviews