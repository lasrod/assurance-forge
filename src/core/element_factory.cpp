#include "core/element_factory.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace core {

namespace {

const char* PrefixFor(NewElementKind kind) {
    switch (kind) {
        case NewElementKind::Goal:          return "G";
        case NewElementKind::Strategy:      return "S";
        case NewElementKind::Solution:      return "Sn";
        case NewElementKind::Context:       return "C";
        case NewElementKind::Assumption:    return "A";
        case NewElementKind::Justification: return "J";
    }
    return "N";
}

const char* DefaultNameFor(NewElementKind kind) {
    switch (kind) {
        case NewElementKind::Goal:          return "New Goal";
        case NewElementKind::Strategy:      return "New Strategy";
        case NewElementKind::Solution:      return "New Solution";
        case NewElementKind::Context:       return "New Context";
        case NewElementKind::Assumption:    return "New Assumption";
        case NewElementKind::Justification: return "New Justification";
    }
    return "New Element";
}

std::unordered_set<std::string> CollectIds(const parser::AssuranceCase& ac) {
    std::unordered_set<std::string> ids;
    ids.reserve(ac.elements.size() * 2);
    for (const auto& e : ac.elements) {
        if (!e.id.empty()) ids.insert(e.id);
    }
    return ids;
}

std::string GenerateUniqueId(const std::unordered_set<std::string>& existing,
                             const std::string& prefix) {
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = prefix + std::to_string(i);
        if (existing.find(candidate) == existing.end()) return candidate;
    }
    return prefix + "x";
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& ac, const std::string& id) {
    for (const auto& e : ac.elements) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

// Determine which ArgumentPackage in the sacm model owns the parent element.
// Falls back to the first package, creating one if necessary.
sacm::ArgumentPackage* FindOwningArgumentPackage(sacm::AssuranceCasePackage* pkg,
                                                 const std::string& parent_id) {
    if (!pkg) return nullptr;
    for (auto& ap : pkg->argumentPackages) {
        for (const auto& c : ap.claims)               if (c.id == parent_id)  return &ap;
        for (const auto& ar : ap.argumentReasonings)  if (ar.id == parent_id) return &ap;
        for (const auto& ar : ap.artifactReferences)  if (ar.id == parent_id) return &ap;
    }
    if (pkg->argumentPackages.empty()) {
        pkg->argumentPackages.emplace_back();
    }
    return &pkg->argumentPackages.front();
}

void MirrorClaim(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap) return;
    sacm::Claim c;
    c.id = src.id;
    c.name = src.name;
    c.name_ml.set("en", src.name);
    c.assertionDeclaration = src.assertion_declaration;
    ap->claims.push_back(std::move(c));
}

void MirrorReasoning(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap) return;
    sacm::ArgumentReasoning r;
    r.id = src.id;
    r.name = src.name;
    r.name_ml.set("en", src.name);
    ap->argumentReasonings.push_back(std::move(r));
}

void MirrorArtifactReference(sacm::ArgumentPackage* ap, const parser::SacmElement& src) {
    if (!ap) return;
    sacm::ArtifactReference ar;
    ar.id = src.id;
    ar.name = src.name;
    ar.name_ml.set("en", src.name);
    ap->artifactReferences.push_back(std::move(ar));
}

void MirrorInference(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap) return;
    sacm::AssertedInference ai;
    ai.id = rel.id;
    ai.sources = rel.source_refs;
    ai.targets = rel.target_refs;
    ai.reasoning = rel.reasoning_ref;
    ap->assertedInferences.push_back(std::move(ai));
}

void MirrorContext(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap) return;
    sacm::AssertedContext ac;
    ac.id = rel.id;
    ac.sources = rel.source_refs;
    ac.targets = rel.target_refs;
    ap->assertedContexts.push_back(std::move(ac));
}

void MirrorEvidence(sacm::ArgumentPackage* ap, const parser::SacmElement& rel) {
    if (!ap) return;
    sacm::AssertedEvidence ae;
    ae.id = rel.id;
    ae.sources = rel.source_refs;
    ae.targets = rel.target_refs;
    ap->assertedEvidences.push_back(std::move(ae));
}

}  // namespace

bool AddChildElement(parser::AssuranceCase& ac,
                     sacm::AssuranceCasePackage* pkg,
                     const std::string& parent_id,
                     NewElementKind kind,
                     std::string& out_new_id,
                     std::string& out_error) {
    out_new_id.clear();
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

    // Minimal validation: only Claim-like elements (claims, strategies) can be parents
    // for structural / contextual children. Solutions and references are leaves.
    const std::string& ptype = parent->type;
    const bool parent_is_container = (ptype == "claim" || ptype == "argumentreasoning");
    if (!parent_is_container) {
        out_error = "Cannot add a child to a leaf element (" + ptype + ").";
        return false;
    }

    // Strategy can only be added under a Claim (matches existing reasoning insertion model).
    if (kind == NewElementKind::Strategy && ptype != "claim") {
        out_error = "Strategy can only be added under a Claim.";
        return false;
    }

    auto existing_ids = CollectIds(ac);

    auto reserve_id = [&](const std::string& prefix) {
        std::string id = GenerateUniqueId(existing_ids, prefix);
        existing_ids.insert(id);
        return id;
    };

    sacm::ArgumentPackage* ap = FindOwningArgumentPackage(pkg, parent_id);

    // Build the new element + its relationship.
    parser::SacmElement new_elem;
    new_elem.id = reserve_id(PrefixFor(kind));
    new_elem.name = DefaultNameFor(kind);

    parser::SacmElement rel;
    rel.id = reserve_id("R");
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
            new_elem.type = "claim";
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

    out_new_id = new_elem.id;

    // Mirror into sacm model first (uses a copy of new_elem before move).
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
            case NewElementKind::Assumption:
            case NewElementKind::Justification:
                MirrorClaim(ap, new_elem);
                MirrorContext(ap, rel);
                break;
        }
    }

    // Append to parser model (drives the tree/canvas rebuild).
    ac.elements.push_back(std::move(new_elem));
    ac.elements.push_back(std::move(rel));

    return true;
}

// ===== Remove helpers (planner) ============================================

namespace {

// Mutable lookup for a parser element by id.
parser::SacmElement* FindElementMut(parser::AssuranceCase& ac, const std::string& id) {
    for (auto& e : ac.elements) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

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

void AddEdge(TreeIndex& idx, const std::string& parent, const std::string& child,
             bool group2 = false) {
    if (parent.empty() || child.empty() || parent == child) return;
    auto inserted = idx.parent_of.emplace(child, parent);
    if (!inserted.second) return;  // first-write-wins
    idx.children_of[parent].push_back(child);
    if (group2) idx.group2_of[parent].push_back(child);
}

TreeIndex BuildTreeIndex(const parser::AssuranceCase& ac) {
    TreeIndex idx;
    for (const auto& e : ac.elements) {
        if (e.target_refs.empty()) continue;
        const std::string& target = e.target_refs[0];
        if (target.empty()) continue;

        if (e.type == "assertedinference") {
            std::string attach_parent = target;
            if (!e.reasoning_ref.empty()) {
                AddEdge(idx, target, e.reasoning_ref);
                attach_parent = e.reasoning_ref;
            }
            for (const auto& s : e.source_refs) AddEdge(idx, attach_parent, s);
        } else if (e.type == "assertedcontext") {
            for (const auto& s : e.source_refs) AddEdge(idx, target, s, /*group2=*/true);
        } else if (e.type == "assertedevidence") {
            for (const auto& s : e.source_refs) AddEdge(idx, target, s);
        }
    }
    return idx;
}

// Find the structural parent of `id` in the visual tree.
std::string FindStructuralParent(const parser::AssuranceCase& ac, const std::string& id) {
    return BuildTreeIndex(ac).ParentOf(id);
}

// Collect ids of Group2 attachments (Context/Assumption/Justification claims)
// that hang off `node_id` via assertedcontext relationships.
void CollectGroup2AttachmentIds(const parser::AssuranceCase& ac,
                                const std::string& node_id,
                                std::unordered_set<std::string>& out) {
    auto idx = BuildTreeIndex(ac);
    for (const auto& s : idx.Group2Of(node_id)) out.insert(s);
}

// All siblings of `node_id` (children of the same tree-parent), including
// `node_id` itself. If `node_id` is a root, returns just `{node_id}`.
std::unordered_set<std::string> CollectSiblingSet(const parser::AssuranceCase& ac,
                                                  const std::string& node_id) {
    std::unordered_set<std::string> result;
    result.insert(node_id);
    auto idx = BuildTreeIndex(ac);
    const std::string& parent = idx.ParentOf(node_id);
    if (parent.empty()) return result;
    for (const auto& c : idx.ChildrenOf(parent)) result.insert(c);
    return result;
}

}  // namespace

namespace {

// True for parser element types that represent relationships (not nodes).
bool IsRelationshipType(const std::string& t) {
    return t == "assertedinference"
        || t == "assertedcontext"
        || t == "assertedevidence";
}

// Collect the closed set of ids reachable as descendants of `root_id` in the
// canonical visual tree (see TreeIndex above). `root_id` itself is included.
std::unordered_set<std::string> CollectSubtreeIds(const parser::AssuranceCase& ac,
                                                  const std::string& root_id) {
    std::unordered_set<std::string> visited;
    auto idx = BuildTreeIndex(ac);
    std::vector<std::string> stack;
    stack.push_back(root_id);
    while (!stack.empty()) {
        std::string current = std::move(stack.back());
        stack.pop_back();
        if (!visited.insert(current).second) continue;
        for (const auto& c : idx.ChildrenOf(current)) stack.push_back(c);
    }
    return visited;
}

// Remove from `vec` every element whose id is in `removed_ids`.
template <typename T>
void EraseByIdSet(std::vector<T>& vec, const std::unordered_set<std::string>& removed_ids) {
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const T& item) { return removed_ids.count(item.id) > 0; }),
              vec.end());
}

// Strip removed ids from the source/target vectors of a SACM relationship.
void ScrubRelationshipRefs(sacm::AssertedRelationship& rel,
                           const std::unordered_set<std::string>& removed_ids) {
    rel.sources.erase(std::remove_if(rel.sources.begin(), rel.sources.end(),
                                     [&](const std::string& r) { return removed_ids.count(r) > 0; }),
                      rel.sources.end());
    rel.targets.erase(std::remove_if(rel.targets.begin(), rel.targets.end(),
                                     [&](const std::string& r) { return removed_ids.count(r) > 0; }),
                      rel.targets.end());
}

// True if a SACM relationship is now structurally empty: has no targets, or
// has neither sources nor a reasoning reference. A relationship that still
// connects at least one source (or a reasoning) to at least one target is
// kept so siblings of the removed element survive.
bool IsRelationshipDangling(const sacm::AssertedRelationship& rel) {
    if (rel.targets.empty()) return true;
    return rel.sources.empty();
}
bool IsInferenceDangling(const sacm::AssertedInference& inf) {
    if (inf.targets.empty()) return true;
    return inf.sources.empty() && inf.reasoning.empty();
}

// Strip removed ids from a parser-side relationship's source/target/reasoning
// references.
void ScrubParserRelationshipRefs(parser::SacmElement& rel,
                                 const std::unordered_set<std::string>& removed_ids) {
    rel.source_refs.erase(
        std::remove_if(rel.source_refs.begin(), rel.source_refs.end(),
                       [&](const std::string& r) { return removed_ids.count(r) > 0; }),
        rel.source_refs.end());
    rel.target_refs.erase(
        std::remove_if(rel.target_refs.begin(), rel.target_refs.end(),
                       [&](const std::string& r) { return removed_ids.count(r) > 0; }),
        rel.target_refs.end());
    if (!rel.reasoning_ref.empty() && removed_ids.count(rel.reasoning_ref)) {
        rel.reasoning_ref.clear();
    }
}

// True if a parser-side relationship has been emptied out by scrubbing.
bool IsParserRelationshipDangling(const parser::SacmElement& rel) {
    if (rel.target_refs.empty()) return true;
    if (rel.type == "assertedinference") {
        return rel.source_refs.empty() && rel.reasoning_ref.empty();
    }
    return rel.source_refs.empty();
}

}  // namespace

int CountDescendants(const parser::AssuranceCase& ac, const std::string& id) {
    if (id.empty()) return 0;
    auto subtree = CollectSubtreeIds(ac, id);
    // subtree includes the root itself; descendants = everything else.
    return subtree.empty() ? 0 : static_cast<int>(subtree.size() - 1);
}

std::unordered_set<std::string> PlanRemoval(const parser::AssuranceCase& ac,
                                            const std::string& id,
                                            RemoveMode mode) {
    std::unordered_set<std::string> result;
    if (id.empty() || !FindElement(ac, id)) return result;

    auto add_attachments_for_all = [&]() {
        std::vector<std::string> snapshot(result.begin(), result.end());
        for (const auto& nid : snapshot) {
            CollectGroup2AttachmentIds(ac, nid, result);
        }
    };

    switch (mode) {
        case RemoveMode::NodeOnly:
            result.insert(id);
            CollectGroup2AttachmentIds(ac, id, result);
            break;
        case RemoveMode::NodeAndDescendants:
            result = CollectSubtreeIds(ac, id);
            add_attachments_for_all();
            break;
        case RemoveMode::NodeAndSiblings: {
            auto siblings = CollectSiblingSet(ac, id);
            for (const auto& s : siblings) {
                auto sub = CollectSubtreeIds(ac, s);
                result.insert(sub.begin(), sub.end());
            }
            add_attachments_for_all();
            break;
        }
    }
    return result;
}

namespace {

// Reparent the structural children of `node_id` to its structural parent.
// Mutates both models so the subsequent scrub-then-drop pass leaves a coherent
// tree. No-op if the node has no structural parent (root or orphan).
void ReparentChildrenToParent(parser::AssuranceCase& ac,
                              sacm::AssuranceCasePackage* pkg,
                              const std::string& node_id) {
    std::string parent_id = FindStructuralParent(ac, node_id);
    if (parent_id.empty()) return;

    // If `node_id` is interposed as a strategy (reasoning of an inference),
    // its sub-goals are already sources of the same inference. Clearing the
    // reasoning_ref promotes them to direct children of the inference target.
    for (auto& e : ac.elements) {
        if (e.type != "assertedinference") continue;
        if (e.reasoning_ref == node_id) e.reasoning_ref.clear();
    }
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            for (auto& inf : ap.assertedInferences) {
                if (inf.reasoning == node_id) inf.reasoning.clear();
            }
        }
    }

    // For inferences/evidence relationships targeting `node_id`, rewrite the
    // target to `parent_id` so the children get promoted up the tree.
    for (auto& e : ac.elements) {
        if (e.type != "assertedinference" && e.type != "assertedevidence") continue;
        for (auto& t : e.target_refs) {
            if (t == node_id) t = parent_id;
        }
    }
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            auto rewrite = [&](std::vector<std::string>& targets) {
                for (auto& t : targets) {
                    if (t == node_id) t = parent_id;
                }
            };
            for (auto& inf : ap.assertedInferences) rewrite(inf.targets);
            for (auto& ev : ap.assertedEvidences)   rewrite(ev.targets);
            // Don't rewrite assertedContexts: contexts of node_id go away with it.
        }
    }
}

}  // namespace

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
    ac.elements.erase(
        std::remove_if(ac.elements.begin(), ac.elements.end(),
                       [&](const parser::SacmElement& e) {
                           if (removed_ids.count(e.id)) return true;
                           if (!IsRelationshipType(e.type)) return false;
                           return IsParserRelationshipDangling(e);
                       }),
        ac.elements.end());

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
            for (auto& r : ap.assertedContexts)   ScrubRelationshipRefs(r, removed_ids);
            for (auto& r : ap.assertedEvidences)  ScrubRelationshipRefs(r, removed_ids);

            ap.assertedInferences.erase(
                std::remove_if(ap.assertedInferences.begin(), ap.assertedInferences.end(),
                               [&](const sacm::AssertedInference& r) {
                                   if (removed_ids.count(r.id)) return true;
                                   return IsInferenceDangling(r);
                               }),
                ap.assertedInferences.end());
            ap.assertedContexts.erase(
                std::remove_if(ap.assertedContexts.begin(), ap.assertedContexts.end(),
                               [&](const sacm::AssertedContext& r) {
                                   return removed_ids.count(r.id) > 0 || IsRelationshipDangling(r);
                               }),
                ap.assertedContexts.end());
            ap.assertedEvidences.erase(
                std::remove_if(ap.assertedEvidences.begin(), ap.assertedEvidences.end(),
                               [&](const sacm::AssertedEvidence& r) {
                                   return removed_ids.count(r.id) > 0 || IsRelationshipDangling(r);
                               }),
                ap.assertedEvidences.end());
        }
    }

    return true;
}

}  // namespace core
