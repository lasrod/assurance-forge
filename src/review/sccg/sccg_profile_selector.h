#pragma once

#include "core/assurance_tree.h"
#include "core/guideline_catalog.h"
#include "parser/guidelines_parser.h"
#include "parser/xml_parser.h"

#include <string>
#include <vector>

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
