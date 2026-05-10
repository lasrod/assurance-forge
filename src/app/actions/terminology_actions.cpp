#include "app/actions/terminology_actions.h"

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "parser/xml_parser.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace app {
namespace {

void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

void CopyToBuffer(char* buffer, size_t buffer_size, const std::string& value) {
    if (!buffer || buffer_size == 0)
        return;
    const size_t count = std::min(buffer_size - 1, value.size());
    std::memcpy(buffer, value.data(), count);
    buffer[count] = '\0';
}

std::string TrimWhitespace(const std::string& value) {
    auto begin = value.begin();
    while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    auto end = value.end();
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}

std::string TerminologySuggestionKey(const std::string& element_id, const std::string& term_value) {
    return element_id + "\n" + term_value;
}

std::string JoinCategoryRefs(const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += ref;
    }
    return result;
}

void CopyTerminologyPackageToEditor(AppRuntimeState& state, const sacm::TerminologyPackage& package) {
    CopyToBuffer(state.terminology_package_name_buf, sizeof(state.terminology_package_name_buf), package.name);
    CopyToBuffer(state.terminology_package_description_buf,
                 sizeof(state.terminology_package_description_buf),
                 package.description);
}

void ClearTermEditorBuffers(AppRuntimeState& state) {
    CopyToBuffer(state.term_value_buf, sizeof(state.term_value_buf), "");
    CopyToBuffer(state.term_name_buf, sizeof(state.term_name_buf), "");
    CopyToBuffer(state.term_definition_buf, sizeof(state.term_definition_buf), "");
    CopyToBuffer(state.term_categories_buf, sizeof(state.term_categories_buf), "");
    CopyToBuffer(state.term_external_reference_buf, sizeof(state.term_external_reference_buf), "");
    CopyToBuffer(state.term_origin_buf, sizeof(state.term_origin_buf), "");
}

void CopyTermToEditor(AppRuntimeState& state, const sacm::Term& term) {
    CopyToBuffer(state.term_value_buf, sizeof(state.term_value_buf), term.value);
    CopyToBuffer(state.term_name_buf, sizeof(state.term_name_buf), term.name);
    CopyToBuffer(state.term_definition_buf, sizeof(state.term_definition_buf), term.description);
    CopyToBuffer(state.term_categories_buf, sizeof(state.term_categories_buf), JoinCategoryRefs(term.category_refs));
    CopyToBuffer(state.term_external_reference_buf, sizeof(state.term_external_reference_buf), term.externalReference);
    CopyToBuffer(state.term_origin_buf, sizeof(state.term_origin_buf), term.origin);
}

std::vector<std::string> SplitCategoryRefs(const std::string& raw) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::stringstream stream(normalized);
    std::vector<std::string> refs;
    std::string item;
    while (stream >> item) {
        item = TrimWhitespace(item);
        if (!item.empty() && std::find(refs.begin(), refs.end(), item) == refs.end())
            refs.push_back(item);
    }
    return refs;
}

core::TerminologyTermDraft TermDraftFromEditor(const AppRuntimeState& state) {
    core::TerminologyTermDraft draft;
    draft.value = TrimWhitespace(state.term_value_buf);
    draft.name = TrimWhitespace(state.term_name_buf);
    draft.description = TrimWhitespace(state.term_definition_buf);
    draft.category_refs = SplitCategoryRefs(state.term_categories_buf);
    draft.externalReference = TrimWhitespace(state.term_external_reference_buf);
    draft.origin = TrimWhitespace(state.term_origin_buf);
    return draft;
}

bool HasTerminologyPackageRef(const core::TerminologyPackageRef& package_ref) {
    return !package_ref.id.empty() || !package_ref.gid.empty();
}

bool TerminologyPackageMatchesRef(const sacm::TerminologyPackage& package,
                                  const core::TerminologyPackageRef& package_ref) {
    return (!package_ref.id.empty() && package.id == package_ref.id) ||
           (!package_ref.gid.empty() && package.gid == package_ref.gid);
}

core::TerminologyPackageRef TerminologyPackageRefFor(const sacm::TerminologyPackage& package) {
    return core::TerminologyPackageRef{package.id, package.gid};
}

bool ArgumentPackageContainsElement(const sacm::ArgumentPackage& argument_package, const std::string& element_id) {
    if (element_id.empty())
        return false;
    for (const auto& claim : argument_package.claims) {
        if (claim.id == element_id || claim.gid == element_id)
            return true;
    }
    for (const auto& reasoning : argument_package.argumentReasonings) {
        if (reasoning.id == element_id || reasoning.gid == element_id)
            return true;
    }
    for (const auto& artifact_reference : argument_package.artifactReferences) {
        if (artifact_reference.id == element_id || artifact_reference.gid == element_id)
            return true;
    }
    return false;
}

const sacm::ArgumentPackage* FindContainingArgumentPackage(const sacm::AssuranceCasePackage& package,
                                                           const std::string& element_id) {
    for (const auto& argument_package : package.argumentPackages) {
        if (ArgumentPackageContainsElement(argument_package, element_id))
            return &argument_package;
    }
    return nullptr;
}

bool IsAssuranceCaseTerminologyPackage(const sacm::AssuranceCasePackage& package,
                                       const core::TerminologyPackageRef& package_ref) {
    for (const auto& terminology_package : package.terminologyPackages) {
        if (TerminologyPackageMatchesRef(terminology_package, package_ref))
            return true;
    }
    return false;
}

bool IsArgumentTerminologyPackage(const sacm::ArgumentPackage& argument_package,
                                  const core::TerminologyPackageRef& package_ref) {
    for (const auto& terminology_package : argument_package.terminologyPackages) {
        if (TerminologyPackageMatchesRef(terminology_package, package_ref))
            return true;
    }
    return false;
}

core::TerminologyPackageRef ResolveQuickDefineTargetPackage(const AppRuntimeState& state,
                                                            const std::string& element_id) {
    if (!state.app_state.sacm_package.has_value())
        return {};

    const sacm::AssuranceCasePackage& package = state.app_state.sacm_package.value();
    const sacm::ArgumentPackage* containing_argument_package = FindContainingArgumentPackage(package, element_id);
    if (HasTerminologyPackageRef(state.selected_terminology_package_ref) &&
        core::FindTerminologyPackage(package, state.selected_terminology_package_ref)) {
        if (IsAssuranceCaseTerminologyPackage(package, state.selected_terminology_package_ref) ||
            (containing_argument_package &&
             IsArgumentTerminologyPackage(*containing_argument_package, state.selected_terminology_package_ref))) {
            return state.selected_terminology_package_ref;
        }
    }

    if (containing_argument_package && !containing_argument_package->terminologyPackages.empty())
        return TerminologyPackageRefFor(containing_argument_package->terminologyPackages.front());
    if (!package.terminologyPackages.empty())
        return TerminologyPackageRefFor(package.terminologyPackages.front());
    return {};
}

void MarkTerminologyDocumentDirty(AppRuntimeState& state) {
    state.app_state.mark_dirty();
    state.document_dirty = true;
}

void InvalidateSacmPackageTreeCache(AppRuntimeState& state, const std::filesystem::path& relative_path) {
    state.sacm_package_tree_cache.erase(relative_path.generic_string());
}

const sacm::ArtifactReference* FindArtifactReferenceById(const sacm::AssuranceCasePackage& package,
                                                         const std::string& artifact_reference_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (artifact_reference.id == artifact_reference_id || artifact_reference.gid == artifact_reference_id)
                return &artifact_reference;
        }
    }
    return nullptr;
}

const sacm::AssertedContext* FindAssertedContextById(const sacm::AssuranceCasePackage& package,
                                                     const std::string& asserted_context_id) {
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::AssertedContext& context : argument_package.assertedContexts) {
            if (context.id == asserted_context_id || context.gid == asserted_context_id)
                return &context;
        }
    }
    return nullptr;
}

bool ParserModelHasElement(const parser::AssuranceCase& model, const std::string& id, const std::string& gid) {
    return std::any_of(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return (!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid);
    });
}

parser::SacmElement* FindParserElement(parser::AssuranceCase& model, const std::string& id, const std::string& gid) {
    for (parser::SacmElement& element : model.elements) {
        if ((!id.empty() && element.id == id) || (!gid.empty() && element.gid == gid))
            return &element;
    }
    return nullptr;
}

std::string TermContextDisplayLabel(const sacm::Term& term) {
    if (term.value.empty())
        return term.name.empty() ? term.id : term.name;
    if (term.name.empty() || term.name == term.value)
        return term.value;
    return term.value + ": " + term.name;
}

bool RefreshVisibleTerminologyContextProjection(core::AppState& app_state) {
    if (!app_state.loaded_case.has_value() || !app_state.sacm_package.has_value())
        return false;

    bool changed = false;
    parser::AssuranceCase& model = app_state.loaded_case.value();
    const sacm::AssuranceCasePackage& package = app_state.sacm_package.value();
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& artifact_reference : argument_package.artifactReferences) {
            if (!core::IsVisibleTerminologyArtifactReference(package, argument_package, artifact_reference))
                continue;
            parser::SacmElement* element = FindParserElement(model, artifact_reference.id, artifact_reference.gid);
            if (!element)
                continue;
            const core::TerminologyTermReferenceResolution resolution =
                core::ResolveTerminologyTermReference(package, artifact_reference.referencedArtifact);
            const std::string previous_name = element->name;
            const std::string previous_description = element->description;
            if (!resolution.resolved || !resolution.term) {
                element->description.clear();
                element->description_langs.clear();
            } else {
                element->name = TermContextDisplayLabel(*resolution.term);
                element->name_langs = resolution.term->name_ml.texts;
                if (element->name_langs.empty() && !element->name.empty())
                    element->name_langs["en"] = element->name;
                element->description = resolution.term->description;
                element->description_langs = resolution.term->description_ml.texts;
                if (element->description_langs.empty() && !element->description.empty())
                    element->description_langs["en"] = element->description;
            }
            changed = changed || element->name != previous_name || element->description != previous_description;
        }
    }
    return changed;
}

bool SyncVisibleTerminologyContextToParser(core::AppState& app_state,
                                           const core::TerminologyContextAssociationResult& result) {
    if (!app_state.loaded_case.has_value() || !app_state.sacm_package.has_value())
        return false;

    const sacm::ArtifactReference* artifact_reference =
        FindArtifactReferenceById(app_state.sacm_package.value(), result.artifact_reference_id);
    const sacm::AssertedContext* context =
        FindAssertedContextById(app_state.sacm_package.value(), result.asserted_context_id);
    if (!artifact_reference || !context || !core::IsVisibleTerminologyContext(*context))
        return false;

    parser::AssuranceCase& model = app_state.loaded_case.value();
    bool changed = false;
    if (!ParserModelHasElement(model, artifact_reference->id, artifact_reference->gid)) {
        parser::SacmElement element;
        element.id = artifact_reference->id;
        element.gid = artifact_reference->gid;
        element.name = artifact_reference->name;
        element.type = "artifactreference";
        element.description = artifact_reference->description;
        element.name_langs = artifact_reference->name_ml.texts;
        element.description_langs = artifact_reference->description_ml.texts;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    if (!ParserModelHasElement(model, context->id, context->gid)) {
        parser::SacmElement element;
        element.id = context->id;
        element.gid = context->gid;
        element.name = context->name;
        element.type = "assertedcontext";
        element.description = context->description;
        element.name_langs = context->name_ml.texts;
        element.description_langs = context->description_ml.texts;
        element.source_refs = context->sources;
        element.target_refs = context->targets;
        element.assertion_declaration = context->assertionDeclaration;
        model.elements.push_back(std::move(element));
        changed = true;
    }
    return RefreshVisibleTerminologyContextProjection(app_state) || changed;
}

std::string TermStatusLabel(const sacm::AssuranceCasePackage& package,
                            const core::TerminologyPackageRef& package_ref,
                            const core::TerminologyTermRef& term_ref) {
    const sacm::Term* term = core::FindTerminologyTerm(package, package_ref, term_ref);
    if (!term)
        return "Term";
    if (!term->value.empty())
        return term->value;
    if (!term->name.empty())
        return term->name;
    return term->id.empty() ? "Term" : term->id;
}

} // namespace

TerminologyActions::TerminologyActions(AppRuntimeState& state) : state_(state) {}

bool TerminologyActions::OpenTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before opening terminology terms.");
        return false;
    }

    const sacm::TerminologyPackage* terminology_package =
        core::FindTerminologyPackage(state_.app_state.sacm_package.value(), package_ref);
    if (!terminology_package) {
        SetStatus(state_, "Terminology package not found.");
        return false;
    }
    const sacm::Term* term = core::FindTerminologyTerm(*terminology_package, term_ref);
    if (!term) {
        SetStatus(state_, "Term not found.");
        return false;
    }

    state_.selected_terminology_package_ref = package_ref;
    state_.selected_terminology_term_ref = term_ref;
    state_.selected_terminology_category_ref = core::TerminologyCategoryRef{};
    state_.selected_terminology_package_file_path = state_.app_state.active_project_file_path;
    CopyTerminologyPackageToEditor(state_, *terminology_package);
    CopyToBuffer(state_.terminology_filter_buf, sizeof(state_.terminology_filter_buf), term->value);
    CopyToBuffer(state_.terminology_category_filter_buf, sizeof(state_.terminology_category_filter_buf), "");
    state_.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.force_center_tab_selection = true;
    SetStatus(state_, "Opened term " + term->value + ".");
    return true;
}

bool TerminologyActions::EditTermFromCanvas(const core::TerminologyPackageRef& package_ref,
                                            const core::TerminologyTermRef& term_ref) {
    if (!OpenTermFromCanvas(package_ref, term_ref))
        return false;
    if (!state_.app_state.sacm_package.has_value())
        return false;
    const sacm::Term* term = core::FindTerminologyTerm(state_.app_state.sacm_package.value(), package_ref, term_ref);
    if (!term)
        return false;
    state_.selected_terminology_term_ref = term_ref;
    CopyTermToEditor(state_, *term);
    state_.editing_existing_terminology_term = true;
    state_.show_terminology_term_editor_modal = true;
    return true;
}

bool TerminologyActions::AddTermAsContextFromCanvas(const std::string& element_id,
                                                    const core::TerminologyPackageRef& package_ref,
                                                    const core::TerminologyTermRef& term_ref) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before associating terminology.");
        return false;
    }

    core::TerminologyContextAssociationResult result = core::AssociateTerminologyTermWithElement(
        state_.app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus(state_, "Could not associate term with element: " + result.error);
        return false;
    }

    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(state_);
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
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before adding terminology context.");
        return false;
    }

    const std::string term_label = TermStatusLabel(state_.app_state.sacm_package.value(), package_ref, term_ref);
    core::TerminologyContextAssociationResult result = core::AddTerminologyTermAsVisibleContext(
        state_.app_state.sacm_package.value(), element_id, package_ref, term_ref);
    if (!result.success) {
        SetStatus(state_, "Could not add term as context: " + result.error);
        return false;
    }

    const bool parser_changed = SyncVisibleTerminologyContextToParser(state_.app_state, result);
    if (!result.already_associated) {
        MarkTerminologyDocumentDirty(state_);
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

void TerminologyActions::BeginFindUsages(const core::TerminologyPackageRef& package_ref,
                                         const core::TerminologyTermRef& term_ref) {
    state_.terminology_usages_active = true;
    state_.focus_terminology_usages_tab = true;
    state_.usage_search_package_ref = package_ref;
    state_.usage_search_term_ref = term_ref;
    state_.usage_search_term_value.clear();
    state_.usage_search_term_name.clear();
    state_.usage_search_message.clear();
    state_.usage_search_error.clear();
    state_.terminology_usage_results.clear();
    state_.selected_terminology_usage_index = -1;

    if (!state_.app_state.sacm_package.has_value()) {
        state_.usage_search_error = "Open a SACM model before finding terminology usages.";
        SetStatus(state_, state_.usage_search_error);
        return;
    }

    core::TerminologyTermUsageSearchResult result =
        core::FindTerminologyTermUsages(state_.app_state.sacm_package.value(), package_ref, term_ref);
    state_.usage_search_term_value = result.term_value;
    state_.usage_search_term_name = result.term_name;
    if (!result.success) {
        state_.usage_search_error = result.error;
        SetStatus(state_, "Find usages failed: " + result.error);
        return;
    }

    state_.terminology_usage_results = std::move(result.usages);
    if (!state_.terminology_usage_results.empty())
        state_.selected_terminology_usage_index = 0;
    const int usage_count = static_cast<int>(state_.terminology_usage_results.size());
    const std::string label = state_.usage_search_term_value.empty() ? "term" : state_.usage_search_term_value;
    SetStatus(state_,
              "Found " + std::to_string(usage_count) + " usage" + (usage_count == 1 ? "" : "s") + " of " + label + ".");
}

void TerminologyActions::NavigateToUsage(std::size_t usage_index) {
    if (usage_index >= state_.terminology_usage_results.size())
        return;
    state_.selected_terminology_usage_index = static_cast<int>(usage_index);
    const core::TerminologyTermUsage& usage = state_.terminology_usage_results[usage_index];
    if (usage.element_id.empty()) {
        SetStatus(state_, "The selected usage has no navigable element id.");
        return;
    }
    state_.show_gsn_tab = true;
    state_.events.Emit(SelectionChangedEvent{usage.element_id, true});
    state_.events.Emit(CenterRequestEvent{CenterViewRequest::GsnCanvas, true, false, true});
}

void TerminologyActions::ChangeMeaningFromCanvas(const std::string& element_id, const std::string& term_value) {
    (void)element_id;
    (void)term_value;
    state_.events.Emit(ModalRequestEvent{ModalKind::NotImplemented, true, "Change linked terminology meaning"});
}

void TerminologyActions::BeginQuickDefineTerm(const std::string& element_id, const std::string& term_value) {
    if (!state_.app_state.sacm_package.has_value()) {
        SetStatus(state_, "Open a SACM model before defining terms.");
        return;
    }

    const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(state_, element_id);
    if (!HasTerminologyPackageRef(target_package_ref)) {
        SetStatus(state_, "Create a TerminologyPackage before defining terms from text.");
        return;
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    ClearTermEditorBuffers(state_);
    CopyToBuffer(state_.term_value_buf, sizeof(state_.term_value_buf), trimmed_term);
    state_.quick_define_element_id = element_id;
    state_.quick_define_source_text = trimmed_term;
    state_.quick_define_target_package_ref = target_package_ref;
    state_.show_quick_define_term_modal = true;
}

void TerminologyActions::BeginLinkExistingTerm(const std::string& element_id, const std::string& term_value) {
    if (state_.app_state.sacm_package.has_value()) {
        const core::TerminologyPackageRef target_package_ref = ResolveQuickDefineTargetPackage(state_, element_id);
        if (HasTerminologyPackageRef(target_package_ref)) {
            state_.selected_terminology_package_ref = target_package_ref;
            if (const sacm::TerminologyPackage* package =
                    core::FindTerminologyPackage(state_.app_state.sacm_package.value(), target_package_ref)) {
                CopyTerminologyPackageToEditor(state_, *package);
            }
        }
    }

    const std::string trimmed_term = TrimWhitespace(term_value);
    CopyToBuffer(state_.terminology_filter_buf, sizeof(state_.terminology_filter_buf), trimmed_term);
    state_.show_terminology_package_tab = true;
    ui::GetUiState().center_view = ui::CenterView::TerminologyPackage;
    state_.force_center_tab_selection = true;
    SetStatus(state_,
              "Filtered the glossary for " + trimmed_term +
                  ". Select a term to link when occurrence binding is available.");
}

bool TerminologyActions::ConfirmQuickDefineTerm(bool add_as_context) {
    if (!state_.app_state.sacm_package.has_value())
        return false;

    const core::TerminologyTermDraft draft = TermDraftFromEditor(state_);
    core::TerminologyTermCreateResult result = core::CreateTerminologyTerm(
        state_.app_state.sacm_package.value(), state_.quick_define_target_package_ref, draft);
    if (!result.success) {
        SetStatus(state_, "Term create failed: " + result.error);
        return false;
    }

    state_.selected_terminology_package_ref = state_.quick_define_target_package_ref;
    state_.selected_terminology_term_ref = result.term_ref;
    state_.selected_terminology_category_ref = core::TerminologyCategoryRef{};
    CopyToBuffer(state_.terminology_filter_buf, sizeof(state_.terminology_filter_buf), draft.value);
    CopyToBuffer(state_.terminology_category_filter_buf, sizeof(state_.terminology_category_filter_buf), "");
    state_.selected_terminology_package_file_path = state_.app_state.active_project_file_path;
    if (const sacm::TerminologyPackage* package = core::FindTerminologyPackage(
            state_.app_state.sacm_package.value(), state_.selected_terminology_package_ref)) {
        CopyTerminologyPackageToEditor(state_, *package);
    }

    MarkTerminologyDocumentDirty(state_);
    state_.events.Emit(TreeDirtyEvent{});
    state_.events.Emit(DocumentDirtyEvent{});
    if (state_.app_state.current_project.has_value() && !state_.app_state.active_project_file_path.empty()) {
        const std::filesystem::path relative = std::filesystem::relative(state_.app_state.active_project_file_path,
                                                                         state_.app_state.current_project->rootPath);
        InvalidateSacmPackageTreeCache(state_, relative);
    }
    const std::string context_element_id = state_.quick_define_element_id;
    if (add_as_context)
        AddVisibleTermContextFromCanvas(context_element_id, state_.quick_define_target_package_ref, result.term_ref);

    state_.show_quick_define_term_modal = false;
    state_.quick_define_element_id.clear();
    state_.quick_define_source_text.clear();
    if (!add_as_context)
        SetStatus(state_, "Added term " + draft.value + ".");
    return true;
}

void TerminologyActions::IgnoreSuggestion(const std::string& element_id, const std::string& term_value) {
    const std::string trimmed_term = TrimWhitespace(term_value);
    state_.ignored_terminology_suggestion_keys.insert(TerminologySuggestionKey(element_id, trimmed_term));
    SetStatus(state_, "Ignored terminology suggestion " + trimmed_term + " for this session.");
}

bool TerminologyActions::IsSuggestionIgnored(const std::string& element_id, const std::string& term_value) const {
    const std::string key = TerminologySuggestionKey(element_id, TrimWhitespace(term_value));
    return state_.ignored_terminology_suggestion_keys.count(key) > 0;
}

} // namespace app
