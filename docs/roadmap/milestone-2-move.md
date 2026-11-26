# Milestone 2, step 4: moving an element

**Authority: plan.** This page says what moving an element is, how it is
checked, and in what order it is built. It is short-lived: when the last goal
is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

An element is moved, with what is under it, to another place in the
argument: under another Goal or Strategy, or to the top level. Until now an
argument could only be grown from the top down, and a branch in the wrong
place was removed and written again.

It is the fourth step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases). It has two parts.
The first is the move itself and a plain way to make it, from the menu. The
second is moving by dragging, which was tried out in the application before
it was designed: how a drag should feel cannot be settled on paper.

### Done when

Part 1, the move from the menu:

1. In every sample, each element that can be moved goes to a place it may,
   and the file stays valid, reads back with the element there and no new
   finding, and writes strictly.
2. A move changes one line of the diagram. The element, its text and what is
   under it are as they were; support stays support and context stays context.
3. Only places GSN allows are offered, and a place that is not says why.
4. A move is one undo step.
5. It works from the menu and the keyboard, in the desktop and the web
   application, in English and Japanese, with browser tests in Chromium,
   Firefox and WebKit.

Part 2, moving by dragging:

6. A drag that begins on a node moves it to the place it is dropped on, by
   the same rules and the same command as the menu's move. A drag that begins
   anywhere else pans.
7. A node that cannot be moved says why at once, and nothing follows the
   pointer. A drop on a place the node cannot go says why. A drop on nothing,
   and Escape, change nothing.
8. The view pans while a dragged node is near an edge, so a place out of view
   is reached.
9. A touch moves a node once it has been held, and pans otherwise.
10. Browser tests in Chromium, Firefox and WebKit; touch in Chromium.

### Out of scope

A second parent: an element is under one element, and a move takes it from
there. Moving an element that is under several, that challenges something, or
into another argument package (modules). The order of the elements under one
parent, which is a presentation hint and has no store yet. Changing an
element's kind.

## Decisions

| | Decision | Why |
|---|---|---|
| M1 | A move is asked of the core in two steps, as an add is: `move_targets` says, for every node and for the top level, whether the element may go there and why not; `move_node` makes it, at the revision the targets were of | The menu, a click and a drag all show the same places and are refused the same way |
| M2 | The writing rules are W21 to W25 of [the mapping record](../standards/sacm-gsn-mapping.md#moving-a-node) | A move is a SACM operation like every edit |
| M3 | From the menu: **Move…** starts choosing a place. The canvas draws the places the element may go in full and the rest faint; a click on a place moves it there, and a click on another says why not and keeps the choice open. A place's own menu offers **Move … here**. Escape, or **Cancel move**, gives it up | A way to move that needs no drag (WCAG 2.5.7), and that the keyboard reaches through the menu key |
| M4 | The prompt is beside the zoom controls, not over the diagram | Over the diagram it covered the top Goal, which is often the place wanted |
| M5 | After a move the place the element went to stays where it was on screen, and the element moved is selected and brought into view | The user was looking at the place they chose |
| M6 | A line that anything refers to, or that holds a description, is not given a new end or replaced (AF-EDIT-015) | What the diagram does not show on a line is not carried elsewhere, or lost, for the user. A challenge of a line is the case that will come up; it waits for challenges to be edited |

| M7 | An element the file joins in a way GSN does not allow (an Assumption by a support line) is moved with its line as it is. The move does not repair it; the repair is offered from the Problems panel, as its own action | A drag that also changed the kind of a line would change the argument unasked. The sample `sacm_example_core_argument` has such an Assumption |

| M8 | A drag that begins on a node moves it. It no longer pans: a drag on the empty canvas, the middle button and the wheel do | Tried against the menu's move, it is much the more convenient. A node is where the hand goes to rearrange; the canvas is still mostly ground to pan by |
| M9 | A badge follows the pointer, beside it: the node's identifier, and how many elements come with it. The node itself stays where it is until the drop | A faint copy of the node hid the text of the place it was over. The count says a branch is being moved, not one element |
| M10 | While a node is dragged the places it may go are drawn as for the menu's move, with no prompt. A drop on a place it may not go says why and ends the drag | One picture of where an element may go, however the move is made |
| M11 | A drop on the empty canvas, a drop back on the node, and Escape give the drag up. A Goal goes to the top level from the menu | A drop that missed must not take a branch out of the argument |
| M12 | The view pans while the pointer is within 56 px of an edge, faster the nearer it is. The place dropped on stays where it was on screen after the drop | A place out of view is reached without letting go; the user was looking at the place they chose |
| M13 | A touch on a node pans, as a touch anywhere does, unless it is held for 300 ms first; then it moves the node. Zoomed out too far for a node's text to be drawn, a drag on it pans with any pointer | A touch cannot both pan and move. A node that cannot be read is not moved by a slip of the hand, and far out the canvas is mostly nodes |

| M14 | No preview of the result before the drop | Tried as the whole result and as the branch alone, faint: both felt off, and showing nothing more felt better ([the findings](../spikes/drag-move.md#a-preview-of-the-move)). Edit ▸ Undo is the way back |

| M15 | A line is not a place to drop on | Tried as putting the node between the line's two ends, it is not needed as it is. To be opened again with challenges: a challenge of a Goal dragged to a line would challenge the relationship instead |

## Still open

- **After the drop.** Whether the change of layout is animated. Not tried.

## The pieces

| Piece | Where |
|---|---|
| The rules: what may move where, and the SACM operations | `af-gsn` (`commands::move_targets`, `commands::move_node`) |
| The command, as one revision and one undo step | `af-session` (`Session::move_node`) |
| The contract: `MoveTargets`, `move_targets`, `move_node` | `af-api`, `af-wasm`, `af-desktop` |
| The places drawn, and the click | `ui/src/canvas` (`Marks.move`) |
| The drag: the press, the badge, the pan at the edges | `ui/src/canvas/DiagramCanvas.tsx`, `drag.ts`, `draw.ts` (`Marks.badge`) |
| The menu items, the prompt and the status | `ui/src/App.tsx`, `ui/src/useDocument.ts` |

## The goals

- [x] **G1.** The rules and the command in the core, with tests on the
      fixtures and on every sample.
- [x] **G2.** The move from the menu, in both shells and both languages, with
      browser tests.
- [x] **G3.** The drag variants, behind a switch for development, tried in
      the application.
- [x] **G4.** The findings written, and this page's goals for the drag.
- [x] **G5.** The drag as chosen (M8 to M13), with browser tests.
- [x] **G6.** A preview of the move, tried and decided against (M14).
- [x] **G7.** A drop on a line, tried and put off until challenges are
      edited (M15).
