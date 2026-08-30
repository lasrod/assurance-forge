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

double DoubleValue(const json& object, const char* key) {
    if (!object.is_object())
        return 0.0;
    auto found = object.find(key);
    if (found == object.end() || !found->is_number())
        return 0.0;
    return found->get<double>();
}

// The `document` block every tool-facing file now carries. Read from whichever
// file supplies it first, so the dist path no longer opens `sccg://guidelines`
// with an empty heading the way it did when only the YAML fallback had it.
void ApplyDocumentBlock(const json& root, GuidelinesDocument& document) {
    const json block = root.value("document", json::object());
    if (!block.is_object())
        return;
    if (document.metadata.title.empty())
        document.metadata.title = StringValue(block, "title");
    if (document.metadata.purpose.empty())
        document.metadata.purpose = StringValue(block, "purpose");
    if (document.metadata.copyright.empty())
        document.metadata.copyright = StringValue(block, "copyright");
    if (document.metadata.license.id.empty()) {
        const json license = block.value("license", json::object());
        document.metadata.license.id = StringValue(license, "id");
        document.metadata.license.name = StringValue(license, "name");
        document.metadata.license.url = StringValue(license, "url");
    }
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

    auto markers = object.find("markers");
    if (markers != object.end() && markers->is_array()) {
        for (const json& marker_json : *markers) {
            GuidelineMarker marker;
            marker.kind = StringValue(marker_json, "kind");
            marker.effect = StringValue(marker_json, "effect");
            marker.terms = StringArrayValue(marker_json, "terms");
            if (!marker.terms.empty())
                tool.markers.push_back(std::move(marker));
        }
    }

    auto thresholds = object.find("thresholds");
    if (thresholds != object.end() && thresholds->is_array()) {
        for (const json& threshold_json : *thresholds) {
            GuidelineThreshold threshold;
            threshold.id = StringValue(threshold_json, "id");
            threshold.value = DoubleValue(threshold_json, "value");
            threshold.unit = StringValue(threshold_json, "unit");
            threshold.note = StringValue(threshold_json, "note");
            if (!threshold.id.empty())
                tool.thresholds.push_back(std::move(threshold));
        }
    }

    auto repairs = object.find("repair");
    if (repairs != object.end() && repairs->is_array()) {
        for (const json& repair_json : *repairs) {
            GuidelineRepair repair;
            repair.action = StringValue(repair_json, "action");
            repair.element_role = StringValue(repair_json, "element_role");
            repair.attach_to = StringValue(repair_json, "attach_to");
            repair.statement = StringValue(repair_json, "statement");
            if (!repair.action.empty())
                tool.repair.push_back(std::move(repair));
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
    guideline.short_rule = StringValue(object, "short_rule");
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
    ApplyDocumentBlock(root, document);

    for (const json& element_json : root.value("selectable_elements", json::array())) {
        SelectableElement element;
        element.element = StringValue(element_json, "element");
        element.notation = StringValue(element_json, "notation");
        element.element_role = StringValue(element_json, "element_role");
        element.basis = StringValue(element_json, "basis");
        if (!element.element.empty())
            document.selectable_elements.push_back(std::move(element));
    }

    for (const json& profile_json : root["review_profiles"]) {
        ReviewProfile profile;
        profile.id = StringValue(profile_json, "id");
        profile.display_name = StringValue(profile_json, "display_name");
        profile.description = StringValue(profile_json, "description");
        profile.applies_to = StringArrayValue(profile_json, "applies_to");
        profile.guideline_ids = StringArrayValue(profile_json, "guideline_ids");
        profile.required_data = StringArrayValue(profile_json, "required_data");
        profile.optional_data = StringArrayValue(profile_json, "optional_data");
        for (const json& absence_json : profile_json.value("when_absent", json::array())) {
            DataPackageAbsenceStatement statement;
            statement.id = StringValue(absence_json, "id");
            statement.statement = StringValue(absence_json, "statement");
            statement.unassessable_guideline_ids = StringArrayValue(absence_json, "unassessable_guideline_ids");
            if (!statement.id.empty())
                profile.when_absent.push_back(std::move(statement));
        }
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
    ApplyDocumentBlock(root, document);

    for (const json& state_json : root.value("availability_states", json::array())) {
        AvailabilityState state;
        state.id = StringValue(state_json, "id");
        state.display_name = StringValue(state_json, "display_name");
        state.meaning = StringValue(state_json, "meaning");
        if (!state.id.empty())
            document.availability_states.push_back(std::move(state));
    }

    for (const json& package_json : root["data_packages"]) {
        DataPackage data_package;
        data_package.id = StringValue(package_json, "id");
        data_package.display_name = StringValue(package_json, "display_name");
        data_package.description = StringValue(package_json, "description");
        data_package.role = StringValue(package_json, "role");
        data_package.element_role = StringValue(package_json, "element_role");
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
    ApplyDocumentBlock(root, document);
    for (const json& precheck_json : root["prechecks"]) {
        Precheck precheck;
        precheck.id = StringValue(precheck_json, "id");
        precheck.display_name = StringValue(precheck_json, "display_name");
        precheck.related_guideline_ids = StringArrayValue(precheck_json, "related_guideline_ids");
        precheck.expected_data = StringArrayValue(precheck_json, "expected_data");
        precheck.result_type = StringValue(precheck_json, "result_type");
        precheck.description = StringValue(precheck_json, "description");
        precheck.fires_when = StringValue(precheck_json, "fires_when");
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

// Optional in the same sense `prechecks.json` is: a dist directory that
// predates the file still loads, and the authoring surfaces then fall back to
// the review profiles rather than refusing to start.
bool ParseAuthoringGuidance(const std::filesystem::path& path, GuidelinesDocument& document, std::string& error) {
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
    if (!root.is_object()) {
        error = path.filename().string() + " is not a JSON object.";
        return false;
    }
    ApplyDocumentBlock(root, document);

    AuthoringGuidance guidance;
    guidance.description = StringValue(root, "description");
    guidance.usage = StringValue(root, "usage");
    for (const json& rule_json : root.value("core_rules", json::array())) {
        AuthoringCoreRule rule;
        rule.id = StringValue(rule_json, "id");
        rule.category = StringValue(rule_json, "category");
        rule.short_rule = StringValue(rule_json, "short_rule");
        rule.statement = StringValue(rule_json, "statement");
        rule.reason = StringValue(rule_json, "reason");
        if (rule.id.empty() || rule.short_rule.empty()) {
            error = path.filename().string() + " contains a core rule missing id or short_rule.";
            return false;
        }
        guidance.core_rules.push_back(std::move(rule));
    }
    for (const json& rule_json : root.value("element_rules", json::array())) {
        AuthoringElementRule rule;
        rule.element_role = StringValue(rule_json, "element_role");
        rule.elements = StringArrayValue(rule_json, "elements");
        rule.guideline_ids = StringArrayValue(rule_json, "guideline_ids");
        rule.review_profile_id = StringValue(rule_json, "review_profile_id");
        if (rule.element_role.empty()) {
            error = path.filename().string() + " contains an element rule missing element_role.";
            return false;
        }
        guidance.element_rules.push_back(std::move(rule));
    }
    document.authoring_guidance = std::move(guidance);
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
        // The element under review is named by whichever required package
        // carries the `selected_element` role. Refusing the catalog here is
        // deliberate: a profile that names none, or names two, leaves the
        // review method with no defensible answer to "which element is this
        // about", and the failure a version ago was silent -- every review kept
        // running and quietly reported the profile's own selected-element
        // package as unavailable.
        std::size_t selected_element_packages = 0;
        for (const std::string& package_id : profile.required_data) {
            const DataPackage* package = document.FindDataPackageById(package_id);
            if (package != nullptr && package->role == "selected_element")
                ++selected_element_packages;
        }
        if (selected_element_packages != 1) {
            error = "SCCG review profile '" + profile.id + "' requires " + std::to_string(selected_element_packages) +
                    " selected-element data packages; exactly one is required.";
            return false;
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
        !ParsePrechecks(dist_dir / "prechecks.json", document, error) ||
        !ParseAuthoringGuidance(dist_dir / "authoring_guidance.json", document, error) ||
        !ValidateConsistency(document, error)) {
        return std::unexpected(std::move(error));
    }

    return document;
}

} // namespace parser