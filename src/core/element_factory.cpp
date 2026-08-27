#include "core/element_factory.h"

#include "core/acp/assurance_claim_point.h"
#include "core/problems/gsn_wellformedness.h"
#include "sacm_adapter/gsn_role_tag.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace core {

namespace {

const char* PrefixFor(NewElementKind kind) {
    switch (kind) {
    case NewElementKind::Goal:
        return "G";
    case NewElementKind::Strategy:
        return "S";
    case NewElementKind::Solution:
        return "Sn";
    case NewElementKind::Context:
        return "C";
    case NewElementKind::Assumption:
        return "A";
    case NewElementKind::Justification:
        return "J";
    }
    return "N";
}

const char* DefaultNameFor(NewElementKind kind) {
    switch (kind) {
    case NewElementKind::Goal:
        return "";
    case NewElementKind::Strategy:
        return "";
    case NewElementKind::Solution:
        return "";
    case NewElementKind::Context:
        return "";
    case NewElementKind::Assumption:
        return "";
    case NewElementKind::Justification:
        return "";
    }
    return "";
}

std::unordered_set<std::string> CollectIds(const parser::AssuranceCase& ac) {
    std::unordered_set<std::string> ids;
    ids.reserve(ac.elements.size() * 2);
    for (const auto& e : ac.elements) {
        if (!e.id.empty())
            ids.insert(e.id);
    }
    return ids;
}

std::string GenerateUniqueId(const std::unordered_set<std::string>& existing, const std::string& prefix) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        if (existing.find(candidate) == existing.end())
            return candidate;
    }
    return prefix + "x";
}

std::string AcpPrefixForPackage(const sacm::ArgumentPackage* package) {
    if (!package || !core::acp::IsConfidenceArgumentPackage(*package))
        return {};
    const std::string marker = "_AP";
    const std::size_t marker_pos = package->id.rfind(marker);
    if (marker_pos == std::string::npos || marker_pos == 0)
        return {};
    for (std::size_t index = marker_pos + marker.size(); index < package->id.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(package->id[index])))
            return {};
    }
    return package->id.substr(0, marker_pos);
}

std::string ScopedPrefixFor(const sacm::ArgumentPackage* package, NewElementKind kind) {
    const std::string acp_prefix = AcpPrefixForPackage(package);
    if (acp_prefix.empty())
        return PrefixFor(kind);
    return acp_prefix + "_" + PrefixFor(kind);
}

std::string ScopedRelationshipPrefixFor(const sacm::ArgumentPackage* package) {
    const std::string acp_prefix = AcpPrefixForPackage(package);
    return acp_prefix.empty() ? std::string("R") : acp_prefix + "_R";
}

const char* ChallengePrefixFor(ChallengeSourceType type) {
    // TODO(gsn-dialectic): nesting-aware prefixes (CCG/CCSn, CCCG/CCCSn, ...) for
    // counter-challenges. Plan 1 uses flat numbering (Option A).
    return type == ChallengeSourceType::CounterArgument ? "CG" : "CSn";
}

std::string ScopedChallengePrefixFor(const sacm::ArgumentPackage* package, ChallengeSourceType type) {
    const std::string acp_prefix = AcpPrefixForPackage(package);
    if (acp_prefix.empty())
        return ChallengePrefixFor(type);
    return acp_prefix + "_" + ChallengePrefixFor(type);
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& ac, const std::string& id) {
    for (const auto& e : ac.elements) {
        if (e.id == id)
            return &e;
    }
    return nullptr;
}

bool IsRelationshipType(const std::string& t) {
    return t == "assertedinference" || t == "assertedcontext" || t == "assertedevidence";
}

// Determine which ArgumentPackage in the sacm model owns the parent element,
// without mutating the package. Falls back to the first package; null when
// there is none (the mutable variant below creates one instead). For id
// planning the two agree: a freshly created package has an empty id, and
// AcpPrefixForPackage yields the same empty prefix for an empty id and for
// null.
const sacm::ArgumentPackage* FindOwningArgumentPackageConst(const sacm::AssuranceCasePackage* pkg,
                                                            const std::string& parent_id) {
    if (!pkg)
        return nullptr;
    for (const auto& ap : pkg->argumentPackages) {
        for (const auto& c : ap.claims)
            if (c.id == parent_id)
                return &ap;
        for (const auto& ar : ap.argumentReasonings)
            if (ar.id == parent_id)
                return &ap;
        for (const auto& ar : ap.artifactReferences)
            if (ar.id == parent_id)
                return &ap;
    }
    return pkg->argumentPackages.empty() ? nullptr : &pkg->argumentPackages.front();
}

// Determine which ArgumentPackage in the sacm model owns the parent element.
// Falls back to the first package, creating one if necessary. `const_cast` is
// well-defined here: `pkg` is non-const and every returned pointer points into
// it.
sacm::ArgumentPackage* FindOwningArgumentPackage(sacm::AssuranceCasePackage* pkg, const std::string& parent_id) {
    if (!pkg)
        return nullptr;
    if (pkg->argumentPackages.empty()) {
        pkg->argumentPackages.emplace_back();
    }
    return const_cast<sacm::ArgumentPackage*>(FindOwningArgumentPackageConst(pkg, parent_id));
}

void MirrorClaim(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap)
        return;
    sacm::Claim c;
    c.id = src.id;
    c.name = src.name;
    c.name_ml.set("en", src.name);
    c.isAbstract = src.is_abstract;
    c.assertionDeclaration = src.assertion_declaration;
    c.undeveloped = src.undeveloped;
    c.taggedValues.push_back(sacm::TaggedValue{
        .id = src.id + "__gsnIdentifier", .key = kGsnIdentifierTagKey, .value = GsnIdentifierFor(src)});
    ap->claims.push_back(std::move(c));
}

void MirrorReasoning(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap)
        return;
    sacm::ArgumentReasoning r;
    r.id = src.id;
    r.name = src.name;
    r.name_ml.set("en", src.name);
    r.isAbstract = src.is_abstract;
    r.taggedValues.push_back(sacm::TaggedValue{
        .id = src.id + "__gsnIdentifier", .key = kGsnIdentifierTagKey, .value = GsnIdentifierFor(src)});
    ap->argumentReasonings.push_back(std::move(r));
}

void MirrorArtifactReference(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap)
        return;
    sacm::ArtifactReference ar;
    ar.id = src.id;
    ar.name = src.name;
    ar.name_ml.set("en", src.name);
    ar.isAbstract = src.is_abstract;
    ar.taggedValues.push_back(sacm::TaggedValue{
        .id = src.id + "__gsnIdentifier", .key = kGsnIdentifierTagKey, .value = GsnIdentifierFor(src)});
    ap->artifactReferences.push_back(std::move(ar));
}

void MirrorInference(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap)
        return;
    sacm::AssertedInference ai;
    ai.id = rel.id;
    ai.isAbstract = rel.is_abstract;
    ai.sources = rel.source_refs;
    ai.targets = rel.target_refs;
    ai.reasoning = rel.reasoning_ref;
    ai.isCounter = rel.is_counter;
    ap->assertedInferences.push_back(std::move(ai));
}

void MirrorContext(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap)
        return;
    sacm::AssertedContext ac;
    ac.id = rel.id;
    ac.isAbstract = rel.is_abstract;
    ac.sources = rel.source_refs;
    ac.targets = rel.target_refs;
    ap->assertedContexts.push_back(std::move(ac));
}

void MirrorEvidence(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap)
        return;
    sacm::AssertedEvidence ae;
    ae.id = rel.id;
    ae.isAbstract = rel.is_abstract;
    ae.sources = rel.source_refs;
    ae.targets = rel.target_refs;
    ae.isCounter = rel.is_counter;
    ap->assertedEvidences.push_back(std::move(ae));
}

} // namespace

namespace {

// A GSN strategy uses the standard single-inference encoding: it is created as a
// standalone ArgumentReasoning carrying a `strategyTarget` tag (the goal it will
// support), with NO inference until its first sub-goal materializes one. The
// package gets the reasoning + tag; the render model gets the reasoning plus a
// sourceless placeholder inference (matching core::SynthesizeBareStrategyPlacements)
// so the strategy renders under its goal until a sub-goal makes it real.
bool InstallStrategy(parser::AssuranceCase& ac,
                     sacm::ArgumentPackage* ap,
                     const std::string& goal_id,
                     const std::string& element_id) {
    parser::SacmElement reasoning;
    reasoning.id = element_id;
    reasoning.gsn_identifier = element_id;
    reasoning.type = "argumentreasoning";
    reasoning.name = DefaultNameFor(NewElementKind::Strategy);

    parser::SacmElement placeholder;
    placeholder.id = element_id + "__pending_inference";
    placeholder.type = "assertedinference";
    placeholder.reasoning_ref = element_id;
    placeholder.target_refs.push_back(goal_id);

    if (ap) {
        MirrorReasoning(ap, reasoning);
        // Record the target goal on the package reasoning so the first sub-goal
        // can materialize the inference. A deterministic explicit id (not a
        // generated_N) survives the save/reload round-trip without the library
        // re-minting and colliding.
        for (sacm::ArgumentReasoning& r : ap->argumentReasonings) {
            if (r.id == element_id) {
                r.taggedValues.push_back(sacm::TaggedValue{.id = element_id + "__strategyTarget",
                                                           .key = sacm_adapter::kGsnStrategyTargetTagKey,
                                                           .value = goal_id});
                break;
            }
        }
    }

    ac.elements.push_back(std::move(reasoning));
    ac.elements.push_back(std::move(placeholder));
    return true;
}

// A sub-goal added under a strategy becomes a source of the strategy's single
// inference. The first sub-goal materializes {target = the goal the strategy
// supports (its strategyTarget tag), reasoning = strategy, source = sub-goal},
// consuming the render placeholder; later sub-goals extend that inference's
// sources. Mirrors the proven seam sacm_adapter::apply_add_subgoal_under_strategy.
bool InstallSubGoalUnderStrategy(parser::AssuranceCase& ac,
                                 sacm::ArgumentPackage* ap,
                                 const std::string& strategy_id,
                                 const std::string& element_id,
                                 const std::string& relationship_id,
                                 std::string& out_error,
                                 std::string* out_created_relationship_id) {
    const std::string placeholder_id = strategy_id + "__pending_inference";

    // The strategy's real (materialized) inference, if one exists yet.
    parser::SacmElement* existing = nullptr;
    for (parser::SacmElement& e : ac.elements) {
        if (e.type == "assertedinference" && e.reasoning_ref == strategy_id && e.id != placeholder_id) {
            existing = &e;
            break;
        }
    }

    parser::SacmElement goal;
    goal.id = element_id;
    goal.gsn_identifier = element_id;
    goal.type = "claim";
    goal.name = DefaultNameFor(NewElementKind::Goal);

    if (existing != nullptr) {
        // Later sub-goal: extend the existing inference's sources (model + package).
        existing->source_refs.push_back(element_id);
        if (ap) {
            for (sacm::AssertedInference& ai : ap->assertedInferences) {
                if (ai.id == existing->id) {
                    ai.sources.push_back(element_id);
                    break;
                }
            }
            MirrorClaim(ap, goal);
        }
        ac.elements.push_back(std::move(goal));
        if (out_created_relationship_id)
            out_created_relationship_id->clear(); // extending an existing inference creates no relationship
        return true;
    }

    // First sub-goal: materialize. The inference targets the goal the strategy was
    // created to support (its strategyTarget tag on the package reasoning).
    std::string target_goal;
    if (ap) {
        for (const sacm::ArgumentReasoning& r : ap->argumentReasonings) {
            if (r.id != strategy_id)
                continue;
            for (const sacm::TaggedValue& tag : r.taggedValues) {
                if (tag.key == sacm_adapter::kGsnStrategyTargetTagKey) {
                    target_goal = tag.value;
                    break;
                }
            }
            break;
        }
    }
    if (target_goal.empty()) {
        out_error = "Strategy has no target goal recorded; cannot materialize its inference.";
        return false;
    }
    if (relationship_id.empty()) {
        out_error = "Materializing a strategy inference requires a relationship id.";
        return false;
    }

    // Consume the render-only placeholder (its role is taken over by the real inference).
    ac.elements.erase(std::remove_if(ac.elements.begin(),
                                     ac.elements.end(),
                                     [&](const parser::SacmElement& e) { return e.id == placeholder_id; }),
                      ac.elements.end());

    parser::SacmElement inference;
    inference.id = relationship_id;
    inference.type = "assertedinference";
    inference.reasoning_ref = strategy_id;
    inference.target_refs.push_back(target_goal);
    inference.source_refs.push_back(element_id);

    if (ap) {
        MirrorClaim(ap, goal);
        MirrorInference(ap, inference);
    }
    ac.elements.push_back(std::move(goal));
    ac.elements.push_back(std::move(inference));
    if (out_created_relationship_id)
        *out_created_relationship_id = relationship_id;
    return true;
}

} // namespace

bool CanAddChildElement(const parser::SacmElement& parent, NewElementKind kind, std::string& out_error) {
    const GsnElementKind parent_kind = GsnKindOf(parent);

    // GSN v3: an Assumption and a Justification are leaves. Both are Claims in
    // SACM, so a test on the SACM type alone cannot tell either from a Goal —
    // which is how the Add menu came to offer a whole sub-argument under an
    // Assumption, a structure the notation has no way to draw or mean.
    if (parent_kind == GsnElementKind::Assumption || parent_kind == GsnElementKind::Justification) {
        out_error = "Cannot add a child to an Assumption or a Justification; both are leaves in GSN.";
        return false;
    }
    // Only a Goal or a Strategy takes children, whether by SupportedBy or by
    // InContextOf. A Solution and a Context are leaves too. Spoken in GSN terms:
    // this refusal reaches the user (status message, disabled-menu tooltip), and
    // "artifactreference" names the SACM storage, not the thing on their canvas.
    if (!GsnCanBeSupported(parent_kind)) {
        if (parent_kind == GsnElementKind::ArtifactBacked) {
            out_error = "Cannot add a child to a Solution or Context; both are leaves in GSN.";
        } else {
            out_error = "Cannot add a child to this element (" + parent.type + ").";
        }
        return false;
    }
    if (kind == NewElementKind::Strategy && parent_kind != GsnElementKind::Goal) {
        out_error = "A Strategy can only be added under a Goal.";
        return false;
    }
    return true;
}

namespace {

// Shared installation logic: validate parent, then build the new element +
// relationship using the supplied ids and install into both models. Both the
// id-generating `AddChildElement` and the replay-only `AddChildElementWithIds`
// route through here.
bool InstallChildElement(parser::AssuranceCase& ac,
                         sacm::AssuranceCasePackage* pkg,
                         const std::string& parent_id,
                         NewElementKind kind,
                         const std::string& element_id,
                         const std::string& relationship_id,
                         std::string& out_error,
                         std::string* out_created_relationship_id = nullptr) {
    if (out_created_relationship_id)
        out_created_relationship_id->clear();
    if (parent_id.empty()) {
        out_error = "No parent element selected.";
        return false;
    }
    // A strategy (and a sub-goal that extends an existing strategy inference)
    // creates no relationship, so only the element id is universally required;
    // the relationship id is checked per-branch where one is actually created.
    if (element_id.empty()) {
        out_error = "Element id must be non-empty.";
        return false;
    }

    const parser::SacmElement* parent = FindElement(ac, parent_id);
    if (!parent) {
        out_error = "Selected element not found in model.";
        return false;
    }

    const std::string& ptype = parent->type;
    if (!CanAddChildElement(*parent, kind, out_error))
        return false;

    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, parent_id);

    // GSN strategies use the single-inference encoding: a strategy is a bare
    // ArgumentReasoning + strategyTarget tag; a sub-goal under a strategy is a
    // source of that strategy's single inference (materialize/extend).
    if (kind == NewElementKind::Strategy)
        return InstallStrategy(ac, ap, parent_id, element_id);
    if (kind == NewElementKind::Goal && ptype == "argumentreasoning")
        return InstallSubGoalUnderStrategy(
            ac, ap, parent_id, element_id, relationship_id, out_error, out_created_relationship_id);

    // Every other child creates its own relationship, which requires an id.
    if (relationship_id.empty()) {
        out_error = "Relationship id must be non-empty.";
        return false;
    }

    parser::SacmElement new_elem;
    new_elem.id = element_id;
    new_elem.gsn_identifier = element_id;
    new_elem.name = DefaultNameFor(kind);

    parser::SacmElement rel;
    rel.id = relationship_id;
    rel.target_refs.push_back(parent_id);

    switch (kind) {
    case NewElementKind::Goal:
        new_elem.type = "claim";
        rel.type = "assertedinference";
        rel.source_refs.push_back(new_elem.id);
        break;
    case NewElementKind::Strategy:
        new_elem.type = "argumentreasoning";
        rel.type = "assertedinference";
        rel.reasoning_ref = new_elem.id;
        break;
    case NewElementKind::Solution:
        new_elem.type = "artifactreference";
        rel.type = "assertedevidence";
        rel.source_refs.push_back(new_elem.id);
        break;
    case NewElementKind::Context:
        new_elem.type = "artifactreference";
        rel.type = "assertedcontext";
        rel.source_refs.push_back(new_elem.id);
        break;
    case NewElementKind::Assumption:
        new_elem.type = "claim";
        new_elem.assertion_declaration = "assumed";
        rel.type = "assertedcontext";
        rel.source_refs.push_back(new_elem.id);
        break;
    case NewElementKind::Justification:
        new_elem.type = "claim";
        new_elem.assertion_declaration = "justification";
        rel.type = "assertedcontext";
        rel.source_refs.push_back(new_elem.id);
        break;
    }

    if (ap) {
        switch (kind) {
        case NewElementKind::Goal:
            MirrorClaim(ap, new_elem);
            MirrorInference(ap, rel);
            break;
        case NewElementKind::Strategy:
            MirrorReasoning(ap, new_elem);
            MirrorInference(ap, rel);
            break;
        case NewElementKind::Solution:
            MirrorArtifactReference(ap, new_elem);
            MirrorEvidence(ap, rel);
            break;
        case NewElementKind::Context:
            MirrorArtifactReference(ap, new_elem);
            MirrorContext(ap, rel);
            break;
        case NewElementKind::Assumption:
        case NewElementKind::Justification:
            MirrorClaim(ap, new_elem);
            MirrorContext(ap, rel);
            break;
        }
    }

    ac.elements.push_back(std::move(new_elem));
    ac.elements.push_back(std::move(rel));
    if (out_created_relationship_id)
        *out_created_relationship_id = relationship_id;
    return true;
}

bool InstallTopGoal(parser::AssuranceCase& ac,
                    sacm::AssuranceCasePackage* pkg,
                    const std::string& element_id,
                    std::string& out_error) {
    if (element_id.empty()) {
        out_error = "Element id must be non-empty.";
        return false;
    }
    parser::SacmElement goal;
    goal.id = element_id;
    goal.gsn_identifier = element_id;
    goal.type = "claim";
    goal.name = DefaultNameFor(NewElementKind::Goal);

    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, goal.id);
    if (ap)
        MirrorClaim(ap, goal);

    ac.elements.push_back(std::move(goal));
    return true;
}

// Shared installation logic for a dialectic challenge. Builds the counter
// element + counter relationship from the supplied ids and installs them into
// both models. Routed through by id-generating `AddChallenge` and replay-only
// `AddChallengeWithIds`.
bool InstallChallenge(parser::AssuranceCase& ac,
                      sacm::AssuranceCasePackage* pkg,
                      const ArgumentTarget& target,
                      ChallengeSourceType source_type,
                      const std::string& element_id,
                      const std::string& relationship_id,
                      std::string& out_error) {
    if (target.id.empty()) {
        out_error = "No challenge target supplied.";
        return false;
    }
    if (element_id.empty() || relationship_id.empty()) {
        out_error = "Element and relationship ids must be non-empty.";
        return false;
    }

    const parser::SacmElement* target_elem = FindElement(ac, target.id);
    if (!target_elem) {
        out_error = "Challenge target not found in model.";
        return false;
    }

    // The counter element joins the argument package owning the target. When the
    // target is a relationship (which FindOwningArgumentPackage does not index),
    // anchor on the element the relationship points at.
    std::string anchor_id = target.id;
    if (target.kind == ArgumentTarget::Kind::Relationship && !target_elem->target_refs.empty()) {
        anchor_id = target_elem->target_refs.front();
    }
    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, anchor_id);

    parser::SacmElement new_elem;
    new_elem.id = element_id;
    new_elem.gsn_identifier = element_id;
    new_elem.name = "";

    parser::SacmElement rel;
    rel.id = relationship_id;
    rel.is_counter = true;
    rel.source_refs.push_back(element_id);
    rel.target_refs.push_back(target.id);

    switch (source_type) {
    case ChallengeSourceType::CounterArgument:
        new_elem.type = "claim";
        rel.type = "assertedinference";
        break;
    case ChallengeSourceType::CounterEvidence:
        new_elem.type = "artifactreference";
        rel.type = "assertedevidence";
        break;
    }

    if (ap) {
        switch (source_type) {
        case ChallengeSourceType::CounterArgument:
            MirrorClaim(ap, new_elem);
            MirrorInference(ap, rel);
            break;
        case ChallengeSourceType::CounterEvidence:
            MirrorArtifactReference(ap, new_elem);
            MirrorEvidence(ap, rel);
            break;
        }
    }

    ac.elements.push_back(std::move(new_elem));
    ac.elements.push_back(std::move(rel));
    return true;
}

} // namespace

bool PlanChildElementIds(const parser::AssuranceCase& ac,
                         const sacm::AssuranceCasePackage* pkg,
                         const std::string& parent_id,
                         NewElementKind kind,
                         std::string& out_element_id,
                         std::string& out_relationship_id,
                         std::string& out_error) {
    out_element_id.clear();
    out_relationship_id.clear();
    out_error.clear();

    if (parent_id.empty()) {
        out_error = "No parent element selected.";
        return false;
    }
    const parser::SacmElement* parent = FindElement(ac, parent_id);
    if (!parent) {
        out_error = "Selected element not found in model.";
        return false;
    }
    if (!CanAddChildElement(*parent, kind, out_error))
        return false;

    std::unordered_set<std::string> existing_ids = CollectIds(ac);
    const sacm::ArgumentPackage* ap = FindOwningArgumentPackageConst(pkg, parent_id);

    out_element_id = GenerateUniqueId(existing_ids, ScopedPrefixFor(ap, kind));
    existing_ids.insert(out_element_id);
    out_relationship_id = GenerateUniqueId(existing_ids, ScopedRelationshipPrefixFor(ap));
    return true;
}

bool PlanChallengeIds(const parser::AssuranceCase& ac,
                      const sacm::AssuranceCasePackage* pkg,
                      const ArgumentTarget& target,
                      ChallengeSourceType source_type,
                      std::string& out_element_id,
                      std::string& out_relationship_id,
                      std::string& out_error) {
    out_element_id.clear();
    out_relationship_id.clear();
    out_error.clear();

    if (target.id.empty()) {
        out_error = "No challenge target supplied.";
        return false;
    }
    const parser::SacmElement* target_elem = FindElement(ac, target.id);
    if (!target_elem) {
        out_error = "Challenge target not found in model.";
        return false;
    }

    // A relationship target is not indexed by the owning-package lookup, so
    // anchor on the element it points at (matching InstallChallenge).
    std::string anchor_id = target.id;
    if (target.kind == ArgumentTarget::Kind::Relationship && !target_elem->target_refs.empty()) {
        anchor_id = target_elem->target_refs.front();
    }

    std::unordered_set<std::string> existing_ids = CollectIds(ac);
    const sacm::ArgumentPackage* ap = FindOwningArgumentPackageConst(pkg, anchor_id);

    out_element_id = GenerateUniqueId(existing_ids, ScopedChallengePrefixFor(ap, source_type));
    existing_ids.insert(out_element_id);
    out_relationship_id = GenerateUniqueId(existing_ids, ScopedRelationshipPrefixFor(ap));
    return true;
}

std::string PlanTopGoalId(const parser::AssuranceCase& ac) {
    return GenerateUniqueId(CollectIds(ac), PrefixFor(NewElementKind::Goal));
}

bool AddChildElement(parser::AssuranceCase& ac,
                     sacm::AssuranceCasePackage* pkg,
                     const std::string& parent_id,
                     NewElementKind kind,
                     std::string& out_new_id,
                     std::string& out_error) {
    std::string rel_id;
    return AddChildElement(ac, pkg, parent_id, kind, out_new_id, rel_id, out_error);
}

bool AddChildElement(parser::AssuranceCase& ac,
                     sacm::AssuranceCasePackage* pkg,
                     const std::string& parent_id,
                     NewElementKind kind,
                     std::string& out_new_id,
                     std::string& out_new_relationship_id,
                     std::string& out_error) {
    out_new_id.clear();
    out_new_relationship_id.clear();
    out_error.clear();

    std::string element_id;
    std::string relationship_id;
    if (!PlanChildElementIds(ac, pkg, parent_id, kind, element_id, relationship_id, out_error))
        return false;

    // A strategy and an inference-extending sub-goal create no relationship, so the
    // event must record an empty relationship id -- take the id actually created.
    std::string created_relationship_id;
    if (!InstallChildElement(
            ac, pkg, parent_id, kind, element_id, relationship_id, out_error, &created_relationship_id))
        return false;

    out_new_id = std::move(element_id);
    out_new_relationship_id = std::move(created_relationship_id);
    return true;
}

bool AddChildElementWithIds(parser::AssuranceCase& ac,
                            sacm::AssuranceCasePackage* pkg,
                            const std::string& parent_id,
                            NewElementKind kind,
                            const std::string& element_id,
                            const std::string& relationship_id,
                            std::string& out_error) {
    out_error.clear();
    return InstallChildElement(ac, pkg, parent_id, kind, element_id, relationship_id, out_error);
}

bool AddChallenge(parser::AssuranceCase& ac,
                  sacm::AssuranceCasePackage* pkg,
                  const ArgumentTarget& target,
                  ChallengeSourceType source_type,
                  std::string& out_new_id,
                  std::string& out_new_relationship_id,
                  std::string& out_error) {
    out_new_id.clear();
    out_new_relationship_id.clear();
    out_error.clear();

    std::string element_id;
    std::string relationship_id;
    if (!PlanChallengeIds(ac, pkg, target, source_type, element_id, relationship_id, out_error))
        return false;

    if (!InstallChallenge(ac, pkg, target, source_type, element_id, relationship_id, out_error))
        return false;

    out_new_id = std::move(element_id);
    out_new_relationship_id = std::move(relationship_id);
    return true;
}

bool AddChallengeWithIds(parser::AssuranceCase& ac,
                         sacm::AssuranceCasePackage* pkg,
                         const ArgumentTarget& target,
                         ChallengeSourceType source_type,
                         const std::string& element_id,
                         const std::string& relationship_id,
                         std::string& out_error) {
    out_error.clear();
    return InstallChallenge(ac, pkg, target, source_type, element_id, relationship_id, out_error);
}

bool AddTopGoal(parser::AssuranceCase& ac,
                sacm::AssuranceCasePackage* pkg,
                std::string& out_new_id,
                std::string& out_error) {
    out_new_id.clear();
    out_error.clear();

    std::string id = PlanTopGoalId(ac);
    if (!InstallTopGoal(ac, pkg, id, out_error))
        return false;
    out_new_id = std::move(id);
    return true;
}

bool AddTopGoalWithId(parser::AssuranceCase& ac,
                      sacm::AssuranceCasePackage* pkg,
                      const std::string& element_id,
                      std::string& out_error) {
    out_error.clear();
    return InstallTopGoal(ac, pkg, element_id, out_error);
}

// ===== Remove helpers (planner) ============================================

namespace {

// ---------------------------------------------------------------------------
// TreeIndex: a single canonical view of the visual tree, computed from the
// parser model the same way `AssuranceTree::Build` wires the canvas. All
// removal logic (parent lookup, descendants, siblings) goes through this so
// strategies behave like ordinary tree nodes:
//
//   * For an AssertedInference with target T:
//       - if reasoning R is set: parent[R] = T, parent[S] = R for each source S
//       - else                 : parent[S] = T for each source S
//   * For an AssertedContext with target T: parent[S] = T for each source S
//     (Group2 attachment, but a child of T for removal purposes).
//   * For an AssertedEvidence with target T: parent[S] = T for each source S.
//
// First-write-wins so a node that already has a parent isn't reparented (this
// matches AssuranceTree::Build's `parent == nullptr` guard).
// ---------------------------------------------------------------------------
struct TreeIndex {
    std::unordered_map<std::string, std::string> parent_of;
    std::unordered_map<std::string, std::vector<std::string>> children_of;
    // Group2 (context) attachment children, separated so NodeOnly removal can
    // sweep them with the node while leaving other children behind.
    std::unordered_map<std::string, std::vector<std::string>> group2_of;

    const std::string& ParentOf(const std::string& id) const {
        static const std::string empty;
        auto it = parent_of.find(id);
        return it == parent_of.end() ? empty : it->second;
    }
    const std::vector<std::string>& ChildrenOf(const std::string& id) const {
        static const std::vector<std::string> empty;
        auto it = children_of.find(id);
        return it == children_of.end() ? empty : it->second;
    }
    const std::vector<std::string>& Group2Of(const std::string& id) const {
        static const std::vector<std::string> empty;
        auto it = group2_of.find(id);
        return it == group2_of.end() ? empty : it->second;
    }
};

void AddEdge(TreeIndex& idx, const std::string& parent, const std::string& child, bool group2 = false) {
    if (parent.empty() || child.empty() || parent == child)
        return;
    auto inserted = idx.parent_of.emplace(child, parent);
    if (!inserted.second)
        return; // first-write-wins
    idx.children_of[parent].push_back(child);
    if (group2)
        idx.group2_of[parent].push_back(child);
}

std::unordered_set<std::string> BuildNodeIdSet(const parser::AssuranceCase& ac) {
    std::unordered_set<std::string> ids;
    for (const auto& e : ac.elements) {
        if (!IsRelationshipType(e.type) && !e.id.empty())
            ids.insert(e.id);
    }
    return ids;
}

std::string FindFirstExistingTarget(const std::vector<std::string>& target_refs,
                                    const std::unordered_set<std::string>& node_ids) {
    for (const auto& target : target_refs) {
        if (!target.empty() && node_ids.find(target) != node_ids.end())
            return target;
    }
    return {};
}

TreeIndex BuildTreeIndex(const parser::AssuranceCase& ac, const std::unordered_set<std::string>& node_ids) {
    TreeIndex idx;
    for (const auto& e : ac.elements) {
        // Counter (dialectic challenge) relationships are not structural support:
        // the counter source must not be wired as a child of its target, or
        // removing the target would cascade into the challenge.
        if (e.is_counter)
            continue;
        const std::string target = FindFirstExistingTarget(e.target_refs, node_ids);
        if (target.empty())
            continue;

        if (e.type == "assertedinference") {
            std::string attach_parent = target;
            if (!e.reasoning_ref.empty() && node_ids.find(e.reasoning_ref) != node_ids.end()) {
                AddEdge(idx, target, e.reasoning_ref);
                attach_parent = e.reasoning_ref;
            }
            for (const auto& s : e.source_refs) {
                if (!s.empty() && node_ids.find(s) != node_ids.end()) {
                    AddEdge(idx, attach_parent, s);
                }
            }
        } else if (e.type == "assertedcontext") {
            for (const auto& s : e.source_refs) {
                if (!s.empty() && node_ids.find(s) != node_ids.end()) {
                    AddEdge(idx, target, s, /*group2=*/true);
                }
            }
        } else if (e.type == "assertedevidence") {
            for (const auto& s : e.source_refs) {
                if (!s.empty() && node_ids.find(s) != node_ids.end()) {
                    AddEdge(idx, target, s);
                }
            }
        }
    }
    return idx;
}

TreeIndex BuildTreeIndex(const parser::AssuranceCase& ac) {
    return BuildTreeIndex(ac, BuildNodeIdSet(ac));
}

// Find the structural parent of `id` in the visual tree.
std::string FindStructuralParent(const parser::AssuranceCase& ac, const std::string& id) {
    return BuildTreeIndex(ac).ParentOf(id);
}

// Collect ids of Group2 attachments (Context/Assumption/Justification claims)
// that hang off `node_id` via assertedcontext relationships.
void CollectGroup2AttachmentIds(const TreeIndex& idx,
                                const std::string& node_id,
                                std::unordered_set<std::string>& out) {
    for (const auto& s : idx.Group2Of(node_id))
        out.insert(s);
}

} // namespace

namespace {

// Collect the closed set of ids reachable as descendants of `root_id` in the
// canonical visual tree (see TreeIndex above). `root_id` itself is included.
std::unordered_set<std::string> CollectSubtreeIds(const TreeIndex& idx, const std::string& root_id) {
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack;
    stack.push_back(root_id);
    while (!stack.empty()) {
        std::string current = std::move(stack.back());
        stack.pop_back();
        if (!visited.insert(current).second)
            continue;
        for (const auto& c : idx.ChildrenOf(current))
            stack.push_back(c);
    }
    return visited;
}

// Remove from `vec` every element whose id is in `removed_ids`.
template <typename T>
void EraseByIdSet(std::vector<T>& vec, const std::unordered_set<std::string>& removed_ids) {
    std::erase_if(vec, [&](const T& item) { return removed_ids.count(item.id) > 0; });
}

// A glossary term cites the element its definition comes from (Term.origin,
// clause 10.7). The element going means the citation goes; the term stays. The
// library's delete does the same for a document it owns, and the two models
// have to agree or a replayed removal and a live one would differ by a term.
void ScrubTermOrigins(sacm::TerminologyPackage& package, const std::unordered_set<std::string>& removed_ids) {
    for (sacm::Term& term : package.terms) {
        if (!term.origin.empty() && removed_ids.count(term.origin) > 0)
            term.origin.clear();
    }
}

// Strip removed ids from the source/target vectors of a SACM relationship.
void ScrubRelationshipRefs(sacm::AssertedRelationship& rel, const std::unordered_set<std::string>& removed_ids) {
    std::erase_if(rel.sources, [&](const std::string& r) { return removed_ids.count(r) > 0; });
    std::erase_if(rel.targets, [&](const std::string& r) { return removed_ids.count(r) > 0; });
}

// True if a SACM relationship is now structurally empty: has no targets, or
// has neither sources nor a reasoning reference. A relationship that still
// connects at least one source (or a reasoning) to at least one target is
// kept so siblings of the removed element survive.
bool IsRelationshipDangling(const sacm::AssertedRelationship& rel) {
    if (rel.targets.empty())
        return true;
    return rel.sources.empty();
}
bool IsInferenceDangling(const sacm::AssertedInference& inf) {
    if (inf.targets.empty())
        return true;
    return inf.sources.empty() && inf.reasoning.empty();
}

// Strip removed ids from a parser-side relationship's source/target/reasoning
// references.
void ScrubParserRelationshipRefs(parser::SacmElement& rel, const std::unordered_set<std::string>& removed_ids) {
    std::erase_if(rel.source_refs, [&](const std::string& r) { return removed_ids.count(r) > 0; });
    std::erase_if(rel.target_refs, [&](const std::string& r) { return removed_ids.count(r) > 0; });
    if (!rel.reasoning_ref.empty() && removed_ids.count(rel.reasoning_ref)) {
        rel.reasoning_ref.clear();
    }
}

} // namespace

// True if a parser-side relationship has been emptied out by scrubbing (or
// never had anything to relate). Exported: the library-primary NodeOnly removal
// and the audit replayer apply this same policy to reparent endpoint rewrites --
// see element_factory.h.
bool IsParserRelationshipDangling(const parser::SacmElement& rel) {
    if (rel.target_refs.empty())
        return true;
    if (rel.type == "assertedinference") {
        return rel.source_refs.empty() && rel.reasoning_ref.empty();
    }
    return rel.source_refs.empty();
}

int CountDescendants(const parser::AssuranceCase& ac, const std::string& id) {
    if (id.empty())
        return 0;
    auto idx = BuildTreeIndex(ac);
    auto subtree = CollectSubtreeIds(idx, id);
    // subtree includes the root itself; descendants = everything else.
    return subtree.empty() ? 0 : static_cast<int>(subtree.size() - 1);
}

std::unordered_set<std::string> PlanRemoval(const parser::AssuranceCase& ac, const std::string& id, RemoveMode mode) {
    std::unordered_set<std::string> result;
    if (id.empty() || !FindElement(ac, id))
        return result;
    auto idx = BuildTreeIndex(ac);

    if (mode == RemoveMode::NodeOnly) {
        result.insert(id);
    } else { // NodeAndDescendants
        result = CollectSubtreeIds(idx, id);
    }
    // Sweep Group2 attachments (Context/Assumption/Justification) for every
    // node in the plan so they don't dangle after deletion.
    std::vector<std::string> snapshot(result.begin(), result.end());
    for (const auto& nid : snapshot) {
        CollectGroup2AttachmentIds(idx, nid, result);
    }
    return result;
}

// Reparent the structural children of `node_id` to its structural parent.
// No-op if the node has no structural parent (root or orphan). Declared in the
// header because a library-primary caller needs the reparent WITHOUT the
// scrub-then-erase that follows it in RemoveElement -- see element_factory.h.
void ReparentChildrenToParent(parser::AssuranceCase& ac, sacm::AssuranceCasePackage* pkg, const std::string& node_id) {
    std::string parent_id = FindStructuralParent(ac, node_id);
    if (parent_id.empty())
        return;

    // If `node_id` is interposed as a strategy (reasoning of an inference),
    // its sub-goals are already sources of the same inference. Clearing the
    // reasoning_ref promotes them to direct children of the inference target.
    for (auto& e : ac.elements) {
        if (e.type != "assertedinference")
            continue;
        if (e.reasoning_ref == node_id)
            e.reasoning_ref.clear();
    }
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            for (auto& inf : ap.assertedInferences) {
                if (inf.reasoning == node_id)
                    inf.reasoning.clear();
            }
        }
    }

    // For inferences/evidence relationships targeting `node_id`, rewrite the
    // target to `parent_id` so the children get promoted up the tree.
    for (auto& e : ac.elements) {
        if (e.type != "assertedinference" && e.type != "assertedevidence")
            continue;
        for (auto& t : e.target_refs) {
            if (t == node_id)
                t = parent_id;
        }
    }
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            auto rewrite = [&](std::vector<std::string>& targets) {
                for (auto& t : targets) {
                    if (t == node_id)
                        t = parent_id;
                }
            };
            for (auto& inf : ap.assertedInferences)
                rewrite(inf.targets);
            for (auto& ev : ap.assertedEvidences)
                rewrite(ev.targets);
            // Don't rewrite assertedContexts: contexts of node_id go away with it.
        }
    }
}

bool RemoveElement(parser::AssuranceCase& ac,
                   sacm::AssuranceCasePackage* pkg,
                   const std::string& id,
                   RemoveMode mode,
                   std::string& out_error) {
    out_error.clear();
    if (id.empty()) {
        out_error = "No element id supplied.";
        return false;
    }
    if (!FindElement(ac, id)) {
        out_error = "Element not found in model.";
        return false;
    }

    auto removed_ids = PlanRemoval(ac, id, mode);
    if (removed_ids.empty()) {
        out_error = "Nothing to remove.";
        return false;
    }

    // For NodeOnly, reparent structural children before we scrub references
    // so they end up attached to the grandparent (or, for a strategy, become
    // direct sources of the parent inference).
    if (mode == RemoveMode::NodeOnly) {
        ReparentChildrenToParent(ac, pkg, id);
    }

    // ---- Parser model: scrub references then drop dead/empty relationships -
    for (auto& e : ac.elements) {
        if (IsRelationshipType(e.type)) {
            ScrubParserRelationshipRefs(e, removed_ids);
        }
        if (e.type == "term" && !e.origin_ref.empty() && removed_ids.count(e.origin_ref) > 0) {
            e.origin_ref.clear();
        }
    }
    std::erase_if(ac.elements, [&](const parser::SacmElement& e) {
        if (removed_ids.count(e.id))
            return true;
        if (!IsRelationshipType(e.type))
            return false;
        return IsParserRelationshipDangling(e);
    });

    // ---- SACM model: scrub references then drop dead/empty relationships ---
    if (pkg) {
        for (auto& tp : pkg->terminologyPackages)
            ScrubTermOrigins(tp, removed_ids);
        for (auto& ap : pkg->argumentPackages) {
            for (auto& tp : ap.terminologyPackages)
                ScrubTermOrigins(tp, removed_ids);
            EraseByIdSet(ap.claims, removed_ids);
            EraseByIdSet(ap.argumentReasonings, removed_ids);
            EraseByIdSet(ap.artifactReferences, removed_ids);

            for (auto& r : ap.assertedInferences) {
                ScrubRelationshipRefs(r, removed_ids);
                if (!r.reasoning.empty() && removed_ids.count(r.reasoning)) {
                    r.reasoning.clear();
                }
            }
            for (auto& r : ap.assertedContexts)
                ScrubRelationshipRefs(r, removed_ids);
            for (auto& r : ap.assertedEvidences)
                ScrubRelationshipRefs(r, removed_ids);

            std::erase_if(ap.assertedInferences, [&](const sacm::AssertedInference& r) {
                if (removed_ids.count(r.id))
                    return true;
                return IsInferenceDangling(r);
            });
            std::erase_if(ap.assertedContexts, [&](const sacm::AssertedContext& r) {
                return removed_ids.count(r.id) > 0 || IsRelationshipDangling(r);
            });
            std::erase_if(ap.assertedEvidences, [&](const sacm::AssertedEvidence& r) {
                return removed_ids.count(r.id) > 0 || IsRelationshipDangling(r);
            });
        }
    }

    return true;
}

// ===== Text-field updates (Phase 1 audit) =====

const char* ElementTextFieldToToken(ElementTextField field) {
    switch (field) {
    case ElementTextField::Name:
        return "name";
    case ElementTextField::Description:
        return "description";
    case ElementTextField::Content:
        return "content";
    }
    return "name";
}

bool ElementTextFieldFromToken(const std::string& token, ElementTextField& out) {
    if (token == "name") {
        out = ElementTextField::Name;
        return true;
    }
    if (token == "description") {
        out = ElementTextField::Description;
        return true;
    }
    if (token == "content") {
        out = ElementTextField::Content;
        return true;
    }
    return false;
}

// Read the current value of `field`/`language` on the parser element.
//
// Exported because `UpdateElementTextCommand` needs the SAME reading when it
// applies natively as this file's legacy mutator does when it bridges: the value
// it captures becomes the audit payload's `old_value`, and a native edit that
// recorded a different old value than a bridged one would make the two routes
// disagree in the log rather than only in the code.
std::string ReadParserField(const parser::SacmElement& elem, ElementTextField field, const std::string& language) {
    auto from_map = [&](const std::map<std::string, std::string>& m, const std::string& scalar) {
        auto it = m.find(language);
        if (it != m.end())
            return it->second;
        return language == "en" ? scalar : std::string{};
    };
    switch (field) {
    case ElementTextField::Name:
        return from_map(elem.name_langs, elem.name);
    case ElementTextField::Description:
        return from_map(elem.description_langs, elem.description);
    case ElementTextField::Content:
        return from_map(elem.content_langs, elem.content);
    }
    return {};
}

namespace {

// Write `new_value` to `field`/`language` on the parser element. Updates the
// canonical scalar too when language == "en".
void WriteParserField(parser::SacmElement& elem,
                      ElementTextField field,
                      const std::string& language,
                      const std::string& new_value) {
    switch (field) {
    case ElementTextField::Name:
        elem.name_langs[language] = new_value;
        if (language == "en")
            elem.name = new_value;
        return;
    case ElementTextField::Description:
        elem.description_langs[language] = new_value;
        if (language == "en")
            elem.description = new_value;
        return;
    case ElementTextField::Content:
        elem.content_langs[language] = new_value;
        if (language == "en")
            elem.content = new_value;
        return;
    }
}

// Mirror the parser-side write onto every matching SACM container element.
// Returns true if at least one SACM element was found and updated; false
// indicates the parser-only state (acceptable — e.g., relationship or
// terminology elements with no SACM-side text). Errors are not signalled
// here because the parser write is the audit source of truth.
template <typename Element>
bool UpdateSacmTexts(Element& e, ElementTextField field, const std::string& language, const std::string& new_value) {
    auto write_ml = [&](sacm::MultiLangText& ml, std::string& scalar) {
        ml.texts[language] = new_value;
        if (language == "en")
            scalar = new_value;
    };
    switch (field) {
    case ElementTextField::Name:
        write_ml(e.name_ml, e.name);
        return true;
    case ElementTextField::Description:
        write_ml(e.description_ml, e.description);
        return true;
    case ElementTextField::Content:
        // The `return false` belongs in the else branch: for a type that does
        // have content_ml, the if-constexpr always returns and a statement after
        // it is unreachable.
        if constexpr (requires {
                          e.content_ml;
                          e.content;
                      }) {
            write_ml(e.content_ml, e.content);
            return true;
        } else {
            return false;
        }
    }
    return false;
}

bool UpdateSacmElementText(sacm::AssuranceCasePackage& pkg,
                           const std::string& element_id,
                           ElementTextField field,
                           const std::string& language,
                           const std::string& new_value) {
    for (auto& ap : pkg.argumentPackages) {
        for (auto& c : ap.claims)
            if (c.id == element_id)
                return UpdateSacmTexts(c, field, language, new_value);
        for (auto& ar : ap.argumentReasonings)
            if (ar.id == element_id)
                return UpdateSacmTexts(ar, field, language, new_value);
        for (auto& ar : ap.artifactReferences)
            if (ar.id == element_id)
                return UpdateSacmTexts(ar, field, language, new_value);
        for (auto& ai : ap.assertedInferences)
            if (ai.id == element_id)
                return UpdateSacmTexts(ai, field, language, new_value);
        for (auto& ac : ap.assertedContexts)
            if (ac.id == element_id)
                return UpdateSacmTexts(ac, field, language, new_value);
        for (auto& ae : ap.assertedEvidences)
            if (ae.id == element_id)
                return UpdateSacmTexts(ae, field, language, new_value);
    }
    for (auto& artpkg : pkg.artifactPackages) {
        for (auto& a : artpkg.artifacts)
            if (a.id == element_id)
                return UpdateSacmTexts(a, field, language, new_value);
    }
    for (auto& tp : pkg.terminologyPackages) {
        for (auto& e : tp.expressions)
            if (e.id == element_id)
                return UpdateSacmTexts(e, field, language, new_value);
    }
    return false;
}

void UpsertGsnIdentifierTag(sacm::SacmElement& element, const std::string& identifier) {
    for (sacm::TaggedValue& tag : element.taggedValues) {
        if (tag.key == kGsnIdentifierTagKey) {
            tag.value = identifier;
            return;
        }
    }
    element.taggedValues.push_back(sacm::TaggedValue{
        .id = element.id + "__gsnIdentifier",
        .key = kGsnIdentifierTagKey,
        .value = identifier,
    });
}

bool UpdateSacmGsnIdentifier(sacm::AssuranceCasePackage& pkg,
                             const std::string& element_id,
                             const std::string& identifier) {
    for (sacm::ArgumentPackage& ap : pkg.argumentPackages) {
        for (sacm::Claim& element : ap.claims) {
            if (element.id == element_id) {
                UpsertGsnIdentifierTag(element, identifier);
                return true;
            }
        }
        for (sacm::ArgumentReasoning& element : ap.argumentReasonings) {
            if (element.id == element_id) {
                UpsertGsnIdentifierTag(element, identifier);
                return true;
            }
        }
        for (sacm::ArtifactReference& element : ap.artifactReferences) {
            if (element.id == element_id) {
                UpsertGsnIdentifierTag(element, identifier);
                return true;
            }
        }
        for (sacm::TerminologyPackage& terminology : ap.terminologyPackages) {
            for (sacm::Expression& element : terminology.expressions) {
                if (element.id == element_id) {
                    UpsertGsnIdentifierTag(element, identifier);
                    return true;
                }
            }
            for (sacm::Term& element : terminology.terms) {
                if (element.id == element_id) {
                    UpsertGsnIdentifierTag(element, identifier);
                    return true;
                }
            }
        }
    }
    for (sacm::ArtifactPackage& artifacts : pkg.artifactPackages) {
        for (sacm::Artifact& element : artifacts.artifacts) {
            if (element.id == element_id) {
                UpsertGsnIdentifierTag(element, identifier);
                return true;
            }
        }
    }
    for (sacm::TerminologyPackage& terminology : pkg.terminologyPackages) {
        for (sacm::Expression& element : terminology.expressions) {
            if (element.id == element_id) {
                UpsertGsnIdentifierTag(element, identifier);
                return true;
            }
        }
        for (sacm::Term& element : terminology.terms) {
            if (element.id == element_id) {
                UpsertGsnIdentifierTag(element, identifier);
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool SetElementTextField(parser::AssuranceCase& ac,
                         sacm::AssuranceCasePackage* pkg,
                         const std::string& element_id,
                         ElementTextField field,
                         const std::string& language,
                         const std::string& new_value,
                         std::string& out_old_value,
                         std::string& out_error) {
    out_old_value.clear();
    out_error.clear();
    if (element_id.empty()) {
        out_error = "Element id is empty.";
        return false;
    }
    if (language.empty()) {
        out_error = "Language code is empty.";
        return false;
    }
    parser::SacmElement* elem = nullptr;
    for (auto& e : ac.elements) {
        if (e.id == element_id) {
            elem = &e;
            break;
        }
    }
    if (!elem) {
        out_error = "Element not found: " + element_id;
        return false;
    }
    if (field == ElementTextField::Content) {
        if (elem->type != "claim" && elem->type != "argumentreasoning") {
            out_error = "Element " + element_id + " of type '" + elem->type + "' has no content field.";
            return false;
        }
    }

    out_old_value = ReadParserField(*elem, field, language);
    if (out_old_value == new_value)
        return true; // no-op

    WriteParserField(*elem, field, language, new_value);
    if (pkg)
        UpdateSacmElementText(*pkg, element_id, field, language, new_value);
    return true;
}

std::string GsnIdentifierFor(const parser::SacmElement& element) {
    if (!element.gsn_identifier.empty())
        return element.gsn_identifier;
    return element.id;
}

bool ValidateGsnIdentifierChange(const parser::AssuranceCase& ac,
                                 const std::string& element_id,
                                 const std::string& new_identifier,
                                 std::string& out_normalized_identifier,
                                 std::string& out_old_identifier,
                                 std::string& out_error) {
    out_normalized_identifier.clear();
    out_old_identifier.clear();
    out_error.clear();

    const auto first_non_space = std::find_if_not(
        new_identifier.begin(), new_identifier.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last_non_space = std::find_if_not(new_identifier.rbegin(), new_identifier.rend(), [](unsigned char ch) {
                                    return std::isspace(ch) != 0;
                                }).base();
    if (first_non_space == new_identifier.end()) {
        out_error = "GSN identifier must be non-empty.";
        return false;
    }
    const std::string normalized_identifier(first_non_space, last_non_space);
    if (normalized_identifier != new_identifier) {
        out_error = "GSN identifier must not start or end with whitespace.";
        return false;
    }

    const parser::SacmElement* target = nullptr;
    for (const parser::SacmElement& element : ac.elements) {
        if (element.id == element_id) {
            target = &element;
            break;
        }
    }
    if (target == nullptr) {
        out_error = "Element not found: " + element_id;
        return false;
    }
    if (IsRelationshipType(target->type)) {
        out_error = "GSN notation identifiers can only be edited on nodes.";
        return false;
    }

    for (const parser::SacmElement& element : ac.elements) {
        if (&element == target || IsRelationshipType(element.type))
            continue;
        if (GsnIdentifierFor(element) == normalized_identifier) {
            out_error = "GSN identifier '" + normalized_identifier + "' is already used by element " + element.id + ".";
            return false;
        }
    }

    out_normalized_identifier = normalized_identifier;
    out_old_identifier = GsnIdentifierFor(*target);
    return true;
}

bool SetGsnIdentifier(parser::AssuranceCase& ac,
                      sacm::AssuranceCasePackage* pkg,
                      const std::string& element_id,
                      const std::string& new_identifier,
                      std::string& out_old_identifier,
                      std::string& out_error) {
    std::string normalized_identifier;
    if (!ValidateGsnIdentifierChange(
            ac, element_id, new_identifier, normalized_identifier, out_old_identifier, out_error))
        return false;
    if (out_old_identifier == normalized_identifier)
        return true;

    if (pkg != nullptr && !UpdateSacmGsnIdentifier(*pkg, element_id, normalized_identifier)) {
        out_error = "SACM element not found for GSN identifier update: " + element_id;
        return false;
    }
    for (parser::SacmElement& element : ac.elements) {
        if (element.id == element_id) {
            element.gsn_identifier = normalized_identifier;
            break;
        }
    }
    return true;
}

std::string NextFreeGsnIdentifier(const parser::AssuranceCase& ac, const std::string& element_id) {
    const parser::SacmElement* target = FindElement(ac, element_id);
    if (!target)
        return std::string();

    // Recover the prefix from the identifier the element already answers to
    // ("G1" -> "G", "Sn12" -> "Sn", "ACP1_G2" -> "ACP1_G") rather than deriving
    // it from the element's kind. An ArtifactReference is a Solution or a
    // Context depending only on how it is wired, so kind cannot decide between
    // "Sn" and "C" -- and keeping the author's own prefix means a renumber stays
    // inside whatever scheme the file already uses.
    const std::string current = GsnIdentifierFor(*target);
    size_t prefix_end = current.size();
    while (prefix_end > 0 && std::isdigit(static_cast<unsigned char>(current[prefix_end - 1])))
        --prefix_end;
    const std::string prefix = current.substr(0, prefix_end);

    std::unordered_set<std::string> taken;
    for (const parser::SacmElement& element : ac.elements) {
        if (&element != target && !IsRelationshipType(element.type))
            taken.insert(GsnIdentifierFor(element));
    }

    // Bounded by the number of taken identifiers, so this always terminates.
    for (size_t suffix = 1; suffix <= taken.size() + 1; ++suffix) {
        std::string candidate = prefix + std::to_string(suffix);
        if (taken.find(candidate) == taken.end())
            return candidate;
    }
    return std::string();
}

bool SetElementUndeveloped(parser::AssuranceCase& ac,
                           sacm::AssuranceCasePackage* pkg,
                           const std::string& element_id,
                           bool undeveloped,
                           bool& out_old_value,
                           std::string& out_error) {
    out_error.clear();
    parser::SacmElement* target = nullptr;
    for (parser::SacmElement& element : ac.elements) {
        if (element.id == element_id) {
            target = &element;
            break;
        }
    }
    if (!target) {
        out_error = "Element not found in model: " + element_id + ".";
        return false;
    }

    out_old_value = target->undeveloped;
    if (out_old_value == undeveloped)
        return true;
    target->undeveloped = undeveloped;

    // GSN "undeveloped" is SACM `needsSupport`; see docs/sacm/sacm-gsn-mapping.md.
    if (pkg) {
        for (sacm::ArgumentPackage& argument_package : pkg->argumentPackages) {
            for (sacm::Claim& claim : argument_package.claims) {
                if (claim.id == element_id)
                    claim.undeveloped = undeveloped;
            }
            for (sacm::ArgumentReasoning& reasoning : argument_package.argumentReasonings) {
                if (reasoning.id == element_id)
                    reasoning.undeveloped = undeveloped;
            }
        }
    }
    return true;
}

bool ElementHasSecondaryTranslation(const parser::SacmElement& element) {
    const std::map<std::string, std::string>* maps[] = {
        &element.name_langs, &element.description_langs, &element.content_langs};
    for (const std::map<std::string, std::string>* langs : maps) {
        for (const auto& [lang, text] : *langs) {
            if (lang != "en" && !text.empty())
                return true;
        }
    }
    return false;
}

} // namespace core
