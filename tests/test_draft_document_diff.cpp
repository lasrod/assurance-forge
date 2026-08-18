#include "core/drafts/draft_document_diff.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

// The draft is a SACM document, and what it changes is a comparison against the
// accepted one (ADR 0016). These pin the comparison itself: everything the
// canvas decorates, the Draft Changes panel lists and the "changes only" view
// narrows to is derived from this result, so a wrong answer here is wrong
// everywhere at once.

namespace {

core::SacmElement Claim(std::string id, std::string content) {
    core::SacmElement element;
    element.id = std::move(id);
    element.type = "claim";
    element.content = std::move(content);
    return element;
}

core::SacmElement Context(std::string id, std::string description) {
    core::SacmElement element;
    element.id = std::move(id);
    element.type = "artifactreference";
    element.description = std::move(description);
    return element;
}

core::AssuranceCase AcceptedCase() {
    core::AssuranceCase model;
    model.id = "case-1";
    model.elements.push_back(Claim("G1", "The blender is acceptably safe."));
    model.elements.push_back(Claim("G2", "Blade hazards are controlled."));
    model.elements.push_back(Context("C1", "Indoor household use."));

    core::SacmElement inference;
    inference.id = "R1";
    inference.type = "assertedinference";
    inference.source_refs.push_back("G2");
    inference.target_refs.push_back("G1");
    model.elements.push_back(inference);
    return model;
}

const core::drafts::DraftDocumentChange* Find(const core::drafts::DraftDocumentDiff& diff, const std::string& id) {
    return diff.Find(id);
}

bool HasField(const core::drafts::DraftDocumentChange& change, const std::string& field) {
    return std::find(change.fields.begin(), change.fields.end(), field) != change.fields.end();
}

} // namespace

TEST(DraftDocumentDiffTest, AFreshDraftChangesNothing) {
    const core::AssuranceCase accepted = AcceptedCase();
    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, accepted);

    EXPECT_FALSE(diff.touches_anything());
    EXPECT_TRUE(diff.changes.empty());
    EXPECT_TRUE(diff.removed.empty());
}

TEST(DraftDocumentDiffTest, RewordingAClaimReportsOnlyThatElementAndThatField) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[0].content = "The blender is acceptably safe for indoor household use.";

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    EXPECT_EQ(diff.modified_count, 1);
    EXPECT_EQ(diff.added_count, 0);
    EXPECT_EQ(diff.removed_count, 0);
    ASSERT_EQ(diff.changes.size(), 1u);

    const core::drafts::DraftDocumentChange* change = Find(diff, "G1");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Modified);
    ASSERT_EQ(change->fields.size(), 1u) << "only the field that moved may be reported";
    EXPECT_TRUE(HasField(*change, "content"));
}

// The defect that motivated ADR 0016 was a text edit landing in the wrong field
// for the element's kind. A context's text is its description, and the
// comparison has to report that as a description change -- if it only ever
// looked at `content`, editing a context would read as "nothing changed".
TEST(DraftDocumentDiffTest, EditingAContextReportsItsDescription) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[2].description = "Indoor household food and drink preparation.";

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    const core::drafts::DraftDocumentChange* change = Find(diff, "C1");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Modified);
    EXPECT_TRUE(HasField(*change, "description"));
    EXPECT_FALSE(HasField(*change, "content"));
}

TEST(DraftDocumentDiffTest, ATranslationIsAChangeEvenWhenThePrimaryTextIsUntouched) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[0].content_langs["ja"] = "ブレンダーは十分に安全である。";

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    const core::drafts::DraftDocumentChange* change = Find(diff, "G1");
    ASSERT_NE(change, nullptr) << "a draft that only adds a translation still changes the argument";
    EXPECT_TRUE(HasField(*change, "content"));
}

TEST(DraftDocumentDiffTest, ANewElementIsAnAddition) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements.push_back(Claim("G3", "Electrical hazards are controlled."));

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    EXPECT_EQ(diff.added_count, 1);
    const core::drafts::DraftDocumentChange* change = Find(diff, "G3");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Added);
    EXPECT_TRUE(change->fields.empty()) << "an addition is whole; naming its fields would imply a comparison";
}

// A removed element is absent from the draft, so anything that wants to show
// the user what is being deleted has to get it from the diff. Carrying the whole
// element is what lets the canvas draw the node it is about to lose.
TEST(DraftDocumentDiffTest, ARemovedElementIsReportedAndCarriedInFull) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    std::erase_if(draft.elements, [](const core::SacmElement& element) { return element.id == "G2"; });

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    EXPECT_EQ(diff.removed_count, 1);
    const core::drafts::DraftDocumentChange* change = Find(diff, "G2");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Removed);

    ASSERT_EQ(diff.removed.size(), 1u);
    EXPECT_EQ(diff.removed.front().id, "G2");
    EXPECT_EQ(diff.removed.front().content, "Blade hazards are controlled.")
        << "the removed element must arrive intact, not as a bare id";
}

// A malformed accepted document must not produce a diff that contradicts
// itself. `removed_count` is derived from the per-id change map, which takes the
// first occurrence of a duplicate id, so the removal list has to follow the same
// rule -- otherwise a panel reports one removal and lists two, and a renderer
// iterating the list draws a tombstone the count says is not there.
TEST(DraftDocumentDiffTest, ADuplicateAcceptedIdKeepsTheRemovalListAndItsCountAgreeing) {
    core::AssuranceCase accepted = AcceptedCase();
    accepted.elements.push_back(Claim("G2", "A second element wearing an id that is already taken."));

    core::AssuranceCase draft = accepted;
    std::erase_if(draft.elements, [](const core::SacmElement& element) { return element.id == "G2"; });

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    EXPECT_EQ(diff.removed_count, 1);
    ASSERT_EQ(diff.removed.size(), 1u)
        << "the removal list and the count it is reported by must describe the same removals";
    EXPECT_EQ(diff.removed.front().content, "Blade hazards are controlled.") << "first wins, as everywhere else";
}

// A relationship is an element like any other here. Under the operation-based
// design a text or endpoint change targeting a relationship id was silently
// dropped by the planner -- accept reported success and the change was absent
// from the saved file. A comparison cannot drop it: the endpoints differ, so the
// difference is reported.
TEST(DraftDocumentDiffTest, ARepointedRelationshipIsReported) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements.push_back(Claim("G3", "Electrical hazards are controlled."));
    for (core::SacmElement& element : draft.elements) {
        if (element.id == "R1")
            element.source_refs = {"G3"};
    }

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    const core::drafts::DraftDocumentChange* change = Find(diff, "R1");
    ASSERT_NE(change, nullptr) << "a relationship change must never be invisible";
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Modified);
    EXPECT_TRUE(HasField(*change, "source_refs"));
}

TEST(DraftDocumentDiffTest, MarkingAClaimUndevelopedIsAChange) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[1].undeveloped = true;

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    const core::drafts::DraftDocumentChange* change = Find(diff, "G2");
    ASSERT_NE(change, nullptr);
    EXPECT_TRUE(HasField(*change, "undeveloped"));
}

TEST(DraftDocumentDiffTest, ChangesAreOrderedByElementIdRegardlessOfDocumentOrder) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[1].content = "Blade contact hazards are controlled.";
    draft.elements[0].content = "The blender is safe enough.";
    // Reversed, so document order and id order disagree.
    std::reverse(draft.elements.begin(), draft.elements.end());

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    ASSERT_EQ(diff.changes.size(), 2u);
    EXPECT_EQ(diff.changes[0].element_id, "G1");
    EXPECT_EQ(diff.changes[1].element_id, "G2");
}

// Several contributors touching one element is the ordinary case in a shared
// draft. The comparison reports the net effect against the accepted argument --
// one modified element -- because that is what a reviewer is being asked to
// accept. Who contributed what is provenance, carried on the element, and is
// deliberately not this function's job.
TEST(DraftDocumentDiffTest, SeveralEditsToOneElementReportAsOneNetChange) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[0].content = "First contributor's wording.";
    draft.elements[0].content = "Second contributor's wording.";
    draft.elements[0].name = "Top goal";

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    EXPECT_EQ(diff.modified_count, 1);
    const core::drafts::DraftDocumentChange* change = Find(diff, "G1");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->fields.size(), 2u);
    EXPECT_TRUE(HasField(*change, "content"));
    EXPECT_TRUE(HasField(*change, "name"));
}

// An element created in the draft and then edited is an addition, not a
// modification: to the accepted argument it is simply new. The operation-based
// index had to reason about contribution order to get this right; a comparison
// gets it for free, and this pins that it does.
TEST(DraftDocumentDiffTest, AnElementAddedThenEditedIsStillAnAddition) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements.push_back(Claim("G3", "First wording."));
    draft.elements.back().content = "Reworded before anyone accepted it.";

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    const core::drafts::DraftDocumentChange* change = Find(diff, "G3");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->change, core::drafts::DraftElementChange::Added);
    EXPECT_EQ(diff.modified_count, 0);
}

// Reject-in-draft is the selective-review gesture ADR 0016 puts in place of
// accepting a subset: the reviewer reverts an element to its accepted value and
// accepts what remains. Reverting must leave no trace in the diff, or the
// reviewer would still be asked to accept something they just removed.
TEST(DraftDocumentDiffTest, RevertingAnElementRemovesItFromTheDiffEntirely) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[0].content = "A wording the reviewer rejects.";
    draft.elements[1].content = "A wording the reviewer keeps.";

    ASSERT_EQ(core::drafts::DiffAcceptedAgainstDraft(accepted, draft).modified_count, 2);

    draft.elements[0] = accepted.elements[0];
    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);

    EXPECT_EQ(diff.modified_count, 1);
    EXPECT_EQ(Find(diff, "G1"), nullptr) << "a reverted element is not a change";
    EXPECT_NE(Find(diff, "G2"), nullptr);
}

// The bridge onto the index the canvas already consumes. Everything that
// decorated a node, ghosted a removal or narrowed the "changes only" view was
// written against `DraftChangeIndex` when a draft was a list of operations, and
// it has to keep working now that the answer comes from a comparison instead.
TEST(DraftDocumentDiffTest, TheComparisonBecomesTheChangeIndexTheCanvasConsumes) {
    const core::AssuranceCase accepted = AcceptedCase();
    core::AssuranceCase draft = accepted;
    draft.elements[0].content = "The blender is acceptably safe for domestic use.";
    draft.elements.push_back(Claim("G9", "Overheating is prevented."));
    draft.elements.erase(draft.elements.begin() + 2); // the context

    const core::drafts::DraftDocumentDiff diff = core::drafts::DiffAcceptedAgainstDraft(accepted, draft);
    const core::drafts::DraftChangeIndex index = core::drafts::ChangeIndexFromDiff(diff);

    EXPECT_EQ(index.added_count, diff.added_count);
    EXPECT_EQ(index.modified_count, diff.modified_count);
    EXPECT_EQ(index.removed_count, diff.removed_count);
    EXPECT_TRUE(index.touches_anything());

    ASSERT_NE(index.Find("G1"), nullptr);
    EXPECT_EQ(index.Find("G1")->change, core::drafts::DraftElementChange::Modified);
    ASSERT_NE(index.Find("G9"), nullptr);
    EXPECT_EQ(index.Find("G9")->change, core::drafts::DraftElementChange::Added);
    ASSERT_NE(index.Find("C1"), nullptr);
    EXPECT_EQ(index.Find("C1")->change, core::drafts::DraftElementChange::Removed);

    // Unchanged elements must not appear, or every check keyed on
    // `ChangedElementIds` would report the whole argument as touched.
    EXPECT_EQ(index.Find("G2"), nullptr);

    // A removed element is absent from the draft by definition, so a renderer
    // that wants to show what is going needs it carried here in full.
    ASSERT_EQ(index.removed.size(), 1u);
    EXPECT_EQ(index.removed.front().id, "C1");
    EXPECT_EQ(index.removed.front().description, "Indoor household use.");

    // Empty, and it has to be: a document records what it holds, not who put it
    // there. A caller that wants attribution reads the element's provenance
    // tags, and must not be quietly handed an empty list as though it were one.
    EXPECT_TRUE(index.ContributingGroupIds("G1").empty());
}
