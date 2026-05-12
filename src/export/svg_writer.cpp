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

std::vector<std::string> WrappedTextLines(const GsnNode& node) {
    const double available_width = node.kind == GsnNodeKind::Solution ? node.width * 0.62 : node.width - 32.0;
    const size_t max_chars = std::max<size_t>(8, static_cast<size_t>(available_width / 7.0));
    std::vector<std::string> lines;
    lines.push_back(node.id);
    for (const std::string& paragraph : SplitParagraphs(node.text)) {
        std::vector<std::string> wrapped = WrapParagraph(paragraph, max_chars);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    return lines;
}

void WriteText(std::ostringstream& out, const GsnNode& node, double x, double y) {
    const std::vector<std::string> lines = WrappedTextLines(node);
    out << "    <text x=\"" << x << "\" y=\"" << y << "\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\""
        << kFontSize << "\" fill=\"black\">\n";
    for (size_t i = 0; i < lines.size(); ++i) {
        out << "      <tspan x=\"" << x << "\" dy=\"" << (i == 0 ? 0.0 : kTextLineHeight) << "\">"
            << EscapeXml(lines[i]) << "</tspan>\n";
    }
    out << "    </text>\n";
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

void WriteEdge(std::ostringstream& out, const GsnEdge& edge, const GsnNode& from, const GsnNode& to) {
    if (edge.kind == GsnEdgeKind::SupportedBy) {
        const Point start = BottomCenter(from);
        const Point end = TopCenter(to);
        out << "  <path d=\"M " << start.x << " " << start.y << " L " << end.x << " " << end.y
            << "\" fill=\"none\" stroke=\"black\" stroke-width=\"1.2\" marker-end=\"url(#supportedByArrow)\"/>\n";
        return;
    }

    const bool to_right = (to.x + to.width / 2.0) >= (from.x + from.width / 2.0);
    const Point start = SidePoint(from, to_right);
    const Point end = SidePoint(to, !to_right);
    out << "  <path d=\"M " << start.x << " " << start.y << " L " << end.x << " " << end.y
        << "\" fill=\"none\" stroke=\"black\" stroke-width=\"1.2\" stroke-dasharray=\"6,4\" "
           "marker-end=\"url(#contextArrow)\"/>\n";
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
    out << "</defs>\n";
    out << "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    out << "<g id=\"diagram\" font-family=\"Arial, Helvetica, sans-serif\" font-size=\"13\" fill=\"none\" "
           "stroke=\"black\" stroke-width=\"1.2\">\n";

    for (const GsnEdge& edge : diagram.edges) {
        auto from_it = node_by_id.find(edge.from_id);
        auto to_it = node_by_id.find(edge.to_id);
        if (from_it == node_by_id.end() || to_it == node_by_id.end())
            continue;
        WriteEdge(out, edge, *from_it->second, *to_it->second);
    }
    for (const GsnNode& node : diagram.nodes) {
        WriteNode(out, node);
    }

    out << "</g>\n";
    out << "</svg>\n";
    return out.str();
}

} // namespace export_gsn
