#include "ui/fonts.h"

namespace ui::fonts {
namespace {

// Multipliers on the base body size. A narrow range on purpose: this is a dense
// engineering tool, and a dramatic scale would cost more rows than the hierarchy
// is worth.
constexpr float kTitleScale = 1.15f;
constexpr float kCaptionScale = 0.87f;

float g_base_size = 15.0f;
ImFont* g_body_font = nullptr;
ImFont* g_bold_font = nullptr;

} // namespace

void Initialize(float base_size, ImFont* body, ImFont* bold) {
    if (base_size > 0.0f)
        g_base_size = base_size;
    g_body_font = body;
    g_bold_font = bold;
}

float BaseSize() {
    return g_base_size;
}

float SizeFor(Role role) {
    switch (role) {
    case Role::Title:
        return g_base_size * kTitleScale;
    case Role::Caption:
        return g_base_size * kCaptionScale;
    case Role::Body:
    case Role::BodyStrong:
        break;
    }
    return g_base_size;
}

void Push(Role role) {
    const bool strong = role == Role::Title || role == Role::BodyStrong;
    // A null font keeps the current family and changes only the size, which is
    // what the non-strong roles want. PushFont always pushes exactly one entry,
    // so Pop() stays symmetric whichever branch is taken.
    ImFont* font = strong ? g_bold_font : g_body_font;
    ImGui::PushFont(font, SizeFor(role));
}

void Pop() {
    ImGui::PopFont();
}

} // namespace ui::fonts
