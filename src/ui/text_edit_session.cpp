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
    } else if (ImGui::IsItemActive()) {
        if (auto it = store.find(id); it != store.end()) {
            // Keep the latest typed value (and refresh metadata) so a forced
            // flush can commit the in-progress edit.
            //
            // Only while the item is ACTIVE. An entry can outlive its edit: a
            // click that changes the selection in the same frame it leaves the
            // field means this widget is never submitted again, so the
            // deactivation commit never fires and the entry stays pending until
            // a flush point commits it. If the widget renders again before that
            // (the user re-selects the element), an unconditional refresh here
            // overwrote the pending entry's captured value with the model's
            // current one -- silently discarding the very edit the flush
            // safety-net exists to save.
            it->second.current = current;
            it->second.element_id = element_id;
            it->second.field_token = field_token;
            it->second.language = language;
        }
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
