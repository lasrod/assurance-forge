#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/project_workflow.h"
#include "ai/ai_claim_review.h"
#include "ai/ai_service.h"
#include "ai/ai_task_runner.h"
#include "ai/http_client.h"
#include "ai/secret_store.h"
#include "core/app_state.h"
#include "core/assurance_tree.h"
#include "core/element_factory.h"
#include "core/problems/problems_manager.h"
#include "core/reviews/review_item.h"
#include "core/reviews/review_item_manager.h"
#include "core/reviews/review_proposal.h"
#include "core/reviews/review_proposal_manager.h"
#include "parser/xml_parser.h"

namespace app {

constexpr size_t kPathBufferSize = 512;

struct AppRuntimeState {
    AppRuntimeState();

    core::AppState app_state;
    core::ProblemsManager problems_manager;
    core::reviews::ReviewItemManager review_item_manager;
    core::reviews::ReviewProposalManager review_proposal_manager;
    bool document_dirty = false;
    bool review_items_dirty = false;
    std::string reviewer_name;
    char reviewer_name_buf[128] = {};
    bool show_reviewer_name_prompt = false;

    std::shared_ptr<ai::AiSettingsStore> ai_settings_store;
    std::shared_ptr<ai::ISecretStore> ai_secret_store;
    std::shared_ptr<ai::IHttpClient> ai_http_client;
    std::shared_ptr<ai::IAiProvider> ai_provider;
    std::shared_ptr<ai::AiService> ai_service;
    ai::AiTaskRunner ai_task_runner;
    std::shared_ptr<ai::AiTaskHandle> ai_test_task;
    std::shared_ptr<ai::AiTaskHandle> ai_review_task;
    ai::AiProviderSettings ai_settings;
    ai::AiConnectionStatus ai_connection_status;
    bool ai_key_stored = false;
    bool ai_secure_store_available = false;
    char ai_api_key_buf[256] = {};
    char ai_model_buf[128] = {};

    char file_path_buf[kPathBufferSize] = "data/oasc-ja.xml";
    char dir_path_buf[kPathBufferSize] = "data";

    std::vector<std::string> xml_files;
    int selected_file_idx = -1;

    bool tree_needs_rebuild = false;
    core::AssuranceTree current_tree;
    bool proposal_preview_active = false;
    bool proposal_creator_active = false;
    parser::AssuranceCase proposal_preview_model;
    std::string proposal_preview_id;
    core::reviews::ReviewProposal proposal_draft;
    std::map<std::string, std::string> proposal_creator_generated_ids;
    bool proposal_creator_preview_refresh_pending = false;
    std::optional<std::string> proposal_creator_pending_select_create_ref;
    bool proposal_creator_pending_clear_selection = false;
    bool show_overwrite_confirm = false;
    bool force_center_tab_selection = false;
    bool pending_focus_root = false;
    bool show_gsn_tab = true;
    bool show_cse_tab = false;
    bool show_evidence_tab = false;

    float left_ratio = 0.20f;
    float right_ratio = 0.20f;
    float right_panel_split_ratio = 0.5f;
    float project_boundary_ratio = 0.50f;
    float problems_panel_height = 220.0f;

    bool show_not_implemented_modal = false;
    std::string not_implemented_feature;
    bool show_startup_project_window = true;

    bool show_create_project_modal = false;
    bool show_project_file_name_modal = false;
    ProjectFileCreateKind pending_project_file_kind = ProjectFileCreateKind::Sacm;
    char project_name_buf[128] = "MySafetyCase";
    char project_parent_buf[kPathBufferSize] = ".";
    char open_project_path_buf[kPathBufferSize] = "";
    char project_file_name_buf[256] = "main.sacm";
    std::vector<RecentProjectEntry> recent_projects;
    bool show_save_before_exit_modal = false;
    bool close_requested = false;

    bool show_remove_confirm = false;
    std::string pending_remove_id;
    core::RemoveMode pending_remove_mode = core::RemoveMode::NodeOnly;
    std::vector<std::string> pending_remove_ids;
    bool show_delete_review_item_confirm = false;
    core::reviews::ReviewItem pending_delete_review_item;

    bool show_preferences_window = false;
    bool show_theme_tweak_window = false;

    bool show_ai_review_debug_modal = false;
    ai::AiReviewRequestArtifacts pending_ai_review;
    std::string pending_ai_review_element_id;
    std::string pending_ai_review_element_type;
    std::string last_ai_review_raw_response;
    std::string last_ai_review_parse_error;

    bool IsProposalCanvasActive() const;

    void LoadAiSettingsState();
    void RefreshStoredAiKeyState();
};

}  // namespace app
