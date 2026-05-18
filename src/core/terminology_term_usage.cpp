#include "core/string_utils.h"
#include "core/terminology_internal.h"
#include "core/terminology_package_service.h"
#include "core/terminology_scope_service.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace core {

namespace {

using detail::MatchesRef;
using detail::RefFor;

struct ScopedTermRef {
    TerminologyPackageRef package_ref;
    TerminologyTermRef term_ref;
};

std::optional<ScopedTermRef> FindScopedTermRef(const sacm::AssuranceCasePackage& package, const sacm::Term& term) {
    const sacm::Term* term_ptr = &term;
    for (const auto& terminology_package : package.terminologyPackages) {
        for (const auto& candidate : terminology_package.terms) {
            if (&candidate == term_ptr)
                return ScopedTermRef{RefFor(terminology_package), RefFor(candidate)};
        }
    }
    for (const auto& argument_package : package.argumentPackages) {
        for (const auto& terminology_package : argument_package.terminologyPackages) {
            for (const auto& candidate : terminology_package.terms) {
                if (&candidate == term_ptr)
                    return ScopedTermRef{RefFor(terminology_package), RefFor(candidate)};
            }
        }
    }

    const TerminologyTermRef term_ref = RefFor(term);
    for (const auto& terminology_package : package.terminologyPackages) {
        for (const auto& candidate : terminology_package.terms) {
            if (MatchesRef(candidate, term_ref))
                return ScopedTermRef{RefFor(terminology_package), RefFor(candidate)};
        }
    }
    for (const auto& argument_package : package.argumentPackages) {
        for (const auto& terminology_package : argument_package.terminologyPackages) {
            for (const auto& candidate : terminology_package.terms) {
                if (MatchesRef(candidate, term_ref))
                    return ScopedTermRef{RefFor(terminology_package), RefFor(candidate)};
            }
        }
    }
    return std::nullopt;
}

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::vector<std::size_t> FindWholeWordMatches(const std::string& text, const std::string& needle, bool case_sensitive) {
    std::vector<std::size_t> matches;
    if (text.empty() || needle.empty() || needle.size() > text.size())
        return matches;

    const std::string searchable_text = case_sensitive ? text : ToLower(text);
    const std::string searchable_needle = case_sensitive ? needle : ToLower(needle);
    std::size_t pos = searchable_text.find(searchable_needle);
    while (pos != std::string::npos) {
        const bool left_ok = pos == 0 || !IsWordChar(searchable_text[pos - 1]);
        const std::size_t end = pos + searchable_needle.size();
        const bool right_ok = end >= searchable_text.size() || !IsWordChar(searchable_text[end]);
        if (left_ok && right_ok)
            matches.push_back(pos);
        pos = searchable_text.find(searchable_needle, pos + 1);
    }
    return matches;
}

bool SameTermRef(const TerminologyTermRef& left, const TerminologyTermRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

bool SamePackageRef(const TerminologyPackageRef& left, const TerminologyPackageRef& right) {
    if (!left.id.empty() && !right.id.empty() && left.id == right.id)
        return true;
    if (!left.gid.empty() && !right.gid.empty() && left.gid == right.gid)
        return true;
    return false;
}

bool SameScopedTerm(const TerminologyScopedTermRef& scoped,
                    const TerminologyPackageRef& package_ref,
                    const TerminologyTermRef& term_ref) {
    return SamePackageRef(scoped.package_ref, package_ref) && SameTermRef(scoped.term_ref, term_ref);
}

bool CandidateContainsTerm(const TermResolution& resolution,
                           const TerminologyPackageRef& package_ref,
                           const TerminologyTermRef& term_ref) {
    return std::any_of(resolution.candidates.begin(), resolution.candidates.end(), [&](const auto& candidate) {
        return SameScopedTerm(candidate, package_ref, term_ref);
    });
}

TerminologyUsageResolutionStatus UsageStatusForOccurrence(const TermOccurrence* occurrence,
                                                          const TerminologyPackageRef& package_ref,
                                                          const TerminologyTermRef& term_ref) {
    if (!occurrence)
        return TerminologyUsageResolutionStatus::Undefined;

    const TermResolution& resolution = occurrence->resolution;
    switch (resolution.status) {
    case TermResolutionStatus::Unique:
        if (resolution.selected.has_value() && SameScopedTerm(*resolution.selected, package_ref, term_ref))
            return TerminologyUsageResolutionStatus::Resolved;
        return TerminologyUsageResolutionStatus::OtherMeaning;
    case TermResolutionStatus::Ambiguous:
        return CandidateContainsTerm(resolution, package_ref, term_ref) ? TerminologyUsageResolutionStatus::Ambiguous
                                                                        : TerminologyUsageResolutionStatus::Undefined;
    case TermResolutionStatus::Explicit:
        if (resolution.selected.has_value() && SameScopedTerm(*resolution.selected, package_ref, term_ref))
            return TerminologyUsageResolutionStatus::ExplicitContext;
        return TerminologyUsageResolutionStatus::OtherMeaning;
    case TermResolutionStatus::Ignored:
        return TerminologyUsageResolutionStatus::Undefined;
    case TermResolutionStatus::None:
        return TerminologyUsageResolutionStatus::Undefined;
    }
    return TerminologyUsageResolutionStatus::Undefined;
}

bool IsSelectedTermUsageStatus(TerminologyUsageResolutionStatus status) {
    return status == TerminologyUsageResolutionStatus::Resolved ||
           status == TerminologyUsageResolutionStatus::Ambiguous ||
           status == TerminologyUsageResolutionStatus::ExplicitContext;
}

const TermOccurrence* FindDetectedOccurrenceAt(const std::vector<TermOccurrence>& occurrences,
                                               std::size_t start,
                                               std::size_t end,
                                               const std::string& text) {
    auto found = std::find_if(occurrences.begin(), occurrences.end(), [&](const TermOccurrence& occurrence) {
        return occurrence.start_offset == start && occurrence.end_offset == end && occurrence.text == text;
    });
    return found == occurrences.end() ? nullptr : &*found;
}

std::string BuildSnippet(const std::string& text, std::size_t start, std::size_t end) {
    constexpr std::size_t context = 56;
    if (text.empty() || start >= text.size() || end > text.size() || start >= end)
        return {};

    const std::size_t snippet_start = start > context ? start - context : 0;
    const std::size_t snippet_end = std::min(text.size(), end + context);
    std::string snippet = text.substr(snippet_start, snippet_end - snippet_start);
    if (snippet_start > 0)
        snippet = "..." + snippet;
    if (snippet_end < text.size())
        snippet += "...";
    return snippet;
}

std::string FirstNonEmpty(std::initializer_list<std::string> values) {
    for (const auto& value : values) {
        if (!TrimWhitespace(value).empty())
            return value;
    }
    return {};
}

std::string ClaimTypeLabel(const sacm::Claim& claim) {
    if (claim.assertionDeclaration == "assumed")
        return "Assumption";
    if (claim.assertionDeclaration == "justification")
        return "Justification";
    return "Goal";
}

std::unordered_set<std::string> CollectRelationshipSources(const std::vector<sacm::AssertedContext>& contexts) {
    std::unordered_set<std::string> sources;
    for (const auto& context : contexts) {
        for (const auto& source : context.sources) {
            const std::string ref = NormalizeRef(source);
            if (!ref.empty())
                sources.insert(ref);
        }
    }
    return sources;
}

std::unordered_set<std::string> CollectRelationshipSources(const std::vector<sacm::AssertedEvidence>& evidences) {
    std::unordered_set<std::string> sources;
    for (const auto& evidence : evidences) {
        for (const auto& source : evidence.sources) {
            const std::string ref = NormalizeRef(source);
            if (!ref.empty())
                sources.insert(ref);
        }
    }
    return sources;
}

bool SourceSetContainsElement(const std::unordered_set<std::string>& sources, const sacm::SacmElement& element) {
    return (!element.id.empty() && sources.find(element.id) != sources.end()) ||
           (!element.gid.empty() && sources.find(element.gid) != sources.end());
}

std::string ArtifactReferenceTypeLabel(const sacm::ArtifactReference& artifact_reference,
                                       const std::unordered_set<std::string>& context_sources,
                                       const std::unordered_set<std::string>& evidence_sources) {
    if (SourceSetContainsElement(context_sources, artifact_reference))
        return "Context";
    if (SourceSetContainsElement(evidence_sources, artifact_reference))
        return "Solution";
    return "ArtifactReference";
}

} // namespace

std::vector<TerminologyTermIssue> ValidateTerminologyTerms(const sacm::TerminologyPackage& package) {
    std::vector<TerminologyTermIssue> issues;
    std::map<std::pair<std::string, std::string>, int> definition_counts;
    for (const auto& term : package.terms) {
        const std::string value = TrimWhitespace(term.value);
        const std::string description = TrimWhitespace(term.description);
        if (!value.empty() && !description.empty())
            ++definition_counts[{value, description}];
    }

    for (const auto& term : package.terms) {
        const TerminologyTermRef ref = RefFor(term);
        const std::string value = TrimWhitespace(term.value);
        if (value.empty()) {
            issues.push_back({ref,
                              TerminologyTermIssueKind::MissingValue,
                              TerminologyTermIssueSeverity::Error,
                              "Term has no value."});
        } else if (const std::string description = TrimWhitespace(term.description);
                   !description.empty() && definition_counts[{value, description}] > 1) {
            issues.push_back({ref,
                              TerminologyTermIssueKind::DuplicateDefinition,
                              TerminologyTermIssueSeverity::Warning,
                              "Duplicate term value and definition exist in this terminology package."});
        }
        if (TrimWhitespace(term.description).empty()) {
            issues.push_back({ref,
                              TerminologyTermIssueKind::MissingDescription,
                              TerminologyTermIssueSeverity::Warning,
                              "Concrete term has no description."});
        }
        if (term.category_refs.empty()) {
            issues.push_back({ref,
                              TerminologyTermIssueKind::MissingCategory,
                              TerminologyTermIssueSeverity::Info,
                              "Term has no category."});
        }
        if (TrimWhitespace(term.externalReference).empty() && TrimWhitespace(term.origin).empty()) {
            issues.push_back({ref,
                              TerminologyTermIssueKind::MissingExternalReference,
                              TerminologyTermIssueSeverity::Info,
                              "Term has no external reference/source."});
        }
    }
    return issues;
}

int CountTerminologyTermUsage(const sacm::AssuranceCasePackage& package, const sacm::Term& term) {
    const std::optional<ScopedTermRef> scoped_ref = FindScopedTermRef(package, term);
    if (!scoped_ref.has_value())
        return 0;
    const TerminologyTermUsageSearchResult result =
        FindTerminologyTermUsages(package, scoped_ref->package_ref, scoped_ref->term_ref);
    return result.success ? static_cast<int>(result.usages.size()) : 0;
}

std::vector<TerminologyTermUsageSummary>
BuildTerminologyTermUsageSummaries(const sacm::AssuranceCasePackage& package,
                                   const sacm::TerminologyPackage& terminology_package) {
    std::vector<TerminologyTermUsageSummary> summaries;
    const TerminologyPackageRef package_ref = RefFor(terminology_package);
    for (const auto& term : terminology_package.terms) {
        const TerminologyTermRef term_ref = RefFor(term);
        const TerminologyTermUsageSearchResult result = FindTerminologyTermUsages(package, package_ref, term_ref);
        summaries.push_back({term_ref, result.success ? static_cast<int>(result.usages.size()) : 0});
    }
    return summaries;
}

TerminologyTermUsageSearchResult FindTerminologyTermUsages(const sacm::AssuranceCasePackage& package,
                                                           const TerminologyPackageRef& package_ref,
                                                           const TerminologyTermRef& term_ref) {
    TerminologyTermUsageSearchResult result;
    result.package_ref = package_ref;
    result.term_ref = term_ref;

    const sacm::TerminologyPackage* terminology_package = FindTerminologyPackage(package, package_ref);
    if (!terminology_package) {
        result.error = "Terminology package not found.";
        return result;
    }

    const sacm::Term* term = FindTerminologyTerm(*terminology_package, term_ref);
    if (!term) {
        result.error = "Term not found.";
        return result;
    }

    result.term_value = TrimWhitespace(term->value);
    result.term_name = term->name;
    if (result.term_value.empty()) {
        result.error = "Term has no value.";
        return result;
    }

    TerminologyService service(package);

    auto add_usages_for_text = [&](const sacm::ArgumentPackage& argument_package,
                                   const std::string& element_id,
                                   const std::string& element_gid,
                                   const std::string& element_name,
                                   const std::string& element_type,
                                   const std::string& text) {
        if (text.empty())
            return;

        const std::vector<std::size_t> matches = FindWholeWordMatches(text, result.term_value, true);
        if (matches.empty())
            return;

        const std::vector<TermOccurrence> detected = service.DetectTermsInText(element_id, text);
        for (std::size_t start : matches) {
            const std::size_t end = start + result.term_value.size();
            const std::string matched_text = text.substr(start, end - start);
            const TermOccurrence* occurrence = FindDetectedOccurrenceAt(detected, start, end, matched_text);

            TerminologyTermUsage usage;
            usage.package_ref = package_ref;
            usage.term_ref = term_ref;
            usage.argument_package_id = argument_package.id;
            usage.argument_package_gid = argument_package.gid;
            usage.argument_package_name = argument_package.name;
            usage.element_id = element_id;
            usage.element_gid = element_gid;
            usage.element_name = element_name;
            usage.element_type = element_type;
            usage.start_offset = start;
            usage.end_offset = end;
            usage.matched_text = matched_text;
            usage.snippet = BuildSnippet(text, start, end);
            usage.resolution_status = UsageStatusForOccurrence(occurrence, package_ref, term_ref);
            if (!IsSelectedTermUsageStatus(usage.resolution_status))
                continue;
            result.usages.push_back(std::move(usage));
        }
    };

    for (const auto& argument_package : package.argumentPackages) {
        const std::unordered_set<std::string> context_sources =
            CollectRelationshipSources(argument_package.assertedContexts);
        const std::unordered_set<std::string> evidence_sources =
            CollectRelationshipSources(argument_package.assertedEvidences);

        for (const auto& claim : argument_package.claims) {
            add_usages_for_text(argument_package,
                                claim.id,
                                claim.gid,
                                claim.name,
                                ClaimTypeLabel(claim),
                                FirstNonEmpty({claim.content, claim.description, claim.name}));
        }
        for (const auto& reasoning : argument_package.argumentReasonings) {
            add_usages_for_text(argument_package,
                                reasoning.id,
                                reasoning.gid,
                                reasoning.name,
                                "Strategy",
                                FirstNonEmpty({reasoning.content, reasoning.description, reasoning.name}));
        }
        for (const auto& artifact_reference : argument_package.artifactReferences) {
            if (IsTerminologyArtifactReference(package, artifact_reference) &&
                !IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            add_usages_for_text(argument_package,
                                artifact_reference.id,
                                artifact_reference.gid,
                                artifact_reference.name,
                                ArtifactReferenceTypeLabel(artifact_reference, context_sources, evidence_sources),
                                FirstNonEmpty({artifact_reference.description, artifact_reference.name}));
        }
    }

    result.success = true;
    return result;
}

const char* ToString(TerminologyUsageResolutionStatus status) {
    switch (status) {
    case TerminologyUsageResolutionStatus::Resolved:
        return "Resolved";
    case TerminologyUsageResolutionStatus::Ambiguous:
        return "Ambiguous";
    case TerminologyUsageResolutionStatus::Undefined:
        return "Undefined";
    case TerminologyUsageResolutionStatus::ExplicitContext:
        return "Explicit context";
    case TerminologyUsageResolutionStatus::OtherMeaning:
        return "Other meaning";
    }
    return "Undefined";
}

} // namespace core
