# Integrated draft workspace — implementation plan

- **Status:** Planned. Decisions recorded in
  [ADR 0009](decisions/0009-one-integrated-working-draft-per-argument.md) and
  [ADR 0010](decisions/0010-draft-provenance-persistence-and-human-promotion.md).
- **Date:** 2026-08-02

This page is the delivery plan for the decisions those two ADRs record. The ADRs
say *what* and *why*; this says *in what order*, *against which files*, and *what
has to be true before a phase is finished*. Where the two disagree, the ADRs win.

## 1. Shape of the change

```text
Accepted SACM baseline
        +
Ordered draft change groups (MCP · SCCG AI · human · imported legacy)
        ↓
One materialized working assurance case
        ↓
Canvas · navigator · inspectors · search · validation · MCP reads · SCCG review
        ↓
Promote a dependency-closed selection, or all
        ↓
New accepted SACM baseline
```

What is replaced: `core::changesets::ChangeSet` as the representation of agent
work, and `reviews/proposals/*.json` as the primary user-facing proposal store.

What is kept, deliberately: `core::reviews::PatchOperation` as the operation
vocabulary, `ReviewProposalPatchService` as the materializer, and
`ApplyProposalCommand` as the promotion mechanism. Promotion must remain an
ordinary audited edit; reusing these is what guarantees it.

## 2. Terms

| Term | Meaning |
|---|---|
| **Accepted baseline** | The SACM argument in the `.sacm` file. The only thing a human has accepted. |
| **Draft workspace** | The active proposal state for one argument file. At most one. |
| **Working model** | The complete assurance case produced by applying all active groups, in order, to the accepted baseline. |
| **Change group** | One coherent, user-reviewable change: clarify a claim, add a branch, apply one SCCG finding. May contain several operations. |
| **Promotion** | The explicit human action that turns selected groups into accepted SACM. |

## 3. Invariants

These are the assertions later phases are tested against. Every one of them
should have a test that has been observed to fail when the behaviour is broken.

1. No draft operation changes a single byte of the accepted `.sacm`.
2. Draft mutation records no audit transaction and triggers no `.sacm` autosave.
3. One argument file has at most one active workspace.
4. Materialization is deterministic: same baseline plus same groups, same model
   and same identities.
5. Generated element identities are stable across materializations and across
   save/load.
6. The workspace revision increases on every successful mutation, and a stale
   `expected_working_revision` is always refused.
7. Proposal state never appears in SACM output and never alters SACM assertion
   semantics.
8. A materialization failure leaves the accepted model untouched and names the
   group that caused it.
9. Promotion is refused before any mutation if the remaining groups cannot be
   rebased onto the prospective baseline.
10. No registered MCP tool applies, accepts or promotes.

## 4. Domain model

New `core::drafts` domain. `core` is the correct layer: `ai/`, `agent/` and
`app/` may include it and it may include none of them. Check
`cmake/check_layer_gates.cmake` still passes — the `mcp/` → `ai/` prohibition
must survive this work untouched.

```cpp
struct DraftWorkspace {
    std::string id;
    std::filesystem::path argument_file;

    std::string base_model_hash;
    std::uint64_t base_revision = 0;
    std::uint64_t working_revision = 0;

    DraftWorkspaceState state;              // Active | NeedsRebase | Blocked | Closed
    std::vector<DraftChangeGroup> groups;
    std::vector<DraftEvent> events;
};

struct DraftChangeGroup {
    std::string id;
    std::string title;
    std::string summary;
    std::string rationale;

    DraftSource source;                     // Human | Mcp | SccgAiReview | ImportedLegacyProposal
    std::string source_label;
    std::string source_session_id;

    std::string created_utc;
    std::string updated_utc;

    DraftGroupState state;                  // Building | Ready | NeedsAttention | Rejected
    std::vector<core::reviews::PatchOperation> operations;
    std::map<std::string, std::string> generated_ids;   // create_ref -> element id
    std::vector<std::string> guideline_ids;
    std::vector<std::string> review_item_ids;
    std::vector<std::string> depends_on_group_ids;
};
```

Promoted groups move to history rather than staying active.

`DraftEvent` gives MCP clients and the UI a revisioned feedback channel:
workspace created, group created, operations staged or replaced, SCCG findings
added, group rejected, changes requested, groups promoted, workspace rebased,
workspace blocked.

### Two things the plan must not get wrong

**Group staleness is positional, not baseline-relative.** `ReviewProposal` carries
`base_model_hash` and `base_element_hashes`, and
`EvaluateReviewProposalValidity` compares them against one current model. A group
at position *n* was authored against the working model after groups 1..*n*−1, so
that comparison is meaningless for it. Those fields are used only for imported
legacy proposals. Inside a workspace, staleness is the workspace revision. This
is the same class of defect already fixed once in change-set acceptance, where
checking references against the committed model alone made a multi-call change
unacceptable by construction.

**Identities are allocated once.** Materialization runs on every rebuild.
`ApplyProposal` generates identities as it goes, so a naive re-materialization
renames proposed elements between frames and breaks selection, the inspector, and
an agent's reference to what it just created. Record `create_ref` → id on the
first successful materialization; use `ApplyProposalWithIds` — the existing
replay variant — for every materialization after that.

## 5. Materialization

`DraftMaterializer` takes the accepted projection, applies active groups in
workspace order, and produces:

```cpp
struct DraftMaterializationResult {
    bool success = false;
    std::string error;
    std::string failing_group_id;
    parser::AssuranceCase working_model;
    DraftChangeIndex change_index;          // per element/relationship: what changed, by which groups
    std::vector<core::ProblemItem> findings; // structural + GSN + SCCG mechanical, over the combination
};
```

Ordering is by workspace sequence, never by source or UI sort order.

When several groups touch one element — MCP creates `G17`, SCCG rewords it, the
user clears its undeveloped marker — the canvas shows the combined result and the
inspector shows the ordered contribution history. The dependency graph is what
makes accepting the SCCG edit also accept the MCP creation.

**Materialization is now on the frame path.** It runs whenever the workspace
revision or the accepted model changes. Budget for it: reuse the
revision-comparison caching already in `workbench_area.cpp`, and measure on a
large argument before Phase 2 exits.

## 6. One authoritative view

```cpp
const parser::AssuranceCase& AppRuntime::CurrentArgumentView() const;
```

Working model when a workspace is active and materializes; accepted model
otherwise. Everything in ADR 0009's list reads through it. Export, the audit
baseline and the canonical model hash do not.

Delete the "newest open change set is the one drawn" rule in
`AppRuntime::RefreshAgentChangePreview` — there is no longer anything to choose
between.

## 7. Change vocabulary limits

`PatchOperationType` covers create (claim, strategy, solution, context,
assumption, justification), update text/name, set/clear undeveloped, add/remove
`supportedBy`, add/remove `inContextOf`, and remove element.

It has **no move operation and no challenge or counter-relationship operation**.
The first release therefore has no `MOVE` and no `CHALLENGE CHANGE` marker, and
the UI must not show one. Extending the vocabulary is a separate decision with
its own GSN review — load the `gsn-expert` skill before proposing it.

## 8. MCP

Connected reads return the working model — earlier MCP groups, SCCG groups, human
draft edits, and whatever remains after a partial promotion — and name the view:

```json
{
  "argument_file": "arguments/main.sacm",
  "view": "working_draft",
  "workspace_id": "draft-...",
  "working_revision": 18
}
```

Every modifying call carries `expected_working_revision`. A stale value is
refused with the current revision, and the agent rereads. This is what makes a
long multi-turn conversation survive a user edit or an SCCG run.

Tools: `get_draft_status`, `begin_change_group`, `stage_operations`,
`replace_change_group`, `remove_change_group`, `describe_change_group`,
`submit_change_group`, `describe_working_draft`, `get_draft_events`,
`close_change_group`. Deliberately no apply, accept or promote.

Every one of them carries `returns_case_content` per ADR 0007, and the
whole-registry test keeps covering the classification. Add a second registry
assertion: no tool promotes.

For one release, `begin_change_set`, `describe_change_set` and
`submit_change_set` alias the group tools and their results name the
replacement. Then remove them with a protocol-version bump.

Offline mode reads the accepted SACM only and does not open `.af/drafts/`.

## 9. SCCG AI review

The review payload is built from `CurrentArgumentView()`, so a review launched
after an MCP conversation evaluates the combination.

One `AiReviewRun` per invocation; one independently selectable group per
suggested correction, each linked to the run and to its `ReviewItem` IDs.
Findings stay findings; only suggested *model changes* become groups.

When an SCCG group edits an element an MCP group created, the dependency is
recorded: rejecting the MCP group rejects or blocks the SCCG group, accepting the
SCCG version selects both.

Adding SCCG suggestions reruns the deterministic checks. It never triggers
another paid AI review automatically.

**Consent note.** The review payload can now contain text an MCP client authored.
The review-request preview must say that draft content is included, and the draft
banner must be visible when a review is launched. See ADR 0009's consequences.

## 10. UI

Persistent banner above the canvas while a workspace is active, stating the
count and the contributing sources, with review / view-toggle / promote actions.
Not colour-alone, per the project's existing rule.

Three view modes: **working draft**, **accepted baseline**, **changes only**.

Element markers name the change and the source (`NEW · MCP`, `EDIT · SCCG`,
`REMOVE · MCP`, `EDIT · HUMAN`, `MULTIPLE CHANGES`). Relationship markers cover
new and removed support and context only — see §7.

**Relationship changes need first-class rendering and hit-testing.** The current
overlay decorates elements. A changed support relationship can alter the meaning
of an argument more than a reworded claim can, and it must be selectable.

One **Draft changes** panel replaces the split proposal / change-set surface.
Each row: source and session, rationale, element and relationship counts,
guideline and review-item links, dependencies, findings, and whether it is
promotable right now. Selecting a row focuses its changes on the canvas.

The inspector shows accepted value, working value, ordered contribution history
with source and rationale, dependencies, and the per-change actions.

Every new string goes through `AF_TR` / `trf`, gets an entry in
`tools/i18n/regenerate_ja_po.py`, and `python tools/i18n/check_catalog.py` passes
before push.

## 11. Selective acceptance

Dependencies are inferred from: references to elements another group created,
updates or removals of them, relationship endpoints created elsewhere, and
explicit source-declared dependencies. Store them so the UI can explain rather
than assert.

**Accept selected:** resolve selection → compute closure → show the closure →
materialize selected against the accepted baseline → materialize the remainder
against the prospective baseline → validate both → refuse before mutation if
either fails → compile to one `ReviewProposal` → dispatch one audited
`ApplyProposalCommand` → atomically swap baseline and workspace → rebuild.

**Accept all:** whole-draft validation → summary of every addition, edit,
removal and relationship change → explicit confirmation → one audited command →
clear workspace → one undo boundary.

**Reject:** remove from materialization, identify dependents, offer cascade or
leave them `NeedsAttention`, rebuild immediately.

**Accept from an element marker:** resolve the groups producing the visible
state, close over dependencies, and show the real acceptance set first.

### Undo

Two stacks, because draft mutation is not a command:

- Draft active → undo operates on the workspace; the accepted undo stack is
  untouched.
- Promotion → one boundary on the accepted stack.
- Undoing a promotion → restores the accepted baseline **and** the pre-promotion
  workspace snapshot, atomically. Promotion takes that snapshot for this reason.
  Restoring only the model leaves the remaining groups rebased onto a baseline
  that no longer exists.

## 12. Persistence

`.af/drafts/<argument-stable-key>/workspace.json` and `events.jsonl`, keyed by
project-relative argument path, atomically written after every successful
mutation.

**The project scaffold must generate `.af/.gitignore`.** `core::ProjectService`
creates `.af/cache`, `.af/backups`, `.af/snapshots` and `.af/history` today with
no ignore file, so those already reach a colleague through version control.
Drafts hold unaccepted AI-authored argument text and must not. This closes an
existing hole as much as it enables the new one — do it in Phase 1, not Phase 6.

Not tracked in `af.proj`. Not in SACM export. Not in the canonical model hash.

Reopen: base hash matches → restore and show the banner. Differs → `NeedsRebase`,
**no silent replay**, offer inspect / rebase / export / discard. A renamed
argument orphans its workspace; surface the orphan.

## 13. Legacy migration

On project open: find review items with `proposal_id`, load and validate the
proposal, convert to an `ImportedLegacyProposal` group preserving author,
timestamps, review-item link, rationale, operations and base hashes, materialize
them together, report conflicts, **apply nothing**. Archive the old files only
after the migration has saved successfully.

`ReviewItem` gains `draft_group_ids` and keeps reading `proposal_id` for the
migration window.

In-memory change sets need no migration: they are lost on restart today.

## 14. Code change map

**New** — `src/core/drafts/`: `draft_workspace`, `draft_workspace_store`,
`draft_materializer`, `draft_change_index`, `draft_dependency_graph`,
`draft_persistence`, `draft_promotion_service`.

**MCP and agent** — `src/agent/change_operations.*`, `src/agent/read_operations.*`,
`src/agent/operations.h` (`ReadContext` must reach the workspace),
`src/app/agent_request_handler.cpp`,
`src/app/controllers/agent_bridge_controller.cpp`, `src/mcp/tools.cpp`,
`src/mcp/session.cpp`, `src/bridge/protocol.*` if the version bumps.

**SCCG review** — `src/app/controllers/ai_review_controller.*`,
`src/app/actions/proposal_actions.*` (`CreateAiGenerated` stops writing proposal
files), the AI review result and event types, review-item linkage and
serialization.

**Runtime and views** — `src/app/app_runtime.cpp` (remove the newest-change-set
rule), `app_runtime_project.cpp`, `app_runtime_state.*`,
`src/app/areas/workbench_area.cpp`, `src/app/areas/review_panel_area.cpp`,
`src/ui/panels/review_panel.*`, `src/ui/gsn/gsn_canvas.cpp`, relationship
rendering and hit-testing, search and placement entry points.

**Project storage** — `src/core/project_service.cpp` for `.af/drafts` and the
generated `.af/.gitignore`.

**Retired** — `src/core/changesets/*`, `ProposalController` creator/preview
state, `ReviewProposalManager` as the primary store, proposal-file creation
paths. Kept: patch operations, preview logic useful to `DraftMaterializer`,
`ApplyProposalCommand`.

## 15. Phases

| Phase | Deliverable | Exit criteria |
|---|---|---|
| **0 — Decisions** | ADR 0009, ADR 0010, supersession notes on 0007/0008, index and nav, planned matrix rows, this invariant list. | Terms unambiguous; retained guarantees listed. **Done.** |
| **1 — Core domain** | `core::drafts`, deterministic materialization, combined checks, atomic `.af/drafts` persistence, `.af/.gitignore`, base-hash recovery. | Accepted bytes unchanged while groups are built; restart restores the same working graph and revision; two conflicting groups report one combined problem naming the failing group. |
| **2 — Unified view** | `CurrentArgumentView()`, canvas/navigator/search/inspectors on the working model, banner, view toggle, element and relationship decorations, accept-all and discard. | One integrated graph on screen; accept-all is one audited undoable transaction; discard leaves the `.sacm` byte-identical; materialization cost measured on a large argument. |
| **3 — MCP** | Working-model reads, revision-checked group tools, event polling, compatibility aliases, protocol and bridge tests. | One agent creates, inspects, revises and develops earlier changes across calls; a user or SCCG edit refuses a stale call with the current revision; registry test proves no promote tool exists. |
| **4 — SCCG review** | Review reads the working model; suggestions become review-linked groups; cross-source dependencies; consent preview text. | MCP changes then SCCG review appear in one graph; SCCG reports a problem introduced only by the combination; no new proposal files. |
| **5 — Selective acceptance** | Dependency graph, accept/reject selected, cascade, element-level closure, atomic rebase, promotion snapshot and two-stack undo. | A wording edit accepts alone; a new branch cannot accept without its nodes and relationships; remaining changes stay visible and valid; promotion refused before mutation when the remainder cannot rebase; undoing a promotion restores model and workspace together. |
| **6 — Migration and cleanup** | Legacy import, `ReviewItem` compatibility, removal of change-set paths, docs, diagrams, matrix. | Legacy projects open with no data loss; no path creates a legacy proposal file; change-set code removed or isolated as compatibility. |

## 16. Tests

**Core** — the ten invariants in §3, each with a test whose failure has been
observed by breaking the behaviour deliberately.

**Dependencies** — edit depends on creation; relationship depends on both
endpoints; SCCG edit depends on MCP creation; rejecting a parent finds every
dependent; element acceptance computes the right closure; partial promotion
rebases the remainder.

**MCP** — multi-call creation and later development of created ids; reads include
earlier MCP and SCCG work; concurrent clients serialize through the frame thread
and the revision check; offline stays read-only and does not read `.af/drafts`;
consent revocation still fails closed on the next call; no promote tool.

**SCCG** — payload uses the working model; suggestion becomes a group; several
findings materialize together; combined conflicts surface; checks run after every
mutation.

**UI** — node and relationship markers; the three view toggles; group selection
focuses its content; the banner persists while any group is unaccepted; no
colour-only distinction.

**Audit and persistence** — accept-all is one transaction and one undo boundary;
accept-selected records group ids, source labels, guideline ids and the human
approver; restart recovers; base-hash mismatch enters recovery; SACM export
excludes draft state; legacy migration preserves content and attribution;
undoing a promotion restores both model and workspace.

### Release gate

The end-to-end scenario, run in the real application, not in tests:

1. Open `main.sacm`; connect an MCP client.
2. Build a new argument branch over several calls; revise wording in a later turn.
3. Confirm the canvas shows every addition **and relationship change**.
4. Run SCCG AI review; confirm it reviewed the combination.
5. Add its suggestions to the same workspace; confirm source badges.
6. Accept one independent wording group; confirm it enters the baseline and the
   rest stay visible and valid.
7. Restart; confirm the remaining groups recover.
8. Accept all; confirm one accepted SACM, no workspace, full audit attribution,
   working undo.

## 17. Capability matrix

Nothing here is marked `supported` until the gate scenario passes in the running
application. `supported` requires a cited test that exists;
`planned`/`candidate` rows must cite none.

- `AF-AI-009` — rewrite for the integrated workspace. The current row describes
  change-set mechanics in detail and every one of those sentences becomes false.
- `AF-AI-010` — SCCG checks run on the combined working model.
- `AF-METH-004` — from persisted per-item proposals to review-linked draft groups
  once migration lands.
- New `planned` rows: dependency-aware selective promotion; draft persistence and
  recovery.

Run `python tools/features/export_feature_matrix.py` and
`python tools/features/check_feature_matrix.py`; the `feature_matrix_check`
CTest enforces both. Use the `feature-matrix-steward` agent for the judgement
pass before any row is raised.

## 18. Non-goals for the first release

An in-app chat interface. Unattended or headless acceptance. Multiple alternative
draft branches per argument. Real-time multi-user editing. Git-style merge
conflict resolution. Proposal status as SACM semantics. Automatic repeated paid
review loops. Move and dialectic-relationship operations (§7).
Standards-clause traceability, which stays `AF-AI-013`.

## 19. Risks

| Risk | Control |
|---|---|
| Working draft mistaken for accepted argument | Persistent banner, per-element badges, explicit view label, accepted-baseline toggle. |
| Selective acceptance leaves an incoherent argument | Dependency closure, prospective materialization of both halves, atomic refusal. |
| Long MCP session writes against a stale draft | Mandatory `expected_working_revision`. |
| SCCG and MCP overwrite each other | Ordered groups, revision checks, dependencies, one materialized graph. |
| Draft silently replayed onto a changed baseline | Base-hash check, `NeedsRebase`, no silent replay. |
| Proposal metadata contaminates SACM interchange | Stored outside SACM; excluded from export and from the canonical hash. |
| Unaccepted AI text committed to version control | Generated `.af/.gitignore`, delivered in Phase 1. |
| Proposed element identities change between frames | Identities allocated once and replayed via `ApplyProposalWithIds`. |
| Undoing a promotion strands the remaining draft | Promotion snapshots the workspace; undo restores model and workspace together. |
| Materialization cost lands on the frame path | Revision-keyed caching; measure on a large argument before Phase 2 exits. |
| AI inference paths converge | Share only `core::drafts`; the `mcp/` → `ai/` gate stays. |
| The draft domain becomes a second writer | Only the running application mutates and persists workspaces. |

## 20. Delivery

Umbrella issue: **Integrated AI draft workspace for MCP and SCCG review**, with
one sub-issue per phase deliverable. Issue #261 ("MCP follow-ups after the
single-owner rebuild") should be re-pointed at it or closed into it in Phase 6.

Keep decisions and capability claims out of implementation pull requests where
practical, and never merge implementation that contradicts an accepted ADR.

## 21. The rule

> Assurance Forge has one accepted SACM baseline and, while changes are being
> considered, one integrated working draft per argument file. Every AI and human
> draft change is evaluated in that complete working graph. Only an explicit
> human promotion changes the accepted SACM.
