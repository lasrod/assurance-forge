#include "app/areas/workbench_area.h"

#include "app/actions/terminology_actions.h"
#include "app/app_events.h"
#include "app/app_runtime_state.h"
#include "core/audit/audit_store.h"
#include "core/commands/command_bus.h"
#include "core/drafts/draft_change_index.h"
#include "core/drafts/draft_operation_apply.h"
#include "core/reviews/review_proposal.h"
#include "core/terminology_package_service.h"
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
// canvas draws, says when what it shows is a draft, and cannot be used to edit
// the accepted document out from under that draft.

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
    EXPECT_TRUE(model.editing_locked);
    EXPECT_FALSE(model.editing_locked_reason.empty());
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
    EXPECT_FALSE(model.editing_locked);
}

// A draft that has changed nothing is indistinguishable from no draft for what
// is SHOWN -- but it still exists, so editing the accepted glossary would still
// be undone by accepting it.
TEST(WorkingGlossary, AnUnchangedDraftShowsTheAcceptedGlossaryButStillLocksEditing) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("unchanged", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    state.terminology.selected_package_ref = kGlossaryRef;

    EXPECT_EQ(state.WorkingPackage(), &state.app_state.projected_package());
    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    EXPECT_TRUE(model.draft_marks.empty());
    EXPECT_TRUE(model.working_draft_notice.empty());
    EXPECT_TRUE(model.editing_locked);
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

// The other half of showing the draft: the glossary actions still write to the
// accepted document, and while a draft exists that document is not what the
// user is looking at. An edit made to it would be silently undone by the
// accept, so every glossary write is refused with a reason, and no editor
// opens onto it.
TEST(WorkingGlossary, GlossaryEditsAreRefusedWhileADraftIsOpen) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("refused", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {RedefineTerm("T1", kDraftDefinition)});
    state.terminology.selected_package_ref = kGlossaryRef;

    std::vector<std::string> statuses;
    const app::AppEvents::SubscriptionId subscription = state.events.Subscribe<app::StatusMessageEvent>(
        [&statuses](const app::StatusMessageEvent& event) { statuses.push_back(event.message); });

    app::actions::TerminologyActions actions(state);
    actions.BeginAddTerm();
    EXPECT_FALSE(state.terminology.show_term_editor_modal);
    EXPECT_FALSE(actions.BeginEditTerm(core::TerminologyTermRef{"T1", "gid-T1"}));
    EXPECT_FALSE(state.terminology.show_term_editor_modal);
    EXPECT_FALSE(actions.ConfirmTermEdit());
    actions.BeginAddCategory();
    EXPECT_FALSE(state.terminology.show_category_editor_modal);
    actions.SeedRecommendedCategories();
    actions.BeginDeleteTerm(core::TerminologyTermRef{"T1", "gid-T1"});
    EXPECT_FALSE(state.terminology.show_delete_term_modal);

    ASSERT_FALSE(statuses.empty());
    for (const std::string& status : statuses)
        EXPECT_NE(status.find("working draft"), std::string::npos) << status;

    // Nothing reached the accepted document or its audit log.
    EXPECT_TRUE(state.command_bus->Store().Transactions().empty());
    const sacm::TerminologyPackage* accepted =
        core::FindTerminologyPackage(state.app_state.projected_package(), kGlossaryRef);
    ASSERT_NE(accepted, nullptr);
    EXPECT_EQ(accepted->categories.size(), 1u);
    EXPECT_EQ(accepted->terms.size(), 1u);

    // Reading is not editing: a term the canvas points at still opens.
    EXPECT_TRUE(actions.OpenTermFromCanvas(kGlossaryRef, core::TerminologyTermRef{"T1", "gid-T1"}));
    state.events.Unsubscribe(subscription);
}

// Once the draft is gone, the lock goes with it.
TEST(WorkingGlossary, DiscardingTheDraftUnlocksEditing) {
    std::unique_ptr<GlossaryFixture> fixture = MakeFixture("unlock", kGlossarySacm);
    app::AppRuntimeState state;
    OpenProject(state, *fixture);
    OpenDraft(state, *fixture);
    StageIntoDraft(state, {RedefineTerm("T1", kDraftDefinition)});
    state.terminology.selected_package_ref = kGlossaryRef;
    ASSERT_TRUE(app::areas::BuildTerminologyPackagePanelModel(state).editing_locked);

    std::string warning;
    state.draft_document.Discard(warning);

    const ui::panels::TerminologyPackagePanelModel model = app::areas::BuildTerminologyPackagePanelModel(state);
    EXPECT_FALSE(model.editing_locked);
    ASSERT_NE(model.package, nullptr);
    EXPECT_EQ(FindTerm(*model.package, "T1")->description, kAcceptedDefinition);

    app::actions::TerminologyActions actions(state);
    actions.BeginAddTerm();
    EXPECT_TRUE(state.terminology.show_term_editor_modal);
}
