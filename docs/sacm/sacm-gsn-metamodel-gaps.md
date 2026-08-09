# GSN v3 / SACM metamodel gaps — analysis for the SCSC ACWG and the OMG SACM RTF

**Purpose.** Assurance Forge implements GSN v3 features — Assurance Claim Points
and dialectics/Challenge — that the current GSN metamodel cannot express and that
SACM 2.3 can only partly express. This document records where each standard falls
short and why, so it can be sent as an actionable change proposal.

**This is time-sensitive.** The OMG SACM 2.4 RTF is active, and two open issues
would remove the only SACM mechanisms these features currently rest on. The
window to influence that is now open and will not stay open.

## Executive summary

| Finding | Consequence |
|---|---|
| GSN metamodel v2.2 forbids `isCounter` and non-`asserted` declarations | Dialectics, Challenge and Defeat are **unrepresentable in conformant GSN**, though SACM 2.3 supports them |
| `ArtifactReference`, `ArgumentReasoning` and `ArgumentAsset` are **not** `Assertion`s | ACP and Defeat on a **Solution, Context or Strategy** are unrepresentable in **SACM 2.3 itself** |
| SACM24-38/-50: `metaClaim` removed from 2.4 | The only native ACP carrier is disappearing; `Claim.subject` (SACM24-83) is the drafted replacement |
| SACM24-12: proposes removing `defeated` from `AssertionDeclaration` | The only native Defeat carrier is disappearing |
| SACM 2.4 draft §15.1 `Join` has `lowerBound`/`upperBound` | Choice "m of n" is **already solved** in draft — align, don't reinvent |

The two SACM issues are not hypothetical; both were verified directly against
issues.omg.org (2026-07-20). SACM24-50 states `metaClaim` *"was removed from the
text in a previous issue resolution but not from the diagram"* — i.e. removal is
already decided. SACM24-12 argues `AssertionDeclaration` *"has a number of
orthogonal ideas that should not be in the enumeration (namely defeated and
asCited)"*.

So the posture toward OMG should not be "please keep these". It should be: *here
is the GSN v3 requirement; if `metaClaim` and `defeated` go, these are the
replacements it needs.*

## The version problem

| Artifact | Version | Date | Models |
|---|---|---|---|
| GSN Community Standard | **v3** (SCSC-141C) | 2021 | ACP, dialectics, Choice cardinality |
| GSN **Metamodel** Specification | **v2.2** | July 2021 | GSN Community Standard **v2** |
| OMG SACM | 2.3 (`formal/23-05-08`) | Oct 2023 | — |
| OMG SACM | 2.4 | unpublished | RTF active, 59 open issues |

The metamodel was published roughly two months *after* GSN v3 yet explicitly
models v2 ("version2 of the GSN standard", footnote 2 → SCSC-141B). It absorbed a
few v3 additions — `AwayAssumption`, `AwayJustification`, `isPublic`,
`uninstantiated` — while omitting every structural one, and has not been revised
in the five years since. **No draft v3 metamodel exists in public.**

## The structural fact that drives everything

In SACM 2.3, `Assertion` owns **both** `assertionDeclaration` and `metaClaim`,
and is extended by exactly `Claim` and `AssertedRelationship`. `ArtifactReference`
and `ArgumentReasoning` extend `ArgumentAsset` **directly**:

| GSN element | SACM supertype | Has `assertionDeclaration` / `metaClaim`? |
|---|---|---|
| Goal, Assumption, Justification, Away{Goal,Assumption,Justification} | `Claim` | **yes** |
| SupportedBy, InContextOf | `AssertedRelationship` | **yes** |
| Solution, AwaySolution, ModuleReference, ContractModuleReference | `ArtifactReference` | **no** |
| Context, AwayContext, Choice | `ArgumentAsset` | **no** |
| Strategy | `ArgumentReasoning` | **no** |

Therefore **"ACP on a Solution", "ACP on a Context", "defeat a Strategy" have no
SACM 2.3 representation at all** — before GSN's own constraints even apply. Those
are exactly the elements GSN v3 most wants to decorate. This is a SACM gap, not
merely a GSN one, and it is the most consequential technical result here.

## Classification

- **(a)** Expressible in SACM 2.3, but the GSN metamodel forbids it — remove a constraint.
- **(b)** Expressible in SACM 2.3, but the GSN metamodel has no class — add a class.
- **(c)** Not expressible in SACM 2.3 — needs new modelling.

| # | GSN v3 construct | Class | Blocking evidence |
|---|---|---|---|
| 1 | Defeated on Goal / Assumption / Justification / Away* | **(a)** | `AssertionDeclaration::defeated` exists. Blocked by v2.2 `Assertion: self.assertionDeclaration = asserted` |
| 2 | Defeated on SupportedBy / InContextOf | **(a)** | `AssertedRelationship` inherits `assertionDeclaration`. Same single OCL line |
| 3 | **Defeated on Strategy / Solution / Context / Choice** | **(c)** | **Normative v3 §1:6.3.12** requires the defeated decorator on a *Strategy*: challenging a multi-`SupportedBy` inference "requires a strategy to be inserted… the defeated decorator… is applied to indicate that the strategy S1 is no longer valid". Strategy maps to `ArgumentReasoning`, which is not an `Assertion` — **no attribute exists to set** |
| 4 | Challenge relationship | **(a)** + residual **(c)** | `AssertedRelationship.isCounter` exists; blocked by v2.2 `self.isCounter = false`. Residual: a challenge *sourced at a Solution* needs SACM24-82 |
| 5 | ACP on SupportedBy / InContextOf | **(a)** | `Assertion.metaClaim : Claim[0..*]` fits exactly; struck through in v2.2 Figure 2 |
| 6 | ACP on Assumption / Justification | **(a)** | Same; both are `Claim`s |
| 7 | **ACP on Solution / Context** | **(c)** | **Normative v3 §1:5.2.2**: ACPs "may also be added to any element of an argument that provides a reference to an artefact e.g. solution or context". Those are `ArtifactReference`/`ArgumentAsset`, not `Assertion`s → no `metaClaim`. Already open at OMG as **SACM24-83** |
| 8 | ACP identifier label | **(c)** | v3 §1:5.2.3: each ACP "should have a unique identifier", module-qualified as `ACP1[Confidence]` when the confidence argument lives elsewhere. Would live in `TaggedValue`; v2.2 Base OCL disables it (`self.taggedValue.isUndefined() = true`) |
| 9 | Choice cardinality "m of n" | **(c)** | v3 §1:3.2.2: optional label gives "the cardinality of the relationship… If no label is included then the cardinality can be any value from one to the number of supporting elements". v2.2 `Choice` has no attributes. SACM 2.4 draft §15.1 `Join.lowerBound/upperBound` solves it |
| 10 | Module interfaces (v3 §1:4.6) | **(a)** | `ArgumentPackageInterface` exists; v2.2 disables it in the `ArgumentPackage` OCL |
| 11 | Architecture View symbols | **(c)** | Diagrammatic; lands in SACM 2.4 draft §15.3 (SACM24-78, -106) |
| 12 | Element identifier now mandatory | **(b)** | Maps to `name`; needs only a multiplicity constraint |
| 13 | Pattern Definition (v3 §1:3.4) | **(c)** | No GSN or SACM class |

Rows 1–9 are verified against **GSN Community Standard v3 (May 2021)** directly.
Rows 10–13 derive from the public v2→v3 changes deck and the v3 contents; they are
sound but were not traced to a specific normative clause.

The two strongest items are rows 3 and 7, because in both cases GSN v3 *normatively
requires* a decoration on an element that SACM 2.3 structurally cannot carry. These
are not speculative interoperability wishes — they are worked examples in the
standard that no conformant SACM 2.3 file can represent.

## Proposed remedies

### To the SCSC ACWG

1. **Scope the `isCounter` prohibition.** v2.2's OCL is already per-class
   (`Context::SupportedBy`, `Context::InContextOf`), so adding a `Challenge`
   class extending `AssertedRelationship` requires **no edit to existing
   constraints** — a genuinely low-cost change.
2. **Relax `Assertion: self.assertionDeclaration = asserted`** so Defeat can be
   expressed for GSN v3.
3. **Re-enable `ArgumentPackageInterface`.** v3 §1:4.6 (module interfaces) is
   normative; v2.2 forbidding it is a defect, not a design choice.
4. **Add an ACP class.** Typing its target as `ArgumentAsset` reaches Solutions,
   Contexts *and* relationships uniformly, sidestepping the `Assertion`
   inheritance problem in rows 3 and 7, and surviving the `metaClaim` removal:

   ```
   class AssuranceClaimPoint extends argumentation.ArgumentAsset {
     attr String acpIdentifier;
     ref  argumentation.ArgumentAsset[1] annotatedElement;
     ref  gsn.Module[0..1] confidenceArgument;
   }
   ```
5. **Adopt SACM 2.4's `Join` bounds for Choice** rather than inventing a syntax,
   so GSN and SACM converge:
   `lowerBound : Integer = 0`, `upperBound : UnlimitedNatural = *`.

### To the OMG SACM RTF

6. **On SACM24-12** — support moving defeat *off* the `AssertionDeclaration`
   enumeration, but ask that it land on `ArgumentAsset` rather than being
   dropped. That satisfies the "orthogonal ideas" objection **and** fixes row 3
   (defeat on Solutions and Strategies), which no current mechanism supports.
   This is the ask most likely to succeed because it gives the RTF what it
   already wants.
7. **On SACM24-82 / SACM24-83** — GSN v3's requirement to attach ACPs to
   Solutions is direct supporting evidence for both. SACM24-83 already states
   that *"confidence claims are about elements in the SACM Assurance Case itself
   and currently cannot be referenced"*, and both issues carry **drafted chapter
   text**: -82 introduces an abstract `Targetable` and an `isDefeated` boolean,
   -83 introduces `Claim.subject : SACMElement[0..*]`. `subject` typed on
   `SACMElement` would reach Solutions, Contexts and Strategies — i.e. it fixes
   rows 3 and 7 — so the useful contribution is evidence that the typing must
   stay that broad, not a fresh proposal. All 59 RTF issues remain formally
   `open` with no disposition, so this is still influenceable.
8. **On SACM24-50** — if `metaClaim` is removed, a replacement carrier for
   confidence claims is needed, on `ArgumentAsset` rather than `Assertion`.

## Defects worth reporting regardless of v3

- **The v2.2 constraints and figures disagree.** `metaClaim`, `isCounter`,
  `TaggedValue`, `Claim`, `ArgumentGroup` and the five `Asserted*` classes are
  disabled **only by red strikethrough in Figures 2 and 3** — there is no
  corresponding OCL for most of them, though §2 promises constraints "specified
  using OCL". A tool implementing the OCL and a tool implementing the figures
  will produce mutually incompatible files.
- **`Choice` is excluded by the OCL it exists to serve.** §2.1 says Choice exists
  "to enable its connection to multiple SupportedBy relationships", but the
  `SupportedBy` constraint lists Choice as neither a valid source nor target.
- **`AwayContext` is inconsistent between prose and ecore.** The v2.2 prose and
  the ACWG transformation rules give `ArtifactReference`; both published ecores
  declare `ArgumentAsset`. Implementers cannot tell which is normative.
- **Namespace versioning is a trap.** `http://scsc.acwg.gsn/2.0` is current and
  supersedes `http://acwg.org/3.0/gsn` despite the lower number. Our own
  implementation initially recognised only the older namespace and silently
  failed to parse current-specification files.
- **A conformant GSN v2.2 model has no extension point at all** — `taggedValue`,
  `note`, `gid`, `abstractForm` and `implementationConstraint` are all disabled.
  Tools with anything to record are forced outside the standard.

## What Assurance Forge does today

Recorded so the report reflects a real implementation rather than a thought
experiment. Both encodings survive a strict SACM 2.3 round-trip in `libs/sacm`
(`SACM23_ARG_001_ChallengeAndAcpEncodingsSurviveStrictRoundTrip`).

| Concept | Encoding | Standing |
|---|---|---|
| Challenge | `isCounter="true"` on `AssertedInference` / `AssertedEvidence` | SACM-native; **non-conformant GSN v2.2** |
| Defeat | `assertionDeclaration="defeated"` | SACM-native; at risk from SACM24-12 |
| ACP | vendor `TaggedValue` keyed `assuranceForge.acp` | Legal SACM (clause 8.12) but **private** — unreadable by any other tool |

The ACP row is the interoperability argument in miniature: with no standard
representation, every vendor invents an incompatible one.

## Editing-time findings (Phase 9 library migration)

Routing Assurance Forge's edit operations through `libs/sacm` (the library that
enforces SACM 2.3 structurally) surfaced two tensions the legacy in-memory model
hid:

- **A bare Strategy is invalid SACM.** GSN editing adds a Strategy *before* the
  sub-goals it will organise, so at creation the strategy's `AssertedInference`
  has a `reasoning` and a `target` but **no source**. SACM `AssertedRelationship`
  types `source : SACMElement[1..*]` (clause 11.13), so that intermediate state
  is not a conformant instance — the library rightly refuses it, whereas the
  legacy model tolerated it. This is the *editing* counterpart to row 3: not only
  can a Strategy not be *defeated*, an unfinished Strategy cannot be *represented*
  at all. GSN's incremental construction workflow and SACM's minimum-cardinality
  invariant are in direct conflict; a conformant tool must either defer
  materialising the inference until its first source exists, or the standard must
  admit an incomplete-inference state.
- **GSN Justification has no `AssertionDeclaration`.** The pure-SACM mapping for a
  Justification is `assertionDeclaration = axiomatic` (see
  `sacm-gsn-mapping.md`); SACM 2.3 has no `justification` literal. Assurance
  Forge historically wrote a non-standard `justification` value, which no other
  SACM tool can read. The library only accepts the enumerated literals, so the
  migration must choose the standards-correct `axiomatic` and accept that it is a
  deliberate change from the legacy encoding.
- **GSN "delete node, reparent children" has no SACM operation.** Deleting a GSN
  node while keeping its subtree (the NodeOnly mode) means re-pointing the
  children's `SupportedBy` relationships at the deleted node's parent. SACM's
  editing model has no retarget/move on an `AssertedRelationship` -- a
  relationship's `source`/`target` are set at creation. A SACM-native tool can
  only delete-and-recreate the relationships (new ids, losing any per-relationship
  metadata) or the standard must add a retarget operation. Deleting a node *with*
  its descendants, by contrast, composes cleanly from per-element deletes.
- **Deleting one sub-goal of a GSN Strategy scrubs a `source`, now handled by a
  scrubbing delete policy (gap closed).** The standards-correct encoding gives a
  Strategy a single `AssertedInference` whose `source` list holds *all* its
  sub-goals (clause 11.13, `source : SACMElement[1..*]`). Removing one sub-goal
  means removing one entry from that list. There is still no *general* per-end
  retarget/move operation on an `AssertedRelationship`, but *source removal as a
  consequence of deleting the sub-goal* is now expressed by the library's
  `ReferenceDeletePolicy::ScrubReferences` on `DeleteElement`: deleting the
  sub-goal scrubs it out of the inference's `source` list and keeps the
  inference as long as at least one source (or, for an inference, its reasoning)
  and one target survive, dropping the relationship only once scrubbing empties
  it. Measured in Assurance Forge: with `sources = {G3, G4}`, deleting `G3` now
  keeps the inference with `sources = {G4}` (the intended result), whereas the
  older `DeleteReferencingRelationships` cascade removed the inference entirely.
  The asymmetry with `AddRelationshipSource` (add a source but no explicit
  *remove-a-source* command) remains for the case of editing a source without
  deleting the sub-goal element itself; deletion-driven source removal is
  covered.

## Submitting this

- **SCSC ACWG** — chair Jane Fenn (BAE Systems), `jane.fenn@scsc.uk`; the named
  route per `scsc.uk/acwg`. The GSN standard is maintained by GSN_SWG, a
  sub-group of ACWG. No public issue tracker or mailing list exists; drafts and
  minutes need Contributor status, which is granted by request after free
  registration.
- **Ran Wei** (`rw741@cam.ac.uk`, Cambridge) authored both the metamodel
  specification and the ecore and sits on the SACM RTF — the highest-leverage
  technical contact, being on both sides.
- **OMG** — anyone may raise a SACM issue, and the RTF is demonstrably active.
  File in parallel; the GSN v3 requirements are supporting evidence for the
  already-open SACM24-82 and SACM24-83.

  Two separate submissions, not one. This page argues for *capability* the
  metamodels lack. [SACM 2.3 specification defects](sacm-23-specification-defects.md)
  reports seven concrete errors in the published 2.3 text and model — a clause
  printing its neighbour's constraints, four mutually contradictory statements of
  one rule, an OCL expression that does not parse, two clauses that cannot both
  be satisfied, and an attribute the text declares and the model omits. Those are
  corrections rather than proposals, they need no agreement about GSN v3, and
  they are the cheaper thing for an RTF to accept.

## Evidence base

- GSN Metamodel Specification v2.2 — https://scsc.uk/file/gc-main/GSN_metamodelV2-2-1210.pdf (CC-BY-4.0, quotable and adaptable with attribution)
- GSN v2→v3 changes — https://scsc.uk/file/gc-main/GSNv2-to-v3_changes-1092.pdf (no inline licence; cite by URL and date)
- GSN Community Standard v3, May 2021 — read directly (§1:3.2, §1:5.2, §1:5.3, §1:6.3 cited above). SCSC copyright; cite, do not redistribute. Note the `scsc.uk/r141C` shortlink is broken and serves GSN **v1** (2011)
- OMG SACM open issues — https://issues.omg.org/issues/spec/SACM/2.3 (SACM24-12 and SACM24-50 verified 2026-07-20)
- OMG SACM 2.3 `formal/23-05-08`, `ptc/22-03-13` — normative; inheritance claims above verified against the vendored model
- `docs/sacm/sacm-2.3-metamodel-inventory.md`, `docs/sacm/sacm-gsn-mapping.md`
