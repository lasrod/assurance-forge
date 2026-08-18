# Promotable by Construction

How a staged change is prevented from becoming one the user can see, cannot
accept, and cannot clear.

- **Status:** Findings, plus one implemented fix (§4). The rest of this page is
  the **evidence base and requirement list** for a redesign that has been agreed
  but not yet written up as an ADR — see [The chosen direction](#the-chosen-direction).
- **Policy it serves:** [ADR 0009](decisions/0009-one-integrated-working-draft-per-argument.md)
  and [ADR 0010](decisions/0010-draft-provenance-persistence-and-human-promotion.md).
  ADR 0010 already states the contract this page is about — *"partial promotion
  is atomic"*, *"a failure before the audit/SACM commit changes nothing"* — and
  the audit below records where the implementation does not yet meet it. The
  redesign will supersede the operation-staging parts of both.

## The defect this exists to make impossible

An MCP client staged one operation: set the text of `C2`, naming the field
`content`. `C2` is a GSN Context — a SACM `ArtifactReference` — and a context
has no `content`; its text is its `description`. The operation was accepted into
the draft, materialized, and drawn on the canvas as a pending change. Only at
**Accept** did the library seam refuse it, with `writing text on C2 failed` and
no reason attached.

The user was then stuck: the change could not be accepted, and the message named
neither the group to reject nor the field to correct.

This was not one wrong branch. It is the visible instance of a structural split.

## The structural cause: two authorities on one question

"Can this change be applied?" is answered twice, by two bodies of code that
never meet until the write.

| | Staging asks | Promotion asks |
|---|---|---|
| What runs | `ReviewProposalPatchService` over the flat POD model | `PlanProposalFromDiff` → `ApplyProposalPlanToLibrary` → the `sacm_adapter` seams |
| What it knows | Every element has `name`, `content` and `description` | An element's fields are decided by its SACM kind |
| Verdict on `content` for a context | Fine | Refused |

`CanStageOperations` rehearses only materialization
(`src/core/drafts/draft_materializer.cpp`). `PlanDraftPromotion`
(`src/core/drafts/draft_promotion_service.h`) — the function the Draft Changes
panel calls to decide whether a row is promotable — takes a
`core::AssuranceCase` and **no library document**, so it cannot know what the
seams will refuse either. The library is consulted for the first time inside the
accept.

The flat POD model is strictly more permissive than the document. Every gap in
this class is that difference, unchecked.

### It is not one gap

An audit of every operation an MCP client can stage against the seam that must
express it found **fourteen** reachable instances, including:

- `UpdateElementText` with `field: "content"` on a Context, Solution or Category
  — the reported defect. `field` is optional and **defaults to `content`**, so an
  agent that omits it hits this too.
- `AddSupportedBy` / `AddInContextOf` naming a Term or Category as an endpoint.
  `list_terms` is an advertised tool, terms appear as ordinary elements, and the
  library requires every endpoint to be an `ArgumentAsset`.
- `SetUndeveloped` on a Solution or Context (not an `Assertion`), or on an
  Assumption or Justification (declaration is `assumed`/`axiomatic`, which
  cannot also be `needsSupport`).
- `RemoveSupportedBy` on a relationship with several sources — a shape the
  application itself builds — because the plan has no retarget verb.
- `RemoveElement` with `node_only` on a node with children, for the same reason.

And one that is worse than a refusal: `UpdateElementText`, `UpdateElementName`
and `SetUndeveloped` targeting a **relationship id** are **silently dropped**.
The planner returns before collecting text writes, the accept reports success,
and the approved change is not in the saved file. The post-promotion semantic
hash check cannot catch it, because the predicted model took the same lossy
path.

### The knowledge is copied, not shared

"Which field holds this element's text" is written out in **eight** places, plus
five more inline restatements elsewhere. The intended single authority already
exists — `core::ClaimLikeCarriesStatementAsDescription` in
`src/core/sacm_model.h` — and its own comment warns that duplicate copies of the
kind list "is how they drift apart." That is exactly what happened: the guard
was written for one direction (`description` on a claim) and never for its
mirror.

## The chosen direction

The obvious repair is to make staging ask the library before accepting an
operation — the rehearsal primitive for it already exists and is proven
(`PreflightProposalAgainstLibrary` serializes the live document, reloads it into
a genuine clone, runs the real apply against the clone, and leaves the
authoritative document byte-identical).

That was rejected in favour of something simpler, because it still *guards* the
gap rather than removing it. The agreed direction removes the second model:

> **The draft is a real SACM document, not a list of operations against a flat
> model. An agent edits that document directly. Accept is an atomic replacement
> of the accepted file by the draft.**

Why this is the better answer to the same problem:

- **The defect class cannot exist.** There is no permissive intermediate model
  for an unrepresentable change to live in. The library accepts or refuses at
  the moment the client asks, so anything a client can do is something the
  application can do — by construction, not by a validator kept in sync.
- **Accept cannot fail.** No plan, no seam sequence, no partial write, no "which
  of five groups broke". §2 and §3 below stop being work to do and become
  situations that cannot arise.
- **It deletes machinery rather than adding it.** The workspace store, the
  materializer, the change-group compiler, `PlanDraftPromotion`, the promotion
  preflight, the `create_ref` indirection and identity pinning, and the use of
  `ReviewProposal` as a promotion vehicle all exist only to carry operations
  across the gap being removed.
- **The MCP surface shrinks with it.** Staged-group vocabulary, patch-operation
  types and `create_ref` placeholders exist because operations are held in
  escrow and cashed later; against a real document, ids are real when an element
  is created and every call gets an immediate, accurate answer.

Two properties of the current code make it viable: human edits are **already**
routed into the draft rather than the accepted model, so the accepted file has
exactly one writer and cannot drift under a draft in normal use; and the
save-then-reload round trip is already relied upon as lossless, with a
byte-identical test behind it.

**Review model.** Accept is all-or-nothing. Selective review is preserved by
inverting the gesture: rather than accepting a subset, the reviewer *rejects
individual changes in the draft* — reverting an element to the accepted value,
a well-defined document operation — and then accepts what remains. Provenance
for "who changed what" belongs in the draft document as vendor `TaggedValue`s
(clause 8.12, the mechanism already used for GSN identifiers and ACPs), so it
travels with the model instead of in a parallel index that can disagree with it.

**Open for the ADR.** The audit and undo semantics need real design rather than
transcription: promotion is currently an audited, replayable, undoable command,
and the audit store's premise is that replaying transactions reproduces the
SACM. Baseline drift also needs a deliberate answer — operations can be replayed
onto a new baseline, whole documents cannot — though with one writer to the
accepted file, "the draft is stale; keep it or discard it" is likely sufficient.
The draft file belongs under `.af/`, not beside the argument, or the project
scanner and version control both pick it up.

## Requirements the redesign must satisfy

The findings below were the case for redesigning. They are recorded here as the
list the new design has to answer, not as work queued against the old one.

### 1. Every refusal reaches the client at the time it asks

An unusable request must never be accepted, rendered as pending, and refused
later. This is the requirement §4 satisfies locally and the redesign satisfies
structurally.

### 2. Promotion must be atomic

`ApplyProposalPlanToLibrary` writes elements, relationships, terminology, text,
flags and deletions into the **live** document in sequence. A refusal on write
*k* leaves writes *1..k-1* applied. The draft-promotion caller compensates with
a serialize-and-restore snapshot, but that rollback is conditional on the audit
bus being open, and its failure is advisory — nothing stops the partial state
reaching disk on the next successful command. The other two callers of the same
command — accepting a review proposal, and accepting an agent change set — have
no preflight, no snapshot and no rollback at all.

A file-replacement accept satisfies this outright. If any part of the sequenced
applier survives the redesign, atomicity belongs in `ApplyProposalCommand`
rather than in one caller.

### 3. No unrecoverable state

Two defects in the current implementation, both found by audit, and both
requirements on whatever replaces it:

- **The `pending_promotion` deadlock.** If a promotion's durable marker survives
  with a hash matching neither the expected result nor the baseline, the
  workspace refuses accept, refuses every edit, *and* refuses discard — while
  the UI's Discard button stays enabled. The only exit is hand-editing
  `.af/drafts/<key>/workspace.json`. It is reachable, and untested. A
  contributing cause is a real inconsistency: the marker records the
  **predicted** model hash, while every consumer compares the **produced** one,
  and the two legitimately differ for a promotion containing terminology.
  Discard must always be available.
- **A failure must name its group.** Mid-plan refusals name a resolved element
  id, which has no visible relationship to a draft group. Accept-all compiles
  every group into one proposal, so "creating XYZ failed" leaves a user with
  five groups and no idea which to reject. Since the compilation already
  namespaces each group's `create_ref`s, the group is known and can be reported.

`rebase` and `export recovery data` are promised by ADR 0010 and do not exist;
with accept and edit both blocked, discard is the only exit from `NeedsRebase`,
and discard destroys the work. Either build them or amend the ADR.

### 4. Remove ambiguity rather than validate it — implemented

The reported defect had a simpler cause underneath: the operation asks the
client *which field* to write, when the element's kind already determines it.
`UpdateElementName` is a separate operation, so for any argument element there
is exactly one text field and only one right answer.

`ElementCarriesContent` (`src/core/sacm_model.h`) now states which kinds have a
`content` at all, beside the sibling rule it completes, and the patch service
uses it:

- An **omitted** field is resolved from the element's kind rather than defaulted
  to `content`. The caller asserted nothing, so there is nothing to contradict.
- A field that **names** `content` on a kind that has none is refused, naming
  `description` as the field to use — the mirror of the guard that already
  refused `description` on a claim.

This is the ninth copy of the rule being deleted, not a tenth being added. Both
directions are now covered by tests; before this change the mirror case had none.

## What is done and what is not

| | State |
|---|---|
| §4 field-kind rule, both directions, with tests | **Implemented** |
| §1 every refusal reaches the client when it asks | Redesign |
| §2 atomic accept | Redesign — an atomic file replacement satisfies it |
| §3 discard always available; failures name their group | Redesign |
| Silent drop of relationship-targeted text/name/flag operations | **Open defect.** A correctness bug, not only a usability one: accept reports success and the change is absent from the saved file. Independent of the redesign, and it should be confirmed fixed by it rather than assumed. |
| `rebase` / `export recovery data` from ADR 0010 | Not built; the ADR overstates. Either build them or amend it. |

The draft-document redesign is expected to retire §1–§3 by construction. The
silent-drop defect and the ADR 0010 overstatement are separate and survive it.
