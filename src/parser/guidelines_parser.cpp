#include "parser/guidelines_parser.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <exception>
#include <sstream>

namespace parser {

namespace {

std::string ReadString(const YAML::Node& node) {
    if (!node || node.IsNull()) return std::string();
    if (!node.IsScalar()) return std::string();
    return node.as<std::string>();
}

std::string ReadStringKey(const YAML::Node& node, const char* key) {
    if (!node || !node.IsMap()) return std::string();
    return ReadString(node[key]);
}

std::vector<std::string> ReadStringSequence(const YAML::Node& node) {
    std::vector<std::string> values;
    if (!node || !node.IsSequence()) return values;

    for (const auto& item : node) {
        std::string value = ReadString(item);
        if (!value.empty()) values.push_back(value);
    }
    return values;
}

bool RequireSequence(const YAML::Node& root, const char* key, std::string& error_message) {
    const YAML::Node node = root[key];
    if (!node || !node.IsSequence()) {
        error_message = std::string("Missing or invalid '") + key + "' section";
        return false;
    }
    return true;
}

GuidelinesDocumentMetadata ParseMetadata(const YAML::Node& node) {
    GuidelinesDocumentMetadata metadata;
    metadata.title = ReadStringKey(node, "title");
    metadata.copyright = ReadStringKey(node, "copyright");

    const YAML::Node license_node = node["license"];
    metadata.license.id = ReadStringKey(license_node, "id");
    metadata.license.name = ReadStringKey(license_node, "name");
    metadata.license.url = ReadStringKey(license_node, "url");

    metadata.purpose = ReadStringKey(node, "purpose");
    metadata.source_policy_summary = ReadStringKey(node["source_policy"], "summary");

    const YAML::Node method_guidance = node["method_application_guidance"];
    metadata.method_application_summary = ReadStringKey(method_guidance, "summary");
    const YAML::Node recommendations_node = method_guidance && method_guidance.IsMap()
        ? method_guidance["recommendations"]
        : YAML::Node();
    if (recommendations_node && recommendations_node.IsSequence()) {
        for (const auto& recommendation_node : recommendations_node) {
            GuidelinesRecommendation recommendation;
            recommendation.method = ReadStringKey(recommendation_node, "method");
            recommendation.recommendation = ReadStringKey(recommendation_node, "recommendation");
            if (!recommendation.method.empty() || !recommendation.recommendation.empty()) {
                metadata.recommendations.push_back(recommendation);
            }
        }
    }

    const YAML::Node id_scheme_node = node["id_scheme"];
    if (id_scheme_node && id_scheme_node.IsSequence()) {
        for (const auto& entry_node : id_scheme_node) {
            GuidelinesIdSchemeEntry entry;
            entry.prefix = ReadStringKey(entry_node, "prefix");
            entry.meaning = ReadStringKey(entry_node, "meaning");
            if (!entry.prefix.empty() || !entry.meaning.empty()) metadata.id_scheme.push_back(entry);
        }
    }

    metadata.required_guideline_sections = ReadStringSequence(node["required_guideline_sections"]);
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

GuidelineToolGuidance ParseToolGuidance(const YAML::Node& node) {
    GuidelineToolGuidance tool_guidance;
    if (!node || !node.IsMap()) return tool_guidance;

    tool_guidance.applicable_elements = ReadStringSequence(node["applicable_elements"]);
    tool_guidance.detection_hints = ReadStringSequence(node["detection_hints"]);

    const YAML::Node checks_node = node["suggested_checks"];
    if (checks_node && checks_node.IsSequence()) {
        for (const auto& check_node : checks_node) {
            SuggestedCheck check;
            check.id = ReadStringKey(check_node, "id");
            check.description = ReadStringKey(check_node, "description");
            if (!check.id.empty() || !check.description.empty()) {
                tool_guidance.suggested_checks.push_back(check);
            }
        }
    }

    return tool_guidance;
}

std::vector<GuidelineReference> ParseGuidelineReferences(const YAML::Node& node) {
    std::vector<GuidelineReference> references;
    if (!node || !node.IsSequence()) return references;

    for (const auto& reference_node : node) {
        GuidelineReference reference;
        reference.source_id = ReadStringKey(reference_node, "source_id");
        reference.display_name = ReadStringKey(reference_node, "display_name");
        reference.clauses = ReadStringSequence(reference_node["clauses"]);
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
        guideline.guideline = ReadStringKey(guideline_node, "guideline");
        guideline.why = ReadStringKey(guideline_node, "why");
        guideline.review_prompts = ReadStringSequence(guideline_node["review_prompts"]);

        const YAML::Node example_node = guideline_node["example"];
        guideline.example.bad = ReadStringKey(example_node, "bad");
        guideline.example.problem = ReadStringKey(example_node, "problem");
        guideline.example.good = ReadStringKey(example_node, "good");

        guideline.references = ParseGuidelineReferences(guideline_node["references"]);
        guideline.tool_guidance = ParseToolGuidance(guideline_node["tool_guidance"]);

        if (guideline.id.empty() || guideline.category.empty() || guideline.title.empty()) {
            std::ostringstream out;
            out << "Guideline at index " << index << " is missing id, category, or title";
            error_message = out.str();
            return {};
        }

        guidelines.push_back(guideline);
        ++index;
    }
    return guidelines;
}

GuidelinesParseResult ParseRoot(const YAML::Node& root) {
    GuidelinesParseResult result;
    if (!root || !root.IsMap()) {
        result.error_message = "Guidelines YAML root must be a map";
        return result;
    }

    result.document.schema_version = ReadStringKey(root, "schema_version");
    if (result.document.schema_version.empty()) {
        result.error_message = "Missing schema_version";
        return result;
    }

    const YAML::Node metadata_node = root["document"];
    if (!metadata_node || !metadata_node.IsMap()) {
        result.error_message = "Missing or invalid 'document' section";
        return result;
    }
    result.document.metadata = ParseMetadata(metadata_node);
    if (result.document.metadata.title.empty()) {
        result.error_message = "Document metadata is missing title";
        return result;
    }

    std::string error_message;
    if (!RequireSequence(root, "reference_sources", error_message) ||
        !RequireSequence(root, "categories", error_message) ||
        !RequireSequence(root, "guidelines", error_message)) {
        result.error_message = error_message;
        return result;
    }

    result.document.reference_sources = ParseReferenceSources(root["reference_sources"], error_message);
    if (!error_message.empty()) {
        result.error_message = error_message;
        return result;
    }

    result.document.categories = ParseCategories(root["categories"], error_message);
    if (!error_message.empty()) {
        result.error_message = error_message;
        return result;
    }

    result.document.guidelines = ParseGuidelines(root["guidelines"], error_message);
    if (!error_message.empty()) {
        result.error_message = error_message;
        return result;
    }

    result.success = true;
    return result;
}

}  // namespace

const Guideline* GuidelinesDocument::FindGuidelineById(const std::string& id) const {
    auto found = std::find_if(guidelines.begin(), guidelines.end(), [&](const Guideline& guideline) {
        return guideline.id == id;
    });
    return found == guidelines.end() ? nullptr : &(*found);
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesByCategory(const std::string& category_id) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        if (guideline.category == category_id) matches.push_back(&guideline);
    }
    return matches;
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesByApplicableElement(const std::string& element_name) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        const auto& elements = guideline.tool_guidance.applicable_elements;
        if (std::find(elements.begin(), elements.end(), element_name) != elements.end()) {
            matches.push_back(&guideline);
        }
    }
    return matches;
}

std::vector<const Guideline*> GuidelinesDocument::FindGuidelinesBySuggestedCheckId(const std::string& check_id) const {
    std::vector<const Guideline*> matches;
    for (const auto& guideline : guidelines) {
        for (const auto& check : guideline.tool_guidance.suggested_checks) {
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
        for (const auto& check : guideline.tool_guidance.suggested_checks) {
            if (check.id == check_id) return &check;
        }
    }
    return nullptr;
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

}  // namespace parser