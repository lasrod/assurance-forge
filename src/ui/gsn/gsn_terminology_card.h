#pragma once

#include "core/terminology_scope_service.h" // core::TermOccurrence (full type used in cache)
#include "ui/gsn/gsn_canvas.h"              // GsnNode, TerminologyCardState, ElementContextActions, UiState

#include <imgui.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace core {
class TerminologyService;
}

namespace sacm {
struct AssuranceCasePackage;
}

namespace ui::gsn {

// Hit region for a single highlighted terminology span inside a node label.
// Produced by `BuildAndDrawTerminologySpans` and consumed by
// `HandleTerminologySpanInteractions` on the same frame.
struct TerminologySpanHitRegion {
    ImVec2 min;
    ImVec2 max;
    TerminologyCardState card;
};

// Per-canvas cache for `TerminologyService::DetectTermsInText` results,
// keyed by element id and verified by the source text. The disk-side
// terminology package is identified by pointer; if it changes between
// frames the entire cache is dropped.
struct TerminologyOccurrenceCache {
    struct Entry {
        std::string text;
        std::vector<core::TermOccurrence> occurrences;
    };
    const sacm::AssuranceCasePackage* package_ptr = nullptr;
    std::unordered_map<std::string, Entry> entries;
};

// Detect terminology occurrences inside the node's description, draw dotted
// underlines, and return the corresponding hit regions for hover/click
// handling. Returns an empty vector if `terminology_service` is null, the
// node has no description, or `zoom` is below the label-rendering threshold.
// `occurrence_cache` (optional) is used to memoise the expensive
// `DetectTermsInText` call across frames.
std::vector<TerminologySpanHitRegion>
BuildAndDrawTerminologySpans(ImDrawList* draw_list,
                             const GsnNode& node,
                             ImVec2 top_left,
                             float text_left,
                             float text_wrap,
                             float zoom,
                             const UiState& ui_state,
                             const core::TerminologyService* terminology_service,
                             TerminologyOccurrenceCache* occurrence_cache = nullptr);

// Render the hover tooltip for the region under the mouse and pin the card
// on click. `overlay_hovered` should be `true` when the mouse is over a
// floating canvas overlay (zoom buttons etc.) so that span interactions are
// suppressed in that case.
void HandleTerminologySpanInteractions(const std::vector<TerminologySpanHitRegion>& regions,
                                       TerminologyCardState* card_state,
                                       const sacm::AssuranceCasePackage* terminology_package,
                                       const ElementContextActions& actions,
                                       bool overlay_hovered);

// Note: `RenderPinnedTerminologyCard` is declared in `ui/gsn/gsn_canvas.h`
// (legacy public surface) and defined in this translation unit.

} // namespace ui::gsn
