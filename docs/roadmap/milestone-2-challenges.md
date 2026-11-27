# Milestone 2, step 6: challenges

**Authority: plan.** This page says what adding and moving a challenge is,
how it is checked, and in what order it is built. It is short-lived: when the
last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

An argument records what speaks against it as well as what supports it. A
Goal, an Assumption, a Justification, a Solution, a Context, a Strategy or a
line is challenged by a new counter-Goal or a new piece of counter-evidence,
from its menu. A challenge is moved to challenge something else, by the menu
and by dragging it, onto a node or onto a line.

Challenges have been read and drawn since 21 November 2026 (E5 in
[the mapping record](../standards/sacm-gsn-mapping.md)): a dashed line with
an open arrowhead, at what is challenged, with the challenger beside it.
Nothing could add, change or remove one. It is the sixth step of milestone 2
(Edit) in the [Phase 0 release plan](rust-rebuild-phase-0.md#releases), and
the first half of "assurance claim points and challenges"; ACPs are the next
step.

### Done when

1. A challenge is added to each kind of node and line that may be challenged,
   as a counter-Goal or as counter-evidence, and the file stays valid, reads
   back with the challenge drawn, and writes strictly. Each is one undo step.
2. Counter-evidence is drawn as a Solution, in files the editor writes and in
   files it reads.
3. What may not be challenged, or may not challenge, is not offered, and says
   why where it is asked for.
4. A challenger is moved to challenge another node or line, by the menu and by
   a drag, including a drop on a line. The challenge keeps its relationship
   and what is under the challenger.
5. A line has a menu, from a right-click and from the keyboard.
6. It works in the desktop and the web application, in English and Japanese,
   with browser tests in Chromium, Firefox and WebKit.

### Out of scope

The defeated decorator and the in-doubt state (decided 27 November 2026:
SACM 2.3 can hold defeat only on a Claim or a relationship, on a Claim it
takes the slot that tells a Goal from an Assumption, and SACM24-12 proposes
removing it; it is decided on its own once challenges can be tried).
Assurance claim points (the next step). A challenge of something in another
argument package (modules). Removing a challenge line alone: a challenge goes
with its challenger, as a removal already takes it (W18).

## Decisions

| | Decision | Why |
|---|---|---|
| C1 | A counter-Goal is a new `Claim` and an `AssertedInference` with `isCounter` from it to what is challenged; counter-evidence is a new `ArtifactReference` and an `AssertedEvidence` with `isCounter`. Each is one `sacm::edit::batch` | `isCounter` is what carries a challenge in SACM (gap row 4, E5). The GSN Metamodel v2.2 forbids it (`self.isCounter = false`, Figure 2), and the project follows GSN v3, which requires challenges, and records the gap |
| C2 | Counter-evidence is drawn as a Solution: an `ArtifactReference` that is the source of a counter `AssertedEvidence` and is not otherwise reached is a Solution, not neutral (R11 changes) | Decided by the user on 27 November 2026. GSN v3's Dialectic Extension expresses counter-evidence by a Solution, and the C++ application drew it so (GSN3-DIA-002). R11 left it undecided |
| C3 | A challenge is offered from the node's menu and the line's menu: **Add Counter Goal** and **Add Counter Evidence** (renamed after the first screenshots, as GSN v3 names them). The new challenger is selected, with its name ready to type (G3) | The menu is where the diagram is edited (step 1). A way that needs no drag (WCAG 2.5.7) |
| C4 | A line takes a menu, as a node does: a right-click on it, or the menu key with it selected | A line is what a challenge of an inference points at; until now nothing was added to a line |

Decided in G1, with the evidence in
[the mapping record](../standards/sacm-gsn-mapping.md#adding-a-challenge):

- **What may be challenged (W29).** A Goal, an Assumption, a Justification,
  a Solution and a Context node; a `SupportedBy`, `InContextOf` or challenge
  line. A Strategy is challenged through the one inference it is the
  `reasoning` of, which E5 draws at the Strategy (GSN v3 §1:6.3.12). A
  neutral node or line is not.
- **Only a relationship's first line is challenged (W29, AF-EDIT-027).**
  Found in G2: a premise's line under a Strategy is part of one inference,
  and a challenge of it was drawn on the line into the Strategy, not on the
  line chosen. Such a line says why and points to the first line or the
  Strategy.
- **What challenges (W30).** A counter goal or counter evidence: a Goal or a
  Solution, GSN v3's "dialectic uses of the following core elements" (§1:6.2.2;
  AF-EDIT-026 otherwise). The menu says **Add Counter Goal** and **Add Counter
  Evidence**.
- **The challenge (W31).** The challenger is made as an add makes a node, in
  the package of what is challenged, numbered `CG1`, `CG2`… or `CSn1`,
  `CSn2`…, as the standard's dialectic figures number them (asked for by the
  user on 27 November 2026, after the first screenshots).
- **Counter-evidence is a Solution (R9).** R11 no longer makes it neutral.

Decided in G3 and G4, from [the spike](../spikes/challenge-move.md):

- **How a challenger is moved (W32).** Its counter relationship gets a new
  target (`SetReferences`), with W25's guards, and what is under the
  challenger comes with it. This lifts AF-EDIT-017 for a node that challenges
  one thing. It is moved by the menu's Move… and by a drag, onto a node or a
  line, never to the top level (AF-EDIT-028).
- **While it is dragged**, every line it may go to is marked, and the one
  under the pointer is drawn heavier.
- **After Add Counter Goal or Add Counter Evidence**, the new node's name is
  ready to type.

### For the next step: the ACP identifier

Read on 27 November 2026, for the step after this one. GSN Metamodel v2.2 has
no ACP class, strikes `metaClaim` out and forbids tagged values, so it gives no
carrier for an ACP's identifier. GSN v3 does not need one: "The {ACP
identifier} may be a reference to a goal, a section in a document, or other
form of unique reference that can be followed by the reader of the argument"
(§1:5.3.2), with the module alongside, as `ACP1[Confidence]`, when the
argument is in another module (§1:5.2.3). An ACP labelled with the identifier
of its `metaClaim` goal is therefore the standard's own option, and writes
nothing new. The same section puts ACPs on `SupportedBy` and `InContextOf`
lines and on elements that reference an artefact (a Solution, a Context), and
calls an ACP on one line below a Strategy "ambiguous" (§1:5.3.3).

## Trying it out

As the move was, the hand-held part is tried in the application before it is
designed. A spike on a local branch puts variants behind a panel:

- **Moving a challenge by dragging.** The challenger dragged onto a node or a
  line (the drop on a line that step 4 left for challenges), with the places
  it may go marked as the move marks them.
- **What follows an add.** The new challenger selected with its text ready to
  type, or only selected.

The findings, and what was chosen, are in
[`docs/spikes/challenge-move.md`](../spikes/challenge-move.md).

## The pieces

| Piece | Where |
|---|---|
| The rules and the SACM operations | `af-gsn` (`commands`), with R11's change in `view` |
| The commands, each one revision and one undo step | `af-session` |
| The contract and the two shells | `af-api`, `af-wasm`, `af-desktop` |
| The menus of nodes and lines, the status, both languages | `ui/` |

## The goals

- [x] **G1.** The rules and the commands in the core: adding a challenge, and
      counter-evidence drawn as a Solution, with tests. The decisions above
      recorded with their evidence.
- [x] **G2.** The menus: a line's menu, and the two challenge items on nodes
      and lines, in both languages, with browser tests.
- [x] **G3.** The spike: moving a challenge by dragging, and what follows an
      add.
- [x] **G4.** Moving a challenger, by the menu and by the drag as chosen,
      with browser tests.
