# Implementing GSN in Assurance Forge

Assurance Forge is being rebuilt in Rust. `ARCHITECTURE.md` at the repository
root is the description of the code; this page says where GSN work goes in it.

## Where GSN lives

| Concern | Crate |
|---|---|
| GSN view of a SACM document, GSN commands, identifiers, well-formedness checks | `af-gsn` |
| Layout, the positioned render model and SVG export | `af-layout` |
| The problem list that reports well-formedness and quality findings | `af-problems` |
| SCCG guideline catalogue and checks | `af-sccg` |

Most crates are planned and not yet built. Check the "Built" column in
`ARCHITECTURE.md` before assuming one exists.

## Rules for GSN code

- **There is no GSN model.** The SACM document (libSACM's `Document`) is the
  only model. A GSN view is derived from it by a pure function and never written
  back. Do not add a struct that holds editable GSN state.
- **A GSN edit is a recipe of SACM operations.** "Add a Strategy under this
  Goal" expands into libSACM operations and is applied all or not at all. The
  recipe is where a mapping decision becomes code, so it must match a row in
  `docs/standards/sacm-gsn-mapping.md`.
- **Only offer what the library accepts.** The interface asks the core what is
  allowed at a selection. A GSN construct with no SACM representation is refused
  with a code, not approximated.
- **One geometry for canvas and export.** Layout produces positioned shapes that
  both the canvas and the SVG export draw. A decorator is added once, in
  `af-layout`. The C++ application had two renderers, and a decorator added to
  one was silently missing from the other; do not reintroduce a second.
- **Codes, not sentences.** A finding is a code with arguments. The frontend
  produces the text and translates it.
- **`af-gsn` does no I/O** and must build for `wasm32-unknown-unknown`.

## The C++ application as a reference

The `cpp` branch holds a working implementation of the current GSN feature set.
Use it to learn how a case behaved, and read its tests for edge cases. Do not
port it line by line: its flat parser model and its projections are what the
rebuild removes.

Useful places on that branch:

| There | What to learn from it |
|---|---|
| `src/core/element_factory.cpp` | Identifier generation and what each GSN element creates in SACM |
| `src/core/problems/gsn_wellformedness.cpp` | The well-formedness rules as implemented, with requirement IDs |
| `src/core/problems/argument_cycles.cpp` | Circular-argument detection |
| `src/ui/gsn/` | Shapes, decorators and layout as drawn |
| `libs/sacm/tests/data/` | SACM fixtures, including GSN-profiled files |

The capability and GSN conformance matrices of that version are kept under
`docs/reference/`.

## Before you finish a GSN change

- Does the change make or alter a mapping decision? Then
  `docs/standards/sacm-gsn-mapping.md` needs a row with its evidence.
- Does it meet a limit of the standards? Record it in
  `docs/standards/sacm-gsn-metamodel-gaps.md`.
- Is there a test that reads a file, applies the change, writes it and reads it
  back with the same meaning?
- Is `ARCHITECTURE.md` still accurate?
