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

struct GuidelineTool {
    std::vector<std::string> applicable_elements;
    std::vector<std::string> detection_hints;
    std::vector<SuggestedCheck> suggested_checks;
};

struct Guideline {
    std::string id;
    std::string category;
    std::string title;
    std::string statement;
    std::string rationale;
    std::vector<std::string> review_prompts;
    GuidelineExample examples;
    std::vector<GuidelineReference> references;
    GuidelineTool tool;
};

struct ReviewProfile {
    std::string id;
    std::string display_name;
    std::string description;
    std::vector<std::string> applies_to;
    std::vector<std::string> guideline_ids;
    std::vector<std::string> required_data;
    std::vector<std::string> optional_data;
};

struct DataPackage {
    std::string id;
    std::string display_name;
    std::string description;
    std::vector<std::string> required_fields;
    std::vector<std::string> optional_fields;
};

struct Precheck {
    std::string id;
    std::string display_name;
    std::vector<std::string> related_guideline_ids;
    std::vector<std::string> expected_data;
    std::string result_type;
    std::string description;
    std::string interpretation;
};

struct GuidelinesDocument {
    std::string schema_version;
    std::string sccg_version;
    GuidelinesDocumentMetadata metadata;
    std::vector<ReferenceSource> reference_sources;
    std::vector<GuidelineCategory> categories;
    std::vector<Guideline> guidelines;
    std::vector<ReviewProfile> review_profiles;
    std::vector<DataPackage> data_packages;
    std::vector<Precheck> prechecks;

    const Guideline* FindGuidelineById(const std::string& id) const;
    std::vector<const Guideline*> FindGuidelinesByCategory(const std::string& category_id) const;
    std::vector<const Guideline*> FindGuidelinesByApplicableElement(const std::string& element_name) const;
    std::vector<const Guideline*> FindGuidelinesByReviewProfile(const std::string& review_profile_id) const;
    std::vector<const Guideline*> FindGuidelinesBySuggestedCheckId(const std::string& check_id) const;
    const SuggestedCheck* FindSuggestedCheckById(const std::string& check_id) const;
    const ReviewProfile* FindReviewProfileById(const std::string& id) const;
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

} // namespace parser