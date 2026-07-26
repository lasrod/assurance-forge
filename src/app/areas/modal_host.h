#pragma once

#include "core/reviews/review_item.h"

#include <functional>
#include <string>

namespace app {
struct AppRuntimeState;
}

namespace app::areas {

struct ModalHostCallbacks {
    std::function<void()> begin_create_project;
    std::function<void()> begin_open_project;
    std::function<bool(const std::string&)> try_open_project_manifest;
    std::function<bool()> open_first_project_sacm_file;
    // Re-points every controller that owns a file of its own at the project
    // that was just created. Review items, confidence assessments and register
    // assessments all live outside the SACM document.
    std::function<void()> ensure_project_side_storage;
    std::function<void()> touch_current_project_recent;
    std::function<bool()> save_project;
    std::function<void(bool)> confirm_pending_project_file_open;
    std::function<void(const std::string&)> set_status;
    std::function<bool(const core::reviews::ReviewItem&)> delete_review_item;
    std::function<void()> confirm_add_terminology_package;
    std::function<void()> confirm_delete_terminology_package;
    std::function<void()> confirm_terminology_term_edit;
    std::function<void(bool)> confirm_quick_define_terminology_term;
    std::function<void()> confirm_delete_terminology_term;
    std::function<void()> confirm_terminology_category_edit;
    std::function<void()> confirm_delete_terminology_category;
};

void RenderModalHost(AppRuntimeState& state, bool& done, const ModalHostCallbacks& callbacks);

} // namespace app::areas
