#include "app/terminology_problem_sync.h"

#include "core/problems/problems_manager.h"
#include "sacm/sacm_model.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// These tests assert the INTENDED contract of SyncTerminologyProblems, derived
// from its name and the terminology-validation domain rules — not the current
// implementation's incidental output. A failing assertion here should be read
// as a finding (expected vs. observed), not something to tune away.
//
// Intended contract:
//  1. Each call is self-cleaning + idempotent: it removes all previously
//     recorded terminology problems and rebuilds them from the current model.
//  2. With a null model or null package it records no terminology problems.
//  3. Term-definition quality issues are surfaced for every terminology
//     package, including those nested inside argument packages.
//  4. Ambiguous term usages and important undefined acronyms in element text
//     are surfaced as warnings, unless the suggestion is ignored.
//  5. Relationship elements are not scanned for terminology usage.

namespace {

parser::SacmElement Element(std::string id, std::string type, std::string text) {
    parser::SacmElement element;
    element.id = std::move(id);
    element.type = std::move(type);
    // Claims/strategies carry visible text in `content`; everything else in
    // `description` (see parser::ElementTerminologyText).
    if (element.type == "claim" || element.type == "argumentreasoning")
        element.content = std::move(text);
    else
        element.description = std::move(text);
    return element;
}

sacm::Term Term(std::string id, std::string value, std::string description = {}) {
    sacm::Term term;
    term.id = std::move(id);
    term.gid = "gid-" + term.id;
    term.value = std::move(value);
    term.description = std::move(description);
    return term;
}

sacm::TerminologyPackage TerminologyPackage(std::string id, std::vector<sacm::Term> terms) {
    sacm::TerminologyPackage package;
    package.id = std::move(id);
    package.gid = "gid-" + package.id;
    package.name = package.id;
    package.terms = std::move(terms);
    return package;
}

bool HasProblemType(const core::ProblemsManager& manager, const std::string& type) {
    const auto& problems = manager.GetProblems();
    return std::any_of(
        problems.begin(), problems.end(), [&](const core::ProblemItem& problem) { return problem.type == type; });
}

const core::ProblemItem* FindProblemByType(const core::ProblemsManager& manager, const std::string& type) {
    const auto& problems = manager.GetProblems();
    auto found =
        std::find_if(problems.begin(), problems.end(), [&](const core::ProblemItem& p) { return p.type == type; });
    return found == problems.end() ? nullptr : &*found;
}

// Suggestion-ignored callback that never ignores anything.
bool NeverIgnored(const std::string&, const std::string&) {
    return false;
}

} // namespace

TEST(TerminologyProblemSyncTest, ReportsTermDefinitionIssuesAsModelValidationWarnings) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    // A term with a value but no description should raise a "missing
    // description" definition-quality issue.
    package.terminologyPackages.push_back(TerminologyPackage("TP1", {Term("T1", "hazard")}));

    core::ProblemsManager manager;
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);

    const core::ProblemItem* problem = FindProblemByType(manager, "TerminologyTermMissingDescription");
    ASSERT_NE(problem, nullptr);
    EXPECT_EQ(problem->source, core::ProblemSource::ModelValidation);
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
}

TEST(TerminologyProblemSyncTest, ValidatesTerminologyNestedInArgumentPackages) {
    parser::AssuranceCase model;
    sacm::AssuranceCasePackage package;
    sacm::ArgumentPackage argument_package;
    argument_package.id = "AP1";
    argument_package.terminologyPackages.push_back(TerminologyPackage("TP_ARG", {Term("T1", "hazard")}));
    package.argumentPackages.push_back(argument_package);

    core::ProblemsManager manager;
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);

    // Terminology packages nested inside argument packages must be validated
    // just like case-level ones.
    EXPECT_TRUE(HasProblemType(manager, "TerminologyTermMissingDescription"));
}

TEST(TerminologyProblemSyncTest, ReportsAmbiguousTermUsageInElementText) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "The ODD is well defined."));

    sacm::AssuranceCasePackage package;
    // Two case-level terms sharing the value "ODD" make the occurrence ambiguous.
    package.terminologyPackages.push_back(TerminologyPackage(
        "TP1", {Term("T1", "ODD", "Operational Design Domain"), Term("T2", "ODD", "Object Detection Dataset")}));

    core::ProblemsManager manager;
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);

    const core::ProblemItem* problem = FindProblemByType(manager, "TerminologyAmbiguity");
    ASSERT_NE(problem, nullptr);
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problem->element_id, "G1");
}

TEST(TerminologyProblemSyncTest, IgnoredSuggestionsAreSuppressed) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "The ODD is well defined."));

    sacm::AssuranceCasePackage package;
    package.terminologyPackages.push_back(TerminologyPackage(
        "TP1", {Term("T1", "ODD", "Operational Design Domain"), Term("T2", "ODD", "Object Detection Dataset")}));

    core::ProblemsManager manager;
    // Ignoring the suggestion for this element/text must suppress the ambiguity warning.
    app::SyncTerminologyProblems(manager, &model, &package, [](const std::string& element_id, const std::string& text) {
        return element_id == "G1" && text == "ODD";
    });

    EXPECT_FALSE(HasProblemType(manager, "TerminologyAmbiguity"));
}

TEST(TerminologyProblemSyncTest, ReportsImportantUndefinedAcronyms) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "The HARA was completed."));

    sacm::AssuranceCasePackage package; // no terminology defined

    core::ProblemsManager manager;
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);

    const core::ProblemItem* problem = FindProblemByType(manager, "TerminologyUndefinedAcronym");
    ASSERT_NE(problem, nullptr);
    EXPECT_EQ(problem->severity, core::ProblemSeverity::Warning);
    EXPECT_EQ(problem->element_id, "G1");
}

TEST(TerminologyProblemSyncTest, DoesNotScanRelationshipElements) {
    parser::AssuranceCase model;
    // A relationship element whose text contains an undefined acronym must be skipped.
    model.elements.push_back(Element("R1", "assertedinference", "The HARA is referenced."));

    sacm::AssuranceCasePackage package;

    core::ProblemsManager manager;
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);

    EXPECT_FALSE(HasProblemType(manager, "TerminologyUndefinedAcronym"));
}

TEST(TerminologyProblemSyncTest, ReSyncClearsResolvedProblems) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "The HARA was completed."));
    sacm::AssuranceCasePackage package;

    core::ProblemsManager manager;
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);
    ASSERT_TRUE(HasProblemType(manager, "TerminologyUndefinedAcronym"));

    // Define the acronym; a re-sync must clear the now-resolved problem.
    package.terminologyPackages.push_back(TerminologyPackage("TP1", {Term("T1", "HARA", "Hazard Analysis")}));
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);

    EXPECT_FALSE(HasProblemType(manager, "TerminologyUndefinedAcronym"));
}

TEST(TerminologyProblemSyncTest, NullModelOrPackageRecordsNoTerminologyProblems) {
    parser::AssuranceCase model;
    model.elements.push_back(Element("G1", "claim", "The HARA was completed."));
    sacm::AssuranceCasePackage package;

    core::ProblemsManager manager;
    // First produce a terminology problem so we can confirm it is cleared.
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);
    ASSERT_TRUE(HasProblemType(manager, "TerminologyUndefinedAcronym"));

    // A null model must clear terminology problems and add none.
    app::SyncTerminologyProblems(manager, nullptr, &package, NeverIgnored);
    EXPECT_FALSE(HasProblemType(manager, "TerminologyUndefinedAcronym"));

    // Re-create, then a null package must likewise clear and add none.
    app::SyncTerminologyProblems(manager, &model, &package, NeverIgnored);
    ASSERT_TRUE(HasProblemType(manager, "TerminologyUndefinedAcronym"));
    app::SyncTerminologyProblems(manager, &model, nullptr, NeverIgnored);
    EXPECT_FALSE(HasProblemType(manager, "TerminologyUndefinedAcronym"));
}
