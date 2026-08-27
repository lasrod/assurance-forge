#include "ui/register_views.h"

#include "core/registers/register_model.h"
#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"
#include "ui/i18n/localization.h"
#include "ui/widgets/danger_button.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

namespace ui {
namespace {

using core::registers::CseMetadata;
using core::registers::EvidenceMetadata;

static std::vector<CseRegisterRow> g_cse_rows;
static std::vector<EvidenceRegisterRow> g_evidence_rows;

// Reads an assessment without creating one. `store.cse[id]` would insert a
// default entry for every derived row, which would then be written to the
// project — turning "nobody has assessed this yet" into a stored row claiming
// "Not Assessed", and later into a phantom orphan once the argument changes.
template <typename MetadataT>
static const MetadataT& StoredOrDefault(const std::map<std::string, MetadataT>& assessments, const std::string& id) {
    static const MetadataT kUnassessed{};
    const auto found = assessments.find(id);
    return found == assessments.end() ? kUnassessed : found->second;
}

static bool EditCellText(const char* id, std::string& value, int max_len = 512) {
    std::vector<char> buf(static_cast<size_t>(max_len));
    size_t n = value.size();
    if (n >= buf.size())
        n = buf.size() - 1;
    memcpy(buf.data(), value.c_str(), n);
    buf[n] = '\0';

    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputText(id, buf.data(), buf.size());
    if (changed)
        value = buf.data();
    return changed;
}

static bool DrawAssessmentStatusCell(std::string& status) {
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

    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##assessment_status", kStatuses[idx])) {
        for (int i = 0; i < 3; ++i) {
            bool selected = (i == idx);
            if (ImGui::Selectable(kStatuses[i], selected) && !selected) {
                status = kStatuses[i];
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// The evidence whose removal is awaiting an answer. The label and use count
// are copied out of the row: the popup outlives the frame, and the rows are
// rebuilt whenever the argument changes.
static std::string g_pending_remove_evidence_id;
static std::string g_pending_remove_evidence_label;
static int g_pending_remove_use_count = 0;
static bool g_open_remove_evidence_modal = false;

// The one location cell being edited. The assessment cells write per keystroke
// into a JSON store, which costs nothing; a location is an audited SACM edit,
// so it is committed once, when the field is left.
static std::string g_location_edit_id;
static std::string g_location_edit_text;

static bool IconButton(const char* id, const char* icon, const std::string& tooltip) {
    const bool clicked = ImGui::SmallButton((std::string(icon) + "##" + id).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.c_str());
    return clicked;
}

static void DrawEvidenceActionsCell(const EvidenceRegisterRow& row, const EvidenceRegisterCallbacks& callbacks) {
    ImGui::BeginDisabled(!callbacks.locate);
    if (IconButton("locate", ICON_FA_CROSSHAIRS, AF_TR("Show in argument")) && callbacks.locate)
        callbacks.locate(row.evidence_id);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!callbacks.remove);
    if (IconButton("remove", ICON_FA_TRASH, AF_TR("Remove evidence"))) {
        g_pending_remove_evidence_id = row.evidence_id;
        g_pending_remove_evidence_label =
            row.evidence.empty() ? row.evidence_id : row.evidence_id + " — " + row.evidence;
        g_pending_remove_use_count = row.used_by_cse_count;
        g_open_remove_evidence_modal = true;
    }
    ImGui::EndDisabled();
}

static void DrawLocationCell(const EvidenceRegisterRow& row, const EvidenceRegisterCallbacks& callbacks) {
    const bool editing = g_location_edit_id == row.evidence_id;
    const std::string& shown = editing ? g_location_edit_text : row.location;
    std::array<char, 1024> buffer{};
    const std::size_t length = std::min(shown.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), shown.data(), length);
    buffer[length] = '\0';

    const float open_button_width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(-open_button_width);
    ImGui::BeginDisabled(!row.is_artifact_reference || !callbacks.set_location);
    ImGui::InputTextWithHint("##location", AF_TR("Path or URL").c_str(), buffer.data(), buffer.size());
    if (ImGui::IsItemActivated()) {
        g_location_edit_id = row.evidence_id;
        g_location_edit_text = row.location;
    }
    if (ImGui::IsItemActive() && g_location_edit_id == row.evidence_id)
        g_location_edit_text = buffer.data();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (callbacks.set_location)
            callbacks.set_location(row.evidence_id, buffer.data());
        g_location_edit_id.clear();
    } else if (ImGui::IsItemDeactivated()) {
        g_location_edit_id.clear();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(row.location.empty() || !callbacks.open_location);
    if (IconButton("open", ICON_FA_FOLDER_OPEN, AF_TR("Open the file or URL")) && callbacks.open_location)
        callbacks.open_location(row.location);
    ImGui::EndDisabled();
}

// Confirmation for a removal started from the table. The canvas removes a
// consequence-free leaf without asking, but a row's trash icon is easier to hit
// by mistake than a selected node, and the claims that rest on the evidence are
// not visible from here -- so the popup names them.
static void RenderRemoveEvidenceModal(const EvidenceRegisterCallbacks& callbacks) {
    const std::string title = AF_TR("Remove Evidence") + "###remove_evidence_modal";
    if (g_open_remove_evidence_modal) {
        ImGui::OpenPopup(title.c_str());
        g_open_remove_evidence_modal = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted(ui::i18n::trf("Remove evidence {0}?", g_pending_remove_evidence_label).c_str());
    if (g_pending_remove_use_count == 0) {
        ImGui::TextUnformatted(AF_TR("No claim rests on this evidence.").c_str());
    } else {
        ImGui::TextUnformatted(ui::i18n::trnf("{0} claim rests on this evidence; the link will be removed too.",
                                              "{0} claims rest on this evidence; the links will be removed too.",
                                              g_pending_remove_use_count,
                                              g_pending_remove_use_count)
                                   .c_str());
    }
    ImGui::Spacing();
    ImGui::Spacing();

    const float button_width = ImGui::GetFontSize() * 7.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_width * 2.0f - spacing) * 0.5f);
    if (ui::widgets::DangerButton(AF_TR("Remove").c_str(), ImVec2(button_width, 0.0f))) {
        if (callbacks.remove)
            callbacks.remove(g_pending_remove_evidence_id);
        g_pending_remove_evidence_id.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button(AF_TR("Cancel").c_str(), ImVec2(button_width, 0.0f))) {
        g_pending_remove_evidence_id.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void StoreCseRow(core::registers::RegisterStore& store, const CseRegisterRow& row) {
    CseMetadata& metadata = store.cse[row.cse_id];
    metadata.claim_owner = row.claim_owner;
    metadata.evidence_owner = row.evidence_owner;
    metadata.safety_case_owner = row.safety_case_owner;
    metadata.claim_criteria = row.claim_criteria;
    metadata.evidence_criteria = row.evidence_criteria;
    metadata.assessment_status = row.assessment_status;
    metadata.notes = row.notes;
}

static void StoreEvidenceRow(core::registers::RegisterStore& store, const EvidenceRegisterRow& row) {
    EvidenceMetadata& metadata = store.evidence[row.evidence_id];
    metadata.evidence_owner = row.evidence_owner;
    metadata.type = row.type;
    metadata.recency = row.recency;
    metadata.maturity = row.maturity;
    metadata.controlled_environment = row.controlled_environment;
    metadata.notes = row.notes;
}

} // namespace

void RebuildRegisterViews(const parser::AssuranceCase* ac, const core::registers::RegisterStore& store) {
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

    auto find_element = [&by_id](const std::string& id) -> const parser::SacmElement* {
        auto found = by_id.find(id);
        return found == by_id.end() ? nullptr : found->second;
    };

    // Structure comes from core::registers, which is where it is tested; this
    // function only joins it to what the user typed and hands the result to
    // the table renderers below. Both collections arrive sorted.
    const std::vector<core::registers::CseLink> links = core::registers::DeriveCseLinks(*ac);
    const std::vector<std::string> evidence_ids = core::registers::DeriveEvidenceIds(*ac);

    for (const core::registers::CseLink& link : links) {
        CseRegisterRow row;
        row.cse_id = core::registers::MakeCseId(link.claim_id, link.evidence_id);
        row.claim_id = link.claim_id;
        row.claim = core::registers::DisplayTextFor(find_element(link.claim_id));
        row.evidence_id = link.evidence_id;
        row.evidence = core::registers::DisplayTextFor(find_element(link.evidence_id));

        const CseMetadata& meta = StoredOrDefault(store.cse, row.cse_id);
        row.claim_owner = meta.claim_owner;
        row.evidence_owner = meta.evidence_owner;
        row.safety_case_owner = meta.safety_case_owner;
        row.claim_criteria = meta.claim_criteria;
        row.evidence_criteria = meta.evidence_criteria;
        row.assessment_status = meta.assessment_status;
        row.notes = meta.notes;

        g_cse_rows.push_back(std::move(row));
    }

    for (const std::string& evidence_id : evidence_ids) {
        EvidenceRegisterRow row;
        row.evidence_id = evidence_id;
        const parser::SacmElement* element = find_element(evidence_id);
        row.evidence = core::registers::DisplayTextFor(element);
        row.used_by_cse_count = core::registers::CountCseUses(links, evidence_id);
        row.location = element != nullptr ? element->artifact_location : std::string{};
        row.is_artifact_reference = element != nullptr && element->type == "artifactreference";

        const EvidenceMetadata& meta = StoredOrDefault(store.evidence, row.evidence_id);
        row.evidence_owner = meta.evidence_owner;
        row.type = meta.type;
        row.recency = meta.recency;
        row.maturity = meta.maturity;
        row.controlled_environment = meta.controlled_environment;
        row.notes = meta.notes;

        g_evidence_rows.push_back(std::move(row));
    }
}

size_t GetCseRegisterRowCount() {
    return g_cse_rows.size();
}

size_t GetEvidenceRegisterRowCount() {
    return g_evidence_rows.size();
}

bool ShowCseRegisterView(core::registers::RegisterStore& store) {
    if (g_cse_rows.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No CSE rows were derived from direct claim-evidence relations.").c_str());
        return false;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;

    if (!ImGui::BeginTable("cse_register_table", 12, flags)) {
        return false;
    }

    ImGui::TableSetupScrollFreeze(2, 1);
    ImGui::TableSetupColumn(AF_TR("CSE ID").c_str());
    ImGui::TableSetupColumn(AF_TR("Claim ID").c_str());
    ImGui::TableSetupColumn(AF_TR("Claim").c_str());
    ImGui::TableSetupColumn(AF_TR("Evidence ID").c_str());
    ImGui::TableSetupColumn(AF_TR("Evidence").c_str());
    ImGui::TableSetupColumn(AF_TR("Claim Owner").c_str());
    ImGui::TableSetupColumn(AF_TR("Evidence Owner").c_str());
    ImGui::TableSetupColumn(AF_TR("Safety Case Owner").c_str());
    ImGui::TableSetupColumn(AF_TR("Claim Criteria").c_str());
    ImGui::TableSetupColumn(AF_TR("Evidence Criteria").c_str());
    ImGui::TableSetupColumn(AF_TR("Assessment Status").c_str());
    ImGui::TableSetupColumn(AF_TR("Notes").c_str());
    ImGui::TableHeadersRow();

    bool edited = false;
    for (auto& row : g_cse_rows) {
        ImGui::PushID(row.cse_id.c_str());
        bool row_edited = false;

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
        row_edited |= EditCellText("##claim_owner", row.claim_owner);

        ImGui::TableSetColumnIndex(6);
        row_edited |= EditCellText("##evidence_owner", row.evidence_owner);

        ImGui::TableSetColumnIndex(7);
        row_edited |= EditCellText("##safety_case_owner", row.safety_case_owner);

        ImGui::TableSetColumnIndex(8);
        row_edited |= EditCellText("##claim_criteria", row.claim_criteria, 1024);

        ImGui::TableSetColumnIndex(9);
        row_edited |= EditCellText("##evidence_criteria", row.evidence_criteria, 1024);

        ImGui::TableSetColumnIndex(10);
        row_edited |= DrawAssessmentStatusCell(row.assessment_status);

        ImGui::TableSetColumnIndex(11);
        row_edited |= EditCellText("##notes", row.notes, 1024);

        if (row_edited) {
            StoreCseRow(store, row);
            edited = true;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
    return edited;
}

bool ShowEvidenceRegisterView(core::registers::RegisterStore& store, const EvidenceRegisterCallbacks& callbacks) {
    if (g_evidence_rows.empty()) {
        ImGui::TextDisabled("%s", AF_TR("No evidence/work-product rows were derived from the model.").c_str());
        return false;
    }

    // Fixed initial widths, so the table is as wide as its content rather than
    // squeezed to the header labels. Under ScrollX the default sizing fits each
    // column to its header, which left every text field a few characters wide
    // and nothing to scroll; sized in font units so DPI scaling carries through.
    const float unit = ImGui::GetFontSize();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("evidence_register_table", 11, flags)) {
        return false;
    }

    ImGui::TableSetupScrollFreeze(3, 1);
    ImGui::TableSetupColumn(AF_TR("Actions").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 4.5f);
    ImGui::TableSetupColumn(AF_TR("Evidence ID").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 6.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 18.0f);
    ImGui::TableSetupColumn(AF_TR("Location").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 20.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence Owner").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 10.0f);
    ImGui::TableSetupColumn(AF_TR("Type").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 8.0f);
    ImGui::TableSetupColumn(AF_TR("Recency").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 8.0f);
    ImGui::TableSetupColumn(AF_TR("Maturity").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 8.0f);
    ImGui::TableSetupColumn(AF_TR("Controlled Environment").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 12.0f);
    ImGui::TableSetupColumn(AF_TR("Used By CSE Count").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 7.0f);
    ImGui::TableSetupColumn(AF_TR("Notes").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 18.0f);
    ImGui::TableHeadersRow();

    bool edited = false;
    for (auto& row : g_evidence_rows) {
        ImGui::PushID(row.evidence_id.c_str());
        bool row_edited = false;

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        DrawEvidenceActionsCell(row, callbacks);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.evidence_id.c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(row.evidence.c_str());
        if (ImGui::IsItemHovered() && !row.evidence.empty())
            ImGui::SetTooltip("%s", row.evidence.c_str());

        ImGui::TableSetColumnIndex(3);
        DrawLocationCell(row, callbacks);

        ImGui::TableSetColumnIndex(4);
        row_edited |= EditCellText("##evidence_owner", row.evidence_owner);

        ImGui::TableSetColumnIndex(5);
        row_edited |= EditCellText("##type", row.type);

        ImGui::TableSetColumnIndex(6);
        row_edited |= EditCellText("##recency", row.recency);

        ImGui::TableSetColumnIndex(7);
        row_edited |= EditCellText("##maturity", row.maturity);

        ImGui::TableSetColumnIndex(8);
        row_edited |= EditCellText("##controlled_environment", row.controlled_environment);

        ImGui::TableSetColumnIndex(9);
        ImGui::Text("%d", row.used_by_cse_count);

        ImGui::TableSetColumnIndex(10);
        row_edited |= EditCellText("##notes", row.notes, 1024);

        if (row_edited) {
            StoreEvidenceRow(store, row);
            edited = true;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
    RenderRemoveEvidenceModal(callbacks);
    return edited;
}

} // namespace ui
