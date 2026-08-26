# 0016. The working draft is a SACM document, not a list of operations

- Status: Accepted
- Date: 2026-08-18
- Deciders: Assurance Forge maintainers
- Supersedes in part: ADR 0009's change vocabulary, materialization and
  working/presentation snapshot mechanism
- Supersedes in part: ADR 0010's operation-log storage, identity pinning,
  positional preconditions, dependency closure and compiled-proposal promotion

## Context

ADR 0009 and ADR 0010 represent a draft as **ordered patch operations against a
flat model**, materialized on demand into a working assurance case and compiled
at promotion into one `ReviewProposal` that an `ApplyProposalCommand` writes
through the library seams.

That representation has a structural consequence neither ADR anticipated: it
introduces a second model, and the two models do not agree about what is
possible.

Staging rehearses `ReviewProposalPatchService` against the flat
`core::AssuranceCase`, in which every element carries `name`, `content` and
`description`. Promotion runs the `sacm_adapter` seams, in which an element's
fields are decided by its SACM kind. `PlanDraftPromotion` — the function that
decides whether the Draft Changes panel shows a row as promotable — takes a
`core::AssuranceCase` and no library document, so it cannot know what the seams
will refuse either. **The library is consulted for the first time inside the
accept.**

The flat model is strictly more permissive than the document, and that
difference is unchecked. The user-visible failure: an MCP client set the text of
a GSN Context by naming the field `content`. A context is an `ArtifactReference`
and has no `content`. The operation was accepted into the draft, materialized,
and drawn on the canvas as a pending change; only Accept refused it, naming
neither the field to correct nor the group to reject. The draft could not be
accepted and could not be repaired.

An audit of the whole staged-operation surface found this is not one branch:

- **Fourteen reachable instances of the same shape**, including a term or
  category named as a relationship endpoint, `SetUndeveloped` on a Solution or
  on an Assumption, and `RemoveSupportedBy` on a multi-source relationship — a
  shape the application itself builds.
- **A silent drop.** Text, name and undeveloped operations targeting a
  relationship id are skipped by the planner: Accept reports success and the
  approved change is absent from the saved file. The post-promotion semantic
  hash cannot catch it, because the predicted model took the same lossy path.
- **An unrecoverable state.** A promotion marker surviving with a hash matching
  neither the expected result nor the baseline leaves a workspace that refuses
  accept, refuses every edit **and refuses discard**, while the Discard button
  stays enabled. The only exit is hand-editing `workspace.json`.
- **Partial writes.** The plan applier writes into the live document in
  sequence; a refusal on write *k* leaves writes *1..k-1* applied. Two of the
  three callers have no preflight, no snapshot and no rollback.
- **Eight copies** of "which field holds this element's text", plus five inline
  restatements. The intended single authority exists and its own comment warns
  that duplicate copies are how they drift apart.

These are not independent defects. They are the cost of maintaining a second,
more permissive model and translating back to the real one at the last moment.

ADR 0009 already considered the alternative and set it aside:

> **Represent drafts as branches of the accepted case, git-style.** … Rejected
> as out of scope for this decision: it needs merge conflict resolution over
> SACM structure … Recorded as a non-goal rather than a rejection on principle.

That objection has since been answered by the architecture rather than argued
away. Interactive edits are **already** routed into the draft when one exists,
so the accepted file has exactly one writer — the accept — and two contributors
never produce two divergent accepted baselines to reconcile. There is no merge
because there is no fork. The remaining round-trip requirement, that
serialize-then-reload be lossless, is already relied upon by the existing
promotion preflight and has a byte-identical test behind it.

## Decision

We will represent the working draft as **a real SACM document**.

### Storage

```text
.af/
  drafts/
    <argument-stable-key>/
      draft.sacm
```

The key remains the project-relative path of the argument file, as in ADR 0010.
The draft is created by copying the accepted document and is written atomically
(temporary file plus rename) through the existing safe-write utilities. It
remains recovery state: not a tracked file role in `af.proj`, not part of the
canonical accepted-model hash, not included in normal SACM export, and covered
by the generated `.af/.gitignore`.

There is now a second `.sacm` on disk. It is not authoritative and never
replaces the accepted file except by an explicit human accept.

### Contributors edit the document directly

MCP clients, SCCG AI review and interactive draft editing apply their changes to
the draft document through the **same library operations** the application uses
on the accepted document. There is no staging model, no materialization step and
no separate operation vocabulary.

This is the whole point of the decision: a contributor's change is accepted or
refused by the model that will hold it, at the moment it is made. A change that
cannot be expressed cannot be staged, cannot be rendered as pending, and cannot
reach a human as something to accept — **by construction, not by a validator
kept in step with the seams.**

Identities are allocated by the document when an element is created, so they are
real immediately. ADR 0010's `create_ref` indirection, its one-time identity
pinning and its allocator over "document plus reserved draft identities" are all
retired: there is nothing to reserve.

### What is shown is a diff

The working view *is* the draft document. What changed is a structural
comparison of the draft against the accepted document, computed for display:
element added, removed, or modified, and which fields. Canvas decorations, the
Draft Changes panel and the "changes only" view all read that one comparison.

ADR 0009's working snapshot, presentation snapshot and tombstone reinsertion are
retired. A removed element is present in the accepted document and absent from
the draft, which is what the comparison reports; nothing has to be re-inserted
into a model to be shown.

### Accept is one atomic write, all or nothing

Accept writes the draft document to the accepted path, atomically, and deletes
the draft. There is no plan, no operation sequence and no partial application,
so promotion cannot fail part-way and cannot leave the accepted document in a
state nobody chose.

Accept is **all or nothing**. ADR 0010's dependency inference, dependency
closure and partial promotion are retired along with the machinery that needed
them — a selection of operations no longer exists to be closed over.

### Selective review is reject-in-draft

The reviewer's per-change judgement is preserved by inverting the gesture.
Rather than accepting a subset, the reviewer **rejects individual changes in the
draft** — reverting an element to its accepted value, or removing one the draft
created — and then accepts what remains.

A reject is an ordinary edit of the draft document, so it is subject to the same
model that accepts everything else, and it can never produce a draft that cannot
be accepted. ADR 0010's requirement that a user action on one element resolve to
the smallest dependency-safe set of groups is met differently: the user is
acting on the working argument itself, and the argument is whatever they leave
behind.

### Provenance travels with the element

Provenance is recorded as vendor `TaggedValue`s on the elements a contributor
touches (clause 8.12, the mechanism already used for GSN identifiers and ACPs),
under an `assuranceForge.draft.` prefix: the contributing source, its label, and
the session or review-run identity.

Tags live in the model rather than in a parallel index, so they cannot desync
from the argument they describe, and they survive arbitrary later edits to the
same element. Accept **strips every `assuranceForge.draft.` tag** as it produces
the accepted document. That transformation is a pure function over the document
and is the one deviation from a byte-for-byte copy; it is deterministic,
testable, and cannot partially apply because the result is written once.

### Discard is always available

Discarding deletes the draft file. It is available in every state, without
exception. No condition may leave a user unable to accept, unable to edit and
unable to discard.

### Baseline drift

If the accepted document changes underneath a draft — a checkout, a second
instance — the draft is reported stale and the user chooses to keep working on
it or discard it. **We will not build a three-way merge.** Operations could be
replayed onto a new baseline and a whole document cannot, and this is the one
property the previous representation had that this one does not. It is accepted
because the accepted file has a single writer, which makes the situation rare
and external rather than ordinary.

### Audit and undo

Accept remains one audited transaction, carrying the human approver, the
contributing source labels drawn from the provenance tags, and the resulting
document. Undo of an accept restores the previous accepted document from the
audit store's snapshot.

Implemented (#409) as an `AcceptWorkingDraft` transaction whose single
`WorkingDraftAccepted` event carries the accepted document in full, so the
replayer reproduces the accept without the draft it consumed and history
reconstruction works across it. The accept also takes a snapshot at its own
sequence and names it the manifest's trusted replay root, so verification of
later work starts from the bytes a human approved. That snapshot is the undo
boundary: Ctrl+Z stops at the accept, and the previous accepted document is
reached through restore-from-history rather than through the command stack.

Draft editing records no audit transaction and triggers no accepted-file write,
exactly as in ADR 0010, so the accepted file stays byte-stable while a draft is
being built and a discarded draft leaves it byte-identical.

ADR 0010's two undo stacks survive in purpose but change in mechanism: while a
draft is active, undo operates on the draft document; accept is one boundary on
the accepted stack. Undoing an accept restores the accepted document and the
draft together.

### What is unchanged

- **No AI promotion.** No MCP tool and no AI response accepts a draft. Asserted
  over the whole tool registry (ADR 0009, ADR 0007).
- **One draft per argument file**, following the open argument (ADR 0009).
- **Consent gates** (ADR 0007), and the offline adapter reads accepted SACM only
  and never `.af/drafts`.
- **Single ownership**: only the running application writes draft state (ADR
  0008).
- **The accepted `.sacm` is the canonical safety case** and is never silently
  modified (ADR 0003).

## Consequences

- The defect class this ADR exists for cannot occur. There is no permissive
  intermediate model for an unrepresentable change to inhabit, so a refusal
  reaches the client in the call that caused it.
- Accept cannot fail part-way. The unrecoverable promotion state, the partial
  write and the "which of five groups broke" problem are removed rather than
  repaired.
- A large amount of machinery is deleted: the workspace store, the materializer,
  the change-group compiler, `PlanDraftPromotion`, the promotion preflight,
  identity pinning, positional preconditions, dependency inference and closure,
  and the use of `ReviewProposal` as a promotion vehicle.
- The MCP surface shrinks with it. Staged-group vocabulary, patch-operation
  types and `create_ref` placeholders exist because operations are held in
  escrow and cashed later; against a real document they have no purpose. This is
  a breaking change to the MCP interface, taken deliberately.
- **A readable, unaccepted SACM document now sits in the project directory.**
  ADR 0010 could claim unaccepted content was never in a `.sacm` file; that is no
  longer true. `.af/.gitignore` and the internal directory remain the controls,
  and the draft carries provenance tags — but a conforming tool ignores unknown
  tags, so a user who copies a project directory wholesale carries a file another
  tool would read as ordinary argument. This is a real reduction in safety
  compared with ADR 0010 and is accepted in exchange for removing the defect
  class. It must be stated in the storage documentation rather than discovered.
- Selective acceptance changes shape. A reviewer who wants only some of a
  draft now removes what they do not want instead of selecting what they do.
  This is a genuine change in the review gesture and must be presented as such
  in the UI, not left to be inferred.
- Losing the ability to replay onto a moved baseline is a real regression in the
  rare drift case, mitigated only by that case being rare.
- Memory cost roughly doubles for an open argument: two documents rather than
  one. For documents in the size range this repository handles, this is not
  material.
- The lossless round trip stops being an implementation detail and becomes a
  load-bearing invariant: the draft is written and re-read repeatedly over its
  life, so any loss compounds. It needs an explicit test at the draft boundary,
  not only the existing one behind the promotion preflight.
- ADR 0009's and ADR 0010's consent, ownership, no-AI-promotion and
  non-export properties are carried forward unchanged; only their representation
  decisions are replaced. `docs/features/mcp-server.md` and the capability rows
  describing change groups, staged operations and partial promotion describe a
  mechanism that will no longer exist and must be rewritten rather than amended.

## Alternatives considered

**Validate staged operations against the library before accepting them.** Run
the existing promotion rehearsal at submit time, so nothing unpromotable can be
staged. Rejected: it guards the gap rather than removing it, keeps both models
and all the machinery, and leaves a rulebook that must be kept in step with the
seams forever. It also cannot express the gaps that are not element-kind rules —
a missing plan verb, a sourceless inference — which is most of them.

**A hand-written predicate describing what the seams accept.** Rejected as the
ninth copy of knowledge that has already drifted eight times.

**Provenance in a sidecar file keyed by element id, so accept is a pure file
copy.** Attractive because it keeps accept a rename. Rejected because an index
beside the model can disagree with it, and because tags survive later edits to
the same element without any reconciliation. The strip-on-accept step is a pure
function and a cheap price for provenance that cannot desync.

**Per-contributor draft documents.** Preserves isolation between clients.
Rejected for the reason ADR 0009 rejected per-client working drafts: it hands
the user several candidate arguments to reconcile by hand and reintroduces the
merge problem this decision avoids.
