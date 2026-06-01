#include "ui/text_edit_session.h"

#include <unordered_map>
#include <utility>

namespace ui {

namespace {

struct Entry {
    std::string original;
    std::string current;
    std::string element_id;
    std::string field_token;
    std::string language;
};

std::unordered_map<ImGuiID, Entry>& Store() {
    static std::unordered_map<ImGuiID, Entry> store;
    return store;
}

} // namespace

bool TextEditSession::Track(ImGuiID id,
                            const std::string& current,
                            const std::string& element_id,
                            const std::string& field_token,
                            const std::string& language,
                            PendingTextEdit& out_commit) {
    auto& store = Store();
    if (ImGui::IsItemActivated()) {
        Entry entry;
        entry.original = current;
        entry.current = current;
        entry.element_id = element_id;
        entry.field_token = field_token;
        entry.language = language;
        store[id] = std::move(entry);
    } else if (auto it = store.find(id); it != store.end()) {
        // Keep the latest typed value (and refresh metadata in case the same
        // widget id renders a different element/field across frames) so a
        // forced flush can commit the in-progress edit.
        it->second.current = current;
        it->second.element_id = element_id;
        it->second.field_token = field_token;
        it->second.language = language;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        auto it = store.find(id);
        if (it != store.end()) {
            const bool changed = it->second.original != current;
            out_commit.element_id = it->second.element_id;
            out_commit.field_token = it->second.field_token;
            out_commit.language = it->second.language;
            out_commit.original_value = it->second.original;
            out_commit.new_value = current;
            store.erase(it);
            return changed;
        }
        // Deactivated without a captured original (e.g., the widget was
        // active when the project switched). Fall through with no commit;
        // the value still moves through ImGui's binding so the UI stays
        // visually consistent, but it will not be audited as an edit.
    } else if (ImGui::IsItemDeactivated()) {
        store.erase(id);
    }
    return false;
}

std::vector<PendingTextEdit> TextEditSession::CollectPendingEdits() {
    std::vector<PendingTextEdit> pending;
    for (auto& [id, entry] : Store()) {
        (void)id;
        if (entry.original == entry.current)
            continue;
        PendingTextEdit edit;
        edit.element_id = entry.element_id;
        edit.field_token = entry.field_token;
        edit.language = entry.language;
        edit.original_value = entry.original;
        edit.new_value = entry.current;
        pending.push_back(std::move(edit));
        // Advance the baseline so the ImGui deactivation that may still arrive
        // for this widget does not record the same change a second time.
        entry.original = entry.current;
    }
    return pending;
}

void TextEditSession::ClearAll() {
    Store().clear();
}

} // namespace ui
