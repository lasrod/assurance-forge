# Milestone 2, step 1: adding elements

**Authority: plan.** This page says what the first editing piece of the
application is, how each part of it is checked, and in what order it is built.
It is short-lived: when the last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and the mapping record, and this page is removed.

## The outcome

Right-click a node of the diagram, or the empty canvas, and a menu lists every
kind of GSN element: Goal, Strategy, Solution, Context, Assumption and
Justification. The kinds GSN allows there can be chosen; the others are shown
greyed out, with the reason. Choosing one adds the element, joined to the node
by the right relationship, as SACM operations on the one document. Edit ▸ Undo
and Redo take it back and bring it again.

It is the first step of milestone 2 (Edit) in the
[Phase 0 release plan](rust-rebuild-phase-0.md#releases), and it builds the
first part of `af-session`.

### Done when

1. On every sample, each choice the menu enables adds an element that the
   reading rules draw as the kind chosen, joined to its parent as chosen, with
   no new code from the view (`AF-VIEW-*`) and a document that libSACM's
   validation accepts as before.
2. Each choice the menu greys out has a reason code, and the rule that greys it
   is tested on each kind of node.
3. Undo returns the document to what it was before the add, `==` by libSACM's
   compare; Redo brings the add back.
4. The menu works from the pointer and from the keyboard (the context-menu key
   and Shift+F10 on the selected node), in the desktop and the web application,
   in English and Japanese, with browser tests in Chromium, Firefox and WebKit.
5. CI is green, the mapping record has the writing rules below, and
   `ARCHITECTURE.md` says what is built.

### Out of scope

Saving and the recovery copy (the next step), editing an element's text,
deleting, moving, challenges, adding by drag and drop, and the Argument
Navigator's own menu. The new element has no name and no statement until text
can be edited; it shows its identifier.

## What may be added where

GSN v3 (Core, Part 1) allows `SupportedBy` from a Goal or a Strategy to a Goal,
a Strategy or a Solution, and `InContextOf` from a Goal or a Strategy to a
Context, an Assumption or a Justification. A Solution, a Context, an Assumption
and a Justification are leaves. The C++ application's rule (`CanAddChildElement`)
and its conformance record (GSN3-CORE-015) agree.

| Right-clicked | Goal | Strategy | Solution | Context, Assumption, Justification |
|---|---|---|---|---|
| Goal | yes | yes, with a first sub-goal (W2) | yes | yes |
| Undeveloped Goal | yes, and it is then developed (W11) | yes, likewise | yes, likewise | yes |
| Strategy | yes (W3) | greyed: no SACM form decided | greyed: no SACM form decided | yes |
| Solution, Context, Assumption, Justification | greyed: a leaf | greyed: a leaf | greyed: a leaf | greyed: a leaf |
| A neutral node (its GSN kind is not known) | greyed | greyed | greyed | greyed |
| The empty canvas | yes, a top-level Goal | greyed: needs a Goal | greyed: needs a Goal | greyed: needs a Goal |

"Greyed: no SACM form decided" is not a GSN refusal. GSN allows a Strategy and a
Solution under a Strategy, but this project has not recorded how SACM holds
them:

- **A Solution under a Strategy.** The SCSC's transformation of GSN to SACM
  (*Relationship of GSN to SACM*, Annex A, rule `Strategy2ArgumentReasoning`)
  writes it as an `AssertedEvidence` from the Solution to the Strategy's goal,
  with the Strategy as its `reasoning`. Reading rule E8 draws a `reasoning` on
  any relationship other than an inference without it, so such a file would
  read back as a Solution under the goal. Enabling the choice means changing
  E8 first, a reading-rule change for the owner to confirm.
- **A Strategy under a Strategy.** SACM can hold it (an asserted relationship's
  ends are `ArgumentAsset`s), but no source says how, and the C++ application
  left it open too.

A Strategy whose `ArgumentReasoning` is the reasoning of no inference, or of
more than one, cannot take a sub-goal: there is no one inference to add the
premise to. Its Goal choice is greyed with that reason.

## Writing rules

The inverse of the reading rules (R2 to R11, E1 to E4), so that what is added
reads back as what was chosen. Each add is one `sacm::edit::batch`: all of it is
applied or none of it. The mapping record holds them, with their evidence, as
"Writing GSN as SACM" (W1 to W14); in short:

| Rule | Added | SACM operations | Evidence |
|---|---|---|---|
| W1 | Goal under a Goal | `CreateClaim` (asserted); `CreateAssertedInference` from the new claim to the goal | R2, E2 |
| W2 | Strategy under a Goal | `CreateArgumentReasoning`; `CreateClaim` (asserted), its first sub-goal; `CreateAssertedInference` from the sub-goal to the goal, with the Strategy as `reasoning` | R8, E1. See [the bare Strategy](#the-bare-strategy) |
| W3 | Goal under a Strategy | `CreateClaim` (asserted); `AddReference` of it to the `source` of the inference the Strategy is the reasoning of | E1 |
| W4 | Solution under a Goal | `CreateArtifactReference`; `CreateAssertedEvidence` from it to the goal | R9 |
| W5 | Context under a Goal or a Strategy | `CreateArtifactReference`; `CreateAssertedContext` from it to the node | R10, E4 |
| W6 | Assumption under a Goal or a Strategy | `CreateClaim` declared `assumed`; `CreateAssertedContext` from it to the node | R4, E4 |
| W7 | Justification under a Goal or a Strategy | `CreateClaim` declared `axiomatic`; `CreateAssertedContext` from it to the node | R5, E4 |
| W8 | A top-level Goal | `CreateClaim` (asserted) | R2 |
| W9 | Where it goes | The new element and relationship go into the `ArgumentPackage` that holds the right-clicked node; a top-level Goal into the first `ArgumentPackage` in document order. With none, the choice is greyed | C++ `apply_add_child` |
| W10 | Its identifier | `CreateTaggedValue` `assuranceForge.gsn.identifier` on the new element: the kind's prefix (G, S, Sn, C, A, J) and the lowest number above every identifier of that prefix in the document | T1; C++ capability AF-GSN-015 |
| W11 | Support under an undeveloped Goal | Also `SetAssertionDeclaration` `asserted` on the goal, in the same batch. The menu says so | R3; GSN well-formedness rule 10 |
| W12 | Name and statement | An empty `name` on each new element and relationship, and no description: the editor invents no text | SACM §8.6 |
| W13, W14 | A Solution, a Strategy, under a Strategy | Not offered: the open questions above | |

No vendor tag records a GSN type: each kind is told by the reading rules from
standard SACM alone.

### The bare Strategy

GSN builds a Strategy before the goals it organises. SACM cannot hold that
state: an `AssertedInference` needs a `source` (`source : SACMElement[1..*]`,
SACM 2.3 clause 11.13), and a Strategy is held only as an inference's
`reasoning`. The SCSC transformation has no answer either: for a Strategy with
no sub-goals it discards the inference, which leaves an `ArgumentReasoning`
joined to nothing and loses the goal it was added under. The C++ application
kept that goal in a vendor tag (`strategyTarget`) until the first sub-goal
arrived, so the file held a Strategy that another SACM tool sees joined to
nothing.

Adding the Strategy with its first sub-goal is the only one of these that is
valid SACM and well-formed GSN at every step: GSN's own well-formedness asks
that a Strategy be supported. The gap is already recorded in
`docs/standards/sacm-gsn-metamodel-gaps.md` (editing-time findings). Other
tools were not checked.

## The pieces

| Crate or part | Adds |
|---|---|
| `af-gsn` | `commands`: the choices for a node or the canvas, each with its reason code when greyed, and the batch for each add (W1 to W14) |
| `af-session` (new) | The open document, its revision, and undo and redo as whole-document snapshots (`Document` is `Clone`). Each accepted command is one change record: revision, command, the ids created and changed, and its origin. Pure: no I/O |
| `af-api` | `add_choices(target)`, `add(target, kind)`, `undo()` and `redo()`, through the session; the TypeScript bindings |
| `af-wasm`, `af-desktop` | The same four, on both shells |
| DATUM | A context menu (it has none): opened at the pointer or from the keyboard, items that can be disabled with a reason. Built in DATUM, not in the application |
| `ui/` | The menu on the canvas, Edit ▸ Undo and Redo with Ctrl+Z and Ctrl+Y (⌘Z and ⇧⌘Z), the added node selected and brought into view, the reasons in English and Japanese |

### Codes

The core sends codes and the interface words them.

| Code | A choice is greyed because |
|---|---|
| AF-EDIT-001 | The node is a leaf in GSN: a Solution, Context, Assumption or Justification |
| AF-EDIT-002 | The node's GSN kind is not known (a neutral node) |
| AF-EDIT-003 | GSN allows it, but no SACM form is decided yet |
| AF-EDIT-004 | The Strategy is the reasoning of no inference, or of several |
| AF-EDIT-005 | There is no argument package to add to |
| AF-EDIT-006 | Only a Goal can be added at the top level |
| AF-EDIT-007 | Nothing is open (an edit sent with no file) |
| AF-EDIT-008 | There is nothing to undo, or nothing to redo |

### Revisions and the interface

An add or an undo makes a new revision. The interface keeps the view where it
is (no fit), selects the added node and brings it into view, and keeps the
Argument Navigator's open branches: they reset only when another file is opened,
not on a new revision of the same file.

## The goals

1. **G1 Commands.** `af-gsn` choices and batches, with tests on hand-written
   cases and every sample (done when 1 and 2). Mapping record rows W1 to W14.
2. **G2 Session.** `af-session`, with the change record and undo and redo
   (done when 3).
3. **G3 Contract.** `af-api`, both shells, the bindings.
4. **G4 DATUM context menu.** In DATUM, packed into `ui/vendor/` as before.
5. **G5 Interface.** The menu, Edit ▸ Undo and Redo, words, browser tests (done
   when 4), checked in the running application.

G1 to G3 are one pull request; G5 is stacked on it once G4 is in DATUM.

## What this leaves for the next steps

Save (the desktop writes the file, the browser downloads it) and the recovery
copy; editing an element's name and statement; delete; the open questions above
(a Solution or a Strategy under a Strategy).
