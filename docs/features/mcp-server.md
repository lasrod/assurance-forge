# MCP server — work on a safety case by chat

- **Status:** Implemented. Capability matrix rows `AF-AI-007` (read),
  `AF-AI-008` (bridge), `AF-AI-009` (propose changes).
- **Architecture:** ADR 0008 — one owner for the open project.
- **Consent:** ADR 0007 — unchanged.

Expose Assurance Forge over the [Model Context Protocol](https://modelcontextprotocol.io)
so a user can drive it from their own AI client (Claude Desktop, Claude Code,
Cursor). Their subscription pays for inference, no API key is configured in
Assurance Forge, and the interaction becomes a conversation that can read,
reason about, and propose changes to a safety case.

This is a **second, separate** AI capability. The SCCG review keeps working
exactly as it does on its own OpenAI path, and the layer gate forbids `mcp/`
from including `ai/` so the two cannot grow a shared inference path.

## How it fits together

```
Claude Desktop / Code / Cursor
   │ stdio, JSON-RPC 2.0
   ▼
assurance-forge-mcp          ← MCP transport only; holds no model
   │ src/bridge — versioned local IPC (named pipe / AF_UNIX, user-private, token)
   ▼
Assurance Forge
   ├── the single loaded model, always current
   ├── the single writer of every project file
   ├── requests execute on the frame thread → no lock, no stale copy
   └── the agent's work drawn live on the GSN canvas
```

`src/agent` holds the operations. Both binaries link it, so an agent gets the
same answers whether or not a window is open.

### Two modes

| | Connected | Offline |
|---|---|---|
| When | Assurance Forge has the project open | No application running |
| Reads | Answered by the application, from the argument the user is looking at, including unsaved edits | Answered from a copy the adapter loaded |
| Changes | Change sets, accepted by a person | **Refused** |

Offline is read-only because changing a safety case needs a command bus, an
audit log and a human to accept it. The refusal says so and says what to do.

The mode is fixed at connection time. A session does not promote itself when the
application starts later: the client has been told what this connection can do,
and changing that mid-conversation is worse than asking for a reconnect.

## Reading

`get_case_overview`, `find_elements`, `get_element`, `get_argument_tree`,
`list_case_files`, `open_case_file`.

`suggest_placement(topic)` answers a question substring search cannot: *where
does new argument about this belong?* It returns ranked goals and strategies
with the path from the top goal, the sub-claims already there and the context in
scope. The agent still decides; without it, an agent guesses and attaches at the
root.

Hard limits on `find_elements` and `get_argument_tree` are a correctness
requirement, not a nicety — an unbounded call on a real case spends the whole
conversation.

## Changing: change sets

An agent builds a **change set** the user watches take shape.

1. `begin_change_set` — from here the user sees it in Assurance Forge.
2. `stage_operations` — checked against the current case and refused as a group
   if they would not apply, so the canvas can always draw the result. Returns the
   diff, the ids given to created elements, and SCCG findings.
3. `unstage_operations` — revise after "not there" without starting over.
4. `describe_change_set` — the full diff, and whether it still applies.
5. `submit_change_set` — hand it to the user for a decision.

**Staging changes nothing.** No SACM write, no command, no audit transaction.
The model changes exactly once, when a person clicks Accept in the Review panel.

**Acceptance is an ordinary audited edit.** It runs the unchanged
`ApplyProposalCommand`, so it is undoable, replayable and attributed to the
connecting client. No agent-specific command type exists — which is what
guarantees an agent cannot make a change the application could not make itself.

There is deliberately **no `apply` tool**. Acceptance is a human action.

### One change set, one argument

A change set records the argument file it was begun against, and everything
afterwards is checked against it. Staging, submitting and accepting are refused
while a different one of the project's arguments is open; `describe_change_set`
reports the mismatch rather than refusing, because a read is still owed an
answer. The refusal names the file and says to call `open_case_file`.

This is not bookkeeping. Every argument in a project is seeded from the same
template, so every argument has a `G1`, and an operation naming `G1` resolves
just as cleanly against the wrong document as the right one. Without the binding
a change set built against `main2.sacm` decorated `main.sacm`'s `G1` on the
canvas, and Accept refused it as stale — reporting the wrong cause, because the
element hashes it compared came from a document nobody had asked about.

### Watching it happen

While a change set is open the canvas draws the preview — the argument as it
would be if accepted — with additions marked NEW, edits EDIT and removals
REMOVE, each by border *and* badge so the cue survives colour-blindness. An
element the change set removes is put back for display only: a reviewer
approving a deletion should see what is being deleted rather than infer it from
a gap.

The per-package canvas projects an argument package through the ids that package
holds, and a staged element is in no package at all — nothing has been applied.
So the preview projection takes in an addition when it is connected to the
package, directly or through other additions; one connected to nothing goes on
the document's first argument package, which is where applying it would put it.
Without that, the canvas drew the committed argument while the Argument
Navigator, which builds from the preview directly, showed all of the staged
work — the two views of one change set disagreeing.

## SCCG

Three mechanisms, weakest first.

**Resources.** `sccg://guidelines` publishes the catalog, so a client can load
the rules at the start of a session.

**Prompts.** `draft_argument_from_standard`, `add_argumentation` and
`restructure_case` each carry the guidance for that job, quoted from the catalog
so prompt and guideline cannot drift. This is what makes an agent aware of the
rules *before* it writes.

**Checks on staged work.** Returned in the result of every staging call so the
agent self-corrects, and shown on the change set for the reviewer.

!!! warning "What the checks do and do not cover"
    Most of SCCG is prose only a reader can judge — whether a claim is
    *sufficiently* justified, whether evidence is *relevant*. Those are not
    decidable here, and claiming otherwise would make a green result read as
    conformance.

    The mechanical set is: **EV.1** a claim with no support and no undeveloped
    marker; **AR.2** a strategy that develops into nothing; **AR.1** a solution
    with children, and support cycles; **CL.5** an unbounded qualifier from the
    list SCCG itself names. Findings are advisory and never block acceptance —
    the reviewer is the authority on a safety argument.

## Consent

`mcp.enabled` in the settings file, off by default, toggled in Preferences
alongside a copyable client configuration. It fails closed on every failure path
— missing file, malformed document, absent or non-boolean flag — and is re-read
on every call, so revoking it takes effect immediately rather than when the
client happens to restart.

Connected clients are named in the Review panel. Something reading a safety
argument should never be invisible to the person responsible for it.

## Security of the bridge

Two gates. The operating system's access control on the pipe or socket (the
creating token's default DACL on Windows; mode 0600 in a user-private directory
on POSIX), and a 256-bit token published in an endpoint record under the user's
runtime directory. A local process that did not read that file is not the
adapter this application published for.

The endpoint record is deliberately **not** in the project: a pipe name, a pid
and a token describe this machine at this moment, and in the project directory
they would be hash-tracked by `af.proj` and would reach a colleague through
version control.

## Known limits

- **Writes need the application running.** The deliberate cost of ADR 0008.
- **Project creation is a user action.** An agent drafts into an argument file
  inside an already-open project.
- **Standards traceability is text, not citation.** The element model has no
  citation field, so "this goal answers ISO 26262-6:9.4.2" lives in the claim's
  description. A first-class citation is a separate decision.
- **No atomic re-parent.** Restructuring is expressed as remove-then-add
  supported-by pairs, which works but loses the intent in the diff. A `MoveUnder`
  operation is designed and not yet built.
- **One change set is drawn at a time.** Concurrent clients are supported and all
  their change sets are listed, but the canvas shows the most recent of those
  written against the argument that is open.
- **A change set is held in memory.** Restarting Assurance Forge discards an
  agent's staged work — expected from "not project data", and not obviously what
  a user wants after a large restructure.
- **Applying collapses argument packages.** The preview places each staged
  addition on the package it attaches to; `ApplyProposalCommand` writes every
  element into the document's first argument package. Pre-existing, shared with
  the interactive proposal flow, and only visible in a document that has more
  than one.
- **SCCG binding is a named subset plus advisory prose**, not "SCCG compliance".

## Verification

- `tests/test_bridge_*.cpp` — wire contract, endpoint records, transport,
  including a real loopback round trip and the shutdown path.
- `tests/test_agent_bridge_controller.cpp` — the online path end to end over a
  real pipe: handshake, token refusal, protocol mismatch, frame-thread execution.
- `tests/test_change_sets.cpp`, `tests/test_change_set_acceptance.cpp` — staging
  changes nothing; acceptance produces one audited `ApplyProposal` transaction
  through a real `CommandBus`.
- `tests/test_sccg_staged_checks.cpp` — one test per mechanical rule.
- `tests/test_mcp_modes.cpp` — offline is read-only, and every published tool
  leaves the project directory byte-identical.
- `cmake/run_mcp_smoke_test.cmake` — the consent gate through the real process.

Both platform branches of the transport are compiled: MSVC and MinGW g++ for
Windows, GCC under WSL for POSIX. Each toolchain proves which branch it took —
one resolves `<windows.h>`, the other `<sys/socket.h>`.
