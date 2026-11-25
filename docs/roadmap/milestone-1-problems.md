# Milestone 1, step 2: the problem list

**Authority: plan.** This page says what the Problems panel lists, how each
problem is found and shown, and in what order it is built. It is short-lived:
when the last goal is done, what it decided is folded into
[`ARCHITECTURE.md`](https://github.com/lasrod/assurance-forge/blob/main/ARCHITECTURE.md)
and this page is removed.

## The outcome

The Problems panel lists what is wrong with the open case, and the status bar
counts it. Four things are listed:

- what libSACM reported reading the file;
- what libSACM's validation finds in the document;
- where the argument breaks GSN's rules;
- what the diagram does not draw.

The list follows every edit. Choosing a problem selects the element it is
about and shows it on the canvas.

It is the step [the diagram](milestone-1-diagram.md) left for later ("The
problem list in the bottom panel: load diagnostics, well-formedness, cycles"),
taken now because editing can leave an argument that breaks GSN's rules with
nothing to say so.

The C++ application's panel (capabilities AF-METH-001, 008 and 010) is the
reference for behaviour:

- what it checked;
- its severities;
- its requirement ids;
- that choosing a row selects the element.

Two of its lessons are taken. Content whose GSN meaning is undecidable is not
reported, because a false structural error is worse than a missing one. A
problem names the element once, not once per relationship.

### Done when

1. Each GSN check reports a fixture built to break it, with the element ids it
   names. The same check stays silent on every sample that does not break it.
   A check that never fires on any input is not counted as tested.
2. Every sample's list is the same on each derivation, follows an edit and its
   undo, and is ordered as P6 says.
3. The panel lists the problems in English and Japanese. Choosing one selects
   its element and shows it. The status bar's counts open the panel. Both
   shells, with browser tests in Chromium, Firefox and WebKit.

### Out of scope

- Marks on the diagram's nodes, and filters (the attention model).
- Repairing a problem from the list (milestone 2's defect repair).
- Dismissing a problem.
- Argument-quality prompts, such as an unsupported Goal without the
  undeveloped decorator.
- Modules and away elements, which are not drawn yet.
- Wording all of libSACM's roughly 200 diagnostic templates.

## Decisions

| | Decision | Why |
|---|---|---|
| P1 | Four sources make one list, each problem tagged with its source. **Read**: what libSACM's strict read reported, kept from the open until the next open, since it describes the file as read. **SACM**: libSACM's `validate`, at every revision. **GSN**: `af-gsn`'s well-formedness checks (P2), at every revision. **Diagram**: the view's `AF-VIEW-*` codes, at every revision, as information; the editor's own placeholder (R16) is not listed | The C++ application never listed libSACM's diagnostics. They describe the file the user is about to save, so they belong here. Validation and the view cost under a millisecond on the largest sample, so the list is derived at every revision |
| P2 | The GSN checks of this step, each with the GSN v3 requirement it enforces: see the table below | GSN3-VAL-001: a reader can take a problem back to the standard |
| P3 | **Not reported**: a node under more than one parent (shared support). GSN3-CORE-014 makes a structured argument a directed acyclic support graph, not a tree. Also not reported: anything at a neutral node, whose GSN type the file leaves unknown; the view already says so with its own code | A false structural error is worse than a missing one (the C++ application's lesson) |
| P4 | A cycle is reported once per set of nodes that support each other in a circle (a strongly connected part of the `SupportedBy` graph), naming them in document order. An element that supports itself is a cycle of one. `InContextOf` and challenges are not followed | Every elementary cycle can be exponentially many. The set is what the user has to break. Context and challenges point backwards without being support (AF-METH-008) |
| P5 | A problem is a code with its arguments, and the SACM ids it is about, each with its identifier when it is drawn. The frontend words it in English and Japanese. A libSACM code with no words of its own yet is worded as a refusal is: a general sentence, the arguments that name what is wrong, and its code | The core sends codes, not sentences. libSACM's English message is a sentence, so it is not sent |
| P6 | Order: errors, then warnings, then information. Within each: GSN, SACM, read, diagram. Within a source, in the order found (document order) | The structural defects the user can act on come first |
| P7 | The panel is a DATUM `ListBox`. Each row is a severity mark, the message, and the code with its requirement as meta. Choosing a row selects the first element it names and shows it on the canvas. The panel's title carries the count, "Problems (3)", on its tab and its rail | The C++ application selected and centred the element. A list gives the keyboard and one tab stop |
| P8 | While a file is open, the status bar shows the errors and warnings. Choosing them opens the Problems panel. The open's status line counts the same list | One count, the list's, everywhere |

### The GSN checks (P2)

| Code | Severity | Requirement | The argument breaks it when |
|---|---|---|---|
| AF-GSN-001 | Error | GSN3-CORE-014 | Nodes support each other in a circle (P4) |
| AF-GSN-002 | Error | GSN3-CORE-015 | A Solution, Context, Assumption or Justification is supported (`SupportedBy` from it) |
| AF-GSN-003 | Error | GSN3-CORE-015 | A Solution, Context, Assumption or Justification has context (`InContextOf` from it) |
| AF-GSN-004 | Error | GSN3-CORE-015 | A Context, Assumption or Justification supports (`SupportedBy` to it) |
| AF-GSN-005 | Error | GSN3-CORE-015 | A Goal, Strategy or Solution is given as context (`InContextOf` to it; R6) |
| AF-GSN-006 | Error | GSN3-CORE-015 | A Strategy is supported by something other than a Goal |
| AF-GSN-007 | Error | GSN3-CORE-002 | A Strategy stands at an end of a relationship instead of as its reasoning (R8) |
| AF-GSN-008 | Warning | GSN3-CORE-002 | A Strategy supports nothing |
| AF-GSN-009 | Error | GSN3-CORE-010 | Two or more nodes have one identifier |
| AF-GSN-010 | Warning | GSN3-CORE-009 | An undeveloped element is supported |

- One relationship is reported for the first of AF-GSN-002 to 006 it breaks,
  not for each.
- AF-GSN-008 and AF-GSN-010 are warnings. An undeveloped decorator left on an
  element that is now supported is stale, not contradictory (the C++
  application's severity). A Strategy with no Goal yet is how the editor adds a
  Strategy (W2): the argument is unfinished, not wrong.

## Known limits

- A read diagnostic stays until the file is opened again, even once an edit
  has removed what it was about.
- libSACM's codes are worded by the code, not by each of its templates: the
  arguments name what is wrong.

## The pieces

| Crate or part | Adds |
|---|---|
| `af-gsn` | `wellformed::check(document, view)`: the findings of P2 to P4 |
| `af-problems` (new) | The list: the four sources made one, with severities and order (P1, P6) |
| `af-api` | The read diagnostics kept from the open; `problems()` at the revision, derived once per revision; the counts of `OpenResult`; the TypeScript bindings |
| `af-desktop`, `af-wasm` | `problems` |
| `ui/` | The panel, its count, the status bar's counts, the words in English and Japanese |

## The goals

1. **G1 Checks.** `af-gsn` and `af-problems` (done when 1).
2. **G2 Contract.** `af-api` and both shells (done when 2).
3. **G3 Interface.** The panel and the status bar (done when 3), checked in the
   running application.

All three are one pull request.
