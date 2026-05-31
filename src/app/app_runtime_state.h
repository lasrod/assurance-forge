#pragma once

#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "ai/http_client.h"
#include "ai/secret_store.h"
#include "app/areas/baseline_modal.h"
#include "app/app_events.h"
#include "app/controllers/ai_review_controller.h"
#include "app/controllers/acp_controller.h"
#include "app/controllers/confidence_controller.h"
#include "app/controllers/element_edit_controller.h"
#include "app/controllers/modal_coordinator.h"
#include "app/controllers/project_controller.h"
#include "app/controllers/proposal_controller.h"
#include "app/controllers/review_controller.h"
#include "app/guideline_catalog.h"
#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/audit/replay_verifier.h"
#include "core/problems/problems_manager.h"
#include "core/terminology_package_service.h"
#include "core/tree_editing.h"
#include "sacm/sacm_package_tree.h"
#include "ui/timeline/timeline_state.h"

namespace core::commands { class CommandBus; }

#include <filesystem>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace app {

struct AiUiState {
    std::shared_ptr<ai::AiSettingsStore> settings_store;
    std::shared_ptr<ai::ISecretStore> secret_store;
    std::shared_ptr<ai::IHttpClient> http_client;
    std::shared_ptr<ai::IAiProvider> provider;
    std::shared_ptr<ai::AiService> service;
    ai::AiTaskRunner task_runner;
    std::shared_ptr<ai::AiTaskHandle> test_task;
    std::unique_ptr<controllers::AiReviewController> review_controller;
    ai::AiProviderSettings settings;
    ai::AiConnectionStatus connection_status;
    bool key_stored = false;
    bool secure_store_available = false;
    char api_key_buf[256] = {};
    char model_buf[128] = {};
};

struct WorkbenchState {
    struct ArgumentPackageCanvasTab {
        std::string key;
        std::string package_id;
        std::string package_gid;
        std::string title;
        std::filesystem::path source_file_path;
        // History slider selection for this canvas tab. std::nullopt = LIVE
        // (renders the current model). A value pins the canvas to the
        // reconstructed state after applying transaction N.
        //
        // Kept alongside `timeline.preview_sequence` during the Phase 1
        // transition; the Timeline widget is the authoritative source of
        // the preview sequence and the controller mirrors its value here
        // so existing consumers (transactions table, etc.) keep working
        // until Phase 2 retires them.
        std::optional<std::uint64_t> selected_transaction_sequence;
        // Always-visible Assurance Timeline rail state for this tab.
        ui::timeline::TimelineState timeline;
        // Per-tab baseline creation modal state.
        app::areas::BaselineModalState baseline_modal;
    };

    bool force_center_tab_selection = false;
    bool pending_focus_root = false;
    bool focus_review_tab = false;
    std::string focus_review_item_id;
    bool focus_history_tab = false;
    // When non-empty, the History panel scopes its transaction list to changes
    // touching this element id. Cleared by the History panel's "Clear filter"
    // control or when the user opens a different element history.
    std::string history_filter_element_id;
    // Free-text author substring filter applied to the History panel
    // (case-insensitive match against AuditTransaction::author). Empty = no
    // author filter.
    std::string history_filter_author;
    // Exact command_name filter applied to the History panel (matches
    // AuditTransaction::command_name). Empty = no command filter.
    std::string history_filter_command;
    bool show_gsn_tab = true;
    bool show_cse_tab = false;
    bool show_evidence_tab = false;
    bool show_package_details_tab = false;
    bool show_terminology_package_tab = false;
    std::vector<ArgumentPackageCanvasTab> argument_package_canvas_tabs;
    std::string active_argument_package_canvas_key;
};

struct TerminologyUiState {
    core::TerminologyPackageRef selected_package_ref;
    std::filesystem::path selected_package_file_path;
    char package_name_buf[256] = {};
    char package_description_buf[2048] = {};
    bool show_create_package_modal = false;
    std::optional<core::ProjectFileEntry> pending_package_parent_entry;
    char new_package_name_buf[256] = {};
    char new_package_description_buf[2048] = {};
    bool show_delete_package_modal = false;
    core::TerminologyTermRef selected_term_ref;
    core::TerminologyCategoryRef selected_category_ref;
    bool usages_active = false;
    bool focus_usages_tab = false;
    core::TerminologyPackageRef usage_search_package_ref;
    core::TerminologyTermRef usage_search_term_ref;
    std::string usage_search_term_value;
    std::string usage_search_term_name;
    std::string usage_search_message;
    std::string usage_search_error;
    std::vector<core::TerminologyTermUsage> usage_results;
    int selected_usage_index = -1;
    char filter_buf[256] = {};
    char category_filter_buf[128] = {};
    bool show_term_editor_modal = false;
    bool editing_existing_term = false;
    bool show_quick_define_term_modal = false;
    std::string quick_define_element_id;
    std::string quick_define_source_text;
    core::TerminologyPackageRef quick_define_target_package_ref;
    std::unordered_set<std::string> ignored_suggestion_keys;
    bool show_delete_term_modal = false;
    int pending_delete_term_usage_count = 0;
    bool show_category_editor_modal = false;
    bool editing_existing_category = false;
    bool show_delete_category_modal = false;
    int pending_delete_category_term_count = 0;
    char term_value_buf[256] = {};
    char term_name_buf[256] = {};
    char term_definition_buf[2048] = {};
    char term_categories_buf[512] = {};
    char term_external_reference_buf[512] = {};
    char term_origin_buf[512] = {};
    char category_name_buf[256] = {};
    char category_description_buf[2048] = {};
};

struct LayoutState {
    float left_ratio = 0.20f;
    float right_ratio = 0.20f;
    float project_boundary_ratio = 0.50f;
    float problems_panel_height = 220.0f;
};

// Per-source "problems need re-syncing" flags. Setting a flag is cheap; the
// matching SyncXProblems() runs at most once per frame in
// AppRuntime::RefreshDirtyProblems(). This mirrors the tree-refresh pattern
// (TreeDirtyEvent -> tree_needs_rebuild -> RebuildDerivedViewsIfNeeded): code
// that changes the model marks the affected source dirty instead of calling the
// heavyweight sync inline.
struct ProblemSyncDirty {
    bool review = false;
    bool terminology = false;
    bool confidence = false;
    bool acp = false;
    bool translation = false;

    void MarkAll() {
        review = terminology = confidence = acp = translation = true;
    }
};

struct AppRuntimeState {
    AppRuntimeState();
    ~AppRuntimeState();

    core::AppState app_state;
    AppEvents events;
    core::ProblemsManager problems_manager;
    std::unique_ptr<controllers::ElementEditController> element_edit_controller;
    std::unique_ptr<controllers::ModalCoordinator> modal_coordinator;
    std::unique_ptr<controllers::ProjectController> project_controller;
    std::unique_ptr<controllers::ProposalController> proposal_controller;
    std::unique_ptr<controllers::ReviewController> review_controller;
    std::unique_ptr<controllers::ConfidenceController> confidence_controller;
    std::unique_ptr<controllers::AcpController> acp_controller;
    std::optional<GuidelineCatalog> guideline_catalog;
    bool guideline_catalog_load_attempted = false;
    std::string guideline_catalog_error;
    bool document_dirty = false;
    std::string reviewer_name;
    char reviewer_name_buf[128] = {};

    AiUiState ai;

    // Audited command bus for the currently-open project's SACM working file.
    // Constructed when a project's SacmArgument file is loaded; cleared when
    // no project SACM file is active. Nullptr is the legacy direct-mutation
    // fallback (e.g. when a SACM file is opened outside any project).
    std::unique_ptr<core::commands::CommandBus> command_bus;

    // Most recent audit replay verification result for the currently-loaded
    // project SACM file. Populated immediately after the project file is
    // opened; cleared on project close. When `ran && !success` the History
    // Timeline area surfaces a warning banner offering to reconcile.
    std::optional<core::audit::ReplayVerificationResult> last_audit_verification;

    // Most recent autosave write failure surfaced from CommandBus::Execute
    // (atomic SACM write or audit-manifest update). Populated via
    // `AutosaveFailedEvent`; cleared when the user dismisses the banner or
    // when the next audited command succeeds. While non-empty, the canvas
    // shows a prominent banner so the user knows the on-disk SACM no longer
    // reflects their latest committed change.
    std::string last_autosave_error;

    // When the user clicks "Reconcile" in the audit-divergence popup we
    // cannot run the reconcile inline: it tears down the canvas tab
    // currently being rendered (PerformOpenProjectFile clears the
    // argument_package_canvas_tabs vector, invalidating the tab reference
    // held by the in-flight render frame). Instead we set this flag and
    // run ReconcileAuditStore() at the top of the next frame.
    bool pending_reconcile_audit_store = false;

    bool tree_needs_rebuild = false;
    ProblemSyncDirty problems_dirty;
    core::AssuranceTree current_tree;
    core::TreeDisplayOrder tree_display_order;
    core::TreeEditIndex tree_edit_index;
    bool tree_edit_index_valid = false;
    WorkbenchState workbench;
    std::map<std::string, sacm::SacmPackageTreeResult> sacm_package_tree_cache;
    std::optional<sacm::SacmPackageTreeNode> selected_package_node;
    std::filesystem::path selected_package_file_path;
    TerminologyUiState terminology;
    LayoutState layout;

    // Element IDs flagged for secondary-language translation review: the user
    // edited the text of an element that has a translation, so the other
    // language may be out of sync until they review and accept. Persisted to a
    // project sidecar so the warning survives reload.
    std::unordered_set<std::string> translation_review_pending_ids;

    bool IsProposalCanvasActive() const;

    void LoadAiSettingsState();
    void RefreshStoredAiKeyState();
};

} // namespace app
