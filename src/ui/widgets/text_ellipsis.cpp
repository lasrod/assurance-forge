#include "ui/widgets/text_ellipsis.h"

#include <algorithm>
#include <cfloat>
#include <string>

namespace ui::widgets {
namespace {

// U+2026 HORIZONTAL ELLIPSIS. One glyph rather than "..." so the cut reads as
// deliberate and costs less width in a panel that is already short of it.
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

float TextWidth(ImFont* font, float font_size, std::string_view text) {
    if (text.empty())
        return 0.0f;
    return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
}

} // namespace

EllipsizedText Ellipsize(std::string_view text, float max_width) {
    EllipsizedText result;
    if (text.empty() || max_width <= 0.0f)
        return result;

    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();

    const float full_width = TextWidth(font, font_size, text);
    if (full_width <= max_width) {
        result.text.assign(text);
        result.width = full_width;
        return result;
    }

    // Reserve room for the ellipsis, then let ImGui find the last whole UTF-8
    // codepoint that still fits. Cutting on a byte index instead would split
    // multi-byte glyphs and render mojibake in the Japanese catalog.
    const float ellipsis_width = TextWidth(font, font_size, kEllipsis);
    const float budget = std::max(0.0f, max_width - ellipsis_width);

    const char* begin = text.data();
    const char* end = begin + text.size();
    const char* remaining = begin;
    font->CalcTextSizeA(font_size, budget, 0.0f, begin, end, &remaining);

    result.text.assign(begin, remaining);
    result.text.append(kEllipsis);
    result.width = TextWidth(font, font_size, result.text);
    result.truncated = true;
    return result;
}

bool AddTextEllipsized(ImDrawList* draw_list, ImVec2 pos, ImU32 color, std::string_view text, float max_width) {
    const EllipsizedText fitted = Ellipsize(text, max_width);
    if (fitted.text.empty())
        return fitted.truncated;
    draw_list->AddText(ImGui::GetFont(),
                       ImGui::GetFontSize(),
                       pos,
                       color,
                       fitted.text.c_str(),
                       fitted.text.c_str() + fitted.text.size());
    return fitted.truncated;
}

void TooltipWhenTruncated(bool truncated, std::string_view full_text) {
    if (!truncated || full_text.empty())
        return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        return;
    const std::string full(full_text);
    ImGui::SetTooltip("%s", full.c_str());
}

} // namespace ui::widgets
