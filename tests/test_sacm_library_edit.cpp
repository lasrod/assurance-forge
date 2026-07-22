// Phase 9 Stage 5: the edit seam onto the library-owned document.
//
// Stage 4 made the library the load source of truth (the app projects
// `loaded_case` from a library document). Editing still runs on the legacy
// models, which the audit log serializes/hashes and the replayer re-applies.
// Stage 5 routes edits through the library one operation at a time; each must
// be proven to reproduce the legacy edit before the live path depends on it.
// These tests are that proof for the text edits wired so far (Name, Content).
//
// Each applies the *same* logical edit through both paths --
// `sacm_adapter::apply_text_edit` on the library document and
// `core::SetElementTextField` on the legacy models -- and asserts the two
// agree on the *edited field*. Whole-case equality is deliberately not
// asserted: the library projection already diverges from the legacy parser on
// unrelated fields (recorded in the parallel-load baseline, e.g. a claim's
// `content=` statement surfacing as its Description per clause 8.9). The edit
// contract is that the field the user changed lands identically.

#include "sacm_adapter/document_edit.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include "core/acp/acp_editing.h"
#include "core/element_factory.h"
#include "core/library_package_projection.h"
#include "core/string_utils.h"
#include "core/terminology_package_service.h"
#include "parser/xml_parser.h"
#include "sacm/sacm_parser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path repo_root() { return std::filesystem::path(AF_REPO_ROOT); }

// fixture_acp_parity carries claim `G1` ("Top Goal", content "The system is
// acceptably safe.") and asserted inference `R1`, and loads cleanly through
// both the library and the legacy parser, so it isolates edit behaviour from
// any load-side difference.
std::filesystem::path fixture_path() {
    return repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
}

const core::SacmElement* find_element(const core::AssuranceCase& assurance_case,
                                      const std::string& id) {
    for (const core::SacmElement& element : assurance_case.elements) {
        if (element.id == id) {
            return &element;
        }
    }
    return nullptr;
}

// Loads the fixture through the library; fails the calling test if it cannot.
sacm_adapter::LoadOutcome load_fixture() {
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture_path());
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    return loaded;
}

// Runs the same field edit through the legacy models and returns the edited
// element, so a test can compare it against the library-projected result.
core::AssuranceCase legacy_edit(const std::string& element_id, core::ElementTextField field,
                                const std::string& language, const std::string& value) {
    const auto legacy_case = parser::parse_sacm_xml(fixture_path().string());
    EXPECT_TRUE(legacy_case.has_value());
    const auto legacy_package = sacm::parse_sacm(fixture_path().string());
    EXPECT_TRUE(legacy_package.has_value());

    core::AssuranceCase edited = *legacy_case;
    sacm::AssuranceCasePackage package = *legacy_package;
    std::string old_value;
    std::string error;
    EXPECT_TRUE(core::SetElementTextField(edited, &package, element_id, field, language, value,
                                          old_value, error))
        << error;
    return edited;
}

// A normalized description of an add-child result, comparable across the two
// paths. The library and legacy id generators differ, so ids are never
// compared directly: link fields record *whether* the relationship points at
// the new element, not its id.
struct ChildShape {
    std::string element_type;
    std::string element_assertion;  // "" and "asserted" both normalize to "asserted"
    std::string relationship_type;
    bool targets_parent = false;
    bool source_is_new_element = false;
    bool reasoning_is_new_element = false;
    bool is_counter = false;
    friend bool operator==(const ChildShape&, const ChildShape&) = default;
};

// The projection stamps a claim's default AssertionDeclaration as "asserted";
// the legacy parser leaves a new Goal's empty. Treat them as the same state.
std::string normalize_assertion(const std::string& assertion) {
    return (assertion.empty() || assertion == "asserted") ? "asserted" : assertion;
}

std::vector<std::string> sorted_element_ids(const core::AssuranceCase& assurance_case) {
    std::vector<std::string> ids;
    for (const core::SacmElement& element : assurance_case.elements) {
        ids.push_back(element.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

const core::AcpRecord* find_acp(const core::AssuranceCase& assurance_case, const std::string& id) {
    for (const core::AcpRecord& acp : assurance_case.acps) {
        if (acp.id == id) {
            return &acp;
        }
    }
    return nullptr;
}

ChildShape describe_child(const core::AssuranceCase& assurance_case, const std::string& parent_id,
                          const std::string& element_id, const std::string& relationship_id) {
    ChildShape shape;
    const core::SacmElement* element = find_element(assurance_case, element_id);
    const core::SacmElement* relationship = find_element(assurance_case, relationship_id);
    if (element == nullptr || relationship == nullptr) {
        return shape;
    }
    shape.element_type = element->type;
    shape.element_assertion = normalize_assertion(element->assertion_declaration);
    shape.relationship_type = relationship->type;
    shape.targets_parent = (relationship->target_refs == std::vector<std::string>{parent_id});
    shape.source_is_new_element =
        (relationship->source_refs == std::vector<std::string>{element_id});
    shape.reasoning_is_new_element = (relationship->reasoning_ref == element_id);
    shape.is_counter = relationship->is_counter;
    return shape;
}

} // namespace

// SACM23-INT-001, edit slice: the library's SetName operation, projected back
// into the POD model, must match what the legacy name edit produces.
TEST(SacmLibraryEdit, SACM23_INT_001_SetNameReproducesLegacyNameEdit) {
    const std::string kNewName = "Revised Top Goal";

    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before_g1 = find_element(before, "G1");
    ASSERT_NE(before_g1, nullptr);
    ASSERT_EQ(before_g1->name, "Top Goal");   // guards against a vacuous edit
    ASSERT_NE(before_g1->name, kNewName);

    const sacm_adapter::EditOutcome edit =
        sacm_adapter::apply_text_edit(*loaded.document, "G1", sacm_adapter::TextField::Name, "en",
                                      kNewName);
    ASSERT_TRUE(edit.supported);
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_g1 = find_element(after, "G1");
    ASSERT_NE(after_g1, nullptr);
    EXPECT_EQ(after_g1->name, kNewName);
    ASSERT_TRUE(after_g1->name_langs.contains("en"));
    EXPECT_EQ(after_g1->name_langs.at("en"), kNewName);

    const core::AssuranceCase legacy = legacy_edit("G1", core::ElementTextField::Name, "en", kNewName);
    const core::SacmElement* legacy_g1 = find_element(legacy, "G1");
    ASSERT_NE(legacy_g1, nullptr);
    EXPECT_EQ(after_g1->name, legacy_g1->name);
    ASSERT_TRUE(legacy_g1->name_langs.contains("en"));
    EXPECT_EQ(after_g1->name_langs.at("en"), legacy_g1->name_langs.at("en"));
}

// SACM23-INT-001, edit slice: a claim's `content` (its statement) maps to the
// library's SetDescription (clause 8.9); the edited content must match the
// legacy content edit.
TEST(SacmLibraryEdit, SACM23_INT_001_ContentEditReproducesLegacyStatementEdit) {
    const std::string kNewContent = "Hazards are controlled to an acceptable level.";

    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before_g1 = find_element(before, "G1");
    ASSERT_NE(before_g1, nullptr);
    ASSERT_EQ(before_g1->content, "The system is acceptably safe.");  // vacuity guard
    ASSERT_NE(before_g1->content, kNewContent);

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "G1", sacm_adapter::TextField::Content, "en", kNewContent);
    ASSERT_TRUE(edit.supported);
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_g1 = find_element(after, "G1");
    ASSERT_NE(after_g1, nullptr);
    EXPECT_EQ(after_g1->content, kNewContent);
    ASSERT_TRUE(after_g1->content_langs.contains("en"));
    EXPECT_EQ(after_g1->content_langs.at("en"), kNewContent);

    const core::AssuranceCase legacy =
        legacy_edit("G1", core::ElementTextField::Content, "en", kNewContent);
    const core::SacmElement* legacy_g1 = find_element(legacy, "G1");
    ASSERT_NE(legacy_g1, nullptr);
    EXPECT_EQ(after_g1->content, legacy_g1->content);
    ASSERT_TRUE(legacy_g1->content_langs.contains("en"));
    EXPECT_EQ(after_g1->content_langs.at("en"), legacy_g1->content_langs.at("en"));
}

// Content maps to a Description only for claim-like elements. On a relationship
// (AssertedInference R1) there is no such mapping, so the seam must report it
// unsupported and leave the document untouched rather than silently writing a
// Description the projection would never read back as content.
TEST(SacmLibraryEdit, SACM23_INT_001_ContentEditUnsupportedForRelationship) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "R1", sacm_adapter::TextField::Content, "en", "not applicable");
    EXPECT_FALSE(edit.supported);
    EXPECT_FALSE(edit.applied);
}

// SACM23-INT-001, edit slice: adding a child element through the library
// (create element + create asserted relationship) must produce the same
// structure the legacy AddChildElement does -- same element kind, assertion,
// relationship kind, and link direction (target = parent; source, or reasoning
// for a Strategy, = the new element). Ids differ between the two generators, so
// the comparison is structural.
TEST(SacmLibraryEdit, SACM23_INT_001_AddChildReproducesLegacyStructure) {
    struct Scenario {
        sacm_adapter::ChildKind library_kind;
        core::NewElementKind legacy_kind;
        ChildShape expected;
    };
    const std::vector<Scenario> scenarios = {
        {sacm_adapter::ChildKind::Goal, core::NewElementKind::Goal,
         {"claim", "asserted", "assertedinference", true, true, false}},
        {sacm_adapter::ChildKind::Solution, core::NewElementKind::Solution,
         {"artifactreference", "asserted", "assertedevidence", true, true, false}},
        {sacm_adapter::ChildKind::Context, core::NewElementKind::Context,
         {"artifactreference", "asserted", "assertedcontext", true, true, false}},
        {sacm_adapter::ChildKind::Assumption, core::NewElementKind::Assumption,
         {"claim", "assumed", "assertedcontext", true, true, false}},
        // Justification: stored as axiomatic + a gsn.role tag, translated back to
        // the app's internal "justification" by the projection -- so it now
        // reproduces the legacy structure.
        {sacm_adapter::ChildKind::Justification, core::NewElementKind::Justification,
         {"claim", "justification", "assertedcontext", true, true, false}},
    };

    for (const Scenario& scenario : scenarios) {
        SCOPED_TRACE(scenario.expected.element_type + " / " + scenario.expected.relationship_type);

        // Library path.
        sacm_adapter::LoadOutcome loaded = load_fixture();
        ASSERT_NE(loaded.document, nullptr);
        const sacm_adapter::AddChildOutcome added =
            sacm_adapter::apply_add_child(*loaded.document, "G1", scenario.library_kind);
        ASSERT_TRUE(added.supported);
        ASSERT_TRUE(added.applied)
            << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
        const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
        const ChildShape library_shape =
            describe_child(after, "G1", added.new_element_id, added.new_relationship_id);

        // Legacy path.
        const auto legacy_case = parser::parse_sacm_xml(fixture_path().string());
        ASSERT_TRUE(legacy_case.has_value()) << legacy_case.error();
        const auto legacy_package = sacm::parse_sacm(fixture_path().string());
        ASSERT_TRUE(legacy_package.has_value()) << legacy_package.error();
        core::AssuranceCase legacy = *legacy_case;
        sacm::AssuranceCasePackage package = *legacy_package;
        std::string legacy_element_id;
        std::string legacy_relationship_id;
        std::string error;
        ASSERT_TRUE(core::AddChildElement(legacy, &package, "G1", scenario.legacy_kind,
                                          legacy_element_id, legacy_relationship_id, error))
            << error;
        const ChildShape legacy_shape =
            describe_child(legacy, "G1", legacy_element_id, legacy_relationship_id);

        // Both match the expected structure, and each other.
        EXPECT_EQ(library_shape, scenario.expected);
        EXPECT_EQ(legacy_shape, scenario.expected);
        EXPECT_EQ(library_shape, legacy_shape);
    }
}

// SACM23-INT-001, edit slice: adding a top goal through the library (a Claim in
// the first ArgumentPackage, no relationship) must reproduce the legacy
// core::AddTopGoal structure. The caller-supplied id is used verbatim, which the
// library-primary audit replay needs.
TEST(SacmLibraryEdit, SACM23_INT_001_AddTopGoalReproducesLegacyStructure) {
    // Library path.
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::AddChildOutcome added =
        sacm_adapter::apply_add_top_goal(*loaded.document, "TG1");
    ASSERT_TRUE(added.supported);
    ASSERT_TRUE(added.applied) << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.new_element_id, "TG1");
    EXPECT_TRUE(added.new_relationship_id.empty()) << "a top goal has no incoming relationship";
    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* library_goal = find_element(after, "TG1");
    ASSERT_NE(library_goal, nullptr);

    // Legacy path.
    const auto legacy_case = parser::parse_sacm_xml(fixture_path().string());
    ASSERT_TRUE(legacy_case.has_value()) << legacy_case.error();
    const auto legacy_package = sacm::parse_sacm(fixture_path().string());
    ASSERT_TRUE(legacy_package.has_value()) << legacy_package.error();
    core::AssuranceCase legacy = *legacy_case;
    sacm::AssuranceCasePackage package = *legacy_package;
    std::string error;
    ASSERT_TRUE(core::AddTopGoalWithId(legacy, &package, "TG1", error)) << error;
    const core::SacmElement* legacy_goal = find_element(legacy, "TG1");
    ASSERT_NE(legacy_goal, nullptr);

    // Same kind and (empty) name, and no relationship references it in either.
    EXPECT_EQ(library_goal->type, "claim");
    EXPECT_EQ(library_goal->type, legacy_goal->type);
    EXPECT_EQ(library_goal->name, legacy_goal->name);
    for (const core::SacmElement& element : after.elements) {
        EXPECT_EQ(std::find(element.source_refs.begin(), element.source_refs.end(), "TG1"),
                  element.source_refs.end());
        EXPECT_EQ(std::find(element.target_refs.begin(), element.target_refs.end(), "TG1"),
                  element.target_refs.end());
    }
}

// SACM23-INT-001, edit slice: a dialectic challenge through the library
// (create counter element + create isCounter relationship) must produce the
// same structure the legacy AddChallenge does -- counter element kind, counter
// relationship kind, isCounter, and a source at the counter element / target at
// the challenged element. Covers both source types and a challenge whose target
// is itself a relationship (challenging an inference).
TEST(SacmLibraryEdit, SACM23_INT_001_ChallengeReproducesLegacyStructure) {
    struct Scenario {
        std::string target_id;
        core::ArgumentTarget::Kind target_kind;
        sacm_adapter::ChallengeSource library_source;
        core::ChallengeSourceType legacy_source;
        ChildShape expected;  // targets_parent means "targets the challenged id"
    };
    const std::vector<Scenario> scenarios = {
        {"G1", core::ArgumentTarget::Kind::Element, sacm_adapter::ChallengeSource::CounterArgument,
         core::ChallengeSourceType::CounterArgument,
         {"claim", "asserted", "assertedinference", true, true, false, true}},
        {"G1", core::ArgumentTarget::Kind::Element, sacm_adapter::ChallengeSource::CounterEvidence,
         core::ChallengeSourceType::CounterEvidence,
         {"artifactreference", "asserted", "assertedevidence", true, true, false, true}},
        {"R1", core::ArgumentTarget::Kind::Relationship,
         sacm_adapter::ChallengeSource::CounterArgument, core::ChallengeSourceType::CounterArgument,
         {"claim", "asserted", "assertedinference", true, true, false, true}},
    };

    for (const Scenario& scenario : scenarios) {
        SCOPED_TRACE(scenario.target_id + " <- " + scenario.expected.relationship_type);

        // Library path.
        sacm_adapter::LoadOutcome loaded = load_fixture();
        ASSERT_NE(loaded.document, nullptr);
        const sacm_adapter::AddChildOutcome added =
            sacm_adapter::apply_challenge(*loaded.document, scenario.target_id,
                                          scenario.library_source);
        ASSERT_TRUE(added.supported);
        ASSERT_TRUE(added.applied)
            << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
        const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
        const ChildShape library_shape =
            describe_child(after, scenario.target_id, added.new_element_id,
                           added.new_relationship_id);

        // Legacy path.
        const auto legacy_case = parser::parse_sacm_xml(fixture_path().string());
        ASSERT_TRUE(legacy_case.has_value()) << legacy_case.error();
        const auto legacy_package = sacm::parse_sacm(fixture_path().string());
        ASSERT_TRUE(legacy_package.has_value()) << legacy_package.error();
        core::AssuranceCase legacy = *legacy_case;
        sacm::AssuranceCasePackage package = *legacy_package;
        const core::ArgumentTarget target{.kind = scenario.target_kind, .id = scenario.target_id};
        std::string legacy_element_id;
        std::string legacy_relationship_id;
        std::string error;
        ASSERT_TRUE(core::AddChallenge(legacy, &package, target, scenario.legacy_source,
                                       legacy_element_id, legacy_relationship_id, error))
            << error;
        const ChildShape legacy_shape =
            describe_child(legacy, scenario.target_id, legacy_element_id, legacy_relationship_id);

        EXPECT_EQ(library_shape, scenario.expected);
        EXPECT_EQ(legacy_shape, scenario.expected);
        EXPECT_EQ(library_shape, legacy_shape);
    }
}

// A Strategy is added before its sub-goals, so it is created as a standalone
// ArgumentReasoning (with a strategyTarget tag naming its goal) and no inference
// yet -- the inference would be source-less, which SACM's source [1..*] forbids.
// The inference is materialized on the first sub-goal (a following increment).
TEST(SacmLibraryEdit, SACM23_INT_001_AddChildStrategyCreatesPendingReasoning) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::AddChildOutcome added = sacm_adapter::apply_add_child(
        *loaded.document, "G1", sacm_adapter::ChildKind::Strategy, "STRAT1");
    ASSERT_TRUE(added.supported);
    ASSERT_TRUE(added.applied)
        << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.new_element_id, "STRAT1");
    EXPECT_TRUE(added.new_relationship_id.empty()) << "no inference until the first sub-goal";

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* strategy = find_element(after, "STRAT1");
    ASSERT_NE(strategy, nullptr);
    EXPECT_EQ(strategy->type, "argumentreasoning");
    // No relationship references it yet.
    for (const core::SacmElement& element : after.elements) {
        EXPECT_EQ(std::find(element.source_refs.begin(), element.source_refs.end(), "STRAT1"),
                  element.source_refs.end());
        EXPECT_NE(element.reasoning_ref, "STRAT1");
    }
}

// Strategy part 2: a sub-goal added under a strategy is a source of the strategy's
// single inference. The first sub-goal materializes {target=the goal the strategy
// supports, reasoning=strategy, source=sub-goal}; later sub-goals extend that same
// inference's sources -- the standard one-inference GSN->SACM encoding, with no
// per-sub-goal inference.
TEST(SacmLibraryEdit, SACM23_INT_001_SubGoalUnderStrategyMaterializesThenExtendsInference) {
    sacm_adapter::LoadOutcome loaded = load_fixture();  // fixture claim G1
    ASSERT_NE(loaded.document, nullptr);

    // A strategy under G1 -- bare, no inference yet.
    const sacm_adapter::AddChildOutcome strategy = sacm_adapter::apply_add_child(
        *loaded.document, "G1", sacm_adapter::ChildKind::Strategy, "S1");
    ASSERT_TRUE(strategy.applied)
        << (strategy.diagnostics.empty() ? "" : strategy.diagnostics.front().message);
    ASSERT_TRUE(strategy.new_relationship_id.empty());

    // First sub-goal under S1 -> materialize the inference with the caller id.
    const sacm_adapter::AddChildOutcome first = sacm_adapter::apply_add_child(
        *loaded.document, "S1", sacm_adapter::ChildKind::Goal, "SG1", "R_INF");
    ASSERT_TRUE(first.applied) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    EXPECT_EQ(first.new_element_id, "SG1");
    EXPECT_EQ(first.new_relationship_id, "R_INF") << "the inference is materialized on the first sub-goal";

    core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* inference = find_element(after, "R_INF");
    ASSERT_NE(inference, nullptr);
    EXPECT_EQ(inference->type, "assertedinference");
    EXPECT_EQ(inference->reasoning_ref, "S1");
    EXPECT_EQ(inference->target_refs, (std::vector<std::string>{"G1"}));
    EXPECT_EQ(inference->source_refs, (std::vector<std::string>{"SG1"}));

    // Second sub-goal -> extend the SAME inference (no new relationship created).
    const sacm_adapter::AddChildOutcome second = sacm_adapter::apply_add_child(
        *loaded.document, "S1", sacm_adapter::ChildKind::Goal, "SG2");
    ASSERT_TRUE(second.applied) << (second.diagnostics.empty() ? "" : second.diagnostics.front().message);
    EXPECT_TRUE(second.new_relationship_id.empty())
        << "extending an existing inference creates no relationship (contract: id only set when created)";

    after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* extended = find_element(after, "R_INF");
    ASSERT_NE(extended, nullptr);
    EXPECT_EQ(extended->source_refs, (std::vector<std::string>{"SG1", "SG2"}));
    int inferences_under_s1 = 0;
    for (const core::SacmElement& element : after.elements) {
        if (element.type == "assertedinference" && element.reasoning_ref == "S1") {
            ++inferences_under_s1;
        }
    }
    EXPECT_EQ(inferences_under_s1, 1) << "single-inference encoding: no per-sub-goal inference";
}

// The GSN Justification round-trips: adding one stores axiomatic + the gsn.role
// tag in the library, and after an XMI save/load the projection still renders it
// as a justification -- proving the vendor tag survives (standards-correct SACM
// on disk, GSN semantics preserved).
TEST(SacmLibraryEdit, SACM23_INT_001_JustificationRoundTripsViaGsnRoleTag) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::AddChildOutcome added = sacm_adapter::apply_add_child(
        *loaded.document, "G1", sacm_adapter::ChildKind::Justification, "JUST1", "JREL1");
    ASSERT_TRUE(added.applied)
        << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);

    const core::AssuranceCase before_case = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before = find_element(before_case, "JUST1");
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->assertion_declaration, "justification");

    // Save to XMI and reload: the gsn.role tag must survive.
    const sacm_adapter::SaveOutcome saved = sacm_adapter::save_document(*loaded.document);
    ASSERT_TRUE(saved.ok);
    sacm_adapter::LibraryDocument reloaded;
    ASSERT_TRUE(sacm_adapter::reload_document(reloaded, saved.xml));
    const core::AssuranceCase after_case = sacm_adapter::project_case(reloaded);
    const core::SacmElement* after = find_element(after_case, "JUST1");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->assertion_declaration, "justification");
}

// Stage 7 prerequisite: when the caller supplies ids, the created element and
// relationship must use them verbatim (not library-generated ones), so a
// library-primary audit replay can reproduce exact ids.
TEST(SacmLibraryEdit, SACM23_INT_001_AddChildUsesCallerSuppliedIds) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::AddChildOutcome added = sacm_adapter::apply_add_child(
        *loaded.document, "G1", sacm_adapter::ChildKind::Goal, "CALLER_GOAL", "CALLER_REL");
    ASSERT_TRUE(added.applied)
        << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.new_element_id, "CALLER_GOAL");
    EXPECT_EQ(added.new_relationship_id, "CALLER_REL");

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    ASSERT_NE(find_element(after, "CALLER_GOAL"), nullptr);
    const core::SacmElement* relationship = find_element(after, "CALLER_REL");
    ASSERT_NE(relationship, nullptr);
    EXPECT_EQ(relationship->source_refs, std::vector<std::string>{"CALLER_GOAL"});
}

TEST(SacmLibraryEdit, SACM23_INT_001_ChallengeUsesCallerSuppliedIds) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::AddChildOutcome added = sacm_adapter::apply_challenge(
        *loaded.document, "G1", sacm_adapter::ChallengeSource::CounterArgument, "CALLER_COUNTER",
        "CALLER_CREL");
    ASSERT_TRUE(added.applied)
        << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.new_element_id, "CALLER_COUNTER");
    EXPECT_EQ(added.new_relationship_id, "CALLER_CREL");

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    ASSERT_NE(find_element(after, "CALLER_COUNTER"), nullptr);
    ASSERT_NE(find_element(after, "CALLER_CREL"), nullptr);
}

// Adding a child under an id that is not a claim-like container in an argument
// package must fail cleanly rather than create a dangling element.
TEST(SacmLibraryEdit, SACM23_INT_001_AddChildUnderMissingParentUnsupported) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::AddChildOutcome added =
        sacm_adapter::apply_add_child(*loaded.document, "NO_SUCH_ID", sacm_adapter::ChildKind::Goal);
    EXPECT_FALSE(added.supported);
    EXPECT_FALSE(added.applied);
}

// A non-primary-language content edit adds that language's LangString to the
// front Description; the projected content_langs entry must match the legacy
// per-language content map, and the primary content must be untouched.
TEST(SacmLibraryEdit, SACM23_INT_001_ContentEditReproducesLegacyForNonPrimaryLanguage) {
    const std::string kJa = "安全";  // Japanese for "safety"

    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* before_g1 = find_element(before, "G1");
    ASSERT_NE(before_g1, nullptr);
    const std::string english_before = before_g1->content;

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "G1", sacm_adapter::TextField::Content, "ja", kJa);
    ASSERT_TRUE(edit.supported);
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_g1 = find_element(after, "G1");
    ASSERT_NE(after_g1, nullptr);
    ASSERT_TRUE(after_g1->content_langs.contains("ja"));
    EXPECT_EQ(after_g1->content_langs.at("ja"), kJa);
    EXPECT_EQ(after_g1->content, english_before) << "the primary content must be untouched";

    const core::AssuranceCase legacy = legacy_edit("G1", core::ElementTextField::Content, "ja", kJa);
    const core::SacmElement* legacy_g1 = find_element(legacy, "G1");
    ASSERT_NE(legacy_g1, nullptr);
    ASSERT_TRUE(legacy_g1->content_langs.contains("ja"));
    EXPECT_EQ(after_g1->content_langs.at("ja"), legacy_g1->content_langs.at("ja"));
}

// The Description field on a non-claim element (here the relationship R1) maps to
// its front Description, so the edit reproduces the legacy description edit.
TEST(SacmLibraryEdit, SACM23_INT_001_DescriptionEditReproducesLegacyForNonClaim) {
    const std::string kNote = "Decomposition over identified hazards.";

    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "R1", sacm_adapter::TextField::Description, "en", kNote);
    ASSERT_TRUE(edit.supported);
    ASSERT_TRUE(edit.applied) << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* after_r1 = find_element(after, "R1");
    ASSERT_NE(after_r1, nullptr);
    EXPECT_EQ(after_r1->description, kNote);
    ASSERT_TRUE(after_r1->description_langs.contains("en"));
    EXPECT_EQ(after_r1->description_langs.at("en"), kNote);

    const core::AssuranceCase legacy =
        legacy_edit("R1", core::ElementTextField::Description, "en", kNote);
    const core::SacmElement* legacy_r1 = find_element(legacy, "R1");
    ASSERT_NE(legacy_r1, nullptr);
    EXPECT_EQ(after_r1->description, legacy_r1->description);
    ASSERT_TRUE(legacy_r1->description_langs.contains("en"));
    EXPECT_EQ(after_r1->description_langs.at("en"), legacy_r1->description_langs.at("en"));
}

// A legacy file using the non-standard assertionDeclaration="justification"
// (predating the gsn.role encoding) projects as the app's "justification" role:
// the library normalizes it to axiomatic + a reserved compat tag on import, and
// the projection honours that tag so old Justifications survive the migration.
TEST(SacmLibraryEdit, SACM23_INT_001_LegacyJustificationProjectsAsJustification) {
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" )"
        R"(xmlns:xmi="http://www.omg.org/spec/XMI/20131001" )"
        R"(xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="acp_1">)"
        R"(<argumentPackage xmi:id="ap_1">)"
        R"(<argumentElement xsi:type="sacm:Claim" xmi:id="J1" assertionDeclaration="justification">)"
        R"(<name content="Just"/></argumentElement></argumentPackage></sacm:AssuranceCasePackage>)";
    sacm_adapter::LibraryDocument document;
    ASSERT_TRUE(sacm_adapter::reload_document(document, xml));
    const core::AssuranceCase projected = sacm_adapter::project_case(document);
    const core::SacmElement* j1 = find_element(projected, "J1");
    ASSERT_NE(j1, nullptr);
    EXPECT_EQ(j1->assertion_declaration, "justification");
}

// The Description field on a claim-like element is a *second* Description (a
// note) that SetDescription's front-only edit cannot target, so it stays
// unsupported and the caller keeps the legacy edit authoritative.
TEST(SacmLibraryEdit, SACM23_INT_001_DescriptionEditUnsupportedForClaim) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "G1", sacm_adapter::TextField::Description, "en", "a note");
    EXPECT_FALSE(edit.supported);
    EXPECT_FALSE(edit.applied);
}

// The ACP id is a TaggedValue value, not an element id. Adding an ACP must not
// force the marker tag's element id to the ACP id, which would collide with the
// library's global element-id uniqueness when a real element already uses it.
TEST(SacmLibraryEdit, SACM23_INT_001_AddAcpDoesNotCollideWithElementId) {
    const std::filesystem::path path =
        repo_root() / "tests" / "data" / "fixture_acp_id_collision.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    // The document already contains an element with id "ACP1"; the generated ACP
    // id is also "ACP1". The add must still succeed.
    const sacm_adapter::AcpOutcome added = sacm_adapter::apply_add_acp(*loaded.document, "S1");
    ASSERT_TRUE(added.supported);
    ASSERT_TRUE(added.applied) << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.acp_id, "ACP1");

    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    EXPECT_NE(find_acp(projected, "ACP1"), nullptr);
}

// SACM23-INT-001, edit slice: a caller-supplied ACP id is used verbatim, so an
// audited/replayed ACP add reproduces the exact id the legacy generator recorded
// rather than regenerating from the (possibly diverged) live document state.
TEST(SacmLibraryEdit, SACM23_INT_001_AddAcpUsesCallerSuppliedId) {
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_edit.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::AcpOutcome added =
        sacm_adapter::apply_add_acp(*loaded.document, "S1", "ACP5");
    ASSERT_TRUE(added.supported);
    ASSERT_TRUE(added.applied) << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.acp_id, "ACP5") << "the supplied id must be used verbatim, not regenerated";

    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    EXPECT_NE(find_acp(projected, "ACP5"), nullptr);
    EXPECT_EQ(find_acp(projected, "ACP1"), nullptr) << "the generated id must not appear";
}

// A caller-supplied ACP id already in use is refused rather than writing duplicate
// marker tags (guards a corrupt or double-applied replay).
TEST(SacmLibraryEdit, SACM23_INT_001_AddAcpRejectsDuplicateRequestedId) {
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_edit.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::AcpOutcome first = sacm_adapter::apply_add_acp(*loaded.document, "S1");
    ASSERT_TRUE(first.applied) << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    ASSERT_EQ(first.acp_id, "ACP1");

    const sacm_adapter::AcpOutcome duplicate =
        sacm_adapter::apply_add_acp(*loaded.document, "S1", "ACP1");
    EXPECT_TRUE(duplicate.supported);
    EXPECT_FALSE(duplicate.applied) << "a duplicate requested ACP id must be rejected";
}

// SACM23-INT-001, edit slice: adding an Assurance Claim Point to an eligible
// element (an ArtifactReference) through the library must produce the same ACP
// record the legacy core::acp::AddAcp does. The id generator is shared
// (deterministic ACP<n>), so unlike the create ops the records -- id included --
// compare exactly.
TEST(SacmLibraryEdit, SACM23_INT_001_AddAcpReproducesLegacyRecord) {
    const std::filesystem::path path = repo_root() / "tests" / "data" / "fixture_acp_edit.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    // Library path: add an ACP to the ArtifactReference S1.
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::AcpOutcome added = sacm_adapter::apply_add_acp(*loaded.document, "S1");
    ASSERT_TRUE(added.supported);
    ASSERT_TRUE(added.applied) << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_EQ(added.acp_id, "ACP1");  // no ACPs pre-exist, so the first id is ACP1

    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    const core::AcpRecord* library_acp = find_acp(projected, added.acp_id);
    ASSERT_NE(library_acp, nullptr) << "projection did not synthesize the added ACP";

    // Legacy path: the same add through core::acp::AddAcp.
    const auto legacy_case = parser::parse_sacm_xml(path.string());
    ASSERT_TRUE(legacy_case.has_value()) << legacy_case.error();
    const auto legacy_package = sacm::parse_sacm(path.string());
    ASSERT_TRUE(legacy_package.has_value()) << legacy_package.error();
    core::AssuranceCase legacy = *legacy_case;
    sacm::AssuranceCasePackage package = *legacy_package;
    const core::acp::AcpEditResult result = core::acp::AddAcp(legacy, &package, "element", "S1");
    ASSERT_TRUE(result.error.empty()) << result.error;
    const core::AcpRecord* legacy_acp = find_acp(legacy, result.acp_id);
    ASSERT_NE(legacy_acp, nullptr);

    // The two records match field for field, ids included.
    EXPECT_EQ(library_acp->id, legacy_acp->id);
    EXPECT_EQ(library_acp->name, legacy_acp->name);
    EXPECT_EQ(library_acp->target_kind, legacy_acp->target_kind);
    EXPECT_EQ(library_acp->target_id, legacy_acp->target_id);
    EXPECT_EQ(library_acp->resolution_kind, legacy_acp->resolution_kind);
    // And they are the expected values for a fresh, unresolved element ACP.
    EXPECT_EQ(library_acp->name, "ACP1");
    EXPECT_EQ(library_acp->target_kind, "element");
    EXPECT_EQ(library_acp->target_id, "S1");
    EXPECT_EQ(library_acp->resolution_kind, "none");
}

// An ACP on an ineligible element (a claim, not an ArtifactReference) must be
// refused, matching core::acp::AddAcp, which rejects the same target. A
// relationship target is likewise unsupported for now (relationship-ACP
// eligibility is a later slice).
TEST(SacmLibraryEdit, SACM23_INT_001_AddAcpRefusesIneligibleTargets) {
    const std::filesystem::path path =
        repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);

    // G1 is a claim -> ineligible for an element ACP.
    const sacm_adapter::AcpOutcome on_claim = sacm_adapter::apply_add_acp(*loaded.document, "G1");
    EXPECT_FALSE(on_claim.supported);
    EXPECT_FALSE(on_claim.applied);

    // Legacy agrees the claim is not an eligible element ACP target.
    const auto legacy_case = parser::parse_sacm_xml(path.string());
    ASSERT_TRUE(legacy_case.has_value());
    const auto legacy_package = sacm::parse_sacm(path.string());
    ASSERT_TRUE(legacy_package.has_value());
    core::AssuranceCase legacy = *legacy_case;
    sacm::AssuranceCasePackage package = *legacy_package;
    const core::acp::AcpEditResult result = core::acp::AddAcp(legacy, &package, "element", "G1");
    EXPECT_FALSE(result.error.empty());

    // R1 is a relationship -> not yet wired.
    const sacm_adapter::AcpOutcome on_relationship =
        sacm_adapter::apply_add_acp(*loaded.document, "R1");
    EXPECT_FALSE(on_relationship.supported);
    EXPECT_FALSE(on_relationship.applied);
}

// SACM23-INT-001, edit slice: deleting a leaf element through the library
// (DeleteElement with reference cleanup) must leave the same set of elements the
// legacy RemoveElement does. A leaf is used because there the two removal modes
// coincide -- no children to reparent or cascade -- so the outcome is
// unambiguous. Ids are preserved by deletion, so the remaining sets compare
// directly.
TEST(SacmLibraryEdit, SACM23_INT_001_DeleteLeafReproducesLegacyRemoval) {
    const std::filesystem::path path =
        repo_root() / "tests" / "data" / "fixture_acp_parity.sacm.xml";
    ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

    // Library path: delete the leaf sub-goal G2 (source of R1, no children).
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    ASSERT_TRUE(loaded.ok);
    ASSERT_NE(loaded.document, nullptr);
    const core::AssuranceCase before = sacm_adapter::project_case(*loaded.document);
    ASSERT_NE(find_element(before, "G2"), nullptr);  // vacuity: the leaf and its
    ASSERT_NE(find_element(before, "R1"), nullptr);  // relationship start present

    const sacm_adapter::DeleteOutcome deleted =
        sacm_adapter::apply_delete_element(*loaded.document, "G2");
    ASSERT_TRUE(deleted.supported);
    ASSERT_TRUE(deleted.applied)
        << (deleted.diagnostics.empty() ? "" : deleted.diagnostics.front().message);

    const core::AssuranceCase after = sacm_adapter::project_case(*loaded.document);
    // G2 and the now-empty relationship R1 are both gone; G1 remains.
    EXPECT_EQ(find_element(after, "G2"), nullptr);
    EXPECT_EQ(find_element(after, "R1"), nullptr);
    ASSERT_NE(find_element(after, "G1"), nullptr);

    // Legacy path: the same removal.
    const auto legacy_case = parser::parse_sacm_xml(path.string());
    ASSERT_TRUE(legacy_case.has_value()) << legacy_case.error();
    const auto legacy_package = sacm::parse_sacm(path.string());
    ASSERT_TRUE(legacy_package.has_value()) << legacy_package.error();
    core::AssuranceCase legacy = *legacy_case;
    sacm::AssuranceCasePackage package = *legacy_package;
    std::string error;
    ASSERT_TRUE(core::RemoveElement(legacy, &package, "G2",
                                    core::RemoveMode::NodeAndDescendants, error))
        << error;

    // The two paths leave the same set of elements.
    EXPECT_EQ(sorted_element_ids(after), sorted_element_ids(legacy));
}

// A rename targeting an id the document does not contain must fail cleanly: the
// mapping is supported but the library reports the operation and leaves the
// document unchanged, so a bad edit can never silently corrupt the source of
// truth.
TEST(SacmLibraryEdit, SACM23_INT_001_SetNameOnMissingElementFailsUnchanged) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "NO_SUCH_ID", sacm_adapter::TextField::Name, "en", "whatever");
    EXPECT_TRUE(edit.supported);
    EXPECT_FALSE(edit.applied);
    EXPECT_FALSE(edit.diagnostics.empty());

    const core::AssuranceCase projected = sacm_adapter::project_case(*loaded.document);
    const core::SacmElement* g1 = find_element(projected, "G1");
    ASSERT_NE(g1, nullptr);
    EXPECT_EQ(g1->name, "Top Goal");
}

// ---------------------------------------------------------------------------
// Terminology edit seams (Phase 0 part 2).
//
// Each test applies the same logical terminology edit through both paths -- the
// library seam (sacm_adapter::apply_*_terminology_*) on a LibraryDocument, and
// the legacy core::*Terminology*WithIds mutator on a sacm::AssuranceCasePackage
// parsed from the same fixture -- and asserts the projected library terminology
// matches the legacy package on the edited fields. Ids differ between the two id
// generators and the library assigns no gid to created terminology elements, so
// neither id nor gid is compared: the contract is that the terminology *content*
// lands identically. fixture_terminology_parity carries package TP1 (categories
// CAT1/CAT2, expression E1, term T1) and argument package AP1 (claims G1/G2).

namespace {

std::filesystem::path terminology_fixture_path() {
    return repo_root() / "tests" / "data" / "fixture_terminology_parity.sacm.xml";
}

sacm_adapter::LoadOutcome load_terminology_fixture() {
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(terminology_fixture_path());
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    return loaded;
}

sacm::AssuranceCasePackage load_legacy_terminology() {
    const auto parsed = sacm::parse_sacm(terminology_fixture_path().string());
    EXPECT_TRUE(parsed.has_value());
    return parsed.has_value() ? *parsed : sacm::AssuranceCasePackage{};
}

const sacm::TerminologyPackage* find_terminology_package(
    const std::vector<sacm::TerminologyPackage>& packages, const std::string& id) {
    for (const sacm::TerminologyPackage& package : packages) {
        if (package.id == id) {
            return &package;
        }
    }
    return nullptr;
}

const sacm::Term* find_term(const sacm::TerminologyPackage& package, const std::string& id) {
    for (const sacm::Term& term : package.terms) {
        if (term.id == id) {
            return &term;
        }
    }
    return nullptr;
}

const sacm::Category* find_category(const sacm::TerminologyPackage& package, const std::string& id) {
    for (const sacm::Category& category : package.categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

// A term's edited fields, comparable across the two paths (id and gid excluded).
struct TermFields {
    std::string value;
    std::string name;
    std::string description;
    std::string external_reference;
    std::string origin;
    std::vector<std::string> category_refs;
    friend bool operator==(const TermFields&, const TermFields&) = default;
};

TermFields term_fields(const sacm::Term& term) {
    return TermFields{term.value,          term.name,   term.description,
                      term.externalReference, term.origin, term.category_refs};
}

// The sorted ids of every element a terminology package contains, for delete
// parity (categories + expressions + terms).
std::vector<std::string> terminology_element_ids(const sacm::TerminologyPackage& package) {
    std::vector<std::string> ids;
    for (const sacm::Category& category : package.categories) {
        ids.push_back(category.id);
    }
    for (const sacm::Expression& expression : package.expressions) {
        ids.push_back(expression.id);
    }
    for (const sacm::Term& term : package.terms) {
        ids.push_back(term.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> strip_hashes(const std::vector<std::string>& refs) {
    std::vector<std::string> out;
    out.reserve(refs.size());
    for (const std::string& ref : refs) {
        out.push_back(core::StripLeadingHash(ref));
    }
    return out;
}

// The shape a term association leaves in an argument package: the created
// artifact reference (name + referencedArtifact) and asserted context (source /
// target / visibility). Extracted by id so it works on either path's package.
struct ContextLink {
    bool found = false;
    std::string ref_name;
    std::string referenced_artifact;
    std::vector<std::string> ctx_sources;
    std::vector<std::string> ctx_targets;
    bool visible = false;
    friend bool operator==(const ContextLink&, const ContextLink&) = default;
};

ContextLink extract_context_link(const sacm::AssuranceCasePackage& package,
                                 const std::string& reference_id, const std::string& context_id) {
    const sacm::ArtifactReference* reference = nullptr;
    const sacm::AssertedContext* context = nullptr;
    for (const sacm::ArgumentPackage& argument_package : package.argumentPackages) {
        for (const sacm::ArtifactReference& candidate : argument_package.artifactReferences) {
            if (candidate.id == reference_id) {
                reference = &candidate;
            }
        }
        for (const sacm::AssertedContext& candidate : argument_package.assertedContexts) {
            if (candidate.id == context_id) {
                context = &candidate;
            }
        }
    }
    ContextLink link;
    if (reference == nullptr || context == nullptr) {
        return link;
    }
    link.found = true;
    link.ref_name = reference->name;
    link.referenced_artifact = core::StripLeadingHash(reference->referencedArtifact);
    link.ctx_sources = strip_hashes(context->sources);
    link.ctx_targets = strip_hashes(context->targets);
    link.visible = core::IsVisibleTerminologyContext(*context);
    return link;
}

core::TerminologyPackageRef package_ref(const std::string& id) {
    return core::TerminologyPackageRef{id, ""};
}

} // namespace

// SACM23-INT-001, terminology slice: creating a TerminologyPackage through the
// library must reproduce the legacy CreateTerminologyPackageWithIds content.
TEST(SacmLibraryEdit, SACM23_INT_001_CreateTerminologyPackageMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyCreateOutcome created =
        sacm_adapter::apply_create_terminology_package(*loaded.document, "Glossary",
                                                       "Project glossary", "TP_NEW");
    ASSERT_TRUE(created.applied)
        << (created.diagnostics.empty() ? "" : created.diagnostics.front().message);
    EXPECT_EQ(created.element_id, "TP_NEW");
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP_NEW");
    ASSERT_NE(library_tp, nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyPackageCreateResult legacy_result =
        core::CreateTerminologyPackageWithIds(legacy, "Glossary", "Project glossary", "TP_NEW", "");
    ASSERT_TRUE(legacy_result.success) << legacy_result.error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP_NEW");
    ASSERT_NE(legacy_tp, nullptr);

    EXPECT_EQ(library_tp->name, legacy_tp->name);
    EXPECT_EQ(library_tp->description, legacy_tp->description);
    EXPECT_EQ(library_tp->name, "Glossary");
    EXPECT_EQ(library_tp->description, "Project glossary");
}

// SACM23-INT-001, terminology slice: creating a Category must reproduce the
// legacy CreateTerminologyCategoryWithIds content.
TEST(SacmLibraryEdit, SACM23_INT_001_CreateTerminologyCategoryMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyCreateOutcome created =
        sacm_adapter::apply_create_terminology_category(*loaded.document, "TP1", "Metrics",
                                                        "Safety metrics", "CAT_NEW");
    ASSERT_TRUE(created.applied)
        << (created.diagnostics.empty() ? "" : created.diagnostics.front().message);
    EXPECT_EQ(created.element_id, "CAT_NEW");
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP1");
    ASSERT_NE(library_tp, nullptr);
    const sacm::Category* library_category = find_category(*library_tp, "CAT_NEW");
    ASSERT_NE(library_category, nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyCategoryDraft draft{"Metrics", "Safety metrics"};
    const core::TerminologyCategoryCreateResult legacy_result =
        core::CreateTerminologyCategoryWithIds(legacy, package_ref("TP1"), draft, "CAT_NEW", "");
    ASSERT_TRUE(legacy_result.success) << legacy_result.error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP1");
    ASSERT_NE(legacy_tp, nullptr);
    const sacm::Category* legacy_category = find_category(*legacy_tp, "CAT_NEW");
    ASSERT_NE(legacy_category, nullptr);

    EXPECT_EQ(library_category->name, legacy_category->name);
    EXPECT_EQ(library_category->description, legacy_category->description);
    EXPECT_EQ(library_category->name, "Metrics");
    EXPECT_EQ(library_category->description, "Safety metrics");
}

// SACM23-INT-001, terminology slice: creating a Term (composing CreateTerm +
// SetDescription + SetExpressionCategories) must reproduce the legacy
// CreateTerminologyTermWithIds content across every draft field.
TEST(SacmLibraryEdit, SACM23_INT_001_CreateTerminologyTermMatchesLegacy) {
    const sacm_adapter::TerminologyTermFields fields{
        .value = "Hazard",
        .name = "Hazard",
        .description = "A potential source of harm.",
        .category_refs = {"CAT1", "CAT2"},
        .external_reference = "ISO-99999",
        .origin = "E1"};

    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyCreateOutcome created =
        sacm_adapter::apply_create_terminology_term(*loaded.document, "TP1", fields, "T_NEW");
    ASSERT_TRUE(created.applied)
        << (created.diagnostics.empty() ? "" : created.diagnostics.front().message);
    EXPECT_EQ(created.element_id, "T_NEW");
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP1");
    ASSERT_NE(library_tp, nullptr);
    const sacm::Term* library_term = find_term(*library_tp, "T_NEW");
    ASSERT_NE(library_term, nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyTermDraft draft{"Hazard", "Hazard", "A potential source of harm.",
                                           {"CAT1", "CAT2"}, "ISO-99999", "E1"};
    const core::TerminologyTermCreateResult legacy_result =
        core::CreateTerminologyTermWithIds(legacy, package_ref("TP1"), draft, "T_NEW", "");
    ASSERT_TRUE(legacy_result.success) << legacy_result.error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP1");
    ASSERT_NE(legacy_tp, nullptr);
    const sacm::Term* legacy_term = find_term(*legacy_tp, "T_NEW");
    ASSERT_NE(legacy_term, nullptr);

    EXPECT_EQ(term_fields(*library_term), term_fields(*legacy_term));
    EXPECT_EQ(library_term->value, "Hazard");
    EXPECT_EQ(library_term->externalReference, "ISO-99999");
    EXPECT_EQ(library_term->origin, "E1");
    EXPECT_EQ(library_term->category_refs, (std::vector<std::string>{"CAT1", "CAT2"}));
}

// SACM23-INT-001, terminology slice: updating a TerminologyPackage's
// name/description must reproduce the legacy UpdateTerminologyPackage content.
TEST(SacmLibraryEdit, SACM23_INT_001_UpdateTerminologyPackageMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyEditOutcome edit = sacm_adapter::apply_update_terminology_package(
        *loaded.document, "TP1", "Renamed Terminology", "Updated description");
    ASSERT_TRUE(edit.applied)
        << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP1");
    ASSERT_NE(library_tp, nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    std::string error;
    ASSERT_TRUE(core::UpdateTerminologyPackage(legacy, package_ref("TP1"), "Renamed Terminology",
                                               "Updated description", error))
        << error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP1");
    ASSERT_NE(legacy_tp, nullptr);

    EXPECT_EQ(library_tp->name, legacy_tp->name);
    EXPECT_EQ(library_tp->description, legacy_tp->description);
    EXPECT_EQ(library_tp->name, "Renamed Terminology");
    EXPECT_EQ(library_tp->description, "Updated description");
}

// SACM23-INT-001, terminology slice: updating a Category must reproduce the
// legacy UpdateTerminologyCategory content.
TEST(SacmLibraryEdit, SACM23_INT_001_UpdateTerminologyCategoryMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyEditOutcome edit = sacm_adapter::apply_update_terminology_category(
        *loaded.document, "CAT1", "Renamed Concept", "Concept category");
    ASSERT_TRUE(edit.applied)
        << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP1");
    ASSERT_NE(library_tp, nullptr);
    const sacm::Category* library_category = find_category(*library_tp, "CAT1");
    ASSERT_NE(library_category, nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyCategoryDraft draft{"Renamed Concept", "Concept category"};
    std::string error;
    ASSERT_TRUE(core::UpdateTerminologyCategory(legacy, package_ref("TP1"),
                                                core::TerminologyCategoryRef{"CAT1", ""}, draft,
                                                error))
        << error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP1");
    ASSERT_NE(legacy_tp, nullptr);
    const sacm::Category* legacy_category = find_category(*legacy_tp, "CAT1");
    ASSERT_NE(legacy_category, nullptr);

    EXPECT_EQ(library_category->name, legacy_category->name);
    EXPECT_EQ(library_category->description, legacy_category->description);
    EXPECT_EQ(library_category->name, "Renamed Concept");
}

// SACM23-INT-001, terminology slice: updating a Term (composing SetName/
// SetExpressionValue/SetDescription/SetExpressionCategories/
// SetTermExternalReference/SetTermOrigin) must reproduce the legacy
// UpdateTerminologyTerm content across every draft field, leaving other
// terminology elements (CAT1/CAT2/E1) intact.
TEST(SacmLibraryEdit, SACM23_INT_001_UpdateTerminologyTermMatchesLegacy) {
    const sacm_adapter::TerminologyTermFields fields{
        .value = "Revised safety",
        .name = "Safety (revised)",
        .description = "Updated definition.",
        .category_refs = {"CAT2"},
        .external_reference = "ISO-54321",
        .origin = "E1"};

    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyEditOutcome edit =
        sacm_adapter::apply_update_terminology_term(*loaded.document, "T1", fields);
    ASSERT_TRUE(edit.applied)
        << (edit.diagnostics.empty() ? "" : edit.diagnostics.front().message);
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP1");
    ASSERT_NE(library_tp, nullptr);
    const sacm::Term* library_term = find_term(*library_tp, "T1");
    ASSERT_NE(library_term, nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyTermDraft draft{"Revised safety", "Safety (revised)",
                                           "Updated definition.", {"CAT2"}, "ISO-54321", "E1"};
    std::string error;
    ASSERT_TRUE(core::UpdateTerminologyTerm(legacy, package_ref("TP1"),
                                            core::TerminologyTermRef{"T1", ""}, draft, error))
        << error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP1");
    ASSERT_NE(legacy_tp, nullptr);
    const sacm::Term* legacy_term = find_term(*legacy_tp, "T1");
    ASSERT_NE(legacy_term, nullptr);

    EXPECT_EQ(term_fields(*library_term), term_fields(*legacy_term));
    EXPECT_EQ(library_term->value, "Revised safety");
    EXPECT_EQ(library_term->category_refs, (std::vector<std::string>{"CAT2"}));
    EXPECT_EQ(library_term->origin, "E1");
    // Unrelated standard terminology data is preserved on both paths.
    EXPECT_EQ(terminology_element_ids(*library_tp), terminology_element_ids(*legacy_tp));
    EXPECT_NE(find_category(*library_tp, "CAT1"), nullptr);
    EXPECT_NE(find_category(*library_tp, "CAT2"), nullptr);
}

// A term update with an unresolvable category ref is rejected and leaves the term
// unchanged -- the composed setters pre-validate categories/origin so the update
// is atomic (the ADR's failure->unchanged contract), rather than partially
// applying the name/value the legacy ApplyTermDraft would also have rolled back.
TEST(SacmLibraryEdit, SACM23_INT_001_UpdateTerminologyTermIsAtomicOnBadRef) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);

    const sacm_adapter::TerminologyTermFields fields{.value = "changed value",
                                                     .name = "changed name",
                                                     .category_refs = {"NO_SUCH_CATEGORY"}};
    const sacm_adapter::TerminologyEditOutcome edit =
        sacm_adapter::apply_update_terminology_term(*loaded.document, "T1", fields);
    EXPECT_FALSE(edit.applied) << "an unresolvable category ref must reject the update";

    const std::vector<sacm::TerminologyPackage> after =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* after_tp = find_terminology_package(after, "TP1");
    ASSERT_NE(after_tp, nullptr);
    const sacm::Term* term = find_term(*after_tp, "T1");
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->name, "Safety") << "name must not have partially applied";
    EXPECT_EQ(term->value, "Freedom from unacceptable risk") << "value must not have partially applied";
}

// SACM23-INT-001, terminology slice: deleting a Term (the primitive the legacy
// delete mutators compose) must leave the same terminology elements the legacy
// DeleteTerminologyTerm does.
TEST(SacmLibraryEdit, SACM23_INT_001_DeleteTerminologyTermMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    {
        const std::vector<sacm::TerminologyPackage> before =
            sacm_adapter::project_terminology_packages(*loaded.document);
        const sacm::TerminologyPackage* before_tp = find_terminology_package(before, "TP1");
        ASSERT_NE(before_tp, nullptr);
        ASSERT_NE(find_term(*before_tp, "T1"), nullptr);  // vacuity guard
    }
    const sacm_adapter::TerminologyEditOutcome deleted =
        sacm_adapter::apply_delete_terminology_element(*loaded.document, "T1");
    ASSERT_TRUE(deleted.applied)
        << (deleted.diagnostics.empty() ? "" : deleted.diagnostics.front().message);
    const std::vector<sacm::TerminologyPackage> library_packages =
        sacm_adapter::project_terminology_packages(*loaded.document);
    const sacm::TerminologyPackage* library_tp = find_terminology_package(library_packages, "TP1");
    ASSERT_NE(library_tp, nullptr);
    EXPECT_EQ(find_term(*library_tp, "T1"), nullptr);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    std::string error;
    ASSERT_TRUE(core::DeleteTerminologyTerm(legacy, package_ref("TP1"),
                                            core::TerminologyTermRef{"T1", ""}, error))
        << error;
    const sacm::TerminologyPackage* legacy_tp =
        find_terminology_package(legacy.terminologyPackages, "TP1");
    ASSERT_NE(legacy_tp, nullptr);

    // Both paths remove T1 and keep CAT1, CAT2, E1.
    EXPECT_EQ(terminology_element_ids(*library_tp), terminology_element_ids(*legacy_tp));
    EXPECT_EQ(terminology_element_ids(*library_tp),
              (std::vector<std::string>{"CAT1", "CAT2", "E1"}));
}

// SACM23-INT-001, terminology slice: associating a term with a visible element
// creates an ArtifactReference (referencedArtifact = term) plus an AssertedContext
// {source = ref, target = element}, matching the legacy association -- reference
// name, referencedArtifact, and context endpoints -- and reports the created-*
// flags. The context is not a visible one.
TEST(SacmLibraryEdit, SACM23_INT_001_AssociateTerminologyTermMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyContextOutcome associated =
        sacm_adapter::apply_associate_terminology_term(*loaded.document, "G1", "T1", "AR_X", "AC_X");
    ASSERT_TRUE(associated.applied)
        << (associated.diagnostics.empty() ? "" : associated.diagnostics.front().message);
    EXPECT_TRUE(associated.created_artifact_reference);
    EXPECT_TRUE(associated.created_asserted_context);
    EXPECT_FALSE(associated.already_associated);
    EXPECT_EQ(associated.artifact_reference_id, "AR_X");
    EXPECT_EQ(associated.asserted_context_id, "AC_X");
    const sacm::AssuranceCasePackage library_package =
        core::project_library_package_with_tags(*loaded.document);
    const ContextLink library_link = extract_context_link(library_package, "AR_X", "AC_X");
    ASSERT_TRUE(library_link.found);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyContextForcedIds forced{"AR_X", "", "AC_X", ""};
    const core::TerminologyContextAssociationResult legacy_result =
        core::AssociateTerminologyTermWithElementWithIds(
            legacy, "G1", package_ref("TP1"), core::TerminologyTermRef{"T1", ""}, forced);
    ASSERT_TRUE(legacy_result.success) << legacy_result.error;
    EXPECT_TRUE(legacy_result.created_artifact_reference);
    EXPECT_TRUE(legacy_result.created_asserted_context);
    EXPECT_FALSE(legacy_result.already_associated);
    const ContextLink legacy_link = extract_context_link(legacy, "AR_X", "AC_X");
    ASSERT_TRUE(legacy_link.found);

    EXPECT_EQ(library_link, legacy_link);
    EXPECT_EQ(library_link.referenced_artifact, "T1");
    EXPECT_EQ(library_link.ctx_sources, (std::vector<std::string>{"AR_X"}));
    EXPECT_EQ(library_link.ctx_targets, (std::vector<std::string>{"G1"}));
    EXPECT_FALSE(library_link.visible);
}

// SACM23-INT-001, terminology slice: adding a term as a visible context is (8)
// plus the visible-context marker on the AssertedContext, so the projected
// context reads back as visible on both paths.
TEST(SacmLibraryEdit, SACM23_INT_001_AddTerminologyVisibleContextMatchesLegacy) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyContextOutcome added =
        sacm_adapter::apply_add_terminology_visible_context(*loaded.document, "G1", "T1", "VR_X",
                                                            "VC_X");
    ASSERT_TRUE(added.applied)
        << (added.diagnostics.empty() ? "" : added.diagnostics.front().message);
    EXPECT_TRUE(added.created_artifact_reference);
    EXPECT_TRUE(added.created_asserted_context);
    EXPECT_FALSE(added.already_associated);
    const sacm::AssuranceCasePackage library_package =
        core::project_library_package_with_tags(*loaded.document);
    const ContextLink library_link = extract_context_link(library_package, "VR_X", "VC_X");
    ASSERT_TRUE(library_link.found);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyContextForcedIds forced{"VR_X", "", "VC_X", ""};
    const core::TerminologyContextAssociationResult legacy_result =
        core::AddTerminologyTermAsVisibleContextWithIds(
            legacy, "G1", package_ref("TP1"), core::TerminologyTermRef{"T1", ""}, forced);
    ASSERT_TRUE(legacy_result.success) << legacy_result.error;
    EXPECT_TRUE(legacy_result.created_artifact_reference);
    EXPECT_TRUE(legacy_result.created_asserted_context);
    const ContextLink legacy_link = extract_context_link(legacy, "VR_X", "VC_X");
    ASSERT_TRUE(legacy_link.found);

    EXPECT_EQ(library_link, legacy_link);
    EXPECT_TRUE(library_link.visible) << "the added context must read back as a visible context";
    EXPECT_EQ(library_link.referenced_artifact, "T1");
    EXPECT_EQ(library_link.ctx_targets, (std::vector<std::string>{"G1"}));
}

// SACM23-INT-001, terminology slice: associating the same term with the same
// element twice reuses the existing reference and context and reports
// already_associated on the second call, matching the legacy reuse semantics.
TEST(SacmLibraryEdit, SACM23_INT_001_AssociateTerminologyTermReusesExisting) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyContextOutcome first =
        sacm_adapter::apply_associate_terminology_term(*loaded.document, "G1", "T1");
    ASSERT_TRUE(first.applied)
        << (first.diagnostics.empty() ? "" : first.diagnostics.front().message);
    ASSERT_TRUE(first.created_asserted_context);
    const sacm_adapter::TerminologyContextOutcome second =
        sacm_adapter::apply_associate_terminology_term(*loaded.document, "G1", "T1");
    ASSERT_TRUE(second.applied);
    EXPECT_TRUE(second.already_associated);
    EXPECT_FALSE(second.created_artifact_reference);
    EXPECT_FALSE(second.created_asserted_context);
    EXPECT_EQ(second.artifact_reference_id, first.artifact_reference_id);
    EXPECT_EQ(second.asserted_context_id, first.asserted_context_id);

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyContextAssociationResult legacy_first =
        core::AssociateTerminologyTermWithElement(legacy, "G1", package_ref("TP1"),
                                                  core::TerminologyTermRef{"T1", ""});
    ASSERT_TRUE(legacy_first.success) << legacy_first.error;
    const core::TerminologyContextAssociationResult legacy_second =
        core::AssociateTerminologyTermWithElement(legacy, "G1", package_ref("TP1"),
                                                  core::TerminologyTermRef{"T1", ""});
    ASSERT_TRUE(legacy_second.success) << legacy_second.error;
    EXPECT_TRUE(legacy_second.already_associated);
    EXPECT_FALSE(legacy_second.created_asserted_context);
    EXPECT_EQ(legacy_second.asserted_context_id, legacy_first.asserted_context_id);
}

// SACM23-INT-001, terminology slice: adding a term as a visible context when a
// non-visible context already links it to the target promotes that context in
// place (no new reference or context), marking it visible -- matching the legacy
// promote semantics.
TEST(SacmLibraryEdit, SACM23_INT_001_AddTerminologyVisibleContextPromotesExisting) {
    sacm_adapter::LoadOutcome loaded = load_terminology_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::TerminologyContextOutcome associated =
        sacm_adapter::apply_associate_terminology_term(*loaded.document, "G1", "T1", "AR_X", "AC_X");
    ASSERT_TRUE(associated.created_asserted_context)
        << (associated.diagnostics.empty() ? "" : associated.diagnostics.front().message);
    const sacm_adapter::TerminologyContextOutcome promoted =
        sacm_adapter::apply_add_terminology_visible_context(*loaded.document, "G1", "T1");
    ASSERT_TRUE(promoted.applied)
        << (promoted.diagnostics.empty() ? "" : promoted.diagnostics.front().message);
    EXPECT_FALSE(promoted.created_artifact_reference) << "the existing reference is reused";
    EXPECT_FALSE(promoted.created_asserted_context) << "the existing context is promoted in place";
    EXPECT_EQ(promoted.artifact_reference_id, "AR_X");
    EXPECT_EQ(promoted.asserted_context_id, "AC_X");
    const sacm::AssuranceCasePackage library_package =
        core::project_library_package_with_tags(*loaded.document);
    const ContextLink library_link = extract_context_link(library_package, "AR_X", "AC_X");
    ASSERT_TRUE(library_link.found);
    EXPECT_TRUE(library_link.visible) << "the promoted context must read back as visible";

    sacm::AssuranceCasePackage legacy = load_legacy_terminology();
    const core::TerminologyContextForcedIds forced{"AR_X", "", "AC_X", ""};
    ASSERT_TRUE(core::AssociateTerminologyTermWithElementWithIds(
                    legacy, "G1", package_ref("TP1"), core::TerminologyTermRef{"T1", ""}, forced)
                    .success);
    const core::TerminologyContextAssociationResult legacy_promoted =
        core::AddTerminologyTermAsVisibleContext(legacy, "G1", package_ref("TP1"),
                                                 core::TerminologyTermRef{"T1", ""});
    ASSERT_TRUE(legacy_promoted.success) << legacy_promoted.error;
    EXPECT_FALSE(legacy_promoted.created_artifact_reference);
    EXPECT_FALSE(legacy_promoted.created_asserted_context);
    EXPECT_EQ(legacy_promoted.asserted_context_id, "AC_X");
    const ContextLink legacy_link = extract_context_link(legacy, "AR_X", "AC_X");
    ASSERT_TRUE(legacy_link.found);
    EXPECT_TRUE(legacy_link.visible);

    EXPECT_EQ(library_link, legacy_link);
}
