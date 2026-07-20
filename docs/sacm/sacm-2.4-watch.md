# SACM 2.4 watch list

**Nothing here is normative, and nothing here is implementable.** SACM 2.3
(`formal/23-05-08`) is the only published SACM. Every row below is *draft* text
from an *open* OMG RTF issue and may change or be dropped.

**These rows must never appear in `sacm-conformance-matrix.md`.** That matrix
records 2.3 compliance; mixing in speculative 2.4 rows would let an unbuilt,
unpublished requirement look like a conformance claim. The `SACM24-WATCH-*`
prefix exists so the two can never be confused, and the `sacm_matrix_check`
CTest only ever reads the 2.3 matrix.

## Why we track this

Our project decision is to support SACM 2.3 only, while not making future
version support impossible. Watching 2.4 tells us which of today's design
choices would be expensive to unwind — and it has already changed one decision:
we now know SACM 2.4 removes `metaClaim`, so building Assurance Claim Point
support on it would have been building on sand.

## Status of the revision

- Issue list: https://issues.omg.org/issues/spec/SACM/2.3 — 59 issues, **all
  status `open`**, all against "SACM 2.3b1" (checked 2026-07-20).
- The `/fixed` endpoint contains no `SACM24-*` item, so **no 2.4 change has a
  formal disposition**. Claims that something is "decided" are not supportable
  from the public tracker, though SACM24-58 refers to "the modifications from
  the first two ballots", so provisional agreement exists that the tracker does
  not show.
- **No 2.4 beta exists.** `omg.org/spec/SACM/2.4/` and its variants 404. Issue
  attachments (`issues.omg.org/secure/attachment/<id>/<name>.pdf`, public
  despite the path) are the only source.
- No published RTF schedule or target date.

## Changes that would break a 2.3 implementation

| ID | Issue | Change | Our exposure |
|---|---|---|---|
| WATCH-001 | SACM24-12 | `AssertionDeclaration` → `AssertionDeclarationKind`; `defeated` and `asCited` leave the enum | `model::AssertionDeclaration` |
| WATCH-002 | SACM24-48/73/82 | `defeated` becomes `isDefeated : Boolean` on a new abstract `ArgumentConcept` | as above |
| WATCH-003 | SACM24-38, -50 | `metaClaim` removed from `Assertion` | `Assertion::meta_claims()`, `AddMetaClaim` |
| WATCH-004 | SACM24-83 | New `Claim.subject : SACMElement[0..*]` — the metaClaim replacement | future ACP work |
| WATCH-005 | SACM24-15 | `AssertedArtifactSupport` and `AssertedArtifactContext` **deleted** | two `ElementKind`s |
| WATCH-006 | SACM24-49, -82 | New abstract `Targetable`; relationship source/target retyped and restricted in the metamodel rather than by OCL | validation rules |
| WATCH-007 | SACM24-17 | `gid` → mandatory private `elementId : UID[1]`; `isAbstract`→`isSACMAbstract`; `isCitation` becomes derived; `citedElement`→`cited` | `SACMElement` identity |
| WATCH-008 | SACM24-13 | Base renames: `UtilityElement`→`BaseElement`, `Note`→`SACMComment`, `TaggedValue`→`NamedValue`, `ImplementationConstraint`→`SACMConstraint` | XMI element names, `ElementKind` |
| WATCH-009 | SACM24-2/3/6, -98 | `LangString` merged into `MultiLangString`; moves Base → Terminology | `model::LangString` |
| WATCH-010 | SACM24-4, -37 | `ModelElement.name : LangString[1]` → `elementName : ExpressionLangString[0..1]` + derived `/name` | naming round-trip |
| WATCH-011 | SACM24-8 | `ArgumentationElement` → `ArgumentElement` | XMI names |
| WATCH-012 | SACM24-11 | `XPackageInterface`→`XInterfacePackage`; one `BindingPackage` replaces the per-domain ones | packaging kinds |
| WATCH-013 | SACM24-7, -13 | `ArtifactElement` moves out of Base; `ModelElement` becomes the cross-domain base | type hierarchy |
| WATCH-016 | SACM24-16 | One `Group` replaces the per-domain groups | three `ElementKind`s |
| WATCH-017 | SACM24-53 | `Property` removed, subsumed by `NamedValue` | one `ElementKind` |
| WATCH-018 | SACM24-18/56/57/105 | Package ownership tightened into a **conformance requirement** | validation |

## Chapter 15 — "Advanced SACM Capabilities"

Seven capabilities (15.1–15.7). Headed *(informative)*, yet SACM24-105 makes six
of them compliance points — a contradiction one side will have to resolve.

Two matter to us:

- **§15.1 `Join`** — `joinType`, `isNot`, `lowerBound`, `upperBound`, `joinRule`.
  This is where **GSN Choice "m of n" cardinality lands**, so our gap report
  should ask SCSC to align with it rather than invent a syntax.
- **§15.3 `SACMView` / `SACMDiagram`** — see below.

## §15.3 and our layout policy

`docs/sacm/sacm-layout-policy.md` says layout is not SACM and must not enter the
library. **That decision survives 2.4**: `SACMView` is an optional compliance
point, the mandatory one is Packaging, and strict export at the mandatory point
still needs no diagram data.

What changes is the *fallback*. Our policy currently suggests a sidecar or a
vendor extension if layout ever has to be persisted. Once `SACMDiagram` exists,
a vendor extension is the wrong target — the standard-aligned one is
`SACMDiagram` + `SACMDiagramElement`, which are built on OMG Diagram
Definition/Interchange. Notably SACM itself defines no geometry: it adds only
that a diagram exists, which elements each diagram element denotes, and an
opaque rendered `representation` (e.g. SVG). Coordinates come from DD/DI.

Practical consequence, no code change now: keep the deterministic layout
module's output expressible as (element reference → geometry/representation)
pairs, because that is the shape `SACMDiagramElement` takes.

## What we changed because of this

- Added `StandardVersion` and `LoadResult::source_version`, so the version axis
  is explicit at the API boundary while adding a value is still cheap. Version
  is deliberately **orthogonal to `io::Mode`** — resist any future
  `Mode::Strict24`.
- Implemented `SaveOptions::namespace_uri`. 2.4 introduces no native SACM nsURI
  either — its own Annex B example serializes through the UML Profile — so the
  pin stays a project choice and callers can rebind it.

## What we deliberately did **not** change

- **`AssertionDeclaration` stays a bare enum.** In 2.3 it *is* one, and
  renaming it now would misrepresent the published standard. The exposure is in
  *client* code: nothing outside `libs/sacm` should branch on
  `== AssertionDeclaration::Defeated`. Route that through a predicate and the
  eventual migration is one function body.
- **`meta_claims()` stays.** It is normative 2.3 and must round-trip. 2.4 does
  not generalize it, it deletes it and solves the problem from the other end
  (`Claim.subject`), so abstracting now would buy nothing. Discipline instead:
  don't surface "metaClaim" as an adapter- or user-level concept name.
- **No 2.4 classes, renames, or deletions.** Implementing an open, unballoted
  draft would break 2.3 conformance to chase a moving target.

## Open questions

1. Does `needsSupport` survive? Annex G says "AssertionDeclaration is one of
   axiomatic, assumed, asserted", omitting it — but SACM24-12's stated scope
   removes only `defeated` and `asCited`, and Annex C still gives concrete
   syntax for needsSupport. Unresolved.
2. `assertionDeclaration` multiplicity: some draft diagrams show `[1] = asserted`
   (as 2.3), others `[0..1]`.
3. Chapter 15: informative or normative?
4. SACM24-107 "various cleanup items" has no attachment — content unknown.
5. If 2.4 keeps producing no nsURI while adding a Profile-based Annex B, the
   interchange story arguably regresses for non-UML tools. Relevant to our
   interop work.
