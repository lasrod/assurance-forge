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
second is moving by dragging, which is tried out in the application before it
is designed: how a drag should feel cannot be settled on paper.

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

Part 2, moving by dragging: the questions below are answered from trying the
variants in the application, the findings are written under `docs/spikes/`,
and this page is given the goals that follow from them.

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

## Questions for part 2

Each is a variant to try, not a decision:

- **What starts a drag.** A drag on a node now pans, as a drag anywhere does.
  A drag on a node could move it at once, after a held press, or only with a
  modifier key.
- **What follows the pointer.** The node, a small mark with a count of what
  comes with it, or nothing but the highlighted places.
- **A long way.** The view pans when the pointer nears an edge; how fast, and
  whether zooming during a drag is wanted.
- **After the drop.** What stays in place while the layout is made again, and
  whether the change is animated.
- **Dropping on nothing.** Whether a drop on the empty canvas moves a Goal to
  the top level, or is too easy to do by accident.
- **A preview.** Whether the result is shown, faint, while the pointer rests
  on a place.
- **Dropping on a line.** Whether a Strategy dropped on a line goes between
  its two ends.

## The pieces

| Piece | Where |
|---|---|
| The rules: what may move where, and the SACM operations | `af-gsn` (`commands::move_targets`, `commands::move_node`) |
| The command, as one revision and one undo step | `af-session` (`Session::move_node`) |
| The contract: `MoveTargets`, `move_targets`, `move_node` | `af-api`, `af-wasm`, `af-desktop` |
| The places drawn, and the click | `ui/src/canvas` (`Marks.move`) |
| The menu items, the prompt and the status | `ui/src/App.tsx`, `ui/src/useDocument.ts` |

## The goals

- [x] **G1.** The rules and the command in the core, with tests on the
      fixtures and on every sample.
- [x] **G2.** The move from the menu, in both shells and both languages, with
      browser tests.
- [ ] **G3.** The drag variants, behind a switch for development, tried in
      the application.
- [ ] **G4.** The findings written, and this page's goals for the drag.
