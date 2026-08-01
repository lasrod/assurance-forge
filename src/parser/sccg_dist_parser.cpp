#include "parser/sccg_dist_parser.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

namespace parser {
namespace {

using json = nlohmann::json;

bool ReadJsonFile(const std::filesystem::path& path, json& out_json, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Could not open " + path.string();
        return false;
    }

    try {
        input >> out_json;
        return true;
    } catch (const json::exception& exception) {
        error = path.filename().string() + ": " + exception.what();
        return false;
    }
}

std::string StringValue(const json& object, const char* key) {
    if (!object.is_object())
        return {};
    auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return {};
    return found->get<std::string>();
}

std::vector<std::string> StringArrayValue(const json& object, const char* key) {
    std::vector<std::string> values;
    if (!object.is_object())
        return values;
    auto found = object.find(key);
    if (found == object.end() || !found->is_array())
        return values;
    for (const json& item : *found) {
        if (item.is_string())
            values.push_back(item.get<std::string>());
    }
    return values;
}

GuidelineReference ParseReference(const json& object) {
    GuidelineReference reference;
    reference.source_id = StringValue(object, "source_id");
    reference.display_name = StringValue(object, "display_name");
    reference.clauses = StringArrayValue(object, "clauses");
    return reference;
}

GuidelineTool ParseTool(const json& object) {
    GuidelineTool tool;
    if (!object.is_object())
        return tool;

    tool.applicable_elements = StringArrayValue(object, "applicable_elements");
    tool.detection_hints = StringArrayValue(object, "detection_hints");
    auto checks = object.find("suggested_checks");
    if (checks != object.end() && checks->is_array()) {
        for (const json& check_json : *checks) {
            SuggestedCheck check;
            check.id = StringValue(check_json, "id");
            check.description = StringValue(check_json, "description");
            if (!check.id.empty() || !check.description.empty())
                tool.suggested_checks.push_back(std::move(check));
        }
    }
    return tool;
}

Guideline ParseRuleRecord(const json& object) {
    Guideline guideline;
    guideline.id = StringValue(object, "id");
    guideline.rule_id = StringValue(object, "rule_id");
    guideline.category = StringValue(object, "category");
    guideline.category_id = StringValue(object, "category_id");
    guideline.title = StringValue(object, "title");
    guideline.statement = StringValue(object, "statement");
    guideline.rationale = StringValue(object, "rationale");
    guideline.review_prompts = StringArrayValue(object, "review_prompts");
    guideline.reference_source_ids = StringArrayValue(object, "reference_source_ids");
    guideline.review_profile_ids = StringArrayValue(object, "review_profile_ids");
    guideline.data_package_ids = StringArrayValue(object, "data_package_ids");
    guideline.schema_version = StringValue(object, "schema_version");
    guideline.sccg_version = StringValue(object, "sccg_version");

    const json examples = object.value("examples", json::object());
    guideline.examples.bad = StringValue(examples, "bad");
    guideline.examples.problem = StringValue(examples, "problem");
    guideline.examples.good = StringValue(examples, "good");

    auto references = object.find("references");
    if (references != object.end() && references->is_array()) {
        for (const json& reference_json : *references) {
            guideline.references.push_back(ParseReference(reference_json));
        }
    }
    guideline.tool = ParseTool(object.value("tool", json::object()));
    if (guideline.rule_id.empty())
        guideline.rule_id = guideline.id;
    if (guideline.category.empty())
        guideline.category = guideline.category_id;
    if (guideline.category_id.empty())
        guideline.category_id = guideline.category;
    return guideline;
}

bool ParseRulesJsonl(const std::filesystem::path& path, GuidelinesDocument& document, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Could not open " + path.string();
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty())
            continue;
        try {
            json record = json::parse(line);
            Guideline guideline = ParseRuleRecord(record);
            if (guideline.id.empty() || guideline.title.empty() || guideline.statement.empty() ||
                guideline.rationale.empty()) {
                std::ostringstream out;
                out << path.filename().string() << ": rule line " << line_number
                    << " is missing id, title, statement, or rationale";
                error = out.str();
                return false;
            }
            if (document.schema_version.empty())
                document.schema_version = guideline.schema_version;
            if (document.sccg_version.empty())
                document.sccg_version = guideline.sccg_version;
            document.guidelines.push_back(std::move(guideline));
        } catch (const json::exception& exception) {
            std::ostringstream out;
            out << path.filename().string() << ": JSONL parse error on line " << line_number << ": "
                << exception.what();
            error = out.str();
            return false;
        }
    }

    if (document.guidelines.empty()) {
        error = path.filename().string() + " contained no SCCG rule records.";
        return false;
    }
    return true;
}

bool ParseReviewProfiles(const std::filesystem::path& path, GuidelinesDocument& document, std::string& error) {
    json root;
    if (!ReadJsonFile(path, root, error))
        return false;
    if (!root.is_object() || !root.contains("review_profiles") || !root["review_profiles"].is_array()) {
        error = path.filename().string() + " is missing review_profiles array.";
        return false;
    }

    const std::string schema_version = StringValue(root, "schema_version");
    const std::string sccg_version = StringValue(root, "sccg_version");
    if (document.schema_version.empty())
        document.schema_version = schema_version;
    if (document.sccg_version.empty())
        document.sccg_version = sccg_version;

    for (const json& profile_json : root["review_profiles"]) {
        ReviewProfile profile;
        profile.id = StringValue(profile_json, "id");
        profile.display_name = StringValue(profile_json, "display_name");
        profile.description = StringValue(profile_json, "description");
        profile.applies_to = StringArrayValue(profile_json, "applies_to");
        profile.guideline_ids = StringArrayValue(profile_json, "guideline_ids");
        profile.required_data = StringArrayValue(profile_json, "required_data");
        profile.optional_data = StringArrayValue(profile_json, "optional_data");
        profile.schema_version = schema_version;
        profile.sccg_version = sccg_version;
        if (profile.id.empty() || profile.display_name.empty() || profile.applies_to.empty() ||
            profile.guideline_ids.empty()) {
            error = path.filename().string() +
                    " contains a review profile missing id, display_name, applies_to, or guideline_ids.";
            return false;
        }
        document.review_profiles.push_back(std::move(profile));
    }
    return true;
}

bool ParseDataPackages(const std::filesystem::path& path, GuidelinesDocument& document, std::string& error) {
    json root;
    if (!ReadJsonFile(path, root, error))
        return false;
    if (!root.is_object() || !root.contains("data_packages") || !root["data_packages"].is_array()) {
        error = path.filename().string() + " is missing data_packages array.";
        return false;
    }

    const std::string schema_version = StringValue(root, "schema_version");
    const std::string sccg_version = StringValue(root, "sccg_version");
    if (document.schema_version.empty())
        document.schema_version = schema_version;
    if (document.sccg_version.empty())
        document.sccg_version = sccg_version;

    for (const json& package_json : root["data_packages"]) {
        DataPackage data_package;
        data_package.id = StringValue(package_json, "id");
        data_package.display_name = StringValue(package_json, "display_name");
        data_package.description = StringValue(package_json, "description");
        data_package.required_fields = StringArrayValue(package_json, "required_fields");
        data_package.optional_fields = StringArrayValue(package_json, "optional_fields");
        data_package.schema_version = schema_version;
        data_package.sccg_version = sccg_version;
        if (data_package.id.empty() || data_package.display_name.empty()) {
            error = path.filename().string() + " contains a data package missing id or display_name.";
            return false;
        }
        document.data_packages.push_back(std::move(data_package));
    }
    return true;
}

bool ParsePrechecks(const std::filesystem::path& path, GuidelinesDocument& document, std::string& error) {
    std::error_code filesystem_error;
    if (!std::filesystem::exists(path, filesystem_error)) {
        if (filesystem_error) {
            error = "Could not check existence of " + path.filename().string() + ": " + filesystem_error.message();
            return false;
        }
        return true;
    }

    json root;
    if (!ReadJsonFile(path, root, error))
        return false;
    if (!root.is_object() || !root.contains("prechecks") || !root["prechecks"].is_array()) {
        error = path.filename().string() + " is missing prechecks array.";
        return false;
    }

    const std::string schema_version = StringValue(root, "schema_version");
    const std::string sccg_version = StringValue(root, "sccg_version");
    for (const json& precheck_json : root["prechecks"]) {
        Precheck precheck;
        precheck.id = StringValue(precheck_json, "id");
        precheck.display_name = StringValue(precheck_json, "display_name");
        precheck.related_guideline_ids = StringArrayValue(precheck_json, "related_guideline_ids");
        precheck.expected_data = StringArrayValue(precheck_json, "expected_data");
        precheck.result_type = StringValue(precheck_json, "result_type");
        precheck.description = StringValue(precheck_json, "description");
        precheck.interpretation = StringValue(precheck_json, "interpretation");
        precheck.schema_version = schema_version;
        precheck.sccg_version = sccg_version;
        if (precheck.id.empty()) {
            error = path.filename().string() + " contains a precheck missing id.";
            return false;
        }
        document.prechecks.push_back(std::move(precheck));
    }
    return true;
}

bool ValidateConsistency(const GuidelinesDocument& document, std::string& error) {
    std::unordered_set<std::string> rule_ids;
    for (const Guideline& guideline : document.guidelines) {
        if (!rule_ids.insert(guideline.id).second) {
            error = "Duplicate SCCG rule id: " + guideline.id;
            return false;
        }
    }

    std::unordered_set<std::string> data_package_ids;
    for (const DataPackage& data_package : document.data_packages) {
        if (!data_package_ids.insert(data_package.id).second) {
            error = "Duplicate SCCG data package id: " + data_package.id;
            return false;
        }
    }

    std::unordered_set<std::string> profile_ids;
    for (const ReviewProfile& profile : document.review_profiles) {
        if (!profile_ids.insert(profile.id).second) {
            error = "Duplicate SCCG review profile id: " + profile.id;
            return false;
        }
        for (const std::string& guideline_id : profile.guideline_ids) {
            if (rule_ids.count(guideline_id) == 0) {
                error =
                    "SCCG review profile '" + profile.id + "' references unknown guideline id '" + guideline_id + "'.";
                return false;
            }
        }
        for (const std::string& package_id : profile.required_data) {
            if (data_package_ids.count(package_id) == 0) {
                error = "SCCG review profile '" + profile.id + "' references unknown required data package '" +
                        package_id + "'.";
                return false;
            }
        }
        for (const std::string& package_id : profile.optional_data) {
            if (data_package_ids.count(package_id) == 0) {
                error = "SCCG review profile '" + profile.id + "' references unknown optional data package '" +
                        package_id + "'.";
                return false;
            }
        }
    }

    for (const Precheck& precheck : document.prechecks) {
        for (const std::string& guideline_id : precheck.related_guideline_ids) {
            if (rule_ids.count(guideline_id) == 0) {
                error = "SCCG precheck '" + precheck.id + "' references unknown guideline id '" + guideline_id + "'.";
                return false;
            }
        }
    }

    return true;
}

} // namespace

GuidelinesParseResult SccgDistParser::ParseDirectory(const std::filesystem::path& dist_dir) {
    std::error_code filesystem_error;
    if (!std::filesystem::exists(dist_dir, filesystem_error) ||
        !std::filesystem::is_directory(dist_dir, filesystem_error)) {
        return std::unexpected("SCCG dist directory was not found: " + dist_dir.string());
    }

    const std::filesystem::path review_profiles_path = dist_dir / "review_profiles.json";
    const std::filesystem::path data_packages_path = dist_dir / "data_packages.json";
    std::filesystem::path rules_path = dist_dir / "ai_rule_export.jsonl";
    if (!std::filesystem::exists(rules_path, filesystem_error))
        rules_path = dist_dir / "sccg.rules.jsonl";

    for (const std::filesystem::path& required_path : {review_profiles_path, data_packages_path, rules_path}) {
        if (!std::filesystem::exists(required_path, filesystem_error)) {
            return std::unexpected("SCCG runtime artifact missing: " + required_path.filename().string() + " in " +
                                   dist_dir.string());
        }
    }

    GuidelinesDocument document;
    std::string error;
    if (!ParseRulesJsonl(rules_path, document, error) || !ParseReviewProfiles(review_profiles_path, document, error) ||
        !ParseDataPackages(data_packages_path, document, error) ||
        !ParsePrechecks(dist_dir / "prechecks.json", document, error) || !ValidateConsistency(document, error)) {
        return std::unexpected(std::move(error));
    }

    return document;
}

} // namespace parser