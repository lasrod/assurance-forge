#pragma once

#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "ai/http_client.h"
#include "ai/secret_store.h"
#include "app/app_events.h"
#include "app/controllers/ai_review_controller.h"
#include "app/controllers/element_edit_controller.h"
#include "app/controllers/modal_coordinator.h"
#include "app/controllers/project_controller.h"
#include "app/controllers/proposal_controller.h"
#include "app/controllers/review_controller.h"
#include "app/guideline_catalog.h"
#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/problems/problems_manager.h"
#include "core/terminology_package_service.h"
#include "core/tree_editing.h"
#include "sacm/sacm_package_tree.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace app {

struct AppRuntimeState {
    AppRuntimeState();

    core::AppState app_state;
    AppEvents events;
    core::ProblemsManager problems_manager;
    std::unique_ptr<controllers::ElementEditController> element_edit_controller;
    std::unique_ptr<controllers::ModalCoordinator> modal_coordinator;
    std::unique_ptr<controllers::ProjectController> project_controller;
    std::unique_ptr<controllers::ProposalController> proposal_controller;
    std::unique_ptr<controllers::ReviewController> review_controller;
    std::optional<GuidelineCatalog> guideline_catalog;
    bool guideline_catalog_load_attempted = false;
    std::string guideline_catalog_error;
    bool document_dirty = false;
    std::string reviewer_name;
    char reviewer_name_buf[128] = {};

    std::shared_ptr<ai::AiSettingsStore> ai_settings_store;
    std::shared_ptr<ai::ISecretStore> ai_secret_store;
    std::shared_ptr<ai::IHttpClient> ai_http_client;
    std::shared_ptr<ai::IAiProvider> ai_provider;
    std::shared_ptr<ai::AiService> ai_service;
    ai::AiTaskRunner ai_task_runner;
    std::shared_ptr<ai::AiTaskHandle> ai_test_task;
    std::unique_ptr<controllers::AiReviewController> ai_review_controller;
    ai::AiProviderSettings ai_settings;
    ai::AiConnectionStatus ai_connection_status;
    bool ai_key_stored = false;
    bool ai_secure_store_available = false;
    char ai_api_key_buf[256] = {};
    char ai_model_buf[128] = {};

    bool tree_needs_rebuild = false;
    core::AssuranceTree current_tree;
    core::TreeDisplayOrder tree_display_order;
    core::TreeEditIndex tree_edit_index;
    bool tree_edit_index_valid = false;
    bool force_center_tab_selection = false;
    bool pending_focus_root = false;
    bool show_gsn_tab = true;
    bool show_cse_tab = false;
    bool show_evidence_tab = false;
    bool show_package_details_tab = false;
    bool show_terminology_package_tab = false;
    std::map<std::string, sacm::SacmPackageTreeResult> sacm_package_tree_cache;
    std::optional<sacm::SacmPackageTreeNode> selected_package_node;
    std::filesystem::path selected_package_file_path;
    core::TerminologyPackageRef selected_terminology_package_ref;
    std::filesystem::path selected_terminology_package_file_path;
    char terminology_package_name_buf[256] = {};
    char terminology_package_description_buf[2048] = {};
    bool show_create_terminology_package_modal = false;
    std::optional<core::ProjectFileEntry> pending_terminology_package_parent_entry;
    char new_terminology_package_name_buf[256] = {};
    char new_terminology_package_description_buf[2048] = {};
    bool show_delete_terminology_package_modal = false;
    core::TerminologyTermRef selected_terminology_term_ref;
    core::TerminologyCategoryRef selected_terminology_category_ref;
    char terminology_filter_buf[256] = {};
    char terminology_category_filter_buf[128] = {};
    bool show_terminology_term_editor_modal = false;
    bool editing_existing_terminology_term = false;
    bool show_delete_terminology_term_modal = false;
    int pending_delete_terminology_term_usage_count = 0;
    bool show_terminology_category_editor_modal = false;
    bool editing_existing_terminology_category = false;
    bool show_delete_terminology_category_modal = false;
    int pending_delete_terminology_category_term_count = 0;
    char term_value_buf[256] = {};
    char term_name_buf[256] = {};
    char term_definition_buf[2048] = {};
    char term_categories_buf[512] = {};
    char term_external_reference_buf[512] = {};
    char term_origin_buf[512] = {};
    char category_name_buf[256] = {};
    char category_description_buf[2048] = {};

    float left_ratio = 0.20f;
    float right_ratio = 0.20f;
    float project_boundary_ratio = 0.50f;
    float problems_panel_height = 220.0f;

    bool IsProposalCanvasActive() const;

    void LoadAiSettingsState();
    void RefreshStoredAiKeyState();
};

} // namespace app
