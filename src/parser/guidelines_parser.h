#pragma once

#include <string>
#include <vector>

namespace parser {

struct GuidelinesLicense {
    std::string id;
    std::string name;
    std::string url;
};

struct GuidelinesRecommendation {
    std::string method;
    std::string recommendation;
};

struct GuidelinesIdSchemeEntry {
    std::string prefix;
    std::string meaning;
};

struct GuidelinesDocumentMetadata {
    std::string title;
    std::string copyright;
    GuidelinesLicense license;
    std::string purpose;
    std::string source_policy_summary;
    std::string method_application_summary;
    std::vector<GuidelinesRecommendation> recommendations;
    std::vector<GuidelinesIdSchemeEntry> id_scheme;
    std::vector<std::string> required_guideline_sections;
};

struct ReferenceSource {
    std::string id;
    std::string display_name;
    std::string type;
};

struct GuidelineCategory {
    std::string id;
    std::string title;
    std::string index_title;
    std::string description;
};

struct GuidelineExample {
    std::string bad;
    std::string problem;
    std::string good;
};

struct GuidelineReference {
    std::string source_id;
    std::string display_name;
    std::vector<std::string> clauses;
};

struct SuggestedCheck {
    std::string id;
    std::string description;
};

struct GuidelineToolGuidance {
    std::vector<std::string> applicable_elements;
    std::vector<std::string> detection_hints;
    std::vector<SuggestedCheck> suggested_checks;
};

struct Guideline {
    std::string id;
    std::string category;
    std::string title;
    std::string guideline;
    std::string why;
    std::vector<std::string> review_prompts;
    GuidelineExample example;
    std::vector<GuidelineReference> references;
    GuidelineToolGuidance tool_guidance;
};

struct GuidelinesDocument {
    std::string schema_version;
    GuidelinesDocumentMetadata metadata;
    std::vector<ReferenceSource> reference_sources;
    std::vector<GuidelineCategory> categories;
    std::vector<Guideline> guidelines;

    const Guideline* FindGuidelineById(const std::string& id) const;
    std::vector<const Guideline*> FindGuidelinesByCategory(const std::string& category_id) const;
    std::vector<const Guideline*> FindGuidelinesByApplicableElement(const std::string& element_name) const;
    std::vector<const Guideline*> FindGuidelinesBySuggestedCheckId(const std::string& check_id) const;
    const SuggestedCheck* FindSuggestedCheckById(const std::string& check_id) const;
    const ReferenceSource* FindReferenceSourceById(const std::string& source_id) const;
    const GuidelineCategory* FindCategoryById(const std::string& category_id) const;
};

struct GuidelinesParseResult {
    bool success = false;
    std::string error_message;
    GuidelinesDocument document;
};

class GuidelinesParser {
public:
    static GuidelinesParseResult ParseFile(const std::string& file_path);
    static GuidelinesParseResult ParseString(const std::string& yaml_content);
};

}  // namespace parser