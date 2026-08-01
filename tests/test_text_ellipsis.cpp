#include "ui/widgets/text_ellipsis.h"

#include <gtest/gtest.h>

#include "imgui.h"

#include <string>
#include <string_view>

namespace {

// U+2026 HORIZONTAL ELLIPSIS, as ui::widgets::Ellipsize appends it.
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

// Ellipsize measures against the current font, so it needs a live context with a
// built atlas. Mirrors the fixture in test_layout.cpp.
class ScopedImGuiFrame {
public:
    ScopedImGuiFrame() : previous_(ImGui::GetCurrentContext()) {
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().DisplaySize = ImVec2(1200.0f, 800.0f);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::NewFrame();
    }

    ~ScopedImGuiFrame() {
        ImGui::EndFrame();
        ImGui::DestroyContext(context_);
        if (previous_) {
            ImGui::SetCurrentContext(previous_);
        }
    }

private:
    ImGuiContext* previous_ = nullptr;
    ImGuiContext* context_ = nullptr;
};

float WidthOf(std::string_view text) {
    return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
}

bool EndsWithEllipsis(const std::string& text) {
    return text.size() >= kEllipsis.size() &&
           text.compare(text.size() - kEllipsis.size(), kEllipsis.size(), kEllipsis) == 0;
}

TEST(TextEllipsis, KeepsTextThatAlreadyFits) {
    ScopedImGuiFrame frame;
    const std::string label = "Arguments";
    const ui::widgets::EllipsizedText fitted = ui::widgets::Ellipsize(label, WidthOf(label) + 10.0f);
    EXPECT_EQ(fitted.text, label);
    EXPECT_FALSE(fitted.truncated);
}

TEST(TextEllipsis, ShortensAndFlagsTextThatDoesNotFit) {
    ScopedImGuiFrame frame;
    const std::string label = "G1: The kitchen blender is acceptably safe for domestic food preparation";
    const float budget = WidthOf(label) * 0.4f;

    const ui::widgets::EllipsizedText fitted = ui::widgets::Ellipsize(label, budget);
    EXPECT_TRUE(fitted.truncated);
    EXPECT_TRUE(EndsWithEllipsis(fitted.text)) << "got: " << fitted.text;
    EXPECT_LT(fitted.text.size(), label.size());
    // The whole point of the budget: the result must actually render within it,
    // ellipsis included. Overrunning is what painted labels under the badges.
    EXPECT_LE(WidthOf(fitted.text), budget);
}

TEST(TextEllipsis, ReportsTheWidthItWillActuallyRenderAt) {
    ScopedImGuiFrame frame;
    const std::string label = "Claim-Evidence Traceability";
    const ui::widgets::EllipsizedText fitted = ui::widgets::Ellipsize(label, WidthOf(label) * 0.5f);
    EXPECT_NEAR(fitted.width, WidthOf(fitted.text), 0.01f);
}

// A byte-index cut would split a multi-byte codepoint and render as mojibake.
// The Japanese catalog makes that a shipping concern, not a theoretical one.
TEST(TextEllipsis, CutsMultiByteTextOnACodepointBoundary) {
    ScopedImGuiFrame frame;
    const std::string label = "\xE5\xAE\x89\xE5\x85\xA8\xE3\x82\xB1\xE3\x83\xBC\xE3\x82\xB9\xE3\x81\xAE\xE6\xA6\x82\xE8"
                              "\xA6\x81"; // 安全ケースの概要

    for (float fraction = 0.1f; fraction < 1.0f; fraction += 0.1f) {
        const ui::widgets::EllipsizedText fitted = ui::widgets::Ellipsize(label, WidthOf(label) * fraction);
        ASSERT_TRUE(EndsWithEllipsis(fitted.text) || !fitted.truncated);

        const std::string body =
            fitted.truncated ? fitted.text.substr(0, fitted.text.size() - kEllipsis.size()) : fitted.text;
        // The retained bytes must be a whole prefix of the original...
        EXPECT_EQ(label.compare(0, body.size(), body), 0) << "not a prefix at fraction " << fraction;
        // ...and must end on a codepoint boundary. The kept text legitimately
        // ends on a continuation byte here (安 is E5 AE 89), so the boundary
        // test is on the byte that FOLLOWS the cut: a continuation byte
        // (10xxxxxx) in that position means the cut landed inside a sequence.
        if (body.size() < label.size()) {
            const unsigned char next = static_cast<unsigned char>(label[body.size()]);
            EXPECT_NE(next & 0xC0u, 0x80u) << "cut mid-codepoint at fraction " << fraction;
        }
    }
}

TEST(TextEllipsis, YieldsNothingForEmptyInputOrNoRoom) {
    ScopedImGuiFrame frame;
    EXPECT_TRUE(ui::widgets::Ellipsize("", 100.0f).text.empty());
    EXPECT_FALSE(ui::widgets::Ellipsize("", 100.0f).truncated);
    EXPECT_TRUE(ui::widgets::Ellipsize("Arguments", 0.0f).text.empty());
    EXPECT_TRUE(ui::widgets::Ellipsize("Arguments", -5.0f).text.empty());
}

} // namespace
