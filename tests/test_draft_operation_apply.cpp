#include "core/drafts/draft_operation_apply.h"

#include "core/drafts/draft_document_diff.h"
#include "core/drafts/draft_document_store.h"
#include "core/drafts/draft_provenance.h"
#include "core/project_file_io.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// Contributor operations applied straight to the draft document (ADR 0016).
//
// The property that matters most is the one the operation-based path could not
// hold: an operation the document cannot express is refused HERE, in the call
// that made it, with a message the sender can act on -- not accepted into a flat
// model, drawn as pending, and refused later by a seam nothing consulted.

namespace {

std::filesystem::path UniqueTempPath(const std::string& stem) {
    static int counter = 0;
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("af_draft_ops_" + stem + "_" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path);
    return path;
}

std::unique_ptr<sacm_adapter::LibraryDocument> NewDocument(const std::filesystem::path& path) {
    const sacm_adapter::SaveOutcome seed = sacm_adapter::new_case_document_xmi("Blender");
    if (!seed.ok)
        return nullptr;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (!core::WriteTextFileAtomic(path, seed.xml).has_value())
        return nullptr;
    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(path);
    return loaded.ok ? std::move(loaded.document) : nullptr;
}

std::string FirstClaimId(const sacm_adapter::LibraryDocument& document) {
    for (const core::SacmElement& element : sacm_adapter::project_case(document).elements) {
        if (element.type == "claim")
            return element.id;
    }
    return {};
}

const core::SacmElement* Find(const core::AssuranceCase& model, const std::string& id) {
    for (const core::SacmElement& element : model.elements) {
        if (element.id == id)
            return &element;
    }
    return nullptr;
}

core::reviews::PatchOperation
Create(core::reviews::PatchOperationType type, const std::string& create_ref, const std::string& text) {
    core::reviews::PatchOperation operation;
    operation.type = type;
    operation.create_ref = create_ref;
    operation.text = text;
    return operation;
}

core::reviews::ElementRef ById(const std::string& id) {
    return core::reviews::ElementRef{id, std::nullopt};
}

core::reviews::ElementRef ByRef(const std::string& create_ref) {
    return core::reviews::ElementRef{std::nullopt, create_ref};
}

core::reviews::PatchOperation Supports(core::reviews::ElementRef source, core::reviews::ElementRef target) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::AddSupportedBy;
    operation.source = std::move(source);
    operation.target = std::move(target);
    return operation;
}

core::reviews::PatchOperation
UpdateText(core::reviews::ElementRef element, const std::string& field, const std::string& value) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::UpdateElementText;
    operation.element = std::move(element);
    operation.field = field;
    operation.new_value = value;
    return operation;
}

// Owns its temporary project, because `DraftDocumentStore` is deliberately
// non-copyable -- a second handle on one draft is exactly the second writer ADR
// 0008 forbids.
struct Fixture {
    std::filesystem::path root;
    std::filesystem::path argument;
    std::unique_ptr<sacm_adapter::LibraryDocument> accepted;
    core::drafts::DraftDocumentStore store;

    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    core::AssuranceCase Draft() const {
        return store.Projection();
    }
};

std::unique_ptr<Fixture> MakeFixture(const std::string& stem) {
    auto fixture = std::make_unique<Fixture>();
    fixture->root = UniqueTempPath(stem);
    fixture->argument = fixture->root / "arguments" / "main.sacm";
    fixture->accepted = NewDocument(fixture->argument);
    if (fixture->accepted == nullptr)
        return nullptr;
    std::string error;
    if (!fixture->store.Open(fixture->root, fixture->argument, *fixture->accepted, error))
        return nullptr;
    // A draft is created by the first unaccepted change, not by opening the
    // argument. These tests are about what an operation does to a draft, so they
    // start with one.
    if (!fixture->store.EnsureDraft(*fixture->accepted, error))
        return nullptr;
    return fixture;
}

} // namespace

TEST(DraftOperationApplyTest, CreatingAClaimAndAttachingItLandsInTheDraft) {
    const std::unique_ptr<Fixture> f = MakeFixture("create");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    ASSERT_FALSE(top.empty());

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "Blade hazards are controlled."),
         Supports(ByRef("$sub"), ById(top))},
        top);

    ASSERT_TRUE(result.applied) << result.error;
    ASSERT_EQ(result.created_ids.count("$sub"), 1u);
    const std::string sub = result.created_ids.at("$sub");
    EXPECT_EQ(sub.rfind("G", 0), 0u) << "a created claim keeps the GSN naming the application uses: " << sub;

    const core::AssuranceCase draft = f->Draft();
    const core::SacmElement* created = Find(draft, sub);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->content, "Blade hazards are controlled.");

    bool linked = false;
    for (const core::SacmElement& element : draft.elements) {
        if (element.type == "assertedinference" && !element.source_refs.empty() && element.source_refs.front() == sub &&
            !element.target_refs.empty() && element.target_refs.front() == top)
            linked = true;
    }
    EXPECT_TRUE(linked) << "the new claim must support the goal it was attached to";
}

// The defect this whole redesign began with. Under the operation-based path this
// was accepted, materialized, drawn as pending, and refused only at accept.
TEST(DraftOperationApplyTest, NamingTheWrongTextFieldIsRefusedWhenItIsAsked) {
    const std::unique_ptr<Fixture> f = MakeFixture("wrongfield");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::drafts::DraftOperationResult created = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateContext, "$ctx", "Indoor household use.")},
        top);
    ASSERT_TRUE(created.applied) << created.error;
    const std::string context_id = created.created_ids.at("$ctx");

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(), {UpdateText(ById(context_id), "content", "Reworded context.")}, top);

    ASSERT_FALSE(result.applied) << "a context has no content, and the refusal must arrive now";
    EXPECT_NE(result.error.find("description"), std::string::npos)
        << "the refusal must name the field to use instead: " << result.error;
    EXPECT_EQ(result.failed_operation, 1u);
}

TEST(DraftOperationApplyTest, OmittingTheFieldResolvesItFromTheElementKind) {
    const std::unique_ptr<Fixture> f = MakeFixture("resolve");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::drafts::DraftOperationResult created = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateContext, "$ctx", "Indoor household use.")},
        top);
    ASSERT_TRUE(created.applied) << created.error;
    const std::string context_id = created.created_ids.at("$ctx");

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(), {UpdateText(ById(context_id), "", "Indoor household food preparation.")}, top);

    ASSERT_TRUE(result.applied) << result.error;
    const core::AssuranceCase draft = f->Draft();
    const core::SacmElement* context = Find(draft, context_id);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->description, "Indoor household food preparation.")
        << "a context's text belongs in its description";
    EXPECT_TRUE(context->content.empty());
}

// A batch is one unit. A client whose third operation is refused must not have
// to reason about whether its first two survived.
TEST(DraftOperationApplyTest, ARefusedBatchLeavesTheDraftExactlyAsItWas) {
    const std::unique_ptr<Fixture> f = MakeFixture("atomic");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const core::AssuranceCase before = f->Draft();

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$a", "A claim that would be fine."),
         Supports(ByRef("$a"), ById(top)),
         UpdateText(ById("does-not-exist"), "content", "An edit to nothing.")},
        top);

    ASSERT_FALSE(result.applied);
    EXPECT_EQ(result.failed_operation, 3u) << "the client is told which operation failed";
    EXPECT_TRUE(result.created_ids.empty()) << "a refused batch reports no created ids";

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(before, f->Draft());
    EXPECT_FALSE(diff.touches_anything()) << "the two operations before the failure must not have survived";
}

TEST(DraftOperationApplyTest, AnUnknownCreateRefIsRefusedWithSomethingToActOn) {
    const std::unique_ptr<Fixture> f = MakeFixture("badref");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {Supports(ByRef("$ghost"), ById(top))}, top);

    ASSERT_FALSE(result.applied);
    EXPECT_NE(result.error.find("$ghost"), std::string::npos) << result.error;
}

// A term or category is an ordinary element in the projection, and `list_terms`
// invites an agent to read them before writing argument. Attaching one as
// context is a request the library refuses, and refusing it here is the whole
// point: the alternative is a draft that renders and cannot be accepted.
TEST(DraftOperationApplyTest, AttachingSomethingThatIsNotAnArgumentAssetIsRefused) {
    const std::unique_ptr<Fixture> f = MakeFixture("badendpoint");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(), {Supports(ById("no-such-element"), ById(top))}, top);

    ASSERT_FALSE(result.applied) << "a link to an element that does not exist cannot be created";
    EXPECT_FALSE(result.error.empty());
}

TEST(DraftOperationApplyTest, TranslationsRideAlongWithTheTextTheyTranslate) {
    const std::unique_ptr<Fixture> f = MakeFixture("translate");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation update = UpdateText(ById(top), "content", "The blender is acceptably safe.");
    update.translations["ja"] = "ブレンダーは十分に安全である。";

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {update}, top);

    ASSERT_TRUE(result.applied) << result.error;
    // The projection is held, not passed straight into `Find`: it is returned by
    // value, so a pointer into a temporary would dangle before it was read.
    const core::AssuranceCase draft = f->Draft();
    const core::SacmElement* claim = Find(draft, top);
    ASSERT_NE(claim, nullptr);
    EXPECT_EQ(claim->content, "The blender is acceptably safe.");
    ASSERT_EQ(claim->content_langs.count("ja"), 1u) << "a bilingual claim is accepted or refused whole";
    EXPECT_EQ(claim->content_langs.at("ja"), "ブレンダーは十分に安全である。");
}

TEST(DraftOperationApplyTest, RemovingAnElementTakesItOutOfTheDraft) {
    const std::unique_ptr<Fixture> f = MakeFixture("remove");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::drafts::DraftOperationResult created = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "A claim to remove."),
         Supports(ByRef("$sub"), ById(top))},
        top);
    ASSERT_TRUE(created.applied) << created.error;
    const std::string sub = created.created_ids.at("$sub");

    core::reviews::PatchOperation remove;
    remove.type = core::reviews::PatchOperationType::RemoveElement;
    remove.element = ById(sub);
    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {remove}, top);

    ASSERT_TRUE(result.applied) << result.error;
    EXPECT_EQ(Find(f->Draft(), sub), nullptr);
}

// A case with no glossary grows its first one here rather than refusing --
// otherwise a project could never gain one over MCP.
TEST(DraftOperationApplyTest, DefiningATermCreatesTheGlossaryWhenTheCaseHasNone) {
    const std::unique_ptr<Fixture> f = MakeFixture("term");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation define = Create(core::reviews::PatchOperationType::CreateTerm, "$hazard", "hazard");
    define.new_value = "A system state that, with environmental conditions, could lead to harm.";

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {define}, top);

    ASSERT_TRUE(result.applied) << result.error;
    const std::string term_id = result.created_ids.at("$hazard");
    const core::AssuranceCase draft = f->Draft();
    const core::SacmElement* term = Find(draft, term_id);
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->type, "term");
    EXPECT_EQ(term->content, "hazard") << "a term's value is the word it defines";
    EXPECT_EQ(term->description, "A system state that, with environmental conditions, could lead to harm.");
}

// Several terms in one batch share the glossary the batch created, rather than
// each making another.
TEST(DraftOperationApplyTest, TwoTermsInOneBatchShareOneGlossary) {
    const std::unique_ptr<Fixture> f = MakeFixture("twoterms");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation first = Create(core::reviews::PatchOperationType::CreateTerm, "$a", "hazard");
    first.new_value = "First definition.";
    core::reviews::PatchOperation second = Create(core::reviews::PatchOperationType::CreateTerm, "$b", "mitigation");
    second.new_value = "Second definition.";

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {first, second}, top);

    ASSERT_TRUE(result.applied) << result.error;
    EXPECT_EQ(result.created_ids.size(), 2u);
}

// SACM gives an ExpressionElement one value (clause 10.11), so a term cannot be
// stated in two languages. Refused when it is asked, naming the field that can
// carry a translation.
TEST(DraftOperationApplyTest, TranslatingATermsValueIsRefusedAndPointsAtTheDefinition) {
    const std::unique_ptr<Fixture> f = MakeFixture("termlang");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation define = Create(core::reviews::PatchOperationType::CreateTerm, "$t", "hazard");
    define.new_value = "A definition.";
    const core::drafts::DraftOperationResult defined =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {define}, top);
    ASSERT_TRUE(defined.applied) << defined.error;
    // By id, not by create_ref: a create_ref is batch-local, so reusing `$t`
    // here would be refused as an unknown reference and this test would pass
    // without ever reaching the rule it exists for.
    const std::string term_id = defined.created_ids.at("$t");

    core::reviews::PatchOperation retitle;
    retitle.type = core::reviews::PatchOperationType::UpdateTerm;
    retitle.element = ById(term_id);
    retitle.field = core::reviews::kTermFieldValue;
    retitle.new_value = "hazard";
    retitle.translations["ja"] = "ハザード";

    const core::drafts::DraftOperationResult staged =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {retitle}, top);
    ASSERT_FALSE(staged.applied) << "a term's value is one string and cannot be translated";
    EXPECT_NE(staged.error.find("definition"), std::string::npos)
        << "the refusal must name the field that can carry a translation: " << staged.error;
}

TEST(DraftOperationApplyTest, UpdateTermOnSomethingThatIsNotATermSaysWhatToUseInstead) {
    const std::unique_ptr<Fixture> f = MakeFixture("nonterm");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation update;
    update.type = core::reviews::PatchOperationType::UpdateTerm;
    update.element = ById(top);
    update.field = core::reviews::kTermFieldDefinition;
    update.new_value = "Not a term.";

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {update}, top);

    ASSERT_FALSE(result.applied);
    EXPECT_NE(result.error.find("UpdateElementText"), std::string::npos)
        << "the refusal must name the operation to use instead: " << result.error;
}

// Nothing an agent stages reaches the accepted argument until a human accepts.
TEST(DraftOperationApplyTest, StagingNeverTouchesTheAcceptedArgument) {
    const std::unique_ptr<Fixture> f = MakeFixture("untouched");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const std::expected<std::string, std::string> before = core::ReadTextFile(f->argument);
    ASSERT_TRUE(before.has_value());

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "Staged, not accepted.")},
        top);
    ASSERT_TRUE(result.applied) << result.error;
    std::string error;
    ASSERT_TRUE(f->store.Save(error)) << error;

    const std::expected<std::string, std::string> after = core::ReadTextFile(f->argument);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after.value(), before.value()) << "the accepted argument does not move until a human accepts";
}

// A GSN Strategy is the reasoning of an inference, never one of its ends
// (docs/sacm/sacm-gsn-mapping.md, rule 5). Wiring it as an end produces an
// argument that looks perfectly ordinary on the canvas and is reported as an
// error by the well-formedness check -- which is what a user saw.
TEST(DraftOperationApplyTest, AStrategyAttachesAsReasoningRatherThanAsAnEnd) {
    const std::unique_ptr<Fixture> fixture = MakeFixture("strategy-reasoning");
    ASSERT_NE(fixture, nullptr);
    const std::string goal = FirstClaimId(*fixture->store.document());
    ASSERT_FALSE(goal.empty());

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *fixture->store.document(),
        {Create(core::reviews::PatchOperationType::CreateStrategy, "$s", "Argue over the identified hazards"),
         Supports(ByRef("$s"), ById(goal))},
        goal);
    ASSERT_TRUE(result.applied) << result.error;
    const std::string strategy = result.created_ids.at("$s");

    // A bare strategy carries no relationship at all: an AssertedInference with
    // only a reasoning end has no source, which SACM's source [1..*] forbids.
    const core::AssuranceCase model = fixture->Draft();
    for (const core::SacmElement& element : model.elements) {
        const bool names_strategy =
            std::find(element.source_refs.begin(), element.source_refs.end(), strategy) != element.source_refs.end() ||
            std::find(element.target_refs.begin(), element.target_refs.end(), strategy) != element.target_refs.end();
        EXPECT_FALSE(names_strategy) << "relationship " << element.id << " wires the strategy as one of its ends";
    }
}

// A GSN Solution and a GSN Context are both ArtifactReference and are told apart
// only by the relationship that attaches them. Attaching a Solution by inference
// leaves it indistinguishable from a Context, and it renders as one.
TEST(DraftOperationApplyTest, ASolutionAttachesAsEvidenceSoItIsNotIndistinguishableFromAContext) {
    const std::unique_ptr<Fixture> fixture = MakeFixture("solution-evidence");
    ASSERT_NE(fixture, nullptr);
    const std::string goal = FirstClaimId(*fixture->store.document());
    ASSERT_FALSE(goal.empty());

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *fixture->store.document(),
        {Create(core::reviews::PatchOperationType::CreateSolution, "$sn", "Triggering condition analysis"),
         Supports(ByRef("$sn"), ById(goal))},
        goal);
    ASSERT_TRUE(result.applied) << result.error;
    const std::string solution = result.created_ids.at("$sn");

    bool attached_as_evidence = false;
    const core::AssuranceCase model = fixture->Draft();
    for (const core::SacmElement& element : model.elements) {
        if (std::find(element.source_refs.begin(), element.source_refs.end(), solution) == element.source_refs.end())
            continue;
        EXPECT_EQ(element.type, "assertedevidence")
            << "a solution attached by " << element.type << " renders with Context notation";
        if (element.type == "assertedevidence")
            attached_as_evidence = true;
    }
    EXPECT_TRUE(attached_as_evidence) << "the solution was not attached to anything";
}

// The sub-goal that gives a deferred strategy inference its source. Without
// this, the strategy a user adds can never gain the inference that makes it
// mean anything.
TEST(DraftOperationApplyTest, ASubGoalUnderAStrategyMaterializesTheStrategysInference) {
    const std::unique_ptr<Fixture> fixture = MakeFixture("strategy-subgoal");
    ASSERT_NE(fixture, nullptr);
    const std::string goal = FirstClaimId(*fixture->store.document());
    ASSERT_FALSE(goal.empty());

    const core::drafts::DraftOperationResult staged = core::drafts::ApplyOperationsToDraftDocument(
        *fixture->store.document(),
        {Create(core::reviews::PatchOperationType::CreateStrategy, "$s", "Argue over the identified hazards"),
         Supports(ByRef("$s"), ById(goal)),
         Create(core::reviews::PatchOperationType::CreateClaim, "$g", "Blade hazards are controlled"),
         Supports(ByRef("$g"), ByRef("$s"))},
        goal);
    ASSERT_TRUE(staged.applied) << staged.error;
    const std::string strategy = staged.created_ids.at("$s");
    const std::string subgoal = staged.created_ids.at("$g");

    // One inference, from the sub-goal to the goal the strategy supports, with
    // the strategy as its reasoning rather than as an end.
    const core::AssuranceCase model = fixture->Draft();
    bool found = false;
    for (const core::SacmElement& element : model.elements) {
        if (element.type != "assertedinference")
            continue;
        if (std::find(element.source_refs.begin(), element.source_refs.end(), subgoal) == element.source_refs.end())
            continue;
        found = true;
        EXPECT_NE(std::find(element.target_refs.begin(), element.target_refs.end(), goal), element.target_refs.end())
            << "the strategy's inference must conclude at the goal the strategy supports";
        EXPECT_EQ(std::find(element.source_refs.begin(), element.source_refs.end(), strategy),
                  element.source_refs.end())
            << "the strategy must not be a source of its own inference";
    }
    EXPECT_TRUE(found) << "no inference was materialized for the strategy's first sub-goal";
}

// Reported twice from real sessions: an agent staged a whole glossary, set the
// category and external reference the guidance names as checked, and left every
// definition empty -- because nothing asked for one. The terms reached the
// accepted argument as words with nothing beside them, which reads as a defined
// glossary and is not one.
TEST(DraftOperationApplyTest, ATermWithNoDefinitionIsRefusedAndSaysWhereItGoes) {
    const std::unique_ptr<Fixture> f = MakeFixture("nodefinition");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation define = Create(core::reviews::PatchOperationType::CreateTerm, "$alarp", "ALARP");

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {define}, top);

    ASSERT_FALSE(result.applied) << "a term that defines nothing must not be staged";
    EXPECT_NE(result.error.find("new_value"), std::string::npos)
        << "the refusal must say where the definition goes: " << result.error;
    EXPECT_NE(result.error.find("ALARP"), std::string::npos)
        << "the refusal must name the term it is about: " << result.error;
}

// The refusal must not cost the ordinary case.
TEST(DraftOperationApplyTest, ATermWithADefinitionStillStages) {
    const std::unique_ptr<Fixture> f = MakeFixture("withdefinition");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());

    core::reviews::PatchOperation define = Create(core::reviews::PatchOperationType::CreateTerm, "$alarp", "ALARP");
    define.new_value = "As low as reasonably practicable.";

    const core::drafts::DraftOperationResult result =
        core::drafts::ApplyOperationsToDraftDocument(*f->store.document(), {define}, top);

    ASSERT_TRUE(result.applied) << result.error;
    const core::AssuranceCase draft = f->Draft();
    const core::SacmElement* term = Find(draft, result.created_ids.at("$alarp"));
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->description, "As low as reasonably practicable.");
}

// ---------------------------------------------------------------------------
// Provenance (ADR 0016, #409): who made a change is written onto the elements
// the change touched, in the same all-or-nothing batch as the change.
// ---------------------------------------------------------------------------

namespace {

core::drafts::DraftProvenance McpProvenance(const std::string& group_id) {
    core::drafts::DraftProvenance provenance;
    provenance.contribution_id = group_id;
    provenance.source = core::drafts::DraftSource::Mcp;
    provenance.label = "Claude Code";
    provenance.session_id = "session-7";
    provenance.title = "Develop the top goal";
    provenance.rationale = "The top goal had no support.";
    return provenance;
}

core::drafts::DraftProvenance HumanProvenance(const std::string& author) {
    core::drafts::DraftProvenance provenance;
    provenance.contribution_id = core::drafts::HumanContributionId(author);
    provenance.source = core::drafts::DraftSource::Human;
    provenance.label = author;
    return provenance;
}

const core::drafts::DraftContribution* FindContribution(const std::vector<core::drafts::DraftContribution>& all,
                                                        const std::string& contribution_id) {
    for (const core::drafts::DraftContribution& contribution : all) {
        if (contribution.provenance.contribution_id == contribution_id)
            return &contribution;
    }
    return nullptr;
}

std::string
RelationshipBetween(const core::AssuranceCase& model, const std::string& source, const std::string& target) {
    for (const core::SacmElement& element : model.elements) {
        if (!element.source_refs.empty() && element.source_refs.front() == source && !element.target_refs.empty() &&
            element.target_refs.front() == target)
            return element.id;
    }
    return {};
}

} // namespace

// The claim and the relationship attaching it are what the batch made. The goal
// it was attached under did not change, so it is not this contribution's to
// claim -- tagging it would name an agent as a contributor to an accepted goal
// it never edited.
TEST(DraftOperationApplyTest, ProvenanceIsWrittenOnExactlyWhatTheBatchAddedOrChanged) {
    const std::unique_ptr<Fixture> f = MakeFixture("provenance_exact");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const core::drafts::DraftProvenance provenance = McpProvenance("group-1");

    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "Blade hazards are controlled."),
         Supports(ByRef("$sub"), ById(top))},
        top,
        &provenance);
    ASSERT_TRUE(result.applied) << result.error;
    const std::string sub = result.created_ids.at("$sub");
    const std::string link = RelationshipBetween(f->Draft(), sub, top);
    ASSERT_FALSE(link.empty());

    std::vector<std::string> expected{sub, link};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(result.attributed_ids, expected);

    const std::vector<core::drafts::DraftContribution> read = core::drafts::ReadDraftProvenance(*f->store.document());
    ASSERT_EQ(read.size(), 1u);
    const core::drafts::DraftProvenance& recorded = read.front().provenance;
    EXPECT_EQ(recorded.contribution_id, "group-1");
    EXPECT_EQ(recorded.source, core::drafts::DraftSource::Mcp);
    EXPECT_EQ(recorded.label, "Claude Code");
    EXPECT_EQ(recorded.session_id, "session-7");
    EXPECT_EQ(recorded.title, "Develop the top goal");
    EXPECT_EQ(recorded.rationale, "The top goal had no support.");
    EXPECT_EQ(read.front().element_ids, expected);
}

// The reason the contribution is in the tag KEY. Had the second contributor
// overwritten the first, the claim would read as the reviewer's alone, and the
// agent that wrote it would vanish from the record of its own sentence.
TEST(DraftOperationApplyTest, ASecondContributorToAnElementDoesNotEraseTheFirst) {
    const std::unique_ptr<Fixture> f = MakeFixture("provenance_two");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const core::drafts::DraftProvenance agent = McpProvenance("group-1");
    const core::drafts::DraftOperationResult created = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "Blade hazards are controlled."),
         Supports(ByRef("$sub"), ById(top))},
        top,
        &agent);
    ASSERT_TRUE(created.applied) << created.error;
    const std::string sub = created.created_ids.at("$sub");

    const core::drafts::DraftProvenance reviewer = HumanProvenance("Ada");
    const core::drafts::DraftOperationResult reworded = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(), {UpdateText(ById(sub), "content", "Blade hazards are fully controlled.")}, {}, &reviewer);
    ASSERT_TRUE(reworded.applied) << reworded.error;
    EXPECT_EQ(reworded.attributed_ids, std::vector<std::string>{sub})
        << "the reword touched the claim only, not the relationship the agent made";

    const std::vector<core::drafts::DraftContribution> read = core::drafts::ReadDraftProvenance(*f->store.document());
    ASSERT_EQ(read.size(), 2u);
    const core::drafts::DraftContribution* first = FindContribution(read, "group-1");
    const core::drafts::DraftContribution* second = FindContribution(read, reviewer.contribution_id);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->element_ids, created.attributed_ids) << "the agent keeps its record of everything it made";
    EXPECT_EQ(second->element_ids, std::vector<std::string>{sub});
    EXPECT_EQ(second->provenance.source, core::drafts::DraftSource::Human);
    EXPECT_EQ(second->provenance.label, "Ada");
}

// The draft is written and re-read repeatedly over its life (ADR 0016), so
// provenance that only lived in memory would be lost on the next project open.
TEST(DraftOperationApplyTest, ProvenanceSurvivesSavingAndReopeningTheDraft) {
    const std::unique_ptr<Fixture> f = MakeFixture("provenance_reopen");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const core::drafts::DraftProvenance provenance = McpProvenance("group-3");
    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(), {UpdateText(ById(top), "content", "The blender is acceptably safe.")}, {}, &provenance);
    ASSERT_TRUE(result.applied) << result.error;
    f->store.MarkChanged();
    std::string error;
    ASSERT_TRUE(f->store.Save(error)) << error;

    core::drafts::DraftDocumentStore reopened;
    ASSERT_TRUE(reopened.Open(f->root, f->argument, *f->accepted, error)) << error;
    ASSERT_TRUE(reopened.active());
    const std::vector<core::drafts::DraftContribution> read = core::drafts::ReadDraftProvenance(*reopened.document());
    ASSERT_EQ(read.size(), 1u);
    EXPECT_EQ(read.front().provenance.contribution_id, "group-3");
    EXPECT_EQ(read.front().provenance.rationale, "The top goal had no support.");
    EXPECT_EQ(read.front().element_ids, std::vector<std::string>{top});
}

// Provenance is written inside the batch's copy, so a contribution that cannot
// be named refuses the whole batch. The alternative -- landing the change
// unattributed -- is the state this design exists to make impossible.
TEST(DraftOperationApplyTest, ABatchWhoseProvenanceCannotBeRecordedIsRefusedWhole) {
    const std::unique_ptr<Fixture> f = MakeFixture("provenance_refused");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const std::size_t before = f->Draft().elements.size();

    const core::drafts::DraftProvenance provenance = McpProvenance("group.1");
    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(),
        {Create(core::reviews::PatchOperationType::CreateClaim, "$sub", "Blade hazards are controlled."),
         Supports(ByRef("$sub"), ById(top))},
        top,
        &provenance);

    EXPECT_FALSE(result.applied);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.created_ids.empty());
    EXPECT_TRUE(result.attributed_ids.empty());
    EXPECT_EQ(f->Draft().elements.size(), before) << "nothing from the refused batch may remain in the draft";
    EXPECT_TRUE(core::drafts::ReadDraftProvenance(*f->store.document()).empty());
}

// End to end against the real strip: the tags the applier writes are under the
// prefix accept removes. A writer and stripper that disagreed would leak draft
// provenance into an accepted safety case.
TEST(DraftOperationApplyTest, AcceptStripsTheProvenanceTheApplierWrote) {
    const std::unique_ptr<Fixture> f = MakeFixture("provenance_accept");
    ASSERT_NE(f, nullptr);
    const std::string top = FirstClaimId(*f->store.document());
    const core::drafts::DraftProvenance provenance = McpProvenance("group-1");
    const core::drafts::DraftOperationResult result = core::drafts::ApplyOperationsToDraftDocument(
        *f->store.document(), {UpdateText(ById(top), "content", "The blender is acceptably safe.")}, {}, &provenance);
    ASSERT_TRUE(result.applied) << result.error;
    ASSERT_FALSE(core::drafts::ReadDraftProvenance(*f->store.document()).empty());
    f->store.MarkChanged();

    std::string error;
    ASSERT_TRUE(f->store.AcceptInto(f->argument, error)) << error;
    const std::expected<std::string, std::string> written = core::ReadTextFile(f->argument);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->find(core::drafts::kDraftProvenanceTagPrefix), std::string::npos);
    EXPECT_NE(written->find("The blender is acceptably safe."), std::string::npos);
}

TEST(DraftOperationApplyTest, HandEditsFromOnePersonShareOneContribution) {
    EXPECT_EQ(core::drafts::HumanContributionId("Ada"), core::drafts::HumanContributionId("Ada"));
    EXPECT_NE(core::drafts::HumanContributionId("Ada"), core::drafts::HumanContributionId("Grace"));
    EXPECT_EQ(core::drafts::HumanContributionId("Ada.Lovelace").find('.'), std::string::npos)
        << "a name with a dot must still produce a usable key segment";
}
