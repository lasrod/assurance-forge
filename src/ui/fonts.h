// Typographic scale.
//
// The application loaded a single font at a single size, so a panel header, a
// filename, and a muted hint all carried identical visual weight — everything
// competed and nothing led. These roles give panels a small, fixed vocabulary
// instead of ad-hoc sizes scattered across call sites.
//
// ImGui 1.92 rasterizes fonts on demand, so a role is a size (and family) push
// rather than a separately loaded face.
#pragma once

#include "imgui.h"

namespace ui::fonts {

enum class Role {
    Title,      // Panel headers, modal titles.
    Body,       // Default. Rarely needs pushing explicitly.
    BodyStrong, // Field labels, node titles, the emphasised half of a pair.
    Caption,    // Counts, paths, timestamps, hints, empty-state text.
};

// Called once during font loading. `bold` may be null, in which case the strong
// roles fall back to the body face at their role size.
void Initialize(float base_size, ImFont* body, ImFont* bold);

float BaseSize();
float SizeFor(Role role);

void Push(Role role);
void Pop();

// Scoped push, so an early return cannot leak an unbalanced font stack.
class Scoped {
public:
    explicit Scoped(Role role) {
        Push(role);
    }
    ~Scoped() {
        Pop();
    }
    Scoped(const Scoped&) = delete;
    Scoped& operator=(const Scoped&) = delete;
};

} // namespace ui::fonts
