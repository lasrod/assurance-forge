#pragma once

#include "sacm/sacm_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace core {

inline constexpr const char* kVisibleTerminologyContextMarker = "assurance-forge:visible-term-context";

struct TerminologyPackageRef {
    std::string id;
    std::string gid;
};

struct TerminologyPackageCreateResult {
    bool success = false;
    TerminologyPackageRef package_ref;
    std::string error;
};

struct TerminologyTermRef {
    std::string id;
    std::string gid;
};

struct TerminologyTermDraft {
    std::string value;
    std::string name;
    std::string description;
    std::vector<std::string> category_refs;
    std::string externalReference;
    std::string origin;
};

struct TerminologyTermCreateResult {
    bool success = false;
    TerminologyTermRef term_ref;
    std::string error;
};

struct TerminologyContextAssociationResult {
    bool success = false;
    bool already_associated = false;
    bool created_artifact_reference = false;
    bool created_asserted_context = false;
    std::string artifact_reference_id;
    std::string artifact_reference_gid;
    std::string asserted_context_id;
    std::string asserted_context_gid;
    std::string error;
};

// Optional forced identifiers for the artifact reference and asserted
// context that may be created by `AssociateTerminologyTermWithElement`
// or `AddTerminologyTermAsVisibleContext`. Used by replay handlers and
// command Apply methods to reproduce the exact identities recorded in
// the audit log. Each field is consulted only when the corresponding
// entity is being newly created (existing entities are always reused as
// in the non-`WithIds` variants).
struct TerminologyContextForcedIds {
    std::string artifact_reference_id;
    std::string artifact_reference_gid;
    std::string asserted_context_id;
    std::string asserted_context_gid;
};

struct TerminologyCategoryRef {
    std::string id;
    std::string gid;
};

struct TerminologyCategoryDraft {
    std::string name;
    std::string description;
};

struct TerminologyCategoryCreateResult {
    bool success = false;
    TerminologyCategoryRef category_ref;
    std::string error;
};

struct TerminologyCategoryUsageSummary {
    TerminologyCategoryRef category_ref;
    int term_count = 0;
};

enum class TerminologyTermIssueSeverity { Error, Warning, Info };

enum class TerminologyTermIssueKind {
    MissingValue,
    DuplicateDefinition,
    MissingDescription,
    MissingCategory,
    MissingExternalReference,
};

struct TerminologyTermIssue {
    TerminologyTermRef term_ref;
    TerminologyTermIssueKind kind = TerminologyTermIssueKind::MissingValue;
    TerminologyTermIssueSeverity severity = TerminologyTermIssueSeverity::Info;
    std::string message;
};

struct TerminologyTermUsageSummary {
    TerminologyTermRef term_ref;
    int count = 0;
};

enum class TerminologyUsageResolutionStatus { Resolved, Ambiguous, Undefined, ExplicitContext, OtherMeaning };

struct TerminologyTermUsage {
    TerminologyPackageRef package_ref;
    TerminologyTermRef term_ref;
    std::string argument_package_id;
    std::string argument_package_gid;
    std::string argument_package_name;
    std::string element_id;
    std::string element_gid;
    std::string element_name;
    std::string element_type;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
    std::string matched_text;
    std::string snippet;
    TerminologyUsageResolutionStatus resolution_status = TerminologyUsageResolutionStatus::Undefined;
};

struct TerminologyTermUsageSearchResult {
    bool success = false;
    TerminologyPackageRef package_ref;
    TerminologyTermRef term_ref;
    std::string term_value;
    std::string term_name;
    std::string error;
    std::vector<TerminologyTermUsage> usages;
};

struct TerminologyTermReferenceResolution {
    bool resolved = false;
    TerminologyPackageRef package_ref;
    TerminologyTermRef term_ref;
    const sacm::TerminologyPackage* package = nullptr;
    const sacm::Term* term = nullptr;
};

enum class TerminologyContextReferenceIssueKind {
    MissingArtifactReference,
    MissingTerm,
    InvalidTarget,
    DuplicateContext,
};

struct TerminologyContextReferenceIssue {
    TerminologyContextReferenceIssueKind kind = TerminologyContextReferenceIssueKind::MissingTerm;
    TerminologyTermIssueSeverity severity = TerminologyTermIssueSeverity::Error;
    std::string argument_package_id;
    std::string argument_package_gid;
    std::string asserted_context_id;
    std::string artifact_reference_id;
    std::string target_ref;
    std::string referenced_artifact;
    std::string message;
};

sacm::TerminologyPackage* FindTerminologyPackage(sacm::AssuranceCasePackage& package,
                                                 const TerminologyPackageRef& package_ref);
const sacm::TerminologyPackage* FindTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                                       const TerminologyPackageRef& package_ref);

TerminologyPackageCreateResult
CreateTerminologyPackage(sacm::AssuranceCasePackage& package, const std::string& name, const std::string& description);

// Replay-friendly variant: caller supplies the (id, gid) instead of having
// them generated. Used by `core::audit::Replayer` and by command Apply
// methods that need to reproduce the exact identities recorded in the
// audit log.
TerminologyPackageCreateResult CreateTerminologyPackageWithIds(sacm::AssuranceCasePackage& package,
                                                               const std::string& name,
                                                               const std::string& description,
                                                               const std::string& forced_id,
                                                               const std::string& forced_gid);

bool UpdateTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              const std::string& name,
                              const std::string& description,
                              std::string& out_error);

bool CanDeleteTerminologyPackage(const sacm::TerminologyPackage& package, std::string& out_reason);

bool DeleteTerminologyPackage(sacm::AssuranceCasePackage& package,
                              const TerminologyPackageRef& package_ref,
                              std::string& out_error);

sacm::Term* FindTerminologyTerm(sacm::TerminologyPackage& package, const TerminologyTermRef& term_ref);
const sacm::Term* FindTerminologyTerm(const sacm::TerminologyPackage& package, const TerminologyTermRef& term_ref);
sacm::Term* FindTerminologyTerm(sacm::AssuranceCasePackage& package,
                                const TerminologyPackageRef& package_ref,
                                const TerminologyTermRef& term_ref);
const sacm::Term* FindTerminologyTerm(const sacm::AssuranceCasePackage& package,
                                      const TerminologyPackageRef& package_ref,
                                      const TerminologyTermRef& term_ref);

TerminologyTermCreateResult CreateTerminologyTerm(sacm::AssuranceCasePackage& package,
                                                  const TerminologyPackageRef& package_ref,
                                                  const TerminologyTermDraft& draft);

// Replay-friendly variant: forces the generated (id, gid) when non-empty.
TerminologyTermCreateResult CreateTerminologyTermWithIds(sacm::AssuranceCasePackage& package,
                                                         const TerminologyPackageRef& package_ref,
                                                         const TerminologyTermDraft& draft,
                                                         const std::string& forced_id,
                                                         const std::string& forced_gid);

bool UpdateTerminologyTerm(sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref,
                           const TerminologyTermDraft& draft,
                           std::string& out_error);

bool DeleteTerminologyTerm(sacm::AssuranceCasePackage& package,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref,
                           std::string& out_error);

TerminologyContextAssociationResult AssociateTerminologyTermWithElement(sacm::AssuranceCasePackage& package,
                                                                        const std::string& target_element_id,
                                                                        const TerminologyPackageRef& package_ref,
                                                                        const TerminologyTermRef& term_ref);

// Replay-friendly variant of `AssociateTerminologyTermWithElement`. The
// `forced` ids are consulted only when a new ArtifactReference or
// AssertedContext is created; otherwise existing entities are reused.
TerminologyContextAssociationResult
AssociateTerminologyTermWithElementWithIds(sacm::AssuranceCasePackage& package,
                                           const std::string& target_element_id,
                                           const TerminologyPackageRef& package_ref,
                                           const TerminologyTermRef& term_ref,
                                           const TerminologyContextForcedIds& forced);

TerminologyContextAssociationResult AddTerminologyTermAsVisibleContext(sacm::AssuranceCasePackage& package,
                                                                       const std::string& target_element_id,
                                                                       const TerminologyPackageRef& package_ref,
                                                                       const TerminologyTermRef& term_ref);

// Replay-friendly variant of `AddTerminologyTermAsVisibleContext`. The
// `forced` ids are consulted only when a new ArtifactReference or
// AssertedContext is created; otherwise existing entities are reused or
// promoted in place.
TerminologyContextAssociationResult
AddTerminologyTermAsVisibleContextWithIds(sacm::AssuranceCasePackage& package,
                                          const std::string& target_element_id,
                                          const TerminologyPackageRef& package_ref,
                                          const TerminologyTermRef& term_ref,
                                          const TerminologyContextForcedIds& forced);

TerminologyTermReferenceResolution ResolveTerminologyTermReference(const sacm::AssuranceCasePackage& package,
                                                                   const std::string& raw_ref);
std::vector<TerminologyContextReferenceIssue>
ValidateTerminologyContextReferences(const sacm::AssuranceCasePackage& package);

bool IsVisibleTerminologyContext(const sacm::AssertedContext& context);
bool IsTerminologyArtifactReference(const sacm::AssuranceCasePackage& package,
                                    const sacm::ArtifactReference& artifact_reference);
bool IsVisibleTerminologyArtifactReference(const sacm::AssuranceCasePackage& package,
                                           const sacm::ArgumentPackage& argument_package,
                                           const sacm::ArtifactReference& artifact_reference);

sacm::Category* FindTerminologyCategory(sacm::TerminologyPackage& package, const TerminologyCategoryRef& category_ref);
const sacm::Category* FindTerminologyCategory(const sacm::TerminologyPackage& package,
                                              const TerminologyCategoryRef& category_ref);
sacm::Category* FindTerminologyCategory(sacm::AssuranceCasePackage& package,
                                        const TerminologyPackageRef& package_ref,
                                        const TerminologyCategoryRef& category_ref);
const sacm::Category* FindTerminologyCategory(const sacm::AssuranceCasePackage& package,
                                              const TerminologyPackageRef& package_ref,
                                              const TerminologyCategoryRef& category_ref);

TerminologyCategoryCreateResult CreateTerminologyCategory(sacm::AssuranceCasePackage& package,
                                                          const TerminologyPackageRef& package_ref,
                                                          const TerminologyCategoryDraft& draft);

// Replay-friendly variant: forces the generated (id, gid) when non-empty.
TerminologyCategoryCreateResult CreateTerminologyCategoryWithIds(sacm::AssuranceCasePackage& package,
                                                                 const TerminologyPackageRef& package_ref,
                                                                 const TerminologyCategoryDraft& draft,
                                                                 const std::string& forced_id,
                                                                 const std::string& forced_gid);

bool UpdateTerminologyCategory(sacm::AssuranceCasePackage& package,
                               const TerminologyPackageRef& package_ref,
                               const TerminologyCategoryRef& category_ref,
                               const TerminologyCategoryDraft& draft,
                               std::string& out_error);

bool DeleteTerminologyCategory(sacm::AssuranceCasePackage& package,
                               const TerminologyPackageRef& package_ref,
                               const TerminologyCategoryRef& category_ref,
                               std::string& out_error);

int CountTermsUsingCategory(const sacm::TerminologyPackage& package, const TerminologyCategoryRef& category_ref);
std::vector<TerminologyCategoryUsageSummary>
BuildTerminologyCategoryUsageSummaries(const sacm::TerminologyPackage& package);
std::string CategoryDisplayName(const sacm::TerminologyPackage& package, const std::string& category_ref);

std::vector<TerminologyTermIssue> ValidateTerminologyTerms(const sacm::TerminologyPackage& package);
int CountTerminologyTermUsage(const sacm::AssuranceCasePackage& package, const sacm::Term& term);
std::vector<TerminologyTermUsageSummary>
BuildTerminologyTermUsageSummaries(const sacm::AssuranceCasePackage& package,
                                   const sacm::TerminologyPackage& terminology_package);
TerminologyTermUsageSearchResult FindTerminologyTermUsages(const sacm::AssuranceCasePackage& package,
                                                           const TerminologyPackageRef& package_ref,
                                                           const TerminologyTermRef& term_ref);
const char* ToString(TerminologyUsageResolutionStatus status);

} // namespace core