#pragma once

#include <string>
#include <vector>

namespace export_gsn {

// Height of the module compartment an Away Goal carries across the bottom of
// its shape (GSN3-MOD-003). Shared, because the layout has to make room for it
// and the writer draws it; if the two disagreed the goal's statement would run
// under the divider and over the module name.
inline constexpr double kAwayModuleCompartmentHeight = 24.0;

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
    // GSN v3 dialectic challenge. Carried by a SACM relationship with
    // `isCounter = true`. It must never be projected as SupportedBy: a challenge
    // drawn as support inverts the meaning of the argument.
    Challenges,
};

struct GsnNode {
    // Stable technical id used by graph edges and the SVG DOM.
    std::string id;
    // User-facing GSN notation identifier rendered inside the node.
    std::string display_id;
    std::string source_gid;
    GsnNodeKind kind = GsnNodeKind::Goal;
    std::string title;
    std::string text;

    // The element requires support that has not been provided. Drawn as the GSN
    // hollow diamond below the shape.
    bool undeveloped = false;
    // The element is an abstract pattern element that remains to be
    // instantiated. Drawn as the GSN hollow triangle below the shape; when
    // combined with undeveloped, the two markers overlap.
    bool uninstantiated = false;
    // Identifiers of Assurance Claim Points attached to this element.
    std::vector<std::string> acp_labels;

    // GSN v3 Modular Extension (GSN3-MOD-003): non-empty only on an Away Goal,
    // naming the module the goal is defined in. Drawn in a compartment across
    // the bottom of the shape. The canvas is a separate renderer with its own
    // model, so this field existing there does not put it here.
    std::string away_module_identifier;

    // Where the evidence this node stands for is: the location recorded on the
    // Artifact/Resource the ArtifactReference cites. The projection copies it
    // verbatim; the exporter rewrites it into something an SVG reader can
    // follow from where the file is written (see LinkTargetForExport).
    std::string location;

    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct GsnEdge {
    std::string id;
    std::string from_id;
    // Target node. Empty only for a Challenges edge aimed at a relationship,
    // which uses `to_edge_id` instead.
    std::string to_id;
    // Set when a Challenges edge targets another relationship rather than an
    // element; the arrow lands on that edge's midpoint. GSN v3 permits
    // challenging a relationship, and a challenge may itself be challenged.
    std::string to_edge_id;
    GsnEdgeKind kind = GsnEdgeKind::SupportedBy;
    // Identifiers of Assurance Claim Points attached to this relationship.
    std::vector<std::string> acp_labels;
};

struct GsnDiagram {
    std::vector<GsnNode> nodes;
    std::vector<GsnEdge> edges;
};

} // namespace export_gsn
