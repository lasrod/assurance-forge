#pragma once

#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "core/element_factory.h"
#include "core/reviews/review_proposal.h"
#include "core/sacm_model.h"
#include "ui/ui_state.h"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace app {
struct AppRuntimeState;
}

namespace app::actions::detail {

struct ElementTextTarget {
    std::string field;
    std::string current_text;
};

inline void SetStatus(AppRuntimeState& state, const std::string& message) {
    state.events.Emit(StatusMessageEvent{message});
}

core::reviews::PatchOperationType CreateOperationFor(core::NewElementKind kind);
bool IsContextLike(core::NewElementKind kind);
ElementTextTarget TextTargetFor(const parser::SacmElement& element);
const char* RemoveModeField(core::RemoveMode mode);

std::string GenerateCreateRef(const core::reviews::ReviewProposal& proposal, core::NewElementKind kind);

core::reviews::ElementRef ExistingElementRef(const std::string& id);
core::reviews::ElementRef CreatedElementRef(const std::string& create_ref);
bool SameElementRef(const core::reviews::ElementRef& lhs, const core::reviews::ElementRef& rhs);

std::optional<core::reviews::ElementRef>
ProposalRefForPreviewId(const std::string& preview_id, const std::map<std::string, std::string>& generated_ids);

std::string PreviewIdForProposalRef(const core::reviews::ElementRef& ref,
                                    const std::map<std::string, std::string>& generated_ids);

void TrackAffectedExistingElement(core::reviews::ReviewProposal& proposal,
                                  const parser::AssuranceCase& base_model,
                                  const std::string& element_id);

void TrackAffectedRef(core::reviews::ReviewProposal& proposal,
                      const parser::AssuranceCase& base_model,
                      const core::reviews::ElementRef& ref);

bool DeleteProposalPatchFile(AppRuntimeState& state, const std::string& proposal_id, std::string& error);

bool SaveProject(AppRuntimeState& state);

void ApplyProposalPreviewVisualState(ui::UiState& ui_state,
                                     parser::AssuranceCase& preview_model,
                                     const parser::AssuranceCase& base_model,
                                     const core::reviews::ReviewProposal& proposal,
                                     const std::map<std::string, std::string>& generated_ids);

} // namespace app::actions::detail
