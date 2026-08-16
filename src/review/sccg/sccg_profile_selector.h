#pragma once

#include <string>
#include <vector>

// Declarations only: this header is part of the controller's include surface,
// so it names the types it touches instead of pulling in the parser and tree
// headers behind them. The element parameter is spelled core::SacmElement --
// the type parser::SacmElement aliases (core/sacm_model.h) -- rather than
// restating another layer's alias here, which would break the moment the
// parser-to-core rename makes them distinct.
namespace core {
struct GuidelineCatalog;
struct SacmElement;
struct TreeNode;
} // namespace core

namespace parser {
struct Guideline;
struct ReviewProfile;
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
                                                         const core::SacmElement& element,
                                                         const core::TreeNode* node = nullptr);

} // namespace review
