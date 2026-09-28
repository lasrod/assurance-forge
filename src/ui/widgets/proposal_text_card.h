// The floating "Original text" card a proposal preview shows on hover: the text
// each changed field held before the proposal. The GSN canvas shows it beside a
// node and the review panel beside a proposal row.
#pragma once

#include "imgui.h"
#include "ui/ui_state.h"

#include <string>
#include <vector>

namespace ui::widgets {

// Draws the card beside the rectangle [item_min, item_max], flipping to the left
// when it would leave the viewport. `window_id` is the ImGui window id, so two
// callers never share one window's state. Draws nothing when `changes` is empty.
void RenderProposalOriginalTextCard(const std::vector<ProposalTextChangePreview>& changes,
                                    ImVec2 item_min,
                                    ImVec2 item_max,
                                    const char* window_id);

} // namespace ui::widgets
