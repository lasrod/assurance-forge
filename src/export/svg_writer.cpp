#include "export/svg_writer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace export_gsn {
namespace {

constexpr double kCanvasPadding = 48.0;
constexpr double kTextLineHeight = 18.0;
constexpr double kFontSize = 13.0;

struct TextLine {
    std::string text;
    bool bold = false;
};

std::string EscapeXml(const std::string& value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
        case '&':
            out << "&amp;";
            break;
        case '<':
            out << "&lt;";
            break;
        case '>':
            out << "&gt;";
            break;
        case '"':
            out << "&quot;";
            break;
        case '\'':
            out << "&apos;";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

// Decorator geometry. GSN element-abstraction markers hang below bottom-centre;
// an ACP badge sits beside them so a node carrying both stays readable.
constexpr double kUndevelopedRadius = 9.0;
constexpr double kUndevelopedGap = 6.0;
constexpr double kAcpBoxHalf = 8.0;
constexpr double kAcpGap = 6.0;
constexpr double kAcpLabelGap = 5.0;
// Challenges read as adversarial and must not be mistaken for support at a
// glance, so they are the one coloured element in an otherwise black-on-white
// diagram. Consumers can restyle via the `gsn-challenges` class.
constexpr const char* kChallengeColor = "#b3261e";

const char* CssClassFor(GsnNodeKind kind) {
    switch (kind) {
    case GsnNodeKind::Goal:
        return "gsn-goal";
    case GsnNodeKind::Strategy:
        return "gsn-strategy";
    case GsnNodeKind::Solution:
        return "gsn-solution";
    case GsnNodeKind::Context:
        return "gsn-context";
    case GsnNodeKind::Assumption:
        return "gsn-assumption";
    case GsnNodeKind::Justification:
        return "gsn-justification";
    }
    return "gsn-node";
}

std::vector<std::string> SplitParagraphs(const std::string& text) {
    std::vector<std::string> paragraphs;
    std::string current;
    for (char ch : text) {
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            paragraphs.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    paragraphs.push_back(current);
    return paragraphs;
}

std::vector<std::string> WrapParagraph(const std::string& text, size_t max_chars) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    while (words >> word) {
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1 + word.size() <= max_chars) {
            line += " " + word;
        } else {
            lines.push_back(line);
            line = word;
        }
        while (line.size() > max_chars && max_chars > 4) {
            lines.push_back(line.substr(0, max_chars));
            line.erase(0, max_chars);
        }
    }
    if (!line.empty())
        lines.push_back(line);
    if (lines.empty())
        lines.push_back({});
    return lines;
}

std::vector<TextLine> WrappedTextLines(const GsnNode& node) {
    const double available_width = node.kind == GsnNodeKind::Solution ? node.width * 0.62 : node.width - 32.0;
    const size_t max_chars = std::max<size_t>(8, static_cast<size_t>(available_width / 7.0));
    std::vector<TextLine> lines;
    std::string title_line = node.id;
    if (!node.title.empty())
        title_line += ": " + node.title;
    for (const std::string& line : WrapParagraph(title_line, max_chars))
        lines.push_back({line, true});
    for (const std::string& paragraph : SplitParagraphs(node.text)) {
        if (paragraph.empty())
            continue;
        std::vector<std::string> wrapped = WrapParagraph(paragraph, max_chars);
        for (const std::string& line : wrapped)
            lines.push_back({line, false});
    }
    return lines;
}

void WriteText(std::ostringstream& out, const GsnNode& node, double x, double y) {
    const std::vector<TextLine> lines = WrappedTextLines(node);
    out << "    <text x=\"" << x << "\" y=\"" << y << "\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\""
        << kFontSize << "\" fill=\"black\">\n";
    for (size_t i = 0; i < lines.size(); ++i) {
        out << "      <tspan x=\"" << x << "\" dy=\"" << (i == 0 ? 0.0 : kTextLineHeight) << "\"";
        if (lines[i].bold)
            out << " font-weight=\"700\"";
        out << ">" << EscapeXml(lines[i].text) << "</tspan>\n";
    }
    out << "    </text>\n";
}

const char* CssClassFor(GsnEdgeKind kind) {
    switch (kind) {
    case GsnEdgeKind::SupportedBy:
        return "gsn-supportedby";
    case GsnEdgeKind::InContextOf:
        return "gsn-incontextof";
    case GsnEdgeKind::Challenges:
        return "gsn-challenges";
    }
    return "gsn-edge";
}

std::string JoinAcpLabels(const std::vector<std::string>& labels) {
    std::string joined;
    for (const std::string& label : labels) {
        if (!joined.empty())
            joined += ", ";
        joined += label;
    }
    return joined;
}

// GSN element abstraction: undeveloped is a hollow diamond, uninstantiated is a
// hollow triangle, and the combined state overlays the two as a bisected
// diamond. Omitting these from an export makes a pattern look like a finished
// assurance argument.
void WriteElementAbstractionMarker(std::ostringstream& out, const GsnNode& node) {
    if (!node.undeveloped && !node.uninstantiated)
        return;
    const double cx = node.x + node.width / 2.0;
    const double cy = node.y + node.height + kUndevelopedGap + kUndevelopedRadius;
    if (node.undeveloped) {
        const char* css_class = node.uninstantiated
                                    ? "gsn-undeveloped gsn-uninstantiated gsn-undeveloped-uninstantiated"
                                    : "gsn-undeveloped";
        out << "    <polygon class=\"" << css_class << "\" points=\"" << cx << "," << cy - kUndevelopedRadius << " "
            << cx + kUndevelopedRadius << "," << cy << " " << cx << "," << cy + kUndevelopedRadius << " "
            << cx - kUndevelopedRadius << "," << cy << "\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        if (node.uninstantiated) {
            out << "    <line class=\"gsn-undeveloped-uninstantiated-divider\" x1=\"" << cx - kUndevelopedRadius
                << "\" y1=\"" << cy << "\" x2=\"" << cx + kUndevelopedRadius << "\" y2=\"" << cy
                << "\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        }
        return;
    }

    out << "    <polygon class=\"gsn-uninstantiated\" points=\"" << cx << "," << cy - kUndevelopedRadius << " "
        << cx + kUndevelopedRadius << "," << cy << " " << cx - kUndevelopedRadius << "," << cy
        << "\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
}

// The ACP badge: a small square carrying the Assurance Claim Point identifier,
// marking where a confidence argument attaches (GSN v3 §1:5.2.3).
void WriteAcpBadge(std::ostringstream& out, double center_x, double center_y, const std::vector<std::string>& labels) {
    if (labels.empty())
        return;
    out << "    <rect class=\"gsn-acp\" x=\"" << center_x - kAcpBoxHalf << "\" y=\"" << center_y - kAcpBoxHalf
        << "\" width=\"" << kAcpBoxHalf * 2.0 << "\" height=\"" << kAcpBoxHalf * 2.0
        << "\" rx=\"2\" ry=\"2\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
    out << "    <text x=\"" << center_x + kAcpBoxHalf + kAcpLabelGap << "\" y=\"" << center_y + 4.0
        << "\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"11\" fill=\"black\" stroke=\"none\">"
        << EscapeXml(JoinAcpLabels(labels)) << "</text>\n";
}

void WriteElementAcpBadge(std::ostringstream& out, const GsnNode& node) {
    if (node.acp_labels.empty())
        return;
    // Sit clear of the element-abstraction marker when the element carries both.
    const double offset = (node.undeveloped || node.uninstantiated) ? kUndevelopedRadius + kAcpBoxHalf + kAcpGap : 0.0;
    const double cx = node.x + node.width / 2.0 + offset;
    const double cy = node.y + node.height + kAcpGap + kAcpBoxHalf;
    WriteAcpBadge(out, cx, cy, node.acp_labels);
}

void WriteNode(std::ostringstream& out, const GsnNode& node) {
    out << "  <g id=\"" << EscapeXml(node.id) << "\" class=\"" << CssClassFor(node.kind) << "\">\n";
    switch (node.kind) {
    case GsnNodeKind::Goal:
        out << "    <rect x=\"" << node.x << "\" y=\"" << node.y << "\" width=\"" << node.width << "\" height=\""
            << node.height << "\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        WriteText(out, node, node.x + 18.0, node.y + 24.0);
        break;
    case GsnNodeKind::Strategy: {
        const double skew = 30.0;
        out << "    <polygon points=\"" << node.x + skew << "," << node.y << " " << node.x + node.width << "," << node.y
            << " " << node.x + node.width - skew << "," << node.y + node.height << " " << node.x << ","
            << node.y + node.height << "\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        WriteText(out, node, node.x + 34.0, node.y + 25.0);
        break;
    }
    case GsnNodeKind::Solution: {
        const double cx = node.x + node.width / 2.0;
        const double cy = node.y + node.height / 2.0;
        out << "    <circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << node.width / 2.0
            << "\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        WriteText(out, node, node.x + node.width * 0.22, node.y + node.height * 0.34);
        break;
    }
    case GsnNodeKind::Context:
        out << "    <rect x=\"" << node.x << "\" y=\"" << node.y << "\" width=\"" << node.width << "\" height=\""
            << node.height << "\" rx=\"18\" ry=\"18\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        WriteText(out, node, node.x + 18.0, node.y + 24.0);
        break;
    case GsnNodeKind::Assumption:
    case GsnNodeKind::Justification: {
        const double cx = node.x + node.width / 2.0;
        const double cy = node.y + node.height / 2.0;
        out << "    <ellipse cx=\"" << cx << "\" cy=\"" << cy << "\" rx=\"" << node.width / 2.0 << "\" ry=\""
            << node.height / 2.0 << "\" fill=\"white\" stroke=\"black\" stroke-width=\"1.2\"/>\n";
        WriteText(out, node, node.x + 22.0, node.y + 27.0);
        out << "    <text x=\"" << node.x + node.width - 25.0 << "\" y=\"" << node.y + 25.0
            << "\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"14\" fill=\"black\">"
            << (node.kind == GsnNodeKind::Assumption ? "A" : "J") << "</text>\n";
        break;
    }
    }
    WriteElementAbstractionMarker(out, node);
    WriteElementAcpBadge(out, node);
    out << "  </g>\n";
}

struct Point {
    double x = 0.0;
    double y = 0.0;
};

Point BottomCenter(const GsnNode& node) {
    return Point{node.x + node.width / 2.0, node.y + node.height};
}

Point TopCenter(const GsnNode& node) {
    return Point{node.x + node.width / 2.0, node.y};
}

Point SidePoint(const GsnNode& node, bool right_side) {
    return Point{right_side ? node.x + node.width : node.x, node.y + node.height / 2.0};
}

Point Center(const GsnNode& node) {
    return Point{node.x + node.width / 2.0, node.y + node.height / 2.0};
}

Point Midpoint(Point a, Point b) {
    return Point{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

// Where an edge's ACP badge and any challenge aimed at this edge should land.
Point EdgeMidpoint(const GsnEdge& edge, const GsnNode& from, const GsnNode& to) {
    if (edge.kind == GsnEdgeKind::SupportedBy)
        return Midpoint(BottomCenter(from), TopCenter(to));
    const bool to_right = (to.x + to.width / 2.0) >= (from.x + from.width / 2.0);
    return Midpoint(SidePoint(from, to_right), SidePoint(to, !to_right));
}

void WriteEdgePath(std::ostringstream& out, const GsnEdge& edge, Point start, Point end) {
    out << "  <path class=\"" << CssClassFor(edge.kind) << "\" d=\"M " << start.x << " " << start.y << " L " << end.x
        << " " << end.y << "\" fill=\"none\" ";
    switch (edge.kind) {
    case GsnEdgeKind::SupportedBy:
        out << "stroke=\"black\" stroke-width=\"1.2\" marker-end=\"url(#supportedByArrow)\"";
        break;
    case GsnEdgeKind::InContextOf:
        out << "stroke=\"black\" stroke-width=\"1.2\" stroke-dasharray=\"6,4\" marker-end=\"url(#contextArrow)\"";
        break;
    case GsnEdgeKind::Challenges:
        out << "stroke=\"" << kChallengeColor
            << "\" stroke-width=\"1.8\" stroke-dasharray=\"6,4\" marker-end=\"url(#challengeArrow)\"";
        break;
    }
    out << "/>\n";
}

// A challenge sourced at `from` and aimed at a point rather than a node -- used
// when the challenged thing is another relationship.
void WriteChallengeToPoint(std::ostringstream& out, const GsnEdge& edge, const GsnNode& from, Point target) {
    WriteEdgePath(out, edge, Center(from), target);
}

void WriteEdge(std::ostringstream& out, const GsnEdge& edge, const GsnNode& from, const GsnNode& to) {
    if (edge.kind == GsnEdgeKind::SupportedBy) {
        WriteEdgePath(out, edge, BottomCenter(from), TopCenter(to));
        return;
    }
    if (edge.kind == GsnEdgeKind::Challenges) {
        // Aim at the challenged element's border on the side facing the counter.
        const bool to_right = (to.x + to.width / 2.0) >= (from.x + from.width / 2.0);
        WriteEdgePath(out, edge, SidePoint(from, to_right), SidePoint(to, !to_right));
        return;
    }

    const bool to_right = (to.x + to.width / 2.0) >= (from.x + from.width / 2.0);
    WriteEdgePath(out, edge, SidePoint(from, to_right), SidePoint(to, !to_right));
}

} // namespace

std::string GenerateGsnSvg(const GsnDiagram& diagram) {
    if (diagram.nodes.empty())
        return {};

    double max_x = 0.0;
    double max_y = 0.0;
    for (const GsnNode& node : diagram.nodes) {
        max_x = std::max(max_x, node.x + node.width + kCanvasPadding);
        max_y = std::max(max_y, node.y + node.height + kCanvasPadding);
    }
    const double width = std::max(1000.0, std::ceil(max_x));
    const double height = std::max(800.0, std::ceil(max_y));

    std::unordered_map<std::string, const GsnNode*> node_by_id;
    for (const GsnNode& node : diagram.nodes) {
        node_by_id[node.id] = &node;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height
        << "\" viewBox=\"0 0 " << width << " " << height << "\">\n";
    out << "<defs>\n";
    out << "  <marker id=\"supportedByArrow\" markerWidth=\"10\" markerHeight=\"10\" refX=\"10\" refY=\"5\" "
           "orient=\"auto\" markerUnits=\"strokeWidth\">\n";
    out << "    <path d=\"M 0 0 L 10 5 L 0 10 Z\" fill=\"black\"/>\n";
    out << "  </marker>\n";
    out << "  <marker id=\"contextArrow\" markerWidth=\"10\" markerHeight=\"10\" refX=\"10\" refY=\"5\" "
           "orient=\"auto\" markerUnits=\"strokeWidth\">\n";
    out << "    <path d=\"M 0 0 L 10 5 L 0 10\" fill=\"none\" stroke=\"black\" stroke-width=\"1\"/>\n";
    out << "  </marker>\n";
    // Hollow (open) arrowhead, matching the canvas's challenge edge.
    out << "  <marker id=\"challengeArrow\" markerWidth=\"10\" markerHeight=\"10\" refX=\"10\" refY=\"5\" "
           "orient=\"auto\" markerUnits=\"strokeWidth\">\n";
    out << "    <path d=\"M 0 0 L 10 5 L 0 10\" fill=\"none\" stroke=\"" << kChallengeColor
        << "\" stroke-width=\"1\"/>\n";
    out << "  </marker>\n";
    out << "</defs>\n";
    out << "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    out << "<g id=\"diagram\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"13\" fill=\"none\" "
           "stroke=\"black\" stroke-width=\"1.2\">\n";

    // Midpoints are recorded as edges are drawn so a challenge aimed at a
    // relationship can land on the edge that represents it, and so relationship
    // ACP badges sit on the line they annotate.
    std::unordered_map<std::string, Point> midpoint_by_edge_id;
    std::vector<const GsnEdge*> challenges_to_edges;

    for (const GsnEdge& edge : diagram.edges) {
        auto from_it = node_by_id.find(edge.from_id);
        if (from_it == node_by_id.end())
            continue;
        if (!edge.to_edge_id.empty()) {
            challenges_to_edges.push_back(&edge);
            continue;
        }
        auto to_it = node_by_id.find(edge.to_id);
        if (to_it == node_by_id.end())
            continue;
        WriteEdge(out, edge, *from_it->second, *to_it->second);
        midpoint_by_edge_id[edge.id] = EdgeMidpoint(edge, *from_it->second, *to_it->second);
    }

    // A challenge may itself be challenged (GSN v3), so a challenge-to-edge can
    // target another one. Resolve repeatedly until no more midpoints appear;
    // anything still unresolved is a cycle or a dangling target and is dropped.
    bool resolved_any = true;
    while (resolved_any && !challenges_to_edges.empty()) {
        resolved_any = false;
        std::vector<const GsnEdge*> pending;
        for (const GsnEdge* edge : challenges_to_edges) {
            auto from_it = node_by_id.find(edge->from_id);
            auto target_it = midpoint_by_edge_id.find(edge->to_edge_id);
            if (from_it == node_by_id.end())
                continue;
            if (target_it == midpoint_by_edge_id.end()) {
                pending.push_back(edge);
                continue;
            }
            WriteChallengeToPoint(out, *edge, *from_it->second, target_it->second);
            midpoint_by_edge_id[edge->id] = Midpoint(Center(*from_it->second), target_it->second);
            resolved_any = true;
        }
        challenges_to_edges.swap(pending);
    }

    for (const GsnNode& node : diagram.nodes) {
        WriteNode(out, node);
    }

    for (const GsnEdge& edge : diagram.edges) {
        auto midpoint = midpoint_by_edge_id.find(edge.id);
        if (edge.acp_labels.empty() || midpoint == midpoint_by_edge_id.end())
            continue;
        out << "  <g class=\"gsn-acp-group\">\n";
        WriteAcpBadge(out, midpoint->second.x, midpoint->second.y, edge.acp_labels);
        out << "  </g>\n";
    }

    out << "</g>\n";
    out << "</svg>\n";
    return out.str();
}

} // namespace export_gsn
