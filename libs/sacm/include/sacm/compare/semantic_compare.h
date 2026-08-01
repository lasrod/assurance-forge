#pragma once

#include <string>
#include <vector>

namespace sacm::model {
class Document;
}

namespace sacm::compare {

// One semantic difference between two documents.
struct SemanticDifference {
    // Containment path of the differing element, e.g. "acp_1/argpkg_1/claim_2".
    std::string path;
    // What differs: "kind", "attribute", "reference", "missing", "extra", ...
    std::string category;
    std::string message;
};

// Semantic document comparison (SACM23-RT-001/002): pairs elements by ID
// within their containment, tolerates ordering differences, normalizes
// attribute defaults, and compares language maps and reference multisets.
// Empty result means semantically equivalent.
std::vector<SemanticDifference> semantic_compare(const model::Document& left, const model::Document& right);

} // namespace sacm::compare
