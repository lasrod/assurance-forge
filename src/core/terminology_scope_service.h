#pragma once

#include "core/terminology_package_service.h"
#include "sacm/sacm_model.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace core {

struct TerminologyArgumentPackageRef {
    std::string id;
    std::string gid;
};

enum class TerminologyLookupLayer {
    ExplicitOccurrenceBinding,
    ExplicitElementContext,
    ArgumentPackageTerminology,
    AssuranceCaseTerminology,
    ParentAssuranceCaseTerminology,
    ProjectGlossary,
    ImportedLibrary
};

struct TerminologyScopeContext {
    std::string element_id;
    std::string element_gid;
    TerminologyArgumentPackageRef argument_package_ref;
    TerminologyPackageRef project_glossary_ref;
    TerminologyTermRef explicit_term_ref;
    bool has_explicit_term_ref = false;
    bool ignored = false;
    bool imported_libraries_enabled = false;
};

struct TextOccurrence {
    std::string element_id;
    std::string element_gid;
    std::string text;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
    TerminologyTermRef explicit_term_ref;
    bool has_explicit_term_ref = false;
    bool ignored = false;
};

struct TerminologyScopedTermRef {
    TerminologyTermRef term_ref;
    TerminologyPackageRef package_ref;
    TerminologyLookupLayer layer = TerminologyLookupLayer::AssuranceCaseTerminology;
    int package_order = 0;
    int term_order = 0;
    const sacm::Term* term = nullptr;
};

enum class TermResolutionStatus { None, Unique, Ambiguous, Explicit, Ignored };

struct TermResolution {
    TermResolutionStatus status = TermResolutionStatus::None;
    std::vector<TerminologyScopedTermRef> candidates;
    std::optional<TerminologyScopedTermRef> selected;
    bool important_undefined = false;
};

enum class TermOccurrenceKind { KnownTerm, UndefinedAcronym };

struct TermOccurrence {
    std::string element_id;
    std::string element_gid;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;
    std::string text;
    TermOccurrenceKind kind = TermOccurrenceKind::KnownTerm;
    TermResolution resolution;
};

struct TerminologyDetectionOptions {
    std::size_t min_known_term_length = 1;
    std::size_t min_acronym_length = 2;
    bool detect_unknown_acronyms = true;
    bool case_sensitive_terms = true;
};

class TerminologyService {
public:
    explicit TerminologyService(const sacm::AssuranceCasePackage& package);

    TerminologyScopeContext BuildScopeContextForElement(const std::string& element_ref) const;
    std::vector<TerminologyScopedTermRef> GetActiveTermsForElement(const std::string& element_gid) const;
    std::vector<TerminologyScopedTermRef> GetActiveTermsForElement(const TerminologyScopeContext& scope) const;
    std::vector<TerminologyScopedTermRef> FindTermsByValue(const std::string& text,
                                                           const TerminologyScopeContext& scope) const;
    TermResolution ResolveOccurrence(const TextOccurrence& occurrence, const TerminologyScopeContext& scope) const;
    std::vector<TermOccurrence> DetectTermsInText(const std::string& element_ref,
                                                  const std::string& text,
                                                  const TerminologyDetectionOptions& options = {}) const;
    std::vector<TermOccurrence> DetectTermsInText(const TerminologyScopeContext& scope,
                                                  const std::string& text,
                                                  const TerminologyDetectionOptions& options = {}) const;

private:
    const sacm::AssuranceCasePackage& package_;
};

bool LooksImportantUndefinedTerm(const std::string& text);

} // namespace core
