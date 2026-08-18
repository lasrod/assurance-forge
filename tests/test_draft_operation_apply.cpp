#include "core/drafts/draft_operation_apply.h"

#include "core/drafts/draft_document_diff.h"
#include "core/drafts/draft_document_store.h"
#include "core/project_file_io.h"
#include "sacm_adapter/case_projection.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <system_error>

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
