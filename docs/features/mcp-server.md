# MCP server — work on a safety case by chat

- **Status:** Implemented. Capability matrix rows `AF-AI-007` (read),
  `AF-AI-008` (bridge), `AF-AI-009` (propose changes).
- **Architecture:** ADR 0008 — one owner for the open project; ADR 0009 and
  ADR 0010 — one persisted integrated draft with human-controlled promotion.
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
| Reads | The complete integrated working draft the user is looking at, including MCP, SCCG and human draft groups | Accepted SACM from a copy the adapter loaded |
| Changes | Revision-checked groups in the application's persisted draft workspace | **Refused** |

Offline is read-only because the running application is the sole owner of the
workspace, presents every unaccepted change to the user, and serializes edits
from all contributors through one revision. A headless copy cannot safely do
that. The refusal says so and says what to do.

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

Every case-content response identifies what it returned. Connected reads name
`view: working_draft` or `view: accepted`, plus `argument_file` and the current
`working_revision`; an active workspace also supplies `workspace_id`. Offline
reads name `view: accepted`. A client never has to infer whether text has been
accepted.

## Changing: integrated draft groups

An agent contributes one coherent **change group** to the same working draft as
the user and SCCG review.

1. Read the case or call `get_draft_status` and retain `working_revision`.
2. `begin_change_group` with a title, rationale and that expected revision.
3. `stage_operations` in small steps. Each call returns stable generated ids,
   combined findings and the next revision.
4. Use `replace_change_group` to revise the group wholesale after feedback;
   `describe_change_group` and `describe_working_draft` inspect the contribution
   and its effect on the combination.
5. `get_draft_events(after_revision)` reports what other contributors changed.
6. `submit_change_group` marks the work ready for the user. `remove_change_group`
   or `close_change_group` abandons it without deleting its provenance record.

Every modifying call carries `expected_working_revision`. If a human edit, SCCG
review or another MCP client changed the shared draft, the call is refused with
`current_working_revision`; the agent rereads and decides whether its intended
operation still means the same thing. This is optimistic concurrency over the
semantic graph rather than silent last-writer-wins.

An MCP session may inspect the whole working draft but may mutate only groups it
created. This prevents one client from rewriting human, SCCG, or another
client's work while still letting all of them reason about the same argument.

**Staging changes no accepted assurance data.** It writes only recovery state
under `.af/drafts`, with source label, session identity, operations, stable
generated ids, dependencies and events. It performs no SACM write, no command
and no audit transaction. Restarting Assurance Forge restores the same graph and
revision.

Promotion remains an ordinary audited `ApplyProposalCommand`, undoable and
replayable, and is exposed only in Assurance Forge. There is deliberately no
MCP `apply`, `accept` or `promote` tool; a registry-wide test enforces this.

For one migration release, `begin_change_set`, `describe_change_set` and
`submit_change_set` are aliases for the group operations and results include
both `group_id` and `change_set_id`. The older unstage, discard and list names
also operate on the integrated workspace. They no longer create private
in-memory change sets.

### Naming and rereading staged elements

`stage_operations` answers with `created_element_ids`, for example
`"$topGoal" -> "G3"`. The identity is allocated once, persisted and reused on
every materialization. Later calls can refer to `G3`, and ordinary read tools
can search or fetch it because connected reads use the complete working draft.

### One workspace per argument

Each argument file has one persisted workspace, keyed by its project-relative
path. Switching arguments switches both the accepted baseline and its draft;
ids such as `G1` that legitimately repeat in another argument cannot receive the
wrong file's operations. Offline mode never reads `.af/drafts`.

### Watching the combination

The canvas, navigator, inspectors, search, MCP and SCCG review all consume the
same materialized model. Additions, edits and removals are marked in place, with
removed elements retained as presentation-only tombstones. When several groups
touch one element, the inspector shows the ordered contribution history rather
than choosing one proposal to preview.

## SCCG

Three mechanisms, weakest first.

**Resources.** `sccg://guidelines` publishes the catalog, so a client can load
the rules at the start of a session.

**Prompts.** `draft_argument_from_standard`, `add_argumentation` and
`restructure_case` each carry the guidance for that job, quoted from the catalog
so prompt and guideline cannot drift. This is what makes an agent aware of the
rules *before* it writes.

**Checks on staged work.** Returned in the result of every staging call so the
agent self-corrects, and shown on the integrated draft for the reviewer.

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

Connected clients are named on their draft groups. Something reading or
contributing to a safety argument should never be invisible to the person
responsible for it.

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
- **Concurrent clients share one draft.** They see each other's submitted and
  building groups; optimistic revision checks replace private client sandboxes.
- **Draft recovery is local to the project checkout.** `.af/.gitignore` keeps it
  out of version control, so unaccepted work is not shared through tracked
  project files and requires an explicit future export to move elsewhere.
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
- `tests/test_agent_request_handler.cpp` — connected reads use the integrated
  working model; MCP groups persist, support multi-call editing, and refuse a
  stale revision after another contributor changes the draft.
- `tests/test_draft_workspace.cpp` — staging changes no accepted SACM; stable ids,
  restart recovery, combined materialization and human promotion are verified.
- `tests/test_sccg_staged_checks.cpp` — one test per mechanical rule.
- `tests/test_mcp_modes.cpp` — offline is read-only, and every published tool
  leaves the project directory byte-identical.
- `tests/test_mcp_server.cpp` — every draft tool is consent-gated and the whole
  registry contains no apply, accept or promote operation.
- `cmake/run_mcp_smoke_test.cmake` — the consent gate through the real process.

Both platform branches of the transport are compiled: MSVC and MinGW g++ for
Windows, GCC under WSL for POSIX. Each toolchain proves which branch it took —
one resolves `<windows.h>`, the other `<sys/socket.h>`.
