# Milestone 2, step 2: saving

**Authority: plan.** This page says what saving a case is, how it is checked,
and in what order it is built. It is short-lived: when the last goal is done,
what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

File ▸ Save writes the open case back to its file, and File ▸ Save As… to a
file the user names. In the browser, Save downloads the case. The window's
title and the status bar say when the case has changes that are not saved,
and nothing that would lose them (Close, Open, a recent file, the example,
a dropped file, closing the window) does so without asking first.

It is the second step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases). The recovery copy
is the next step.

### Done when

1. Every sample, opened, edited and saved, reads back `==` to the document
   that was saved, and the bytes of a second save are the bytes of the first.
2. A save that would not read back as the document is refused with a code,
   and the file on disk is not touched.
3. The desktop application writes the file whole or not at all: a failed
   save leaves the file as it was. A file changed on disk since it was opened
   or last saved is not overwritten.
4. Undo back to the saved state shows the case as saved again; an edit after
   a save shows it as changed.
5. Save and the unsaved-changes question work from the menu and the keyboard
   (Ctrl+S, Ctrl+Shift+S; ⌘S, ⇧⌘S), in the desktop and the web application,
   in English and Japanese, with browser tests in Chromium, Firefox and WebKit.
6. CI is green and `ARCHITECTURE.md` says what is built.

### Out of scope

The recovery copy and the working folder beside the file (`<file>.sacm.af/`):
the next step. Saving to a place in the browser other than its downloads (the
File System Access API is in Chromium only). Opening files that a strict read
refuses (libSACM#66): when they open, saving one converts it, which needs its
own rule.

## Decisions

| | Decision | Why |
|---|---|---|
| S1 | The core writes the document with libSACM's strict save, reads the bytes back by a strict read, and gives them out only if what it read is `==` to the open document. Otherwise the save is refused (AF-SAVE-003) | "Never silently change an argument": a writer fault must not reach the disk. It costs one read per save |
| S2 | The first save of a file written by another tool or by hand rewrites its layout: libSACM writes deterministically, in its own order. Its meaning is unchanged | Accepted on 25 November 2026. A byte-preserving writer is not something libSACM offers |
| S3 | Whether the case is saved is told by what the document is, not by its revision: undo back to the saved state is saved again | A revision only goes up, undo included, so it cannot say this |
| S4 | The desktop writes a temporary file beside the target and renames it over the target | A crash or a full disk mid-save leaves the old file, never half a new one |
| S5 | The desktop keeps the length and modified time of the file as it was read or last written. File ▸ Save refuses to overwrite a file that is there and has changed since (AF-SAVE-004). A file the user chose in the save dialog, which asks before it replaces one, is written; a file gone from disk is written again | Another program's change to the file is not lost without the user knowing |
| S6 | A file opened without a path (dropped on the window) has nowhere to be saved to: Save is Save As… | |
| S7 | The browser's Save downloads the file under its name, and the status says it was downloaded, not saved. There is no Save As… in the browser | Downloads are where every browser can write. The page cannot tell whether the user kept the download, so the case is marked saved once it is handed over |
| S10 | A save is of the document at the revision on screen when it was asked for. The save dialog is the window's child, so the window takes no input while it is open; a document that changed meanwhile all the same is not saved (AF-SAVE-006) | Found in review: on Windows a dialog without a parent let another file be opened, and its content written over the first file's name |
| S8 | Asking before changes are lost: Save, Don't save, Cancel. Save that is cancelled or refused cancels what was asked for | What every editor does |
| S9 | Closing the desktop window with unsaved changes is held in Rust and asked about in the page; the browser asks through `beforeunload` | The window's close needs no extra permission on the page |

## Known limits

- Text typed in Element Properties and not yet applied (by Enter, or by
  leaving the field) is not a change: closing the window or the tab then
  loses it without asking.
- The rename that makes a save whole replaces a symbolic link with a file, and
  gives the file the folder's default permissions.
- A change on disk of the same length within the file system's time
  resolution (two seconds on FAT) is not seen.

## Codes

| Code | A save is refused because |
|---|---|
| AF-SAVE-001 | Nothing is open |
| AF-SAVE-002 | libSACM refused to write the document |
| AF-SAVE-003 | What was written does not read back as the document (S1) |
| AF-SAVE-004 | The file has changed on disk since it was opened or saved (S5) |
| AF-SAVE-005 | The file could not be written: the system refused it |
| AF-SAVE-006 | The document changed while it was being saved |

## The pieces

| Crate or part | Adds |
|---|---|
| `af-session` | Which document state was saved, and whether the document is that state (S3) |
| `af-api` | `write()`: the checked bytes (S1); `mark_saved(revision)`; `modified` in `History`; the TypeScript bindings |
| `af-desktop` | The open file's path and stamp; `save` and `save_as` with the save dialog; the atomic write (S4, S5); the window's close held while changes are not saved (S9); a file saved as becomes a recent file |
| `af-wasm` | `write` and `markSaved` |
| `ui/` | File ▸ Save and Save As…, their shortcuts, the title's and the status bar's mark, the unsaved-changes dialog before Close, Open, a recent file, the example, a drop and the window's close; the codes in English and Japanese |

## The goals

1. **G1 Saved state.** `af-session` and `af-api` (done when 1, 2, 4 in Rust).
2. **G2 Shells.** `af-desktop` and `af-wasm` (done when 3).
3. **G3 Interface.** The menu, the dialog and the marks, words, browser tests
   (done when 5), checked in the running application.

All three are one pull request.
