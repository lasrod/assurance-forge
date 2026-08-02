# 0009. One integrated working draft per argument file

- Status: Proposed
- Date: 2026-08-02
- Deciders: Assurance Forge maintainers
- Supersedes in part: ADR 0007's "its only write surface is `ReviewProposal`
  drafts" scope statement
- Supersedes in part: ADR 0008's representation of agent work as independent
  `core::changesets::ChangeSet` objects

## Context

Assurance Forge has two paths by which an AI can propose a change to a safety
argument, and they cannot see each other.

**The MCP path** builds a `core::changesets::ChangeSet` in memory, one per
connection. It is visible from its first staged operation, which was the point
of ADR 0008, but three properties of it now bite:

- `ChangeSetStore::Begin` replaces whatever a connection had open, so an agent
  has exactly one change in flight and cannot build a second on top of it.
- Two connected clients produce two change sets and
  `AppRuntime::RefreshAgentChangePreview` draws `open.back()` — the newest. The
  Review panel lists the other so it is not hidden, but the canvas never shows
  both at once, and nothing anywhere shows their combination.
- `agent::ReadContext` holds only a `const core::AppState&`. It has no access to
  the change-set store by construction, so **a connected read returns the
  committed argument even to the agent that staged the operations**. An agent
  continuing a long conversation cannot read back the argument it is building.

**The in-app SCCG path** writes a separate `ReviewProposal` file under
`reviews/proposals/` per suggested correction, each linked to one `ReviewItem`,
each reviewed and applied on its own. The payload sent to the provider is built
from the committed model, so a review launched during an MCP conversation is
blind to everything that conversation has proposed.

Neither mechanism is wrong about consent or ownership. ADR 0007's gate and
no-agent-apply rule hold; ADR 0008's single owner, frame-thread execution and
ordinary command path hold. The defect is narrower and entirely about
representation: **several individually reasonable proposals can be authored
against different effective versions of the same argument.** A duplicated claim,
a strategy left developing into nothing after another proposal removed its only
child, an evidence link weakened by a wording change elsewhere — none of these
are visible in any single proposal. They appear when the proposals are combined,
which today happens for the first time in the accepted model.

The mechanism that would catch them already exists. `core::sccg::staged_checks`
and the GSN well-formedness rules run over an assurance case; they simply have
never been given the *combination* to run over, because no such object exists.

## Decision

We will maintain **at most one active integrated working draft per argument
file**.

The accepted SACM argument remains the baseline. A draft workspace holds ordered,
attributable change groups originating from MCP clients, SCCG AI review, imported
legacy proposals, and the user's own draft edits. Assurance Forge materializes
those groups, in workspace order, into **one complete working assurance case**.

### One authoritative view

An application-level resolver returns the materialized working model when a draft
workspace is active and materializes, and the accepted model otherwise. The
following read through it and no longer read the accepted model directly:

- GSN canvas and Argument Navigator.
- Element and relationship inspectors.
- Search and placement suggestions.
- Connected MCP read operations.
- SCCG AI review payload construction.
- Structural validation, cycle detection, GSN well-formedness checks and SCCG
  mechanical checks.

Export and audit stay on the accepted baseline. Normal SACM export writes the
accepted case; the audit baseline and canonical model hash are computed from the
accepted case. A labelled working-draft export may be added later as a separate
decision.

`AppRuntime::RefreshAgentChangePreview`'s "the newest open change set is the one
drawn" rule is removed. There is nothing to choose between.

### Convergence point, and what does not converge

MCP, SCCG AI review and interactive draft editing converge on a new
`core::drafts` domain. `core` is the correct home: `ai/`, `agent/` and `app/` may
all include it, and it may include none of them.

**The inference paths do not converge.** MCP does not invoke the in-app provider
layer and the in-app review does not use MCP credentials or consent.
`cmake/check_layer_gates.cmake` continues to forbid `mcp/` from including `ai/`,
and that gate is what keeps this decision from eroding into a shared inference
path by convenience.

### Change vocabulary

A draft group's operations are exactly the existing
`core::reviews::PatchOperation` set. That set has no move operation and no
challenge or counter-relationship operation, so **the first release of the draft
workspace cannot represent a move or a dialectic-relationship change, and the UI
must not show markers for either.** Extending the vocabulary is a separate
decision, taken when the domain needs it rather than when a mock-up implies it.

### Staleness is a workspace revision, not a base hash

Every modifying MCP call names the working revision it was based on. A call
naming an older revision is refused and the client must reread the working
argument. This is what protects a long conversation when the user, an SCCG review
or another client changes the draft between two of its calls.

A group's operations are authored against the working model as of its position in
the stack, **not** against the accepted baseline. `base_model_hash` and
`base_element_hashes` on a `ReviewProposal` compare against a single current
model and therefore cannot express that; they are retained only for imported
legacy proposals, where they mean what they always meant. Group-level staleness
inside a workspace is the workspace revision.

### Editing while a draft is active

When a draft workspace is active, model-affecting interactive edits enter a
human-authored draft group. Assurance Forge will not maintain a second,
independently changing accepted editing surface underneath an active draft. The
user returns to ordinary accepted-case editing by promoting or discarding the
draft. Navigation, comments, review-item status, preferences and non-argument
metadata are unaffected.

### The workspace follows the open argument

A workspace belongs to one argument file. The application has one argument
loaded, so exactly one workspace materializes at a time. As with change sets
today, an MCP call against a workspace whose argument is not the loaded one is
refused, naming the file to open.

### Offline MCP

Offline MCP mode reads the accepted SACM only, and refuses every draft mutation.
It does not read `.af/drafts/` even though ADR 0010 makes that state readable.
The accepted baseline is the only content a human has accepted, and a headless
read has no application UI to mark unaccepted content as unaccepted. The adapter
remains transport-only and never becomes a second model owner.

### No AI promotion

No MCP tool and no AI response can promote draft work. Promotion is an explicit
human action in Assurance Forge. The absence of an apply, accept or promote tool
is asserted over the whole tool registry, in the same manner ADR 0007's
`returns_case_content` classification is asserted, and every new draft tool
carries that classification.

## Consequences

- MCP, SCCG AI review and the user reason about the same complete working
  argument. A conflict between two proposals becomes a finding on the combined
  model instead of a surprise after acceptance.
- Connected MCP reads change meaning. They now include all active draft work —
  including another client's, and the user's own draft edits — and identify the
  workspace revision they were taken at. A client that assumed reads return
  accepted content will be wrong, which is why the response names the view.
- **Two independently consented egress paths now compose.** An SCCG review
  payload built from the working model can contain text an MCP client authored,
  and an MCP read can return text the configured AI provider authored. Each
  transfer is still covered by its own gate — ADR 0005 for the provider, ADR 0007
  for MCP — but the user consented to each path separately and not obviously to
  the composition. The review-request preview must therefore state that draft
  content is included, and the draft banner must be visible when a review is
  launched.
- Concurrent clients lose their private sandboxes. Today two agents cannot
  disturb each other because each has its own change set; now they share one
  draft and the loser of a race is told to reread. That is a deliberate downgrade
  in isolation and the whole point of the change: two agents working on one
  argument should see each other's work.
- The canvas gains a real obligation it does not have today: a relationship
  change must be rendered and hit-tested as a first-class object. A changed
  support relationship can alter the meaning of an argument more than a reworded
  claim, and the current overlay decorates elements.
- Materialization is on the frame path. It runs whenever the workspace revision
  or the accepted model changes, over an argument that may be large, and its cost
  is now in the interactive loop rather than in a preview nobody was waiting on.
- A materialization failure must be non-destructive: the accepted model is
  untouched and the failure is reported against the group that caused it.
- ADR 0007 remains authoritative for consent and for the rule that an agent
  cannot apply its own work. Its statement that the server's only write surface
  is saved `ReviewProposal` drafts is replaced.
- ADR 0008 remains authoritative for single ownership, the IPC boundary,
  frame-thread execution, the ordinary command and audit path, and offline
  read-only behaviour. Its `ChangeSet` representation is replaced.
- `docs/features/mcp-server.md`'s change-set design and capability rows
  `AF-AI-009` and `AF-AI-010` describe a mechanism that will no longer exist and
  must be rewritten rather than amended.

## Alternatives considered

**Keep independent change sets and merge them only at acceptance.** Cheapest, and
it preserves per-client isolation. Rejected because the merge would happen in the
accepted model, which is exactly where a conflict must not first appear, and
because it leaves connected reads unable to see staged work — the defect that
makes multi-turn agent conversations fail today.

**Give each client its own working draft.** Preserves isolation and still fixes
read-back. Rejected because the user would then have several complete candidate
arguments to reconcile by hand, SCCG review would have to pick one to review, and
the canvas would be back to choosing which to draw. It moves the combination
problem from the model to the person.

**Represent drafts as branches of the accepted case, git-style.** Familiar, and
it makes divergence explicit. Rejected as out of scope for this decision: it
needs merge conflict resolution over SACM structure, which is a much larger
problem than the one being solved, and safety-argument merge semantics are not
obviously textual. Recorded as a non-goal rather than a rejection on principle.
