#include "app/proposal_ui_state.h"

#include "ui/ui_state.h"

namespace app {

void ClearProposalHighlightState(ui::UiState& ui_state) {
    ui_state.proposal_highlight_ids.clear();
    ui_state.proposal_text_changes.clear();
    ui_state.marked_for_removal.clear();
    ui_state.center_on_marked = false;
    ui_state.dim_non_proposal_nodes = false;
}

} // namespace app