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
| When | Assurance Forge has the project open | No application reachable |
| Reads | The complete integrated working draft the user is looking at, including MCP, SCCG and human draft groups | Accepted SACM from a copy the adapter loaded |
| Changes | Revision-checked groups in the application's persisted draft workspace | **Refused** |

Offline is read-only because the running application is the sole owner of the
workspace, presents every unaccepted change to the user, and serializes edits
from all contributors through one revision. A headless copy cannot safely do
that. The refusal says so and says what to do.

### Dynamic sessions and explicit offline mode

Launched with **no project argument**, the adapter is a *dynamic session*
([ADR 0014](../architecture/decisions/0014-projectless-mcp-discovery-with-runtime-case-binding.md)):
it initializes even when no application is running, discovers the running
instance at call time, and connects **unbound**. An unbound session can report
status — including the coarse fact that a project is open, never which one —
and receives no project content until the user grants it access (see Consent
below); the master `mcp.enabled` flag alone never discloses whatever project
the user has open. With more than one instance running, a dynamic session
refuses to choose — picking the newest would be picking a safety case by
timestamp.

`--offline-project <path>` is the offline mode above chosen deliberately:
accepted SACM from the named path, read-only, and it never connects — even
while an application with that project open is right there. `--project`
remains the project-bound mode described in the rest of this page.

The mode is re-evaluated on every call. MCP clients launch this process when
the *client* starts, so the application being absent, appearing later, or
restarting are ordinary events in a session's life, not faults: an offline
session promotes itself when the application appears, and a lost connection
reconnects on the next call — the user never has to restart their AI client.
An earlier design fixed the mode at connection time, reasoning that changing a
session's capability mid-conversation is worse than asking for a reconnect;
the permanently dead session turned out to be the worse trap. The change is
announced rather than quiet: every case-content result names its `view`, the
connection errors say the session heals by itself, `initialize` states the
mode in its `instructions`, and `get_connection_status` reports it on demand
(without returning case content, so it also works before consent is granted).
An interrupted read is retried once after a successful reconnect; an
interrupted change is reported but **never replayed** — the application may
have applied it before the connection broke, and a silent duplicate is exactly
what this surface exists to prevent. The session id survives reconnects, so
draft-group ownership persists across an application restart.

## Reading

`get_case_overview`, `find_elements`, `get_element`, `get_argument_tree`,
`list_case_files`, `open_case_file`, `list_terms`.

`suggest_placement(topic)` answers a question substring search cannot: *where
does new argument about this belong?* It returns ranked goals and strategies
with the path from the top goal, the sub-claims already there and the context in
scope. The agent still decides; without it, an agent guesses and attaches at the
root.

`list_assurance_claim_points` returns the case's GSN assurance claim points —
what each one annotates and how it is resolved, with the confidence argument's
claim, package and top-goal ids as links `get_element` can follow. An ACP marks
where the argument's own assurance is questioned, which is exactly what an
agent should read before proposing changes nearby; `get_element` also carries
the ACPs on the element and on the relationships touching it. Read-only: the
patch vocabulary has no ACP operation, and adding one is an ADR 0009 decision
with its own GSN review, so ACP authoring stays in the application.

`list_terms` returns the case's terminology in term-domain names — each term's
`value` (the word or phrase being defined), `name`, `definition`, `categories`,
`external_reference` and `origin`, plus the `categories` the case defines and
the ids a term may be filed under — in one call. Terms are ordinary elements to the other read tools too:
`get_case_overview` counts them under `term`, `find_elements` matches them by
type or text, and `get_element` fetches one by id. What `list_terms` adds is
the definition in the listing, which `find_elements` summaries omit, so an
agent checking what a case's terms mean does not need one `get_element` round
trip per term.

Hard limits on `find_elements` and `get_argument_tree` are a correctness
requirement, not a nicety — an unbounded call on a real case spends the whole
conversation.

Every case-content response identifies what it returned. Connected reads name
`view: working_draft` or `view: accepted`, plus `argument_file`, the current
`working_revision`, and the session's `context_generation`; an active
workspace also supplies `workspace_id`. Offline reads name `view: accepted`.
A client never has to infer whether text has been accepted.

Every draft mutation names both values it read: `expected_working_revision`
(the draft moved) and `expected_context_generation` (the ground under the
whole session moved — a project switch, a fresh grant, a revocation). Either
being stale refuses the mutation before anything is stored, so a change
computed against one context can never land in another (ADR 0014).

## Changing: the working draft

The working draft is **a SACM document** — a copy of the accepted argument that
contributors edit directly (ADR 0016). An agent's operations are applied to that
document through the same library seams the application uses on the accepted
one, so a change the model cannot hold is refused in the call that made it,
naming the operation by its position in the batch. There is no staging model to
accept something the accept would later have to refuse.

An agent still contributes one coherent **change group**, which is now the
record of who contributed what rather than a container of pending operations.

1. Read the case or call `get_draft_status` and retain `working_revision`.
2. `begin_change_group` with a title, rationale and that expected revision.
3. `check_operations` rehearses operations on a copy of the draft first — the
   same validation and findings staging would return, with nothing stored, no
   ids allocated and nothing drawn on the user's canvas, so an agent iterates
   privately instead of flickering half-finished work in front of the user. It
   also works offline, against the accepted case.
4. `stage_operations` in small steps. Each call applies its batch to the draft
   document and returns the ids the document allocated, what the draft now
   changes about the accepted argument, findings against it, and the next
   revision.
5. `describe_change_group` and `describe_working_draft` inspect the contribution
   and what the draft as a whole now changes.
6. `get_draft_events(after_revision)` reports what other contributors changed.
7. `submit_change_group` marks the work ready for the user — and is **refused
   while problem-severity findings stand against the group**, each one named in
   the refusal. Staging deliberately never refuses on findings, because every
   intermediate shape is legitimately unfinished; submit is the author
   declaring itself done. `acknowledge_findings: true` submits anyway and
   records the acknowledged findings on the group, where the reviewer sees
   them; a later restage clears the record, since it described a superseded
   shape. Advisory findings never refuse anything. `close_change_group` ends
   the group without deleting its provenance record.

A batch is **atomic**: if any operation is refused, none of them is applied and
the draft is exactly as it was. Ids returned by `stage_operations` are real and
addressable immediately — the document allocated them when it created the
element, and nothing later reallocates them.

`replace_change_group`, `remove_change_group` and `unstage_operations` are
**refused** against a document-backed draft, and say so. They withdraw
operations from a log; the draft holds a document. Reporting them as done while
the change was still in the draft would be a success message over an edit that
never went away. To reverse a change, edit the draft back — set the text to what
it was, or remove the element that was added — or ask the user to discard the
whole draft.

Accept is all-or-nothing and belongs to the user alone. It writes the draft over
the accepted argument as one atomic replacement, so it cannot half-apply.

Every modifying call carries `expected_working_revision`. The revision tracks
the draft document as well as the change-group record, so a user editing the
argument moves it — their edits go into the same draft. If a human edit, SCCG
review or another MCP client changed the shared draft, the call is refused with
`current_working_revision`; the agent rereads and decides whether its intended
operation still means the same thing. This is optimistic concurrency over the
semantic graph rather than silent last-writer-wins.

An MCP session may inspect the whole working draft but may mutate only groups it
created. This prevents one client from rewriting human, SCCG, or another
client's work while still letting all of them reason about the same argument.

### Terminology operations

Glossary work goes through the same change groups as argument edits:
`CreateTerm` defines a term (`text` is the term itself, `new_value` its
definition), `UpdateTerm` revises one field of an existing term, and
`RemoveTerm` deletes one. `CreateCategory` and `UpdateCategory` manage the
categories terms are classified under (`text` is the category name, `new_value`
its description). A staged term is visible to `list_terms` and the other reads
in the working-draft view, revision-checked like every draft mutation, and
lands in the SACM document only when a human promotes the group.

`UpdateTerm` takes a `field` of `value`, `definition`, `name`, `category`
(one or more category ids, space separated; empty clears them),
`external_reference` (a citation string — a URL, a standard clause, a document)
or `origin` (the id of the element the definition comes from). The last three
are what answer the terminology check: it reports a term with no category, and
a term with neither an external reference nor an origin, and before those
fields were addressable an agent could read both findings and fix neither. A
category id that does not resolve, or names something that is not a category,
is refused at staging rather than at acceptance; an `origin` that looks like a
citation string is refused with a pointer to `external_reference`.

Each field is written by its own seam, so classifying a term cannot disturb its
definition — routing the whole term through the library's replace-everything
update would rewrite the definition in one language and drop its translations.

A staged glossary is reviewable on its Draft Changes row, which lists each
term with its definition, categories and source in full — a term is
deliberately not a GSN node, so unlike an argument change there is no canvas
rendering beside the row to read it from. Clicking such a row therefore does
not jump to the canvas: it opens the terminology view for a term already in the
accepted glossary, and stays on the row for one this draft created, which that
view cannot show until it is promoted.

When the case has no `terminologyPackage`, accepting the first `CreateTerm`
creates one under the root assurance case package rather than refusing — a case
with no glossary could otherwise never grow one over MCP. Definitions may carry
`translations`; a term's value is a single string (SACM 10.11) and cannot,
which staging enforces with an explanation rather than at acceptance.

Terms address SCCG CL.5 directly: a claim relying on a broad evaluative term
(*safe*, *timely*, *all*) is answerable by defining the term once instead of
repeating a bound in every claim. A term defined at case level is in scope for
term detection across the whole case.

`RemoveTerm` of a term that an argument package references (for example as a
visible term context) is refused at acceptance by the SACM library's
cross-package delete guard; removing such a term remains an in-application
action where the cascade can be shown and confirmed.

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

Three mechanisms, weakest first — then the doctrine, which exists because all
three land only when the user or client asks. The plan for the rest of this
surface is [MCP authoring quality](mcp-authoring-quality-plan.md).

**Resources.** `sccg://guidelines` publishes the catalog, so a client can load
the rules at the start of a session.

**Prompts.** `draft_argument_from_standard`, `add_argumentation` and
`restructure_case` each carry the guidance for that job, quoted from the catalog
so prompt and guideline cannot drift. This is what makes an agent aware of the
rules *before* it writes.

**Checks on staged work.** Returned in the result of every staging call so the
agent self-corrects, and shown on the integrated draft for the reviewer.

**The authoring doctrine** (AF-AI-023). A ~15-line condensation — one rule per
line, each naming the guideline it condenses, every SCCG family represented —
delivered through the channels that reach the model even when nobody asked:
`initialize.instructions`, an `authoring_guidance` field on the pre-write reads
(`get_case_overview`, `get_argument_tree`, `suggest_placement`,
`get_draft_status` — the looped reads stay lean deliberately), and the
operation schema's `text` description, which is in context at the moment a
claim's words are generated. Every id the doctrine names is resolved against
the catalog by a test, so the condensation cannot outlive the rules it
condenses.

!!! warning "What the checks do and do not cover"
    Most of SCCG is prose only a reader can judge — whether a claim is
    *sufficiently* justified, whether evidence is *relevant*. Those are not
    decidable here, and claiming otherwise would make a green result read as
    conformance.

    The mechanical set is the twelve catalog-bound checks AF-AI-024 records:
    the structural four (**EV.1** a claim with no support and no undeveloped
    marker; **AR.2** a decomposition whose sub-claims carry no reasoning step;
    **AR.1** a solution with children, a strategy that develops into nothing,
    and support cycles) and the lexical eight (**CL.5**
    unbounded qualifiers, **CL.2** bundled properties, **CL.6** chained
    lifecycle steps, **RD.1** reasoning inside claim text, **RD.4** promotional
    language, **EV.7** uncontrolled evidence references, **EV.8** mutable
    sources cited unfixed, **LF.3** absence offered as support). Every lexical
    check is proven against the guideline's own bad and good examples, read
    from the catalog. Lexical findings are advisory. Problem-severity findings
    gate one thing only: an agent's `submit_change_group`, which refuses until
    they are fixed or explicitly acknowledged (AF-AI-025). Nothing here ever
    blocks *acceptance* — the reviewer is the authority on a safety argument.

## Consent

Two gates ([ADR 0007](../architecture/decisions/0007-mcp-server-consent.md) and
[ADR 0014](../architecture/decisions/0014-projectless-mcp-discovery-with-runtime-case-binding.md)).

**Gate 1 — the master flag.** `mcp.enabled` in the settings file, off by
default, toggled in Preferences alongside a copyable client configuration. It
fails closed on every failure path — missing file, malformed document, absent
or non-boolean flag — and is re-read on every call, so revoking it takes
effect immediately rather than when the client happens to restart. Disabling
it also revokes every session grant below.

**Gate 2 — the per-session grant.** The master flag alone discloses nothing.
Each session's first project operation (or an explicit
`request_project_access` call) raises an access request inside the running
application — client label, project name, *Allow while open* / *Deny* — and
the operation is refused with `project_access_pending` until the user answers.
Grants are keyed by session id and project, live only in application memory,
survive a bridge reconnect to the same instance, and end on deny, revoke,
project close or switch, MCP disable, or application restart. A re-granted
session still owns the draft groups it authored earlier — they are keyed by
the same session id. The client label is attribution, never authentication;
the token and the pipe's ACL remain the security boundary.

Connected clients are named on their draft groups and shown with their access
state. Something reading or contributing to a safety argument should never be
invisible to the person responsible for it.

## Discovery

The application publishes **one instance record per running instance**
([ADR 0014](../architecture/decisions/0014-projectless-mcp-discovery-with-runtime-case-binding.md)),
keyed by a runtime instance id, under the user's runtime directory. The
listener outlives the open project: opening, closing or switching projects
updates the record's project fingerprint but never recreates the listener or
rotates its token. Liveness is the recorded pid — a record whose process is
gone is ignored and pruned by whichever side meets it first.

The record carries a *fingerprint* of the open project root, never the path:
discovery metadata must not disclose where a user keeps their safety cases.
The project-bound adapter matches that fingerprint to find its application; a
session whose project is not the active one is refused with
`project_not_active` — a precise, recoverable refusal, not content from
whatever the user switched to. Service resumes on the same connection when the
project is reopened.

Two instances that open the same project trip an **advisory warning** in the
second instance: two writers on one `.af/drafts` is a real corruption risk,
but a crashed instance must never lock a user out of their own safety case.

## Security of the bridge

Two gates. The operating system's access control on the pipe or socket (an
explicit user-only DACL on Windows for both the pipe and the record directory;
mode 0600 in a user-private directory on POSIX), and a 256-bit token published
in the instance record. A local process that did not read that file is not the
adapter this application published for.

The instance record is deliberately **not** in the project: a pipe name, a pid
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
- **Terminology stops short of deletion and association.** Terms and categories
  can be created and edited, including a term's categories, external reference
  and origin. Removing a *category*, and associating a term with an element as
  a visible context, stay in the application: a category deletion has cascade
  semantics that need a confirmation the MCP surface cannot raise. Created
  terms and categories land in the case's first terminology package (created on
  demand) — choosing among several packages is not expressible.

## Verification

- `tests/test_bridge_protocol.cpp`, `tests/test_bridge_transport.cpp`,
  `tests/test_instance_registry.cpp` — wire contract, instance records and
  discovery, stale-record pruning, transport, including a real loopback round
  trip and the shutdown path.
- `tests/test_agent_bridge_controller.cpp` — the online path end to end over a
  real pipe: handshake, token refusal, protocol mismatch, frame-thread
  execution, the listener surviving a project switch, and the
  `project_not_active` refusal.
- `tests/test_agent_request_handler.cpp` — connected reads use the integrated
  working model; MCP groups persist, support multi-call editing, and refuse a
  stale revision after another contributor changes the draft; terms stage
  through change groups and read back through `list_terms` in the working-draft
  view.
- `tests/test_change_set_acceptance.cpp` — accepting a staged `CreateTerm`
  creates the terminology package when the case has none, matches the
  acceptance preflight by semantic hash, and term edits and removals land in
  the library document.
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
