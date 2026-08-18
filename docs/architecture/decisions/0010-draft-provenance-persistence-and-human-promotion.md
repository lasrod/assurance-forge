# 0010. Draft provenance, persistence, and human-controlled promotion

- Status: Accepted
- Date: 2026-08-02
- Deciders: Assurance Forge maintainers
- **Superseded in part by [ADR 0016](0016-the-draft-is-a-sacm-document.md):** the
  operation-log storage, identity pinning, positional preconditions, dependency
  closure and compiled-proposal promotion are replaced by a draft that is itself
  a SACM document, accepted as one atomic write. The provenance requirement, the
  `.af/.gitignore` decision, the non-export and non-hashed properties, and
  human-controlled promotion stand.
- Supersedes in part: ADR 0008's "Change sets are held in memory" consequence
- Supersedes in part: the persisted one-`ReviewProposal`-per-review-item workflow

## Context

ADR 0009 introduces one integrated working draft per argument file. That draft
has to be unmistakably unaccepted, survive a restart, remember where every
contribution came from, support accepting some of itself without the rest, and
never be readable as accepted argument by anything — including another tool.

**SACM cannot carry the state.** It has general extension mechanisms, but no
assertion declaration meaning "proposed by an AI and not accepted by the
assurance-case author". The states it does have mean other things:
`needsSupport`, `defeated`, `isAbstract` and citation state are all claims *about
the argument*, made by its author. Writing proposal status as any of them would
misrepresent the argument to every reader, including a future reader of our own
files. Writing an Assurance Forge proposal tag into the accepted `.sacm` instead
is worse in a different way: to a tool that ignores unknown tags — which is every
conforming tool — an unaccepted claim becomes ordinary argument content.

**The two existing stores each fail a different half of the requirement.** MCP
change sets live in `ChangeSetStore`'s vectors and are gone on restart. Persisted
`ReviewProposal` files survive, but they are independent patches against a common
baseline, not one recoverable working graph; nothing records that one depends on
another, and nothing can reconstruct the combination.

**Selective acceptance needs dependency data that nothing currently records.** A
new claim may need a strategy and two relationships to be meaningful. An SCCG
wording correction may apply to an element an MCP group created, which does not
exist in the accepted baseline at all. Accepting the second without the first is
not a smaller change; it is an impossible one.

**ADR 0008 already refused to put transient AI state in the project directory.**
It kept the endpoint record in the user's runtime directory because "in the
project it would be hash-tracked by `af.proj` and would reach a colleague through
version control", and it kept change sets in memory "for the same reason". Any
decision to persist drafts under the project owes that argument a direct answer.

ADR 0003 makes SACM XML the canonical assurance-case representation and forbids
the tool from silently modifying or reinterpreting assurance data. Whatever we
persist must leave that true of the accepted baseline.

## Decision

Draft state and proposal metadata will live **outside SACM semantics**, and the
draft will be persisted as recovery state under the project's internal directory.

### Storage

The accepted `.sacm` file remains the canonical accepted safety case. Draft work
is stored as ordered change groups of patch operations plus provenance and
dependency metadata, and the working model is reconstructed from the accepted
baseline plus those groups. There is no second authoritative `.sacm`.

```text
.af/
  drafts/
    <argument-stable-key>/
      workspace.json
      events.jsonl
```

The key is the project-relative path of the argument file. A rename orphans its
workspace; the orphan is surfaced to the user rather than silently discarded.

Writes are atomic — temporary file plus rename, through the existing safe-write
utilities — after every successful workspace mutation.

Draft state is **not** tracked as an assurance-case file role in `af.proj`, is
**not** included in normal SACM export, and is **not** part of the canonical
accepted-model hash.

### Answering ADR 0008 on version control

`.af/` is created by the project scaffold today with `cache`, `backups`,
`snapshots` and `history` subdirectories, and **nothing generates an ignore file
for it**. Under version control those already reach a colleague. Adding drafts to
that directory would put unaccepted, AI-authored safety-argument text on the same
path.

The project scaffold will therefore generate `.af/.gitignore` covering the whole
internal directory. This closes an existing hole as much as it opens the door for
drafts.

With that in place ADR 0008's objection is answered rather than overridden.
Drafts differ from the endpoint record in the two ways that mattered: they are
per-project rather than per-machine, so the project directory is their natural
home, and they are not manifest-tracked, so they cannot rot `af.proj` hashes.
What remains true is that only the running application ever writes them, so the
single-owner rule is untouched.

### What a change group records

- Stable group identity, and workspace sequence position.
- Source (`Human`, `Mcp`, `SccgAiReview`, `ImportedLegacyProposal`) and source
  label.
- MCP connection/session or AI review run identity, where applicable.
- Title, summary and rationale.
- Creation and update timestamps.
- Patch operations, and the generated identities they produced.
- SCCG guideline IDs and linked review-item IDs.
- Explicit and inferred dependencies on other groups.
- Group state and workspace revision.

### Generated identities are allocated once

Materialization runs whenever the workspace or the accepted model changes.
`ReviewProposalPatchService::ApplyProposal` generates element identities as it
goes, so materializing twice would produce two different identities for the same
proposed element — and a canvas selection, an inspector, or an agent's reference
to "the goal you just created" would break between frames.

The workspace therefore records the `create_ref` → element-id map on the first
successful materialization and every later materialization uses
`ApplyProposalWithIds`, the replay variant that already exists for the audit log.
Identities are drawn from one allocator over the complete authoritative SACM
document plus every identity reserved by an active draft group. UI projections
are not an ID authority because they intentionally omit some document elements.

Promotion re-checks identity ownership but never silently chooses a replacement
for an identity already shown to a user or client. A collision means the draft
baseline or reservation set changed incorrectly; promotion is blocked and an
explicit rebase/remap operation must update the identity and every reference to
it atomically.

Patch-local `create_ref` values are unique within a group. Interactive editing
must allocate a new reference for every creation rather than reusing a constant
such as `$new`.

### Positional preconditions and conflict detection

Workspace revision prevents a source from staging against a working graph it
has not read, but it is not sufficient for later rejection, replacement or
partial promotion. Each operation therefore records the semantic state it was
authored against: expected field value or element hash for updates/removals, and
expected existence/non-existence for relationships and creations.

Materialization of the original ordered stack verifies those preconditions.
Rebase verifies them again against the prospective prefix. An operation that can
still execute by ID but no longer has the meaning it was authored against is a
conflict, not a successful rebase. Conflicts name the group, operation and
expected/actual state and leave both the accepted baseline and workspace
unchanged.

Groups remain the atomic review unit. The UI must disclose every element and
relationship a selected group changes; an element-level action cannot imply it
accepts only that element when the group also changes others.

### Dependencies

Dependencies are inferred and stored, so the UI can explain them rather than
assert them:

- An operation referencing an element another group created.
- An update or removal of an element another group created.
- A relationship whose endpoint another group created.
- Dependencies the source declared explicitly.

### Promotion

The user promotes all active groups or a dependency-closed selection. Before
anything is mutated, Assurance Forge computes the dependency closure, shows it,
materializes the selected result against the accepted baseline, materializes the
remaining groups against the *prospective* new baseline, and validates both.
**If either fails, promotion is refused and the accepted SACM is not touched.**

Promotion preflight uses the exact authoritative document projection, operation
interpreter and predetermined identities used by commit. Testing a UI-filtered
parser copy and committing through a different library bridge is not a valid
preflight.

Promotion then compiles the selected groups into one `ReviewProposal` and
dispatches a single audited `ApplyProposalCommand` — the same path an interactive
edit takes, which is what keeps promotion replayable, undoable, and attributed.
The audit record carries the human approver, the contributing source labels, the
group IDs, the rationale, and the guideline and review-item references.

Partial promotion is atomic. After it succeeds, promoted groups leave the active
workspace for history and the remaining groups stay visible, rebased.

The accepted SACM write, audit transaction and workspace transition form one
recoverable transaction. A failure before the audit/SACM commit changes nothing.
A failure after the audit commit is recovery state, not an ordinary refusal: the
workspace snapshot remains available and reopening deterministically completes
or rolls back the transition. A post-commit semantic comparison is a diagnostic
assertion, never the first point at which a mismatched promotion is discovered.

Only `Ready` groups are promotable. A `Building` group may be materialized for
author feedback but Accept All cannot commit work its author has not submitted.

A user action directed at one element or relationship resolves to the smallest
dependency-safe set of groups that produces the visible state, and the actual
closure is shown before promotion. Accepting a created claim silently without the
strategy that gives it meaning is the failure this exists to prevent.

### Undo has two stacks

Draft mutation is deliberately not a command: it records no audit transaction and
triggers no `.sacm` autosave. That is what keeps the accepted file byte-stable
while a draft is being built, and it means the accepted model's undo stack must
not be used for draft edits.

- While a draft is active, undo operates on the **workspace**, and the accepted
  model's undo stack is untouched.
- Promotion is one boundary on the accepted stack.
- **Undoing a promotion restores the accepted baseline and the pre-promotion
  workspace together, atomically.** Promotion snapshots the workspace for exactly
  this. Restoring only the model would leave the remaining groups rebased onto a
  baseline that no longer exists.

### Reopen

If the recorded accepted base hash matches the file on disk, the draft is
restored and the banner shown. If it differs, the workspace enters `NeedsRebase`
and **operations are not replayed silently**. The user may inspect, attempt a
rebase, export the recovery data, or discard.

The same base-hash rule is enforced while the application is running. An
accepted revision change cannot continue materializing against an obsolete base
hash merely because the workspace has not been closed and reopened.

Because drafts are autosaved recovery state, closing the application never forces
a decision. Work resumes later.

### Failure-atomic workspace mutation

Every workspace mutation is prepared on a copy, persisted atomically, and only
then published in memory. If persistence fails, the caller receives an error and
the previously published workspace and working snapshot remain intact. This
applies equally to group creation, staging/replacement, ready/reject transitions,
identity pinning, promotion cleanup and discard.

`workspace.json` is the recovery authority. The event log may be reconstructed
from it or lag with an explicit diagnostic, but failure to update the event log
must not produce an in-memory state that differs silently from the recoverable
workspace.

### Legacy migration

Existing `reviews/proposals/*.json` remain readable. On project open, proposals
referenced by review items are loaded, validated, and converted to
`ImportedLegacyProposal` groups preserving author, timestamps, review-item link,
rationale, operations and base hashes. They are materialized together and
conflicts reported. **Nothing is applied.** Old files are archived only after the
migration has been saved successfully.

`ReviewItem` gains a `draft_group_ids` field and keeps reading `proposal_id`
until the migration window closes. New SCCG suggestions stop creating proposal
files once the new workflow is enabled.

In-memory MCP change sets have no migration path and need none: they are already
lost on restart today.

## Consequences

- No other SACM tool has to understand an Assurance Forge proposal tag to tell
  accepted argument from unaccepted proposal, because unaccepted content is never
  in the file.
- The accepted `.sacm` and its audit hash stay stable until an explicit
  promotion, and a discarded draft leaves the file byte-identical.
- Draft work survives restart without becoming accepted project content, which is
  new — an MCP conversation is no longer bounded by the application's lifetime.
- Provenance can show that one visible element was created by an MCP client,
  reworded by SCCG review, and edited by the user, in that order.
- `.af/drafts` becomes an internal format with real compatibility obligations:
  versioning, forward-compatible reads, and migration tests. It is the first
  Assurance Forge format that holds argument content and is not SACM.
- Unaccepted AI-authored argument text now sits in the project directory. The
  generated `.af/.gitignore` keeps it out of version control, but a user who
  copies or archives a project directory wholesale takes the draft with them.
  That is a change in exposure and should be stated in the storage documentation
  rather than left to be discovered.
- Selective acceptance is safe but not cheap: every promotion runs two
  materializations and two validations before mutating anything.
- Two undo stacks is more machinery and more to explain than one, and the
  promotion snapshot is the part most likely to be got wrong. It is also the only
  arrangement in which undoing a promotion leaves a coherent draft.
- Sharing a project through tracked files does not share an unaccepted draft. A
  portable draft export is possible later as its own decision.
- `ReviewProposal`, `ReviewProposalPatchService` and `ApplyProposalCommand` are
  retained and reused — as the operation vocabulary, the materializer, and the
  promotion mechanism respectively. `ReviewProposalManager` stops being the
  primary user-facing proposal store.
- ADR 0008's single-owner rule is unchanged: only the running application writes
  draft recovery state. Only its in-memory-only consequence is replaced.
