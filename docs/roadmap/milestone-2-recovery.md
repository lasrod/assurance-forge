# Milestone 2, step 3: the recovery copy

**Authority: plan.** This page says what the recovery copy is, how it is
checked, and in what order it is built. It is short-lived: when the last goal
is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

Changes that are not saved survive a crash. While the open case has changes
that are not saved, the application keeps a copy of it, at most about a
second behind. When the application ends without closing the file (a crash,
a killed process, a power cut, a browser tab that was reloaded or closed),
the copy stays; opening that file again offers it back. Restoring is one
ordinary change: Edit ▸ Undo shows the file as it was saved.

It is the third step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases), after
[saving](milestone-2-save.md).

The C++ application had no recovery copy: it saved the file itself after
every command. It is not a model here. Its lessons are the ones taken: a copy
records which file and which content of it it was made from, and a copy that
cannot be read is reported, never deleted.

### Done when

1. A copy made of each sample, edited, restores to a document `==` to the
   one that was copied; a copy of a document that is saved is not made.
2. A copy a strict read refuses, one in a format this version does not know,
   and one of another file's content are each told apart and refused or
   flagged with a code, and the document open is not touched.
3. The desktop application writes the copy whole or not at all, beside the
   file, and removes it, and its folder when empty, once the changes are
   saved, undone, or given up by the user. A crash leaves it.
4. Opening a file with a copy asks before anything else; Restore makes one
   undoable change, Discard removes the copy. Both shells, English and
   Japanese, with browser tests in Chromium, Firefox and WebKit.

### Out of scope

A recovery copy of a file dropped on the desktop window (it has no path, so
no folder beside it). Listing copies on the welcome page. The undo history
of the session that ended (the copy is the document only). The rest of the
working folder (drafts, review state).

## Decisions

| | Decision | Why |
|---|---|---|
| R1 | A copy is the whole document as SACM 2.3 XMI, made by the core's checked write (S1), in an envelope the core makes and reads: a format name and version, the file's name, a SHA-256 of what the file held when these changes began (its **base**), when it was written, and the XMI. It is one file, written whole | One file cannot be half new. The base tells a copy of this content from one of another (R6). The core owns the format, so both shells store bytes they do not read |
| R2 | The page asks the shell to keep a copy about one second after the last accepted change, and at once when the document is back in its saved state. A copy asked for at a revision that is no longer current is not made | The clock is the page's: the core has none, and one timer serves both shells. Commands are whole steps (an add, a text applied), so a second is short |
| R3 | A copy exists only while the document has changes that are not saved. It is removed when they are saved, when undo returns to the saved state, and when the user closes the file after choosing Don't save (Close, opening another file, closing the window). Only an end without a close leaves it | The copy is for what the user did not choose to lose. Kept after Don't save, it would offer back what was given up |
| R4 | The desktop keeps the copy in the working folder beside the file (Phase 0): `<file name>.af/recovery.json`, made with the first copy and removed when it is empty again. The browser keeps it in the page's IndexedDB, by the file's name, as it keeps recent files, in a database of its own | The working folder is where Phase 0 put working state. A folder that is removed when empty leaves nothing beside the file after ordinary use. The page knows a file only by its name |
| R5 | Opening a file that has a copy asks first: Restore or Discard, saying when the copy was made. Restore is one undoable command (`Restore`); Escape restores. Discard removes the copy. While the question is open, no copy is written over the one found, and closing the file leaves it | Restore loses nothing, since undo takes it back: it is the safe answer. Discard is the only way a copy found is deleted |
| R6 | When the file holds something other than the copy's base (another program saved it, or in the browser another file has the name), the question says so. Restoring then takes the copy all the same, and the desktop's File ▸ Save becomes Save As… until the case is saved | The changes are the user's to take, but File ▸ Save must not write them over a newer file without asking (S5) |
| R7 | A copy that cannot be read (not an envelope, a format this version does not know, or XMI a strict read refuses) is set aside, never offered and never deleted: renamed to `recovery.unreadable-<time>.json` on the desktop, kept under another key in the browser. The status says so | A copy is the user's work. A copy of a newer format may be read by the version that made it |
| R8 | A copy that cannot be written (the folder is read-only, the browser's storage is full or blocked) is said once in the status; editing and saving go on | The copy is a safety net, not a condition of editing |

## Known limits

- The desktop keeps no copy of a file dropped on its window: it has no path,
  and Save is Save As… (S6).
- Two windows or two copies of the application with one file open write one
  copy: the last one written wins.
- In the browser, leaving the page after `beforeunload` asked keeps the copy:
  the page cannot tell that the user chose to leave. Opening the file again
  offers it, and Discard removes it.
- In the browser, Save downloads the case and removes the copy (S7): the page
  cannot tell whether the download was kept.
- Copies set aside (R7) stay until the user deletes them.

## Codes

| Code | Meaning |
|---|---|
| AF-RECOVER-001 | Nothing is open to restore into |
| AF-RECOVER-002 | The copy is not a recovery copy this version reads (R7) |
| AF-RECOVER-003 | The copy's SACM is refused by a strict read (R7) |
| AF-RECOVER-004 | The copy could not be written (R8) |

## The pieces

| Crate or part | Adds |
|---|---|
| `af-session` | `Command::Restore`: the document replaced by another, as one undoable command |
| `af-api` | The base of the open document; `recovery_copy` (the envelope, or that none is needed), `read_recovery` (what a copy is), `restore`; the TypeScript bindings |
| `af-desktop` | The working folder: write the copy whole, remove it and the folder, set an unreadable one aside; look for a copy on open; remove on save, close, opening another file and the window's end; Save As… after R6 |
| `af-wasm` | `recoveryCopy`, `readRecovery`, `restore`; the page keeps the copies in IndexedDB |
| `ui/` | The timer, the question before anything else on open, the status lines; the words in English and Japanese |

## The goals

1. **G1 Core.** `af-session` and `af-api` (done when 1, 2 in Rust).
2. **G2 Shells.** `af-desktop` and the browser's store (done when 3).
3. **G3 Interface.** The timer, the question and the words, browser tests
   (done when 4), checked in the running application.

All three are one pull request.
