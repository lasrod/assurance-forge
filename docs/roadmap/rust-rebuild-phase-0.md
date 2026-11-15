# Rust rebuild: Phase 0 decisions

**Authority: planning record.** This page records what was decided in Phase 0 of
rebuilding Assurance Forge in Rust, and what remains before implementation
starts. It describes the tool that is planned. What is built so far is stated in
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md).

## What is changing

Assurance Forge will be rebuilt in Rust as a desktop application with a web
technology user interface. It is a rebuild from the ideas, not a line-by-line
conversion of the C++ code.

- **Core:** Rust, on top of [libSACM](https://github.com/lasrod/libSACM), the
  Rust SACM 2.3 library.
- **User interface:** React with the DATUM design system, packaged as a desktop
  application with Tauri.
- **Repository:** this one, at the same URL. `main` holds the Rust workspace.
  The C++ application is on the
  [`cpp` branch](https://github.com/lasrod/assurance-forge/tree/cpp), and its
  last release is
  [0.3.0-alpha.3](https://github.com/lasrod/assurance-forge/releases/tag/0.3.0-alpha.3).
- **Licence:** MIT, unchanged.

The product principle does not change: users work in GSN, SACM stays underneath,
and the tool is kept simple without preventing anyone from building an argument
the way the standards intend.

## Scope of the tool

Assurance Forge stays a fully local desktop tool. It is a document editor for
assurance cases:

- All GSN and SACM notation: core elements, pattern notation, modules,
  assurance claim points and challenges.
- SACM 2.3 XMI import, export and round-trip.
- Problem checks, GSN well-formedness validation and the SCCG guidelines.
- Guided top-down and bottom-up development methods.
- Evidence and CSE registers, and per-case terminology.
- AI review with the user's own key, including consensus review.
- An MCP server for external AI clients.
- SVG diagram export.

How it works with files:

- **One case is one SACM file.** There is no project, no manifest and no file
  hashing. The tool opens SACM XMI files.
- **Explicit save**, with a recovery copy against crashes.
- **Working state lives in a folder beside the file** (for example
  `brake-system.sacm.af/`): the AI working draft, review items, translation
  review state and the recovery copy. Deleting it loses working state, never
  argument content.

## Lessons from the C++ architecture

A review of the [16 architecture decision records](https://github.com/lasrod/assurance-forge/tree/cpp/docs/architecture/decisions) of the C++
application gave these verdicts.

| ADR | Verdict for the rebuild |
|---|---|
| 0001 Record decisions | Keep; new format below |
| 0002 Layers with build-time gates | Replaced by crate boundaries |
| 0003 SACM XML is the source of truth | Keep, and strengthen: one model only |
| 0004 MkDocs Material site | Keep for the public site |
| 0005 Provider-agnostic AI, explicit consent | Keep |
| 0006 SACM as an independent library | Done: libSACM |
| 0007 MCP behind explicit consent | Keep |
| 0008 The running application owns the open project | Keep, as one owner per open document |
| 0009, 0010 Integrated draft, human-controlled promotion | Keep the principles; storage simplifies |
| 0011 Panels own their view types | Enforced by the frontend boundary |
| 0012 A claim carries one Description | Keep; a libSACM concern |
| 0013 Review methods independent of providers | Keep |
| 0014 Projectless MCP with runtime binding | Rework for a file-based tool |
| 0015 Versioned provider profiles, fail closed | Keep; build in from the start |
| 0016 The draft is a SACM document | Keep |

Four lessons shaped the new design:

1. **Nearly every serious defect came from a second model** kept beside the SACM
   document: the flat parser model, projections, a second process holding its
   own copy.
2. **The frame loop made ordering fragile.** Dirty flags and rebuild-next-frame
   meant the order of steps inside a frame carried meaning.
3. **Project storage grew heavy**: a manifest, four hashes per file, ten file
   roles.
4. **Decisions were hard to read.** Several ADRs are superseded in part by later
   ones, so one rule is spread over five documents, and plan pages grew to
   thousands of lines.

## Target architecture

### One model

The libSACM `Document` is the only model. GSN is a way of looking at it and a
way of asking for edits; it holds no state of its own.

- Views (diagram, tree, registers, terminology, problems) are derived by pure
  functions and never written back.
- A GSN command expands into one or more libSACM operations, applied all or
  nothing.
- The interface offers only edits the library will accept, by asking the core
  what is allowed at a selection.
- Anything SACM cannot express lives outside the document.

### Revisions and the change stream

Every open document lives in a session that numbers its revisions. Each accepted
command produces one change record: the revision, the command, the libSACM
mutations and the origin (user, AI review or MCP session).

- View derivation, problem checks, the recovery copy and the frontend subscribe
  to this stream. It replaces dirty flags and frame ordering.
- Undo starts as document snapshots behind a small interface, until libSACM
  settles its own mechanism.
- A working draft is a second session with its own SACM document. Accepting it
  is one ordinary, undoable change.
- The tool keeps the stream in memory. It is a public interface of the core, so
  anything built on the core can subscribe to it.

### Crates

| Crate | Owns |
|---|---|
| `sacm` | The SACM model (libSACM, external) |
| `af-gsn` | GSN view of a document, GSN commands, identifiers, well-formedness checks |
| `af-layout` | Diagram layout, positioned render model, SVG export |
| `af-session` | Open document, revisions, change stream, undo, drafts, storage trait |
| `af-problems` | The problem list and its attention model |
| `af-sccg` | Guideline catalogue and mechanical checks |
| `af-registers` | Evidence and CSE registers |
| `af-terminology` | Terminology assist and term usage |
| `af-review` | Review methods: scope, prompt and result contracts, result validation |
| `af-ai` | Provider profiles and inference; knows nothing about SACM or GSN |
| `af-agent` | Operations an external agent may request |
| `af-api` | The frontend contract: commands, queries, views |
| `af-desktop` | Tauri shell: files, dialogs, secret storage, HTTP, local bridge |
| `af-mcp` | MCP server executable |
| `af-wasm` | Browser wrapper around `af-api` |
| `ui/` | React and DATUM frontend |

Rules, each checked in CI:

- Every crate down to `af-api` is pure: no file system, network, threads or
  clock. Those arrive through traits the shells implement. These crates are
  built for WebAssembly on every pull request.
- `af-review` and `af-ai` never depend on each other.
- Only `af-desktop`, `af-mcp` and `af-wasm` touch the outside world.
- A crate exists when it can be built and tested without the rest. `af-gsn` is
  kept clean enough to publish separately later.

### Frontend contract

Commands go in, views come out. The frontend never holds the document.

- **Command:** a requested change, answered with the new revision or a refusal
  code.
- **Query:** a view at the current revision.
- **Event:** pushed when the revision changes or a long task progresses.
- The interface, AI review and MCP agents all use the same commands.
- Rust is the source of the types; TypeScript definitions are generated and
  checked for staleness.
- The core sends codes and arguments, never sentences. The frontend translates.
- Selection, pan, zoom and open panels stay in the frontend.
- Layout runs in Rust and the frontend only draws, so the canvas and the SVG
  export share one geometry.

## How the work is done

- **Skills carry project knowledge**, and an agent has authority over the whole
  change. The guard rails are the compiler, crate boundaries and CI checks.
- **Independent verification is reserved** for changes where a mistake silently
  damages an argument: GSN to SACM mapping, edit commands, reading and saving.
- **User interface changes** go through reviewed pull requests and a check in
  the running application.
- **Fast feedback is a requirement.** A one-line change is checked locally in
  seconds and in CI in minutes:
    - CI tests only the crates a change affects; frontend and Rust are checked
      separately.
    - Three test tiers: unit tests on every change, integration tests when a
      crate or its dependants change, slow suites on merge or nightly.
    - A failing nightly run opens an issue.
    - Tests share no state on disk.

## How the architecture is documented

- One short, living
  [`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md):
  crate map, rules, data flow and two diagrams. Each rule names the check that
  enforces it.
- ADRs record why. They are amended in place and never superseded in part. The
  Rust tool starts a new series.
- Each crate's own documentation says what it owns and what it must not do.
- The capability matrix becomes TOML with generated output, and keeps the rule
  that `supported` needs a cited test.
- No long-lived plan pages. A finished plan is folded into `ARCHITECTURE.md` or
  an ADR and closed.
- Repository checks move into an `xtask` crate.

## Requests raised on libSACM

| Issue | Request |
|---|---|
| [libSACM#57](https://github.com/lasrod/libSACM/issues/57) | serde for operations, mutations, value types and diagnostics |
| [libSACM#58](https://github.com/lasrod/libSACM/issues/58) | CI check that the crate builds for WebAssembly |
| [libSACM#59](https://github.com/lasrod/libSACM/issues/59) | Several operations as one atomic change |
| [libSACM#60](https://github.com/lasrod/libSACM/issues/60) | Check an operation without applying it; public containment query |
| [libSACM#61](https://github.com/lasrod/libSACM/issues/61) | What the application needs from undo and redo |

None blocks a start; the application can work around each until the library
provides it.

## Releases

The capabilities of the C++ application
([capability matrix](../reference/cpp-capability-matrix.md)) arrive in five
milestones. Each gives a tool that can be used, and each waits only on libSACM
work that exists by then.

| Milestone | What a user can do | Needs from libSACM |
|---|---|---|
| **1. View** | Open a SACM file, see the GSN diagram with automatic layout, browse the tree and packages, read element details, see well-formedness and cycle problems, export SVG. English and Japanese, dark and light. | What exists now |
| **2. Edit** | Create, change and delete elements and relationships, with menus that offer only valid edits. Undo, explicit save, recovery copy. Assurance claim points, challenges, defect repair, dual-language content. | Delete operations |
| **3. Registers and guidance** | Evidence and CSE registers, per-case terminology with term detection, the SCCG guideline catalogue, the full problem list. | Artifact and Terminology models |
| **4. AI review** | Provider profiles with keys in the system secret store, SCCG review, suggested changes in a working draft, accept and reject, consensus review. | Serialisable operations helps |
| **5. External agents** | The MCP server: read a case, propose changes into the draft, SCCG guidance and checks, consent and session binding to the open file. | Nothing new |

Some capabilities change shape instead of being ported:

| In the C++ application | In the Rust tool |
|---|---|
| Project model, project explorer, creating a project from a SACM file | Opening and saving a single file |
| Recent projects | Recent files |
| Crash-resilient project recovery | The recovery copy |
| Layered architecture enforced at build time | The crate rules in `ARCHITECTURE.md` |
| Projectless MCP sessions | A session bound to an open file |
| Windows installer | Packages from the Tauri bundler, from milestone 1 |

GSN v3 features that are planned and not yet in the C++ application (pattern
decorators and choice, modules and contracts, defeat states, the guided
development methods) follow after milestone 5. The layout and render model
allows for them from milestone 1.

## What remains in Phase 0

- [ ] **Spike: desktop shell.** Tauri and DATUM, opening a SACM file through
      libSACM and showing the tree.
- [ ] **Spike: GSN canvas.** The largest real case, with pan, zoom and editing.
      Text measurement in Rust, including Japanese, is the first thing to test.
- [ ] **Spike: one core, two targets.** The same crate running natively and as
      WebAssembly.
- [x] **Release plan.** The five milestones above.
- [ ] **Remaining decisions:** draft storage and acceptance, MCP consent and
      session binding for a file-based tool, and the shared layout model.
- [x] **Repository setup:** `main` cleared for the Rust workspace, with CI that
      checks only what a change touches.
- [ ] **Milestone plan** for milestone 1.

Phase 0 ends when the spikes have run with their findings written down and the
first decision records of the new series are accepted.
