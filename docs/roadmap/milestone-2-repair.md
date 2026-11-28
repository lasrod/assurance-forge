# Milestone 2, step 8: defect repair

**Authority: plan.** This page says which problems the editor repairs, what
each repair does, and in what order it is built. It is short-lived: when the
last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

A problem the Problems panel reports against GSN's rules offers the repair
that clears it, and the repair differs by rule: a line GSN does not allow is
removed, a duplicated identifier is renumbered, a stale undeveloped mark is
cleared, a cycle is broken at the line the user chooses, and a Strategy drawn
at an end of a relationship is made that relationship's reasoning. A line
is also removed from its own menu, as an edit like any other.

It is the eighth step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases): "defect repair".
[Step 5](milestone-2-mark-and-repair.md) built the first repair, of a
Context, Assumption or Justification joined by a support line (W27), and
left "repairs of the other problems" to this step. The C++ application's
capability AF-METH-011 is the reference for behaviour: every
well-formedness finding carried a repair, chosen by rule rather than by
deleting whatever was wrong, and removing a relationship became an edit of
its own, because without it the validator reported defects in an imported
case that nothing could correct.

### Done when

1. Each repair below clears its problem on a fixture built to break the
   rule, and the file stays valid, reads back with the problem gone, and
   writes strictly. Each is one undo step.
2. A repair that cannot be made correctly changes nothing and says why, and
   a test shows the refusal can fail: the check broken in turn, the test
   failing.
3. **Remove Line** on a line's menu removes a `SupportedBy` or `InContextOf`
   line, or says why not. The nodes at its ends stay. An ACP on the line is
   listed and asked about first, as a node's removal does (W19).
4. The Problems panel offers each of a problem's repairs, worded by what it
   does, in English and Japanese.
5. Every GSN rule of the problem list (AF-GSN-001 to 010) is either
   repaired or listed below with why not.
6. It works in the desktop and the web application, with browser tests in
   Chromium, Firefox and WebKit.

### Out of scope

- A repair of every problem in a file at once (as step 5).
- Repairs of what libSACM reports (the SACM and Read sources) and of what
  the diagram does not draw (AF-VIEW-*): they are about the file's SACM, not
  its GSN, and are libSACM's to say how to mend.
- AF-GSN-008, a Strategy that supports nothing. It is the problem the
  samples hold most (five Strategies across `sample.sacm.xmi`, `oasc-ja` and
  the sanitized case, each with context only), and the file does not say
  which Goal they reason for. A Strategy with no Goal yet is also how the
  editor adds one (W2). It stays a warning, with no repair.
- Removing a challenge line alone: a challenge goes with its challenger
  (W18, step 6). Removing a neutral line, whose GSN meaning the file leaves
  unknown (P3).

## What the standards say

The rules are the problem list's (P2 of [step 2 of milestone
1](milestone-1-problems.md#the-gsn-checks-p2)), each with its GSN v3
requirement: GSN3-CORE-014 (a structured argument is acyclic), CORE-015
(which elements may be joined by which relationship), CORE-002 (a Strategy
is the reasoning between a Goal and its sub-goals), CORE-010 (identifiers are
unique) and CORE-009 (an undeveloped element has no support). The mapping
rows each repair writes are recorded with their evidence in
[the mapping record](../standards/sacm-gsn-mapping.md), as every edit's are.

## Decisions

| | Decision | Why |
|---|---|---|
| F1 | Each GSN rule has the repair in the table below, or none and the reason | The C++ application's lesson (AF-METH-011): a repair chosen by rule keeps what the author meant, where deleting the offending element loses it. Decided by the user on 28 November 2026 for AF-GSN-001, 005 and 007 and for line removal |
| F2 | **Remove Line** is an item of a `SupportedBy` or `InContextOf` line's menu, and the repairs that remove a line are that same command. The line alone in its relationship: `DeleteElement` of the relationship. One of several sources: `RemoveReference` of that source, the relationship staying for the others; the last Goal under a Strategy leaves the placeholder W20 adds. Refused, and saying why, when something refers to the relationship (a challenge of it, a citation, a group's member), as W25 refuses a move | Decided by the user on 28 November 2026, over a removal offered only as a repair: the C++ application made it a first-class edit for the same reason. The refusals are W25's: "never silently change an argument" |
| F3 | A removed line takes no node with it. A node left with no parent is drawn as a top-level node. The line from a Goal to its Strategy is not removed alone: SACM 2.3 §11.13 gives an inference exactly one `target`
(`target:ArgumentAsset[1]`), so it is refused, and the Strategy is removed instead | Removing a line is not removing a node (W18), so nothing goes unlisted; the C++ application's removal detached a Strategy and everything under it, and only the status message said so |
| F4 | The panel offers the chosen problem's repairs above the list, as step 5's Repair is (U5), each worded by what it does: "Remove the line from G3 to G1", "Renumber G3 as G7". A repair that this instance of the problem cannot have is not offered, and the panel says why | One action per repair where there is more than one, for a cycle. The core sends a repair as a code with its arguments; the words are the frontend's |
| F5 | AF-GSN-009: the first node in document order keeps the identifier; each other gets the next free identifier of its own prefix (W10), by W15, in one repair | The one the file lists first is the one a reader most likely knows it by. An ACP labelled with a renumbered goal follows it (A4) |
| F6 | AF-GSN-007: where it is unambiguous, the Strategy becomes the reasoning of one inference from the Goals under it to the Goal above it, and the relationships that had it at an end are deleted. Where it is not (several Goals above it, a relationship something refers to, a counter, a citation, a Strategy that is already some inference's reasoning), refused, saying why | Decided by the user on 28 November 2026, over removing the line: it keeps the structure the author drew. No sample has it; the shapes are fixed by fixtures before the command, and the mapping row records them with SACM 2.3 clause 11.13 (an inference's `reasoning`) and R8 as evidence |

### The repairs (F1)

| Code | The argument breaks it when | The repair |
|---|---|---|
| AF-GSN-001 | Nodes support each other in a circle | Each `SupportedBy` line among them is offered: Remove Line (F2), the user choosing which |
| AF-GSN-002 | A Solution, Context, Assumption or Justification is supported | Remove the line (F2) |
| AF-GSN-003 | A Solution, Context, Assumption or Justification has context | Remove the line (F2) |
| AF-GSN-004 | A Context, Assumption or Justification supports | Make it context (W27, step 5); with a Strategy as the reasoning, which W27 does not repair (U7), remove the line (F2) |
| AF-GSN-005 | A Goal, Strategy or Solution is given as context | Remove the line (F2). Making it support instead would decide that the author meant support, which the file does not say |
| AF-GSN-006 | A Strategy is supported by something other than a Goal | Remove the line (F2) |
| AF-GSN-007 | A Strategy stands at an end of a relationship | Make it the reasoning (F6) |
| AF-GSN-008 | A Strategy supports nothing | None (out of scope above) |
| AF-GSN-009 | Two or more nodes have one identifier | Renumber all but the first (F5) |
| AF-GSN-010 | An undeveloped element is supported | Clear the mark (W26, as Element Properties does) |

## The pieces

| Piece | Where |
|---|---|
| Removing a line, renumbering, making a Strategy the reasoning, and which repairs a problem has | `af-gsn` (`commands`) |
| The commands, each one revision and one undo step | `af-session` |
| `Problem.repairs`, a list in place of the one `repair`; the calls in both shells | `af-api`, `af-wasm`, `af-desktop` |
| Remove Line on a line's menu, the repairs above the Problems list, both languages | `ui/` |

## The goals

- [ ] **G1.** Removing a line (F2, F3): the rule and its mapping row, the
      command with tests, and Remove Line on a line's menu, with what it
      refuses and why, the ACP it takes listed and asked about. Both
      languages, browser tests.
- [ ] **G2.** The repairs that reuse a command: AF-GSN-001, 002, 003, 005
      and 006 by removing a line, AF-GSN-004 under a Strategy too, and
      AF-GSN-010 by clearing the mark; `Problem.repairs`, and the panel
      offering each (F4).
- [ ] **G3.** Renumbering a duplicated identifier (F5), with its mapping row.
- [ ] **G4.** Making a Strategy at an end the reasoning (F6): the shapes from
      fixtures, the mapping row with its evidence, the command and its
      refusals.
