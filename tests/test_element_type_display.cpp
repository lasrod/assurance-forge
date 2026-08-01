#include "ui/panels/element_panel.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using ui::panels::ElementTypeDisplayName;

// SacmElement::type is the XML local name lowercased -- a storage identifier.
// Putting "assertedinference" in front of a reader deciding whether to trust a
// safety argument is leaking the file format into the interface.
TEST(ElementTypeDisplay, HumanisesTheStoredIdentifier) {
    EXPECT_EQ(ElementTypeDisplayName("assertedinference"), "Asserted Inference");
    EXPECT_EQ(ElementTypeDisplayName("argumentreasoning"), "Argument Reasoning");
    EXPECT_EQ(ElementTypeDisplayName("artifactreference"), "Artifact Reference");
    EXPECT_EQ(ElementTypeDisplayName("claim"), "Claim");
}

// The parser admits a closed set, so every one of them must be covered -- an
// uncovered value would fall through and surface the raw identifier again.
TEST(ElementTypeDisplay, CoversEveryTypeTheParserAdmits) {
    const char* admitted[] = {"claim",
                              "argumentreasoning",
                              "artifact",
                              "artifactreference",
                              "expression",
                              "assertedinference",
                              "assertedcontext",
                              "assertedevidence"};
    for (const char* raw : admitted) {
        const std::string display = ElementTypeDisplayName(raw);
        EXPECT_NE(display, std::string(raw)) << "no display name for the admitted type '" << raw << "'";
        // Capitalised, since it is a name shown to a reader rather than a token.
        ASSERT_FALSE(display.empty());
        EXPECT_TRUE(display[0] >= 'A' && display[0] <= 'Z') << "not capitalised: " << display;
    }
}

// An unexpected value is shown as-is rather than guessed at: inventing a label
// for an element we did not expect would misrepresent what is in the file.
TEST(ElementTypeDisplay, PassesThroughAnUnrecognisedType) {
    EXPECT_EQ(ElementTypeDisplayName("somefuturesacmtype"), "somefuturesacmtype");
    EXPECT_EQ(ElementTypeDisplayName(""), "");
}

} // namespace
