# Milestone 1, step 1: the diagram

**Authority: plan.** This page says what the first piece of the application is,
how each part of it is checked, and in what order it is built. It is short-lived:
when the last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and the mapping record, and this page is removed.

## The outcome

Open a SACM 2.3 file in the desktop application or the web application and see
its argument as a GSN diagram in the middle of a DATUM workspace that has left, right and bottom
panels. Pan, zoom, fit and select. The selected element's details show in the
right panel. Nothing is edited.

It is the first step of milestone 1 (View) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases), and it also runs the
open Phase 0 spike "one core, two targets".

### Done when

1. `testdata/sacm/sacm_example_core_argument.sacm.xmi` opens in the desktop
   application and in the browser build, and draws the view in
   [the expected view](#the-expected-view) below. The other four samples open
   too; the three with a terminology package open by a tolerant read and say
   what was not read (D8).
2. Every interaction under [Interaction](#interaction) works, each with an
   automated check in the browser build.
3. `testdata/sacm/safety-case-sanitized-strict.sacm.xmi` (1,090 elements, the
   largest file libSACM reads today) opens and pans and zooms within the
   [budgets](#budgets).
4. A file libSACM refuses shows the refusal code with its line and column, and
   no diagram.
5. CI is green, and `ARCHITECTURE.md` says what is built.

### Out of scope

Editing, undo and save. The tree in the left panel and the problem list in the
bottom panel: the panels are there, titled and empty. SVG export. Modules,
patterns, assurance claim points and challenges. Those come in the next steps of
milestone 1 and in milestone 2.

## The pieces

| Piece | Crate or folder | New in this step |
|---|---|---|
| The SACM model | `sacm` 0.1 from crates.io | dependency |
| GSN view of a document | `af-gsn` | the view and its reading rules |
| Text measurement, layout, render model | `af-layout` | crate |
| The frontend contract | `af-api` | crate, holding the open document |
| Browser wrapper | `af-wasm` | crate |
| Desktop shell | `af-desktop` | crate |
| Interface | `ui/` | React, DATUM, the canvas |

`af-session` is not added yet. With nothing edited there is one revision, and
`af-api` holds the document. The session crate arrives with the first edit, when
it has a change stream to own.

## Decisions this plan takes

Each is a proposal until the tracking issue, [#493](https://github.com/lasrod/assurance-forge/issues/493), records agreement.

| # | Decision | Why |
|---|---|---|
| D1 | **Desktop and web from the first step, and the web build is the test target.** Both are products (`ARCHITECTURE.md`, goal 5). The same `ui/` runs on `af-desktop` in Tauri and on `af-wasm` in a browser, behind one `Core` interface with two implementations. Automated interface checks drive the web build with Playwright in Chromium, Firefox and WebKit. The desktop application gets a smoke check. | Driving a desktop window is slow and fragile; the C++ application's GUI driver cost hours to locked sessions and stolen focus. WebKit in Playwright is also the nearest automated stand-in for Tauri on macOS and Linux, which the canvas spike could not test. |
| D2 | **Reading rules are written down before they are coded.** How each SACM element and relationship is drawn in GSN is a new section of `docs/standards/sacm-gsn-mapping.md`, with a basis for each row. A case without evidence is drawn with a neutral shape and a code, never guessed. | The mapping table says how GSN is stored in SACM. A SACM 2.3 file needs the reverse, and the reverse has ambiguous cases (see below). |
| D3 | **The view draws what the file says.** If the file has an inference from an assumption, the diagram shows a SupportedBy arrow to an Assumption. Well-formedness checks report it later; the view never rearranges an argument to make it well formed. | Never silently reinterpret an argument. |
| D4 | **Every argument element appears in the view exactly once**: as a node, as part of an edge, or as a coded item the view could not place. A test checks this for every sample file. | Nothing is dropped without a trace. |
| D5 | **The workspace is composed from DATUM's `Panel`, `Splitter` and `Rail`**, following the contract's rules for docks, until DATUM decides on a shared component ([datum-ui#11](https://github.com/lasrod/datum-ui/issues/11)). The composition says which issue replaces it. DATUM comes from the tarballs of `datum-ui`'s `release:pack`, as in the shell spike, from a release that has `Tree` and the fonts ([datum-ui#12](https://github.com/lasrod/datum-ui/issues/12)). | DATUM's contract makes a product's docking its own; Simulation Studio wrote the same dock, so a shared one is proposed rather than assumed. |
| D6 | **One font for the diagram**, given to `af-layout` as bytes by the shell and loaded once by the page. Shape with `rustybuzz`, break with `unicode-linebreak`, kern Latin text and not kana. The whole OpenType files from `@datum-ui/fonts` ([datum-ui#9](https://github.com/lasrod/datum-ui/issues/9)). Its woff2 pieces cannot be used: the shaper does not read woff2, and a face split by Unicode range loses kerning between pieces. | The canvas spike's findings. Not embedding the font in the WebAssembly package halves its size. |
| D7 | **A simple layout first.** A layered tree: SupportedBy children below their parent, InContextOf attachments beside it, siblings in document order, straight edges. The C++ `src/ui/gsn/gsn_layout.cpp` is the reference for spacing and placement, not code to port. | A diagram that can be read beats a clever one that is late. Crossing reduction comes when real cases need it. |
| D8 | **Files open by a strict read, and by a tolerant read when the strict one refuses only content that libSACM keeps** (SACM-XMI-014). The interface says the file opened with parts not read, and which. Nothing is saved in this step, so nothing kept is at risk. | All five samples are readable tolerantly in `sacm` 0.1.0, which keeps the terminology package as preserved content and writes it back. |

### Reading rules to record (D2)

From the mapping table, the GSN skill and the C++ reader (`classify_role` in
`src/core/assurance_tree.cpp` on the `cpp` branch). Rows marked *open* need a
decision before they are coded.

| SACM | Drawn as | Basis |
|---|---|---|
| `sacm.import.extensionType` tagged value present | The recorded GSN type | Mapping record, "Recording the original GSN type" |
| `Claim`, declaration `asserted` or `needsSupport` | Goal | Mapping table |
| `Claim`, declaration `assumed` | Assumption | Mapping table |
| `Claim`, declaration `axiomatic` | Justification | Mapping table. The C++ reader tested for `justification`, which is not a SACM literal; do not carry that over. |
| `Claim`, declaration `needsSupport` | The undeveloped decorator, on a Goal | Mapping record, "`undeveloped` occupies the declaration" |
| `ArgumentReasoning` | Strategy, between the inference's target and its sources | Mapping table, "Strategy is a node in GSN" |
| `ArtifactReference` reached by `AssertedEvidence` | Solution | Mapping table; C++ reader |
| `ArtifactReference` reached by `AssertedContext` | Context | C++ reader; v2.2 §2.1 |
| `AssertedInference`, `AssertedEvidence` | SupportedBy, endpoints swapped | Mapping table |
| `AssertedContext` | InContextOf, endpoints swapped | Mapping table |
| `assuranceForge.gsn.identifier` tagged value | The node's identifier label | C++ capability AF-GSN-015 |
| *open:* `Claim` declared `asserted` and reached only by `AssertedContext` | Context (the C++ reader) or Goal | v2.2 allows a Claim as Context, "determined by the nature of its content" |
| *open:* `Claim` declared `axiomatic` and reached by `AssertedContext` | Justification or Context | Both readings have a basis |
| *open:* `ArtifactReference` reached by both, or by neither | Neutral shape and a code, until decided | none yet |
| *open:* which text a node shows | Statement (first description) in the interface language, else the first language, else the name | To confirm against the C++ application |

### The expected view

Written by hand from `sacm_example_core_argument.sacm.xmi`, and checked by a
second reader before it becomes the test's expected value.

| Nodes (8) | Edges (7) |
|---|---|
| Goal `cl_top` | SupportedBy `cl_top` → `ar_1` (via `inf_1`) |
| Strategy `ar_1` | SupportedBy `ar_1` → `cl_sub_1` (via `inf_1`) |
| Goal `cl_sub_1` | SupportedBy `ar_1` → `cl_sub_2` (via `inf_1`) |
| Goal `cl_sub_2`, undeveloped | SupportedBy `cl_sub_1` → `cl_assumption_1` (via `inf_2`) |
| Assumption `cl_assumption_1` | InContextOf `cl_top` → `ctx_1` (via `acx_1`) |
| Context `ctx_1` | SupportedBy `cl_sub_1` → `ev_1` (via `ae_1`) |
| Solution `ev_1` | SupportedBy `cl_sub_2` → `ev_2` (via `ae_2`) |
| Solution `ev_2` | |

Two of these are not well formed GSN: the SupportedBy to an Assumption, and an
undeveloped Goal that has a Solution. They are drawn as the file says (D3).

## Interaction

| Action | Input | What happens |
|---|---|---|
| Pan | Drag on empty canvas; middle-button drag; wheel and two-finger scroll, both axes | The view moves; nothing is selected |
| Zoom | Ctrl and wheel; pinch; `+` and `-` | Zooms around the pointer (keys: around the centre), 10% to 400%; the zoom shows in the status bar |
| Fit | Toolbar button; `Shift+1`; on open | The whole diagram fits the canvas |
| Select | Click a node | The node is outlined; the right panel shows its GSN kind, identifier, name, statement and SACM id |
| Clear | Click empty canvas; `Esc` | Nothing is selected; the right panel says so |
| Hover | Pointer over a node | The node is highlighted and the cursor changes |
| Resize | Drag a panel edge; collapse and reopen a panel | The canvas fills the centre and keeps its view centre |
| Theme | Switch dark and light | The canvas redraws from DATUM tokens |

Selection, pan and zoom stay in the frontend. Hit testing uses the render
model's geometry, the same geometry that is drawn. The canvas draws only what is
in view and is sharp at any device pixel ratio. Scrolling pans on both axes
because the C++ application could not scroll horizontally on a laptop (#145).

### Budgets

On the development machine, browser build, Chromium:

| Measure | 24-element sample | 1,090-element case |
|---|---:|---:|
| Open to first paint | 200 ms | 1.5 s |
| Native layout, release | 10 ms | 300 ms |
| Frame interval while panning, mean | display interval + 10% | display interval + 10% |
| Text width, Rust against `measureText`, largest error | 1.5 px | 1.5 px |

## The goals

Each goal has one check that decides whether it is done. Goals with † change how
an argument is read, and get the independent review described in
[the loop](#the-loop).

| Goal | Outcome | Check | Needs |
|---|---|---|---|
| **G1** † | Reading rules recorded in the mapping record; the open rows decided or left drawing a neutral shape | Independent review passes; the user agrees the open rows in #493 | |
| **G2** † | `af-gsn` derives a view from a `sacm::Document`: nodes with kind, labels and decorators; edges in GSN direction with their SACM relationship id; coded items for what it could not place | `cargo test -p af-gsn`: the expected view, and D4 for every sample libSACM reads. `cargo build -p af-gsn --target wasm32-unknown-unknown` | G1 |
| **G3** | `af-layout` measures and wraps text from font bytes, sizes nodes per kind, lays out the tree, and returns a render model: shapes, text runs, edge lines with arrowhead kind, decorators. Measurements are cached per text and width. | `cargo test -p af-layout`: output is deterministic; no two nodes overlap; edges end on their node's outline; children sit below their parent and context beside it; native layout within budget. Wasm build. | G2 |
| **G4** | `af-api`: `open(bytes)` answers with a revision and diagnostic counts, or a refusal code with line and column; `diagram()` returns the render model at the revision; `element(id)` returns the details panel's content. TypeScript types generated with `ts-rs`. | `cargo test -p af-api`, including a test that fails when the generated types are stale. Wasm build. | G3 |
| **G5** | The web application: `af-wasm` and `ui/` in a browser. Open a file by picker or drop, or a sample by URL in development. The canvas draws the render model. | `pnpm --dir ui check` (types, lint, unit tests) and `pnpm --dir ui e2e` against the production build: the sample draws 8 nodes and 7 edges | G4 |
| **G6** | The workspace: DATUM dock with left "Outline", right "Details", bottom "Problems", resizable and collapsible, titles in English and Japanese | DATUM: its own `pnpm check` with the dock component. Here: `e2e` collapses and reopens each panel and checks that the canvas fills the centre | D5 in `datum-ui` |
| **G7** | Drawing: GSN shapes, arrowheads (filled for SupportedBy, hollow for InContextOf), the undeveloped decorator, DATUM tokens in both themes, culling, device pixel ratio | `e2e`: screenshot comparison per theme; text width against `measureText` in Chromium, Firefox and WebKit within budget, for English and for the Japanese text of `oasc-ja.sacm.xmi` | G5 |
| **G8** | Every row of [Interaction](#interaction) | `e2e`: one test per row, reading viewport and selection through a hook built only into test builds | G6, G7 |
| **G9** | `af-desktop`: open with a dialog or from the command line; a refused file shows its code and location | CI builds the desktop application on Windows; a smoke test starts it with the sample and reads its log for the revision and node count | G5 |
| **G10** | The large case within budget | `e2e` performance test on the 1,090-element case | G8 |
| **G11** | Folded back: `ARCHITECTURE.md` "Built" column and crate map, CI builds every pure crate for wasm, this page removed, issue closed | Review of the final pull request | all |

G6 can start at once, beside G1 to G4: the dock needs no core.

### What waits on other repositories

| Issue | Needed by | Until it is done |
|---|---|---|
| [datum-ui#12](https://github.com/lasrod/datum-ui/issues/12) A release with `Tree` and the fonts | G5, G6 | Pack the tarballs from `datum-ui`'s main locally |
| [datum-ui#9](https://github.com/lasrod/datum-ui/issues/9) Whole font files | G3, G7 | Noto Sans JP from the spike, behind the same font-bytes input |
| [datum-ui#10](https://github.com/lasrod/datum-ui/issues/10) Diagram canvas: text, roles, navigation | G7, G8 | Existing tokens (`surface.canvas`, `surface.raised`, `line.control`, `selection.indicator`, `focus.ring`), named in one place |
| [datum-ui#11](https://github.com/lasrod/datum-ui/issues/11) Workspace docks | G6 | D5: composed in the application |
| [libSACM#82](https://github.com/lasrod/libSACM/issues/82) Codes and arguments on diagnostics and refusals | G4, G9 | The code and location are worded; the rest of libSACM's English message is shown as technical detail |
| [libSACM#66](https://github.com/lasrod/libSACM/issues/66) Older namespaces, terminology | after this step | D8: tolerant read |

None stops a goal from starting. Each stopgap names its issue in the code, and is
removed when the issue is done.

## The loop

An agent, or a person, runs the plan like this:

1. Take the first goal whose check does not pass and whose needs are done.
2. Make the change. Run the goal's check and the standing checks below.
3. Show that each new check can fail: break what it checks, watch it fail,
   restore. A check that cannot fail checks nothing.
4. For a † goal, a fresh agent reviews the change against the standards and the
   mapping record. It is given the code and the documents, not the author's
   conclusion.
5. For a goal that changes the interface, a screenshot of the running
   application goes in the pull request.
6. One goal, one pull request. Merge when CI is green and tick the goal in the
   tracking issue, #493.
7. If a goal cannot pass without changing its check, stop and ask. Never weaken
   a check to pass it.

Standing checks, run every time:

```bash
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
# each pure crate, from the goal that adds it:
cargo build -p af-gsn -p af-layout -p af-api --target wasm32-unknown-unknown
# from G5:
pnpm --dir ui check
pnpm --dir ui e2e
```

## What this leaves for the next steps

- The argument tree in the left panel, with the same view and DATUM's `Tree`. It
  carries the accessible structure the canvas cannot.
- The problem list in the bottom panel: load diagnostics, well-formedness, cycles.
- SVG export from the render model.
- Reading terminology packages, so the three samples that have one open
  strictly and can be saved strictly
  ([libSACM#66](https://github.com/lasrod/libSACM/issues/66)).
- Opening files in the older SACM namespace that the C++ application wrote
  (also libSACM#66).
- Choosing the language of the diagram's text separately from the interface's.
- Saving from the web application. The File System Access API writes back to
  the opened file in Chromium browsers only; elsewhere saving is a download.
