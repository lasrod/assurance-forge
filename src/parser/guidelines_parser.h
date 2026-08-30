#pragma once

#include <expected>
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

// One published word list, and what a hit means. `effect` is the whole point:
// a `candidate` hit is a signal, a `suppress` hit cancels one another marker
// raised, and an `expected` marker is the reverse -- its *absence* is the
// signal. A tool that matched terms and ignored the effect would report EV.4's
// citation-precision markers as findings against every precise citation.
struct GuidelineMarker {
    std::string kind;
    std::string effect;
    std::vector<std::string> terms;
};

// A numeric parameter a deterministic check needs, published so the number is
// SCCG's rather than each tool's. `value` is a double because the schema says
// number; the two published so far are whole.
struct GuidelineThreshold {
    std::string id;
    double value = 0.0;
    std::string unit;
    std::string note;
};

// What SCCG prescribes when the guideline is broken, in notation-neutral terms.
// `action` is one of add_element, split_element, reword_element, move_text,
// define_term, mark_undeveloped. `element_role` names what to add or move text
// into and `attach_to` where it goes, both empty where the repair needs none.
struct GuidelineRepair {
    std::string action;
    std::string element_role;
    std::string attach_to;
    std::string statement;
};

struct GuidelineTool {
    std::vector<std::string> applicable_elements;
    std::vector<std::string> detection_hints;
    std::vector<SuggestedCheck> suggested_checks;
    std::vector<GuidelineMarker> markers;
    std::vector<GuidelineThreshold> thresholds;
    std::vector<GuidelineRepair> repair;
};

struct Guideline {
    std::string id;
    std::string rule_id;
    std::string category;
    std::string category_id;
    std::string title;
    std::string statement;
    // One line carrying the whole rule, published for the places a full
    // statement does not fit -- a system prompt, an editor hint.
    std::string short_rule;
    std::string rationale;
    std::vector<std::string> review_prompts;
    GuidelineExample examples;
    std::vector<GuidelineReference> references;
    std::vector<std::string> reference_source_ids;
    std::vector<std::string> review_profile_ids;
    std::vector<std::string> data_package_ids;
    GuidelineTool tool;
    std::string schema_version;
    std::string sccg_version;
};

// What a review should do when a required package is missing anyway. Declared
// upstream so a degraded review is a stated degradation rather than an
// improvised one: it names the guidelines that cannot be assessed without the
// package, and what the review should say in their place.
struct DataPackageAbsenceStatement {
    std::string id;
    std::string statement;
    std::vector<std::string> unassessable_guideline_ids;
};

struct ReviewProfile {
    std::string id;
    std::string display_name;
    std::string description;
    std::vector<std::string> applies_to;
    std::vector<std::string> guideline_ids;
    std::vector<std::string> required_data;
    std::vector<std::string> optional_data;
    std::vector<DataPackageAbsenceStatement> when_absent;
    std::string schema_version;
    std::string sccg_version;
};

struct DataPackage {
    std::string id;
    std::string display_name;
    std::string description;
    // "selected_element" or "supporting". The selected-element packages are one
    // per element role, so a profile names the role it reviews by requiring one
    // of them -- which is what a tool looks up, rather than hard-coding seven
    // package names.
    std::string role;
    // The notation-neutral role a selected-element package carries: claim,
    // strategy, evidence, context, assumption, justification, challenge. Empty
    // on a supporting package.
    std::string element_role;
    std::vector<std::string> required_fields;
    std::vector<std::string> optional_fields;
    std::string schema_version;
    std::string sccg_version;
};

// The mapping point for a tool whose model is neither GSN nor CAE: SCCG names
// the elements a user sees in each notation, and gives each one the
// notation-neutral `element_role` a tool maps its own types onto.
struct SelectableElement {
    std::string element;
    std::string notation;
    std::string element_role;
    std::string basis;
};

// The states a data package can be in, published so a tool reports absence in
// SCCG's names rather than its own.
struct AvailabilityState {
    std::string id;
    std::string display_name;
    std::string meaning;
};

struct Precheck {
    std::string id;
    std::string display_name;
    std::vector<std::string> related_guideline_ids;
    std::vector<std::string> expected_data;
    std::string result_type;
    std::string description;
    // The firing condition, stated precisely enough that two tools implement
    // the same check. Carried rather than paraphrased: a pre-check whose
    // condition each tool reads for itself is not one check.
    std::string fires_when;
    std::string interpretation;
    std::string schema_version;
    std::string sccg_version;
};

// The SCCG subset to deliver while an author or agent is *writing*, as opposed
// to when a review is run. Published so the rules a tool puts in front of an
// author are SCCG's selection and wording rather than each tool's.
struct AuthoringCoreRule {
    std::string id;
    std::string category;
    std::string short_rule;
    std::string statement;
    // Why this guideline is in the writing-time subset at all.
    std::string reason;
};

// Which review profile each element role is judged under, and the notation
// element names that role covers.
struct AuthoringElementRule {
    std::string element_role;
    std::vector<std::string> elements;
    std::vector<std::string> guideline_ids;
    std::string review_profile_id;
};

struct AuthoringGuidance {
    std::string description;
    // SCCG's own instruction for rendering this subset. Carried so a tool
    // cannot present the subset as conformance with the whole standard.
    std::string usage;
    std::vector<AuthoringCoreRule> core_rules;
    std::vector<AuthoringElementRule> element_rules;
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
    std::vector<SelectableElement> selectable_elements;
    std::vector<AvailabilityState> availability_states;
    std::vector<Precheck> prechecks;
    AuthoringGuidance authoring_guidance;

    const Guideline* FindGuidelineById(const std::string& id) const;
    std::vector<const Guideline*> FindGuidelinesByCategory(const std::string& category_id) const;
    std::vector<const Guideline*> FindGuidelinesByApplicableElement(const std::string& element_name) const;
    std::vector<const Guideline*> FindGuidelinesByReviewProfile(const std::string& review_profile_id) const;
    std::vector<const Guideline*> FindGuidelinesBySuggestedCheckId(const std::string& check_id) const;
    const SuggestedCheck* FindSuggestedCheckById(const std::string& check_id) const;
    const ReviewProfile* FindReviewProfileById(const std::string& id) const;
    const ReferenceSource* FindReferenceSourceById(const std::string& source_id) const;
    const GuidelineCategory* FindCategoryById(const std::string& category_id) const;
    const DataPackage* FindDataPackageById(const std::string& id) const;
    const Precheck* FindPrecheckById(const std::string& id) const;
    const AvailabilityState* FindAvailabilityStateById(const std::string& id) const;
    // The one package in `profile.required_data` whose role is
    // `selected_element`. Null where the catalog names none, which is a
    // malformed profile rather than a case to work around.
    const DataPackage* FindSelectedElementPackage(const ReviewProfile& profile) const;
    // The notation-neutral role of a notation element name, e.g. "GSN Goal" ->
    // "claim". Empty where the catalog does not publish the name.
    std::string ElementRoleForSelectableElement(const std::string& element_name) const;
    const AuthoringElementRule* FindAuthoringElementRule(const std::string& element_role) const;
};

using GuidelinesParseResult = std::expected<GuidelinesDocument, std::string>;

class GuidelinesParser {
public:
    static GuidelinesParseResult ParseFile(const std::string& file_path);
    static GuidelinesParseResult ParseString(const std::string& yaml_content);
};

} // namespace parser