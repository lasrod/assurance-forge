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

// Strategy and Justification have no like-for-like library mapping yet: a bare
// strategy inference would have no source (SACM source [1..*]), and the
// standards-correct Justification value is axiomatic rather than the legacy
// "justification" literal. The seam reports them unsupported rather than
// silently producing invalid or divergent structure.
TEST(SacmLibraryEdit, SACM23_INT_001_AddChildStrategyAndJustificationUnsupported) {
    for (const sacm_adapter::ChildKind kind :
         {sacm_adapter::ChildKind::Strategy, sacm_adapter::ChildKind::Justification}) {
        sacm_adapter::LoadOutcome loaded = load_fixture();
        ASSERT_NE(loaded.document, nullptr);
        const sacm_adapter::AddChildOutcome added =
            sacm_adapter::apply_add_child(*loaded.document, "G1", kind);
        EXPECT_FALSE(added.supported);
        EXPECT_FALSE(added.applied);
    }
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

// A non-primary-language content edit is not wired (the flat per-language
// content map is a later multi-language slice), so it reports unsupported and
// the command bus re-derives instead -- rather than the seam guessing at a
// LangString entry and diverging from the legacy edit.
TEST(SacmLibraryEdit, SACM23_INT_001_ContentEditUnsupportedForNonPrimaryLanguage) {
    sacm_adapter::LoadOutcome loaded = load_fixture();
    ASSERT_NE(loaded.document, nullptr);
    const sacm_adapter::EditOutcome edit = sacm_adapter::apply_text_edit(
        *loaded.document, "G1", sacm_adapter::TextField::Content, "ja", "\xe5\xae\x89\xe5\x85\xa8");
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
