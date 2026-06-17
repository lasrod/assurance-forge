#include "core/gsn_layout.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Contour-based tidy-tree layout.
//
// Each node owns a *contour* — its left/right silhouette per row, relative to the
// node's own centre. Sibling subtrees (and root trees) are slid against each
// other's contour until they nearly touch, so the diagram only grows where there
// is real content.
//
// Side content (Group2 context attachments and dialectic challenges) sits in a
// fixed lane immediately beside the node. To make room for it without stretching
// sideways, a node's structural children are pushed DOWN to start below the
// deepest side content (see ChildStartOffset) — so a tall challenge tree simply
// lengthens the parent→child line instead of forcing the argument wide.
//
// Challenge placement (ChallengeSide is decided in the tree builder):
//   * Left / Right: the challenge sits in that side lane beside the host.
//   * Below: the challenge is the host's structural child (used when challenging
//     a context/assumption/justification — it hangs directly beneath it).

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

    // Collect the host's own side challenges first, so we know which lanes are
    // occupied before distributing the Group2 attachments. A Below challenge is a
    // context's own challenge and hangs as its structural child, not in a lane.
    bool has_left_challenge = false;
    bool has_right_challenge = false;
    for (const ChallengeChild& cc : node.input->challenge_children) {
        if (!Find(state, cc.root_id))
            continue;
        switch (cc.side) {
        case ChallengeSide::Below:
            items.structural.push_back(cc.root_id);
            break;
        case ChallengeSide::Left:
            items.left_items.push_back(cc.root_id);
            has_left_challenge = true;
            break;
        case ChallengeSide::Right:
            items.right_items.push_back(cc.root_id);
            has_right_challenge = true;
            break;
        }
    }

    // All Group2 attachments live in a side lane (stacked). A challenge to the host
    // sits OUTSIDE the attachment column in the same lane, so its challenge edge to
    // the host would cross any attachment placed between them. When challenges
    // occupy exactly one lane, steer every attachment to the other (free) lane so a
    // context never overlaps a challenge edge. With challenges on both lanes (or
    // neither) fall back to the balanced left/right split.
    const std::vector<std::string> attachments = ExistingIds(state, node.input->group2_attachments);
    std::vector<int> left_indices;
    std::vector<int> right_indices;
    if (!attachments.empty() && has_left_challenge != has_right_challenge) {
        std::vector<int>& free_lane = has_left_challenge ? right_indices : left_indices;
        for (int i = 0; i < static_cast<int>(attachments.size()); ++i)
            free_lane.push_back(i);
    } else {
        const auto split = DistributeAttachmentSides(static_cast<int>(attachments.size()));
        left_indices = split.first;
        right_indices = split.second;
    }
    for (int idx : left_indices)
        items.plain_g2_left.push_back(attachments[idx]);
    for (int idx : right_indices)
        items.plain_g2_right.push_back(attachments[idx]);

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
        const int ch_offset = 0; // a host challenge sits beside the node, in its own column at the node's row
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
        for (const std::string& att : ExistingIds(state, node->input->group2_attachments))
            stack.push_back({att, frame.row}); // contexts beside the host, at its row
        for (const ChallengeChild& cc : node->input->challenge_children) {
            if (!Find(state, cc.root_id))
                continue;
            // Below: a context's own challenge, as its structural child (pushed
            // below the context's side margins). Left/Right: a side challenge sits
            // beside the host in its own column, at the host's row.
            const int row = (cc.side == ChallengeSide::Below) ? frame.row + child_start : frame.row;
            stack.push_back({cc.root_id, row});
        }
    }
}

// ===== Contour layout (post-order) =====

// Place a vertically-substacked column of Group2 attachments at a fixed lane
// centre x beside the node. They share the x and are sub-stacked within the host's
// row by the y pass (so an assumption sits below a context). A challenge to an
// individual attachment is positioned relative to that attachment (beside it at
// its substacked y, or below it), not here.
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

// Place one side's content (Group2 attachments + the host's own side challenges)
// in columns beside the node, packed outward from the node's edge. Plain
// attachments (no challenge of their own) share a single vertically-substacked
// column nearest the node; every attachment that carries a challenge, and every
// challenge to the host itself, gets its OWN column further out — so challenge
// clusters in the same lane never overlap. The main subtree is pushed below all
// of it via ChildStartOffset.
void PlaceSideLane(LayoutState& state,
                   const NodeLayout& host,
                   const std::vector<std::string>& attachments,
                   const std::vector<std::string>& host_challenges,
                   double gap,
                   bool is_left) {
    double edge = host.width * 0.5 + gap; // distance from host centre to the inner edge of the next column

    // Reserve the next outward slot for a column whose contour spans [cmin, cmax]
    // (relative to the placed item's own centre) and return the offset to apply to
    // that item's centre so its contour sits exactly in the slot. A column's
    // contour can be asymmetric (e.g. an assumption with its own challenge hanging
    // to one side), so we align by contour edges, not by box centre.
    auto reserve_slot = [&](double cmin, double cmax) {
        const double width = cmax - cmin;
        const double offset = is_left ? (-edge - cmax) : (edge - cmin);
        edge += width + gap;
        return offset;
    };
    auto contour_bounds = [&](const std::string& id) -> std::pair<double, double> {
        const Contour& c = ContourOf(state, id, gap);
        if (!c.empty())
            return {GlobalMin(c), GlobalMax(c)};
        const NodeLayout* n = Find(state, id);
        const double half = n ? n->width * 0.5 : 0.0;
        return {-half, half};
    };

    // 1. All attachments (contexts, assumptions, justifications) share ONE
    //    vertically-substacked column nearest the node — they always stack, so an
    //    assumption sits below a context rather than beside it. Each attachment's
    //    own challenge extends outward (beside) or downward (below) within that
    //    attachment's contour, so size the column to the combined extent.
    if (!attachments.empty()) {
        double cmin = 0.0;
        double cmax = 0.0;
        for (const std::string& id : attachments) {
            const auto [a, b] = contour_bounds(id);
            cmin = std::min(cmin, a);
            cmax = std::max(cmax, b);
        }
        PlaceContextColumn(state, host, attachments, reserve_slot(cmin, cmax), is_left);
    }

    // 2. The host's own challenges each get a column further out.
    for (const std::string& id : host_challenges) {
        NodeLayout* n = Find(state, id);
        if (!n)
            continue;
        const auto [cmin, cmax] = contour_bounds(id);
        n->layout_parent = host.input->id;
        n->offset = reserve_slot(cmin, cmax);
        n->is_left_side = is_left;
    }
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

        // 2. Side content beside the node, in columns packed outward. Plain
        //    attachments share one substacked column; each challenged attachment
        //    and each challenge to the host gets its own column so their clusters
        //    never overlap. The main subtree is pushed BELOW these margins (see
        //    ChildStartOffset in AssignRows).
        const bool has_left = !items.plain_g2_left.empty() || !items.left_items.empty();
        const bool has_right = !items.plain_g2_right.empty() || !items.right_items.empty();
        if (!has_left && !has_right)
            continue;

        PlaceSideLane(state, *node, items.plain_g2_left, items.left_items, gap, /*is_left=*/true);
        PlaceSideLane(state, *node, items.plain_g2_right, items.right_items, gap, /*is_left=*/false);
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

    // Build the side substacks. Each Group2 attachment is one entry; a LEAF
    // below-challenge is interleaved right after the context it hangs from, so
    // multiple same-lane context challenges stack vertically instead of landing on
    // the same spot. (Developed/tree challenges keep their row-based placement.)
    struct StackEntry {
        const NodeLayout* nl = nullptr;
        std::string key;     // shared by a context and its interleaved below-challenge
        int height_row = 0;  // row whose stack height this entry contributes to
        int order_primary = 0;   // context stack index
        int order_secondary = 0; // 0 = the context, 1 = its below-challenge
    };
    std::unordered_map<std::string, int> stack_member_row; // substack member id -> row driving its y
    std::unordered_map<std::string, std::string> stack_member_key;
    std::vector<StackEntry> entries;
    for (const auto& [id, nl] : state.nodes) {
        if (!abs_center.count(id))
            continue;
        if (nl.is_group2_plain) {
            entries.push_back({&nl, stack_key(nl), nl.row, nl.stack_index, 0});
            continue;
        }
        const NodeLayout* parent = nl.layout_parent.empty() ? nullptr : Find(state, nl.layout_parent);
        const auto lc = layout_children.find(id);
        const bool is_leaf = lc == layout_children.end() || lc->second.empty();
        if (parent && parent->input && parent->is_group2_plain && nl.row > parent->row && is_leaf)
            entries.push_back({&nl, stack_key(*parent), parent->row, parent->stack_index, 1});
        else
            row_max_height[static_cast<std::size_t>(nl.row)] =
                std::max(row_max_height[static_cast<std::size_t>(nl.row)], nl.height);
    }
    std::sort(entries.begin(), entries.end(), [](const StackEntry& a, const StackEntry& b) {
        if (a.key != b.key)
            return a.key < b.key;
        if (a.order_primary != b.order_primary)
            return a.order_primary < b.order_primary;
        return a.order_secondary < b.order_secondary;
    });
    for (const StackEntry& e : entries) {
        double& height = g2_stack_height[e.key];
        if (height > 0.0)
            height += options.side_stack_gap;
        g2_stack_offset[e.nl->input->id] = height;
        stack_member_row[e.nl->input->id] = e.height_row;
        stack_member_key[e.nl->input->id] = e.key;
        height += e.nl->height;
        double& row_h = row_g2_stack_height[e.height_row];
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

        // The y of a substack member (a Group2 attachment or an interleaved leaf
        // below-challenge): centre its substack in the driving row, then offset to
        // its slot within the stack.
        auto member_y = [&](const std::string& member_id) {
            const int r = stack_member_row[member_id];
            const double rh = row_heights[static_cast<std::size_t>(r)];
            const double stack_height = g2_stack_height[stack_member_key[member_id]];
            return row_y[static_cast<std::size_t>(r)] + std::max(0.0, (rh - stack_height) * 0.5) +
                   g2_stack_offset[member_id];
        };

        const NodeLayout* parent = nl.layout_parent.empty() ? nullptr : Find(state, nl.layout_parent);
        if (g2_stack_offset.count(nl.input->id)) {
            out.y = member_y(nl.input->id);
        } else if (parent && parent->input && parent->is_group2_plain && parent->row == nl.row &&
                   g2_stack_offset.count(parent->input->id)) {
            // A challenge sitting beside a substacked attachment tracks that
            // attachment's y, so challenges to different attachments never collide.
            out.y = member_y(parent->input->id) + std::max(0.0, (parent->height - nl.height) * 0.5);
        } else {
            out.y = row_y[static_cast<std::size_t>(nl.row)] + std::max(0.0, (row_height - nl.height) * 0.5);
        }

        // Straight-down distance for this node's child edges: reach the bottom of
        // the side margins (one row above the children) before curving, so the
        // lines stay under the node and clear any side challenge tree.
        const int child_row = nl.row + ChildStartOffset(state, nl);
        if (child_row > nl.row + 1 && child_row <= max_row) {
            const double node_bottom = out.y + out.height;
            const double curve_start_y = row_y[static_cast<std::size_t>(child_row)] - options.vertical_spacing;
            out.child_edge_drop = std::max(0.0, curve_start_y - node_bottom);
        }

        result.nodes.push_back(std::move(out));
    }

    ShiftIntoPositiveCoordinates(result.nodes, options);
    return result;
}

} // namespace core
