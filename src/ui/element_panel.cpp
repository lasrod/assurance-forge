#include "ui/element_panel.h"
#include "ui/ui_state.h"
#include "ui/gsn_canvas.h"
#include "core/sccg_review.h"
#include "imgui.h"
#include <cstddef>
#include <cstring>
#include <vector>

namespace ui {

namespace {

// Find a parser element by ID (mutable).
parser::SacmElement* find_parser_element(parser::AssuranceCase* ac, const std::string& id) {
    if (!ac) return nullptr;
    for (auto& elem : ac->elements) {
        if (elem.id == id) return &elem;
    }
    return nullptr;
}

// Sync a parser element's editable fields into matching sacm model elements.
// We search across all argument packages, artifact packages, and terminology packages.
void sync_to_sacm(sacm::AssuranceCasePackage* pkg, const std::string& id,
                  const std::string& new_name, const std::string& new_description,
                  const std::string& new_content,
                  const std::map<std::string, std::string>& name_langs,
                  const std::map<std::string, std::string>& desc_langs,
                  const std::map<std::string, std::string>& content_langs) {
    if (!pkg) return;

    // Helper lambda to update name_ml and description_ml from lang maps
    auto update_ml = [&](sacm::SacmElement& se) {
        se.name = new_name;
        se.name_ml.texts = name_langs;
        se.description = new_description;
        se.description_ml.texts = desc_langs;
    };

    for (auto& ap : pkg->argumentPackages) {
        for (auto& c : ap.claims) {
            if (c.id == id) {
                update_ml(c);
                c.content = new_content;
                c.content_ml.texts = content_langs;
                return;
            }
        }
        for (auto& ar : ap.argumentReasonings) {
            if (ar.id == id) {
                update_ml(ar);
                ar.content = new_content;
                ar.content_ml.texts = content_langs;
                return;
            }
        }
        for (auto& ar : ap.artifactReferences) {
            if (ar.id == id) { update_ml(ar); return; }
        }
        for (auto& ai : ap.assertedInferences) {
            if (ai.id == id) { update_ml(ai); return; }
        }
        for (auto& ac : ap.assertedContexts) {
            if (ac.id == id) { update_ml(ac); return; }
        }
        for (auto& ae : ap.assertedEvidences) {
            if (ae.id == id) { update_ml(ae); return; }
        }
    }
    for (auto& artpkg : pkg->artifactPackages) {
        for (auto& a : artpkg.artifacts) {
            if (a.id == id) { update_ml(a); return; }
        }
    }
    for (auto& tp : pkg->terminologyPackages) {
        for (auto& e : tp.expressions) {
            if (e.id == id) { update_ml(e); return; }
        }
    }
}

// Helper: multi-line InputText with a buffer sized to accommodate edits.
// Returns true if the text was modified.
static bool EditableTextField(const char* label, std::string& text, float width = -1.0f) {
    char buf[2048];
    size_t len = text.size();
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text.c_str(), len);
    buf[len] = '\0';

    ImGui::PushID(label);
    if (width > 0.0f) ImGui::SetNextItemWidth(width);
    else ImGui::SetNextItemWidth(-1);

    bool changed = ImGui::InputTextMultiline(
        "##edit", buf, sizeof(buf),
        ImVec2(-1, ImGui::GetTextLineHeight() * 4),
        ImGuiInputTextFlags_AllowTabInput);

    if (changed) {
        text = buf;
    }
    ImGui::PopID();
    return changed;
}

static bool EditableSingleLine(const char* label, std::string& text) {
    char buf[512];
    size_t len = text.size();
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text.c_str(), len);
    buf[len] = '\0';

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputText("##edit", buf, sizeof(buf));
    if (changed) {
        text = buf;
    }
    ImGui::PopID();
    return changed;
}

// Check if element has a translation entry for the given language (key exists)
static bool element_has_secondary(const parser::SacmElement& elem, const std::string& lang) {
    if (elem.name_langs.count(lang)) return true;
    if (elem.description_langs.count(lang)) return true;
    if (elem.content_langs.count(lang)) return true;
    return false;
}

// Supported secondary languages (no special font requirements except ja which uses merged font)
static const char* const kLangCodes[] = { "ja", "de", "fr", "es", "it", "pt", "nl", "sv", "no", "da", "fi", "pl", "cs", "ro", "hu" };
static const char* const kLangLabels[] = { "Japanese", "German", "French", "Spanish", "Italian", "Portuguese", "Dutch", "Swedish", "Norwegian", "Danish", "Finnish", "Polish", "Czech", "Romanian", "Hungarian" };
static const int kLangCount = 15;

}  // namespace

bool ShowElementPanel(parser::AssuranceCase* ac, sacm::AssuranceCasePackage* sacm_pkg) {
    const UiState& state = GetUiState();
    bool modified = false;

    if (state.selected_element_id.empty()) {
        ImGui::TextDisabled("No element selected.");
        return false;
    }

    if (!ac) {
        ImGui::TextDisabled("No safety case loaded.");
        return false;
    }

    // Find the element by ID
    parser::SacmElement* elem = find_parser_element(ac, state.selected_element_id);
    if (!elem) {
        ImGui::TextDisabled("Element not found: %s", state.selected_element_id.c_str());
        return false;
    }

    // Scope all widgets by element ID so switching elements creates fresh widget state
    ImGui::PushID(elem->id.c_str());

    const std::string sec_lang = state.active_secondary_lang;
    bool has_secondary = element_has_secondary(*elem, sec_lang);

    // ID (read-only)
    ImGui::Text("ID");
    ImGui::Separator();
    ImGui::TextWrapped("%s", elem->id.c_str());
    ImGui::Spacing();

    // Type (read-only)
    ImGui::Text("Type");
    ImGui::Separator();
    ImGui::TextWrapped("%s", elem->type.c_str());
    ImGui::Spacing();

    // Name (editable)
    ImGui::Text("Name");
    ImGui::Separator();
    if (EditableSingleLine("name", elem->name)) {
        elem->name_langs["en"] = elem->name;
        modified = true;
    }
    // Secondary language name (only show if this field has the secondary language)
    if (elem->name_langs.count(sec_lang)) {
        ImGui::Text("Name (%s)", sec_lang.c_str());
        if (g_BoldFont) ImGui::PushFont(g_BoldFont);
        std::string sec_name = elem->name_langs.at(sec_lang);
        if (EditableSingleLine("name_sec", sec_name)) {
            elem->name_langs[sec_lang] = sec_name;
            modified = true;
        }
        if (g_BoldFont) ImGui::PopFont();
    }
    ImGui::Spacing();

    // Content (editable, for claims and argument reasonings)
    bool has_content = (elem->type == "claim" || elem->type == "argumentreasoning");
    if (has_content) {
        ImGui::Text("Content");
        ImGui::Separator();
        if (EditableTextField("content", elem->content)) {
            elem->content_langs["en"] = elem->content;
            modified = true;
        }
        // Secondary language content (only show if this field has the secondary language)
        if (elem->content_langs.count(sec_lang)) {
            ImGui::Text("Content (%s)", sec_lang.c_str());
            std::string sec_content = elem->content_langs.at(sec_lang);
            if (EditableTextField("content_sec", sec_content)) {
                elem->content_langs[sec_lang] = sec_content;
                modified = true;
            }
        }
        ImGui::Spacing();
    }

    // Description (editable)
    ImGui::Text("Description");
    ImGui::Separator();
    if (EditableTextField("description", elem->description)) {
        elem->description_langs["en"] = elem->description;
        modified = true;
    }
    // Secondary language description (only show if this field has the secondary language)
    if (elem->description_langs.count(sec_lang)) {
        ImGui::Text("Description (%s)", sec_lang.c_str());
        std::string sec_desc = elem->description_langs.at(sec_lang);
        if (EditableTextField("description_sec", sec_desc)) {
            elem->description_langs[sec_lang] = sec_desc;
            modified = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Translation controls: checkbox + language dropdown
    {
        UiState& mut_state = GetUiState();

        // Language selector dropdown
        ImGui::Text("Translation Language");
        int current_lang_idx = 0;
        for (int i = 0; i < kLangCount; ++i) {
            if (mut_state.active_secondary_lang == kLangCodes[i]) { current_lang_idx = i; break; }
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##trans_lang", &current_lang_idx, kLangLabels, kLangCount)) {
            mut_state.active_secondary_lang = kLangCodes[current_lang_idx];
        }

        ImGui::Spacing();

        // Checkbox to enable/disable translation for this element
        bool has_trans = element_has_secondary(*elem, mut_state.active_secondary_lang);
        if (ImGui::Checkbox("Add Translation", &has_trans)) {
            if (has_trans) {
                // Enable: insert empty entries so the fields appear
                elem->name_langs[mut_state.active_secondary_lang] = "";
                elem->description_langs[mut_state.active_secondary_lang] = "";
                if (has_content) {
                    elem->content_langs[mut_state.active_secondary_lang] = "";
                }
            } else {
                // Disable: remove the language entries
                elem->name_langs.erase(mut_state.active_secondary_lang);
                elem->description_langs.erase(mut_state.active_secondary_lang);
                elem->content_langs.erase(mut_state.active_secondary_lang);
            }
            modified = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("SCCG Review Tags");
    ImGui::TextDisabled("Attach guideline findings to this element.");

    bool sccg_modified = false;
    std::vector<core::ElementSccgTag> tags = core::GetTagsForElement(elem->id);
    const std::vector<core::SccgGuideline>& guidelines = core::GetSccgGuidelines();

    if (tags.empty()) {
        ImGui::TextDisabled("No SCCG tags on this element.");
    }

    for (size_t i = 0; i < tags.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();

        int selected_guideline_idx = -1;
        for (int gi = 0; gi < static_cast<int>(guidelines.size()); ++gi) {
            if (guidelines[gi].id == tags[i].guideline_id) {
                selected_guideline_idx = gi;
                break;
            }
        }

        const char* preview = tags[i].guideline_id.empty() ? "Select guideline" : tags[i].guideline_id.c_str();
        if (ImGui::BeginCombo("Guideline ID", preview)) {
            for (int gi = 0; gi < static_cast<int>(guidelines.size()); ++gi) {
                bool is_selected = (gi == selected_guideline_idx);
                std::string label = guidelines[gi].id + " - " + guidelines[gi].title;
                if (ImGui::Selectable(label.c_str(), is_selected)) {
                    tags[i].guideline_id = guidelines[gi].id;
                    sccg_modified = true;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        static const char* kStatuses[] = {
            "Open",
            "Mitigated",
            "Accepted",
            "False Positive",
        };
        int status_idx = 0;
        for (int si = 0; si < 4; ++si) {
            if (tags[i].status == kStatuses[si]) {
                status_idx = si;
                break;
            }
        }
        if (ImGui::Combo("Status", &status_idx, kStatuses, 4)) {
            tags[i].status = kStatuses[status_idx];
            sccg_modified = true;
        }

        static const char* kSeverities[] = {
            "Info",
            "Minor",
            "Major",
        };
        int severity_idx = 0;
        for (int si = 0; si < 3; ++si) {
            if (tags[i].severity == kSeverities[si]) {
                severity_idx = si;
                break;
            }
        }
        if (ImGui::Combo("Severity", &severity_idx, kSeverities, 3)) {
            tags[i].severity = kSeverities[severity_idx];
            sccg_modified = true;
        }

        ImGui::Text("Comment");
        if (EditableTextField("sccg_comment", tags[i].comment)) {
            sccg_modified = true;
        }

        if (ImGui::Button("Remove Tag")) {
            tags.erase(tags.begin() + static_cast<std::ptrdiff_t>(i));
            sccg_modified = true;
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    if (ImGui::Button("Add SCCG Tag")) {
        core::ElementSccgTag tag;
        tag.status = "Open";
        tag.severity = "Minor";
        if (!guidelines.empty()) {
            tag.guideline_id = guidelines.front().id;
        }
        tags.push_back(std::move(tag));
        sccg_modified = true;
    }

    if (sccg_modified) {
        core::SetTagsForElement(elem->id, tags);
    }

    // Sync edits to SACM model
    if (modified) {
        sync_to_sacm(sacm_pkg, elem->id, elem->name, elem->description, elem->content,
                      elem->name_langs, elem->description_langs, elem->content_langs);
    }

    ImGui::PopID();  // element ID scope
    return modified;
}

}  // namespace ui
