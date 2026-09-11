#include "parser/sccg_dist_parser.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

namespace parser {
namespace {

using json = nlohmann::json;

// The one contract major this loader reads. A minor adds keys and fields and
// never changes an existing one, so it is compared by major only; SCCG's own
// changelog says to do exactly that. Another major may rename or restructure
// what this loader reads, and loading it anyway would be reading a contract by
// guesswork.
constexpr int kSupportedSchemaMajor = 3;

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

int MajorVersion(const std::string& version) {
    try {
        return std::stoi(version.substr(0, version.find('.')));
    } catch (const std::exception&) {
        return 0;
    }
}

void ParseDocumentBlock(const json& root, GuidelinesDocument& document) {
    const json block = root.value("document", json::object());
    if (!block.is_object())
        return;
    document.metadata.title = StringValue(block, "title");
    document.metadata.purpose = StringValue(block, "purpose");
    document.metadata.copyright = StringValue(block, "copyright");
    const json license = block.value("license", json::object());
    document.metadata.license.id = StringValue(license, "id");
    document.metadata.license.name = StringValue(license, "name");
    document.metadata.license.url = StringValue(license, "url");
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
    for (const json& check_json : object.value("suggested_checks", json::array())) {
        SuggestedCheck check;
        check.id = StringValue(check_json, "id");
        check.description = StringValue(check_json, "description");
        if (!check.id.empty() || !check.description.empty())
            tool.suggested_checks.push_back(std::move(check));
    }
    for (const json& marker_json : object.value("markers", json::array())) {
        GuidelineMarker marker;
        marker.kind = StringValue(marker_json, "kind");
        marker.effect = StringValue(marker_json, "effect");
        marker.terms = StringArrayValue(marker_json, "terms");
        if (!marker.terms.empty())
            tool.markers.push_back(std::move(marker));
    }
    for (const json& threshold_json : object.value("thresholds", json::array())) {
        GuidelineThreshold threshold;
        threshold.id = StringValue(threshold_json, "id");
        threshold.value = DoubleValue(threshold_json, "value");
        threshold.unit = StringValue(threshold_json, "unit");
        threshold.note = StringValue(threshold_json, "note");
        if (!threshold.id.empty())
            tool.thresholds.push_back(std::move(threshold));
    }
    for (const json& repair_json : object.value("repair", json::array())) {
        GuidelineRepair repair;
        repair.action = StringValue(repair_json, "action");
        repair.element_role = StringValue(repair_json, "element_role");
        repair.attach_to = StringValue(repair_json, "attach_to");
        repair.statement = StringValue(repair_json, "statement");
        if (!repair.action.empty())
            tool.repair.push_back(std::move(repair));
    }
    return tool;
}

bool ParseGuidelines(const json& root, GuidelinesDocument& document, std::string& error) {
    const json guidelines = root.value("guidelines", json::array());
    if (!guidelines.is_array() || guidelines.empty()) {
        error = "sccg.full.json contains no guidelines.";
        return false;
    }
    for (const json& object : guidelines) {
        Guideline guideline;
        guideline.id = StringValue(object, "id");
        guideline.category = StringValue(object, "category");
        guideline.title = StringValue(object, "title");
        guideline.statement = StringValue(object, "statement");
        guideline.short_rule = StringValue(object, "short_rule");
        guideline.rationale = StringValue(object, "rationale");
        guideline.review_prompts = StringArrayValue(object, "review_prompts");

        const json examples = object.value("examples", json::object());
        guideline.examples.bad = StringValue(examples, "bad");
        guideline.examples.problem = StringValue(examples, "problem");
        guideline.examples.good = StringValue(examples, "good");

        std::set<std::string> source_ids;
        for (const json& reference_json : object.value("references", json::array())) {
            GuidelineReference reference = ParseReference(reference_json);
            if (!reference.source_id.empty())
                source_ids.insert(reference.source_id);
            guideline.references.push_back(std::move(reference));
        }
        guideline.reference_source_ids.assign(source_ids.begin(), source_ids.end());

        for (const json& distinction_json : object.value("distinguish_from", json::array())) {
            GuidelineDistinction distinction;
            distinction.id = StringValue(distinction_json, "id");
            distinction.note = StringValue(distinction_json, "note");
            if (!distinction.id.empty())
                guideline.distinguish_from.push_back(std::move(distinction));
        }
        guideline.tool = ParseTool(object.value("tool", json::object()));

        // The whole-catalogue record carries no copy of these; the rule export
        // does, derived from the same catalogue. Derived here the way the export
        // derives them, so the request a tool builds is the same whichever file
        // it was written against.
        guideline.rule_id = guideline.id;
        guideline.category_id = guideline.category;
        guideline.schema_version = document.schema_version;
        guideline.sccg_version = document.sccg_version;

        if (guideline.id.empty() || guideline.title.empty() || guideline.statement.empty() ||
            guideline.rationale.empty()) {
            error = "sccg.full.json contains a guideline missing id, title, statement, or rationale.";
            return false;
        }
        document.guidelines.push_back(std::move(guideline));
    }
    return true;
}

bool ParseReviewProfiles(const json& root, GuidelinesDocument& document, std::string& error) {
    const json profiles = root.value("review_profiles", json::array());
    if (!profiles.is_array() || profiles.empty()) {
        error = "sccg.full.json contains no review profiles.";
        return false;
    }

    for (const json& element_json : root.value("selectable_elements", json::array())) {
        SelectableElement element;
        element.element = StringValue(element_json, "element");
        element.notation = StringValue(element_json, "notation");
        element.element_role = StringValue(element_json, "element_role");
        element.basis = StringValue(element_json, "basis");
        if (!element.element.empty())
            document.selectable_elements.push_back(std::move(element));
    }

    for (const json& profile_json : profiles) {
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
        for (const json& pass_json : profile_json.value("review_passes", json::array())) {
            ReviewPass pass;
            pass.id = StringValue(pass_json, "id");
            pass.display_name = StringValue(pass_json, "display_name");
            pass.question = StringValue(pass_json, "question");
            pass.guideline_ids = StringArrayValue(pass_json, "guideline_ids");
            if (!pass.id.empty())
                profile.review_passes.push_back(std::move(pass));
        }
        profile.schema_version = document.schema_version;
        profile.sccg_version = document.sccg_version;
        if (profile.id.empty() || profile.display_name.empty() || profile.applies_to.empty() ||
            profile.guideline_ids.empty()) {
            error = "sccg.full.json contains a review profile missing id, display_name, applies_to, or guideline_ids.";
            return false;
        }
        document.review_profiles.push_back(std::move(profile));
    }
    document.review_pass_instruction = StringValue(root, "review_pass_instruction");
    return true;
}

bool ParseDataPackages(const json& root, GuidelinesDocument& document, std::string& error) {
    const json packages = root.value("data_packages", json::array());
    if (!packages.is_array() || packages.empty()) {
        error = "sccg.full.json contains no data packages.";
        return false;
    }
    document.when_unavailable = StringValue(root, "when_unavailable");

    for (const json& state_json : root.value("availability_states", json::array())) {
        AvailabilityState state;
        state.id = StringValue(state_json, "id");
        state.display_name = StringValue(state_json, "display_name");
        state.meaning = StringValue(state_json, "meaning");
        if (!state.id.empty())
            document.availability_states.push_back(std::move(state));
    }

    for (const json& package_json : packages) {
        DataPackage data_package;
        data_package.id = StringValue(package_json, "id");
        data_package.display_name = StringValue(package_json, "display_name");
        data_package.description = StringValue(package_json, "description");
        data_package.role = StringValue(package_json, "role");
        data_package.element_role = StringValue(package_json, "element_role");
        data_package.required_fields = StringArrayValue(package_json, "required_fields");
        data_package.optional_fields = StringArrayValue(package_json, "optional_fields");
        const json meanings = package_json.value("field_meanings", json::object());
        if (meanings.is_object()) {
            for (const auto& [field, meaning] : meanings.items()) {
                if (meaning.is_string())
                    data_package.field_meanings[field] = meaning.get<std::string>();
            }
        }
        data_package.schema_version = document.schema_version;
        data_package.sccg_version = document.sccg_version;
        if (data_package.id.empty() || data_package.display_name.empty()) {
            error = "sccg.full.json contains a data package missing id or display_name.";
            return false;
        }
        document.data_packages.push_back(std::move(data_package));
    }
    return true;
}

bool ParsePrechecks(const json& root, GuidelinesDocument& document, std::string& error) {
    for (const json& precheck_json : root.value("prechecks", json::array())) {
        Precheck precheck;
        precheck.id = StringValue(precheck_json, "id");
        precheck.display_name = StringValue(precheck_json, "display_name");
        precheck.related_guideline_ids = StringArrayValue(precheck_json, "related_guideline_ids");
        precheck.expected_data = StringArrayValue(precheck_json, "expected_data");
        precheck.result_type = StringValue(precheck_json, "result_type");
        precheck.description = StringValue(precheck_json, "description");
        precheck.fires_when = StringValue(precheck_json, "fires_when");
        precheck.interpretation = StringValue(precheck_json, "interpretation");
        precheck.schema_version = document.schema_version;
        precheck.sccg_version = document.sccg_version;
        if (precheck.id.empty()) {
            error = "sccg.full.json contains a precheck missing id.";
            return false;
        }
        document.prechecks.push_back(std::move(precheck));
    }
    return true;
}

// Under `authoring_guidance` in the whole file, because its keys (`description`,
// `usage`) would be ambiguous at the root -- the one per-concern file SCCG does
// not place at the root.
bool ParseAuthoringGuidance(const json& root, GuidelinesDocument& document, std::string& error) {
    const json block = root.value("authoring_guidance", json::object());
    if (!block.is_object())
        return true;

    AuthoringGuidance guidance;
    guidance.description = StringValue(block, "description");
    guidance.usage = StringValue(block, "usage");
    for (const json& rule_json : block.value("core_rules", json::array())) {
        AuthoringCoreRule rule;
        rule.id = StringValue(rule_json, "id");
        rule.category = StringValue(rule_json, "category");
        rule.short_rule = StringValue(rule_json, "short_rule");
        rule.statement = StringValue(rule_json, "statement");
        rule.reason = StringValue(rule_json, "reason");
        if (rule.id.empty() || rule.short_rule.empty()) {
            error = "sccg.full.json contains an authoring core rule missing id or short_rule.";
            return false;
        }
        guidance.core_rules.push_back(std::move(rule));
    }
    for (const json& rule_json : block.value("element_rules", json::array())) {
        AuthoringElementRule rule;
        rule.element_role = StringValue(rule_json, "element_role");
        rule.elements = StringArrayValue(rule_json, "elements");
        rule.guideline_ids = StringArrayValue(rule_json, "guideline_ids");
        rule.review_profile_id = StringValue(rule_json, "review_profile_id");
        if (rule.element_role.empty()) {
            error = "sccg.full.json contains an authoring element rule missing element_role.";
            return false;
        }
        guidance.element_rules.push_back(std::move(rule));
    }
    document.authoring_guidance = std::move(guidance);
    return true;
}

bool ParseRetiredGuidelines(const json& root, GuidelinesDocument& document, std::string& error) {
    for (const json& entry_json : root.value("retired_guidelines", json::array())) {
        RetiredGuideline retired;
        retired.id = StringValue(entry_json, "id");
        retired.title = StringValue(entry_json, "title");
        retired.retired_in = StringValue(entry_json, "retired_in");
        retired.replaced_by = StringArrayValue(entry_json, "replaced_by");
        retired.note = StringValue(entry_json, "note");
        if (retired.id.empty() || retired.replaced_by.empty()) {
            error = "sccg.full.json contains a retired guideline missing id or replaced_by.";
            return false;
        }
        document.retired_guidelines.push_back(std::move(retired));
    }
    return true;
}

// The per-guideline profile and package lists the rule export carries,
// derived from the profiles: a guideline is reviewed by every profile that lists
// it, and may be given any package those profiles require or accept.
void DeriveGuidelineProfileLinks(GuidelinesDocument& document) {
    for (Guideline& guideline : document.guidelines) {
        std::set<std::string> packages;
        for (const ReviewProfile& profile : document.review_profiles) {
            if (std::find(profile.guideline_ids.begin(), profile.guideline_ids.end(), guideline.id) ==
                profile.guideline_ids.end())
                continue;
            guideline.review_profile_ids.push_back(profile.id);
            packages.insert(profile.required_data.begin(), profile.required_data.end());
            packages.insert(profile.optional_data.begin(), profile.optional_data.end());
        }
        guideline.data_package_ids.assign(packages.begin(), packages.end());
    }
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
        // carries the `selected_element` role. A profile that names none, or
        // two, leaves the review method with no defensible answer to "which
        // element is this about", and the failure a version ago was silent --
        // every review kept running and reported the profile's own
        // selected-element package as unavailable.
        std::size_t selected_element_packages = 0;
        const DataPackage* selected_element_package = nullptr;
        for (const std::string& package_id : profile.required_data) {
            const DataPackage* package = document.FindDataPackageById(package_id);
            if (package != nullptr && package->role == "selected_element") {
                ++selected_element_packages;
                selected_element_package = package;
            }
        }
        if (selected_element_packages != 1) {
            error = "SCCG review profile '" + profile.id + "' requires " + std::to_string(selected_element_packages) +
                    " selected-element data packages; exactly one is required.";
            return false;
        }
        if (selected_element_package->element_role.empty()) {
            error = "SCCG review profile '" + profile.id + "' requires selected-element data package '" +
                    selected_element_package->id + "', which carries no element_role.";
            return false;
        }

        // A pass partition that dropped a guideline would be a fan-out review
        // that never asks about it, and one listing a guideline twice would ask
        // and report it twice. SCCG validates this upstream; the tool refuses a
        // catalogue that fails it rather than trusting that it passed.
        if (profile.review_passes.empty())
            continue;
        std::unordered_set<std::string> in_profile(profile.guideline_ids.begin(), profile.guideline_ids.end());
        std::unordered_set<std::string> covered;
        std::unordered_set<std::string> pass_ids;
        for (const ReviewPass& pass : profile.review_passes) {
            if (!pass_ids.insert(pass.id).second) {
                error = "SCCG review profile '" + profile.id + "' names review pass '" + pass.id + "' twice.";
                return false;
            }
            if (pass.guideline_ids.empty()) {
                error =
                    "SCCG review profile '" + profile.id + "' has review pass '" + pass.id + "' with no guidelines.";
                return false;
            }
            for (const std::string& guideline_id : pass.guideline_ids) {
                if (in_profile.count(guideline_id) == 0) {
                    error = "SCCG review pass '" + profile.id + "/" + pass.id + "' lists '" + guideline_id +
                            "', which the profile does not carry.";
                    return false;
                }
                if (!covered.insert(guideline_id).second) {
                    error = "SCCG review profile '" + profile.id + "' lists guideline '" + guideline_id +
                            "' in more than one review pass.";
                    return false;
                }
            }
        }
        for (const std::string& guideline_id : profile.guideline_ids) {
            if (covered.count(guideline_id) == 0) {
                error = "SCCG review profile '" + profile.id + "' has review passes that omit guideline '" +
                        guideline_id + "'.";
                return false;
            }
        }
    }

    for (const Guideline& guideline : document.guidelines) {
        for (const GuidelineDistinction& distinction : guideline.distinguish_from) {
            if (rule_ids.count(distinction.id) == 0) {
                error = "SCCG guideline '" + guideline.id + "' distinguishes itself from unknown guideline '" +
                        distinction.id + "'.";
                return false;
            }
        }
    }

    // A retired id still in use is two meanings for one id, and a redirect to a
    // guideline that does not exist sends a stored finding nowhere.
    for (const RetiredGuideline& retired : document.retired_guidelines) {
        if (rule_ids.count(retired.id) != 0) {
            error = "SCCG guideline '" + retired.id + "' is both retired and published.";
            return false;
        }
        for (const std::string& replacement : retired.replaced_by) {
            if (rule_ids.count(replacement) == 0) {
                error = "Retired SCCG guideline '" + retired.id + "' is replaced by unknown guideline '" + replacement +
                        "'.";
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

    // SCCG requires {question} exactly once and no other brace; a tool that
    // substituted into anything else would send the model a literal placeholder.
    if (!document.review_pass_instruction.empty()) {
        const std::string& instruction = document.review_pass_instruction;
        const std::size_t placeholder = instruction.find("{question}");
        const std::size_t braces = std::count(instruction.begin(), instruction.end(), '{') +
                                   std::count(instruction.begin(), instruction.end(), '}');
        if (placeholder == std::string::npos || braces != 2) {
            error = "SCCG review_pass_instruction must contain {question} once and no other brace.";
            return false;
        }
    }
    return true;
}

} // namespace

GuidelinesParseResult SccgDistParser::ParseFile(const std::filesystem::path& catalog_path) {
    json root;
    std::string error;
    if (!ReadJsonFile(catalog_path, root, error))
        return std::unexpected(std::move(error));
    if (!root.is_object())
        return std::unexpected(catalog_path.filename().string() + " is not a JSON object.");

    GuidelinesDocument document;
    document.schema_version = StringValue(root, "schema_version");
    document.sccg_version = StringValue(root, "sccg_version");
    if (MajorVersion(document.schema_version) != kSupportedSchemaMajor) {
        return std::unexpected(
            "SCCG contract " +
            (document.schema_version.empty() ? std::string("(unversioned)") : document.schema_version) +
            " is not supported; this tool reads contract " + std::to_string(kSupportedSchemaMajor) + ".x.");
    }
    ParseDocumentBlock(root, document);

    if (!ParseGuidelines(root, document, error) || !ParseReviewProfiles(root, document, error) ||
        !ParseDataPackages(root, document, error) || !ParsePrechecks(root, document, error) ||
        !ParseAuthoringGuidance(root, document, error) || !ParseRetiredGuidelines(root, document, error)) {
        return std::unexpected(std::move(error));
    }
    DeriveGuidelineProfileLinks(document);
    if (!ValidateConsistency(document, error))
        return std::unexpected(std::move(error));
    return document;
}

GuidelinesParseResult SccgDistParser::ParseDirectory(const std::filesystem::path& dist_dir) {
    const std::filesystem::path catalog_path = dist_dir / kCatalogFileName;
    std::error_code filesystem_error;
    if (!std::filesystem::exists(catalog_path, filesystem_error))
        return std::unexpected("SCCG catalogue not found: " + catalog_path.string());
    return ParseFile(catalog_path);
}

} // namespace parser
