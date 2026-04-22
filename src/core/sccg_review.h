#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace core {

struct SccgGuideline {
    std::string id;
    std::string category;
    std::string title;
    std::string guideline;
};

struct ElementSccgTag {
    std::string guideline_id;
    std::string status;
    std::string severity;
    std::string comment;
};

bool LoadSccgCatalog(const std::string& catalog_path);
const std::vector<SccgGuideline>& GetSccgGuidelines();
const SccgGuideline* FindSccgGuidelineById(const std::string& guideline_id);

void ClearReviewData();

bool LoadReviewSidecarForModel(const std::string& model_path);
bool SaveReviewSidecarForModel(const std::string& model_path);

std::vector<ElementSccgTag> GetTagsForElement(const std::string& element_id);
void SetTagsForElement(const std::string& element_id, const std::vector<ElementSccgTag>& tags);

bool HasPendingReviewChanges();

int CountTaggedElements();
int CountOpenFindings();

}  // namespace core
