// Regression tests for the strategy single-inference encoding's lifecycle
// (goal -> strategy -> sub-goal -> remove/re-add), driven through the REAL
// command bus with a library document present, mirroring the app's dispatch +
// frame-boundary re-derive.
//
// The removal cases pin a user-reported defect: removing a strategy's only
// sub-goal leaves the strategy's single inference sourceless (deliberately, so
// the strategy stays placed under its goal), and the NodeOnly removal's
// reparent diff then either forwarded that sourceless inference (refused,
// SACM-CMD-005 clause 11.13 source[1..*]) or the render-only
// `__pending_inference` placeholder (refused, SACM-CMD-001 -- never in the
// library) to `apply_set_relationship_ends`, failing the whole removal. Since
// the context menu only enables NodeOnly for a childless node, every childless
// strategy was unremovable from the UI.

#include "core/audit/audit_paths.h"
#include "core/audit/audit_store.h"
#include "core/commands/command_bus.h"
#include "core/commands/element_commands.h"
#include "core/derived_views.h"
#include "core/element_factory.h"
#include "core/project_model.h"
#include "parser/xml_parser.h"
#include "sacm_adapter/library_load.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr const char* kEmptyPackageSacm = R"(<?xml version="1.0" encoding="UTF-8"?>
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/2.2/Argumentation" id="AC1" name="Sample">
  <argumentPackage id="AP1" name="Args"/>
</sacm:AssuranceCasePackage>
)";

std::filesystem::path MakeTempProjectRoot(const std::string& tag) {
    auto root = std::filesystem::temp_directory_path() /
                ("af_strategy_" + tag + "_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

struct EditFixture {
    core::AssuranceProject project;
    std::filesystem::path sacm_abs;
    sacm::AssuranceCasePackage package;
    parser::AssuranceCase model;
    std::unique_ptr<sacm_adapter::LibraryDocument> document;
    std::unique_ptr<core::commands::CommandBus> bus;
};

std::unique_ptr<EditFixture> MakeFixture(const std::string& tag) {
    auto fixture = std::make_unique<EditFixture>();
    const auto root = MakeTempProjectRoot(tag);
    const std::filesystem::path sacm_rel = "argument.sacm";
    WriteFile(root / sacm_rel, kEmptyPackageSacm);

    fixture->project.id = "p";
    fixture->project.name = "Project";
    fixture->project.rootPath = root;
    core::ProjectFileEntry entry;
    entry.id = "f1";
    entry.relativePath = sacm_rel;
    entry.role = core::ProjectFileRole::SacmArgument;
    fixture->project.files.push_back(entry);

    core::audit::EnsureAuditStoreResult ensure;
    std::string error;
    EXPECT_TRUE(core::audit::EnsureAuditStore(fixture->project, sacm_rel, ensure, error)) << error;
    fixture->sacm_abs = fixture->project.rootPath / sacm_rel;

    sacm_adapter::LoadOutcome loaded = sacm_adapter::load_document(fixture->sacm_abs);
    EXPECT_TRUE(loaded.ok);
    EXPECT_NE(loaded.document, nullptr);
    if (loaded.document == nullptr)
        return fixture;
    core::RebuildDerivedViewsFromLibrary(*loaded.document, fixture->model, fixture->package);
    fixture->document = std::move(loaded.document);

    fixture->bus = core::commands::CommandBus::Open(fixture->project, fixture->sacm_abs, error);
    EXPECT_TRUE(fixture->bus) << error;
    return fixture;
}

core::commands::CommandContext MakeContext(EditFixture& fixture) {
    return core::commands::CommandContext{fixture.model, fixture.package, fixture.document.get()};
}

// Mirror app::commands::DispatchAuditedCommand + the next frame's
// ApplyPendingLibraryRederive: run the command, then re-derive the live views
// from the library when the command flipped.
core::commands::CommandResult
RunCommand(EditFixture& fixture, core::commands::ICommand& command, core::commands::CommandContext& ctx) {
    core::commands::CommandResult result = fixture.bus->Execute(command, ctx, "tester");
    if (ctx.library_primary && fixture.document != nullptr)
        core::RebuildDerivedViewsFromLibrary(*fixture.document, fixture.model, fixture.package);
    ctx.library_primary = false;
    return result;
}

const parser::SacmElement* FindElement(const parser::AssuranceCase& model, const std::string& id) {
    const auto it = std::find_if(model.elements.begin(), model.elements.end(), [&](const parser::SacmElement& element) {
        return element.id == id;
    });
    return it == model.elements.end() ? nullptr : &*it;
}

std::string ContentOf(const parser::AssuranceCase& model, const std::string& id) {
    const parser::SacmElement* element = FindElement(model, id);
    return element == nullptr ? std::string("<missing>") : element->content;
}

int CountInferences(const parser::AssuranceCase& model) {
    return static_cast<int>(std::count_if(model.elements.begin(),
                                          model.elements.end(),
                                          [](const parser::SacmElement& e) { return e.type == "assertedinference"; }));
}

// Builds goal -> strategy -> sub-goal and returns the three generated ids.
struct GoalStrategySubgoal {
    std::string goal;
    std::string strategy;
    std::string sub_goal;
};

GoalStrategySubgoal BuildGoalStrategySubgoal(EditFixture& fixture, core::commands::CommandContext& ctx) {
    GoalStrategySubgoal ids;

    core::commands::CreateTopGoalCommand top_goal;
    EXPECT_TRUE(RunCommand(fixture, top_goal, ctx).success);
    ids.goal = top_goal.GeneratedId();

    core::commands::CreateChildElementCommand add_strategy(ids.goal, core::NewElementKind::Strategy);
    EXPECT_TRUE(RunCommand(fixture, add_strategy, ctx).success);
    ids.strategy = add_strategy.GeneratedId();

    core::commands::CreateChildElementCommand add_sub_goal(ids.strategy, core::NewElementKind::Goal);
    EXPECT_TRUE(RunCommand(fixture, add_sub_goal, ctx).success);
    ids.sub_goal = add_sub_goal.GeneratedId();
    return ids;
}

} // namespace

// goal -> text -> strategy -> sub-goal -> text on sub-goal. The first goal's
// text must survive every step.
TEST(StrategyLifecycle, FirstGoalTextSurvivesSubgoalTextEdit) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("text_survives");
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTopGoalCommand top_goal;
    ASSERT_TRUE(RunCommand(*fixture, top_goal, ctx).success);
    const std::string first_goal = top_goal.GeneratedId();

    core::commands::UpdateElementTextCommand first_text(
        first_goal, core::ElementTextField::Content, "en", "First goal statement");
    ASSERT_TRUE(RunCommand(*fixture, first_text, ctx).success);
    EXPECT_EQ(ContentOf(fixture->model, first_goal), "First goal statement");

    core::commands::CreateChildElementCommand add_strategy(first_goal, core::NewElementKind::Strategy);
    ASSERT_TRUE(RunCommand(*fixture, add_strategy, ctx).success);
    const std::string strategy = add_strategy.GeneratedId();
    EXPECT_EQ(ContentOf(fixture->model, first_goal), "First goal statement")
        << "first goal text lost when the strategy was added";

    core::commands::CreateChildElementCommand add_sub_goal(strategy, core::NewElementKind::Goal);
    ASSERT_TRUE(RunCommand(*fixture, add_sub_goal, ctx).success);
    const std::string sub_goal = add_sub_goal.GeneratedId();
    EXPECT_EQ(ContentOf(fixture->model, first_goal), "First goal statement")
        << "first goal text lost when the sub-goal was added";

    core::commands::UpdateElementTextCommand sub_text(
        sub_goal, core::ElementTextField::Content, "en", "Second goal statement");
    ASSERT_TRUE(RunCommand(*fixture, sub_text, ctx).success);
    EXPECT_EQ(ContentOf(fixture->model, sub_goal), "Second goal statement");
    EXPECT_EQ(ContentOf(fixture->model, first_goal), "First goal statement")
        << "first goal text lost when the sub-goal's text was set";
}

// Remove a strategy's only sub-goal, then remove the strategy NodeOnly -- the
// one enabled context-menu item for a childless node. Both removals must
// succeed, and nothing (in particular no sourceless inference) may be left
// behind.
TEST(StrategyLifecycle, BareStrategyCanBeRemovedAfterItsSubgoal) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("remove_after_subgoal");
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);
    const GoalStrategySubgoal ids = BuildGoalStrategySubgoal(*fixture, ctx);

    core::commands::RemoveElementCommand remove_sub_goal(ids.sub_goal, core::RemoveMode::NodeOnly);
    const core::commands::CommandResult sub_result = RunCommand(*fixture, remove_sub_goal, ctx);
    EXPECT_TRUE(sub_result.success) << sub_result.error;
    EXPECT_EQ(FindElement(fixture->model, ids.sub_goal), nullptr);

    core::commands::RemoveElementCommand remove_strategy(ids.strategy, core::RemoveMode::NodeOnly);
    const core::commands::CommandResult strategy_result = RunCommand(*fixture, remove_strategy, ctx);
    EXPECT_TRUE(strategy_result.success) << "remove strategy failed: " << strategy_result.error;
    EXPECT_EQ(FindElement(fixture->model, ids.strategy), nullptr) << "strategy still present after removal";
    EXPECT_EQ(CountInferences(fixture->model), 0) << "an inference outlived the strategy it reasoned over";
    EXPECT_NE(FindElement(fixture->model, ids.goal), nullptr) << "the goal must survive its strategy's removal";
}

// The other user-reported recovery path: remove the only sub-goal, then give
// the strategy a NEW sub-goal. The new goal must re-source the strategy's
// inference (or materialize one) so the tree is whole again.
TEST(StrategyLifecycle, NewSubgoalCanBeAddedAfterRemovingTheOnlyOne) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("readd_subgoal");
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);
    const GoalStrategySubgoal ids = BuildGoalStrategySubgoal(*fixture, ctx);

    core::commands::RemoveElementCommand remove_sub_goal(ids.sub_goal, core::RemoveMode::NodeOnly);
    ASSERT_TRUE(RunCommand(*fixture, remove_sub_goal, ctx).success);

    core::commands::CreateChildElementCommand add_replacement(ids.strategy, core::NewElementKind::Goal);
    const core::commands::CommandResult replacement_result = RunCommand(*fixture, add_replacement, ctx);
    ASSERT_TRUE(replacement_result.success) << "adding a new sub-goal failed: " << replacement_result.error;
    const std::string replacement = add_replacement.GeneratedId();
    ASSERT_NE(FindElement(fixture->model, replacement), nullptr);

    // The strategy's single inference must relate {source = the new sub-goal,
    // reasoning = the strategy, target = the goal}.
    bool found_inference = false;
    for (const parser::SacmElement& element : fixture->model.elements) {
        if (element.type != "assertedinference" || element.reasoning_ref != ids.strategy)
            continue;
        found_inference = true;
        EXPECT_EQ(element.source_refs.size(), 1u);
        if (!element.source_refs.empty())
            EXPECT_EQ(element.source_refs.front(), replacement);
        ASSERT_EQ(element.target_refs.size(), 1u);
        EXPECT_EQ(element.target_refs.front(), ids.goal);
    }
    EXPECT_TRUE(found_inference) << "no inference relates the new sub-goal through the strategy";
}

// A strategy that never had a sub-goal (bare: strategyTarget tag only, no
// inference in the library, render-only placeholder in the projection) must
// also be removable NodeOnly.
TEST(StrategyLifecycle, NeverMaterializedStrategyCanBeRemoved) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("remove_bare");
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);

    core::commands::CreateTopGoalCommand top_goal;
    ASSERT_TRUE(RunCommand(*fixture, top_goal, ctx).success);
    const std::string goal = top_goal.GeneratedId();

    core::commands::CreateChildElementCommand add_strategy(goal, core::NewElementKind::Strategy);
    ASSERT_TRUE(RunCommand(*fixture, add_strategy, ctx).success);
    const std::string strategy = add_strategy.GeneratedId();

    core::commands::RemoveElementCommand remove_strategy(strategy, core::RemoveMode::NodeOnly);
    const core::commands::CommandResult result = RunCommand(*fixture, remove_strategy, ctx);
    EXPECT_TRUE(result.success) << "remove bare strategy failed: " << result.error;
    EXPECT_EQ(FindElement(fixture->model, strategy), nullptr) << "strategy still present after removal";
    EXPECT_EQ(CountInferences(fixture->model), 0) << "a placeholder or inference outlived the bare strategy";
}

// The same removal via NodeAndDescendants, the context menu's other item.
TEST(StrategyLifecycle, BareStrategyCanBeRemovedWithDescendantsAfterItsSubgoal) {
    std::unique_ptr<EditFixture> fixture = MakeFixture("remove_desc");
    ASSERT_NE(fixture->document, nullptr);
    core::commands::CommandContext ctx = MakeContext(*fixture);
    const GoalStrategySubgoal ids = BuildGoalStrategySubgoal(*fixture, ctx);

    core::commands::RemoveElementCommand remove_sub_goal(ids.sub_goal, core::RemoveMode::NodeAndDescendants);
    const core::commands::CommandResult sub_result = RunCommand(*fixture, remove_sub_goal, ctx);
    EXPECT_TRUE(sub_result.success) << sub_result.error;

    core::commands::RemoveElementCommand remove_strategy(ids.strategy, core::RemoveMode::NodeAndDescendants);
    const core::commands::CommandResult strategy_result = RunCommand(*fixture, remove_strategy, ctx);
    EXPECT_TRUE(strategy_result.success) << "remove strategy failed: " << strategy_result.error;
    EXPECT_EQ(FindElement(fixture->model, ids.strategy), nullptr) << "strategy still present after removal";
    EXPECT_EQ(CountInferences(fixture->model), 0) << "an inference outlived the strategy it reasoned over";
}
