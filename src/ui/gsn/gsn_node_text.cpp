#include "ui/gsn/gsn_node_text.h"

#include "ui/gsn/gsn_dpi.h"
#include "ui/gsn/gsn_shapes.h" // kParallelogramSkew

#include <cfloat>
#include <cstring>

namespace ui::gsn {

namespace {

constexpr float kMinTextWrap = 40.0f;      // minimum text wrap width
constexpr float kCircleTextInset = 0.29f;  // fraction of radius for circle text area (~1 - sqrt(2)/2)
constexpr float kStadiumTextInset = 0.15f; // fraction of height for stadium shape text inset

} // namespace

void ComputeTextRegion(const GsnNode& node,
                       ImVec2 top_left,
                       ImVec2 bottom_right,
                       float zoom,
                       bool /*reserve_attention_badge*/,
                       float& out_text_left,
                       float& out_text_wrap) {
    float scaled_padding = DpiSize(kTextPadding) * zoom;
    float scaled_width = node.size.x * zoom;
    float scaled_height = node.size.y * zoom;

    out_text_left = top_left.x + scaled_padding;
    out_text_wrap = scaled_width - scaled_padding * 2.0f;

    if (node.type == "Strategy") {
        float skew = scaled_width * kParallelogramSkew;
        out_text_left = top_left.x + skew + scaled_padding;
        out_text_wrap = scaled_width - skew * 2.0f - scaled_padding * 2.0f;
    } else if (node.type == "Solution" || node.type == "Evidence") {
        float center_x = (top_left.x + bottom_right.x) * 0.5f;
        float radius = (scaled_width < scaled_height ? scaled_width : scaled_height) * 0.5f;
        float inset = radius * kCircleTextInset;
        out_text_left = center_x - radius + inset + scaled_padding;
        out_text_wrap = (radius - inset) * 2.0f - scaled_padding * 2.0f;
    } else if (node.type == "Context" || node.type == "Assumption" || node.type == "Justification") {
        float inset = scaled_height * kStadiumTextInset;
        out_text_left = top_left.x + inset + scaled_padding;
        out_text_wrap = scaled_width - inset * 2.0f - scaled_padding * 2.0f;
    }

    float scaled_min_wrap = DpiSize(kMinTextWrap) * zoom;
    if (out_text_wrap < scaled_min_wrap)
        out_text_wrap = scaled_min_wrap;
}

void DrawNodeLabel(ImDrawList* draw_list,
                   const GsnNode& node,
                   ImVec2 top_left,
                   ImVec2 /*bottom_right*/,
                   float text_left,
                   float text_wrap,
                   float zoom,
                   ImU32 ink_color,
                   const UiState& ui_state) {
    ImFont* bold_font = g_BoldFont ? g_BoldFont : ImGui::GetFont();
    ImFont* normal_font = ImGui::GetFont();
    float font_size = ImGui::GetFontSize() * zoom;
    float scaled_padding = DpiSize(kTextPadding) * zoom;
    const bool compact_label = zoom < kFullLabelZoom;

    // Pick label based on language toggle
    const std::string& active_label =
        (ui_state.show_secondary_language && !node.label_secondary.empty()) ? node.label_secondary : node.label;
    const char* label_start = active_label.c_str();
    const char* first_newline = strchr(label_start, '\n');

    // Measure both parts for vertical centering
    ImVec2 bold_text_size(0, 0);
    ImVec2 rest_text_size(0, 0);
    if (first_newline) {
        bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, first_newline);
        if (!compact_label) {
            rest_text_size = normal_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, first_newline + 1, nullptr);
        }
    } else {
        bold_text_size = bold_font->CalcTextSizeA(font_size, FLT_MAX, text_wrap, label_start, nullptr);
    }

    float scaled_node_height = node.size.y * zoom;
    float total_text_height = bold_text_size.y + rest_text_size.y;
    float text_y = top_left.y + (scaled_node_height - total_text_height) * 0.5f;
    if (text_y < top_left.y + scaled_padding)
        text_y = top_left.y + scaled_padding;

    // Bold first line
    draw_list->AddText(bold_font,
                       font_size,
                       ImVec2(text_left, text_y),
                       ink_color,
                       label_start,
                       first_newline ? first_newline : nullptr,
                       text_wrap);
    // Normal rest
    if (first_newline && !compact_label) {
        draw_list->AddText(normal_font,
                           font_size,
                           ImVec2(text_left, text_y + bold_text_size.y),
                           ink_color,
                           first_newline + 1,
                           nullptr,
                           text_wrap);
    }
}

} // namespace ui::gsn
