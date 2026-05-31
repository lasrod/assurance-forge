#include "app/translation_review_sync.h"

#include "core/element_factory.h"
#include "core/problems/problems_manager.h"
#include "core/translation_review_store.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

parser::SacmElement Element(std::string id, std::string type) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    return element;
}

bool HasProblemId(const core::ProblemsManager& manager, const std::string& id) {
    const auto& problems = manager.GetProblems();
    return std::any_of(problems.begin(), problems.end(),
                       [&](const core::ProblemItem& problem) { return problem.id == id; });
}

} // namespace

// ---- store round-trip ------------------------------------------------------

TEST(TranslationReviewStore, RoundTripsElementIds) {
    const std::vector<std::string> ids = {"G1", "S2", "C3"};
    const std::string json = core::translation::SerializeTranslationReview(ids);

    std::vector<std::string> parsed;
    std::string error;
    ASSERT_TRUE(core::translation::ParseTranslationReview(json, parsed, error)) << error;
    EXPECT_EQ(parsed, ids);
}

TEST(TranslationReviewStore, EmptyListRoundTrips) {
    std::vector<std::string> parsed;
    std::string error;
    ASSERT_TRUE(core::translation::ParseTranslationReview(
        core::translation::SerializeTranslationReview({}), parsed, error))
        << error;
    EXPECT_TRUE(parsed.empty());
}

TEST(TranslationReviewStore, MissingArrayYieldsEmptySuccess) {
    std::vector<std::string> parsed;
    std::string error;
    ASSERT_TRUE(core::translation::ParseTranslationReview(
        R"({"format":"assurance-forge-translation-review"})", parsed, error))
        << error;
    EXPECT_TRUE(parsed.empty());
}

TEST(TranslationReviewStore, MalformedJsonFails) {
    std::vector<std::string> parsed;
    std::string error;
    EXPECT_FALSE(core::translation::ParseTranslationReview("{ not json", parsed, error));
    EXPECT_FALSE(error.empty());
}

TEST(TranslationReviewStore, SkipsEmptyAndNonStringEntries) {
    std::vector<std::string> parsed;
    std::string error;
    ASSERT_TRUE(core::translation::ParseTranslationReview(
        R"({"pending":["G1","",42,"G2"]})", parsed, error))
        << error;
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0], "G1");
    EXPECT_EQ(parsed[1], "G2");
}

// ---- ElementHasSecondaryTranslation ----------------------------------------

TEST(ElementHasSecondaryTranslation, FalseForEnglishOnly) {
    parser::SacmElement element = Element("G1", "claim");
    element.name_langs["en"] = "Brakes are safe";
    EXPECT_FALSE(core::ElementHasSecondaryTranslation(element));
}

TEST(ElementHasSecondaryTranslation, TrueWhenSecondaryPresent) {
    parser::SacmElement element = Element("G1", "claim");
    element.name_langs["en"] = "Brakes are safe";
    element.name_langs["ja"] = "ブレーキは安全です";
    EXPECT_TRUE(core::ElementHasSecondaryTranslation(element));
}

TEST(ElementHasSecondaryTranslation, IgnoresEmptySecondaryEntry) {
    parser::SacmElement element = Element("S1", "argumentreasoning");
    element.content_langs["en"] = "Strategy text";
    element.content_langs["ja"] = "";
    EXPECT_FALSE(core::ElementHasSecondaryTranslation(element));
}

// ---- SyncTranslationReviewProblems -----------------------------------------

TEST(TranslationReviewSync, EmitsWarningForPendingTranslatedElement) {
    parser::AssuranceCase model;
    parser::SacmElement element = Element("G1", "claim");
    element.name_langs["en"] = "Brakes are safe";
    element.name_langs["ja"] = "ブレーキは安全です";
    model.elements.push_back(element);

    std::unordered_set<std::string> pending = {"G1"};
    core::ProblemsManager manager;
    bool pending_changed = false;
    app::SyncTranslationReviewProblems(manager, &model, pending, pending_changed);

    EXPECT_FALSE(pending_changed);
    EXPECT_TRUE(HasProblemId(manager, "translation-review:G1"));
    const auto problem = manager.GetProblemById("translation-review:G1");
    ASSERT_TRUE(problem.has_value());
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problem->type, "TranslationReviewNeeded");
    EXPECT_EQ(problem->element_id, "G1");
    EXPECT_EQ(problem->quick_fix_payload, "G1");
}

TEST(TranslationReviewSync, NoProblemWhenNotPending) {
    parser::AssuranceCase model;
    parser::SacmElement element = Element("G1", "claim");
    element.name_langs["en"] = "Brakes are safe";
    element.name_langs["ja"] = "ブレーキは安全です";
    model.elements.push_back(element);

    std::unordered_set<std::string> pending; // empty
    core::ProblemsManager manager;
    bool pending_changed = false;
    app::SyncTranslationReviewProblems(manager, &model, pending, pending_changed);

    EXPECT_FALSE(HasProblemId(manager, "translation-review:G1"));
}

TEST(TranslationReviewSync, PrunesPendingWhenTranslationRemoved) {
    parser::AssuranceCase model;
    parser::SacmElement element = Element("G1", "claim");
    element.name_langs["en"] = "Brakes are safe"; // no secondary translation
    model.elements.push_back(element);

    std::unordered_set<std::string> pending = {"G1"};
    core::ProblemsManager manager;
    bool pending_changed = false;
    app::SyncTranslationReviewProblems(manager, &model, pending, pending_changed);

    EXPECT_TRUE(pending_changed);
    EXPECT_EQ(pending.count("G1"), 0u);
    EXPECT_FALSE(HasProblemId(manager, "translation-review:G1"));
}

TEST(TranslationReviewSync, PrunesPendingWhenElementDeleted) {
    parser::AssuranceCase model; // G1 does not exist

    std::unordered_set<std::string> pending = {"G1"};
    core::ProblemsManager manager;
    bool pending_changed = false;
    app::SyncTranslationReviewProblems(manager, &model, pending, pending_changed);

    EXPECT_TRUE(pending_changed);
    EXPECT_TRUE(pending.empty());
}
