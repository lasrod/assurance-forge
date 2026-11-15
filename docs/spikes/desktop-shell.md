# Desktop shell spike: findings

**Authority: evidence.** A record of what a throwaway experiment showed on
2026-11-15. The spike code is not in this repository.

## The question

The plan is a Tauri desktop application whose interface is React with the DATUM
design system, on a Rust core that holds the libSACM document. The spike tested
whether those parts fit together: open a SACM file through libSACM, show it as a
tree, change it, and save it.

## What was built

- **A pure Rust crate** that opens a file's bytes with libSACM, derives a tree
  view from the document, renames an element through a libSACM edit operation,
  and writes the document back. It touches no files and also builds for
  `wasm32-unknown-unknown`.
- **A Tauri shell** with four commands: open, rename, save, and "which file was I
  started with". It is the only part that reads or writes files.
- **A React interface** built from DATUM components: toolbar, two panels, a
  property grid, a status bar, and a switch between English and Japanese.
- **A script** that drove the built application through open, select, rename,
  collapse, language switch, save, reopen, and a file that libSACM refuses.

Versions: Tauri 2.12, React 19, Vite 6, DATUM 0.3.0-rc.1, libSACM from its
repository on the day.

## Findings

### 1. The parts fit, and the chain works end to end

Every step the script took in the built application passed:

| Step | Result |
|---|---|
| Start with a file on the command line | The tree shows 141 rows for the sample case. |
| Select a claim, move with the arrow keys | The details panel follows the selection. |
| Rename the claim | Rust applies the edit and returns a new view; the tree, the panel title and "Unsaved changes" update. |
| Switch to Japanese | Every label changes; names and text from the file do not. |
| Save, then start again with the saved file | The new name is there. |
| Open a file libSACM refuses | An error with the line and column; no tree. |

The saved file was valid in libSACM, survived a strict round trip, and differed
from the original in the one renamed element only: the same 278 ids, and the
same text values apart from the name.

### 2. It is small and fast enough

| Measure | Value |
|---|---:|
| Executable, release build, not installed | 13.5 MB |
| Of which the interface (mostly fonts) | 8.2 MB |
| Process start to tree on screen | 0.6 s |
| Read, parse and build the view for the sample case | 12 ms |
| First build, debug | 2 min 44 s |
| First build, release | 3 min 40 s |
| Rebuild after a change to the core crate, debug | 9 s |

One machine, Windows 11. The interface runs in the system's WebView2.

### 3. The frontend contract holds

- **Commands in, views out.** The interface holds no copy of the argument. Each
  command returns a whole new view or a refusal, and the screen is drawn from
  that.
- **Types come from Rust.** `ts-rs` wrote the TypeScript types for the view and
  the refusals from the Rust definitions. The interface was type-checked
  against them, and a renamed field would fail the build.
- **Codes, not sentences, almost.** Element kinds and refusals arrive as codes
  and are worded in one file of the interface, in both languages. The exception
  is the detail of a libSACM refusal, which is an English sentence today.

### 4. DATUM covers the frame, not the tree

Toolbar, panels, property grid, fields, segmented control and status bar were
used as they are, and the result needed 15 lines of layout CSS and no colours.

- **There is no tree component.** DATUM styles list rows for a tree and says to
  use the tree pattern, but selection, expanding and arrow keys had to be
  written in the spike.
- **Indentation stops at four levels.** Deeper rows need a style override. An
  argument is deeper than that.
- **Fonts are the application's job.** IBM Plex Sans, its Japanese companion and
  the monospace face are 8 MB as shipped, in about 490 files.

### 5. The sample cases are further from libSACM than the namespace

The canvas spike found that libSACM refuses the sample files for their
namespace. Fixing the namespace alone was not enough. To get a real case into
the spike, the bilingual sample had to be converted by a script, because the
files the C++ application wrote also differ in these ways:

| In the C++ application's files | What libSACM reads today |
|---|---|
| `id="G1"` | `xmi:id="G1"` |
| `<claim>`, `<assertedInference>` | `<argumentElement xmi:type="sacm:Claim">` |
| `<source href="#G3"/>` child elements | Read, in this and the attribute form |
| A name in several languages | One language; the legacy form is planned |
| The statement as `content` on the claim | Not read on a claim yet |
| Terminology and artifact packages | Not read yet |

The converted file keeps the four argument packages and their 136 elements. It
drops the terminology and artifact packages, keeps the English name only, and
puts each statement in the element's description.

## What this decides

| Decision | Outcome |
|---|---|
| Tauri with React and DATUM on a Rust core | Confirmed. |
| The interface holds no model; it draws views and sends commands | Confirmed for a tree and one edit. |
| Rust is the source of the TypeScript types | Confirmed with `ts-rs`. |
| The core is pure and the shell does the file work | Confirmed; the same crate builds for WebAssembly. |

## What it leaves open

- **A tree component.** Build it in DATUM, where the other products can use it,
  or in this application.
- **What the tree shows.** The spike shows containment: packages and what is in
  them. The application's tree should follow the argument, goal to strategy to
  sub-goal, which is the work of the GSN view.
- **Sending the whole view after every change.** Fine at 141 rows. It will need
  to become "what changed" for large cases, as the canvas spike also found.
- **Wording of libSACM refusals.** They need a code and parameters, so they can
  be translated.
- **Opening the real sample files.** Either libSACM reads the C++ application's
  dialect, or the samples are converted once and committed as SACM 2.3.
- **macOS and Linux.** Tauri uses WebKit there. Nothing was run on either.
- **Installer, signing and updates.** The spike built a bare executable.
- **Font size.** Ship fewer weights, or one variable font per script.

## Limits

- One case of 141 visible rows. No large case was tried in the tree.
- One edit, a rename. Undo, drafts and the change stream were not built.
- The open and save dialogs were not exercised by the script; it started the
  application with a file and saved to the same file.
