#include "core/problems/gsn_wellformedness.h"

#include "core/element_factory.h"
#include "core/string_utils.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace core {
namespace {

bool IsRelationshipType(const std::string& type) {
    return type == "assertedinference" || type == "assertedcontext" || type == "assertedevidence";
}

bool IsSupportRelationship(const parser::SacmElement& relationship) {
    return relationship.type == "assertedinference" || relationship.type == "assertedevidence";
}

// Resolves a reference to the element it names. References appear as bare ids,
// as `#id`, and against an element's `gid` rather than its `id`, so all three
// have to hit — a lookup that only matched `id` would report half a real file
// as broken.
class ElementIndex {
public:
    explicit ElementIndex(const parser::AssuranceCase& model) {
        by_key_.reserve(model.elements.size() * 2);
        for (const parser::SacmElement& element : model.elements) {
            // First occurrence wins, matching parser::FindElementByIdOrGid.
            if (!element.id.empty())
                by_key_.emplace(element.id, &element);
            if (!element.gid.empty())
                by_key_.emplace(element.gid, &element);
        }
    }

    const parser::SacmElement* Resolve(const std::string& reference) const {
        const std::string key = NormalizeRef(reference);
        if (key.empty())
            return nullptr;
        const auto found = by_key_.find(key);
        return found == by_key_.end() ? nullptr : found->second;
    }

private:
    std::unordered_map<std::string, const parser::SacmElement*> by_key_;
};

// Where to send a reader who selects a finding about a broken relationship. The
// relationship itself is not a node on the canvas, so anchor on whichever end
// still resolves; that is the element next to the damage.
std::string ResolvableAnchor(const parser::SacmElement& relationship, const ElementIndex& index) {
    for (const std::string& reference : relationship.target_refs) {
        if (const parser::SacmElement* element = index.Resolve(reference))
            return element->id;
    }
    for (const std::string& reference : relationship.source_refs) {
        if (const parser::SacmElement* element = index.Resolve(reference))
            return element->id;
    }
    if (const parser::SacmElement* element = index.Resolve(relationship.reasoning_ref))
        return element->id;
    return std::string();
}

void AddFinding(std::vector<GsnFinding>& findings,
                GsnRule rule,
                std::string element_id,
                std::string related_id,
                std::string relationship_id,
                std::string detail) {
    findings.push_back(GsnFinding{rule,
                                  std::move(element_id),
                                  std::move(related_id),
                                  std::move(relationship_id),
                                  std::move(detail)});
}

// Every reference a relationship makes must name something in the case. An
// unresolved endpoint is not a cosmetic defect: the relationship is silently
// absent from the rendered argument, so the diagram shows a smaller, tidier
// case than the file actually contains.
void CheckEndpoints(const parser::SacmElement& relationship,
                    const ElementIndex& index,
                    std::vector<GsnFinding>& findings) {
    const std::string anchor = ResolvableAnchor(relationship, index);

    for (const std::string& reference : relationship.target_refs) {
        const std::string normalized = NormalizeRef(reference);
        if (normalized.empty() || index.Resolve(normalized) != nullptr)
            continue;
        // A counter relationship's target is the thing being challenged, so
        // losing it loses the challenge itself, not just an edge.
        AddFinding(findings,
                   relationship.is_counter ? GsnRule::ChallengeTargetUnresolved : GsnRule::UnresolvedEndpoint,
                   anchor,
                   std::string(),
                   relationship.id,
                   normalized);
    }

    for (const std::string& reference : relationship.source_refs) {
        const std::string normalized = NormalizeRef(reference);
        if (normalized.empty() || index.Resolve(normalized) != nullptr)
            continue;
        AddFinding(findings, GsnRule::UnresolvedEndpoint, anchor, std::string(), relationship.id, normalized);
    }

    const std::string reasoning = NormalizeRef(relationship.reasoning_ref);
    if (!reasoning.empty() && index.Resolve(reasoning) == nullptr)
        AddFinding(findings, GsnRule::UnresolvedEndpoint, anchor, std::string(), relationship.id, reasoning);
}

// The permitted element/relationship combinations of GSN v3 (GSN3-CORE-015),
// checked from the SACM side. SACM's endpoints run the other way round from
// GSN's: an AssertedInference points premise -> conclusion, so its *target* is
// the element that GSN draws as supported. See docs/sacm/sacm-gsn-mapping.md.
void CheckConnectionRules(const parser::SacmElement& relationship,
                          const ElementIndex& index,
                          std::vector<GsnFinding>& findings) {
    // A challenge is not support, and GSN v3 lets one point at anything in the
    // argument including a relationship. Judging it by the support rules would
    // report every dialectic argument as malformed.
    if (relationship.is_counter)
        return;

    const bool support = IsSupportRelationship(relationship);
    for (const std::string& reference : relationship.target_refs) {
        const parser::SacmElement* target = index.Resolve(reference);
        if (target == nullptr)
            continue;
        const GsnElementKind kind = GsnKindOf(*target);
        // An unrecognized element has no GSN role to violate. A relationship as
        // an endpoint is legal SACM (an AssertedRelationship is an Assertion)
        // and is how GSN v3 attaches to a relationship, so neither is judged.
        if (kind == GsnElementKind::Unrecognized || kind == GsnElementKind::Relationship)
            continue;
        if (support && !GsnCanBeSupported(kind))
            AddFinding(findings, GsnRule::SupportedElementIsALeaf, target->id, std::string(), relationship.id, {});
        if (!support && !GsnCanBeContextualized(kind))
            AddFinding(
                findings, GsnRule::ContextualizedElementIsALeaf, target->id, std::string(), relationship.id, {});
    }

    for (const std::string& reference : relationship.source_refs) {
        const parser::SacmElement* source = index.Resolve(reference);
        if (source == nullptr)
            continue;
        const GsnElementKind kind = GsnKindOf(*source);
        // A Strategy is a node in GSN and an annotation in SACM: it belongs in
        // the inference's `reasoning` slot, not at either end. Wired as an
        // endpoint it is also invalid SACM, because ArgumentReasoning is an
        // ArgumentAsset and clause 11.13 requires an Assertion there.
        if (kind == GsnElementKind::Strategy && relationship.type == "assertedinference")
            AddFinding(findings, GsnRule::StrategyUsedAsAssertion, source->id, std::string(), relationship.id, {});
    }
}

// GSN v3: a Solution is a *reference to evidence*. Anything else standing in
// that position is a claim dressed as evidence — the argument appears to be
// discharged by an artifact that does not exist.
void CheckEvidenceSources(const parser::SacmElement& relationship,
                          const ElementIndex& index,
                          std::vector<GsnFinding>& findings) {
    if (relationship.type != "assertedevidence")
        return;
    for (const std::string& reference : relationship.source_refs) {
        const parser::SacmElement* source = index.Resolve(reference);
        if (source == nullptr)
            continue;
        const GsnElementKind kind = GsnKindOf(*source);
        if (kind == GsnElementKind::ArtifactBacked || kind == GsnElementKind::Unrecognized ||
            kind == GsnElementKind::Relationship)
            continue;
        AddFinding(findings, GsnRule::EvidenceSourceIsNotASolution, source->id, std::string(), relationship.id, {});
    }
}

// GSN v3 makes the notation identifier mandatory and unique: it is how a review
// comment, a report and a conversation name a node. Two nodes answering to the
// same identifier make every such reference ambiguous.
void CheckIdentifiers(const parser::AssuranceCase& model, std::vector<GsnFinding>& findings) {
    std::map<std::string, const parser::SacmElement*> first_by_identifier;
    for (const parser::SacmElement& element : model.elements) {
        if (IsRelationshipType(element.type))
            continue;
        const std::string identifier = GsnIdentifierFor(element);
        if (identifier.empty())
            continue;
        const auto inserted = first_by_identifier.emplace(identifier, &element);
        if (inserted.second)
            continue;
        AddFinding(findings,
                   GsnRule::DuplicateNotationIdentifier,
                   element.id,
                   inserted.first->second->id,
                   std::string(),
                   identifier);
    }
}

// The undeveloped diamond is a promise that support is still missing. Left on
// an element that has since been supported it is a lie in the reader's favour:
// it invites a reviewer to skip a branch that is in fact making a claim.
void CheckUndevelopedDecorators(const parser::AssuranceCase& model,
                                const ElementIndex& index,
                                std::vector<GsnFinding>& findings) {
    for (const parser::SacmElement& relationship : model.elements) {
        if (!IsSupportRelationship(relationship) || relationship.is_counter)
            continue;

        bool carries_support = false;
        for (const std::string& reference : relationship.source_refs)
            carries_support = carries_support || index.Resolve(reference) != nullptr;
        // A strategy placed under a goal is support the reader can see, even
        // before the strategy itself has sub-goals.
        carries_support = carries_support || index.Resolve(relationship.reasoning_ref) != nullptr;
        if (!carries_support)
            continue;

        for (const std::string& reference : relationship.target_refs) {
            const parser::SacmElement* target = index.Resolve(reference);
            if (target == nullptr || !target->undeveloped)
                continue;
            AddFinding(findings,
                       GsnRule::UndevelopedElementHasSupport,
                       target->id,
                       std::string(),
                       relationship.id,
                       {});
        }
    }
}

// Findings are collected in document order, which the model does not control —
// two files carrying the same argument can list their elements differently. A
// total order over the finding's own content makes the reported list a function
// of the argument rather than of its serialization.
std::tuple<int, std::string_view, std::string_view, std::string_view, std::string_view>
SortKey(const GsnFinding& finding) {
    return {static_cast<int>(finding.rule),
            finding.element_id,
            finding.related_id,
            finding.relationship_id,
            finding.detail};
}

} // namespace

GsnElementKind GsnKindOf(const parser::SacmElement& element) {
    if (IsRelationshipType(element.type))
        return GsnElementKind::Relationship;
    if (element.type == "claim") {
        // Matches core::classify_role, which is what the canvas draws. The
        // checker has to judge the argument the reader is looking at.
        if (element.assertion_declaration == "assumed")
            return GsnElementKind::Assumption;
        if (element.assertion_declaration == "justification")
            return GsnElementKind::Justification;
        return GsnElementKind::Goal;
    }
    if (element.type == "argumentreasoning")
        return GsnElementKind::Strategy;
    if (element.type == "artifactreference" || element.type == "artifact")
        return GsnElementKind::ArtifactBacked;
    return GsnElementKind::Unrecognized;
}

bool GsnCanBeSupported(GsnElementKind kind) {
    return kind == GsnElementKind::Goal || kind == GsnElementKind::Strategy;
}

bool GsnCanBeContextualized(GsnElementKind kind) {
    return kind == GsnElementKind::Goal || kind == GsnElementKind::Strategy;
}

const char* GsnRequirementId(GsnRule rule) {
    switch (rule) {
    case GsnRule::UnresolvedEndpoint:
        return "GSN3-CORE-007";
    case GsnRule::ChallengeTargetUnresolved:
        return "GSN3-DIA-003";
    case GsnRule::SupportedElementIsALeaf:
    case GsnRule::ContextualizedElementIsALeaf:
        return "GSN3-CORE-015";
    case GsnRule::StrategyUsedAsAssertion:
        return "GSN3-CORE-002";
    case GsnRule::EvidenceSourceIsNotASolution:
        return "GSN3-CORE-003";
    case GsnRule::DuplicateNotationIdentifier:
        return "GSN3-CORE-010";
    case GsnRule::UndevelopedElementHasSupport:
        return "GSN3-CORE-009";
    }
    return "";
}

const char* GsnRuleName(GsnRule rule) {
    switch (rule) {
    case GsnRule::UnresolvedEndpoint:
        return "UnresolvedEndpoint";
    case GsnRule::ChallengeTargetUnresolved:
        return "ChallengeTargetUnresolved";
    case GsnRule::SupportedElementIsALeaf:
        return "SupportedElementIsALeaf";
    case GsnRule::ContextualizedElementIsALeaf:
        return "ContextualizedElementIsALeaf";
    case GsnRule::StrategyUsedAsAssertion:
        return "StrategyUsedAsAssertion";
    case GsnRule::EvidenceSourceIsNotASolution:
        return "EvidenceSourceIsNotASolution";
    case GsnRule::DuplicateNotationIdentifier:
        return "DuplicateNotationIdentifier";
    case GsnRule::UndevelopedElementHasSupport:
        return "UndevelopedElementHasSupport";
    }
    return "";
}

std::vector<GsnFinding> CheckGsnWellFormedness(const parser::AssuranceCase& model) {
    const ElementIndex index(model);
    std::vector<GsnFinding> findings;

    for (const parser::SacmElement& element : model.elements) {
        if (!IsRelationshipType(element.type))
            continue;
        CheckEndpoints(element, index, findings);
        CheckConnectionRules(element, index, findings);
        CheckEvidenceSources(element, index, findings);
    }
    CheckIdentifiers(model, findings);
    CheckUndevelopedDecorators(model, index, findings);

    std::sort(findings.begin(), findings.end(), [](const GsnFinding& lhs, const GsnFinding& rhs) {
        return SortKey(lhs) < SortKey(rhs);
    });
    findings.erase(std::unique(findings.begin(),
                               findings.end(),
                               [](const GsnFinding& lhs, const GsnFinding& rhs) {
                                   return SortKey(lhs) == SortKey(rhs);
                               }),
                   findings.end());
    return findings;
}

} // namespace core
