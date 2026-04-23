#include "core/element_factory.h"

#include <algorithm>
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

// ===== Remove ===============================================================

namespace {

// True for parser element types that represent relationships (not nodes).
bool IsRelationshipType(const std::string& t) {
    return t == "assertedinference"
        || t == "assertedcontext"
        || t == "assertedevidence";
}

// Collect the ids of all direct children of `parent_id` by scanning
// relationship elements. A child is any element referenced as a source or as
// the reasoning of a relationship whose targets include the parent.
void CollectDirectChildren(const parser::AssuranceCase& ac,
                           const std::string& parent_id,
                           std::vector<std::string>& out_children) {
    for (const auto& e : ac.elements) {
        if (!IsRelationshipType(e.type)) continue;
        bool targets_parent = false;
        for (const auto& t : e.target_refs) {
            if (t == parent_id) { targets_parent = true; break; }
        }
        if (!targets_parent) continue;

        for (const auto& s : e.source_refs) {
            if (!s.empty()) out_children.push_back(s);
        }
        if (!e.reasoning_ref.empty()) {
            out_children.push_back(e.reasoning_ref);
        }
    }
}

// Collect the closed set of ids reachable as descendants of `root_id`.
// `root_id` itself is included in the result.
std::unordered_set<std::string> CollectSubtreeIds(const parser::AssuranceCase& ac,
                                                  const std::string& root_id) {
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack;
    stack.push_back(root_id);
    while (!stack.empty()) {
        std::string current = std::move(stack.back());
        stack.pop_back();
        if (!visited.insert(current).second) continue;  // already seen

        std::vector<std::string> children;
        CollectDirectChildren(ac, current, children);
        for (auto& c : children) stack.push_back(std::move(c));
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

bool RemoveElement(parser::AssuranceCase& ac,
                   sacm::AssuranceCasePackage* pkg,
                   const std::string& id,
                   bool cascade,
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

    // Determine the set of node ids to remove.
    std::unordered_set<std::string> removed_ids;
    if (cascade) {
        removed_ids = CollectSubtreeIds(ac, id);
    } else {
        std::vector<std::string> direct_children;
        CollectDirectChildren(ac, id, direct_children);
        if (!direct_children.empty()) {
            out_error = "Element has children; remove with cascade.";
            return false;
        }
        removed_ids.insert(id);
    }

    // ---- Parser model ------------------------------------------------------
    // 1) Drop the node elements themselves.
    // 2) For surviving relationships, scrub references to removed ids and
    //    only drop the relationship if it becomes structurally empty. This
    //    preserves a shared inference (e.g. one inference with multiple
    //    sub-goal sources and a reasoning strategy) when only one of its
    //    participants is being removed.
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

    // ---- SACM model --------------------------------------------------------
    if (pkg) {
        for (auto& ap : pkg->argumentPackages) {
            EraseByIdSet(ap.claims, removed_ids);
            EraseByIdSet(ap.argumentReasonings, removed_ids);
            EraseByIdSet(ap.artifactReferences, removed_ids);

            // Scrub source/target lists, then drop dangling relationships.
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
