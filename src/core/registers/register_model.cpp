#include "core/registers/register_model.h"

#include "core/problems/gsn_wellformedness.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core::registers {
namespace {

constexpr const char* kFormat = "assurance-forge-register-assessments";
constexpr const char* kFormatVersion = "1.0.0";

bool IsClaimType(const std::string& type) {
    return type == "claim";
}

bool IsEvidenceType(const std::string& type) {
    return type == "artifact" || type == "artifactreference" || type == "expression";
}

// Reads a string field, tolerating absence and null so a file written by an
// older or newer build still loads what it can rather than failing wholesale.
std::string ReadString(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return {};
    return found->get<std::string>();
}

void WriteIfSet(nlohmann::json& object, const char* key, const std::string& value) {
    if (!value.empty())
        object[key] = value;
}

} // namespace

bool operator==(const CseLink& a, const CseLink& b) {
    return a.claim_id == b.claim_id && a.evidence_id == b.evidence_id;
}

std::string MakeCseId(const std::string& claim_id, const std::string& evidence_id) {
    return std::string("CSE:") + claim_id + "->" + evidence_id;
}

std::string DisplayTextFor(const parser::SacmElement* element) {
    if (!element)
        return {};
    if (!element->name.empty())
        return element->name;
    if (!element->content.empty())
        return element->content;
    if (!element->description.empty())
        return element->description;
    return element->id;
}

std::vector<CseLink> DeriveCseLinks(const parser::AssuranceCase& model) {
    std::unordered_map<std::string, const parser::SacmElement*> by_id;
    by_id.reserve(model.elements.size());
    for (const parser::SacmElement& element : model.elements)
        by_id[element.id] = &element;

    // A map keyed on the pair both deduplicates and sorts, so two documents
    // that differ only in element order produce the same register. The value is
    // the relationship the pairing came from, and whether that relationship
    // carries others besides it.
    std::map<std::pair<std::string, std::string>, std::pair<std::string, bool>> links;

    for (const parser::SacmElement& relationship : model.elements) {
        if (relationship.type != "assertedevidence")
            continue;

        std::vector<std::string> claim_ids;
        std::vector<std::string> evidence_ids;
        // Endpoints are read from both ends: which side carries the claim and
        // which the evidence varies with the dialect the file came from.
        for (const std::vector<std::string>* refs : {&relationship.source_refs, &relationship.target_refs}) {
            for (const std::string& id : *refs) {
                const auto found = by_id.find(id);
                if (found == by_id.end() || !found->second)
                    continue;
                if (IsClaimType(found->second->type))
                    claim_ids.push_back(id);
                else if (IsEvidenceType(found->second->type))
                    evidence_ids.push_back(id);
            }
        }

        const bool carries_more = claim_ids.size() * evidence_ids.size() > 1;
        for (const std::string& claim_id : claim_ids) {
            for (const std::string& evidence_id : evidence_ids) {
                // First relationship wins where two carry the same pairing: the
                // register shows one row per pairing, and picking the earlier
                // one keeps that row stable as the document grows.
                links.emplace(std::pair<std::string, std::string>{claim_id, evidence_id},
                              std::pair<std::string, bool>{relationship.id, carries_more});
            }
        }
    }

    std::vector<CseLink> result;
    result.reserve(links.size());
    for (const auto& link : links) {
        result.push_back(CseLink{link.first.first, link.first.second, link.second.first, link.second.second});
    }
    return result;
}

std::vector<std::string> DeriveEvidenceIds(const parser::AssuranceCase& model) {
    // An ArtifactReference is what SACM offers for both a GSN Solution and a
    // GSN Context; only how it is attached tells them apart. One reached by an
    // AssertedContext is context, and listing it as evidence reported every
    // context in the case as "evidence nothing cites" -- a finding about
    // something that was never evidence. Both ends are read, as DeriveCseLinks
    // does, because which end carries the reference varies with the dialect.
    std::set<std::string> context_ids;
    for (const parser::SacmElement& relationship : model.elements) {
        if (relationship.type != "assertedcontext")
            continue;
        for (const std::vector<std::string>* refs : {&relationship.source_refs, &relationship.target_refs}) {
            for (const std::string& id : *refs)
                context_ids.insert(id);
        }
    }

    // An Artifact an ArtifactReference cites is that evidence's record -- what
    // it is, its version and date -- not a second piece of evidence. Only an
    // Artifact nothing cites stands on its own in the register.
    std::set<std::string> cited_ids;
    for (const parser::SacmElement& element : model.elements) {
        if (element.type != "artifactreference")
            continue;
        if (!element.referenced_artifact_id.empty())
            cited_ids.insert(element.referenced_artifact_id);
        if (!element.evidence.artifact_id.empty())
            cited_ids.insert(element.evidence.artifact_id);
    }

    std::set<std::string> ids;
    for (const parser::SacmElement& element : model.elements) {
        if (IsEvidenceType(element.type) && !element.id.empty() && context_ids.count(element.id) == 0 &&
            cited_ids.count(element.id) == 0) {
            ids.insert(element.id);
        }
    }
    return std::vector<std::string>(ids.begin(), ids.end());
}

int CountCseUses(const std::vector<CseLink>& links, const std::string& evidence_id) {
    return static_cast<int>(std::count_if(
        links.begin(), links.end(), [&](const CseLink& link) { return link.evidence_id == evidence_id; }));
}

std::vector<EvidenceCitation> DeriveEvidenceCitations(const parser::AssuranceCase& model,
                                                      const std::string& evidence_id) {
    std::unordered_map<std::string, const parser::SacmElement*> by_id;
    by_id.reserve(model.elements.size());
    for (const parser::SacmElement& element : model.elements)
        by_id[element.id] = &element;

    std::vector<EvidenceCitation> citations;
    for (const parser::SacmElement& relationship : model.elements) {
        if (relationship.type != "assertedevidence")
            continue;

        // Endpoints are read from both ends and told apart by the kind of
        // element they name, exactly as DeriveCseLinks does: which side
        // carries the claim and which the evidence varies with the dialect the
        // file came from, and reading only `source` silently lost every link
        // in a document written the other way round.
        std::vector<std::string> claim_ids;
        std::vector<std::string> evidence_ids;
        for (const std::vector<std::string>* refs : {&relationship.source_refs, &relationship.target_refs}) {
            for (const std::string& id : *refs) {
                const auto found = by_id.find(id);
                if (found == by_id.end() || !found->second)
                    continue;
                if (IsClaimType(found->second->type))
                    claim_ids.push_back(id);
                else if (IsEvidenceType(found->second->type))
                    evidence_ids.push_back(id);
            }
        }
        if (std::find(evidence_ids.begin(), evidence_ids.end(), evidence_id) == evidence_ids.end())
            continue;

        // One relationship can carry several claim/evidence pairings; deleting
        // it withdraws all of them, which is what `shared` warns about.
        const bool carries_more = claim_ids.size() * evidence_ids.size() > 1;
        for (const std::string& claim_id : claim_ids) {
            citations.push_back(EvidenceCitation{
                .claim_id = claim_id,
                .relationship_id = relationship.id,
                .shared = carries_more,
            });
        }
    }
    std::sort(citations.begin(), citations.end(), [](const EvidenceCitation& a, const EvidenceCitation& b) {
        return a.claim_id != b.claim_id ? a.claim_id < b.claim_id : a.relationship_id < b.relationship_id;
    });
    return citations;
}

std::vector<std::string> DeriveEvidenceSupportTargets(const parser::AssuranceCase& model) {
    std::set<std::string> ids;
    for (const parser::SacmElement& element : model.elements) {
        if (element.id.empty())
            continue;
        if (GsnCanBeSupported(GsnKindOf(element)))
            ids.insert(element.id);
    }
    return std::vector<std::string>(ids.begin(), ids.end());
}

std::string SerializeRegisterStore(const RegisterStore& store) {
    nlohmann::json root;
    root["format"] = kFormat;
    root["formatVersion"] = kFormatVersion;

    nlohmann::json cse_entries = nlohmann::json::array();
    for (const auto& [cse_id, metadata] : store.cse) {
        nlohmann::json entry;
        entry["cseId"] = cse_id;
        WriteIfSet(entry, "claimOwner", metadata.claim_owner);
        WriteIfSet(entry, "evidenceOwner", metadata.evidence_owner);
        WriteIfSet(entry, "safetyCaseOwner", metadata.safety_case_owner);
        WriteIfSet(entry, "claimCriteria", metadata.claim_criteria);
        WriteIfSet(entry, "evidenceCriteria", metadata.evidence_criteria);
        WriteIfSet(entry, "assessmentStatus", metadata.assessment_status);
        WriteIfSet(entry, "notes", metadata.notes);
        cse_entries.push_back(std::move(entry));
    }
    root["cseAssessments"] = std::move(cse_entries);

    nlohmann::json evidence_entries = nlohmann::json::array();
    for (const auto& [evidence_id, metadata] : store.evidence) {
        nlohmann::json entry;
        entry["evidenceId"] = evidence_id;
        WriteIfSet(entry, "evidenceOwner", metadata.evidence_owner);
        WriteIfSet(entry, "type", metadata.type);
        WriteIfSet(entry, "recency", metadata.recency);
        WriteIfSet(entry, "maturity", metadata.maturity);
        WriteIfSet(entry, "controlledEnvironment", metadata.controlled_environment);
        WriteIfSet(entry, "notes", metadata.notes);
        evidence_entries.push_back(std::move(entry));
    }
    root["evidenceAssessments"] = std::move(evidence_entries);

    return root.dump(2) + "\n";
}

bool DeserializeRegisterStore(const std::string& content, RegisterStore& store, std::string& error) {
    error.clear();
    store = RegisterStore{};

    nlohmann::json root = nlohmann::json::parse(content, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        error = "Register assessments file is not valid JSON.";
        return false;
    }
    if (!root.is_object()) {
        error = "Register assessments file must contain a JSON object.";
        return false;
    }
    // The format tag is checked but a mismatch is refused rather than guessed
    // at: loading someone else's schema into these fields would silently
    // fabricate assessments.
    const std::string format = ReadString(root, "format");
    if (!format.empty() && format != kFormat) {
        error = "Unexpected register assessments format '" + format + "'.";
        return false;
    }

    const auto cse_entries = root.find("cseAssessments");
    if (cse_entries != root.end() && cse_entries->is_array()) {
        for (const nlohmann::json& entry : *cse_entries) {
            if (!entry.is_object())
                continue;
            const std::string cse_id = ReadString(entry, "cseId");
            if (cse_id.empty())
                continue;
            CseMetadata metadata;
            metadata.claim_owner = ReadString(entry, "claimOwner");
            metadata.evidence_owner = ReadString(entry, "evidenceOwner");
            metadata.safety_case_owner = ReadString(entry, "safetyCaseOwner");
            metadata.claim_criteria = ReadString(entry, "claimCriteria");
            metadata.evidence_criteria = ReadString(entry, "evidenceCriteria");
            const std::string status = ReadString(entry, "assessmentStatus");
            if (!status.empty())
                metadata.assessment_status = status;
            metadata.notes = ReadString(entry, "notes");
            store.cse[cse_id] = std::move(metadata);
        }
    }

    const auto evidence_entries = root.find("evidenceAssessments");
    if (evidence_entries != root.end() && evidence_entries->is_array()) {
        for (const nlohmann::json& entry : *evidence_entries) {
            if (!entry.is_object())
                continue;
            const std::string evidence_id = ReadString(entry, "evidenceId");
            if (evidence_id.empty())
                continue;
            EvidenceMetadata metadata;
            metadata.evidence_owner = ReadString(entry, "evidenceOwner");
            metadata.type = ReadString(entry, "type");
            metadata.recency = ReadString(entry, "recency");
            metadata.maturity = ReadString(entry, "maturity");
            metadata.controlled_environment = ReadString(entry, "controlledEnvironment");
            metadata.notes = ReadString(entry, "notes");
            store.evidence[evidence_id] = std::move(metadata);
        }
    }

    return true;
}

OrphanedMetadata FindOrphanedMetadata(const RegisterStore& store,
                                      const std::vector<CseLink>& links,
                                      const std::vector<std::string>& evidence_ids) {
    std::set<std::string> live_cse_ids;
    for (const CseLink& link : links)
        live_cse_ids.insert(MakeCseId(link.claim_id, link.evidence_id));
    const std::set<std::string> live_evidence_ids(evidence_ids.begin(), evidence_ids.end());

    OrphanedMetadata orphans;
    for (const auto& [cse_id, unused] : store.cse) {
        if (live_cse_ids.find(cse_id) == live_cse_ids.end())
            orphans.cse_ids.push_back(cse_id);
    }
    for (const auto& [evidence_id, unused] : store.evidence) {
        if (live_evidence_ids.find(evidence_id) == live_evidence_ids.end())
            orphans.evidence_ids.push_back(evidence_id);
    }
    return orphans;
}

} // namespace core::registers
