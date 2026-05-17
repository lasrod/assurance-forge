#pragma once

#include "ui/confidence_model.h"

#include <functional>
#include <string>

namespace ui::panels {

struct ConfidencePanelModel {
	std::string element_id;
	bool has_assessment = false;
	bool stale = false;
	std::string method_label;
	std::string status_label = "Active";
	float expected_confidence = 0.0f;
	ElementConfidence confidence;
    std::string storage_warning;
};

struct ConfidencePanelCallbacks {
    std::function<bool(ConfidenceInputMode)> add_confidence;
    std::function<bool(const ElementConfidence&)> save_confidence;
    std::function<bool()> clear_confidence;
    std::function<bool()> mark_reviewed;
    std::function<bool()> backup_invalid_and_reset;
};

bool ShowConfidencePanel(const ConfidencePanelModel& model, const ConfidencePanelCallbacks& callbacks);

} // namespace ui::panels