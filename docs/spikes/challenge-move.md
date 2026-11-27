# Moving a challenge spike: findings

**Authority: evidence.** A record of what an experiment in the application
showed on 2026-11-27. The spike code is on the local branch
`spike/challenge-move`, and is not merged.

## The question

A challenge is added from a node's or a line's menu (milestone 2, step 6).
It then has to be moved: a counter goal that was put against the wrong claim,
or that turns out to speak against an inference rather than its conclusion.
Moving a node by dragging was settled by [the drag spike](drag-move.md), which
left a drop on a line for challenges, the only thing that goes onto a line.
Three things could not be settled on paper: whether a challenge is dropped on
a line at all, how the lines it may go to are shown, and what follows adding
a counter goal or counter evidence.

## What was built

A panel chose between:

| Setting | Variants |
|---|---|
| A drop on a line | On, or off (only nodes take a challenge) |
| The lines a challenge may go to | All of them marked while dragging, or only the one under the pointer |
| What follows Add Counter Goal or Add Counter Evidence | The new node selected; its name ready to type; its description ready to type |

Every variant used the core's answer for where the challenge may go
(`move_targets`, with a line for each line the diagram draws) and one command
(`move_challenge`, a new `target` for the counter relationship): the spike
changed only the gesture and what is marked.

## Findings

1. **A challenge is dropped on a line.** Chosen by the user. A challenge of an
   inference is a challenge of a line, and without the drop the only way to
   move one there is to remove the challenger and add it again.
2. **Every line it may go to is marked.** Chosen by the user over marking
   only the line under the pointer, although in the bundled example almost
   every line is a place. Marked only under the pointer, nothing shows in
   advance which lines take the challenge and which do not, such as a
   premise's line under a Strategy (AF-EDIT-027). With all of them marked,
   the one under the pointer looked like the rest; it is now drawn heavier,
   so the line the drop would go to is the one that stands out.
3. **A new counter goal has its name ready to type.** Chosen by the user over
   the description and over selecting it only. A counter is added to be
   written, and its identifier (`CG1`, `CSn1`) says nothing yet. Its name is
   focused only once the add is shown, so a refused add leaves the focus
   where it was.
4. **The view must keep the target in view after a drop.** The first build
   kept the challenger in view and pushed what it now challenges off screen.
   The view follows the target node, the upper end of the target line, or,
   for a challenge line, the node that challenge is drawn to.
5. **A challenge never goes to the top level.** A challenger with nothing to
   challenge is no longer a counter goal. The menu's move offers no top
   level, and the core refuses one (AF-EDIT-028). A drag dropped on the
   empty canvas is given up, as any drag's is.
6. **A challenge that is itself challenged stays.** Moving it would change
   what the challenge of it is about. W25's guards refuse it (AF-EDIT-015),
   as they refuse a move of a line something else refers to.

## What was built from it

Milestone 2, step 6, goal G4: the drop on a line, every allowed line marked
and the one under the pointer heavier, the name ready after an add, and the
rule W32 in [the mapping record](../standards/sacm-gsn-mapping.md#adding-a-challenge).
The panel was not kept.
