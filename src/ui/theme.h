// Centralized visual theme for Assurance Forge ("Aurora Dark").
// Provides a single source of truth for colors, rounding, and the global
// ImGui style. All UI / canvas code should pull colors from GetTheme()
// instead of using hard-coded literals.
#pragma once

#include <imgui.h>

namespace ui {

struct Theme {
    // ===== Surfaces (background tiers) =====
    ImU32 bg_app;        // Main framebuffer / app background (deepest)
    ImU32 surface_1;     // Panel background
    ImU32 surface_2;     // Card / elevated surface
    ImU32 surface_3;     // Hover / pressed surface, badges

    // ===== Borders =====
    ImU32 border;        // Subtle 1px panel borders
    ImU32 border_strong; // Outlines around shapes / focused elements

    // ===== Text =====
    ImU32 text_primary;
    ImU32 text_secondary;
    ImU32 text_muted;
    ImU32 ink_dark;      // Text drawn on top of light node fills
    ImU32 ink_darker;    // Text drawn on top of mid-tone node fills

    // ===== Accent / semantic =====
    ImU32 accent;        // Indigo - selection, focus, Group2 edges
    ImU32 accent_hover;
    ImU32 accent_pressed;
    ImU32 success;
    ImU32 warning;
    ImU32 danger;

    // ===== GSN node fills (desaturated for dark canvas) =====
    ImU32 node_claim;
    ImU32 node_strategy;
    ImU32 node_solution;
    ImU32 node_context;
    ImU32 node_assumption;
    ImU32 node_justification;
    ImU32 node_evidence;

    // ===== Edges =====
    ImU32 edge_group1;   // Structural (solid)
    ImU32 edge_group2;   // Contextual (dashed) - accent tinted

    // ===== Geometry / elevation =====
    float rounding_ui;          // Buttons, inputs, frames
    float rounding_panel;       // Windows, child cards
    float rounding_node;        // Rectangular GSN node corners
    float outline_thickness;    // Hairline outline on shapes
    float shadow_alpha_top;     // First (closest) shadow layer alpha [0..1]
    float shadow_offset;        // Vertical offset of shadow stack (px)

    // ===== Canvas =====
    ImU32 canvas_bg;
    ImU32 canvas_grid_minor;
    ImU32 canvas_grid_major;
    float canvas_grid_spacing;  // Cell size in content-space pixels
};

// Global theme accessor.
const Theme& GetTheme();

// Apply theme colors and metrics to ImGui's global style. Call once after
// ImGui::CreateContext() and StyleColorsDark().
void ApplyImGuiStyle();

// Linear-interpolate two ImU32 colors in straight-alpha space.
ImU32 LerpColor(ImU32 a, ImU32 b, float t);

// Multiply only the alpha channel of an ImU32 by `factor` (clamped).
ImU32 WithAlpha(ImU32 c, float factor);

// Pick a readable text color (dark vs light) for a given node fill, based on
// perceived luminance. Returns theme.ink_dark or theme.text_primary.
ImU32 InkOn(ImU32 background);

// Lighten/darken an ImU32 by mixing toward white/black. amount in [-1..1].
ImU32 ShadeColor(ImU32 c, float amount);

}  // namespace ui
