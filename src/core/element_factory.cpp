#include "core/element_factory.h"

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
        std::string candidate = prefix + "_" + std::to_string(i);
        if (existing.find(candidate) == existing.end()) return candidate;
    }
    return prefix + "_x";
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

}  // namespace core
