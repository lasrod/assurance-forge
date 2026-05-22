#include "ui/text_edit_session.h"

#include <unordered_map>

namespace ui {

namespace {

std::unordered_map<ImGuiID, std::string>& OriginalStore() {
    static std::unordered_map<ImGuiID, std::string> store;
    return store;
}

} // namespace

bool TextEditSession::TryCommit(ImGuiID id, const std::string& current, std::string& out_original) {
    auto& store = OriginalStore();
    if (ImGui::IsItemActivated()) {
        store[id] = current;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        auto it = store.find(id);
        if (it != store.end()) {
            out_original = std::move(it->second);
            store.erase(it);
            return out_original != current;
        }
        // Deactivated without a captured original (e.g., the widget was
        // active when the project switched). Fall through with no commit;
        // the value still moves through ImGui's binding so the UI stays
        // visually consistent, but it will not be audited as an edit.
    } else if (ImGui::IsItemDeactivated()) {
        auto it = store.find(id);
        if (it != store.end())
            store.erase(it);
    }
    return false;
}

void TextEditSession::ClearAll() {
    OriginalStore().clear();
}

} // namespace ui
