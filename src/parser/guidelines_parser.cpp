#include "parser/guidelines_parser.h"

#include <algorithm>
#include <exception>
#include <sstream>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace parser {

namespace {

bool IsDefinedNode(const YAML::Node& node) {
    return node.IsDefined() && !node.IsNull();
}

YAML::Node ReadMapValue(const YAML::Node& node, const char* key) {
    if (!IsDefinedNode(node) || !node.IsMap())
        return YAML::Node();
    for (const auto& item : node) {
        if (item.first.IsScalar() && item.first.as<std::string>() == key)
            return item.second;
    }
    return YAML::Node();
}

std::string ReadString(const YAML::Node& node) {
    if (!IsDefinedNode(node))
        return std::string();
    if (!node.IsScalar())
        return std::string();
    return node.as<std::string>();
}

std::string ReadStringKey(const YAML::Node& node, const char* key) {
    if (!IsDefinedNode(node) || !node.IsMap())
        return std::string();
    return ReadString(ReadMapValue(node, key));
}

std::string ReadStringKeyFallback(const YAML::Node& node, const char* primary_key, const char* fallback_key) {
    std::string value = ReadStringKey(node, primary_key);
    return value.empty() ? ReadStringKey(node, fallback_key) : value;
}

std::vector<std::string> ReadStringSequence(const YAML::Node& node) {
    std::vector<std::string> values;
    if (!IsDefinedNode(node) || !node.IsSequence())
        return values;

    for (const auto& item : node) {
        std::string value = ReadString(item);
        if (!value.empty())
            values.push_back(value);
    }
    return values;
}

bool RequireSequence(const YAML::Node& root, const char* key, std::string& error_message) {
    const YAML::Node node = ReadMapValue(root, key);
    if (!IsDefinedNode(node) || !node.IsSequence()) {
        error_message = std::string("Missing or invalid '") + key + "' section";
        return false;
    }
    return true;
}

YAML::Node ReadSectionFallback(const YAML::Node& root, const char* primary_key, const char* fallback_key) {
    const YAML::Node primary = ReadMapValue(root, primary_key);
    if (IsDefinedNode(primary))
        return primary;
    if (const YAML::Node tool_support = ReadMapValue(root, "tool_support");
        IsDefinedNode(tool_support) && tool_support.IsMap()) {
        return ReadMapValue(tool_support, fallback_key);
    }
    return YAML::Node();
}

GuidelinesDocumentMetadata ParseMetadata(const YAML::Node& root) {
    GuidelinesDocumentMetadata metadata;
    const YAML::Node document_node = ReadMapValue(root, "document");
    const bool has_document = IsDefinedNode(document_node) && document_node.IsMap();
    if (has_document) {
        metadata.title = ReadStringKey(document_node, "title");
        metadata.copyright = ReadStringKey(document_node, "copyright");

        const YAML::Node license_node = ReadMapValue(document_node, "license");
        metadata.license.id = ReadStringKey(license_node, "id");
        metadata.license.name = ReadStringKey(license_node, "name");
        metadata.license.url = ReadStringKey(license_node, "url");

        metadata.purpose = ReadStringKey(document_node, "purpose");
    }

    metadata.source_policy_summary = ReadStringKey(ReadMapValue(root, "source_policy"), "summary");
    if (metadata.source_policy_summary.empty() && has_document) {
        metadata.source_policy_summary = ReadStringKey(ReadMapValue(document_node, "source_policy"), "summary");
    }

    YAML::Node method_guidance = ReadMapValue(root, "method_application_guidance");
    if (!IsDefinedNode(method_guidance) && has_document) {
        method_guidance = ReadMapValue(document_node, "method_application_guidance");
    }
    metadata.method_application_summary = ReadStringKey(method_guidance, "summary");
    const YAML::Node recommendations_node = IsDefinedNode(method_guidance) && method_guidance.IsMap()
                                                ? ReadMapValue(method_guidance, "recommendations")
                                                : YAML::Node();
    if (IsDefinedNode(recommendations_node) && recommendations_node.IsSequence()) {
        for (const auto& recommendation_node : recommendations_node) {
            GuidelinesRecommendation recommendation;
            recommendation.method = ReadStringKey(recommendation_node, "method");
            recommendation.recommendation = ReadStringKey(recommendation_node, "recommendation");
            if (!recommendation.method.empty() || !recommendation.recommendation.empty()) {
                metadata.recommendations.push_back(recommendation);
            }
        }
    }

    YAML::Node id_scheme_node = ReadMapValue(root, "id_scheme");
    if (!IsDefinedNode(id_scheme_node) && has_document)
        id_scheme_node = ReadMapValue(document_node, "id_scheme");
    if (IsDefinedNode(id_scheme_node) && id_scheme_node.IsSequence()) {
        for (const auto& entry_node : id_scheme_node) {
            GuidelinesIdSchemeEntry entry;
            entry.prefix = ReadStringKey(entry_node, "prefix");
            entry.meaning = ReadStringKey(entry_node, "meaning");
            if (!entry.prefix.empty() || !entry.meaning.empty())
                metadata.id_scheme.push_back(entry);
        }
    }

    metadata.required_guideline_sections = ReadStringSequence(ReadMapValue(root, "required_guideline_sections"));
    if (metadata.required_guideline_sections.empty() && has_document) {
        metadata.required_guideline_sections =
            ReadStringSequence(ReadMapValue(document_node, "required_guideline_sections"));
    }
    return metadata;
}

std::vector<ReferenceSource> ParseReferenceSources(const YAML::Node& node, std::string& error_message) {
    std::vector<ReferenceSource> reference_sources;
    int index = 0;
    for (const auto& source_node : node) {
        ReferenceSource source;
        source.id = ReadStringKey(source_node, "id");
        source.display_name = ReadStringKey(source_node, "display_name");
        source.type = ReadStringKey(source_node, "type");
        if (source.id.empty() || source.display_name.empty()) {
            std::ostringstream out;
            out << "Reference source at index " << index << " is missing id or display_name";
            error_message = out.str();
            return {};
        }
        reference_sources.push_back(source);
        ++index;
    }
    return reference_sources;
}

std::vector<GuidelineCategory> ParseCategories(const YAML::Node& node, std::string& error_message) {
    std::vector<GuidelineCategory> categories;
    int index = 0;
    for (const auto& category_node : node) {
        GuidelineCategory category;
        category.id = ReadStringKey(category_node, "id");
        category.title = ReadStringKey(category_node, "title");
        category.index_title = ReadStringKey(category_node, "index_title");
        category.description = ReadStringKey(category_node, "description");
        if (category.id.empty() || category.title.empty()) {
            std::ostringstream out;
            out << "Category at index " << index << " is missing id or title";
            error_message = out.str();
            return {};
        }
        categories.push_back(category);
        ++index;
    }
    return categories;
}

GuidelineTool ParseTool(const YAML::Node& node) {
    GuidelineTool tool;
    if (!IsDefinedNode(node) || !node.IsMap())
        return tool;

    tool.applicable_elements = ReadStringSequence(ReadMapValue(node, "applicable_elements"));
    tool.detection_hints = ReadStringSequence(ReadMapValue(node, "detection_hints"));

    const YAML::Node checks_node = ReadMapValue(node, "suggested_checks");
    if (IsDefinedNode(checks_node) && checks_node.IsSequence()) {
        for (const auto& check_node : checks_node) {
            SuggestedCheck check;
            check.id = ReadStringKey(check_node, "id");
            check.description = ReadStringKey(check_node, "description");
            if (!check.id.empty() || !check.description.empty()) {
                tool.suggested_checks.push_back(check);
            }
        }
    }

    return tool;
}

std::vector<GuidelineReference> ParseGuidelineReferences(const YAML::Node& node) {
    std::vector<GuidelineReference> references;
    if (!IsDefinedNode(node) || !node.IsSequence())
        return references;

    for (const auto& reference_node : node) {
        GuidelineReference reference;
        reference.source_id = ReadStringKey(reference_node, "source_id");
        reference.display_name = ReadStringKey(reference_node, "display_name");
        reference.clauses = ReadStringSequence(ReadMapValue(reference_node, "clauses"));
        if (!reference.source_id.empty() || !reference.display_name.empty() || !reference.clauses.empty()) {
            references.push_back(reference);
        }
    }

    return references;
}

std::vector<Guideline> ParseGuidelines(const YAML::Node& node, std::string& error_message) {
    std::vector<Guideline> guidelines;
    int index = 0;
    for (const auto& guideline_node : node) {
        Guideline guideline;
        guideline.id = ReadStringKey(guideline_node, "id");
        guideline.category = ReadStringKey(guideline_node, "category");
        guideline.title = ReadStringKey(guideline_node, "title");
        guideline.statement = ReadStringKeyFallback(guideline_node, "statement", "guideline");
        guideline.rationale = ReadStringKeyFallback(guideline_node, "rationale", "why");
        guideline.review_prompts = ReadStringSequence(ReadMapValue(guideline_node, "review_prompts"));

        YAML::Node examples_node = ReadMapValue(guideline_node, "examples");
        if (!IsDefinedNode(examples_node))
            examples_node = ReadMapValue(guideline_node, "example");
        guideline.examples.bad = ReadStringKey(examples_node, "bad");
        guideline.examples.problem = ReadStringKey(examples_node, "problem");
        guideline.examples.good = ReadStringKey(examples_node, "good");

        guideline.references = ParseGuidelineReferences(ReadMapValue(guideline_node, "references"));
        YAML::Node tool_node = ReadMapValue(guideline_node, "tool");
        if (!IsDefinedNode(tool_node))
            tool_node = ReadMapValue(guideline_node, "tool_guidance");
        guideline.tool = ParseTool(tool_node);

        if (guideline.id.empty() || guideline.category.empty() || guideline.title.empty() ||
            guideline.statement.empty()) {
            std::ostringstream out;
            out << "Guideline at index " << index << " is missing id, category, title, or statement";
            error_message = out.str();
            return {};
        }

        guidelines.push_back(guideline);
        ++index;
    }
    return guidelines;
}

std::vector<ReviewProfile> ParseReviewProfiles(const YAML::Node& node) {
    std::vector<ReviewProfile> review_profiles;
    if (!IsDefinedNode(node) || !node.IsSequence())
        return review_profiles;

    for (const auto& profile_node : node) {
        ReviewProfile profile;
        profile.id = ReadStringKey(profile_node, "id");
        profile.display_name = ReadStringKey(profile_node, "display_name");
        profile.description = ReadStringKey(profile_node, "description");
        profile.applies_to = ReadStringSequence(ReadMapValue(profile_node, "applies_to"));
        profile.guideline_ids = ReadStringSequence(ReadMapValue(profile_node, "guideline_ids"));
        profile.required_data = ReadStringSequence(ReadMapValue(profile_node, "required_data"));
        profile.optional_data = ReadStringSequence(ReadMapValue(profile_node, "optional_data"));
        if (!profile.id.empty())
            review_profiles.push_back(std::move(profile));
    }

    return review_profiles;
}

std::vector<DataPackage> ParseDataPackages(const YAML::Node& node) {
    std::vector<DataPackage> data_packages;
    if (!IsDefinedNode(node) || !node.IsSequence())
        return data_packages;

    for (const auto& package_node : node) {
        DataPackage data_package;
        data_package.id = ReadStringKey(package_node, "id");
        data_package.display_name = ReadStringKey(package_node, "display_name");
        data_package.description = ReadStringKey(package_node, "description");
        data_package.required_fields = ReadStringSequence(ReadMapValue(package_node, "required_fields"));
        data_package.optional_fields = ReadStringSequence(ReadMapValue(package_node, "optional_fields"));
        if (!data_package.id.empty())
            data_packages.push_back(std::move(data_package));
    }

    return data_packages;
}

std::vector<Precheck> ParsePrechecks(const YAML::Node& node) {
    std::vector<Precheck> prechecks;
    if (!IsDefinedNode(node) || !node.IsSequence())
        return prechecks;

    for (const auto& precheck_node : node) {
        Precheck precheck;
        precheck.id = ReadStringKey(precheck_node, "id");
        precheck.display_name = ReadStringKey(precheck_node, "display_name");
        precheck.related_guideline_ids = ReadStringSequence(ReadMapValue(precheck_node, "related_guideline_ids"));
        precheck.expected_data = ReadStringSequence(ReadMapValue(precheck_node, "expected_data"));
        precheck.result_type = ReadStringKey(precheck_node, "result_type");
        precheck.description = ReadStringKey(precheck_node, "description");
        precheck.interpretation = ReadStringKey(precheck_node, "interpretation");
        if (!precheck.id.empty())
            prechecks.push_back(std::move(precheck));
    }

    return prechecks;
}

GuidelinesParseResult ParseRoot(const YAML::Node& root) {
    GuidelinesParseResult result;
    if (!IsDefinedNode(root) || !root.IsMap()) {
        result.error_message = "SCCG catalog YAML root must be a map";
        return result;
    }

    result.document.schema_version = ReadStringKey(root, "schema_version");
    result.document.sccg_version = ReadStringKey(root, "sccg_version");
    if (result.document.schema_version.empty()) {
        result.error_message = "Missing schema_version";
        return result;
    }

    result.document.metadata = ParseMetadata(root);

    std::string error_message;
    if (!RequireSequence(root, "guidelines", error_message)) {
        result.error_message = error_message;
        return result;
    }

    if (IsDefinedNode(ReadMapValue(root, "reference_sources"))) {
        result.document.reference_sources =
            ParseReferenceSources(ReadMapValue(root, "reference_sources"), error_message);
        if (!error_message.empty()) {
            result.error_message = error_message;
            return result;
        }
    }

    if (IsDefinedNode(ReadMapValue(root, "categories"))) {
        result.document.categories = ParseCategories(ReadMapValue(root, "categories"), error_message);
        if (!error_message.empty()) {
            result.error_message = error_message;
            return result;
        }
    }

    result.document.guidelines = ParseGuidelines(ReadMapValue(root, "guidelines"), error_message);
    if (!error_message.empty()) {
        result.error_message = error_message;
        return result;
    }

    result.document.review_profiles =
        ParseReviewProfiles(ReadSectionFallback(root, "review_profiles", "review_profiles"));
    result.document.data_packages = ParseDataPackages(ReadSectionFallback(root, "data_packages", "data_packages"));
    result.document.prechecks = ParsePrechecks(ReadSectionFallback(root, "prechecks", "prechecks"));

    result.success = true;
    return result;
}

} // namespace

const Guideline* GuidelinesDocument::FindGuidelineById(const std::string& id) const {
    auto found = std::find_if(
        guidelines.begin(), guidelines.end(), [&](const Guideline& guideline) { return guideline.id == id; });
    return found == guidelines.end() ? nullptr : &(*found);
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesByCategory(const std::string& category_id) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        if (guideline.category == category_id)
            matches.push_back(&guideline);
    }
    return matches;
}

std::vector<const Guideline*>
GuidelinesDocument::FindGuidelinesByApplicableElement(const std::string& element_name) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        const auto& elements = guideline.tool.applicable_elements;
        if (std::find(elements.begin(), elements.end(), element_name) != elements.end()) {
            matches.push_back(&guideline);
        }
    }
    return matches;
}

std::vector<const Guideline*>
GuidelinesDocument::FindGuidelinesByReviewProfile(const std::string& review_profile_id) const {
    std::vector<const Guideline*> matches;
    const ReviewProfile* profile = FindReviewProfileById(review_profile_id);
    if (!profile)
        return matches;

    for (const std::string& guideline_id : profile->guideline_ids) {
        const Guideline* guideline = FindGuidelineById(guideline_id);
        if (guideline)
            matches.push_back(guideline);
    }
    return matches;
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesBySuggestedCheckId(const std::string& check_id) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        for (const auto& check : guideline.tool.suggested_checks) {
            if (check.id == check_id) {
                matches.push_back(&guideline);
                break;
            }
        }
    }
    return matches;
}

const SuggestedCheck* GuidelinesDocument::FindSuggestedCheckById(const std::string& check_id) const {
    for (const auto& guideline : guidelines) {
        for (const auto& check : guideline.tool.suggested_checks) {
            if (check.id == check_id)
                return &check;
        }
    }
    return nullptr;
}

const ReviewProfile* GuidelinesDocument::FindReviewProfileById(const std::string& id) const {
    auto found = std::find_if(
        review_profiles.begin(), review_profiles.end(), [&](const ReviewProfile& profile) { return profile.id == id; });
    return found == review_profiles.end() ? nullptr : &(*found);
}

const ReferenceSource* GuidelinesDocument::FindReferenceSourceById(const std::string& source_id) const {
    auto found = std::find_if(reference_sources.begin(), reference_sources.end(), [&](const ReferenceSource& source) {
        return source.id == source_id;
    });
    return found == reference_sources.end() ? nullptr : &(*found);
}

const GuidelineCategory* GuidelinesDocument::FindCategoryById(const std::string& category_id) const {
    auto found = std::find_if(categories.begin(), categories.end(), [&](const GuidelineCategory& category) {
        return category.id == category_id;
    });
    return found == categories.end() ? nullptr : &(*found);
}

GuidelinesParseResult GuidelinesParser::ParseFile(const std::string& file_path) {
    try {
        return ParseRoot(YAML::LoadFile(file_path));
    } catch (const YAML::Exception& exception) {
        GuidelinesParseResult result;
        result.error_message = exception.what();
        return result;
    } catch (const std::exception& exception) {
        GuidelinesParseResult result;
        result.error_message = exception.what();
        return result;
    }
}

GuidelinesParseResult GuidelinesParser::ParseString(const std::string& yaml_content) {
    try {
        return ParseRoot(YAML::Load(yaml_content));
    } catch (const YAML::Exception& exception) {
        GuidelinesParseResult result;
        result.error_message = exception.what();
        return result;
    } catch (const std::exception& exception) {
        GuidelinesParseResult result;
        result.error_message = exception.what();
        return result;
    }
}

} // namespace parser