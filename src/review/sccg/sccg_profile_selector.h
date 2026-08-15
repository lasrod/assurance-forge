#pragma once

#include <string>
#include <vector>

// Declarations only: this header is part of the controller's include surface,
// so it names the types it touches instead of pulling in the parser and tree
// headers behind them. `parser::SacmElement` is currently an alias into
// `core` (see core/sacm_model.h); the alias is restated here identically,
// which is legal and disappears with the parser-to-core rename.
namespace core {
struct GuidelineCatalog;
struct SacmElement;
struct TreeNode;
} // namespace core

namespace parser {
struct Guideline;
struct ReviewProfile;
using SacmElement = core::SacmElement;
} // namespace parser

namespace review {

struct AiReviewGuidelineSelection {
    const parser::ReviewProfile* review_profile = nullptr;
    std::vector<const parser::Guideline*> guidelines;
    std::string error_message;
};

AiReviewGuidelineSelection SelectReviewProfileGuidelines(const core::GuidelineCatalog& guideline_catalog,
                                                         const std::string& review_profile_id);
AiReviewGuidelineSelection SelectReviewProfileForElement(const core::GuidelineCatalog& guideline_catalog,
                                                         const parser::SacmElement& element,
                                                         const core::TreeNode* node = nullptr);

} // namespace review
