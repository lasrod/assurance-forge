#include "core/element_factory.h"

#include "core/acp/assurance_claim_point.h"
#include "core/pattern_model.h"

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

// Determine which ArgumentPackage in the sacm model owns the parent element.
// Falls back to the first package, creating one if necessary.
sacm::ArgumentPackage* FindOwningArgumentPackage(sacm::AssuranceCasePackage* pkg, const std::string& parent_id) {
    if (!pkg)
        return nullptr;
    for (auto& ap : pkg->argumentPackages) {
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
    if (pkg->argumentPackages.empty()) {
        pkg->argumentPackages.emplace_back();
    }
    return &pkg->argumentPackages.front();
}

// Find an argument package by id (preferred) or gid. Returns nullptr when
// `package_id` is empty or no package matches.
sacm::ArgumentPackage* FindArgumentPackageById(sacm::AssuranceCasePackage* pkg, const std::string& package_id) {
    if (!pkg || package_id.empty())
        return nullptr;
    for (auto& ap : pkg->argumentPackages) {
        if ((!ap.id.empty() && ap.id == package_id) || (!ap.gid.empty() && ap.gid == package_id))
            return &ap;
    }
    return nullptr;
}

void MirrorClaim(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap)
        return;
    sacm::Claim c;
    c.id = src.id;
    c.name = src.name;
    c.name_ml.set("en", src.name);
    c.assertionDeclaration = src.assertion_declaration;
    c.undeveloped = src.undeveloped;
    ap->claims.push_back(std::move(c));
}

void MirrorReasoning(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap)
        return;
    sacm::ArgumentReasoning r;
    r.id = src.id;
    r.name = src.name;
    r.name_ml.set("en", src.name);
    ap->argumentReasonings.push_back(std::move(r));
}

void MirrorArtifactReference(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap)
        return;
    sacm::ArtifactReference ar;
    ar.id = src.id;
    ar.name = src.name;
    ar.name_ml.set("en", src.name);
    ap->artifactReferences.push_back(std::move(ar));
}

void MirrorInference(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap)
        return;
    sacm::AssertedInference ai;
    ai.id = rel.id;
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
    ac.sources = rel.source_refs;
    ac.targets = rel.target_refs;
    ap->assertedContexts.push_back(std::move(ac));
}

void MirrorEvidence(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap)
        return;
    sacm::AssertedEvidence ae;
    ae.id = rel.id;
    ae.sources = rel.source_refs;
    ae.targets = rel.target_refs;
    ae.isCounter = rel.is_counter;
    ap->assertedEvidences.push_back(std::move(ae));
}

} // namespace

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
                         std::string& out_error) {
    if (parent_id.empty()) {
        out_error = "No parent element selected.";
        return false;
    }
    if (element_id.empty() || relationship_id.empty()) {
        out_error = "Element and relationship ids must be non-empty.";
        return false;
    }

    const parser::SacmElement* parent = FindElement(ac, parent_id);
    if (!parent) {
        out_error = "Selected element not found in model.";
        return false;
    }

    const std::string& ptype = parent->type;
    const bool parent_is_container = (ptype == "claim" || ptype == "argumentreasoning");
    if (!parent_is_container) {
        out_error = "Cannot add a child to a leaf element (" + ptype + ").";
        return false;
    }
    if (kind == NewElementKind::Strategy && ptype != "claim") {
        out_error = "Strategy can only be added under a Claim.";
        return false;
    }

    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, parent_id);

    parser::SacmElement new_elem;
    new_elem.id = element_id;
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
    return true;
}

bool InstallTopGoal(parser::AssuranceCase& ac,
                    sacm::AssuranceCasePackage* pkg,
                    const std::string& element_id,
                    const std::string& target_package_id,
                    std::string& out_error) {
    if (element_id.empty()) {
        out_error = "Element id must be non-empty.";
        return false;
    }
    parser::SacmElement goal;
    goal.id = element_id;
    goal.type = "claim";
    goal.name = DefaultNameFor(NewElementKind::Goal);

    // A new top goal has no parent to locate its owning package, so route it to
    // the explicitly requested package (the active canvas's argument package,
    // e.g. a pattern). Fall back to the first/main package when unspecified.
    sacm::ArgumentPackage* ap = FindArgumentPackageById(pkg, target_package_id);
    if (!ap)
        ap = FindOwningArgumentPackage(pkg, goal.id);
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

    // Dialectic challenges are forbidden inside a GSN pattern definition
    // (ADR-0006/0007). Reject at the command layer so replay and programmatic
    // callers are guarded even though Pattern View hides the UI actions.
    if (ap && IsPatternPackage(*ap)) {
        out_error = "Challenges cannot be added inside a GSN pattern.";
        return false;
    }

    parser::SacmElement new_elem;
    new_elem.id = element_id;
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

    if (parent_id.empty()) {
        out_error = "No parent element selected.";
        return false;
    }
    const parser::SacmElement* parent = FindElement(ac, parent_id);
    if (!parent) {
        out_error = "Selected element not found in model.";
        return false;
    }
    const std::string& ptype = parent->type;
    const bool parent_is_container = (ptype == "claim" || ptype == "argumentreasoning");
    if (!parent_is_container) {
        out_error = "Cannot add a child to a leaf element (" + ptype + ").";
        return false;
    }
    if (kind == NewElementKind::Strategy && ptype != "claim") {
        out_error = "Strategy can only be added under a Claim.";
        return false;
    }

    auto existing_ids = CollectIds(ac);
    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, parent_id);

    std::string element_id = GenerateUniqueId(existing_ids, ScopedPrefixFor(ap, kind));
    existing_ids.insert(element_id);
    std::string relationship_id = GenerateUniqueId(existing_ids, ScopedRelationshipPrefixFor(ap));

    if (!InstallChildElement(ac, pkg, parent_id, kind, element_id, relationship_id, out_error))
        return false;

    out_new_id = std::move(element_id);
    out_new_relationship_id = std::move(relationship_id);
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

    if (target.id.empty()) {
        out_error = "No challenge target supplied.";
        return false;
    }
    const parser::SacmElement* target_elem = FindElement(ac, target.id);
    if (!target_elem) {
        out_error = "Challenge target not found in model.";
        return false;
    }

    std::string anchor_id = target.id;
    if (target.kind == ArgumentTarget::Kind::Relationship && !target_elem->target_refs.empty()) {
        anchor_id = target_elem->target_refs.front();
    }

    auto existing_ids = CollectIds(ac);
    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, anchor_id);

    std::string element_id = GenerateUniqueId(existing_ids, ScopedChallengePrefixFor(ap, source_type));
    existing_ids.insert(element_id);
    std::string relationship_id = GenerateUniqueId(existing_ids, ScopedRelationshipPrefixFor(ap));

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
                const std::string& target_package_id,
                std::string& out_new_id,
                std::string& out_error) {
    out_new_id.clear();
    out_error.clear();

    auto existing_ids = CollectIds(ac);
    std::string id = GenerateUniqueId(existing_ids, PrefixFor(NewElementKind::Goal));
    if (!InstallTopGoal(ac, pkg, id, target_package_id, out_error))
        return false;
    out_new_id = std::move(id);
    return true;
}

bool AddTopGoalWithId(parser::AssuranceCase& ac,
                      sacm::AssuranceCasePackage* pkg,
                      const std::string& target_package_id,
                      const std::string& element_id,
                      std::string& out_error) {
    out_error.clear();
    return InstallTopGoal(ac, pkg, element_id, target_package_id, out_error);
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

// True if a parser-side relationship has been emptied out by scrubbing.
bool IsParserRelationshipDangling(const parser::SacmElement& rel) {
    if (rel.target_refs.empty())
        return true;
    if (rel.type == "assertedinference") {
        return rel.source_refs.empty() && rel.reasoning_ref.empty();
    }
    return rel.source_refs.empty();
}

} // namespace

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

namespace {

// Reparent the structural children of `node_id` to its structural parent.
// Mutates both models so the subsequent scrub-then-drop pass leaves a coherent
// tree. No-op if the node has no structural parent (root or orphan).
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

} // namespace

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
        for (auto& ap : pkg->argumentPackages) {
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
    case ElementTextField::Name:        return "name";
    case ElementTextField::Description: return "description";
    case ElementTextField::Content:     return "content";
    }
    return "name";
}

bool ElementTextFieldFromToken(const std::string& token, ElementTextField& out) {
    if (token == "name")        { out = ElementTextField::Name; return true; }
    if (token == "description") { out = ElementTextField::Description; return true; }
    if (token == "content")     { out = ElementTextField::Content; return true; }
    return false;
}

namespace {

// Read the current value of `field`/`language` on the parser element.
std::string ReadParserField(const parser::SacmElement& elem,
                            ElementTextField field,
                            const std::string& language) {
    auto from_map = [&](const std::map<std::string, std::string>& m, const std::string& scalar) {
        auto it = m.find(language);
        if (it != m.end())
            return it->second;
        return language == "en" ? scalar : std::string{};
    };
    switch (field) {
    case ElementTextField::Name:        return from_map(elem.name_langs, elem.name);
    case ElementTextField::Description: return from_map(elem.description_langs, elem.description);
    case ElementTextField::Content:     return from_map(elem.content_langs, elem.content);
    }
    return {};
}

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
bool UpdateSacmTexts(Element& e,
                     ElementTextField field,
                     const std::string& language,
                     const std::string& new_value) {
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
        if constexpr (requires { e.content_ml; e.content; }) {
            write_ml(e.content_ml, e.content);
            return true;
        }
        return false;
    }
    return false;
}

bool UpdateSacmElementText(sacm::AssuranceCasePackage& pkg,
                           const std::string& element_id,
                           ElementTextField field,
                           const std::string& language,
                           const std::string& new_value) {
    for (auto& ap : pkg.argumentPackages) {
        for (auto& c : ap.claims)               if (c.id == element_id) return UpdateSacmTexts(c, field, language, new_value);
        for (auto& ar : ap.argumentReasonings)  if (ar.id == element_id) return UpdateSacmTexts(ar, field, language, new_value);
        for (auto& ar : ap.artifactReferences)  if (ar.id == element_id) return UpdateSacmTexts(ar, field, language, new_value);
        for (auto& ai : ap.assertedInferences)  if (ai.id == element_id) return UpdateSacmTexts(ai, field, language, new_value);
        for (auto& ac : ap.assertedContexts)    if (ac.id == element_id) return UpdateSacmTexts(ac, field, language, new_value);
        for (auto& ae : ap.assertedEvidences)   if (ae.id == element_id) return UpdateSacmTexts(ae, field, language, new_value);
    }
    for (auto& artpkg : pkg.artifactPackages) {
        for (auto& a : artpkg.artifacts)        if (a.id == element_id) return UpdateSacmTexts(a, field, language, new_value);
    }
    for (auto& tp : pkg.terminologyPackages) {
        for (auto& e : tp.expressions)          if (e.id == element_id) return UpdateSacmTexts(e, field, language, new_value);
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
        if (e.id == element_id) { elem = &e; break; }
    }
    if (!elem) {
        out_error = "Element not found: " + element_id;
        return false;
    }
    if (field == ElementTextField::Content) {
        if (elem->type != "claim" && elem->type != "argumentreasoning") {
            out_error = "Element " + element_id + " of type '" + elem->type +
                        "' has no content field.";
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

bool SetElementUninstantiated(parser::AssuranceCase& ac,
                              sacm::AssuranceCasePackage* pkg,
                              const std::string& element_id,
                              bool value,
                              std::string& out_error) {
    out_error.clear();
    parser::SacmElement* elem = nullptr;
    for (auto& e : ac.elements) {
        if (e.id == element_id) { elem = &e; break; }
    }
    if (!elem) {
        out_error = "Element not found: " + element_id;
        return false;
    }
    elem->uninstantiated = value;
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            for (auto& c : ap.claims)              if (c.id == element_id)  { SetElementUninstantiated(c, value);   return true; }
            for (auto& ar : ap.argumentReasonings) if (ar.id == element_id) { SetElementUninstantiated(ar, value);  return true; }
            for (auto& ar : ap.artifactReferences) if (ar.id == element_id) { SetElementUninstantiated(ar, value);  return true; }
            for (auto& ai : ap.assertedInferences) if (ai.id == element_id) { SetElementUninstantiated(ai, value);  return true; }
            for (auto& acx : ap.assertedContexts)  if (acx.id == element_id) { SetElementUninstantiated(acx, value); return true; }
            for (auto& ae : ap.assertedEvidences)  if (ae.id == element_id) { SetElementUninstantiated(ae, value);  return true; }
        }
    }
    return true;
}

bool SetElementUndeveloped(parser::AssuranceCase& ac,
                           sacm::AssuranceCasePackage* pkg,
                           const std::string& element_id,
                           bool value,
                           std::string& out_error) {
    out_error.clear();
    parser::SacmElement* elem = nullptr;
    for (auto& e : ac.elements) {
        if (e.id == element_id) { elem = &e; break; }
    }
    if (!elem) {
        out_error = "Element not found: " + element_id;
        return false;
    }
    if (elem->type != "claim" && elem->type != "argumentreasoning") {
        out_error = "Undeveloped applies only to goals and strategies.";
        return false;
    }
    elem->undeveloped = value;
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            for (auto& c : ap.claims)              if (c.id == element_id)  { c.undeveloped = value;  return true; }
            for (auto& ar : ap.argumentReasonings) if (ar.id == element_id) { ar.undeveloped = value; return true; }
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
