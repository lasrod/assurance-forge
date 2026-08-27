#include "ui/register_views.h"

#include "core/registers/register_model.h"
#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"
#include "ui/i18n/localization.h"

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

// The one SACM-backed cell being edited, keyed by row and column. The
// project-file assessment cells write per keystroke into a JSON store, which
// costs nothing; a SACM column is an audited edit, so it is committed once,
// when the field is left.
static std::string g_sacm_edit_key;
static std::string g_sacm_edit_text;

static bool IsEditingCell(const std::string& key) {
    return g_sacm_edit_key == key;
}

// Draws a text field for `stored` and returns true, with the new text in
// `committed`, on the frame the user leaves the field having changed it.
static bool CommitOnLeaveCell(const char* id,
                              const std::string& key,
                              const std::string& stored,
                              const std::string& hint,
                              std::string& committed) {
    const bool editing = IsEditingCell(key);
    const std::string& shown = editing ? g_sacm_edit_text : stored;
    std::array<char, 1024> buffer{};
    const std::size_t length = std::min(shown.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), shown.data(), length);
    buffer[length] = '\0';

    ImGui::InputTextWithHint(id, hint.c_str(), buffer.data(), buffer.size());
    if (ImGui::IsItemActivated()) {
        g_sacm_edit_key = key;
        g_sacm_edit_text = stored;
    }
    if (ImGui::IsItemActive() && IsEditingCell(key))
        g_sacm_edit_text = buffer.data();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        committed = buffer.data();
        g_sacm_edit_key.clear();
        return true;
    }
    if (ImGui::IsItemDeactivated())
        g_sacm_edit_key.clear();
    return false;
}

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
    // The application confirms, with the library's preview of what goes with
    // the row (its links, a term citing it), so nothing is asked twice.
    ImGui::BeginDisabled(!callbacks.remove);
    if (IconButton("remove", ICON_FA_TRASH, AF_TR("Remove evidence")) && callbacks.remove)
        callbacks.remove(row.evidence_id);
    ImGui::EndDisabled();
}

static void DrawLocationCell(const EvidenceRegisterRow& row, const EvidenceRegisterCallbacks& callbacks) {
    const std::string key = row.evidence_id + "/location";
    // Two buttons follow the field: browse for a file, open what is recorded.
    const float buttons_width = 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(-buttons_width);
    ImGui::BeginDisabled(!row.is_artifact_reference || !callbacks.set_location);
    std::string committed;
    if (CommitOnLeaveCell("##location", key, row.location, AF_TR("Path or URL"), committed) && callbacks.set_location)
        callbacks.set_location(row.evidence_id, committed);
    ImGui::EndDisabled();

    ImGui::SameLine();
    // Browse is always offered: a location that is not set yet is exactly when
    // a picker is wanted, and a folder icon that only ever opened read as one.
    ImGui::BeginDisabled(!row.is_artifact_reference || !callbacks.browse_location);
    if (IconButton("browse", ICON_FA_FOLDER_OPEN, AF_TR("Browse for a file")) && callbacks.browse_location)
        callbacks.browse_location(row.evidence_id);
    ImGui::EndDisabled();
    ImGui::SameLine();
    // Opens the RECORDED location. While the cell is being edited the text
    // shown is not yet the record, so the button waits for the commit rather
    // than opening whatever the row held before the user started typing.
    ImGui::BeginDisabled(IsEditingCell(key) || row.location.empty() || !callbacks.open_location);
    if (IconButton("open", ICON_FA_LINK, AF_TR("Open the file or URL")) && callbacks.open_location)
        callbacks.open_location(row.location);
    ImGui::EndDisabled();
}

// One register column. A row still assessed in the project file edits its
// JSON field in place, as every cell did before the columns moved into SACM;
// a row assessed in the document commits to the cited Artifact when the
// field is left. Returns true when the JSON field changed this frame.
static bool DrawAttributeCell(const char* id,
                              EvidenceRegisterRow& row,
                              core::EvidenceAttribute attribute,
                              std::string& json_field,
                              const EvidenceRegisterCallbacks& callbacks) {
    if (row.stored_in_project_file) {
        const bool changed = EditCellText(id, json_field);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", AF_TR("Stored in the project file until moved into SACM.").c_str());
        return changed;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(!row.is_artifact_reference || !callbacks.set_attribute);
    std::string committed;
    const std::string key = row.evidence_id + "/" + core::EvidenceAttributeToken(attribute);
    if (CommitOnLeaveCell(id, key, core::EvidenceRecordField(row.record, attribute), std::string{}, committed) &&
        static_cast<bool>(callbacks.set_attribute)) {
        callbacks.set_attribute(row.evidence_id, attribute, committed);
    }
    ImGui::EndDisabled();
    return false;
}

// Assessments the project file still holds for evidence in the argument, and
// the one action that moves them into the document. Never automatic: the
// SACM file is the safety argument, and rewriting it on open would be a
// change nobody asked for.
static void RenderProjectFileAssessmentsBanner(const EvidenceRegisterCallbacks& callbacks) {
    int stored = 0;
    for (const EvidenceRegisterRow& row : g_evidence_rows) {
        if (row.stored_in_project_file && row.is_artifact_reference)
            ++stored;
    }
    if (stored == 0)
        return;
    ImGui::TextWrapped("%s",
                       ui::i18n::trnf("{0} assessment is stored in the project file rather than the SACM document.",
                                      "{0} assessments are stored in the project file rather than the SACM document.",
                                      stored,
                                      stored)
                           .c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!callbacks.migrate_assessments);
    if (ImGui::SmallButton(AF_TR("Move into SACM").c_str()) && callbacks.migrate_assessments)
        callbacks.migrate_assessments();
    ImGui::EndDisabled();
    ImGui::Spacing();
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
        if (element != nullptr)
            row.record = element->evidence;
        row.stored_in_project_file = store.evidence.count(evidence_id) > 0;

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

    // Sized like the evidence table: fixed initial widths in font units, so the
    // table is as wide as its content and scrolls rather than squeezing every
    // field to its header label.
    const float unit = ImGui::GetFontSize();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("cse_register_table", 12, flags)) {
        return false;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(AF_TR("CSE ID").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 10.0f);
    ImGui::TableSetupColumn(AF_TR("Claim ID").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 6.0f);
    ImGui::TableSetupColumn(AF_TR("Claim").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 20.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence ID").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 6.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 16.0f);
    ImGui::TableSetupColumn(AF_TR("Claim Owner").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 10.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence Owner").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 10.0f);
    ImGui::TableSetupColumn(AF_TR("Safety Case Owner").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 10.0f);
    ImGui::TableSetupColumn(AF_TR("Claim Criteria").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 14.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence Criteria").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 14.0f);
    ImGui::TableSetupColumn(AF_TR("Assessment Status").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 13.0f);
    ImGui::TableSetupColumn(AF_TR("Notes").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 18.0f);
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

    RenderProjectFileAssessmentsBanner(callbacks);

    // Fixed initial widths, so the table is as wide as its content rather than
    // squeezed to the header labels. Under ScrollX the default sizing fits each
    // column to its header, which left every text field a few characters wide
    // and nothing to scroll; sized in font units so DPI scaling carries through.
    const float unit = ImGui::GetFontSize();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("evidence_register_table", 12, flags)) {
        return false;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(AF_TR("Actions").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 4.5f);
    ImGui::TableSetupColumn(AF_TR("Evidence ID").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 6.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 18.0f);
    ImGui::TableSetupColumn(AF_TR("Location").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 20.0f);
    ImGui::TableSetupColumn(AF_TR("Evidence Owner").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 10.0f);
    ImGui::TableSetupColumn(AF_TR("Type").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 8.0f);
    ImGui::TableSetupColumn(AF_TR("Version").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 6.0f);
    ImGui::TableSetupColumn(AF_TR("Date").c_str(), ImGuiTableColumnFlags_WidthFixed, unit * 8.0f);
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
        row_edited |=
            DrawAttributeCell("##evidence_owner", row, core::EvidenceAttribute::Owner, row.evidence_owner, callbacks);

        ImGui::TableSetColumnIndex(5);
        row_edited |= DrawAttributeCell("##type", row, core::EvidenceAttribute::Type, row.type, callbacks);

        ImGui::TableSetColumnIndex(6);
        // The project file never held a version; the column waits for the move.
        if (row.stored_in_project_file) {
            ImGui::TextDisabled("%s", "—");
        } else {
            std::string unused;
            row_edited |= DrawAttributeCell("##version", row, core::EvidenceAttribute::Version, unused, callbacks);
        }

        ImGui::TableSetColumnIndex(7);
        // Recency, the project file's free text about when the evidence dates
        // from, is shown in the Date column it migrates into.
        row_edited |= DrawAttributeCell("##date", row, core::EvidenceAttribute::Date, row.recency, callbacks);

        ImGui::TableSetColumnIndex(8);
        row_edited |= DrawAttributeCell("##maturity", row, core::EvidenceAttribute::Maturity, row.maturity, callbacks);

        ImGui::TableSetColumnIndex(9);
        row_edited |= DrawAttributeCell("##controlled_environment",
                                        row,
                                        core::EvidenceAttribute::ControlledEnvironment,
                                        row.controlled_environment,
                                        callbacks);

        ImGui::TableSetColumnIndex(10);
        ImGui::Text("%d", row.used_by_cse_count);

        ImGui::TableSetColumnIndex(11);
        row_edited |= DrawAttributeCell("##notes", row, core::EvidenceAttribute::Notes, row.notes, callbacks);

        if (row_edited) {
            StoreEvidenceRow(store, row);
            edited = true;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
    return edited;
}

} // namespace ui
