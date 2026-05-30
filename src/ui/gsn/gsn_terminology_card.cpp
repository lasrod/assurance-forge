#include "ui/gsn/gsn_terminology_card.h"

#include "core/sacm_model.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "core/terminology_scope_service.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_node_text.h" // kTextPadding, kFullLabelZoom
#include "ui/i18n/localization.h"
#include "ui/theme.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <string>
#include <utility>

namespace ui::gsn {

namespace {

ImU32 TerminologyUnderlineColor(const core::TermOccurrence& occurrence) {
    const Theme& theme = GetTheme();
    if (occurrence.kind == core::TermOccurrenceKind::UndefinedAcronym)
        return WithAlpha(theme.warning, 0.68f);
    if (occurrence.resolution.status == core::TermResolutionStatus::Ambiguous)
        return theme.warning;
    if (occurrence.resolution.status == core::TermResolutionStatus::Unique ||
        occurrence.resolution.status == core::TermResolutionStatus::Explicit)
        return WithAlpha(theme.success, 0.78f);
    return WithAlpha(theme.accent, 0.72f);
}

bool HasTermRef(const core::TerminologyTermRef& term_ref) {
    return !term_ref.id.empty() || !term_ref.gid.empty();
}

bool HasPackageRef(const core::TerminologyPackageRef& package_ref) {
    return !package_ref.id.empty() || !package_ref.gid.empty();
}

TerminologyCardState
CardStateFromOccurrence(const std::string& element_id, const core::TermOccurrence& occurrence, ImVec2 anchor) {
    TerminologyCardState card;
    card.element_id = element_id;
    card.text = occurrence.text;
    card.start_offset = occurrence.start_offset;
    card.end_offset = occurrence.end_offset;
    card.anchor = anchor;

    if (occurrence.kind == core::TermOccurrenceKind::UndefinedAcronym) {
        card.kind = TerminologyCardKind::Undefined;
        return card;
    }

    if (occurrence.resolution.status == core::TermResolutionStatus::Ambiguous) {
        card.kind = TerminologyCardKind::Ambiguous;
        for (const auto& candidate : occurrence.resolution.candidates) {
            card.candidates.push_back({candidate.package_ref, candidate.term_ref});
        }
        return card;
    }

    card.kind = TerminologyCardKind::Resolved;
    if (occurrence.resolution.selected.has_value()) {
        card.package_ref = occurrence.resolution.selected->package_ref;
        card.term_ref = occurrence.resolution.selected->term_ref;
    } else if (!occurrence.resolution.candidates.empty()) {
        card.package_ref = occurrence.resolution.candidates.front().package_ref;
        card.term_ref = occurrence.resolution.candidates.front().term_ref;
    }
    return card;
}

void DrawDottedUnderline(ImDrawList* draw_list, ImVec2 start, ImVec2 end, ImU32 color, float thickness, float zoom) {
    const float length = end.x - start.x;
    if (length <= 0.5f)
        return;
    const float dash = std::max(2.0f, DpiSize(3.0f) * zoom);
    const float gap = std::max(2.0f, DpiSize(3.0f) * zoom);
    if (length <= dash * 1.5f) {
        draw_list->AddLine(start, end, color, thickness);
        return;
    }
    for (float x = start.x; x < end.x; x += dash + gap) {
        const float x2 = std::min(end.x, x + dash);
        draw_list->AddLine(ImVec2(x, start.y), ImVec2(x2, end.y), color, thickness);
    }
}

std::vector<TerminologySpanHitRegion>
BuildAndDrawTerminologySpansForText(ImDrawList* draw_list,
                                    const std::string& element_id,
                                    const char* text_start,
                                    const char* text_end,
                                    ImFont* font,
                                    float font_size,
                                    float text_wrap,
                                    ImVec2 text_pos,
                                    float zoom,
                                    const std::vector<core::TermOccurrence>& occurrences) {
    std::vector<TerminologySpanHitRegion> regions;
    if (!text_start || !text_end || text_start >= text_end)
        return regions;

    const char* line_start = text_start;
    float line_y = text_pos.y;
    const float underline_offset = std::max(1.0f, DpiSize(1.0f) * zoom);
    const float underline_thickness = std::max(1.0f, DpiSize(1.4f) * zoom);
    const float font_scale = zoom;

    while (line_start < text_end) {
        const char* hard_break = static_cast<const char*>(memchr(line_start, '\n', text_end - line_start));
        const char* line_limit = hard_break ? hard_break : text_end;
        const char* line_end = line_limit;
        if (text_wrap > 0.0f && line_start < line_limit) {
            const char* wrap_end = font->CalcWordWrapPositionA(font_scale, line_start, line_limit, text_wrap);
            if (wrap_end && wrap_end > line_start && wrap_end < line_limit)
                line_end = wrap_end;
        }
        const char* visible_end = line_end;

        while (visible_end > line_start && std::isspace(static_cast<unsigned char>(*(visible_end - 1))))
            --visible_end;

        const std::size_t line_start_offset = static_cast<std::size_t>(line_start - text_start);
        const std::size_t line_end_offset = static_cast<std::size_t>(visible_end - text_start);
        for (const auto& occurrence : occurrences) {
            if (occurrence.end_offset <= line_start_offset || occurrence.start_offset >= line_end_offset)
                continue;

            const std::size_t range_start_offset = std::max(occurrence.start_offset, line_start_offset);
            const std::size_t range_end_offset = std::min(occurrence.end_offset, line_end_offset);
            if (range_start_offset >= range_end_offset)
                continue;

            const char* range_start = text_start + range_start_offset;
            const char* range_end = text_start + range_end_offset;
            float x1 = text_pos.x + font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, line_start, range_start).x;
            float x2 = text_pos.x + font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, line_start, range_end).x;
            float y = line_y + font_size - underline_offset;
            DrawDottedUnderline(draw_list,
                                ImVec2(x1, y),
                                ImVec2(x2, y),
                                TerminologyUnderlineColor(occurrence),
                                underline_thickness,
                                zoom);

            TerminologySpanHitRegion region;
            region.min = ImVec2(x1, line_y);
            region.max = ImVec2(x2, line_y + font_size);
            region.card = CardStateFromOccurrence(element_id, occurrence, ImVec2(x2 + DpiSize(8.0f), line_y));
            regions.push_back(std::move(region));
        }

        line_y += font_size;

        if (line_end >= line_limit) {
            line_start = hard_break ? hard_break + 1 : text_end;
        } else {
            line_start = line_end;
            while (line_start < line_limit && std::isspace(static_cast<unsigned char>(*line_start)))
                ++line_start;
        }
    }

    return regions;
}

bool RegionContains(const TerminologySpanHitRegion& region, ImVec2 point) {
    return point.x >= region.min.x && point.x <= region.max.x && point.y >= region.min.y && point.y <= region.max.y;
}

const TerminologySpanHitRegion* FindHoveredTerminologyRegion(const std::vector<TerminologySpanHitRegion>& regions,
                                                             ImVec2 mouse_pos) {
    const TerminologySpanHitRegion* hovered = nullptr;
    float hovered_area = 0.0f;
    for (const auto& region : regions) {
        if (!RegionContains(region, mouse_pos))
            continue;
        const float area = std::max(1.0f, (region.max.x - region.min.x) * (region.max.y - region.min.y));
        if (!hovered || area < hovered_area) {
            hovered = &region;
            hovered_area = area;
        }
    }
    return hovered;
}

const sacm::Term* ResolveCardTerm(const sacm::AssuranceCasePackage* package,
                                  const core::TerminologyPackageRef& package_ref,
                                  const core::TerminologyTermRef& term_ref,
                                  const sacm::TerminologyPackage** out_package = nullptr) {
    if (out_package)
        *out_package = nullptr;
    if (!package || !HasPackageRef(package_ref) || !HasTermRef(term_ref))
        return nullptr;
    const sacm::TerminologyPackage* terminology_package = core::FindTerminologyPackage(*package, package_ref);
    if (!terminology_package)
        return nullptr;
    if (out_package)
        *out_package = terminology_package;
    return core::FindTerminologyTerm(*terminology_package, term_ref);
}

std::string JoinCategoryNames(const sacm::TerminologyPackage& package, const std::vector<std::string>& refs) {
    std::string result;
    for (const auto& ref : refs) {
        if (ref.empty())
            continue;
        if (!result.empty())
            result += ", ";
        result += core::CategoryDisplayName(package, ref);
    }
    return result;
}

std::string CandidateSummary(const sacm::TerminologyPackage* package, const sacm::Term* term) {
    if (!term)
        return AF_TR("Missing term");
    std::string result = term->value.empty() ? AF_TR("Term") : term->value;
    if (!term->name.empty() && core::TrimWhitespace(term->name) != core::TrimWhitespace(term->value))
        result += " - " + term->name;
    if (package && !term->category_refs.empty()) {
        const std::string categories = JoinCategoryNames(*package, term->category_refs);
        if (!categories.empty())
            result += " (" + categories + ")";
    }
    return result;
}

void RenderTermDetails(const sacm::AssuranceCasePackage* package,
                       const sacm::TerminologyPackage* terminology_package,
                       const sacm::Term* term,
                       const TerminologyCardState& card_state) {
    if (!term) {
        ImGui::TextColored(GetErrorColor(), "%s", AF_TR("Term reference could not be resolved.").c_str());
        return;
    }

    ImGui::TextUnformatted(term->value.empty() ? card_state.text.c_str() : term->value.c_str());
    if (!term->name.empty() && core::TrimWhitespace(term->name) != core::TrimWhitespace(term->value))
        ImGui::TextWrapped("%s", term->name.c_str());
    if (!term->description.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", term->description.c_str());
    }
    if (terminology_package && !term->category_refs.empty()) {
        const std::string categories = JoinCategoryNames(*terminology_package, term->category_refs);
        if (!categories.empty())
            ImGui::TextDisabled("%s", ui::i18n::trf("Category: {0}", categories).c_str());
    }
    if (!term->externalReference.empty())
        ImGui::TextDisabled("%s", ui::i18n::trf("Reference: {0}", term->externalReference).c_str());
    if (!term->origin.empty())
        ImGui::TextDisabled("%s", ui::i18n::trf("Origin: {0}", term->origin).c_str());
    if (package)
        ImGui::TextDisabled("%s", ui::i18n::trf("Usage count: {0}",
                                                core::CountTerminologyTermUsage(*package, *term))
                                      .c_str());
}

void RenderTerminologyCardContents(const TerminologyCardState& card_state,
                                   const sacm::AssuranceCasePackage* package,
                                   const ElementContextActions& actions,
                                   bool interactive) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + DpiSize(320.0f));
    if (card_state.kind == TerminologyCardKind::Undefined) {
        ImGui::TextColored(GetWarningColor(), "%s", ui::i18n::trf("{0} is not defined.", card_state.text).c_str());
        ImGui::TextWrapped("%s", AF_TR("Define this term from the active terminology scope.").c_str());
        if (interactive && actions.define_terminology_term) {
            if (ImGui::Button(AF_TR("Define term").c_str(), ImVec2(120.0f, 0.0f)))
                actions.define_terminology_term(card_state.element_id, card_state.text);
        }
        ImGui::PopTextWrapPos();
        return;
    }

    if (card_state.kind == TerminologyCardKind::Ambiguous) {
        ImGui::TextColored(GetWarningColor(), "%s",
                           ui::i18n::trf("{0} has multiple meanings.", card_state.text).c_str());
        int shown = 0;
        for (const auto& candidate : card_state.candidates) {
            ImGui::PushID(shown);
            const sacm::TerminologyPackage* candidate_package = nullptr;
            const sacm::Term* candidate_term =
                ResolveCardTerm(package, candidate.package_ref, candidate.term_ref, &candidate_package);
            ImGui::BulletText("%s", CandidateSummary(candidate_package, candidate_term).c_str());
            if (interactive && actions.add_terminology_term_as_context && candidate_term) {
                if (ImGui::SmallButton(AF_TR("Use for this element").c_str())) {
                    actions.add_terminology_term_as_context(
                        card_state.element_id, candidate.package_ref, candidate.term_ref);
                }
            }
            ImGui::PopID();
            if (++shown >= 4 && static_cast<int>(card_state.candidates.size()) > shown) {
                const int remaining = static_cast<int>(card_state.candidates.size()) - shown;
                ImGui::TextDisabled(
                    "%s", ui::i18n::trnf("{0} more candidate.", "{0} more candidates.", remaining, remaining).c_str());
                break;
            }
        }
        if (interactive) {
            if (actions.define_terminology_term && ImGui::Button(AF_TR("Create new meaning").c_str(), ImVec2(165.0f, 0.0f)))
                actions.define_terminology_term(card_state.element_id, card_state.text);
        }
        ImGui::PopTextWrapPos();
        return;
    }

    const sacm::TerminologyPackage* terminology_package = nullptr;
    const sacm::Term* term =
        ResolveCardTerm(package, card_state.package_ref, card_state.term_ref, &terminology_package);
    RenderTermDetails(package, terminology_package, term, card_state);
    if (interactive && term) {
        ImGui::Spacing();
        if (actions.open_terminology_term && ImGui::Button(AF_TR("Open term").c_str(), ImVec2(100.0f, 0.0f)))
            actions.open_terminology_term(card_state.package_ref, card_state.term_ref);
        ImGui::SameLine();
        if (actions.edit_terminology_term && ImGui::Button(AF_TR("Edit term").c_str(), ImVec2(95.0f, 0.0f)))
            actions.edit_terminology_term(card_state.package_ref, card_state.term_ref);
        if (actions.add_visible_terminology_term_context &&
            ImGui::Button(AF_TR("Add as context").c_str(), ImVec2(130.0f, 0.0f))) {
            actions.add_visible_terminology_term_context(
                card_state.element_id, card_state.package_ref, card_state.term_ref);
        }
        ImGui::SameLine();
        if (actions.find_terminology_usages && ImGui::Button(AF_TR("Find usages").c_str(), ImVec2(120.0f, 0.0f)))
            actions.find_terminology_usages(card_state.package_ref, card_state.term_ref);
    }
    ImGui::PopTextWrapPos();
}

void RenderTerminologyHoverCard(const TerminologyCardState& card_state,
                                const sacm::AssuranceCasePackage* package,
                                const ElementContextActions& actions) {
    ImGui::BeginTooltip();
    RenderTerminologyCardContents(card_state, package, actions, false);
    ImGui::EndTooltip();
}

} // namespace

std::vector<TerminologySpanHitRegion> BuildAndDrawTerminologySpans(ImDrawList* draw_list,
                                                                   const GsnNode& node,
                                                                   ImVec2 top_left,
                                                                   float text_left,
                                                                   float text_wrap,
                                                                   float zoom,
                                                                   const UiState& ui_state,
                                                                   const core::TerminologyService* terminology_service,
                                                                   TerminologyOccurrenceCache* occurrence_cache) {
    if (!terminology_service || zoom < kFullLabelZoom)
        return {};

    const std::string& active_label =
        (ui_state.show_secondary_language && !node.label_secondary.empty()) ? node.label_secondary : node.label;
    if (active_label.empty())
        return {};

    ImFont* bold_font = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * zoom;
    float scaled_padding = DpiSize(kTextPadding) * zoom;
    const char* label_start = active_label.c_str();
    const char* label_end = label_start + active_label.size();
    const char* first_newline = strchr(label_start, '\n');
    if (!first_newline || first_newline + 1 >= label_end)
        return {};

    const char* detail_start = first_newline + 1;
    const std::string detail_text(detail_start, label_end);

    const sacm::AssuranceCasePackage* package_ptr = terminology_service->GetPackage();
    const std::vector<core::TermOccurrence>* occurrences_ptr = nullptr;
    std::vector<core::TermOccurrence> fresh_occurrences;
    if (occurrence_cache) {
        if (occurrence_cache->package_ptr != package_ptr) {
            occurrence_cache->entries.clear();
            occurrence_cache->package_ptr = package_ptr;
        }
        auto it = occurrence_cache->entries.find(node.id);
        if (it != occurrence_cache->entries.end() && it->second.text == detail_text) {
            occurrences_ptr = &it->second.occurrences;
        } else {
            fresh_occurrences = terminology_service->DetectTermsInText(node.id, detail_text);
            TerminologyOccurrenceCache::Entry entry;
            entry.text = detail_text;
            entry.occurrences = fresh_occurrences;
            auto inserted = occurrence_cache->entries.insert_or_assign(node.id, std::move(entry));
            occurrences_ptr = &inserted.first->second.occurrences;
        }
    } else {
        fresh_occurrences = terminology_service->DetectTermsInText(node.id, detail_text);
        occurrences_ptr = &fresh_occurrences;
    }
    if (occurrences_ptr->empty())
        return {};

    ImVec2 bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, first_newline);
    ImVec2 rest_text_size = normal_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, detail_start, nullptr);

    float scaled_node_height = node.size.y * zoom;
    float total_text_height = bold_text_size.y + rest_text_size.y;
    float text_y = top_left.y + (scaled_node_height - total_text_height) * 0.5f;
    if (text_y < top_left.y + scaled_padding)
        text_y = top_left.y + scaled_padding;

    return BuildAndDrawTerminologySpansForText(draw_list,
                                               node.id,
                                               detail_start,
                                               label_end,
                                               normal_font,
                                               font_size,
                                               text_wrap,
                                               ImVec2(text_left, text_y + bold_text_size.y),
                                               zoom,
                                               *occurrences_ptr);
}

void RenderPinnedTerminologyCard(TerminologyCardState& card_state,
                                 const sacm::AssuranceCasePackage* terminology_package,
                                 const ElementContextActions& actions) {
    if (!card_state.pinned)
        return;

    ImGui::SetNextWindowPos(card_state.anchor, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(DpiSize(280.0f), 0.0f), ImVec2(DpiSize(420.0f), FLT_MAX));
    bool open = true;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin((AF_TR("Term Card") + "##gsn_term_card").c_str(), &open, flags)) {
        card_state.card_hovered_this_frame =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows);
        RenderTerminologyCardContents(card_state, terminology_package, actions, true);
    }
    ImGui::End();
    if (!open || ImGui::IsKeyPressed(ImGuiKey_Escape))
        card_state.pinned = false;
}

void HandleTerminologySpanInteractions(const std::vector<TerminologySpanHitRegion>& regions,
                                       TerminologyCardState* card_state,
                                       const sacm::AssuranceCasePackage* terminology_package,
                                       const ElementContextActions& actions,
                                       bool overlay_hovered) {
    if (!card_state || regions.empty() || overlay_hovered)
        return;

    const TerminologySpanHitRegion* hovered_region = FindHoveredTerminologyRegion(regions, ImGui::GetIO().MousePos);
    if (!hovered_region)
        return;

    RenderTerminologyHoverCard(hovered_region->card, terminology_package, actions);
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        hovered_region->card.kind == TerminologyCardKind::Resolved && actions.open_terminology_term) {
        card_state->clicked_term_this_frame = true;
        actions.open_terminology_term(hovered_region->card.package_ref, hovered_region->card.term_ref);
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        *card_state = hovered_region->card;
        card_state->pinned = true;
        card_state->clicked_term_this_frame = true;
    }
}

} // namespace ui::gsn
