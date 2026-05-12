#pragma once

#include <string>
#include <vector>

namespace export_gsn {

enum class GsnNodeKind {
    Goal,
    Strategy,
    Solution,
    Context,
    Assumption,
    Justification,
};

enum class GsnEdgeKind {
    SupportedBy,
    InContextOf,
};

struct GsnNode {
    std::string id;
    std::string source_gid;
    GsnNodeKind kind = GsnNodeKind::Goal;
    std::string title;
    std::string text;

    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct GsnEdge {
    std::string id;
    std::string from_id;
    std::string to_id;
    GsnEdgeKind kind = GsnEdgeKind::SupportedBy;
};

struct GsnDiagram {
    std::vector<GsnNode> nodes;
    std::vector<GsnEdge> edges;
};

} // namespace export_gsn