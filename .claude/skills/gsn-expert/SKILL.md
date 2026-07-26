---
name: gsn-expert
description: Goal Structuring Notation domain expertise for Assurance Forge — element and relationship semantics, decorators, the Pattern/Modular/Dialectic/Confidence extensions, well-formedness rules, and how each maps onto SACM 2.3 and onto this codebase. Load before implementing, reviewing, or reasoning about any GSN feature: new element types, decorators, relationship kinds, layout or rendering changes, GSN import/export, argument-quality checks, or any question about what a GSN construct means or whether an argument is well formed.
---

# GSN expert

Goal Structuring Notation is a graphical argument notation. Assurance Forge
renders GSN over a SACM 2.3 model, so almost every GSN decision here is really
two decisions: *what does the notation mean*, and *what carries it in SACM*.
Getting the first right and the second wrong silently reinterprets a safety
argument, which is the one failure mode this codebase exists to prevent.

## Operating rules

**1. Standards precedence.** GSN Community Standard **v3** (SCSC-141C, 2021) is
normative for the notation. The GSN **Metamodel** Specification **v2.2** (July
2021) is normative for interchange but models GSN **v2** — it has never been
revised for v3. Where v2.2 forbids something v3 requires, *the metamodel is the
stale artifact, not our behaviour*. Do not "fix" a v3 feature by removing it to
satisfy v2.2 OCL. Record the conflict in `docs/sacm/sacm-gsn-metamodel-gaps.md`
instead — that document is an active change proposal to the SCSC ACWG and the
OMG SACM 2.4 RTF, and it is where the project's leverage lives.

**2. Never guess a mapping.** Every GSN→SACM mapping in this project is
evidence-backed and recorded in `docs/sacm/sacm-gsn-mapping.md` with its basis
(`[M]` mapping, `[N]` evidenced absence, `[G]` genuine gap). If a construct you
need is not in that table, it is an open decision — raise it, add a row, and say
what the evidence is. Do not pick the plausible option in code.

**3. Preserve rather than reinterpret.** Where the standards are genuinely
undecidable — Context as Claim vs ArtifactReference, AwayContext's supertype —
the library preserves the construct and emits a diagnostic. A parser that picks
is a parser that rewrites the argument. A human in the loop may resolve it; the
code may not.

**4. Two directions are reversed.** GSN `SupportedBy` runs conclusion→premise;
SACM `AssertedInference` runs premise→conclusion. Same for `InContextOf` /
`AssertedContext`. The reader swaps endpoints on import. Any new relationship
code must be explicit about which direction it is in.

**5. Strategy is a node in GSN and an annotation in SACM.** It maps to
`ArgumentReasoning`, which hangs off a relationship rather than connecting
elements. Never assume a GSN node corresponds to a SACM element.

**6. Some things are structurally impossible, not merely unimplemented.** ACP on
a Solution or Context, and Defeat on a Strategy, Solution or Context, have *no
SACM 2.3 representation* — those GSN elements extend `ArgumentAsset`, which has
neither `metaClaim` nor `assertionDeclaration`. Both are normatively required by
GSN v3. Do not design around this by inventing an encoding without recording the
decision; check `sacm-gsn-metamodel-gaps.md` rows 3 and 7 first.

## References

Load the one that matches the task.

| File | Load when |
|---|---|
| `references/notation.md` | You need element/relationship/decorator semantics, extension vocabulary, or the shapes. |
| `references/well-formedness.md` | Reviewing or validating an argument, or building a quality check. |
| `references/implementing-in-assurance-forge.md` | Adding or changing a GSN element, decorator, relationship, layout or export behaviour. |

Repository documents that are authoritative and must not be duplicated into this
skill — read them directly:

- `docs/sacm/sacm-gsn-mapping.md` — the GSN→SACM 2.3 mapping table and its evidence.
- `docs/sacm/sacm-gsn-metamodel-gaps.md` — where the standards fall short, with clause-level citations.
- `docs/sacm/sacm-conformance-matrix.md` — SACM requirement IDs; tests embed them.
- `docs/features/feature-matrix.md` — which GSN capabilities actually ship today.

## Before you finish any GSN change

- Does `docs/features/feature-matrix.md` still describe reality? A new decorator
  or element kind almost always moves a row. The `feature_matrix_check` CTest
  will fail if a `planned` row has acquired tests, but it cannot notice a
  `supported` row that quietly grew a caveat — that judgement is yours.
- Does the change touch a mapping decision? Then `sacm-gsn-mapping.md` needs a
  row, and the SACM conformance matrix may need one too.
- Canvas and SVG export are **separate renderers** with separate models. A
  decorator added to the canvas is not in exported diagrams until it is added to
  `src/export/` as well, and the omission is silent. This has already produced a
  real defect (AF-ENG-015), and not a cosmetic one: the export projected
  challenges as support, drawing counter-evidence as evidence *for* the claim.
- Every user-visible string goes through `AF_TR` / `ui::i18n`.
