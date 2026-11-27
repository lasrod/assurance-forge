# Milestone 2, step 5: the undeveloped mark, and a repair

**Authority: plan.** This page says what setting the undeveloped mark and
repairing a wrongly joined element are, and how they are checked. It is
short-lived: when the last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

A Goal is marked undeveloped, and the mark is cleared, from Element Properties
and from the Goal's menu. The mark is drawn twice the size it was, so it is
read at the zoom a whole branch is seen at.

An element the file joins by a support line where GSN has context (an
Assumption, a Justification or a Context that "supports" a Goal) is repaired
from the Problems panel: the problem that reports it offers **Repair**, which
joins the element as context.

It is the fifth step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases), and follows
[moving an element](milestone-2-move.md), whose decision M7 left the repair
to the Problems panel.

### Done when

1. A Goal with no support is marked and the mark cleared; the file reads back
   as it was set. A supported Goal is not marked, and says why. A mark the
   file left on a supported Goal is cleared.
2. Anything but a Goal offers no mark.
3. The repair replaces the support line by a context line between the same
   two elements, the problem goes from the list, and the file stays valid
   and writes strictly. It is one undo step.
4. A line that something refers to is not replaced.
5. Both work in the desktop and the web application, in English and Japanese,
   with browser tests in Chromium, Firefox and WebKit.

### Out of scope

The undeveloped mark of a Strategy, which GSN has and SACM 2.3 has no place
for (below). Repairs of the other problems. A repair of
every such join in a file at once.

## Decisions

| | Decision | Why |
|---|---|---|
| U1 | The writing rules are W26 and W27 of [the mapping record](../standards/sacm-gsn-mapping.md#the-undeveloped-mark) | Each is a SACM operation like every edit |
| U2 | The mark is a checkbox in Element Properties, shown for a Goal only, and an item of the Goal's menu where the mark can be changed. A supported Goal shows the checkbox greyed with the reason beside it | The property is where an element's state is read; the menu is where the diagram is edited. A control that cannot be honoured says why and does not report success (the C++ application's lesson, in the mapping record) |
| U3 | A supported Goal is not marked undeveloped (AF-EDIT-022). Clearing is always allowed | GSN: an undeveloped element has no support. A mark the file left on a supported Goal is a problem (AF-GSN-010) that clearing mends |
| U4 | The diamond is 32 units across, where it was 16: half the space between two rows | Asked for by the user: at 16 it was lost at the zoom a branch is read at |
| U5 | The repair is offered by the problem that reports the join (AF-GSN-004), above the list once that problem is chosen. It is asked of the core at the revision the list is of | A repair changes the argument, so it is the user's act on a problem they have read, not something a move or an open does for them (decision M7 of the move). A list row is one option of a listbox and cannot hold a button |
| U6 | The new context relationship takes the name of the support relationship it replaces; its `xmi:id` is new | SACM cannot change a relationship's class in place. The name is the one thing of the old relationship a reader may know it by |
| U7 | With a Strategy as the relationship's reasoning, the join is not repaired | The element is then one of the Strategy's premises, and whether it is context of the Strategy or of its Goal is not the editor's to say |

## The undeveloped mark of a Strategy

Not supported, because SACM 2.3 cannot hold it (decided 26 November 2026).
The GSN Metamodel v2.2 gives `Strategy` an `undeveloped` boolean of its own,
on a class that extends `ArgumentReasoning`, which has no
`assertionDeclaration`; it leaves the mapping onto SACM out. The evidence is in
[the mapping record](../standards/sacm-gsn-mapping.md#the-undeveloped-mark), and
the gap is row 15 of
[the metamodel gaps](../standards/sacm-gsn-metamodel-gaps.md). A Strategy that
has no Goal yet is still reported by the problem list (AF-GSN-008).

## The pieces

| Piece | Where |
|---|---|
| The rules and the SACM operations | `af-gsn` (`commands::set_undeveloped`, `undeveloped_change`, `repair_join`, `repairable`) |
| The commands, each one revision and one undo step | `af-session` |
| The contract: `ElementDetails.undevelopedRefusal`, `Problem.repair`, `set_undeveloped`, `repair_join` | `af-api`, `af-wasm`, `af-desktop` |
| The size of the diamond | `af-layout` (`DIAMOND`) |
| The checkbox, the menu item and the Repair action | `ui/src/ElementProperties.tsx`, `App.tsx`, `ProblemsPanel.tsx` |

## The goals

- [x] **G1.** The rules and the commands in the core, with tests.
- [x] **G2.** The larger diamond.
- [x] **G3.** The checkbox, the menu item and the Repair action, in both
      languages, with browser tests.
- [x] **G4.** The undeveloped mark of a Strategy decided, with its evidence:
      not supported.
