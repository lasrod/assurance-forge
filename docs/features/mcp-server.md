# MCP Server — build safety cases by chat

- **Status:** Implemented and confirmed in a running app. Capability matrix row `AF-AI-007`.
- **Date:** 2026-07-26 (plan), 2026-07-27 (read side, then write side)

## Implementation status

| Phase | State |
|---|---|
| 2 — `af_mcp` layer, JSON-RPC over stdio, consent gate, read tools | **Implemented** |
| 1 — top goal in the proposal vocabulary | **Implemented**, differently (see below) |
| 3 — writing via proposals | **Implemented** |
| 4 — discoverability, preferences toggle, consent ADR | **Implemented** |

Shipped in phase 4:

- A **Preferences toggle** for the consent flag, so enabling MCP no longer means
  hand-editing the settings file.
- A **copyable client configuration** block naming the server binary and the open
  project, so a user does not hand-write JSON containing two absolute paths.
  Paths use forward slashes, which need no JSON escaping.
- **ADR 0007** records the consent decision. ADR 0005 governs provider egress and
  does not cover a server answering an arbitrary connecting client.

Confirmed in a running app: the Preferences section renders, the consent toggle
writes `mcp.enabled` to the settings file, and the sibling `ai` section survives
that write — the live counterpart to the regression test added when
`AiSettingsStore::Save` was fixed. The consent flag was toggled back off
afterwards; it is not something a check should leave switched on.

Phases 1 and 2 were built in the opposite order to the numbering below. The
top-goal operation is a prerequisite for *writing*, not for the server itself, so
building the server first produced something runnable sooner without changing any
published tool schema.

### Phase 1 needed no new operation

The plan called for a `PatchOperationType::CreateTopGoal`. Reading the patch
service showed it was unnecessary: `ApplyCreateOperation` creates a standalone
element with no parent, and relationships come from separate `AddSupportedBy`
operations, so **`CreateClaim` already produces exactly a top goal**. A distinct
operation would have been redundant surface producing byte-identical output.

The real blocker was `EvaluateReviewProposalValidity` requiring a non-empty
`anchor_element_id` that resolves. That is now relaxed for *purely additive*
proposals only: a proposal may omit its anchor when it names no affected existing
elements and no operation references an existing id. Anything that touches an
existing element still requires an anchor, so no staleness detection is lost.

Shipped in phase 3:

- `preview_review_proposal` — dry-runs operations and reports effects, writing
  nothing.
- `create_review_proposal` — saves a draft through `ReviewProposalManager`,
  attributed to the connected client (`MCP: <client> <version>`). Runs the same
  validity check and dry run as preview, so nothing that cannot apply reaches
  disk.
- `list_review_proposals` / `get_review_proposal` — including whether each draft
  still applies to the current case.

The agent never edits the case. It cannot make a change nobody looked at, and
because drafts apply through the existing `ApplyProposalCommand` and
`PatchOperationType` vocabulary, it cannot make one the app could not make
itself.

Shipped in phase 2:

- `assurance-forge-mcp`, a headless binary an MCP client launches
  (`src/mcp/main.cpp`).
- JSON-RPC 2.0 over newline-delimited stdio (`src/mcp/jsonrpc.cpp`), with
  `initialize`, `notifications/initialized`, `ping`, `tools/list` and `tools/call`
  (`src/mcp/server.cpp`). Protocol version is pinned, not echoed.
- Read tools `get_case_overview`, `find_elements`, `get_element` and
  `get_argument_tree` (`src/mcp/tools.cpp`).
- A consent gate that fails closed, driven by `mcp.enabled` in the settings file.

### Two things the plan below got wrong

**`LibraryDocument` is opaque.** The plan assumed the read tools would call
`Document::for_each_element` and `sacm::validation::validate` directly.
`sacm_adapter::LibraryDocument` exposes no accessors by design — that is what
keeps `sacm::model` names out of `core` — so the read tools work off
`parser::AssuranceCase`, the same projection the GUI renders, plus
`core::AssuranceTree` for structure. This is a better answer anyway: an agent and
a user now see the same view of the argument.

**`validate_case` is deferred.** It needs a new `sacm_adapter` seam to reach the
library's validator. Until then load-time diagnostics are surfaced through
`get_case_overview`'s `load_warnings`, which is honest but narrower than a full
validation pass. `list_review_items` and `get_audit_history` are likewise not
built yet.

### Note on the settings path

The consent gate reads the same `settings.json` the AI settings live in, but the
layer gate forbids `mcp/` from including `ai/`. The path rules therefore exist in
two places: `core::UserSettingsFilePath` and
`ai::AiSettingsStore::DefaultSettingsPath`. `tests/test_user_settings_path.cpp`
pins them equal, because a divergence would point the consent gate at a file
nobody writes.

## Summary

Expose Assurance Forge over the [Model Context Protocol](https://modelcontextprotocol.io) so a user
can drive it from their own AI client (Claude Desktop, Claude Code, Cursor). Their subscription pays
for inference, no API key is configured in Assurance Forge, and the interaction becomes a
conversation that can read, reason about, and propose changes to a safety case.

This is a **second, separate** AI capability. The existing SCCG review keeps working exactly as it
does today on its own OpenAI path.

## Context

Today's AI review calls the OpenAI Responses API with a user-supplied token held in Windows
Credential Manager. Every user must obtain and configure a key, the app pays for inference, and the
interaction is one-shot: right-click an element, get findings back. There is no way to converse about
a safety case while building it.

MCP is a **tool** protocol, not an inference protocol. As an MCP *server*, Assurance Forge gains a
conversational surface and drops the key requirement, but it can no longer initiate reasoning on its
own — which is precisely why the existing provider path must stay for app-initiated review.

## Scope decisions

- **Purely additive; the two AI features never touch.** The MCP server does not consume the in-tool
  AI, shares no prompt path with it, and does not run SCCG review. `src/ai/` is unmodified apart from
  one settings-section change that touches no AI behaviour.
- **Chat lives in the user's AI client.** No in-app chat panel is built.
- **The MCP server never writes SACM XML.** Its only write surface is `core::reviews::ReviewProposal`
  drafts that a human accepts in the GUI. This upholds *"the tool must never silently modify or
  reinterpret safety arguments"* using machinery that already exists, and removes the dual-writer
  problem — the SACM file is read-only to the server.
- **Create scope:** structure inside an already-open case, plus its top goal. Not creating a new
  project from scratch.
- **Separate headless binary over stdio**, launched by the AI client. No port, no GUI coupling.

## Why no GSN work is required first

An earlier reading of `src/sacm_adapter/document_edit.h` suggested that a strategy could not carry
more than one sub-goal, which would have blocked the most common GSN authoring request. That is
**not** the case — the gap is closed. `apply_add_subgoal_under_strategy` in
`src/sacm_adapter/document_edit.cpp` materializes the strategy's single `AssertedInference` on the
first sub-goal and extends its `source` list on later ones, covered by
`SACM23_INT_001_SubGoalUnderStrategyMaterializesThenExtendsInference` and by
`tests/test_library_primary_edit_flip.cpp`, which also asserts library-vs-legacy parity.

The comment in `document_edit.h` and the block comment in `document_edit.cpp` describing this as "a
following increment" are stale and should be deleted as part of this work.

The remaining GSN gaps do not block chat authoring:

- Rows 3, 7, 8, 9, 11 and 13 of `docs/sacm/sacm-gsn-metamodel-gaps.md` are class **(c)** — not
  expressible in SACM 2.3 at all, and open at OMG as SACM24-82/-83. They move at RTF pace regardless
  of what is built here.
- NodeOnly reparent (delete a node, keep its children) has no SACM retarget operation. It is a real
  limit, but it is equally the GUI's limit.

That generalises into the core safety argument for building the write surface now: MCP proposals are
applied by the existing `ApplyProposalCommand` through the same 15-operation `PatchOperationType`
vocabulary the current AI-review flow already uses. **The MCP write surface cannot be more capable,
or more broken, than the GUI itself.**

## What the architecture already provides

- `libs/sacm` `Document::preview()` for dry-run, `apply(op, expected_revision)` for optimistic
  concurrency, and `MutationResult::changes` for structured diffs. **No phase changes anything under
  `libs/sacm/`, so no conformance-matrix update is triggered.**
- `core::reviews::ReviewProposal` — a serializable patch with `base_model_hash` and
  `base_element_hashes` for staleness detection, plus `EvaluateReviewProposalValidity`.
- `ReviewProposalManager::ListProposals` memoises on a directory signature (file count + summed
  mtimes) with a poll interval, so the GUI already discovers externally-written proposal files with
  no new file-watching work.

## Phase 1 — Top goal in the proposal vocabulary

The one genuine prerequisite. `PatchOperationType` has no top-goal operation, and `ReviewProposal`
carries an `anchor_element_id` that an empty case cannot supply.

- Add `PatchOperationType::CreateTopGoal` in `src/core/reviews/review_proposal.h`, with its
  `PatchOperationTypeToString` / `FromString` entries.
- Route it in `core/reviews/review_proposal_patch_service` to the existing `core::AddTopGoal` /
  `CreateTopGoalCommand`.
- Allow an empty `anchor_element_id` when a proposal's operations are all top-goal creates, and make
  `EvaluateReviewProposalValidity` treat that as `Valid` rather than `Broken`. Confirm
  `ComputeModelSemanticHash` behaves on an empty model.
- The GUI proposal list must render an unanchored proposal sensibly.

Useful independently of MCP — it closes a hole in the proposal format itself.

## Phase 2 — `af_mcp` layer, JSON-RPC over stdio, read tools

**New layer.** Add `af_mcp` as an OBJECT library in `src/CMakeLists.txt` and register the gate in
`cmake/check_layer_gates.cmake`:

```cmake
set(_AF_FORBIDDEN_mcp "ui/" "app/" "ai/" "export/")
```

Forbidding `ai/` is deliberate: it is the build-time enforcement of the two AI features staying
separate.

**New executable** `assurance-forge-mcp` in the root `CMakeLists.txt` via plain `add_executable` —
*not* `hello_imgui_add_app`, since there is no window. Links
`af_mcp af_core af_sacm_adapter af_sacm af_parser`. Entry `src/mcp/main.cpp`, taking
`--project <path>` or `AF_MCP_PROJECT`. Needs `af_copy_sccg_data(assurance-forge-mcp)` so catalog
discovery resolves beside the binary.

**Project loading.** Reuse `core::AppState::open_project` — it yields the `LibraryDocument` for reads,
the projected `parser::AssuranceCase` for hashing and tree building, and the project root for
`ReviewProposalManager`, all within `core`. **Do not open a `CommandBus`**: the server executes no
commands and must not touch the audit store.

**Protocol.** No mature C++ MCP SDK is worth depending on; hand-roll JSON-RPC 2.0 over
newline-delimited stdio with nlohmann. Implement `initialize` (pin and verify a protocol version),
`notifications/initialized`, `ping`, `tools/list` and `tools/call`. Structure it as a pure
`HandleRequest(const json&, McpSession&) -> json` so the protocol is unit-testable without spawning a
process.

!!! warning "stdout is the transport"
    A single stray `printf` / `std::cout` from any linked layer corrupts the stream, and it will
    present as a client bug. Audit `core`, `parser`, `sacm` and `sacm_adapter` for stdout writes,
    route everything to stderr, and test for byte-exact framing.

**Consent gate, before any tool returns case content.** Add an `"mcp"` section to the settings file
with `enabled` defaulting to `false`; refuse every content-bearing tool with a clear error until it
is set. `AiSettingsStore::Save` currently rewrites the whole JSON document and only ever emits the
`"ai"` key, so it must be taught to preserve sibling sections. ADR-0005 covers provider egress and
does **not** cover an MCP server handing case content to an arbitrary connecting client — this needs
a new ADR.

**Read tools**

| Tool | Backed by |
|---|---|
| `get_case_overview` | `Document::roots` / `element_count` / `revision`, `AppState::load_warnings` |
| `find_elements(query, kind?, limit)` | `Document::for_each_element` |
| `get_element(id, include_neighbourhood)` | `Document::find`, relationship walk |
| `get_argument_tree(root_id?, depth?)` | `core::AssuranceTree::Build` |
| `validate_case(strict?)` | `sacm::validation::validate` |
| `list_review_items(element_id?)` | `core/reviews/review_item_manager.h` |
| `get_audit_history(limit)` | `audit::EventStore`, read-only |

Every result carries the document `revision`. Hard `limit` defaults on `find_elements` and
`get_argument_tree` are a correctness requirement — a large case will otherwise exhaust the client's
context on one call.

## Phase 3 — Writing, via proposals only

- **`preview_operation(operation)`** — wraps `Document::preview` on a scratch document so the agent
  can ground a patch before proposing it. For deletes use `sacm_adapter::preview_delete_elements`,
  which correctly models a *set* delete (deleting three sub-goals is not the union of deleting each
  one).
- **`create_review_proposal(anchor_element_id?, title, summary, operations[])`** — builds a
  `ReviewProposal`, stamps `author_name = "MCP: <clientName>"` from the `initialize` handshake,
  computes `base_model_hash` via `ComputeModelSemanticHash` and per-element hashes via
  `ComputeElementSemanticHash`, then calls `ReviewProposalManager::SaveProposal`. Staleness detection
  then behaves exactly as it does for AI-authored proposals, and the GUI picks the file up on its next
  directory-signature poll. Accepts the Phase 1 `CreateTopGoal` operation with no anchor.
- **`list_review_proposals`** / **`get_review_proposal(id)`** — so the agent can see what it already
  proposed rather than duplicating.

Use a collision-resistant proposal id; two concurrent MCP sessions must not clash in the proposals
directory.

Deliberately **not** exposed: any `apply_*`, `CommandBus::Execute`, or `undo`. Acceptance is a human
action in the GUI.

## Phase 4 — Discoverability and docs

- In-app **"Copy MCP client config"** button in preferences emitting the block for the current
  project — users will otherwise hand-edit JSON:

    ```json
    {"mcpServers":{"assurance-forge":{"command":"C:/.../assurance-forge-mcp.exe",
                                      "args":["--project","C:/cases/MyCase"]}}}
    ```

- A visible indicator that MCP is enabled, and enough prominence on agent-authored proposals that
  they do not accumulate unseen.
- `docs/architecture/mcp-server.md` and the new consent ADR.
- *Optional, deferred:* expose the SCCG catalog read-only as authoring reference (house style for
  claim wording). This needs `app::GuidelineCatalog` moved to `core` — a clean lift-and-shift, since
  it depends only on `parser/guidelines_parser.h`. It is reference data only and does not run review,
  so it does not breach the separation.

## Files touched

**New:** `src/mcp/{main.cpp, jsonrpc.{h,cpp}, server.{h,cpp}, session.{h,cpp}, tools_read.{h,cpp},
tools_proposals.{h,cpp}}`, `src/mcp/CMakeLists.txt`.

**Edited:** `src/core/reviews/review_proposal.h` and its patch service (Phase 1); root
`CMakeLists.txt` (executable, test files, SCCG copy); `src/CMakeLists.txt` (`af_mcp`);
`cmake/check_layer_gates.cmake` (gate); `src/ai/ai_settings.cpp` (settings section preserving
siblings — the only `src/ai/` change, and it touches no AI behaviour); the GUI proposal list
(unanchored proposals); stale comments in `src/sacm_adapter/document_edit.{h,cpp}`.

**Untouched:** all of `libs/sacm/`, so no conformance-matrix update is triggered; all AI review
behaviour in `src/ai/`.

## Verification

The layer gate runs at configure time and as an `ALL` target, so a stray cross-layer include fails
the build.

**Unit tests** in the `tests` target — the root `CMakeLists.txt` lists all test files explicitly, so
each new file must be added there:

- `test_review_proposal_top_goal.cpp` — a `CreateTopGoal` proposal round-trips through serialize /
  deserialize, evaluates `Valid` with an empty anchor, and applying it yields a top goal
- `test_mcp_jsonrpc.cpp` — handshake, malformed request, unknown method, exact framing
- `test_mcp_tools_read.cpp` — each read tool against `tests/data/` fixtures, limits enforced
- `test_mcp_consent.cpp` — every content-bearing tool refuses while `mcp.enabled` is false
- `test_mcp_proposals.cpp` — a created proposal loads back via `LoadProposal` and evaluates `Valid`;
  mutate the model and assert it evaluates `Broken`
- `test_ai_claim_review.cpp` and `test_ai_review_controller.cpp` must pass **unmodified**, proving the
  existing review path is untouched

**Process-level smoke tests** via `add_test`, following the four `SacmCli*` CTests in
`libs/sacm/CMakeLists.txt` as prior art: pipe `initialize` + `tools/list` into the binary and assert
a well-formed response and clean exit.

**End-to-end, manual.** Point Claude Desktop at `assurance-forge-mcp` with a real project and confirm:

1. it can read and explain the argument;
2. asking for a strategy decomposed into three sub-goals produces a proposal with one inference
   carrying three sources;
3. the proposal file appears in the project's proposals directory and the *running* GUI surfaces it
   without a restart;
4. accepting it produces an audited transaction;
5. undo reverses it;
6. right-click → SCCG review still works exactly as before, on its own OpenAI path.

Before pushing: `python tools/i18n/check_catalog.py` and
`python tools/sacm/check_conformance_matrix.py`.

## Risks

- **stdout pollution breaks the transport.** Highest-probability failure and the easiest to miss.
- **We own protocol conformance.** No C++ SDK means MCP spec revisions are our maintenance burden.
  Pin a version in `initialize` and fail loudly on mismatch rather than guessing.
- **Context cost.** A large safety case cannot be handed to a chat client wholesale.
- **Agents amplify the remaining gaps.** A human clicks around the NodeOnly-reparent limitation
  without noticing; an agent restructuring a subtree hits it immediately. `preview_operation` must
  report unsupported cases honestly rather than approximating them.
- **Discoverability.** Requiring a hand-edited JSON config will lose non-technical users; the Phase 4
  config-copy button is not optional polish.
