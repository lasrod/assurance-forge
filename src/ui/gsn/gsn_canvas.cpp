#include "ui/gsn/gsn_canvas.h"

#include "core/string_utils.h"
#include "core/terminology_scope_service.h"
#include "ui/gsn/gsn_badges.h"
#include "ui/gsn/gsn_canvas_renderer.h"
#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_node_text.h"
#include "ui/gsn/gsn_shapes.h"
#include "ui/gsn/gsn_terminology_card.h"
#include "ui/theme.h"
#include "ui/ui_state.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

namespace ui::gsn {

// g_BoldFont is defined in gsn_layout.cpp (shared between layout and drawing)

// ===== Node drawing constants =====
// `kTextPadding` and `kFullLabelZoom` are declared in `ui/gsn/gsn_node_text.h`
// because both label rendering and the terminology-span renderer below need
// them. Shape geometry constants live in `ui/gsn/gsn_shapes.h`.

// Zoom step used by keyboard and button controls (matches renderer constant)
static constexpr float kZoomStep = 0.1f;

// Single shared renderer instance used by the compatibility wrapper.
static GsnCanvas& GlobalRenderer() {
    static GsnCanvas instance;
    return instance;
}

// ===== Shape color mapping =====

static ImU32 ColorForType(const std::string& type) {
    const Theme& th = GetTheme();
    if (type == "Claim")
        return th.node_claim;
    if (type == "Strategy")
        return th.node_strategy;
    if (type == "Solution")
        return th.node_solution;
    if (type == "Context")
        return th.node_context;
    if (type == "Assumption")
        return th.node_assumption;
    if (type == "Justification")
        return th.node_justification;
    if (type == "Evidence")
        return th.node_evidence;
    return th.node_context;
}

static ImU32 DimmedProposalColor(ImU32 color) {
    if (GetCurrentAppTheme() == AppTheme::Light) {
        ImU32 softened = LerpColor(color, GetTheme().surface_1, 0.72f);
        return LerpColor(softened, GetTheme().canvas_bg, 0.42f);
    }

    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    float gray = value.x * 0.299f + value.y * 0.587f + value.z * 0.114f;
    ImVec4 dimmed(gray * 0.62f, gray * 0.62f, gray * 0.62f, value.w * 0.58f);
    return ImGui::ColorConvertFloat4ToU32(dimmed);
}

static ImU32 DimmedProposalInk(ImU32 fill_color) {
    if (GetCurrentAppTheme() == AppTheme::Light) {
        return WithAlpha(InkOn(fill_color), 0.72f);
    }
    return WithAlpha(GetTheme().text_secondary, 0.62f);
}

static std::string ProposalFieldDisplayLabel(const std::string& field) {
    if (field == "name")
        return "Name";
    if (field == "content")
        return "Content";
    if (field == "description")
        return "Description";
    if (field.empty())
        return "Text";
    return field;
}

static ImVec2 ProposalOriginalTextCardPosition(ImVec2 node_min, ImVec2 node_max) {
    const float offset = DpiSize(8.0f);
    const float estimated_width = DpiSize(360.0f);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 work_min = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_max =
        viewport ? ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y)
                 : ImVec2(FLT_MAX, FLT_MAX);

    float x = node_max.x + offset;
    if (x + estimated_width > work_max.x) {
        x = node_min.x - estimated_width - offset;
    }
    x = std::max(work_min.x + offset, std::min(x, work_max.x - estimated_width - offset));

    float y = node_min.y;
    const float estimated_height = ImGui::GetTextLineHeightWithSpacing() * 8.0f;
    if (y + estimated_height > work_max.y) {
        y = std::max(work_min.y + offset, work_max.y - estimated_height);
    }
    return ImVec2(x, y);
}

static void RenderProposalOriginalTextCard(const std::vector<ProposalTextChangePreview>& changes,
                                           ImVec2 node_min,
                                           ImVec2 node_max) {
    if (changes.empty())
        return;

    ImGui::SetNextWindowPos(ProposalOriginalTextCardPosition(node_min, node_max), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(DpiSize(280.0f), 0.0f), ImVec2(DpiSize(420.0f), FLT_MAX));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Original Text##proposal_original_text_node_hover", nullptr, flags)) {
        ImGui::TextUnformatted("Original text");
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + DpiSize(380.0f));
        for (size_t index = 0; index < changes.size(); ++index) {
            if (index > 0)
                ImGui::Separator();
            ImGui::TextDisabled("%s", ProposalFieldDisplayLabel(changes[index].field).c_str());
            if (changes[index].old_value.empty()) {
                ImGui::TextDisabled("(empty)");
            } else {
                ImGui::TextWrapped("%s", changes[index].old_value.c_str());
            }
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

// Node shape primitives (`DrawParallelogram`, `DrawStadium`, `DrawCircle`,
// `DrawRoundedRect`, `DrawUndevelopedMarker`, `DrawReviewScopeHighlight`) plus
// their private shadow/shading helpers live in `ui/gsn/gsn_shapes.{h,cpp}`.
// `BadgeRect`, `ComputeBadgeRect`, `DrawAttentionBadge`, and `DrawReviewBadge`
// live in `ui/gsn/gsn_badges.{h,cpp}`.

// ===== Text layout helper =====
// `ComputeTextRegion` and `DrawNodeLabel` live in `ui/gsn/gsn_node_text.{h,cpp}`.

// ===== Terminology hover/pinned card =====
// `TerminologySpanHitRegion`, `BuildAndDrawTerminologySpans`,
// `HandleTerminologySpanInteractions`, and `RenderPinnedTerminologyCard`
// live in `ui/gsn/gsn_terminology_card.{h,cpp}`.

// ===== Main node drawing function =====

void DrawGsnNode(const GsnNode& node,
                 ImVec2 canvas_origin,
                 UiState& ui_state,
                 const parser::AssuranceCase* active_case,
                 const ElementContextActions& actions,
                 const core::TerminologyService* terminology_service,
                 const sacm::AssuranceCasePackage* terminology_package,
                 TerminologyCardState* terminology_card_state,
                 float zoom,
                 bool overlay_hovered) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 top_left = ImVec2(canvas_origin.x + node.position.x * zoom, canvas_origin.y + node.position.y * zoom);
    ImVec2 bottom_right = ImVec2(top_left.x + node.size.x * zoom, top_left.y + node.size.y * zoom);
    ImVec2 scaled_size = ImVec2(node.size.x * zoom, node.size.y * zoom);

    const bool proposal_dim_active = ui_state.dim_non_proposal_nodes && !ui_state.proposal_highlight_ids.empty();
    const bool proposal_highlighted = proposal_dim_active && ui_state.proposal_highlight_ids.count(node.id) > 0;
    const bool proposal_dimmed = proposal_dim_active && !proposal_highlighted;
    const bool has_attention = ui_state.attention_element_ids.count(node.id) > 0;
    const ElementReviewVisualState* review_state = FindElementReviewVisualState(ui_state, node.id);
    const ElementReviewVisualStatus review_status =
        review_state ? ResolveElementReviewVisualStatus(*review_state) : ElementReviewVisualStatus::None;
    const bool has_review_badge = review_status != ElementReviewVisualStatus::None;
    const bool in_review_scope = ui_state.ai_review_scope_element_ids.count(node.id) > 0;
    const bool primary_review_scope_node = in_review_scope && ui_state.ai_review_primary_element_id == node.id;

    ImU32 fill_color = ColorForType(node.type);
    if (proposal_dimmed) {
        fill_color = DimmedProposalColor(fill_color);
    }

    // If this node is marked for pending removal, override the fill with a
    // strong red tint so the user can see exactly what will be removed.
    const bool marked_for_removal = ui_state.marked_for_removal.count(node.id) > 0;
    if (marked_for_removal) {
        fill_color = GetTheme().danger;
    }

    // Subtle hover brighten so nodes feel responsive without shifting layout.
    {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        if (!overlay_hovered && mouse.x >= top_left.x && mouse.x <= bottom_right.x && mouse.y >= top_left.y &&
            mouse.y <= bottom_right.y) {
            fill_color = ShadeColor(fill_color, 0.06f);
        }
    }

    // Draw the GSN shape
    if (node.type == "Strategy") {
        DrawParallelogram(draw_list, top_left, bottom_right, fill_color, zoom);
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        DrawStadium(draw_list, top_left, bottom_right, fill_color, zoom);
    } else if (node.type == "Solution" || node.type == "Evidence") {
        DrawCircle(draw_list, top_left, bottom_right, fill_color, zoom);
    } else {
        DrawRoundedRect(draw_list, top_left, bottom_right, fill_color, zoom);
    }

    // Draw label text
    float text_left, text_wrap;
    ComputeTextRegion(node, top_left, bottom_right, zoom, has_attention, text_left, text_wrap);
    ImU32 ink = marked_for_removal ? GetTheme().text_primary : InkOn(fill_color);
    if (proposal_dimmed)
        ink = DimmedProposalInk(fill_color);
    DrawNodeLabel(draw_list, node, top_left, bottom_right, text_left, text_wrap, zoom, ink, ui_state);
    const std::vector<TerminologySpanHitRegion> terminology_regions = BuildAndDrawTerminologySpans(
        draw_list, node, top_left, text_left, text_wrap, zoom, ui_state, terminology_service);
    HandleTerminologySpanInteractions(
        terminology_regions, terminology_card_state, terminology_package, actions, overlay_hovered);
    DrawUndevelopedMarker(draw_list, node, top_left, bottom_right, zoom);

    // Invisible button for hit-testing.
    // SetNextItemAllowOverlap lets overlay buttons (zoom/language) receive clicks
    // even when they overlap a node's hit area.
    ImGui::SetCursorScreenPos(top_left);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(node.id.c_str(), scaled_size);
    const bool term_click_consumed = terminology_card_state && terminology_card_state->clicked_term_this_frame;
    auto proposal_text_change = ui_state.proposal_text_changes.find(node.id);
    if (!overlay_hovered && proposal_text_change != ui_state.proposal_text_changes.end() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        RenderProposalOriginalTextCard(proposal_text_change->second, top_left, bottom_right);
    }
    if (ImGui::IsItemClicked() && !overlay_hovered && !term_click_consumed) {
        ui_state.selected_element_id = node.id;
        ui_state.selected_acp_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
    }

    // Right-click context menu: select the node, then offer the Add submenu.
    if (ImGui::BeginPopupContextItem(node.id.c_str())) {
        ui_state.selected_element_id = node.id;
        ui_state.selected_acp_id.clear();
        ui_state.selected_relationship_id.clear();
        ui_state.selected_relationship_edge_key.clear();
        RenderAddElementMenu(actions);
        RenderRemoveSubmenu(active_case, ui_state.selected_element_id, actions);
        ImGui::Separator();
        RenderAiReviewMenu(actions);
        ImGui::EndPopup();
    }

    // Highlight selected node with a soft accent glow ring (3 concentric rects,
    // decreasing alpha) instead of a hard outline.
    if (ui_state.selected_element_id == node.id) {
        const Theme& th_sel = GetTheme();
        float scale = DpiScale() * zoom;
        for (int i = 0; i < 3; ++i) {
            float pad = (2.0f + (float)i * 2.0f) * scale;
            float alpha = 0.55f - (float)i * 0.15f;
            draw_list->AddRect(ImVec2(top_left.x - pad, top_left.y - pad),
                               ImVec2(bottom_right.x + pad, bottom_right.y + pad),
                               WithAlpha(th_sel.accent, alpha),
                               DpiSize(kClaimRounding) * zoom + pad,
                               0,
                               1.5f * scale);
        }
    }

    if (proposal_highlighted) {
        const Theme& th_prop = GetTheme();
        float scale = DpiScale() * zoom;
        float pad = 5.0f * scale;
        draw_list->AddRect(ImVec2(top_left.x - pad, top_left.y - pad),
                           ImVec2(bottom_right.x + pad, bottom_right.y + pad),
                           WithAlpha(th_prop.accent, 0.85f),
                           DpiSize(kClaimRounding) * zoom + pad,
                           0,
                           2.4f * scale);
    }

    if (in_review_scope) {
        DrawReviewScopeHighlight(draw_list, top_left, bottom_right, zoom, primary_review_scope_node);
    }

    // Marked-for-removal border (drawn after the selection highlight so a
    // selected & marked node still looks unambiguously red).
    if (marked_for_removal) {
        const Theme& th_rm = GetTheme();
        float scale = DpiScale() * zoom;
        draw_list->AddRect(ImVec2(top_left.x - 3.0f * scale, top_left.y - 3.0f * scale),
                           ImVec2(bottom_right.x + 3.0f * scale, bottom_right.y + 3.0f * scale),
                           ShadeColor(th_rm.danger, -0.20f),
                           DpiSize(kClaimRounding) * zoom + 3.0f * scale,
                           0,
                           2.5f * scale);
    }

    // Status badges drawn last so they always render above all outlines.
    if (has_attention || has_review_badge) {
        const int slot_count = (has_attention ? 1 : 0) + (has_review_badge ? 1 : 0);
        int slot = 0;
        if (has_attention) {
            DrawAttentionBadge(draw_list, ComputeBadgeRect(top_left, bottom_right, zoom, slot++, slot_count), zoom);
        }
        if (has_review_badge && review_state) {
            DrawReviewBadge(draw_list,
                            ComputeBadgeRect(top_left, bottom_right, zoom, slot, slot_count),
                            zoom,
                            review_status,
                            *review_state);
        }
    }
}

void ShowGsnCanvasContentWithRenderer(GsnCanvas& renderer,
                                      UiState& ui_state,
                                      const parser::AssuranceCase* active_case,
                                      const ElementContextActions& actions,
                                      const sacm::AssuranceCasePackage* terminology_package) {
    // Child region with clipping; we manage our own pan/zoom offset
    // so no ImGui scrollbars are needed.
    ImU32 canvas_bg = GetTheme().canvas_bg;
    if (ui_state.proposal_canvas_active) {
        canvas_bg = LerpColor(canvas_bg, GetTheme().attention, 0.16f);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(canvas_bg));
    ImGui::BeginChild("gsn_canvas_child",
                      ImVec2(0, 0),
                      false,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();

    // --- Zoom & pan input handling ---
    ImVec2 child_pos = ImGui::GetWindowPos();

    // --- Background dot grid (drawn behind everything else) ---
    {
        const Theme& th_grid = GetTheme();
        ImDrawList* bg = ImGui::GetWindowDrawList();
        ImVec2 sz = ImGui::GetWindowSize();
        float zoom = renderer.GetZoom();
        ImVec2 offset = renderer.GetViewOffset();
        float spacing = DpiSize(th_grid.canvas_grid_spacing) * zoom;
        // Raise skip threshold to avoid merging dots and excessive draw calls
        // at low zoom levels. Pre-check total dot count so we either draw the
        // full grid or skip it entirely (avoids mid-loop cutoff artifacts).
        const float min_spacing = DpiSize(10.0f);
        constexpr int kMaxDots = 4000;
        if (spacing >= min_spacing) {
            int est_x = static_cast<int>(sz.x / spacing) + 1;
            int est_y = static_cast<int>(sz.y / spacing) + 1;
            if (est_x * est_y <= kMaxDots) {
                float start_x = -fmodf(offset.x, spacing);
                float start_y = -fmodf(offset.y, spacing);
                int ix = (int)floorf(offset.x / spacing);
                int iy0 = (int)floorf(offset.y / spacing);
                float dot = std::max(1.0f, DpiScale() * zoom * 0.9f);
                for (float x = start_x; x < sz.x; x += spacing, ++ix) {
                    int iy = iy0;
                    for (float y = start_y; y < sz.y; y += spacing, ++iy) {
                        bool major = (ix % 4 == 0) && (iy % 4 == 0);
                        ImU32 c = major ? th_grid.canvas_grid_major : th_grid.canvas_grid_minor;
                        ImVec2 p(child_pos.x + x, child_pos.y + y);
                        bg->AddRectFilled(
                            ImVec2(p.x - dot * 0.5f, p.y - dot * 0.5f), ImVec2(p.x + dot * 0.5f, p.y + dot * 0.5f), c);
                    }
                }
            }
        }
    }

    // Center on selected element if requested (e.g. from tree view click)
    {
        if (ui_state.center_on_selection && !ui_state.selected_element_id.empty()) {
            ImVec2 viewport_size = ImGui::GetWindowSize();
            renderer.CenterOnNode(ui_state.selected_element_id, viewport_size);
            ui_state.center_on_selection = false;
        }
        if (ui_state.center_on_marked && !ui_state.marked_for_removal.empty()) {
            ImVec2 viewport_size = ImGui::GetWindowSize();
            renderer.CenterOnIds(ui_state.marked_for_removal, viewport_size);
            ui_state.center_on_marked = false;
        }
    }

    // Ctrl + mouse scroll wheel: zoom at mouse pointer position
    // Plain scrolling (no Ctrl): pan using both wheel axes when available
    if (ImGui::IsWindowHovered()) {
        const ImGuiIO& io = ImGui::GetIO();
        float wheel_y = io.MouseWheel;
        float wheel_x = io.MouseWheelH;
        if (wheel_y != 0.0f && io.KeyCtrl) {
            // Convert mouse screen position to content-space (unzoomed layout coords)
            ImVec2 mouse = io.MousePos;
            ImVec2 offset = renderer.GetViewOffset();
            float zoom = renderer.GetZoom();
            ImVec2 focus_content((mouse.x - child_pos.x + offset.x) / zoom, (mouse.y - child_pos.y + offset.y) / zoom);
            float new_zoom = zoom + (wheel_y > 0.0f ? kZoomStep : -kZoomStep);
            renderer.ZoomAtPoint(new_zoom, focus_content);
        } else if (wheel_x != 0.0f || wheel_y != 0.0f) {
            // Prefer native horizontal deltas from touchpads; keep Shift+wheel as a fallback.
            float scroll_speed = DpiSize(60.0f);
            float pan_x = -wheel_x * scroll_speed;
            float pan_y = -wheel_y * scroll_speed;
            if (io.KeyShift && wheel_x == 0.0f)
                pan_x = -wheel_y * scroll_speed;
            renderer.Pan(pan_x, pan_y);
        }
    }

    // Middle mouse button panning
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        renderer.Pan(-delta.x, -delta.y);
    }

    // Keyboard +/- (numpad and main keyboard)
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            renderer.ZoomIn();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            renderer.ZoomOut();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
            renderer.ResetZoom();
        }
    }

    // --- Pre-compute overlay button rects and check if mouse is over them ---
    // This prevents node clicks from firing when clicking overlay controls.
    bool overlay_hovered = false;
    {
        ImVec2 child_size_pre = ImGui::GetWindowSize();
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;

        // Zoom strip rect
        float btn_sz = DpiSize(28.0f);
        float mgn = DpiSize(12.0f);
        float lbl_w = DpiSize(50.0f);
        float strip_w = btn_sz * 2 + lbl_w + DpiSize(12.0f);
        float zx = child_pos.x + child_size_pre.x - (btn_sz * 2 + lbl_w + mgn + DpiSize(8.0f));
        float zy = child_pos.y + child_size_pre.y - (btn_sz + mgn);
        ImVec2 zoom_tl(zx - DpiSize(4.0f), zy - DpiSize(2.0f));
        ImVec2 zoom_br(zx + strip_w, zy + btn_sz + DpiSize(2.0f));
        if (mouse_pos.x >= zoom_tl.x && mouse_pos.x <= zoom_br.x && mouse_pos.y >= zoom_tl.y &&
            mouse_pos.y <= zoom_br.y) {
            overlay_hovered = true;
        }

        // Language button rect
        if (ui_state.show_secondary_language || ui_state.model_has_translations) {
            float lbw = DpiSize(36.0f), lbh = DpiSize(24.0f), lmgn = DpiSize(12.0f);
            float lx = child_pos.x + child_size_pre.x - (lbw + lmgn);
            float ly = child_pos.y + child_size_pre.y - (DpiSize(28.0f) + lmgn) - lbh - DpiSize(6.0f);
            ImVec2 lang_tl(lx - DpiSize(2.0f), ly - DpiSize(2.0f));
            ImVec2 lang_br(lx + lbw + DpiSize(2.0f), ly + lbh + DpiSize(2.0f));
            if (mouse_pos.x >= lang_tl.x && mouse_pos.x <= lang_br.x && mouse_pos.y >= lang_tl.y &&
                mouse_pos.y <= lang_br.y) {
                overlay_hovered = true;
            }
        }
    }

    // Render the canvas content
    renderer.Render(ui_state, active_case, actions, terminology_package, overlay_hovered);

    const bool relationship_context_menu_active = renderer.GetLastRenderStats().relationship_context_menu_active;
    if (!relationship_context_menu_active &&
        ImGui::BeginPopupContextWindow("##gsn_canvas_background_context",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        const bool can_add_top_goal = static_cast<bool>(actions.add_top_goal);
        if (ImGui::MenuItem("Add New Top Goal", nullptr, false, can_add_top_goal)) {
            actions.add_top_goal();
        }
        ImGui::EndPopup();
    }

    // --- Language toggle button above zoom strip (bottom-right) ---
    {
        // Only show when model has translations
        if (ui_state.show_secondary_language || ui_state.model_has_translations) {
            ImVec2 child_size_lang = ImGui::GetWindowSize();
            float lang_btn_w = DpiSize(36.0f);
            float lang_btn_h = DpiSize(24.0f);
            float lang_margin = DpiSize(12.0f);
            float lang_x = child_pos.x + child_size_lang.x - (lang_btn_w + lang_margin);
            float lang_y =
                child_pos.y + child_size_lang.y - (DpiSize(28.0f) + lang_margin) - lang_btn_h - DpiSize(6.0f);

            ImDrawList* fg_lang = ImGui::GetWindowDrawList();
            fg_lang->AddRectFilled(ImVec2(lang_x - DpiSize(4.0f), lang_y - DpiSize(3.0f)),
                                   ImVec2(lang_x + lang_btn_w + DpiSize(4.0f), lang_y + lang_btn_h + DpiSize(3.0f)),
                                   WithAlpha(GetTheme().surface_2, 0.85f),
                                   DpiSize(8.0f));
            fg_lang->AddRect(ImVec2(lang_x - DpiSize(4.0f), lang_y - DpiSize(3.0f)),
                             ImVec2(lang_x + lang_btn_w + DpiSize(4.0f), lang_y + lang_btn_h + DpiSize(3.0f)),
                             GetTheme().border,
                             DpiSize(8.0f),
                             0,
                             DpiSize(1.0f));

            ImGui::SetCursorScreenPos(ImVec2(lang_x, lang_y));
            // Show "EN" when primary, or the active secondary language code (uppercased)
            char lang_upper[4] = {};
            const std::string& sl = ui_state.active_secondary_lang;
            for (size_t i = 0; i < sl.size() && i < 3; ++i)
                lang_upper[i] = (char)toupper((unsigned char)sl[i]);
            const char* lang_label = ui_state.show_secondary_language ? lang_upper : "EN";
            if (ImGui::Button(lang_label, ImVec2(lang_btn_w, lang_btn_h))) {
                ui_state.show_secondary_language = !ui_state.show_secondary_language;
            }
        }
    }

    // --- Overlay zoom buttons in bottom-right corner ---
    {
        ImVec2 child_size = ImGui::GetWindowSize();
        float button_size = DpiSize(28.0f);
        float margin = DpiSize(12.0f);
        float label_width = DpiSize(50.0f);

        // Position: bottom-right of the child window
        float buttons_x = child_pos.x + child_size.x - (button_size * 2 + label_width + margin + DpiSize(8.0f));
        float buttons_y = child_pos.y + child_size.y - (button_size + margin);

        // Semi-transparent background for the zoom control strip
        ImDrawList* fg = ImGui::GetWindowDrawList();
        float strip_width = button_size * 2 + label_width + DpiSize(12.0f);
        ImVec2 strip_tl(buttons_x - DpiSize(4.0f), buttons_y - DpiSize(2.0f));
        ImVec2 strip_br(buttons_x + strip_width, buttons_y + button_size + DpiSize(2.0f));
        const Theme& th_zoom = GetTheme();
        fg->AddRectFilled(strip_tl, strip_br, WithAlpha(th_zoom.surface_2, 0.85f), DpiSize(8.0f));
        fg->AddRect(strip_tl, strip_br, th_zoom.border, DpiSize(8.0f), 0, DpiSize(1.0f));

        ImGui::SetCursorScreenPos(ImVec2(buttons_x, buttons_y));
        if (ImGui::Button("-##zoom_out", ImVec2(button_size, button_size))) {
            renderer.ZoomOut();
        }

        ImGui::SameLine(0.0f, 0.0f);
        // Zoom percentage label
        char zoom_label[16];
        snprintf(zoom_label, sizeof(zoom_label), "%d%%", static_cast<int>(renderer.GetZoom() * 100.0f + 0.5f));
        ImVec2 label_slot_pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##zoom_label", ImVec2(label_width, button_size));
        ImVec2 text_size = ImGui::CalcTextSize(zoom_label);
        float label_x = label_slot_pos.x + (label_width - text_size.x) * 0.5f;
        float label_y = label_slot_pos.y + (button_size - text_size.y) * 0.5f;
        fg->AddText(ImVec2(label_x, label_y), GetTheme().text_secondary, zoom_label);

        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::Button("+##zoom_in", ImVec2(button_size, button_size))) {
            renderer.ZoomIn();
        }
    }

    // --- Custom scrollbars ---
    {
        ImVec2 content_min, content_max;
        renderer.GetContentBounds(content_min, content_max);

        float zoom = renderer.GetZoom();
        ImVec2 offset = renderer.GetViewOffset();
        ImVec2 child_size = ImGui::GetWindowSize();

        // Total content size in screen pixels (zoomed)
        float content_w = (content_max.x - content_min.x) * zoom;
        float content_h = (content_max.y - content_min.y) * zoom;

        // Viewport position relative to content origin (in screen pixels)
        float viewport_x = offset.x - content_min.x * zoom;
        float viewport_y = offset.y - content_min.y * zoom;

        float scrollbar_thickness = DpiSize(10.0f);
        float scrollbar_margin = DpiSize(2.0f);
        const Theme& th_sb = GetTheme();
        ImU32 track_color = WithAlpha(th_sb.surface_1, 0.55f);
        ImU32 thumb_color = th_sb.surface_3;
        ImU32 thumb_hover = LerpColor(th_sb.surface_3, th_sb.accent, 0.45f);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Horizontal scrollbar (along bottom edge, above zoom controls area)
        if (content_w > child_size.x) {
            float bar_y = child_pos.y + child_size.y - scrollbar_thickness - scrollbar_margin;
            float bar_x = child_pos.x + scrollbar_margin;
            float bar_w = child_size.x - scrollbar_thickness - scrollbar_margin * 3;

            // Track
            draw_list->AddRectFilled(
                ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + scrollbar_thickness), track_color, DpiSize(4.0f));

            // Thumb
            float thumb_ratio = child_size.x / content_w;
            float thumb_w = bar_w * thumb_ratio;
            if (thumb_w < DpiSize(20.0f))
                thumb_w = DpiSize(20.0f);
            float scroll_ratio = viewport_x / (content_w - child_size.x);
            if (scroll_ratio < 0.0f)
                scroll_ratio = 0.0f;
            if (scroll_ratio > 1.0f)
                scroll_ratio = 1.0f;
            float thumb_x = bar_x + scroll_ratio * (bar_w - thumb_w);

            ImVec2 thumb_tl(thumb_x, bar_y);
            ImVec2 thumb_br(thumb_x + thumb_w, bar_y + scrollbar_thickness);

            // Hit test for dragging
            ImGui::SetCursorScreenPos(thumb_tl);
            ImGui::InvisibleButton("##hscroll_thumb", ImVec2(thumb_w, scrollbar_thickness));
            bool h_hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                float delta_px = ImGui::GetIO().MouseDelta.x;
                float delta_scroll = delta_px / (bar_w - thumb_w) * (content_w - child_size.x);
                renderer.Pan(delta_scroll, 0.0f);
            }

            draw_list->AddRectFilled(thumb_tl, thumb_br, h_hovered ? thumb_hover : thumb_color, DpiSize(4.0f));
        }

        // Vertical scrollbar (along right edge)
        if (content_h > child_size.y) {
            float bar_x = child_pos.x + child_size.x - scrollbar_thickness - scrollbar_margin;
            float bar_y = child_pos.y + scrollbar_margin;
            float bar_h = child_size.y - scrollbar_thickness - scrollbar_margin * 3;

            // Track
            draw_list->AddRectFilled(
                ImVec2(bar_x, bar_y), ImVec2(bar_x + scrollbar_thickness, bar_y + bar_h), track_color, DpiSize(4.0f));

            // Thumb
            float thumb_ratio = child_size.y / content_h;
            float thumb_h = bar_h * thumb_ratio;
            if (thumb_h < DpiSize(20.0f))
                thumb_h = DpiSize(20.0f);
            float scroll_ratio = viewport_y / (content_h - child_size.y);
            if (scroll_ratio < 0.0f)
                scroll_ratio = 0.0f;
            if (scroll_ratio > 1.0f)
                scroll_ratio = 1.0f;
            float thumb_y = bar_y + scroll_ratio * (bar_h - thumb_h);

            ImVec2 thumb_tl(bar_x, thumb_y);
            ImVec2 thumb_br(bar_x + scrollbar_thickness, thumb_y + thumb_h);

            // Hit test for dragging
            ImGui::SetCursorScreenPos(thumb_tl);
            ImGui::InvisibleButton("##vscroll_thumb", ImVec2(scrollbar_thickness, thumb_h));
            bool v_hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                float delta_px = ImGui::GetIO().MouseDelta.y;
                float delta_scroll = delta_px / (bar_h - thumb_h) * (content_h - child_size.y);
                renderer.Pan(0.0f, delta_scroll);
            }

            draw_list->AddRectFilled(thumb_tl, thumb_br, v_hovered ? thumb_hover : thumb_color, DpiSize(4.0f));
        }
    }

    ImGui::EndChild();
}

void ShowGsnCanvasContent(UiState& ui_state,
                          const parser::AssuranceCase* active_case,
                          const ElementContextActions& actions,
                          const sacm::AssuranceCasePackage* terminology_package) {
    ShowGsnCanvasContentWithRenderer(GlobalRenderer(), ui_state, active_case, actions, terminology_package);
}

void ShowGsnCanvasWindow() {
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("GSN Canvas", nullptr, window_flags)) {
        ElementContextActions actions{};
        ShowGsnCanvasContent(GetUiState(), nullptr, actions, nullptr);
    }
    ImGui::End();
}

void SetCanvasElements(const std::vector<CanvasElement>& elements) {
    GlobalRenderer().SetElements(elements);
}

void SetCanvasTree(const core::AssuranceTree& tree) {
    GlobalRenderer().SetTree(tree);
}

CanvasRenderStats GetLastCanvasRenderStats() {
    return GlobalRenderer().GetLastRenderStats();
}

} // namespace ui::gsn
