// Tests for the GSN canvas picking geometry.
//
// `PickRelationshipEdge` reads ImGui's hover and mouse state, so its entry
// point needs a windowed harness. Everything it decides with — where a node's
// rect lands under pan and zoom, whether an edge's bounds reach the viewport,
// the key an edge is identified by, and whether that key is the selected one —
// is pure and is pinned here. Before this file the capability row cited
// `test_ui_state.cpp`, which covers transient canvas state and never touches
// picking: edge selection could have returned the wrong relationship with every
// cited test still green.

#include "ui/gsn/gsn_hit_tester.h"

#include <gtest/gtest.h>

namespace {

ui::gsn::LayoutNode MakeNode(const std::string& id, ImVec2 position, ImVec2 size) {
    ui::gsn::LayoutNode node;
    node.id = id;
    node.position = position;
    node.size = size;
    return node;
}

TEST(GsnHitTester, EdgeKeyIdentifiesTheOrderedPair) {
    const std::string forward = ui::gsn::EdgeKey("G1", "S1");
    EXPECT_EQ(forward, ui::gsn::EdgeKey("G1", "S1"));
    EXPECT_NE(forward, ui::gsn::EdgeKey("S1", "G1"));
}

TEST(GsnHitTester, EdgeKeySeparatorCannotBeForgedFromIds) {
    // Concatenation alone would make ("G1S", "1") collide with ("G1", "S1").
    EXPECT_NE(ui::gsn::EdgeKey("G1S", "1"), ui::gsn::EdgeKey("G1", "S1"));
}

TEST(GsnHitTester, RectsIntersectAcceptsOverlapAndTouching) {
    EXPECT_TRUE(ui::gsn::RectsIntersect(ImVec2(0, 0), ImVec2(10, 10), ImVec2(5, 5), ImVec2(15, 15)));
    EXPECT_TRUE(ui::gsn::RectsIntersect(ImVec2(0, 0), ImVec2(10, 10), ImVec2(10, 10), ImVec2(20, 20)));
}

TEST(GsnHitTester, RectsIntersectRejectsSeparatedRects) {
    EXPECT_FALSE(ui::gsn::RectsIntersect(ImVec2(0, 0), ImVec2(10, 10), ImVec2(11, 0), ImVec2(20, 10)));
    EXPECT_FALSE(ui::gsn::RectsIntersect(ImVec2(0, 0), ImVec2(10, 10), ImVec2(0, 11), ImVec2(10, 20)));
}

TEST(GsnHitTester, PointInsideNodeUsesTheNodeRectAtUnitZoom) {
    const ui::gsn::LayoutNode node = MakeNode("G1", ImVec2(100, 50), ImVec2(200, 80));
    const ImVec2 origin(0, 0);

    EXPECT_TRUE(ui::gsn::PointInsideNode(ImVec2(150, 90), node, origin, 1.0f));
    EXPECT_TRUE(ui::gsn::PointInsideNode(ImVec2(100, 50), node, origin, 1.0f));  // top-left corner
    EXPECT_TRUE(ui::gsn::PointInsideNode(ImVec2(300, 130), node, origin, 1.0f)); // bottom-right corner
    EXPECT_FALSE(ui::gsn::PointInsideNode(ImVec2(99, 90), node, origin, 1.0f));
    EXPECT_FALSE(ui::gsn::PointInsideNode(ImVec2(301, 90), node, origin, 1.0f));
}

TEST(GsnHitTester, PointInsideNodeFollowsThePannedOrigin) {
    const ui::gsn::LayoutNode node = MakeNode("G1", ImVec2(100, 50), ImVec2(200, 80));
    const ImVec2 origin(-40, 25);

    // The same content point is now 40px left and 25px down the screen.
    EXPECT_TRUE(ui::gsn::PointInsideNode(ImVec2(110, 115), node, origin, 1.0f));
    EXPECT_FALSE(ui::gsn::PointInsideNode(ImVec2(150, 60), node, origin, 1.0f));
}

TEST(GsnHitTester, PointInsideNodeScalesPositionAndSizeWithZoom) {
    const ui::gsn::LayoutNode node = MakeNode("G1", ImVec2(100, 50), ImVec2(200, 80));
    const ImVec2 origin(0, 0);

    // At 2x the rect is (200,100)..(600,260): a point inside at 1x can be
    // outside at 2x, which is the bug an unscaled hit test would produce.
    EXPECT_TRUE(ui::gsn::PointInsideNode(ImVec2(300, 200), node, origin, 2.0f));
    EXPECT_FALSE(ui::gsn::PointInsideNode(ImVec2(150, 90), node, origin, 2.0f));
    EXPECT_FALSE(ui::gsn::PointInsideNode(ImVec2(601, 200), node, origin, 2.0f));

    // At 0.5x it is (50,25)..(150,65).
    EXPECT_TRUE(ui::gsn::PointInsideNode(ImVec2(100, 40), node, origin, 0.5f));
    EXPECT_FALSE(ui::gsn::PointInsideNode(ImVec2(160, 40), node, origin, 0.5f));
}

TEST(GsnHitTester, RelationshipEdgeSelectedNeedsBothTheRelationshipAndTheEdgeKey) {
    ui::UiState ui_state;
    ui_state.selected_relationship_id = "R1";
    ui_state.selected_relationship_edge_key = ui::gsn::EdgeKey("G1", "S1");

    core::acp::AcpRelationshipTarget target;
    target.relationship_id = "R1";

    EXPECT_TRUE(ui::gsn::RelationshipEdgeSelected(ui_state, &target, ui::gsn::EdgeKey("G1", "S1")));

    // One relationship can be drawn as several edges — the key is what tells
    // the drawn instances apart, so it has to be compared as well as the id.
    EXPECT_FALSE(ui::gsn::RelationshipEdgeSelected(ui_state, &target, ui::gsn::EdgeKey("G1", "S2")));

    core::acp::AcpRelationshipTarget other;
    other.relationship_id = "R2";
    EXPECT_FALSE(ui::gsn::RelationshipEdgeSelected(ui_state, &other, ui::gsn::EdgeKey("G1", "S1")));
}

TEST(GsnHitTester, RelationshipEdgeSelectedIsFalseWithoutATarget) {
    ui::UiState ui_state;
    ui_state.selected_relationship_id = "R1";
    ui_state.selected_relationship_edge_key = ui::gsn::EdgeKey("G1", "S1");

    EXPECT_FALSE(ui::gsn::RelationshipEdgeSelected(ui_state, nullptr, ui::gsn::EdgeKey("G1", "S1")));
}

} // namespace
