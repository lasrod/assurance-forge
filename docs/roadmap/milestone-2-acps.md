# Milestone 2, step 7: assurance claim points

**Authority: plan.** This page says what an assurance claim point (ACP) is in
the editor, how it is stored and checked, and in what order it is built. It is
short-lived: when the last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

A line of the argument carries an ACP: a solid square with its identifier,
`ACP1`, that says a confidence argument exists for that relationship. The
confidence argument is either a short description, or a confidence argument
of its own in a new argument package, which has its own diagram, is listed in
the Case Explorer, and is opened by clicking the ACP.

It is the seventh step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases), the second half of
"assurance claim points and challenges"
([challenges](milestone-2-challenges.md) were the first).

### Done when

1. An ACP is added to a `SupportedBy` or `InContextOf` line from the line's
   menu, as a description or as a new confidence argument, and the file stays
   valid, reads back with the ACP drawn, and writes strictly. Each is one undo
   step.
2. Each argument package has its own diagram. The Case Explorer opens one, and
   a click on an ACP opens the diagram of its confidence argument with its top
   goal selected.
3. An ACP is drawn on its line, labelled `ACP1`, or `ACP1[Package]` when its
   argument is in another package, in the canvas and in the SVG export, from
   one geometry.
4. Where an ACP may not go, the menu does not offer it, or says why: on a
   Solution or a Context it comes with SACM 2.4.
5. It works in the desktop and the web application, in English and Japanese,
   with browser tests in Chromium, Firefox and WebKit.

### Out of scope

ACPs on a Solution or a Context (decided 27 November 2026: SACM 2.3 cannot
hold them, gap row 7; SACM 2.4 can, through a Claim's `subject`, and they come
with the move to 2.4). ACPs on a Goal, an Assumption, a Justification or a
Strategy, which GSN v3 does not define. Linking an ACP to a goal of the
line's own argument package (A8 links one to the top goal of another).
Turning a description into a confidence argument. An ACP across
several lines of one Goal as one mark (§1:5.3.5): each relationship carries
its own.

## What the standards say

Read on 27 November 2026, from GSN v3 §1:5 and SACM 2.3, with the gap report
([`sacm-gsn-metamodel-gaps.md`](../standards/sacm-gsn-metamodel-gaps.md)):

- **Where.** "It can be applied to 'SupportedBy' and 'InContextOf'
  relationships" (Table 1:5-1), and to "elements that make reference to an
  artefact (e.g. solution, context)" (Table 1:5-2). "The placement of an ACP
  on an individual 'SupportedBy' relationship below the strategy is ambiguous
  and should be avoided" (§1:5.3.3): an inference through a Strategy carries
  it on its first line.
- **What it points at.** "The corresponding argument could be located in a
  paragraph of accompanying text, a goal in the local argument, or a goal in a
  separate module" (§1:5.2.3). "The {ACP identifier} may be a reference to a
  goal" (§1:5.3.2).
- **Its label.** "Each ACP should have a unique identifier, e.g. 'ACP1' …
  Where the corresponding argument is located in a separate module, the
  module identifier should be shown alongside the ACP identifier delimited
  with square brackets e.g. ACP1[Confidence]" (§1:5.2.3).
- **SACM 2.3.** `Assertion.metaClaim : Claim[0..*]` fits an ACP on a
  relationship exactly (gap row 5). A Solution or a Context is an
  `ArtifactReference`, which has no `metaClaim` (gap row 7). The GSN Metamodel
  v2.2 has no ACP class and strikes `metaClaim` out.
- **SACM 2.4 (beta).** `metaClaim` is removed (SACM24-38, -50); a Claim's
  `subject` may name any element, which closes row 7. ACPs written now move to
  `subject` with the move to 2.4.
- **Other tools.** gsn2x declares an ACP by name on an element or on a
  relationship (`acp: {ACP1: S1}`), and documents no link from it to another
  diagram. No vendor documentation found for ASCE, AdvoCATE or Astah describes
  their ACP support.

## Decisions

| | Decision | Why |
|---|---|---|
| A1 | An ACP is a `metaClaim` of the relationship a `SupportedBy` or `InContextOf` line is drawn for, naming a confidence `Claim` | SACM's own carrier (gap row 5), readable by any SACM tool. Decided by the user on 27 November 2026, over the C++ application's private `assuranceForge.acp` tagged values |
| A2 | Only on lines, and on an inference through a Strategy only on its first line. A Solution or a Context says it comes with SACM 2.4 | Table 1:5-1 and §1:5.3.3; gap row 7. Decided by the user on 27 November 2026, after reading that SACM 2.4 closes the gap with a different carrier |
| A3 | Added as one of two: **a description**, a `Claim` in the line's package that holds the text and is drawn only as the ACP; or **a confidence argument**, a new `ArgumentPackage` whose top goal is that `Claim` | §1:5.2.3's "paragraph of accompanying text" and "goal in a separate module". Decided by the user on 27 November 2026 |
| A4 | The confidence `Claim` is numbered `ACP1`, `ACP2`…, and the ACP is labelled with its identifier, with `[package name]` when it is in another package | §1:5.3.2 and §1:5.2.3: the identifier is the reference to the goal, so nothing else is stored, and renaming the goal renames the ACP. Decided by the user on 27 November 2026 |
| A5 | The whole case is a diagram, as now, and what a file opens on; each argument package is also a diagram of its own. The Case Explorer's file row opens the whole case and a package's row that package; a click on an ACP opens its confidence argument's package with the goal selected | Decided by the user on 27 November 2026: a confidence argument is usually a diagram of its own (§1:5.3.1: "a separate confidence argument module"). The whole case stays because real files split one argument across packages: 8 of the 49 relationships of `oasc-ja` and 23 of the 128 of the large case join nodes of two packages |
| A6 | A package's diagram draws its nodes and every line with an end among them. A node of another package that such a line reaches is drawn too, marked with its package's name, and the mark opens that package | A line is not cut in two: what the package says about a node elsewhere stays in view, with where that node is. Nothing in the file changes: the mark is of the diagram, not a citation (an away goal is `isCitation`, E9) |
| A7 | Removing an ACP takes its description with it. A confidence argument in a package of its own is kept or deleted with it as the user chooses each time, in a dialog that lists what deleting it removes; a goal of the line's own argument stays | Decided by the user on 28 November 2026, over always deleting the package or always keeping it: the app has no other way to delete a package, and a confidence argument may be worth keeping without its ACP |
| A8 | A line's menu links an ACP to a goal that exists, under Link ACP to: the top goal of each argument package other than the line's, labelled as the ACP will be. A goal already the line's ACP, or one whose argument holds the line, is not offered | Decided by the user on 28 November 2026, after A7 kept confidence arguments with no way to attach them again: top goals of other packages over any goal, and a submenu over a dialog or a click on the goal. §1:5.2.3's "goal in a separate module" |

## The pieces

| Piece | Where |
|---|---|
| A package's diagram, and the ACPs of the view | `af-gsn` (`view`) |
| The square and its label on a line, in one geometry | `af-layout` |
| The commands that add an ACP, and what may carry one | `af-gsn` (`commands`), `af-session` |
| The contract and the two shells | `af-api`, `af-wasm`, `af-desktop` |
| The package diagrams, the Case Explorer, the line's menu, the ACP's click, both languages | `ui/` |

## The goals

- [x] **G1.** One diagram per argument package: the view of one package, the
      Case Explorer opening it, and what a diagram draws of another package.
      The Argument Navigator still lists the whole case; a row the package
      does not draw shows the whole case.
- [x] **G2.** Reading and drawing ACPs: a `metaClaim` of a relationship drawn
      as a square with its label on the line; a description's `Claim` drawn
      only as the ACP; E11 changed and R17 added, with their evidence, and
      the gallery's `acps` fixture.
- [x] **G3.** Adding an ACP from a line's menu, as a description or as a new
      confidence argument, with tests; what may not carry one, and why
      (W33, W34). A confidence argument's package is named "Confidence
      ACP1" in the interface's language.
- [x] **G4.** A click on an ACP: the confidence argument's diagram with its
      goal selected, or the description in Element Properties. Both
      languages, browser tests.
- [x] **G5.** Removing an ACP from its line's menu, or by Delete with its
      description selected: a description goes with it; a confidence
      argument in a package of its own is kept or deleted as the user
      chooses (decided 28 November 2026; W35). One undo step, both
      languages, browser tests.
- [x] **G6.** Linking an ACP to the top goal of another argument package
      from the line's menu (decided 28 November 2026; A8, W36), such as a
      confidence argument G5 kept. One undo step, both languages, browser
      tests.

Known limits, found in review: a package's diagram decides a relationship's
first line against the whole case, so a line whose relationship begins in
another package says to use a first line that diagram does not draw; the
whole case draws it.
