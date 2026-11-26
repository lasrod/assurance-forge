# Moving by dragging spike: findings

**Authority: evidence.** A record of what an experiment in the application
showed on 2026-11-26. The spike code is on the branch `spike/drag-move` and is
not merged.

## The question

Moving an element from its menu works, but takes a menu, a prompt and a click.
A drag is the obvious way to rearrange an argument, and how one should feel
cannot be settled on paper: what starts it, what follows the pointer, and what
a drop that misses does. The spike put the variants behind a panel in the
running application, on top of the move the menu already makes.

## What was built

A panel chose between:

| Setting | Variants |
|---|---|
| What starts a drag | Off (a drag pans, as before); a drag on a node; a press held for 300 ms, then a drag; Alt and a drag |
| What follows the pointer | The node, faint; a badge with the identifier and how many elements come with it; nothing but the highlighted places |
| What stays in place after the drop | The place dropped on; the node moved |
| At the edges | The view pans while the pointer is within 56 px of one, or not |
| A drop on the empty canvas | A Goal goes to the top level, or the drag is given up |

Every variant used the core's answer for where the node may go
(`move_targets`) and the menu's command (`move_node`): the spike changed
only the gesture.

## Findings

1. **A drag on a node is how a move should be made.** Tried against the move
   from the menu, the user's verdict was that it is much more convenient. The
   menu's move stays as the way that needs no drag, and the one the keyboard
   reaches.
2. **With the drag off, nothing says a node can be dragged.** The first try
   only panned the view, because the panel started at "Off" and was closed.
   A move by dragging has to be on from the start.
3. **The faint node hides the place it is over.** It is as large as the node
   under the pointer, so the text of the place being aimed at cannot be read.
   The badge sits beside the pointer and leaves it readable.
4. **A file's own mistakes meet the drag at once.** The bundled example joins
   its Assumption by a support line, which GSN does not allow, and the first
   rule refused to move it. Decided: it moves with its line as it is, and the
   repair is its own action from the Problems panel (plan decision M7).
5. **A touch cannot both pan and move.** A touch on a node is how the view is
   panned on a touchscreen, so the held press is kept for touch alone.
6. **The canvas did not claim touches.** With no `touch-action`, the browser
   took a touch over for its own scrolling after a few pixels, which cut both
   a pan and a held drag short. Found by the touch test, and fixed with the
   drag.

## Not tried

The preview of the result while the pointer rests on a place, a drop on a line
to put a Strategy between its two ends, and an animated change of layout. Each
needs its own try in the application.

## Consequences

The drag is built as plan decisions M8 to M13 of
[the plan](../roadmap/milestone-2-move.md): a drag on a node moves it, a
badge follows the pointer, the view pans at the edges, the place dropped on
stays where it was, and a drop on nothing gives the drag up.
