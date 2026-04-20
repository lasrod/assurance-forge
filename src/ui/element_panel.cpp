#include "ui/element_panel.h"
#include "ui/ui_state.h"
#include "imgui.h"
#include <cstring>

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
                  const std::string& new_content) {
    if (!pkg) return;

    for (auto& ap : pkg->argumentPackages) {
        for (auto& c : ap.claims) {
            if (c.id == id) { c.name = new_name; c.description = new_description; c.content = new_content; return; }
        }
        for (auto& ar : ap.argumentReasonings) {
            if (ar.id == id) { ar.name = new_name; ar.description = new_description; ar.content = new_content; return; }
        }
        for (auto& ar : ap.artifactReferences) {
            if (ar.id == id) { ar.name = new_name; ar.description = new_description; return; }
        }
        for (auto& ai : ap.assertedInferences) {
            if (ai.id == id) { ai.name = new_name; ai.description = new_description; return; }
        }
        for (auto& ac : ap.assertedContexts) {
            if (ac.id == id) { ac.name = new_name; ac.description = new_description; return; }
        }
        for (auto& ae : ap.assertedEvidences) {
            if (ae.id == id) { ae.name = new_name; ae.description = new_description; return; }
        }
    }
    for (auto& artpkg : pkg->artifactPackages) {
        for (auto& a : artpkg.artifacts) {
            if (a.id == id) { a.name = new_name; a.description = new_description; return; }
        }
    }
    for (auto& tp : pkg->terminologyPackages) {
        for (auto& e : tp.expressions) {
            if (e.id == id) { e.name = new_name; e.description = new_description; return; }
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
        modified = true;
    }
    ImGui::Spacing();

    // Content (editable, for claims and argument reasonings)
    if (elem->type == "claim" || elem->type == "argumentreasoning") {
        ImGui::Text("Content");
        ImGui::Separator();
        if (EditableTextField("content", elem->content)) {
            modified = true;
        }
        ImGui::Spacing();
    }

    // Description (editable)
    ImGui::Text("Description");
    ImGui::Separator();
    if (EditableTextField("description", elem->description)) {
        modified = true;
    }

    // Sync edits to SACM model
    if (modified) {
        sync_to_sacm(sacm_pkg, elem->id, elem->name, elem->description, elem->content);
    }

    return modified;
}

}  // namespace ui
