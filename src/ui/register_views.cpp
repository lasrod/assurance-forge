#include "ui/register_views.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "imgui.h"

namespace ui {
namespace {

struct CseMetadata {
    std::string claim_owner;
    std::string evidence_owner;
    std::string safety_case_owner;
    std::string claim_criteria;
    std::string evidence_criteria;
    std::string assessment_status = "Not Assessed";
    std::string notes;
};

struct EvidenceMetadata {
    std::string evidence_owner;
    std::string type;
    std::string recency;
    std::string maturity;
    std::string controlled_environment;
    std::string notes;
};

static std::vector<CseRegisterRow> g_cse_rows;
static std::vector<EvidenceRegisterRow> g_evidence_rows;

static std::unordered_map<std::string, CseMetadata> g_cse_metadata;
static std::unordered_map<std::string, EvidenceMetadata> g_evidence_metadata;

static bool IsClaimType(const std::string& t) {
    return t == "claim";
}

static bool IsEvidenceType(const std::string& t) {
    return t == "artifact" || t == "artifactreference" || t == "expression";
}

static std::string DisplayTextFor(const parser::SacmElement* e) {
    if (!e) return "";
    if (!e->name.empty()) return e->name;
    if (!e->content.empty()) return e->content;
    if (!e->description.empty()) return e->description;
    return e->id;
}

static std::string BuildCseId(const std::string& claim_id, const std::string& evidence_id) {
    return std::string("CSE:") + claim_id + "->" + evidence_id;
}

static bool EditCellText(const char* id, std::string& value, int max_len = 512) {
    std::vector<char> buf(static_cast<size_t>(max_len));
    size_t n = value.size();
    if (n >= buf.size()) n = buf.size() - 1;
    memcpy(buf.data(), value.c_str(), n);
    buf[n] = '\0';

    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputText(id, buf.data(), buf.size());
    if (changed) value = buf.data();
    return changed;
}

static void DrawAssessmentStatusCell(std::string& status) {
    static const char* kStatuses[] = {
        "Not Assessed",
        "Adequately Supported",
        "Insufficiently Supported",
    };

    int idx = 0;
    for (int i = 0; i < 3; ++i) {
        if (status == kStatuses[i]) {
            idx = i;
            break;
        }
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##assessment_status", kStatuses[idx])) {
        for (int i = 0; i < 3; ++i) {
            bool selected = (i == idx);
            if (ImGui::Selectable(kStatuses[i], selected)) {
                status = kStatuses[i];
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

}  // namespace

void RebuildRegisterViews(const parser::AssuranceCase* ac) {
    g_cse_rows.clear();
    g_evidence_rows.clear();

    if (!ac) {
        return;
    }

    std::unordered_map<std::string, const parser::SacmElement*> by_id;
    by_id.reserve(ac->elements.size());
    for (const auto& elem : ac->elements) {
        by_id[elem.id] = &elem;
    }

    std::set<std::pair<std::string, std::string>> claim_evidence_links;
    std::set<std::string> evidence_ids;

    for (const auto& elem : ac->elements) {
        if (IsEvidenceType(elem.type)) {
            evidence_ids.insert(elem.id);
        }

        if (elem.type != "assertedevidence") {
            continue;
        }

        std::vector<std::string> claim_ids;
        std::vector<std::string> local_evidence_ids;

        auto collect_ref = [&](const std::string& id) {
            auto it = by_id.find(id);
            if (it == by_id.end() || !it->second) return;
            const auto* ref = it->second;
            if (IsClaimType(ref->type)) {
                claim_ids.push_back(id);
            } else if (IsEvidenceType(ref->type)) {
                local_evidence_ids.push_back(id);
            }
        };

        for (const auto& id : elem.source_refs) collect_ref(id);
        for (const auto& id : elem.target_refs) collect_ref(id);

        std::sort(claim_ids.begin(), claim_ids.end());
        claim_ids.erase(std::unique(claim_ids.begin(), claim_ids.end()), claim_ids.end());
        std::sort(local_evidence_ids.begin(), local_evidence_ids.end());
        local_evidence_ids.erase(std::unique(local_evidence_ids.begin(), local_evidence_ids.end()), local_evidence_ids.end());

        for (const auto& claim_id : claim_ids) {
            for (const auto& evidence_id : local_evidence_ids) {
                claim_evidence_links.insert({claim_id, evidence_id});
                evidence_ids.insert(evidence_id);
            }
        }
    }

    std::unordered_map<std::string, int> used_count;
    for (const auto& link : claim_evidence_links) {
        used_count[link.second] += 1;
    }

    for (const auto& link : claim_evidence_links) {
        const std::string& claim_id = link.first;
        const std::string& evidence_id = link.second;

        const parser::SacmElement* claim_elem = nullptr;
        const parser::SacmElement* evidence_elem = nullptr;

        auto claim_it = by_id.find(claim_id);
        if (claim_it != by_id.end()) claim_elem = claim_it->second;

        auto evidence_it = by_id.find(evidence_id);
        if (evidence_it != by_id.end()) evidence_elem = evidence_it->second;

        CseRegisterRow row;
        row.cse_id = BuildCseId(claim_id, evidence_id);
        row.claim_id = claim_id;
        row.claim = DisplayTextFor(claim_elem);
        row.evidence_id = evidence_id;
        row.evidence = DisplayTextFor(evidence_elem);

        CseMetadata& meta = g_cse_metadata[row.cse_id];
        if (meta.assessment_status.empty()) {
            meta.assessment_status = "Not Assessed";
        }

        row.claim_owner = meta.claim_owner;
        row.evidence_owner = meta.evidence_owner;
        row.safety_case_owner = meta.safety_case_owner;
        row.claim_criteria = meta.claim_criteria;
        row.evidence_criteria = meta.evidence_criteria;
        row.assessment_status = meta.assessment_status;
        row.notes = meta.notes;

        g_cse_rows.push_back(std::move(row));
    }

    std::sort(g_cse_rows.begin(), g_cse_rows.end(), [](const CseRegisterRow& a, const CseRegisterRow& b) {
        if (a.claim_id == b.claim_id) return a.evidence_id < b.evidence_id;
        return a.claim_id < b.claim_id;
    });

    for (const auto& evidence_id : evidence_ids) {
        const parser::SacmElement* evidence_elem = nullptr;
        auto it = by_id.find(evidence_id);
        if (it != by_id.end()) evidence_elem = it->second;

        EvidenceRegisterRow row;
        row.evidence_id = evidence_id;
        row.evidence = DisplayTextFor(evidence_elem);
        row.used_by_cse_count = used_count[evidence_id];

        EvidenceMetadata& meta = g_evidence_metadata[row.evidence_id];
        row.evidence_owner = meta.evidence_owner;
        row.type = meta.type;
        row.recency = meta.recency;
        row.maturity = meta.maturity;
        row.controlled_environment = meta.controlled_environment;
        row.notes = meta.notes;

        g_evidence_rows.push_back(std::move(row));
    }

    std::sort(g_evidence_rows.begin(), g_evidence_rows.end(), [](const EvidenceRegisterRow& a, const EvidenceRegisterRow& b) {
        return a.evidence_id < b.evidence_id;
    });
}

void ShowCseRegisterView() {
    if (g_cse_rows.empty()) {
        ImGui::TextDisabled("No CSE rows were derived from direct claim-evidence relations.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders
                          | ImGuiTableFlags_RowBg
                          | ImGuiTableFlags_Resizable
                          | ImGuiTableFlags_ScrollX
                          | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("cse_register_table", 12, flags)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("CSE ID");
    ImGui::TableSetupColumn("Claim ID");
    ImGui::TableSetupColumn("Claim");
    ImGui::TableSetupColumn("Evidence ID");
    ImGui::TableSetupColumn("Evidence");
    ImGui::TableSetupColumn("Claim Owner");
    ImGui::TableSetupColumn("Evidence Owner");
    ImGui::TableSetupColumn("Safety Case Owner");
    ImGui::TableSetupColumn("Claim Criteria");
    ImGui::TableSetupColumn("Evidence Criteria");
    ImGui::TableSetupColumn("Assessment Status");
    ImGui::TableSetupColumn("Notes");
    ImGui::TableHeadersRow();

    for (auto& row : g_cse_rows) {
        ImGui::PushID(row.cse_id.c_str());
        CseMetadata& meta = g_cse_metadata[row.cse_id];

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(row.cse_id.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.claim_id.c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(row.claim.c_str());

        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(row.evidence_id.c_str());

        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(row.evidence.c_str());

        ImGui::TableSetColumnIndex(5);
        EditCellText("##claim_owner", row.claim_owner);

        ImGui::TableSetColumnIndex(6);
        EditCellText("##evidence_owner", row.evidence_owner);

        ImGui::TableSetColumnIndex(7);
        EditCellText("##safety_case_owner", row.safety_case_owner);

        ImGui::TableSetColumnIndex(8);
        EditCellText("##claim_criteria", row.claim_criteria, 1024);

        ImGui::TableSetColumnIndex(9);
        EditCellText("##evidence_criteria", row.evidence_criteria, 1024);

        ImGui::TableSetColumnIndex(10);
        DrawAssessmentStatusCell(row.assessment_status);

        ImGui::TableSetColumnIndex(11);
        EditCellText("##notes", row.notes, 1024);

        meta.claim_owner = row.claim_owner;
        meta.evidence_owner = row.evidence_owner;
        meta.safety_case_owner = row.safety_case_owner;
        meta.claim_criteria = row.claim_criteria;
        meta.evidence_criteria = row.evidence_criteria;
        meta.assessment_status = row.assessment_status;
        meta.notes = row.notes;

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void ShowEvidenceRegisterView() {
    if (g_evidence_rows.empty()) {
        ImGui::TextDisabled("No evidence/work-product rows were derived from the model.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders
                          | ImGuiTableFlags_RowBg
                          | ImGuiTableFlags_Resizable
                          | ImGuiTableFlags_ScrollX
                          | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("evidence_register_table", 9, flags)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Evidence ID");
    ImGui::TableSetupColumn("Evidence");
    ImGui::TableSetupColumn("Evidence Owner");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Recency");
    ImGui::TableSetupColumn("Maturity");
    ImGui::TableSetupColumn("Controlled Environment");
    ImGui::TableSetupColumn("Used By CSE Count");
    ImGui::TableSetupColumn("Notes");
    ImGui::TableHeadersRow();

    for (auto& row : g_evidence_rows) {
        ImGui::PushID(row.evidence_id.c_str());
        EvidenceMetadata& meta = g_evidence_metadata[row.evidence_id];

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(row.evidence_id.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.evidence.c_str());

        ImGui::TableSetColumnIndex(2);
        EditCellText("##evidence_owner", row.evidence_owner);

        ImGui::TableSetColumnIndex(3);
        EditCellText("##type", row.type);

        ImGui::TableSetColumnIndex(4);
        EditCellText("##recency", row.recency);

        ImGui::TableSetColumnIndex(5);
        EditCellText("##maturity", row.maturity);

        ImGui::TableSetColumnIndex(6);
        EditCellText("##controlled_environment", row.controlled_environment);

        ImGui::TableSetColumnIndex(7);
        ImGui::Text("%d", row.used_by_cse_count);

        ImGui::TableSetColumnIndex(8);
        EditCellText("##notes", row.notes, 1024);

        meta.evidence_owner = row.evidence_owner;
        meta.type = row.type;
        meta.recency = row.recency;
        meta.maturity = row.maturity;
        meta.controlled_environment = row.controlled_environment;
        meta.notes = row.notes;

        ImGui::PopID();
    }

    ImGui::EndTable();
}

}  // namespace ui
