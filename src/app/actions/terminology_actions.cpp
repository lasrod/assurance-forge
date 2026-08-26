#include "app/actions/terminology_actions.h"

#include "app/actions/terminology_actions_internal.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/commands/package_commands.h"
#include "core/commands/terminology_commands.h"
#include "core/ignored_terminology_store.h"
#include "core/reviews/review_proposal.h"
#include "core/string_utils.h"
#include "core/terminology_text_utils.h"
#include "sacm_adapter/document_edit.h"
#include "parser/model_utils.h"
#include "parser/xml_parser.h"
#include "ui/i18n/localization.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/ui_state.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace app::actions {

using core::TrimWhitespace;
using detail::CanSwitchProjectSacmFile;
using detail::CategoryDraftFromEditor;
using detail::CategoryNameExists;
using detail::ClearCategoryEditorBuffers;
using detail::ClearTermEditorBuffers;
using detail::CopyCategoryToEditor;
using detail::CopyTerminologyPackageToEditor;
using detail::CopyTermToEditor;
using detail::DecodeTerminologyTermQuickFixPayload;
using detail::EnsureProjectSacmFileOpen;
using detail::EnsureQuickDefineTargetPackage;
using detail::HasTerminologyPackageRef;
using detail::InvalidateSacmPackageTreeCache;
using detail::OpenTerminologyProblemTerm;
using detail::QuickDefineTargetPackageResult;
using detail::RefreshVisibleTerminologyContextProjection;
using detail::ResolveQuickDefineTargetPackage;
using detail::SetStatus;
using detail::SyncVisibleTerminologyContextToParser;
using detail::TermDraftFromEditor;
using detail::TerminologyPackageRefFor;
using detail::TerminologySuggestionKey;
using detail::TerminologyTermQuickFixPayload;
using detail::TermStatusLabel;
using ui::CopyToBuffer;

namespace {

// The editor's result as the operations an MCP client would send (ADR 0016):
// one `UpdateTerm` per field that changed, so classifying a term cannot rewrite
// its definition or drop the translations of it -- the same guarantee the seams
// give a client -- and a term whose editor was opened and saved unchanged sends
// nothing.

core::reviews::ElementRef ExistingRef(const std::string& id) {
    core::reviews::ElementRef ref;
    ref.existing_id = id;
    return ref;
}

core::reviews::ElementRef CreatedRef(const std::string& create_ref) {
    core::reviews::ElementRef ref;
    ref.create_ref = create_ref;
    return ref;
}

// Space separated: the idref-list convention `UpdateTerm` field "category"
// reads, distinct from the comma-separated form the editor buffer shows.
std::string JoinCategoryIds(const std::vector<std::string>& refs) {
    std::string joined;
    for (const std::string& ref : refs) {
        if (ref.empty())
            continue;
        if (!joined.empty())
            joined += ' ';
        joined += ref;
    }
    return joined;
}

core::reviews::PatchOperation
UpdateTermField(const core::reviews::ElementRef& term, const char* field, const std::string& value) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::UpdateTerm;
    operation.element = term;
    operation.field = field;
    operation.new_value = value;
    return operation;
}

void AppendTermFieldUpdates(std::vector<core::reviews::PatchOperation>& operations,
                            const core::reviews::ElementRef& term,
                            const sacm::Term& current,
                            const core::TerminologyTermDraft& draft) {
    if (draft.value != current.value)
        operations.push_back(UpdateTermField(term, core::reviews::kTermFieldValue, draft.value));
    if (draft.description != current.description)
        operations.push_back(UpdateTermField(term, core::reviews::kTermFieldDefinition, draft.description));
    if (draft.name != current.name)
        operations.push_back(UpdateTermField(term, core::reviews::kTermFieldName, draft.name));
    if (draft.category_refs != current.category_refs)
        operations.push_back(
            UpdateTermField(term, core::reviews::kTermFieldCategory, JoinCategoryIds(draft.category_refs)));
    if (draft.externalReference != current.externalReference)
        operations.push_back(
            UpdateTermField(term, core::reviews::kTermFieldExternalReference, draft.externalReference));
    if (draft.origin != current.origin)
        operations.push_back(UpdateTermField(term, core::reviews::kTermFieldOrigin, draft.origin));
}

// A create carries the value and the definition; every other field follows as
// an update against the created reference, in the same batch.
void AppendCreateTerm(std::vector<core::reviews::PatchOperation>& operations,
                      const std::string& create_ref,
                      const core::TerminologyTermDraft& draft) {
    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateTerm;
    create.create_ref = create_ref;
    create.text = draft.value;
    create.new_value = draft.description;
    operations.push_back(create);

    sacm::Term created;
    created.value = draft.value;
    created.description = draft.description;
    AppendTermFieldUpdates(operations, CreatedRef(create_ref), created, draft);
}

core::reviews::PatchOperation
UpdateCategoryField(const core::reviews::ElementRef& category, const char* field, const std::string& value) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::UpdateCategory;
    operation.element = category;
    operation.field = field;
    operation.new_value = value;
    return operation;
}

core::reviews::PatchOperation CreateCategoryOperation(const std::string& create_ref,
                                                      const core::TerminologyCategoryDraft& draft) {
    core::reviews::PatchOperation create;
    create.type = core::reviews::PatchOperationType::CreateCategory;
    create.create_ref = create_ref;
    create.text = draft.name;
    create.new_value = draft.description;
    return create;
}

constexpr const char* kCreatedTermRef = "$term";
constexpr const char* kCreatedCategoryRef = "$category";

} // namespace

TerminologyActions::TerminologyActions(AppRuntimeState& state) : state_(state) {}

void TerminologyActions::BeginAddPackage(const core::ProjectFileEntry& entry,
                                         const sacm::SacmPackageTreeNode& parent_node) {
    if (AcceptedEditRefused(AF_TR("Adding a terminology package")))
        return;
    if (parent_node.type != sacm::SacmPackageNodeType::AssuranceCasePackage)
        return;
    if (!CanSwitchProjectSacmFile(state_.app_state, entry)) {
        SetStatus(state_, "Save the current SACM file before adding a terminology package.");
        return;
    }

    state_.terminology.pending_package_parent_entry = entry;
    CopyToBuffer(state_.terminology.new_package_name_buf,
                 sizeof(state_.terminology.new_package_name_buf),
                 "Terminology Package");
    CopyToBuffer(
        state_.terminology.new_package_description_buf, sizeof(state_.terminology.new_package_description_buf), "");
    state_.terminology.show_create_package_modal = true;
}

bool TerminologyActions::ConfirmAddPackage() {
    if (AcceptedEditRefused(AF_TR("Adding a terminology package")))
        return false;
    if (!state_.terminology.pending_package_parent_entry.has_value()) {
        state_.terminology.show_create_package_modal = false;
        return false;
    }

    const core::ProjectFileEntry entry = state_.terminology.pending_package_parent_entry.value();
    if (!CanSwitchProjectSacmFile(state_.app_state, entry)) {
        SetStatus(state_, "Save the current SACM file before adding a terminology package.");
        return false;
    }
    if (!EnsureProjectSacmFileOpen(state_, entry, false))
        return false;
    if (!state_.app_state.has_projected_package()) {
        SetStatus(state_, "Could not load an editable SACM package model.");
        return false;
    }

    core::commands::CreateTerminologyPackageCommand command(
        TrimWhitespace(state_.terminology.new_package_name_buf),
        TrimWhitespace(state_.terminology.new_package_description_buf));
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Terminology package create failed: " + outcome.error);
        return false;
    }
    const core::TerminologyPackageRef created_ref = command.GeneratedRef();

    state_.terminology.selected_package_ref = created_ref;
    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package =
            core::FindTerminologyPackage(state_.app_state.projected_package(), created_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }
    InvalidateSacmPackageTreeCache(state_, entry.relativePath);
    state_.terminology.show_create_package_modal = false;
    state_.terminology.pending_package_parent_entry.reset();
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Added terminology package " + created_ref.id + ".");
    return true;
}

bool TerminologyActions::ApplyPackageEdits() {
    if (AcceptedEditRefused(AF_TR("Editing a terminology package")))
        return false;
    if (!state_.app_state.has_projected_package())
        return false;

    core::commands::UpdateTerminologyPackageCommand command(state_.terminology.selected_package_ref,
                                                            TrimWhitespace(state_.terminology.package_name_buf),
                                                            TrimWhitespace(state_.terminology.package_description_buf));
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Terminology package update failed: " + outcome.error);
        return false;
    }

    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    return true;
}

void TerminologyActions::BeginDeletePackage() {
    if (AcceptedEditRefused(AF_TR("Deleting a terminology package")))
        return;
    state_.terminology.show_delete_package_modal = true;
}

bool TerminologyActions::ConfirmDeletePackage() {
    if (AcceptedEditRefused(AF_TR("Deleting a terminology package")))
        return false;
    // Route the delete through the command bus instead of mutating `sacm_package`
    // directly. With a project audit bus this makes it a recorded, replayable,
    // library-primary transaction; opened outside a project it falls back to the
    // shared dispatch path (a direct apply that still keeps the library in step).
    const core::TerminologyPackageRef package_ref = state_.terminology.selected_package_ref;
    core::commands::RemoveTerminologyPackageCommand command(package_ref.id, package_ref.gid);
    const app::commands::DispatchOutcome outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        // Always surface a failure (the modal stays open otherwise); fall back to a
        // generic message if the dispatch reported no error string.
        SetStatus(state_,
                  "Terminology package delete failed: " +
                      (outcome.error.empty() ? std::string("the delete could not be completed.") : outcome.error));
        return false;
    }

    state_.events.Emit(DocumentDirtyEvent{});
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    state_.terminology.selected_package_ref = core::TerminologyPackageRef{};
    state_.terminology.show_delete_package_modal = false;
    state_.workbench.show_terminology_package_tab = false;
    ui::GetUiState().center_view = ui::CenterView::PackageDetails;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Deleted terminology package.");
    return true;
}

bool TerminologyActions::OpenTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    // The working glossary (ADR 0016): the canvas detects terms against it, so
    // a chip it drew may name a term only the draft holds yet. Opening is a
    // read; the tab it opens says whether what it shows is accepted.
    const sacm::AssuranceCasePackage* working_package = state_.WorkingPackage();
    if (working_package == nullptr) {
        SetStatus(state_, "Open a SACM model before opening terminology terms.");
        return false;
    }

    const sacm::TerminologyPackage* terminology_package = core::FindTerminologyPackage(*working_package, package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return false;
    }
    const sacm::Term* term = core::FindTerminologyTerm(*terminology_package, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }

    state_.terminology.selected_package_ref = package_ref;
    state_.terminology.selected_term_ref = term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    CopyTerminologyPackageToEditor(state_, *terminology_package);
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), term->value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_, "Opened term " + term->value + ".");
    return true;
}

bool TerminologyActions::EditTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!OpenTermFromCanvas(package_ref, term_ref))
        return false;
    const sacm::Term* term = WorkingTerm(package_ref, term_ref);
    if (!term)
        return false;
    state_.terminology.selected_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.terminology.editing_existing_term = true;
    state_.terminology.show_term_editor_modal = true;
    return true;
}

bool TerminologyActions::AddTermAsContextFromCanvas(const std::string& element_id,
                                                    const core::TerminologyPackageRef& package_ref,
                                                    const core::TerminologyTermRef& term_ref) {
    if (AcceptedEditRefused(AF_TR("Linking a term to an element")))
        return false;
    if (!state_.app_state.has_projected_package()) {
        SetStatus(state_, "Open a SACM model before associating terminology.");
        return false;
    }

    core::commands::AssociateTerminologyTermWithElementCommand command(element_id, package_ref, term_ref);
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Could not associate term with element: " + outcome.error);
        return false;
    }
    const core::TerminologyContextAssociationResult& result = command.Result();

    if (!result.already_associated) {
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
    }
    SetStatus(state_,
              result.already_associated ? "Term is already associated with this element."
                                        : "Associated term with this element.");
    return true;
}

bool TerminologyActions::AddVisibleTermContextFromCanvas(const std::string& element_id,
                                                         const core::TerminologyPackageRef& package_ref,
                                                         const core::TerminologyTermRef& term_ref) {
    if (AcceptedEditRefused(AF_TR("Adding a term as context")))
        return false;
    if (!state_.app_state.has_projected_package()) {
        SetStatus(state_, "Open a SACM model before adding terminology context.");
        return false;
    }

    const std::string term_label = TermStatusLabel(state_.app_state.projected_package(), package_ref, term_ref);
    core::commands::AddTerminologyTermAsVisibleContextCommand command(element_id, package_ref, term_ref);
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Could not add term as context: " + outcome.error);
        return false;
    }
    const core::TerminologyContextAssociationResult& result = command.Result();

    // Parser projection is synchronised inside the command's Apply, so the
    // parser model is already up to date; nothing more to do here besides
    // refreshing dirty state and emitting tree events.
    const bool parser_changed = !result.already_associated;
    if (!result.already_associated) {
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
    }
    if (!result.already_associated || parser_changed)
        state_.events.Emit(TreeDirtyEvent{});
    SetStatus(state_,
              result.already_associated ? term_label + " is already attached as context to this element."
                                        : "Added " + term_label + " as context.");
    return true;
}

void TerminologyActions::SelectTerm(const core::TerminologyTermRef& term_ref) {
    state_.terminology.selected_term_ref = term_ref;
}

void TerminologyActions::BeginAddTerm() {
    if (state_.WorkingPackage() == nullptr) {
        SetStatus(state_, "Open a terminology package before adding terms.");
        return;
    }
    if (DraftTakesGlossaryEdits() && DraftCreateTargetRefused(state_.terminology.selected_package_ref))
        return;
    ClearTermEditorBuffers(state_);
    state_.terminology.editing_existing_term = false;
    state_.terminology.show_term_editor_modal = true;
}

bool TerminologyActions::BeginEditTerm(const core::TerminologyTermRef& term_ref) {
    const sacm::Term* term = WorkingTerm(state_.terminology.selected_package_ref, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }
    state_.terminology.selected_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.terminology.editing_existing_term = true;
    state_.terminology.show_term_editor_modal = true;
    return true;
}

bool TerminologyActions::ConfirmTermEdit() {
    if (state_.WorkingPackage() == nullptr)
        return false;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    if (DraftTakesGlossaryEdits())
        return ConfirmTermEditInDraft(draft);
    if (state_.terminology.editing_existing_term) {
        core::commands::UpdateTerminologyTermCommand command(
            state_.terminology.selected_package_ref, state_.terminology.selected_term_ref, draft);
        const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
        if (!outcome.success) {
            SetStatus(state_, "Term update failed: " + outcome.error);
            return false;
        }
        SetStatus(state_, "Updated term " + draft.value + ".");
    } else {
        core::commands::CreateTerminologyTermCommand command(state_.terminology.selected_package_ref, draft);
        const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
        if (!outcome.success) {
            SetStatus(state_, "Term create failed: " + outcome.error);
            return false;
        }
        state_.terminology.selected_term_ref = command.GeneratedRef();
        SetStatus(state_, "Added term " + draft.value + ".");
    }

    state_.events.Emit(DocumentDirtyEvent{});
    if (RefreshVisibleTerminologyContextProjection(state_.app_state))
        state_.events.Emit(TreeDirtyEvent{});
    state_.terminology.show_term_editor_modal = false;
    return true;
}

bool TerminologyActions::DraftTakesGlossaryEdits() const {
    return app::commands::DraftDocumentTakesEdits(state_);
}

bool TerminologyActions::AcceptedEditRefused(const std::string& gesture) {
    std::string reason;
    if (!detail::AcceptedGlossaryEditBlockedByDraft(state_, gesture, reason))
        return false;
    SetStatus(state_, reason);
    return true;
}

bool TerminologyActions::DraftCreateTargetRefused(const core::TerminologyPackageRef& package_ref) {
    if (!DraftTakesGlossaryEdits() || !HasTerminologyPackageRef(package_ref))
        return false;
    const std::string target = sacm_adapter::resolve_terminology_package_id(*state_.draft_document.document());
    if (target.empty())
        return false;
    const sacm::TerminologyPackage* chosen = WorkingTerminologyPackage(package_ref);
    if (chosen != nullptr && chosen->id == target)
        return false;
    SetStatus(state_,
              ui::i18n::trf("While a working draft is open, new terms and categories go into the case's first "
                            "glossary ({0}). Select it, or accept or discard the draft first.",
                            target));
    return true;
}

const sacm::TerminologyPackage*
TerminologyActions::WorkingTerminologyPackage(const core::TerminologyPackageRef& package_ref) {
    const sacm::AssuranceCasePackage* package = state_.WorkingPackage();
    return package != nullptr ? core::FindTerminologyPackage(*package, package_ref) : nullptr;
}

const sacm::Term* TerminologyActions::WorkingTerm(const core::TerminologyPackageRef& package_ref,
                                                  const core::TerminologyTermRef& term_ref) {
    const sacm::TerminologyPackage* package = WorkingTerminologyPackage(package_ref);
    return package != nullptr ? core::FindTerminologyTerm(*package, term_ref) : nullptr;
}

const sacm::Category* TerminologyActions::WorkingCategory(const core::TerminologyPackageRef& package_ref,
                                                          const core::TerminologyCategoryRef& category_ref) {
    const sacm::TerminologyPackage* package = WorkingTerminologyPackage(package_ref);
    return package != nullptr ? core::FindTerminologyCategory(*package, category_ref) : nullptr;
}

bool TerminologyActions::ConfirmTermEditInDraft(const core::TerminologyTermDraft& draft) {
    std::vector<core::reviews::PatchOperation> operations;
    const bool editing = state_.terminology.editing_existing_term;
    if (editing) {
        const sacm::Term* current =
            WorkingTerm(state_.terminology.selected_package_ref, state_.terminology.selected_term_ref);
        if (current == nullptr) {
            SetStatus(state_, "Term not found.");
            return false;
        }
        AppendTermFieldUpdates(operations, ExistingRef(current->id), *current, draft);
        if (operations.empty()) {
            state_.terminology.show_term_editor_modal = false;
            return true;
        }
    } else {
        if (DraftCreateTargetRefused(state_.terminology.selected_package_ref))
            return false;
        AppendCreateTerm(operations, kCreatedTermRef, draft);
    }

    const app::commands::DraftEditOutcome outcome = app::commands::DispatchDraftDocumentEdit(state_, operations);
    if (!outcome.success) {
        SetStatus(state_, (editing ? "Term update failed: " : "Term create failed: ") + outcome.error);
        return false;
    }
    if (!editing) {
        const auto created = outcome.created_ids.find(kCreatedTermRef);
        state_.terminology.selected_term_ref =
            core::TerminologyTermRef{created != outcome.created_ids.end() ? created->second : std::string{}, {}};
    }
    state_.terminology.show_term_editor_modal = false;
    SetStatus(state_,
              editing ? ui::i18n::trf("Updated term {0} in the working draft.", draft.value)
                      : ui::i18n::trf("Added term {0} to the working draft.", draft.value));
    return true;
}

// Asks the SACM library what deleting `term_id` from `document` would take with
// it. A glossary term that has been added as a visible context is referenced
// by an ArtifactReference/AssertedContext pair living in an ArgumentPackage,
// and removing those is a cascade across a package boundary that the library
// will not perform unless the caller opts in -- so the user has to be shown
// the list and asked. An empty result means the plain delete is enough.
void TerminologyActions::PreviewTermDeleteReferences(const sacm_adapter::LibraryDocument& document,
                                                     const std::string& term_id) {
    state_.terminology.pending_delete_term_references.clear();
    state_.terminology.pending_delete_term_blockers.clear();
    state_.terminology.pending_delete_term_preview_available = false;
    if (term_id.empty())
        return;
    // Only offer the cascade where the delete can actually honour it. Without a
    // command bus -- a SACM file opened outside a project -- the dispatch hands
    // the command no library document (the tracked #347 exception), so it takes
    // the legacy path, which has no cascade. Previewing anyway would ask the user
    // to confirm removals and then refuse them.
    if (state_.command_bus == nullptr && !DraftTakesGlossaryEdits())
        return;

    const sacm_adapter::DeletePreview preview = sacm_adapter::preview_delete_terminology_element(document, term_id);
    if (!preview.supported)
        return;

    // `can_apply` here means the term itself would go. When it would not, the
    // consequential list describes removals that lead to a refusal rather than to
    // a delete, so offering it would collect consent for something that cannot
    // happen. Show the library's reason instead and leave the plain delete to
    // fail with the same one.
    if (!preview.can_apply) {
        for (const sacm_adapter::LoadDiagnostic& diagnostic : preview.diagnostics)
            state_.terminology.pending_delete_term_blockers.push_back(diagnostic.code + ": " + diagnostic.message);
        return;
    }

    // The draft's `RemoveTerm` has no cascade: removing what an argument package
    // references crosses a package boundary the library refuses by default, and
    // the draft surface offers no consent for it. Listing the references as a
    // cascade the confirm would perform, and then refusing, would collect
    // consent for something that cannot happen -- so they are a blocker here.
    if (DraftTakesGlossaryEdits() && !preview.consequential.empty()) {
        state_.terminology.pending_delete_term_blockers.push_back(
            ui::i18n::trnf("Deleting it would also remove {0} element that references it, which the working draft "
                           "cannot do. Remove that reference first, or accept or discard the draft.",
                           "Deleting it would also remove {0} elements that reference it, which the working draft "
                           "cannot do. Remove those references first, or accept or discard the draft.",
                           static_cast<int>(preview.consequential.size()),
                           static_cast<int>(preview.consequential.size())));
        return;
    }

    state_.terminology.pending_delete_term_preview_available = true;
    for (const sacm_adapter::DeleteEffect& effect : preview.consequential) {
        state_.terminology.pending_delete_term_references.push_back(
            app::controllers::ElementEditController::RemovalEffect{
                .element_id = effect.element_id,
                .kind = effect.kind,
                .name = effect.name,
                .is_relationship = effect.is_relationship,
                .deleted = effect.deleted,
            });
    }
}

void TerminologyActions::BeginDeleteTerm(const core::TerminologyTermRef& term_ref) {
    const sacm::AssuranceCasePackage* working_package = state_.WorkingPackage();
    if (working_package == nullptr)
        return;
    const sacm::Term* term = WorkingTerm(state_.terminology.selected_package_ref, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return;
    }
    state_.terminology.selected_term_ref = term_ref;
    state_.terminology.pending_delete_term_usage_count = core::CountTerminologyTermUsage(*working_package, *term);
    if (DraftTakesGlossaryEdits()) {
        PreviewTermDeleteReferences(*state_.draft_document.document(), term->id);
    } else if (state_.app_state.library_document != nullptr) {
        PreviewTermDeleteReferences(*state_.app_state.library_document, term->id);
    } else {
        state_.terminology.pending_delete_term_references.clear();
        state_.terminology.pending_delete_term_blockers.clear();
        state_.terminology.pending_delete_term_preview_available = false;
    }
    state_.terminology.show_delete_term_modal = true;
}

bool TerminologyActions::ConfirmDeleteTermInDraft() {
    const sacm::Term* current =
        WorkingTerm(state_.terminology.selected_package_ref, state_.terminology.selected_term_ref);
    if (current == nullptr) {
        SetStatus(state_, "Term not found.");
        return false;
    }
    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveTerm;
    remove.element = ExistingRef(current->id);
    const std::string value = current->value;

    const app::commands::DraftEditOutcome outcome = app::commands::DispatchDraftDocumentEdit(state_, {remove});
    if (!outcome.success) {
        SetStatus(state_, "Term delete failed: " + outcome.error);
        return false;
    }
    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.pending_delete_term_references.clear();
    state_.terminology.pending_delete_term_blockers.clear();
    state_.terminology.pending_delete_term_preview_available = false;
    state_.terminology.show_delete_term_modal = false;
    SetStatus(state_, ui::i18n::trf("Deleted term {0} in the working draft.", value));
    return true;
}

bool TerminologyActions::ConfirmDeleteTerm() {
    if (state_.WorkingPackage() == nullptr)
        return false;
    if (DraftTakesGlossaryEdits())
        return ConfirmDeleteTermInDraft();

    // Confirming the modal IS the consent to remove what the preview listed, so
    // the cascade is opted into exactly when something was listed. With nothing
    // listed the flag stays false and the command behaves as it always has.
    const bool cascade_references = !state_.terminology.pending_delete_term_references.empty();
    core::commands::DeleteTerminologyTermCommand command(
        state_.terminology.selected_package_ref, state_.terminology.selected_term_ref, cascade_references);
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Term delete failed: " + outcome.error);
        return false;
    }
    const std::size_t also_removed = command.RemovedIds().size() > 1 ? command.RemovedIds().size() - 1 : 0;

    state_.terminology.selected_term_ref = core::TerminologyTermRef{};
    state_.terminology.pending_delete_term_references.clear();
    state_.terminology.pending_delete_term_blockers.clear();
    state_.terminology.pending_delete_term_preview_available = false;
    state_.terminology.show_delete_term_modal = false;
    state_.events.Emit(DocumentDirtyEvent{});
    if (RefreshVisibleTerminologyContextProjection(state_.app_state))
        state_.events.Emit(TreeDirtyEvent{});
    // Say what actually went. "Deleted term." after a cascade would understate
    // it, and the count is the one thing a user cannot re-check afterwards.
    SetStatus(state_,
              also_removed == 0 ? std::string("Deleted term.")
                                : ui::i18n::trnf("Deleted term and {0} element that referenced it.",
                                                 "Deleted term and {0} elements that referenced it.",
                                                 static_cast<int>(also_removed),
                                                 static_cast<int>(also_removed)));
    return true;
}

void TerminologyActions::SelectCategory(const core::TerminologyCategoryRef& category_ref) {
    state_.terminology.selected_category_ref = category_ref;
}

void TerminologyActions::SetCategoryFilter(const std::string& category_filter) {
    CopyToBuffer(
        state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), category_filter);
}

void TerminologyActions::BeginAddCategory() {
    if (state_.WorkingPackage() == nullptr) {
        SetStatus(state_, "Open a terminology package before adding categories.");
        return;
    }
    if (DraftTakesGlossaryEdits() && DraftCreateTargetRefused(state_.terminology.selected_package_ref))
        return;
    ClearCategoryEditorBuffers(state_);
    state_.terminology.editing_existing_category = false;
    state_.terminology.show_category_editor_modal = true;
}

bool TerminologyActions::BeginEditCategory(const core::TerminologyCategoryRef& category_ref) {
    const sacm::Category* category = WorkingCategory(state_.terminology.selected_package_ref, category_ref);
    if (!category) {
        SetStatus(state_, "Category not found.");
        return false;
    }

    state_.terminology.selected_category_ref = category_ref;
    CopyCategoryToEditor(state_, *category);
    state_.terminology.editing_existing_category = true;
    state_.terminology.show_category_editor_modal = true;
    return true;
}

bool TerminologyActions::ConfirmCategoryEditInDraft(const core::TerminologyCategoryDraft& draft) {
    std::vector<core::reviews::PatchOperation> operations;
    const bool editing = state_.terminology.editing_existing_category;
    if (editing) {
        const sacm::Category* current =
            WorkingCategory(state_.terminology.selected_package_ref, state_.terminology.selected_category_ref);
        if (current == nullptr) {
            SetStatus(state_, "Category not found.");
            return false;
        }
        const core::reviews::ElementRef ref = ExistingRef(current->id);
        if (draft.name != current->name)
            operations.push_back(UpdateCategoryField(ref, core::reviews::kCategoryFieldName, draft.name));
        if (draft.description != current->description)
            operations.push_back(UpdateCategoryField(ref, core::reviews::kCategoryFieldDescription, draft.description));
        if (operations.empty()) {
            state_.terminology.show_category_editor_modal = false;
            return true;
        }
    } else {
        if (DraftCreateTargetRefused(state_.terminology.selected_package_ref))
            return false;
        operations.push_back(CreateCategoryOperation(kCreatedCategoryRef, draft));
    }

    const app::commands::DraftEditOutcome outcome = app::commands::DispatchDraftDocumentEdit(state_, operations);
    if (!outcome.success) {
        SetStatus(state_, (editing ? "Category update failed: " : "Category create failed: ") + outcome.error);
        return false;
    }
    if (!editing) {
        const auto created = outcome.created_ids.find(kCreatedCategoryRef);
        state_.terminology.selected_category_ref =
            core::TerminologyCategoryRef{created != outcome.created_ids.end() ? created->second : std::string{}, {}};
    }
    state_.terminology.show_category_editor_modal = false;
    SetStatus(state_,
              editing ? ui::i18n::trf("Updated category {0} in the working draft.", draft.name)
                      : ui::i18n::trf("Added category {0} to the working draft.", draft.name));
    return true;
}

void TerminologyActions::ConfirmCategoryEdit() {
    if (state_.WorkingPackage() == nullptr)
        return;

    const core::TerminologyCategoryDraft draft = CategoryDraftFromEditor(state_);
    if (DraftTakesGlossaryEdits()) {
        ConfirmCategoryEditInDraft(draft);
        return;
    }
    if (state_.terminology.editing_existing_category) {
        core::commands::UpdateTerminologyCategoryCommand command(
            state_.terminology.selected_package_ref, state_.terminology.selected_category_ref, draft);
        const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
        if (!outcome.success) {
            SetStatus(state_, "Category update failed: " + outcome.error);
            return;
        }
        SetStatus(state_, "Updated category " + draft.name + ".");
    } else {
        core::commands::CreateTerminologyCategoryCommand command(state_.terminology.selected_package_ref, draft);
        const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
        if (!outcome.success) {
            SetStatus(state_, "Category create failed: " + outcome.error);
            return;
        }
        state_.terminology.selected_category_ref = command.GeneratedRef();
        SetStatus(state_, "Added category " + draft.name + ".");
    }

    state_.events.Emit(DocumentDirtyEvent{});
    state_.terminology.show_category_editor_modal = false;
}

void TerminologyActions::BeginDeleteCategory(const core::TerminologyCategoryRef& category_ref) {
    if (AcceptedEditRefused(AF_TR("Deleting a category")))
        return;
    if (!state_.app_state.has_projected_package())
        return;
    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.projected_package(), state_.terminology.selected_package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return;
    }
    const sacm::Category* category = core::FindTerminologyCategory(*terminology_package, category_ref);
    if (!category) {
        SetStatus(state_, "Category not found.");
        return;
    }

    state_.terminology.selected_category_ref = category_ref;
    state_.terminology.pending_delete_category_term_count =
        core::CountTermsUsingCategory(*terminology_package, category_ref);
    state_.terminology.show_delete_category_modal = true;
}

void TerminologyActions::ConfirmDeleteCategory() {
    if (AcceptedEditRefused(AF_TR("Deleting a category")))
        return;
    if (!state_.app_state.has_projected_package())
        return;

    core::commands::DeleteTerminologyCategoryCommand command(state_.terminology.selected_package_ref,
                                                             state_.terminology.selected_category_ref);
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Category delete failed: " + outcome.error);
        return;
    }

    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.show_delete_category_modal = false;
    state_.events.Emit(DocumentDirtyEvent{});
    SetStatus(state_, "Deleted category.");
}

void TerminologyActions::SeedRecommendedCategoriesInDraft(const sacm::TerminologyPackage& terminology_package,
                                                          const std::vector<std::string>& missing_names) {
    if (missing_names.empty()) {
        SetStatus(state_, "Recommended terminology categories already exist.");
        return;
    }
    if (DraftCreateTargetRefused(detail::TerminologyPackageRefFor(terminology_package)))
        return;
    // One batch, so the glossary gains all of them or none: the same
    // all-or-nothing an MCP client's batch gets.
    std::vector<core::reviews::PatchOperation> operations;
    for (std::size_t index = 0; index < missing_names.size(); ++index) {
        core::TerminologyCategoryDraft draft;
        draft.name = missing_names[index];
        operations.push_back(CreateCategoryOperation("$category" + std::to_string(index), draft));
    }
    const app::commands::DraftEditOutcome outcome = app::commands::DispatchDraftDocumentEdit(state_, operations);
    if (!outcome.success) {
        SetStatus(state_, "Category create failed: " + outcome.error);
        return;
    }
    SetStatus(state_, AF_TR("Added recommended terminology categories to the working draft."));
}

void TerminologyActions::SeedRecommendedCategories() {
    const sacm::TerminologyPackage* terminology_package =
        WorkingTerminologyPackage(state_.terminology.selected_package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return;
    }

    const char* recommended[] = {"Operational Context",
                                 "System",
                                 "Hazard / Risk",
                                 "Evidence",
                                 "Requirement",
                                 "Standard",
                                 "Project Specific",
                                 "Deprecated"};
    if (DraftTakesGlossaryEdits()) {
        std::vector<std::string> missing_names;
        for (const char* name : recommended) {
            if (!CategoryNameExists(*terminology_package, name))
                missing_names.push_back(name);
        }
        SeedRecommendedCategoriesInDraft(*terminology_package, missing_names);
        return;
    }
    int added = 0;
    for (const char* name : recommended) {
        if (CategoryNameExists(*terminology_package, name))
            continue;
        core::TerminologyCategoryDraft draft;
        draft.name = name;
        core::commands::CreateTerminologyCategoryCommand command(state_.terminology.selected_package_ref, draft);
        const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
        if (outcome.success)
            ++added;
    }

    if (added > 0) {
        state_.events.Emit(DocumentDirtyEvent{});
        SetStatus(state_, "Added recommended terminology categories.");
    } else {
        SetStatus(state_, "Recommended terminology categories already exist.");
    }
}

void TerminologyActions::BeginFindUsages(const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref) {
    state_.terminology.usages_active = true;
    state_.terminology.focus_usages_tab = true;
    state_.terminology.usage_search_package_ref = package_ref;
    state_.terminology.usage_search_term_ref = term_ref;
    state_.terminology.usage_search_term_value.clear();
    state_.terminology.usage_search_term_name.clear();
    state_.terminology.usage_search_message.clear();
    state_.terminology.usage_search_error.clear();
    state_.terminology.usage_results.clear();
    state_.terminology.selected_usage_index = -1;

    if (!state_.app_state.has_projected_package()) {
        state_.terminology.usage_search_error = "Open a SACM model before finding terminology usages.";
        SetStatus(state_, state_.terminology.usage_search_error);
        return;
    }

    core::TerminologyTermUsageSearchResult result =
        core::FindTerminologyTermUsages(state_.app_state.projected_package(), package_ref, term_ref);
    state_.terminology.usage_search_term_value = result.term_value;
    state_.terminology.usage_search_term_name = result.term_name;
    if (!result.success) {
        state_.terminology.usage_search_error = result.error;
        SetStatus(state_, "Find usages failed: " + result.error);
        return;
    }

    state_.terminology.usage_results = std::move(result.usages);
    if (!state_.terminology.usage_results.empty())
        state_.terminology.selected_usage_index = 0;
    const int usage_count = static_cast<int>(state_.terminology.usage_results.size());
    const std::string label =
        state_.terminology.usage_search_term_value.empty() ? "term" : state_.terminology.usage_search_term_value;
    SetStatus(state_,
              "Found " + std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " of " + label + ".");
}

void TerminologyActions::NavigateToUsage(std::size_t usage_index) {
    if (usage_index >= state_.terminology.usage_results.size())
        return;
    state_.terminology.selected_usage_index = static_cast<int>(usage_index);
    const core::TerminologyTermUsage& usage = state_.terminology.usage_results[usage_index];
    if (usage.element_id.empty()) {
        SetStatus(state_, "The selected usage has no navigable element id.");
        return;
    }
    state_.workbench.show_gsn_tab = true;
    state_.events.Emit(SelectionChangedEvent{usage.element_id, true});
    state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
}

void TerminologyActions::ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    (void)element_id;
    (void)term_value;
    state_.events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, "Change linked terminology meaning"});
}

void TerminologyActions::BeginQuickDefineTerm(const std::string& element_id, const std::string& term_value) {
    const sacm::AssuranceCasePackage* working_package = state_.WorkingPackage();
    if (working_package == nullptr) {
        SetStatus(state_, "Open a SACM model before defining terms.");
        return;
    }

    QuickDefineTargetPackageResult target;
    if (DraftTakesGlossaryEdits()) {
        // The draft's glossary, or none: a draft with no glossary grows its
        // first one when the term is created, the way it does for an MCP
        // client, rather than creating one in the accepted document now.
        target.package_ref = detail::ResolveQuickDefineTargetPackageIn(state_, *working_package, element_id);
        if (DraftCreateTargetRefused(target.package_ref))
            return;
    } else {
        target = EnsureQuickDefineTargetPackage(state_, element_id);
    }
    if (!HasTerminologyPackageRef(target.package_ref) && !DraftTakesGlossaryEdits()) {
        SetStatus(state_,
                  target.error.empty() ? "Could not create a TerminologyPackage for the new term."
                                       : "Terminology package create failed: " + target.error);
        return;
    }
    if (target.created) {
        state_.events.Emit(TreeDirtyEvent{});
        state_.events.Emit(DocumentDirtyEvent{});
        if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
            const std::filesystem::path relative = std::filesystem::relative(
                state_.app_state.active_project_file_path, state_.app_state.current_project->rootPath);
            InvalidateSacmPackageTreeCache(state_, relative);
        }
        SetStatus(state_, "Created a TerminologyPackage for new terms.");
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    ClearTermEditorBuffers(state_);
    CopyToBuffer(state_.terminology.term_value_buf, sizeof(state_.terminology.term_value_buf), trimmed_term);
    state_.terminology.quick_define_element_id = element_id;
    state_.terminology.quick_define_source_text = trimmed_term;
    state_.terminology.quick_define_target_package_ref = target.package_ref;
    state_.terminology.show_quick_define_term_modal = true;
}

void TerminologyActions::BeginLinkExistingTerm(const std::string& element_id, const std::string& term_value) {
    if (const sacm::AssuranceCasePackage* working_package = state_.WorkingPackage()) {
        const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(state_, element_id);
        if (HasTerminologyPackageRef(target_package_ref)) {
            state_.terminology.selected_package_ref = target_package_ref;
            if (const sacm::TerminologyPackage* package =
                    core::FindTerminologyPackage(*working_package, target_package_ref)) {
                CopyTerminologyPackageToEditor(state_, *package);
            }
        }
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), trimmed_term);
    state_.workbench.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.workbench.force_center_tab_selection = true;
    SetStatus(state_,
              "Filtered the glossary for " + trimmed_term +
                  ". Select a term to link when occurrence binding is available.");
}

bool TerminologyActions::ConfirmQuickDefineTermInDraft(bool add_as_context) {
    if (DraftCreateTargetRefused(state_.terminology.quick_define_target_package_ref))
        return false;
    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    std::vector<core::reviews::PatchOperation> operations;
    AppendCreateTerm(operations, kCreatedTermRef, draft);
    const app::commands::DraftEditOutcome outcome = app::commands::DispatchDraftDocumentEdit(state_, operations);
    if (!outcome.success) {
        SetStatus(state_, "Term create failed: " + outcome.error);
        return false;
    }
    const auto created = outcome.created_ids.find(kCreatedTermRef);
    const core::TerminologyTermRef new_term_ref{created != outcome.created_ids.end() ? created->second : std::string{},
                                                {}};

    // The glossary the term went to, which may be one the draft has just
    // created and the accepted case has never had.
    core::TerminologyPackageRef package_ref;
    package_ref.id = sacm_adapter::resolve_terminology_package_id(*state_.draft_document.document());
    state_.terminology.selected_package_ref = package_ref;
    state_.terminology.selected_term_ref = new_term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), draft.value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package = WorkingTerminologyPackage(package_ref))
        CopyTerminologyPackageToEditor(state_, *package);

    state_.terminology.show_quick_define_term_modal = false;
    state_.terminology.quick_define_element_id.clear();
    state_.terminology.quick_define_source_text.clear();
    // Linking the term to the element as context writes to the accepted
    // argument (the draft's vocabulary has no operation for it), so the term is
    // defined and the link is what is left to do once the draft is accepted.
    SetStatus(state_,
              add_as_context ? ui::i18n::trf("Added term {0} to the working draft. Adding it as context changes the "
                                             "accepted argument, so do that once the draft is accepted.",
                                             draft.value)
                             : ui::i18n::trf("Added term {0} to the working draft.", draft.value));
    return true;
}

bool TerminologyActions::ConfirmQuickDefineTerm(bool add_as_context) {
    if (state_.WorkingPackage() == nullptr)
        return false;
    if (DraftTakesGlossaryEdits())
        return ConfirmQuickDefineTermInDraft(add_as_context);

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    core::commands::CreateTerminologyTermCommand command(state_.terminology.quick_define_target_package_ref, draft);
    const auto outcome = app::commands::DispatchAuditedCommand(state_, command);
    if (!outcome.success) {
        SetStatus(state_, "Term create failed: " + outcome.error);
        return false;
    }
    const core::TerminologyTermRef new_term_ref = command.GeneratedRef();

    state_.terminology.selected_package_ref = state_.terminology.quick_define_target_package_ref;
    state_.terminology.selected_term_ref = new_term_ref;
    state_.terminology.selected_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology.filter_buf, sizeof(state_.terminology.filter_buf), draft.value);
    CopyToBuffer(state_.terminology.category_filter_buf, sizeof(state_.terminology.category_filter_buf), "");
    state_.terminology.selected_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package = core::FindTerminologyPackage(
            state_.app_state.projected_package(), state_.terminology.selected_package_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }

    state_.events.Emit(TreeDirtyEvent{});
    state_.events.Emit(DocumentDirtyEvent{});
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    const std::string context_element_id = state_.terminology.quick_define_element_id;
    if (add_as_context)
        AddVisibleTermContextFromCanvas(
            context_element_id, state_.terminology.quick_define_target_package_ref, new_term_ref);

    state_.terminology.show_quick_define_term_modal = false;
    state_.terminology.quick_define_element_id.clear();
    state_.terminology.quick_define_source_text.clear();
    if (!add_as_context)
        SetStatus(state_, "Added term " + draft.value + ".");
    return true;
}

void TerminologyActions::HandleProblemQuickFix(const core::ProblemItem& problem) {
    if (problem.type == "TerminologyUndefinedAcronym") {
        BeginQuickDefineTerm(problem.element_id, problem.quick_fix_payload);
        return;
    }
    if (problem.type == "TerminologyAmbiguity") {
        BeginLinkExistingTerm(problem.element_id, problem.quick_fix_payload);
        return;
    }
    if (core::StartsWith(problem.type, "TerminologyTerm")) {
        TerminologyTermQuickFixPayload payload;
        if (!DecodeTerminologyTermQuickFixPayload(problem.quick_fix_payload, payload)) {
            SetStatus(state_, "Could not decode terminology quick fix target.");
            return;
        }
        if (!OpenTerminologyProblemTerm(state_, payload.package_ref, payload.term_ref, payload.term_value)) {
            SetStatus(state_, "Terminology quick fix target was not found.");
            return;
        }
        if (problem.type == "TerminologyTermDuplicateDefinition") {
            SetStatus(state_, "Filtered glossary to duplicated term definition " + payload.term_value + ".");
            return;
        }
        BeginEditTerm(payload.term_ref);
        SetStatus(state_, "Opened terminology term editor.");
        return;
    }
    if (!problem.element_id.empty()) {
        state_.events.Emit(SelectionChangedEvent{problem.element_id, true});
        state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
    }
}

void TerminologyActions::IgnoreSuggestion(const std::string& element_id, const std::string& term_value) {
    const std::string trimmed_term = TrimWhitespace(term_value);
    state_.terminology.ignored_suggestion_keys.insert(TerminologySuggestionKey(element_id, trimmed_term));
    const auto save_result = detail::SaveIgnoredSuggestions(state_);
    if (!save_result.success) {
        SetStatus(state_,
                  "Ignored terminology suggestion " + trimmed_term +
                      " for this session, but could not persist: " + save_result.error);
        return;
    }
    SetStatus(state_, "Ignored terminology suggestion " + trimmed_term + ".");
}

bool TerminologyActions::IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    const std::string key = TerminologySuggestionKey(element_id, TrimWhitespace(term_value));
    return state_.terminology.ignored_suggestion_keys.count(key) > 0;
}

void TerminologyActions::RestoreSuggestion(const std::string& element_id, const std::string& term_value) {
    const std::string trimmed_term = TrimWhitespace(term_value);
    if (state_.terminology.ignored_suggestion_keys.erase(TerminologySuggestionKey(element_id, trimmed_term)) == 0)
        return;
    const auto save_result = detail::SaveIgnoredSuggestions(state_);
    if (!save_result.success) {
        SetStatus(state_,
                  "Restored terminology suggestion " + trimmed_term +
                      " for this session, but could not persist: " + save_result.error);
        return;
    }
    SetStatus(state_, "Restored terminology suggestion " + trimmed_term + ".");
}

std::vector<IgnoredSuggestionView> TerminologyActions::ListIgnoredSuggestions() const {
    std::vector<IgnoredSuggestionView> views;
    views.reserve(state_.terminology.ignored_suggestion_keys.size());
    for (const std::string& key : state_.terminology.ignored_suggestion_keys) {
        IgnoredSuggestionView view;
        const std::size_t separator = key.find('\n');
        if (separator == std::string::npos) {
            view.term = key;
        } else {
            view.element_id = key.substr(0, separator);
            view.term = key.substr(separator + 1);
        }
        views.push_back(std::move(view));
    }
    std::sort(views.begin(), views.end(), [](const IgnoredSuggestionView& a, const IgnoredSuggestionView& b) {
        if (a.term != b.term)
            return a.term < b.term;
        return a.element_id < b.element_id;
    });
    return views;
}

void TerminologyActions::LoadIgnoredSuggestions() {
    state_.terminology.ignored_suggestion_keys.clear();
    const std::filesystem::path path = detail::IgnoredTerminologyFilePath(state_);
    if (path.empty())
        return;
    std::error_code ec;
    const bool file_exists = std::filesystem::exists(path, ec);
    if (ec) {
        SetStatus(state_, "Ignored terminology list could not be loaded: " + ec.message());
        return;
    }
    if (!file_exists)
        return;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SetStatus(state_, "Ignored terminology list could not be loaded: could not open file.");
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    std::vector<core::terminology::IgnoredSuggestion> items;
    std::string error;
    if (!core::terminology::ParseIgnoredSuggestions(buffer.str(), items, error)) {
        SetStatus(state_, "Ignored terminology list could not be loaded: " + error);
        return;
    }
    for (const core::terminology::IgnoredSuggestion& item : items)
        state_.terminology.ignored_suggestion_keys.insert(TerminologySuggestionKey(item.element_id, item.term));
}

} // namespace app::actions
