#include "app/areas/workbench_area.h"

#include "app/actions/terminology_actions.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "app/commands/dispatch.h"
#include "core/audit/audit_store.h"
#include "core/commands/command_bus.h"
#include "core/drafts/draft_change_index.h"
#include "core/drafts/draft_operation_apply.h"
#include "core/reviews/review_proposal.h"
#include "core/terminology_package_service.h"
#include "ui/imgui_buffer_utils.h"
#include "ui/panels/terminology_package_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// What the terminology surfaces show while an AI client's draft defines or
// revises terms (ADR 0016).
//
// Reported from a demo: an MCP client added a definition to a term, and the
// application showed nothing -- the terminology tab read the ACCEPTED package,
// and the definition was in the draft document, which is the only place a
// contributor's change lives until a human accepts it. A restart then showed
// the definition, because by then it had been accepted, together with an audit
// divergence banner over the accept itself.
//
// The property under test: the terminology tab describes the same argument the
// canvas draws, says when what it shows is a draft, and the user's own glossary
// edits go INTO that draft -- through the operations an MCP client sends --
// rather than into the accepted document out from under it. What the draft
// cannot express (packages, category deletion, term-as-context links) is
// refused with a reason until the draft is accepted or discarded.

namespace {

// A case whose glossary holds one classified, defined term -- the state a
// project is in when a client is asked to improve its definitions.
constexpr const char* kGlossarySacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="AC1">
  <name content="Sample" />
  <argumentPackage xmi:id="AP1">
    <name content="Args" />
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1">
      <name content="Top goal" />
      <description xmi:id="d1">
        <content>
          <value lang="en" content="Residual risk is ALARP." />
        </content>
      </description>
    </argumentElement>
  </argumentPackage>
  <terminologyPackage xmi:id="TP1" gid="gid-TP1">
    <name content="Terminology" />
    <terminologyElement xsi:type="sacm:Category" xmi:id="CAT1" gid="gid-CAT1">
      <name content="Regulatory terms" />
    </terminologyElement>
    <terminologyElement xsi:type="sacm:Term" xmi:id="T1" gid="gid-T1" value="ALARP" category="CAT1" externalReference="HSE R2P2, 2001">
      <name content="ALARP" />
      <description xmi:id="d2">
        <content>
          <value lang="en" content="As low as reasonably practicable." />
        </content>
      </description>
    </terminologyElement>
  </terminologyPackage>
</sacm:AssuranceCasePackage>
)";

// The same argument with no glossary at all: the first term a client defines
// creates the terminology package, in the draft.
constexpr const char* kNoGlossarySacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmi:version="2.0" xmi:id="AC1">
  <name content="Sample" />
  <argumentPackage xmi:id="AP1">
    <name content="Args" />
    <argumentElement xsi:type="sacm:Claim" xmi:id="G1">
      <name content="Top goal" />
      <description xmi:id="d1">
        <content>
          <value lang="en" content="All hazards are mitigated." />
        </content>
      </description>
    </argumentElement>
  </argumentPackage>
</sacm:AssuranceCasePackage>
)";

constexpr const char* kAcceptedDefinition = "As low as reasonably practicable.";
constexpr const char* kDraftDefinition =
    "Risk reduced as low as reasonably practicable: further reduction would cost grossly disproportionate to the "
    "benefit gained (HSE R2P2).";

struct GlossaryFixture {
    std::filesystem::path root;
    std::filesystem::path sacm_absolute;
    core::AssuranceProject project;

    ~GlossaryFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::unique_ptr<GlossaryFixture> MakeFixture(const std::string& tag, std::string_view content) {
    auto fixture = std::make_unique<GlossaryFixture>();
    fixture->root =
        std::filesystem::temp_directory_path() /
        ("af_working_glossary_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(fixture->root);
    std::filesystem::create_directories(fixture->root);

    const std::filesystem::path relative = "argument.sacm";
    fixture->sacm_absolute = fixture->root / relative;
    std::ofstream out(fixture->sacm_absolute, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    fixture->project.id = "p";
    fixture->project.name = "Project";
    fixture->project.rootPath = fixture->root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = relative;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture->project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture->project, relative, ensure, error)) << error;
    return fixture;
}

// The argument open with a live command bus, the way the runtime holds it.
void OpenProject(app::AppRuntimeState& state, GlossaryFixture& fixture) {
    state.app_state.current_project = fixture.project;
    ASSERT_TRUE(state.app_state.load_file(fixture.sacm_absolute.string())) << state.app_state.status_message;
    ASSERT_NE(state.app_state.library_document, nullptr);
    std::string error;
    state.command_bus = core::commands::CommandBus::Open(fixture.project, fixture.sacm_absolute, error);
    ASSERT_NE(state.command_bus, nullptr) << error;
}

// A draft document the way the first MCP batch brings one into existence: a
// copy of the accepted argument, taken at the moment of the first edit.
void OpenDraft(app::AppRuntimeState& state, const GlossaryFixture& fixture) {
    std::string error;
    ASSERT_TRUE(
        state.draft_document.Open(fixture.root, fixture.sacm_absolute, *state.app_state.library_document, error))
        << error;
    ASSERT_TRUE(state.draft_document.EnsureDraft(*state.app_state.library_document, error)) << error;
    ASSERT_TRUE(state.draft_document.active());
}

core::reviews::PatchOperation RedefineTerm(const std::string& term_id, const std::string& definition) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::UpdateTerm;
    operation.element = core::reviews::ElementRef{term_id, std::nullopt};
    operation.field = core::reviews::kTermFieldDefinition;
    operation.new_value = definition;
    return operation;
}

core::reviews::PatchOperation
DefineTerm(const std::string& create_ref, const std::string& value, const std::string& definition) {
    core::reviews::PatchOperation operation;
    operation.type = core::reviews::PatchOperationType::CreateTerm;
    operation.create_ref = create_ref;
    operation.text = value;
    operation.new_value = definition;
    return operation;
}

// What `StageOntoDraftDocument` does with a client's batch, minus the ledger.
void StageIntoDraft(app::AppRuntimeState& state, const std::vector<core::reviews::PatchOperation>& operations) {
    const core::drafts::DraftOperationResult applied =
        core::drafts::ApplyOperationsToDraftDocument(*state.draft_document.document(), operations);
    ASSERT_TRUE(applied.applied) << applied.error;
    state.draft_document.MarkChanged();
}

const sacm::Term* FindTerm(const sacm::TerminologyPackage& package, const std::string& id) {
    for (const sacm::Term& term : package.terms) {
        if (term.id == id)
            return &term;
    }
    return nullptr;
}

const ui::panels::TerminologyDraftMark* FindMark(const ui::panels::TerminologyPackagePanelModel& model,
                                                 const std::string& element_id) {
    for (const ui::panels::TerminologyDraftMark& mark : model.draft_marks) {
        if (mark.element_id == element_id)
            return &mark;
    }
    return nullptr;
}

const core::TerminologyPackageRef kGlossaryRef{"TP1", "gid-TP1"};
const core::TerminologyTermRef kAlarpRef{"T1", "gid-T1"};

void SetBuffer(char* buffer, std::size_t size, const std::string& value) {
    ui::CopyToBuffer(buffer, size, value);
}

// The term editor filled the way a user fills it.
void FillTermEditor(app::AppRuntimeState& state,
                    const std::string& value,
                    const std::string& definition,
                    const std::string& external_reference = {}) {
    SetBuffer(state.terminology.term_value_buf, sizeof(state.terminology.term_value_buf), value);
    SetBuffer(state.terminology.term_definition_buf, sizeof(state.terminology.term_definition_buf), definition);
    SetBuffer(state.terminology.term_external_reference_buf,
              sizeof(state.terminology.term_external_reference_buf),
              external_reference);
}

const sacm::TerminologyPackage* AcceptedGlossary(app::AppRuntimeState& state) {
    return core::FindTerminologyPackage(state.app_state.projected_package(), kGlossaryRef);
}

// The draft's glossary as the tab shows it, refreshed for the draft's revision.
const sacm::TerminologyPackage* DraftGlossary(app::AppRuntimeState& state) {
    return app::areas::BuildTerminologyPackagePanelModel(state).package;
}

struct StatusLog {
    std::vector<std::string> messages;
    app::AppEvents::SubscriptionId subscription;
    app::AppRuntimeState& state;

    explicit StatusLog(app::AppRuntimeState& runtime_state) : state(runtime_state) {
        subscription = state.events.Subscribe<app::StatusMessageEvent>(
            [this](const app::StatusMessageEvent& event) { messages.push_back(event.message); });
    }
    ~StatusLog() {
        state.events.Unsubscribe(subscription);
    }

    bool AllMention(std::string_view needle) const {
        return !messages.empty() && std::all_of(messages.begin(), messages.end(), [needle](const std::string& m) {
            return m.find(needle) != std::string::npos;
        });
    }
};

} // namespace

// The reported defect: a definition an MCP client revised was invisible in the
// terminology tab. It has to show there, from the draft, marked as a draft.
TEST(WorkingGlossary, TheTabShowsTheDraftsDefinitionMarkedAsDraft) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("draft_definition", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {RedefineTerm("T1", kDraftDefinition)});
    state.terminology.selected_package_ref = kGlossaryRef;

    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    ASSERT_NE(model.package, nullptr);
    const sacm::Term* shown = FindTerm(*model.package, "T1");
    ASSERT_NE(shown, nullptr);
    EXPECT_EQ(shown->description, kDraftDefinition) << "the tab must show the definition the client wrote";

    // The accepted glossary is untouched: the draft is a proposal, not an edit.
    const sacm::TerminologyPackage* accepted =
        core::FindTerminologyPackage(state.app_state.projected_package(), kGlossaryRef);
    ASSERT_NE(accepted, nullptr);
    ASSERT_NE(FindTerm(*accepted, "T1"), nullptr);
    EXPECT_EQ(FindTerm(*accepted, "T1")->description, kAcceptedDefinition);

    const ui::panels::TerminologyDraftMark* mark = FindMark(model, "T1");
    ASSERT_NE(mark, nullptr) << "the row has to say it is a draft, or a reader quotes it as accepted";
    EXPECT_EQ(mark->change, core::drafts::DraftElementChange::Modified);
    EXPECT_NE(std::find(mark->fields.begin(), mark->fields.end(), "description"), mark->fields.end());
    EXPECT_FALSE(model.working_draft_notice.empty());
    EXPECT_FALSE(model.draft_edit_notice.empty()) << "the tab says where an edit goes before the first click";
    EXPECT_TRUE(model.package_edits_locked);
    EXPECT_FALSE(model.package_edits_locked_reason.empty());
}

TEST(WorkingGlossary, WithoutADraftTheTabShowsTheAcceptedGlossaryUnlocked) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("accepted", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    state.terminology.selected_package_ref = kGlossaryRef;

    EXPECT_EQ(state.WorkingPackage(), &state.app_state.projected_package());

    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    ASSERT_NE(model.package, nullptr);
    ASSERT_NE(FindTerm(*model.package, "T1"), nullptr);
    EXPECT_EQ(FindTerm(*model.package, "T1")->description, kAcceptedDefinition);
    EXPECT_TRUE(model.draft_marks.empty());
    EXPECT_TRUE(model.working_draft_notice.empty());
    EXPECT_TRUE(model.draft_edit_notice.empty());
    EXPECT_FALSE(model.package_edits_locked);
}

// A draft that has changed nothing is indistinguishable from no draft for what
// is SHOWN -- but it still exists, so an edit still belongs in it: made to the
// accepted glossary instead, it would be undone by accepting the draft.
TEST(WorkingGlossary, AnUnchangedDraftShowsTheAcceptedGlossaryButStillTakesTheEdits) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("unchanged", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    state.terminology.selected_package_ref = kGlossaryRef;

    EXPECT_EQ(state.WorkingPackage(), &state.app_state.projected_package());
    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    EXPECT_TRUE(model.draft_marks.empty());
    EXPECT_TRUE(model.working_draft_notice.empty());
    EXPECT_FALSE(model.draft_edit_notice.empty());
    EXPECT_TRUE(model.package_edits_locked);
}

// A case with no glossary grows its first one in the draft. Nothing in the
// accepted package can be selected for it, so the tab shows the draft's.
TEST(WorkingGlossary, AGlossaryTheDraftCreatesIsShownWithoutAnAcceptedCounterpart) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("created", kNoGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {DefineTerm("$hazard", "hazard", "A system state that could lead to harm.")});

    EXPECT_TRUE(state.app_state.projected_package().terminologyPackages.empty());
    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    ASSERT_NE(model.package, nullptr) << "the draft's glossary has to be reachable somewhere";
    ASSERT_EQ(model.package->terms.size(), 1u);
    EXPECT_EQ(model.package->terms.front().value, "hazard");
    const ui::panels::TerminologyDraftMark* mark = FindMark(model, model.package->terms.front().id);
    ASSERT_NE(mark, nullptr);
    EXPECT_EQ(mark->change, core::drafts::DraftElementChange::Added);
}

// The other half of showing the draft: the user's own glossary edits go into
// it. A term added in the tab while a draft is open lands in the draft
// document -- and in its recovery file on disk -- while the accepted glossary
// and its audit log stay exactly as they were, the same way an MCP client's
// CreateTerm does.
TEST(WorkingGlossary, ATermAddedInTheTabGoesIntoTheDraftNotTheAcceptedGlossary) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("add_term", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    state.terminology.selected_package_ref = kGlossaryRef;
    StatusLog statuses(state);

    app::actions::TerminologyActions actions(state);
    actions.BeginAddTerm();
    ASSERT_TRUE(state.terminology.show_term_editor_modal) << "the editor opens: the edit can be made";
    FillTermEditor(state, "hazard", "A system state that could lead to harm.", "ISO 26262-1:2018, 3.75");
    ASSERT_TRUE(actions.ConfirmTermEdit());
    EXPECT_FALSE(state.terminology.show_term_editor_modal);
    EXPECT_TRUE(statuses.AllMention("working draft")) << "the status says where the term went";

    // In the draft, with every field the editor carried.
    const sacm::TerminologyPackage* draft = DraftGlossary(state);
    ASSERT_NE(draft, nullptr);
    ASSERT_EQ(draft->terms.size(), 2u);
    const sacm::Term* added = FindTerm(*draft, state.terminology.selected_term_ref.id);
    ASSERT_NE(added, nullptr) << "the new term is selected";
    EXPECT_EQ(added->value, "hazard");
    EXPECT_EQ(added->description, "A system state that could lead to harm.");
    EXPECT_EQ(added->externalReference, "ISO 26262-1:2018, 3.75");
    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    const ui::panels::TerminologyDraftMark* mark = FindMark(model, added->id);
    ASSERT_NE(mark, nullptr);
    EXPECT_EQ(mark->change, core::drafts::DraftElementChange::Added);

    // Not in the accepted glossary, not in the audit log, and the recovery
    // copy of the draft is on disk.
    ASSERT_NE(AcceptedGlossary(state), nullptr);
    EXPECT_EQ(AcceptedGlossary(state)->terms.size(), 1u);
    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
    EXPECT_TRUE(std::filesystem::exists(state.draft_document.path()));
}

// Editing sends one operation per field that changed, so revising a definition
// leaves the external reference and the category exactly as the seams hold
// them -- and the editor opens on the DRAFT's text, not the accepted one.
TEST(WorkingGlossary, EditingATermInTheDraftRevisesOnlyTheChangedFields) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("edit_term", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {RedefineTerm("T1", kDraftDefinition)});
    state.terminology.selected_package_ref = kGlossaryRef;

    app::actions::TerminologyActions actions(state);
    ASSERT_TRUE(actions.BeginEditTerm(kAlarpRef));
    ASSERT_TRUE(state.terminology.show_term_editor_modal);
    EXPECT_STREQ(state.terminology.term_definition_buf, kDraftDefinition) << "the editor shows the draft's text";

    const std::string revised = std::string(kDraftDefinition) + " Applies to residual risk.";
    SetBuffer(state.terminology.term_definition_buf, sizeof(state.terminology.term_definition_buf), revised);
    ASSERT_TRUE(actions.ConfirmTermEdit());

    const sacm::TerminologyPackage* draft = DraftGlossary(state);
    ASSERT_NE(draft, nullptr);
    const sacm::Term* term = FindTerm(*draft, "T1");
    ASSERT_NE(term, nullptr);
    EXPECT_EQ(term->description, revised);
    EXPECT_EQ(term->externalReference, "HSE R2P2, 2001");
    ASSERT_EQ(term->category_refs.size(), 1u);
    EXPECT_EQ(term->category_refs.front(), "CAT1");
    EXPECT_EQ(FindTerm(*AcceptedGlossary(state), "T1")->description, kAcceptedDefinition);
    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
}

TEST(WorkingGlossary, DeletingATermInTheDraftRemovesItFromTheDraftOnly) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("delete_term", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    state.terminology.selected_package_ref = kGlossaryRef;

    app::actions::TerminologyActions actions(state);
    actions.BeginDeleteTerm(kAlarpRef);
    ASSERT_TRUE(state.terminology.show_delete_term_modal);
    EXPECT_TRUE(state.terminology.pending_delete_term_blockers.empty()) << "nothing references the term";
    ASSERT_TRUE(actions.ConfirmDeleteTerm());
    EXPECT_FALSE(state.terminology.show_delete_term_modal);

    const sacm::TerminologyPackage* draft = DraftGlossary(state);
    ASSERT_NE(draft, nullptr);
    EXPECT_TRUE(draft->terms.empty());
    EXPECT_EQ(AcceptedGlossary(state)->terms.size(), 1u);
    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
}

TEST(WorkingGlossary, CategoriesAddedInTheTabGoIntoTheDraft) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("add_category", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    state.terminology.selected_package_ref = kGlossaryRef;

    app::actions::TerminologyActions actions(state);
    actions.BeginAddCategory();
    ASSERT_TRUE(state.terminology.show_category_editor_modal);
    SetBuffer(state.terminology.category_name_buf, sizeof(state.terminology.category_name_buf), "Hazard / Risk");
    actions.ConfirmCategoryEdit();
    EXPECT_FALSE(state.terminology.show_category_editor_modal);

    const sacm::TerminologyPackage* draft = DraftGlossary(state);
    ASSERT_NE(draft, nullptr);
    ASSERT_EQ(draft->categories.size(), 2u);
    EXPECT_FALSE(state.terminology.selected_category_ref.id.empty()) << "the new category is selected";
    EXPECT_EQ(AcceptedGlossary(state)->categories.size(), 1u);

    // The recommended set lands as one batch, skipping the one that exists.
    actions.SeedRecommendedCategories();
    draft = DraftGlossary(state);
    ASSERT_NE(draft, nullptr);
    EXPECT_EQ(draft->categories.size(), 9u);
    EXPECT_EQ(AcceptedGlossary(state)->categories.size(), 1u);
    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
}

// What the draft's vocabulary cannot express stays refused: the package's own
// fields, deleting the package or a category, and linking a term to an element
// as context all write to the accepted document, which the draft no longer
// descends from. Each refusal names the draft, and nothing reaches the accepted
// document or its log.
TEST(WorkingGlossary, PackageEditsAndCategoryDeletionAreRefusedWhileADraftIsOpen) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("refused", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {RedefineTerm("T1", kDraftDefinition)});
    state.terminology.selected_package_ref = kGlossaryRef;
    StatusLog statuses(state);

    app::actions::TerminologyActions actions(state);
    EXPECT_FALSE(actions.ApplyPackageEdits());
    actions.BeginDeletePackage();
    EXPECT_FALSE(state.terminology.show_delete_package_modal);
    actions.BeginDeleteCategory(core::TerminologyCategoryRef{"CAT1", "gid-CAT1"});
    EXPECT_FALSE(state.terminology.show_delete_category_modal);
    EXPECT_FALSE(actions.AddVisibleTermContextFromCanvas("G1", kGlossaryRef, kAlarpRef));
    EXPECT_FALSE(actions.AddTermAsContextFromCanvas("G1", kGlossaryRef, kAlarpRef));
    EXPECT_TRUE(statuses.AllMention("working draft"));
    EXPECT_EQ(statuses.messages.size(), 5u);

    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
    ASSERT_NE(AcceptedGlossary(state), nullptr);
    EXPECT_EQ(AcceptedGlossary(state)->categories.size(), 1u);
    EXPECT_EQ(AcceptedGlossary(state)->terms.size(), 1u);

    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    EXPECT_TRUE(model.package_edits_locked);
    EXPECT_NE(model.package_edits_locked_reason.find("working draft"), std::string::npos);

    // Reading is not editing: a term the canvas points at still opens.
    EXPECT_TRUE(actions.OpenTermFromCanvas(kGlossaryRef, kAlarpRef));
}

// A draft with no glossary grows its first one when the user defines a term
// from the canvas, the way it does for an MCP client -- not in the accepted
// document underneath the draft.
TEST(WorkingGlossary, QuickDefiningATermInADraftWithNoGlossaryCreatesTheGlossaryInTheDraft) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("quick_define", kNoGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);

    app::actions::TerminologyActions actions(state);
    actions.BeginQuickDefineTerm("G1", "hazard");
    ASSERT_TRUE(state.terminology.show_quick_define_term_modal);
    EXPECT_TRUE(state.terminology.quick_define_target_package_ref.id.empty()) << "no glossary exists yet";
    EXPECT_TRUE(state.app_state.projected_package().terminologyPackages.empty())
        << "nothing was created in the accepted document";

    SetBuffer(state.terminology.term_definition_buf,
              sizeof(state.terminology.term_definition_buf),
              "A system state that could lead to harm.");
    ASSERT_TRUE(actions.ConfirmQuickDefineTerm(false));
    EXPECT_FALSE(state.terminology.show_quick_define_term_modal);

    const sacm::TerminologyPackage* draft = DraftGlossary(state);
    ASSERT_NE(draft, nullptr) << "the draft's new glossary is what the tab shows";
    ASSERT_EQ(draft->terms.size(), 1u);
    EXPECT_EQ(draft->terms.front().value, "hazard");
    EXPECT_EQ(state.terminology.selected_package_ref.id, draft->id);
    EXPECT_EQ(state.terminology.selected_term_ref.id, draft->terms.front().id);
    EXPECT_TRUE(state.app_state.projected_package().terminologyPackages.empty());
    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
}

// Once the draft is gone, edits go to the accepted glossary again, through the
// audited commands, and the package-level lock goes with it.
TEST(WorkingGlossary, DiscardingTheDraftReturnsEditsToTheAcceptedGlossary) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("unlock", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {RedefineTerm("T1", kDraftDefinition)});
    state.terminology.selected_package_ref = kGlossaryRef;
    ASSERT_TRUE(app::areas::BuildTerminologyPackagePanelModel(state).package_edits_locked);

    std::string warning;
    state.draft_document.Discard(warning);

    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    EXPECT_FALSE(model.package_edits_locked);
    EXPECT_TRUE(model.draft_edit_notice.empty());
    ASSERT_NE(model.package, nullptr);
    EXPECT_EQ(FindTerm(*model.package, "T1")->description, kAcceptedDefinition);

    app::actions::TerminologyActions actions(state);
    actions.BeginAddTerm();
    ASSERT_TRUE(state.terminology.show_term_editor_modal);
    FillTermEditor(state, "hazard", "A system state that could lead to harm.");
    ASSERT_TRUE(actions.ConfirmTermEdit());
    // The audited path re-derives the views at the next frame boundary.
    app::commands::ApplyPendingLibraryRederive(state);
    EXPECT_EQ(AcceptedGlossary(state)->terms.size(), 2u);
    EXPECT_FALSE(state.command_bus->Store().Transactions().empty()) << "an accepted-glossary edit is audited";
}
