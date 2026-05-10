#include "app/areas/proposal_editor_area.h"

#include "app/app_runtime_state.h"
#include "core/reviews/review_proposal.h"
#include "imgui.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace app::areas {
namespace {

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

const parser::SacmElement* FindParserElement(const parser::AssuranceCase& model, const std::string& element_id) {
    auto found = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == element_id;
    });
    return found == model.elements.end() ? nullptr : &*found;
}

core::reviews::ElementRef ExistingElementRef(const std::string& id) {
    return core::reviews::ElementRef{id, std::nullopt};
}

core::reviews::ElementRef CreatedElementRef(const std::string& create_ref) {
    return core::reviews::ElementRef{std::nullopt, create_ref};
}

bool SameElementRef(const core::reviews::ElementRef& lhs, const core::reviews::ElementRef& rhs) {
    return lhs.existing_id == rhs.existing_id && lhs.create_ref == rhs.create_ref;
}

std::optional<core::reviews::ElementRef>
ProposalRefForPreviewId(const std::string& preview_id, const std::map<std::string, std::string>& generated_ids) {
    for (const auto& generated : generated_ids) {
        if (generated.second == preview_id)
            return CreatedElementRef(generated.first);
    }
    if (!preview_id.empty())
        return ExistingElementRef(preview_id);
    return std::nullopt;
}

void TrackAffectedExistingElement(core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::string& element_id) {
    if (element_id.empty())
        return;
    if (std::find(proposal.affected_existing_element_ids.begin(),
                  proposal.affected_existing_element_ids.end(),
                  element_id) == proposal.affected_existing_element_ids.end()) {
        proposal.affected_existing_element_ids.push_back(element_id);
    }
    if (proposal.base_element_hashes.count(element_id) == 0) {
        if (const parser::SacmElement* element = FindParserElement(base_model, element_id)) {
            proposal.base_element_hashes[element_id] = core::reviews::ComputeElementSemanticHash(*element);
        }
    }
}

void TrackAffectedRef(core::reviews::ReviewProposal& proposal,
                      const parser::AssuranceCase& base_model,
                      const core::reviews::ElementRef& ref) {
    if (ref.existing_id.has_value())
        TrackAffectedExistingElement(proposal, base_model, ref.existing_id.value());
}

bool IsUpdateForElement(const core::reviews::PatchOperation& operation,
                        core::reviews::PatchOperationType type,
                        const core::reviews::ElementRef& ref,
                        const std::string& field) {
    return operation.type == type && operation.element.has_value() && SameElementRef(operation.element.value(), ref) &&
           operation.field == field;
}

void UpsertElementUpdate(core::reviews::ReviewProposal& proposal,
                         core::reviews::PatchOperationType type,
                         const core::reviews::ElementRef& ref,
                         const std::string& field,
                         const std::string& old_value,
                         const std::string& new_value) {
    proposal.operations.erase(std::remove_if(proposal.operations.begin(),
                                             proposal.operations.end(),
                                             [&](const core::reviews::PatchOperation& operation) {
                                                 return IsUpdateForElement(operation, type, ref, field);
                                             }),
                              proposal.operations.end());

    if (old_value == new_value)
        return;

    core::reviews::PatchOperation operation;
    operation.type = type;
    operation.element = ref;
    operation.field = field;
    operation.old_value = old_value;
    operation.new_value = new_value;
    proposal.operations.push_back(std::move(operation));
}

void UpsertUndevelopedUpdate(core::reviews::ReviewProposal& proposal,
                             const core::reviews::ElementRef& ref,
                             bool old_value,
                             bool new_value) {
    proposal.operations.erase(
        std::remove_if(proposal.operations.begin(),
                       proposal.operations.end(),
                       [&](const core::reviews::PatchOperation& operation) {
                           if (operation.type != core::reviews::PatchOperationType::SetUndeveloped &&
                               operation.type != core::reviews::PatchOperationType::ClearUndeveloped) {
                               return false;
                           }
                           return operation.element.has_value() && SameElementRef(operation.element.value(), ref);
                       }),
        proposal.operations.end());

    if (old_value == new_value)
        return;

    core::reviews::PatchOperation operation;
    operation.type = new_value ? core::reviews::PatchOperationType::SetUndeveloped
                               : core::reviews::PatchOperationType::ClearUndeveloped;
    operation.element = ref;
    proposal.operations.push_back(std::move(operation));
}

std::string EditableTextFor(const parser::SacmElement& element) {
    return (element.type == "claim" || element.type == "argumentreasoning") ? element.content : element.description;
}

const char* EditableTextFieldFor(const parser::SacmElement& element) {
    return (element.type == "claim" || element.type == "argumentreasoning") ? "content" : "description";
}

void SetStatus(const ProposalEditorAreaCallbacks& callbacks, const std::string& message) {
    if (callbacks.set_status)
        callbacks.set_status(message);
}

} // namespace

void RenderProposalElementEditor(AppRuntimeState& state, const ProposalEditorAreaCallbacks& callbacks) {
    auto& proposals = *state.proposal_controller;
    if (!proposals.creator_active)
        return;

    ImGui::TextUnformatted("Proposal Creator");
    ImGui::TextDisabled("Edits are recorded in the proposal draft only.");
    ImGui::Separator();

    const std::string selected_id = ui::GetUiState().selected_element_id;
    if (selected_id.empty()) {
        ImGui::TextWrapped("Select a proposal preview element to edit its proposed properties.");
        return;
    }

    const parser::SacmElement* element = FindParserElement(proposals.preview_model, selected_id);
    if (!element) {
        ImGui::TextWrapped("The selected proposal preview element no longer exists.");
        return;
    }

    static std::string active_editor_key;
    static char name_buf[256] = "";
    static char text_buf[2048] = "";
    const std::string editor_key = proposals.draft.id + ":" + selected_id;
    if (active_editor_key != editor_key) {
        active_editor_key = editor_key;
        CopyToBuffer(name_buf, sizeof(name_buf), element->name);
        CopyToBuffer(text_buf, sizeof(text_buf), EditableTextFor(*element));
    }

    const parser::SacmElement element_snapshot = *element;
    std::optional<core::reviews::ElementRef> ref =
        ProposalRefForPreviewId(selected_id, proposals.creator_generated_ids);
    if (!ref.has_value()) {
        ImGui::TextWrapped("Could not resolve this preview element for proposal edits.");
        return;
    }

    std::string old_name = element_snapshot.name;
    std::string old_text = EditableTextFor(element_snapshot);
    bool old_undeveloped = element_snapshot.undeveloped;
    if (ref->existing_id.has_value() && state.app_state.loaded_case.has_value()) {
        if (const parser::SacmElement* base = FindParserElement(state.app_state.loaded_case.value(), ref->existing_id.value())) {
            old_name = base->name;
            old_text = EditableTextFor(*base);
            old_undeveloped = base->undeveloped;
        }
    }

    ImGui::TextDisabled("%s  %s", ref->existing_id.has_value() ? "Existing" : "New", selected_id.c_str());
    if (ImGui::Button("Remove")) {
        if (callbacks.remove_selected)
            callbacks.remove_selected(core::RemoveMode::NodeOnly);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Subtree")) {
        if (callbacks.remove_selected)
            callbacks.remove_selected(core::RemoveMode::NodeAndDescendants);
        return;
    }
    ImGui::Separator();

    ImGui::PushID(editor_key.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    const bool name_changed = ImGui::InputText("Name", name_buf, sizeof(name_buf));
    ImGui::SetNextItemWidth(-1.0f);
    const bool text_changed =
        ImGui::InputTextMultiline("Text", text_buf, sizeof(text_buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f));
    bool undeveloped_value = element_snapshot.undeveloped;
    const bool undeveloped_changed = ImGui::Checkbox("Undeveloped", &undeveloped_value);
    ImGui::PopID();

    if (!name_changed && !text_changed && !undeveloped_changed)
        return;

    if (!state.app_state.loaded_case.has_value()) {
        SetStatus(callbacks, "Load a SACM model before editing proposal drafts.");
        return;
    }

    TrackAffectedRef(proposals.draft, state.app_state.loaded_case.value(), ref.value());
    if (name_changed) {
        UpsertElementUpdate(proposals.draft,
                            core::reviews::PatchOperationType::UpdateElementName,
                            ref.value(),
                            "name",
                            old_name,
                            name_buf);
    }
    if (text_changed) {
        UpsertElementUpdate(proposals.draft,
                            core::reviews::PatchOperationType::UpdateElementText,
                            ref.value(),
                            EditableTextFieldFor(element_snapshot),
                            old_text,
                            text_buf);
    }
    if (undeveloped_changed) {
        UpsertUndevelopedUpdate(proposals.draft, ref.value(), old_undeveloped, undeveloped_value);
    }

    if (callbacks.refresh_preview && callbacks.refresh_preview()) {
        SetStatus(callbacks, "Recorded proposal property change.");
    }
}

} // namespace app::areas
