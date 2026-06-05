#include "core/gsn_layout.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Contour-based tidy-tree layout.
//
// Each node owns a *contour* — its left/right silhouette per row — expressed
// relative to the node's own centre. Sub-items (structural children, Group2
// attachments, and dialectic challenge clusters) are slid against the running
// contour until they nearly touch, so everything sits as close as possible and
// the diagram only grows where there is real content. This replaces the older
// column-grid model, whose single per-side "overhang" pushed challenges out
// beyond a node's entire subtree width.
//
// Placement of challenges (see ChallengeSide, decided in the tree builder):
//   * Side (Left/Right): the challenge root sits at the host's row and grows
//     downward, hugging the host subtree's contour on that side.
//   * Below: the challenge root is folded in as an extra structural child of the
//     host, so it grows straight down beneath it (used for context/assumption/
//     justification references).

namespace core {
namespace {

using Contour = std::map<int, std::pair<double, double>>; // row -> {minX, maxX}, relative to node centre

struct NodeLayout {
    const GsnLayoutInputNode* input = nullptr;
    double width = 1.0;
    double height = 1.0;
    int row = 0;
    bool reachable = false;
    int visit_state = 0; // post-order: 0 unseen, 1 in-progress, 2 done

    // Number of rows this node's whole laid-out subtree spans (incl. side margins
    // and the push-down of structural children below them). Computed bottom-up.
    int subtree_height = 1;
    int height_state = 0; // 0 unseen, 1 in-progress, 2 done

    // Position: centre x relative to this node's layout-parent centre.
    std::string layout_parent; // empty => a root/orphan; `offset` is then the absolute base centre
    double offset = 0.0;
    Contour contour;            // relative to this node's own centre (built lazily)
    bool contour_built = false; // memoization guard for ContourOf

    // Group2 vertical-stack bookkeeping (plain context/assumption/justification).
    bool is_group2_plain = false;
    bool is_left_side = false;
    int stack_index = 0;
};

struct LayoutState {
    std::unordered_map<std::string, NodeLayout> nodes;
    bool cycle_seen = false;
};

NodeLayout* Find(LayoutState& state, const std::string& id) {
    auto it = state.nodes.find(id);
    return it == state.nodes.end() ? nullptr : &it->second;
}
const NodeLayout* Find(const LayoutState& state, const std::string& id) {
    auto it = state.nodes.find(id);
    return it == state.nodes.end() ? nullptr : &it->second;
}

std::vector<std::string> ExistingIds(const LayoutState& state, const std::vector<std::string>& ids) {
    std::vector<std::string> out;
    for (const std::string& id : ids)
        if (Find(state, id))
            out.push_back(id);
    return out;
}

std::pair<std::vector<int>, std::vector<int>> DistributeAttachmentSides(int count) {
    std::vector<int> left_indices;
    std::vector<int> right_indices;
    const int left_count = (count + 1) / 2;
    for (int i = 0; i < count; ++i) {
        if (i < left_count)
            left_indices.push_back(i);
        else
            right_indices.push_back(i);
    }
    return {left_indices, right_indices};
}

// ===== Contour operations =====

void MergeInto(Contour& dst, const Contour& src, double dx) {
    for (const auto& [row, span] : src) {
        const double lo = span.first + dx;
        const double hi = span.second + dx;
        auto it = dst.find(row);
        if (it == dst.end())
            dst[row] = {lo, hi};
        else {
            it->second.first = std::min(it->second.first, lo);
            it->second.second = std::max(it->second.second, hi);
        }
    }
}

double GlobalMax(const Contour& c) {
    double m = -1e18;
    for (const auto& [row, span] : c)
        m = std::max(m, span.second);
    return m;
}
double GlobalMin(const Contour& c) {
    double m = 1e18;
    for (const auto& [row, span] : c)
        m = std::min(m, span.first);
    return m;
}

double ContourWidth(const Contour& c) {
    return c.empty() ? 0.0 : (GlobalMax(c) - GlobalMin(c));
}

const Contour& ContourOf(LayoutState& state, const std::string& id, double gap);

// Centre offset to place `item` immediately to the right of `base` with `gap`
// clearance, hugging the base contour row by row.
double ShiftRightOf(const Contour& base, const Contour& item, double gap) {
    if (base.empty())
        return 0.0;
    double shift = -1e18;
    bool shared = false;
    for (const auto& [row, span] : item) {
        auto it = base.find(row);
        if (it == base.end())
            continue;
        shift = std::max(shift, it->second.second + gap - span.first);
        shared = true;
    }
    if (!shared)
        shift = GlobalMax(base) + gap - GlobalMin(item);
    return shift;
}

// ===== Sub-item categorisation =====

struct SubItems {
    std::vector<std::string> structural;   // Group1 children + Below challenge roots (a context's
                                           // below-challenge is its structural child → hangs under it)
    std::vector<std::string> left_items;   // Left side challenges to this node
    std::vector<std::string> right_items;  // Right side challenges to this node
    std::vector<std::string> plain_g2_left;  // all Group2 attachments assigned to the left lane
    std::vector<std::string> plain_g2_right; // all Group2 attachments assigned to the right lane
};

SubItems Categorize(LayoutState& state, const NodeLayout& node) {
    SubItems items;
    items.structural = ExistingIds(state, node.input->group1_children);
    for (const ChallengeChild& cc : node.input->challenge_children) {
        if (cc.side == ChallengeSide::Below && Find(state, cc.root_id))
            items.structural.push_back(cc.root_id);
    }

    // All Group2 attachments live in a side lane (stacked). A context that is
    // itself challenged keeps its challenge as a structural child, so it hangs
    // directly below the context within the lane — no separate "band".
    const std::vector<std::string> attachments = ExistingIds(state, node.input->group2_attachments);
    const auto [left_indices, right_indices] = DistributeAttachmentSides(static_cast<int>(attachments.size()));
    for (int idx : left_indices)
        items.plain_g2_left.push_back(attachments[idx]);
    for (int idx : right_indices)
        items.plain_g2_right.push_back(attachments[idx]);

    for (const ChallengeChild& cc : node.input->challenge_children) {
        if (!Find(state, cc.root_id))
            continue;
        if (cc.side == ChallengeSide::Left)
            items.left_items.push_back(cc.root_id);
        else if (cc.side == ChallengeSide::Right)
            items.right_items.push_back(cc.root_id);
    }
    return items;
}

// Every node referenced as a sub-item, for post-order discovery.
std::vector<std::string> AllSubItems(LayoutState& state, const NodeLayout& node) {
    SubItems items = Categorize(state, node);
    std::vector<std::string> all = items.structural;
    all.insert(all.end(), items.left_items.begin(), items.left_items.end());
    all.insert(all.end(), items.right_items.begin(), items.right_items.end());
    all.insert(all.end(), items.plain_g2_left.begin(), items.plain_g2_left.end());
    all.insert(all.end(), items.plain_g2_right.begin(), items.plain_g2_right.end());
    return all;
}

// ===== Side-margin depth & subtree heights =====

// Deepest row below a node's own row reached by its side content (contexts at
// offset 0, side challenges just below same-side contexts). Uses sub-item
// subtree heights, so call only after ComputeHeights has run for them.
int SideDepth(LayoutState& state, const NodeLayout& node) {
    const SubItems items = Categorize(state, node);
    auto side_of = [&](const std::vector<std::string>& contexts, const std::vector<std::string>& challenges) {
        int ctx_h = 0;
        for (const std::string& id : contexts)
            if (const NodeLayout* n = Find(state, id))
                ctx_h = std::max(ctx_h, n->subtree_height);
        int ch_h = 0;
        for (const std::string& id : challenges)
            if (const NodeLayout* n = Find(state, id))
                ch_h = std::max(ch_h, n->subtree_height);
        const int ch_offset = contexts.empty() ? 0 : 1; // challenges drop below the context stack
        int depth = 0;
        if (ctx_h > 0)
            depth = std::max(depth, ctx_h - 1);
        if (ch_h > 0)
            depth = std::max(depth, ch_offset + ch_h - 1);
        return depth;
    };
    return std::max(side_of(items.plain_g2_left, items.left_items),
                    side_of(items.plain_g2_right, items.right_items));
}

// Row offset (below the node's row) where the node's structural children begin.
// Pushed below the side margins so a side challenge tree never has to stretch
// sideways past the main subtree.
int ChildStartOffset(LayoutState& state, const NodeLayout& node) {
    return std::max(1, SideDepth(state, node) + 1);
}

// Compute subtree_height for every node in a tree (iterative post-order).
void ComputeHeights(LayoutState& state, const std::string& root_id) {
    struct Frame {
        std::string id;
        bool ready;
    };
    std::vector<Frame> stack{{root_id, false}};
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        NodeLayout* node = Find(state, frame.id);
        if (!node)
            continue;
        if (!frame.ready) {
            if (node->height_state == 1) {
                state.cycle_seen = true;
                continue;
            }
            if (node->height_state == 2)
                continue;
            node->height_state = 1;
            stack.push_back({frame.id, true});
            for (const std::string& sub : AllSubItems(state, *node))
                stack.push_back({sub, false});
            continue;
        }
        if (node->height_state == 2)
            continue;

        const SubItems items = Categorize(state, *node);
        const int side_depth = SideDepth(state, *node);
        const int child_start = std::max(1, side_depth + 1);
        int max_child_h = 0;
        for (const std::string& id : items.structural)
            if (const NodeLayout* c = Find(state, id))
                max_child_h = std::max(max_child_h, c->subtree_height);
        const int structural_depth = (max_child_h > 0) ? (child_start + max_child_h - 1) : 0;
        node->subtree_height = 1 + std::max(side_depth, structural_depth);
        node->height_state = 2;
    }
}

// ===== Row assignment (top-down) =====

void AssignRows(LayoutState& state, const std::string& root_id) {
    struct Frame {
        std::string id;
        int row;
    };
    std::vector<Frame> stack{{root_id, 0}};
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        NodeLayout* node = Find(state, frame.id);
        if (!node)
            continue;
        if (node->reachable) {
            state.cycle_seen = true;
            continue;
        }
        node->reachable = true;
        node->row = frame.row;

        // Structural children (and Below challenges, which are a context's own
        // structural child) start below the node's side margins, lengthening the
        // connecting line when a side challenge is a deep tree.
        const int child_start = ChildStartOffset(state, *node);
        for (const std::string& child : ExistingIds(state, node->input->group1_children))
            stack.push_back({child, frame.row + child_start});
        const std::vector<std::string> attachments = ExistingIds(state, node->input->group2_attachments);
        for (const std::string& att : attachments)
            stack.push_back({att, frame.row}); // contexts beside the host, at its row
        const auto [left_indices, right_indices] = DistributeAttachmentSides(static_cast<int>(attachments.size()));
        const bool left_has_context = !left_indices.empty();
        const bool right_has_context = !right_indices.empty();
        for (const ChallengeChild& cc : node->input->challenge_children) {
            if (!Find(state, cc.root_id))
                continue;
            int row = frame.row;
            if (cc.side == ChallengeSide::Below)
                row = frame.row + child_start; // a context's challenge, as its structural child
            else if (cc.side == ChallengeSide::Left && left_has_context)
                row = frame.row + 1; // a side challenge drops just below the context stack
            else if (cc.side == ChallengeSide::Right && right_has_context)
                row = frame.row + 1;
            stack.push_back({cc.root_id, row});
        }
    }
}

// ===== Contour layout (post-order) =====

// Place a vertical column of Group2 attachments at a fixed lane centre x beside
// the node. Contexts share the x and are sub-stacked within the host's row by the
// y pass; a challenged context keeps its challenge as a structural child, so it
// hangs directly below at the same x (no horizontal collision, because the main
// subtree is pushed below the side margins).
void PlaceContextColumn(LayoutState& state,
                        const NodeLayout& host,
                        const std::vector<std::string>& column,
                        double lane_centre_x,
                        bool is_left) {
    int stack_index = 0;
    for (const std::string& id : column) {
        NodeLayout* a = Find(state, id);
        if (!a)
            continue;
        a->layout_parent = host.input->id;
        a->offset = lane_centre_x;
        a->is_group2_plain = true;
        a->is_left_side = is_left;
        a->stack_index = stack_index++;
    }
}

// Lazily build (and memoize) a node's contour relative to its own centre, from
// its already-positioned sub-items. Only nodes that participate in a horizontal
// packing comparison (multi-child parents, side-item hosts, sibling roots) ever
// get here, so a deep single-child chain builds no contours at all.
const Contour& ContourOf(LayoutState& state, const std::string& id, double gap) {
    static const Contour kEmpty;
    NodeLayout* node = Find(state, id);
    if (!node || !node->input)
        return kEmpty;
    if (node->contour_built)
        return node->contour;
    node->contour_built = true; // set first so a cycle returns the (empty) in-progress contour

    Contour c;
    c[node->row] = {-node->width * 0.5, node->width * 0.5};
    const SubItems items = Categorize(state, *node);
    auto merge_all = [&](const std::vector<std::string>& ids) {
        for (const std::string& sub_id : ids)
            if (const NodeLayout* sub = Find(state, sub_id))
                MergeInto(c, ContourOf(state, sub_id, gap), sub->offset);
    };
    merge_all(items.structural);
    merge_all(items.left_items);
    merge_all(items.right_items);
    merge_all(items.plain_g2_left);
    merge_all(items.plain_g2_right);

    node->contour = std::move(c);
    return node->contour;
}

// Post-order: compute each node's sub-item offsets (relative to the node centre).
// Contours are pulled on demand via ContourOf, so the common single-child path is
// O(1) per node.
void ComputeOffsets(LayoutState& state, const std::string& root_id, double gap) {
    struct Frame {
        std::string id;
        bool ready;
    };
    std::vector<Frame> stack{{root_id, false}};
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        NodeLayout* node = Find(state, frame.id);
        if (!node)
            continue;

        if (!frame.ready) {
            if (node->visit_state == 1) {
                state.cycle_seen = true;
                continue;
            }
            if (node->visit_state == 2)
                continue;
            node->visit_state = 1;
            stack.push_back({frame.id, true});
            for (const std::string& sub : AllSubItems(state, *node))
                stack.push_back({sub, false});
            continue;
        }
        if (node->visit_state == 2)
            continue;
        node->visit_state = 2;

        const SubItems items = Categorize(state, *node);

        // 1. Structural children offsets (centred under the node).
        if (items.structural.size() == 1) {
            if (NodeLayout* child = Find(state, items.structural.front())) {
                child->layout_parent = node->input->id;
                child->offset = 0.0;
            }
        } else if (items.structural.size() > 1) {
            Contour running;
            std::vector<double> centres;
            centres.reserve(items.structural.size());
            for (const std::string& child_id : items.structural) {
                const double centre = running.empty() ? 0.0 : ShiftRightOf(running, ContourOf(state, child_id, gap), gap);
                centres.push_back(centre);
                MergeInto(running, ContourOf(state, child_id, gap), centre);
            }
            const double children_mid = (centres.front() + centres.back()) * 0.5;
            std::size_t ci = 0;
            for (const std::string& child_id : items.structural) {
                NodeLayout* child = Find(state, child_id);
                const double centre = centres[ci++];
                if (!child)
                    continue;
                child->layout_parent = node->input->id;
                child->offset = centre - children_mid;
            }
        }

        // 2. Side items: each side has ONE lane at a fixed x beside the node box.
        //    The main subtree is pushed BELOW these margins (see ChildStartOffset
        //    in AssignRows), so side content never collides with the subtree and
        //    needs no sideways stretching — it just sits next to the node.
        const bool has_left = !items.plain_g2_left.empty() || !items.left_items.empty();
        const bool has_right = !items.plain_g2_right.empty() || !items.right_items.empty();
        if (!has_left && !has_right)
            continue;

        auto lane_width = [&](const std::vector<std::string>& a, const std::vector<std::string>& b) {
            double w = 0.0;
            for (const std::string& id : a)
                w = std::max(w, ContourWidth(ContourOf(state, id, gap)));
            for (const std::string& id : b)
                w = std::max(w, ContourWidth(ContourOf(state, id, gap)));
            return w;
        };
        const double left_lane_w = lane_width(items.plain_g2_left, items.left_items);
        const double right_lane_w = lane_width(items.plain_g2_right, items.right_items);
        const double left_lane_centre = -node->width * 0.5 - gap - left_lane_w * 0.5;
        const double right_lane_centre = node->width * 0.5 + gap + right_lane_w * 0.5;

        PlaceContextColumn(state, *node, items.plain_g2_left, left_lane_centre, /*is_left=*/true);
        for (const std::string& id : items.left_items)
            if (NodeLayout* item = Find(state, id)) {
                item->layout_parent = node->input->id;
                item->offset = left_lane_centre;
            }
        PlaceContextColumn(state, *node, items.plain_g2_right, right_lane_centre, /*is_left=*/false);
        for (const std::string& id : items.right_items)
            if (NodeLayout* item = Find(state, id)) {
                item->layout_parent = node->input->id;
                item->offset = right_lane_centre;
            }
    }
}

GsnLayoutSize SizeFor(const std::unordered_map<std::string, GsnLayoutSize>& sizes,
                      const GsnLayoutOptions& options,
                      const std::string& id) {
    auto it = sizes.find(id);
    if (it != sizes.end() && it->second.width > 0.0 && it->second.height > 0.0)
        return it->second;
    return {options.base_node_width, options.default_node_height};
}

void ShiftIntoPositiveCoordinates(std::vector<GsnLayoutNode>& nodes, const GsnLayoutOptions& options) {
    if (nodes.empty())
        return;
    double min_x = nodes.front().x;
    double min_y = nodes.front().y;
    for (const GsnLayoutNode& node : nodes) {
        min_x = std::min(min_x, node.x);
        min_y = std::min(min_y, node.y);
    }
    const double dx = min_x < options.margin_x ? options.margin_x - min_x : 0.0;
    const double dy = min_y < options.margin_y ? options.margin_y - min_y : 0.0;
    if (dx == 0.0 && dy == 0.0)
        return;
    for (GsnLayoutNode& node : nodes) {
        node.x += dx;
        node.y += dy;
    }
}

} // namespace

GsnLayoutGraphResult LayoutGsnGraph(const GsnLayoutInput& input,
                                    const std::unordered_map<std::string, GsnLayoutSize>& node_sizes,
                                    const GsnLayoutOptions& options) {
    GsnLayoutGraphResult result;
    LayoutState state;

    for (const GsnLayoutInputNode& node : input.nodes) {
        if (node.id.empty()) {
            result.warnings.push_back("Layout skipped a node with an empty id.");
            continue;
        }
        NodeLayout nl;
        nl.input = &node;
        const GsnLayoutSize size = SizeFor(node_sizes, options, node.id);
        nl.width = size.width;
        nl.height = size.height;
        if (!state.nodes.emplace(node.id, nl).second)
            result.warnings.push_back("Layout skipped duplicate node id '" + node.id + "'.");
    }

    std::vector<std::string> roots = input.roots;
    if (roots.empty() && !input.nodes.empty())
        roots.push_back(input.nodes.front().id);

    const double gap = options.horizontal_spacing;

    // Assign rows and compute each tree's contour.
    std::vector<std::string> ordered_roots;
    auto schedule = [&](const std::string& id) {
        NodeLayout* node = Find(state, id);
        if (!node || node->reachable) // already laid out as part of another tree
            return;
        ComputeHeights(state, id);    // subtree heights drive the children push-down
        AssignRows(state, id);        // marks this whole tree reachable
        ComputeOffsets(state, id, gap);
        ordered_roots.push_back(id);
    };
    for (const std::string& id : roots)
        schedule(id);
    for (const std::string& id : input.orphans)
        schedule(id);
    // Defensive: lay out any still-unreached node as its own root.
    for (const GsnLayoutInputNode& node : input.nodes)
        schedule(node.id);

    // Pack the roots/orphans left -> right by contour and set their base centres.
    Contour forest;
    for (std::size_t i = 0; i < ordered_roots.size(); ++i) {
        NodeLayout* root = Find(state, ordered_roots[i]);
        if (!root)
            continue;
        const double centre = forest.empty() ? 0.0 : ShiftRightOf(forest, ContourOf(state, ordered_roots[i], gap), gap);
        root->layout_parent.clear();
        root->offset = centre;
        if (i + 1 < ordered_roots.size()) // contour only needed to pack the next root
            MergeInto(forest, ContourOf(state, ordered_roots[i], gap), centre);
    }

    if (state.cycle_seen)
        result.warnings.push_back("Layout detected a cycle and used a best-effort traversal.");

    // Accumulate absolute centre x by walking the layout-parent tree.
    std::unordered_map<std::string, std::vector<std::string>> layout_children;
    for (const auto& [id, nl] : state.nodes)
        if (!nl.layout_parent.empty())
            layout_children[nl.layout_parent].push_back(id);

    std::unordered_map<std::string, double> abs_center;
    std::vector<std::string> queue = ordered_roots;
    for (const std::string& id : ordered_roots)
        if (const NodeLayout* r = Find(state, id))
            abs_center[id] = r->offset;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::string id = queue[head];
        const double base = abs_center[id];
        for (const std::string& child : layout_children[id]) {
            const NodeLayout* c = Find(state, child);
            if (!c)
                continue;
            abs_center[child] = base + c->offset;
            queue.push_back(child);
        }
    }

    // ===== Rows -> y, with Group2 vertical stacking, then emit nodes =====
    int max_row = 0;
    for (const auto& [id, nl] : state.nodes)
        if (abs_center.count(id))
            max_row = std::max(max_row, nl.row);

    std::vector<double> row_max_height(static_cast<std::size_t>(max_row + 1), options.default_node_height);
    std::unordered_map<std::string, double> g2_stack_height; // key -> running height
    std::unordered_map<std::string, double> g2_stack_offset; // node id -> y offset within stack
    std::unordered_map<int, double> row_g2_stack_height;

    auto stack_key = [](const NodeLayout& nl) {
        return nl.layout_parent + "|" + std::to_string(nl.row) + (nl.is_left_side ? "|L" : "|R");
    };

    // Plain Group2 stacked in stack_index order per key.
    std::vector<const NodeLayout*> g2_nodes;
    for (const auto& [id, nl] : state.nodes) {
        if (!abs_center.count(id))
            continue;
        if (nl.is_group2_plain)
            g2_nodes.push_back(&nl);
        else
            row_max_height[static_cast<std::size_t>(nl.row)] =
                std::max(row_max_height[static_cast<std::size_t>(nl.row)], nl.height);
    }
    std::sort(g2_nodes.begin(), g2_nodes.end(), [&](const NodeLayout* a, const NodeLayout* b) {
        if (a->layout_parent != b->layout_parent)
            return a->layout_parent < b->layout_parent;
        if (a->row != b->row)
            return a->row < b->row;
        if (a->is_left_side != b->is_left_side)
            return a->is_left_side && !b->is_left_side;
        return a->stack_index < b->stack_index;
    });
    for (const NodeLayout* nl : g2_nodes) {
        const std::string key = stack_key(*nl);
        double& height = g2_stack_height[key];
        if (height > 0.0)
            height += options.side_stack_gap;
        g2_stack_offset[nl->input->id] = height;
        height += nl->height;
        double& row_h = row_g2_stack_height[nl->row];
        row_h = std::max(row_h, height);
    }

    std::vector<double> row_y(static_cast<std::size_t>(max_row + 1), 0.0);
    std::vector<double> row_heights(static_cast<std::size_t>(max_row + 1), options.default_node_height);
    double cumulative_y = options.margin_y;
    for (int row = 0; row <= max_row; ++row) {
        row_y[static_cast<std::size_t>(row)] = cumulative_y;
        const auto it = row_g2_stack_height.find(row);
        const double g2h = it != row_g2_stack_height.end() ? it->second : 0.0;
        row_heights[static_cast<std::size_t>(row)] = std::max(row_max_height[static_cast<std::size_t>(row)], g2h);
        cumulative_y += row_heights[static_cast<std::size_t>(row)] + options.vertical_spacing;
    }

    for (const auto& [id, nl] : state.nodes) {
        auto center_it = abs_center.find(id);
        if (center_it == abs_center.end() || !nl.input)
            continue;
        const double row_height = row_heights[static_cast<std::size_t>(nl.row)];

        GsnLayoutNode out;
        out.id = nl.input->id;
        out.role = nl.input->role;
        out.group = nl.input->group;
        out.label = nl.input->label;
        out.label_secondary = nl.input->label_secondary;
        out.undeveloped = nl.input->undeveloped;
        out.parent_id = nl.input->parent_id;
        out.width = nl.width;
        out.height = nl.height;
        out.side_stack_index = nl.stack_index;
        out.is_left_side = nl.is_left_side;
        out.x = center_it->second - nl.width * 0.5;

        if (nl.is_group2_plain) {
            const std::string key = stack_key(nl);
            const double stack_height = g2_stack_height[key];
            out.y = row_y[static_cast<std::size_t>(nl.row)] + std::max(0.0, (row_height - stack_height) * 0.5) +
                    g2_stack_offset[nl.input->id];
        } else {
            out.y = row_y[static_cast<std::size_t>(nl.row)] + std::max(0.0, (row_height - nl.height) * 0.5);
        }
        result.nodes.push_back(std::move(out));
    }

    ShiftIntoPositiveCoordinates(result.nodes, options);
    return result;
}

} // namespace core
