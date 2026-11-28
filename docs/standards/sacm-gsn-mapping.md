# GSN to SACM 2.3 mapping

!!! note "Paths in this page"
    Source and fixture paths such as `libs/sacm/...` name files of the C++
    implementation, on the [`cpp` branch](https://github.com/lasrod/assurance-forge/tree/cpp).
    The mappings and the evidence for them hold for any implementation.

Real-world SACM interchange files are usually **GSN models built on SACM**, not
plain SACM. GSN defines its own concrete classes whose supertypes are SACM
classes, so `xsi:type="gsn_:Goal"` denotes an element that *is a* SACM `Claim`.
Reading such a file therefore involves a transformation, and this page records
what that transformation does and on what evidence — so the decisions stop being
made one at a time.

## Version skew — read this before changing anything here

The three specifications involved move independently, and the GSN metamodel is
the one lagging:

| | Version | Date | Covers |
|---|---|---|---|
| GSN Community Standard | **v3** (SCSC-141C) | current | ACP, Dialectics/Challenge, Choice cardinality |
| GSN **Metamodel** Specification | **v2.2** | July 2021 | GSN Community Standard **v2** only |
| OMG SACM | **2.3** (`formal/23-05-08`) | 2023 | — |

**The metamodel does not cover GSN v3.** Anything v3 introduced — ACP,
Dialectics, Challenge, Choice "m of n" — has no interchange representation, and
v2.2's OCL actively *forbids* the SACM features that would express some of it
(`isCounter = false`, `assertionDeclaration = asserted`).

Two consequences for this library:

- A mapping decision justified only by v2.2 must not be allowed to block GSN v3
  or SACM 2.3 usage. Where v2.2 constrains something that SACM 2.3 permits and
  GSN v3 needs, **the constraint is the stale artifact, not our behaviour.**
- Gaps we hit are tracked in `docs/sacm/sacm-gsn-metamodel-gaps.md` for reporting
  to the SCSC ACWG, rather than being worked around silently in the reader.

## Sources

| Source | Status | Licence |
|---|---|---|
| [GSN Metamodel Specification v2.2](https://scsc.uk/file/gc-main/GSN_metamodelV2-2-1210.pdf) (SCSC ACWG, July 2021) | Normative for GSN | CC-BY-4.0 |
| [Relationship of GSN to SACM](https://scsc.uk/file/gc-main/GSN-and-SACM-v1-1083.pdf) + Annex A ETL rules | Informative, marked legacy (Metamodel V1) | No inline licence — paraphrase only |
| `scsc.acwg.gsn/2.0` ecore (SystemsAssuranceGroup/SACM) | Machine-readable form of v2.2 | No repo licence — study only |
| `acwg.org/3.0/gsn` ecore (wrwei/SACM) | Older revision | Apache-2.0 |
| OMG SACM 2.3 `formal/23-05-08`, `ptc/22-03-13` | **Normative for SACM** | OMG terms |

Fixtures under `libs/sacm/tests/data/sacm23/interop-*` are authored from the v2.2
specification, not copied from tool output.

## Namespaces

Watch the version numbering — it is inverted:

| Namespace | Revision | Notes |
|---|---|---|
| `http://scsc.acwg.gsn/2.0` | **Current** (v2.2 spec) | Adds `AwayAssumption`, `AwayJustification`; spells `Choice` |
| `http://acwg.org/3.0/gsn` | Older | Spells `ChoiceNode`; has the dropped `refersToExternalMaterial` |
| `http://org.eclipse.acme/1.0/gsn` | Legacy ACME tooling | |

All three are accepted on import. See `libs/sacm/src/io/name_tables.cpp`.

## Two things that are not obvious

**Relationship endpoints are reversed.** GSN's `SupportedBy` runs
conclusion→premise; SACM's `AssertedInference` runs premise→conclusion. SACM 2.3
clause 11.14: *"the truth of Claim A — the source — is said to infer the truth of
Claim B — the target."* The v2.2 OCL confirms the GSN direction by constraining
`SupportedBy.source` to `Goal`/`Strategy`. **The reader swaps them on import.**
Without the swap every inference in the argument points the wrong way — a silent
reinterpretation of the safety argument, not a formatting detail.

**Strategy is a node in GSN, an annotation in SACM.** `ArgumentReasoning` hangs
off a relationship via `reasoning` rather than connecting elements directly, so
`Goal ← Strategy ← subgoals` collapses in pure SACM to one `AssertedInference`
carrying an `ArgumentReasoning`. GSN's node-like usage is still legal SACM
because `ArgumentReasoning` extends `ArgumentAsset`.

## Mapping table

`[M]` evidence-backed mapping · `[N]` evidence-backed *absence* of a mapping ·
`[G]` genuine gap needing a project decision.

| GSN construct | SACM 2.3 | | Basis |
|---|---|---|---|
| Goal | `Claim` | [M] | v2.2 Fig 2; ecore |
| Strategy | `ArgumentReasoning` | [M] | v2.2 Fig 2; see note above |
| Solution | `ArtifactReference` | [M] | v2.2 Fig 2 |
| Assumption | `Claim` | [M] | ecore. Pure-SACM export: `assertionDeclaration = assumed` |
| Justification | `Claim` | [M] | ecore. Pure-SACM export: `assertionDeclaration = axiomatic` |
| Module | `ArgumentPackage` | [M] | ecore |
| ContractModule | `ArgumentPackageBinding` | [M] | ecore — *not* ArgumentPackage |
| ModuleReference, ContractModuleReference | `ArtifactReference` | [M] | v2.2: "Modules are modelled as Artifacts" |
| AwayGoal, AwayAssumption, AwayJustification | `Claim` | [M] | ecore. Away\* carry `isCitation`/`citedElement` |
| AwaySolution | `ArtifactReference` | [M] | ecore |
| SupportedBy | `AssertedInference` **(endpoints swapped)** | [M] | v2.2 OCL + SACM 11.14 + real files |
| InContextOf | `AssertedContext` **(endpoints swapped)** | [M] | same |
| **Context** | *none — preserved* | [N] | See below |
| **Choice** / ChoiceNode | *none — preserved* | [N] | See below |
| **AwayContext** | *none — preserved* | [G] | Contested; see below |
| Choice "m of n" cardinality | *none* | [G] | GSN v3 concept, no metamodel exists |
| **Off-diagram decorator** (v3 §1:2.2.20) | *none — an SVG export split, not model data* | [N] | Decided 2026-09-19; see below |
| ACP, Challenge, Defeated (v3) | partial — see gap report | [G] | v3 has no published metamodel. Challenge/Defeat/ACP work on `Claim` and `AssertedRelationship` (both are `Assertion`s) but **not** on `Solution`, `Context` or `Strategy`, which extend `ArgumentAsset` directly and so have neither `metaClaim` nor `assertionDeclaration`. SACM 2.4 is removing both. See `sacm-gsn-metamodel-gaps.md`. |

### Context — undecidable by design

v2.2 §2.1: *"context elements may be either of type Axiomatic Claim or of type
ArtifactReference. **The type of the Context element is determined by the nature
of its content.**"* The specification's own examples turn on meaning — "The
contractors for this project are BTR construction" is a Claim; "System design
description (Ref Y)" is an ArtifactReference.

There is no property to switch on. The older `refersToExternalMaterial` flag that
some tooling used was dropped from the current metamodel. So the library
preserves Context and diagnoses it rather than picking one, because picking would
be reinterpreting the argument. A tool with a human in the loop can resolve it;
a parser cannot.

### Choice — no SACM equivalent

v2.2 models it as an `ArgumentAsset` purely *"to enable its connection to
multiple SupportedBy relationships"*, and the class has no attributes. Its "m of
n" semantics arrived in GSN v3, which has no metamodel yet.

### AwayContext — contested, left preserved (decided 2026-07-20)

Both published ecores declare `ArgumentAsset` (no concrete equivalent), while the
v2.2 prose and the ACWG transformation rules say `ArtifactReference`. Retyping
evidence on a 2-versus-2 split is not something to do silently.

**Decision: leave it preserved and diagnosed.** Revisit only if a corrected
metamodel settles it, or if real files show the ambiguity actually costs us
something. Recorded here so it is a standing decision rather than an oversight
someone "fixes" later without the context.

### Off-diagram — an export split, not model data (decided 2026-09-19)

GSN v3 §1:2.2.20 makes an off-diagram decorator normative. The public v2→v3 changes
deck (`https://scsc.uk/file/gc-main/GSNv2-to-v3_changes-1092.pdf`, 04/05/2021,
slides 27–28) introduces it as *"'off diagram' notation to link separate diagrams
that illustrate fragments of a goal structure"*, records §1:2.2.20 as an *"added
normative part of the standard saying that off-diagram decorator need to be distinct
from the GSN elements"*, and says the standard *"recommends a preferred off-diagram
symbol"*. The accompanying figure draws the same goal `G5` twice, once as the bottom
element of diagram y and once as the top element of diagram x, each occurrence
labelled with the other diagram's name.

*Fragments of a goal structure* is the reading that settles it: one argument, drawn in
several pieces because it does not fit on one. **The decision (#454): off-diagram is an
SVG export concern.** When an export is split across several SVG files, the decorator
marks where each piece continues, and the "diagram" it names is the other file.

- **Nothing is stored in SACM or the project.** SACM models argument content, not its
  presentation, and has no diagram construct; this reading does not need one. The
  model, authoring, validation and interchange columns of GSN3-CORE-012 are therefore
  `n/a`, and only rendering remains.
- **The canvas does not draw it.** It shows the whole argument and has no pages to
  split. The decorator appears only in split exports, which is what report creation
  (`AF-ENG-004`) needs.
- **It is not an `ArgumentPackage`.** A package is a GSN **Module**, and a goal in
  another module is reached through an Away Goal (GSN3-MOD-003), which cites within one
  SACM file. Reading "diagram" as "package" would answer a Core requirement with the
  Modular extension's semantics.

GSN Metamodel v2.2 still has no class for the decorator (gap row 14 in
`sacm-gsn-metamodel-gaps.md`), which matters to tools exchanging split diagrams and not
to this one, since an export split is regenerated rather than exchanged.

## Attribute mappings for pure-SACM export

These apply when transforming *out* to plain SACM. Inside a GSN model the v2.2
OCL forces `assertionDeclaration = asserted` on everything, so the distinctions
live in `xsi:type` and in GSN's own boolean attributes — **drop the GSN type and
you lose them.**

| GSN attribute | SACM |
|---|---|
| `undeveloped` | `assertionDeclaration = needsSupport` |
| `uninstantiated` | `isAbstract = true` |
| `toBeSupportedByContract` | `needsSupport` + TaggedValue |
| `isPublic` | TaggedValue |
| `isMany`, `isOptional` | *no SACM feature* |

### `undeveloped` occupies the declaration, so it is confined to a Goal

The row above is a *substitution*, not an addition: SACM has one
`assertionDeclaration`, so `needsSupport` and `assumed` / `axiomatic` /
`defeated` / `asCited` are mutually exclusive. Inside a GSN model that never
bites, because v2.2's OCL pins the declaration to `asserted` and GSN keeps
`undeveloped` in its own boolean. In a SACM-native store — which is what
Assurance Forge keeps — the two land in the same slot.

**Decision: the editor sets the decorator only where the declaration is
`asserted` or `needsSupport`, and refuses otherwise.** This is not a workaround
for the collision; it is what the notation already says. Undeveloped means "requires
support that has not yet been provided", and GSN reaches an Assumption or a
Justification through `InContextOf`, never `SupportedBy` — there is no support
for them to be missing. An element that would collide is, by construction, an
element the decorator does not apply to.

So this is **not** a gap worth reporting: no GSN v3 construct needs
"assumed *and* undeveloped", and inventing an encoding for it would create a
combination the notation cannot draw. (Contrast the genuine gaps in
`sacm-gsn-metamodel-gaps.md`, which are all cases where v3 *requires* something
SACM cannot hold.)

Getting here cost a real defect, recorded so the reasoning is not re-derived from
scratch. Assurance Forge used to keep `undeveloped` as a boolean beside the
declaration, write both on save, and read the shorthand back only when the
declaration was still `asserted` (`xmi_reader.cpp`, deliberate and correct).
Marking a GSN Assumption undeveloped therefore reported success and changed
nothing, in memory or on disk, while the status bar said it had worked. The
editor now writes the declaration itself, and the inspector offers the control
only where it can be honoured. Tests:
`LibraryPrimaryEditFlip.UndevelopedDecoratorSticksOnAGoalAndIsRefusedOnAnAssumption`.

## Recording the original GSN type

Resolving a GSN type to its SACM supertype is lossy — `Goal`, `Assumption` and
`Justification` all land on `Claim`. Two mechanisms keep the distinction, both
in `libs/sacm/src/io/xmi_reader.cpp` (`record_extension_origin`):

1. **The original type is recorded** in a reserved TaggedValue with the key
   `sacm.import.extensionType`, whose value is the namespace-qualified type in
   Clark notation: `{http://scsc.acwg.gsn/2.0}Goal`. Qualified, because a
   document may mix GSN revisions and a bare `Goal` would be ambiguous. This
   follows the `sacm.import.name` / `sacm.import.assertionDeclaration`
   convention (clause 8.12).
2. **The declaration v2.2 requires is applied** — `Assumption`/`AwayAssumption`
   → `assumed`, `Justification`/`AwayJustification` → `axiomatic` — but only
   when the file did not state one. An explicit `assertionDeclaration` is more
   specific than one inferred from the type and wins, the same rule the
   `undeveloped` shorthand follows.

Both survive save-and-reload. Note that a save **normalizes to SACM types**: the
information round-trips, the GSN syntax does not. Re-emitting `xsi:type="gsn_:Goal"`
would be a compatibility-save feature and is not implemented.

Tests: `SACM23_COMPAT_002_OriginalGsnTypeIsRecordedOnImport`,
`SACM23_COMPAT_002_GsnAssumptionAndJustificationAreNotPlainGoals`,
`SACM23_COMPAT_002_RecordedGsnTypeSurvivesSaveAndReload`,
`SACM23_COMPAT_002_ExplicitAssertionDeclarationBeatsTheGsnType`.

## Reading a SACM 2.3 file as GSN

The table above says how GSN is written into SACM. Opening a SACM 2.3 file needs
the reverse: which GSN element each SACM element is drawn as, and which edge
each relationship becomes. The reverse is not unique, because several GSN
elements share one SACM class. These are the rules the Rust view (`af-gsn`)
follows. Each has an id that its tests cite, a status and a basis.

**Status.** *Decided*: settled by the mapping table, the standards, or a rule of
the diagram plan, `docs/roadmap/milestone-1-diagram.md`: the view draws what the
file says (D3), every argument element appears exactly once (D4), and a case
without evidence is drawn with a neutral shape and a code, never guessed.
*Decided in #493*: a choice between readings that both have a basis, made by the
owner on 18 November 2026
([#493](https://github.com/lasrod/assurance-forge/issues/493)). The reasons and
the readings not chosen are kept below.

Four principles run through the rules:

1. **A Claim's shape comes from what it declares, never from how it is
   reached.** GSN's element types are kinds of statement. Whether a Goal may be
   reached by `InContextOf` is a well-formedness question, which the problem
   list answers. A view that retyped an element by its position would repair the
   argument while drawing it.
2. **An `ArtifactReference` declares nothing**, so how it is reached is all
   there is to go on.
3. **What these rules do not name is drawn neutral, with a code.** The core
   sends the code and its arguments; the frontend words it.
4. **The view reads; it never changes the document.**

The view covers the elements of every `ArgumentPackage` in the document, at any
depth, in one diagram. A package is a container, not a node: a nested package
(a GSN Module) is drawn as its elements until modules are a step of their own.
Elements of artifact and terminology packages are not argument elements and are
not drawn; an `ArtifactReference` points at them.

**Reached.** Where a rule says an element is *reached* by a relationship, only
relationships that the view draws as SupportedBy or InContextOf count (E1 to
E4, with E6's code or without). A relationship drawn neutral, or not drawn,
reaches nothing, so an `AssertedArtifactSupport` from a piece of evidence does
not make it less of a Solution. A challenge (E5) reaches nothing either, with one
exception: the source of a counter `AssertedEvidence` the view draws is
counter-evidence, a Solution (R9). The target of a challenge is not reached by
it, so a challenged Solution stays a Solution.

**Order.** For a node: R14, R15 and R16 first, since what is not drawn is not
drawn whatever else it says; then R12, since a citation is neutral; then R1, a
recorded type; then R2 to R11, by the element's class and declaration. R13 adds
its code to whatever kind those give. For a relationship: R15 and E10 first;
then E5, since a challenge is drawn as a challenge even when it is also cited or
defeated; then E5a or E9; then E1 to E4. Every code whose condition holds is
sent, whichever rule decided how the edge is drawn: a cited, defeated challenge
carries AF-VIEW-004 and 011. An element that is not drawn (R14, R15, R16,
E10) carries only its own code.

### Nodes

| Rule | SACM | Drawn as | Status | Basis |
|---|---|---|---|---|
| R1 | A recorded GSN type: `sacm.import.extensionType` (Goal, Strategy, Solution, Context, Assumption or Justification, in any GSN namespace above); `assuranceForge.gsn.role` = `Justification`; or `sacm.import.assertionDeclaration` = `justification` | The recorded type, when it agrees with the element (below). Otherwise R2 to R11 decide, with code AF-VIEW-001 | Decided | "Recording the original GSN type" above; the C++ role tag and the legacy literal it normalized (`src/sacm_adapter/gsn_role_tag.h`, `case_projection.cpp`). An explicit declaration beats the type there too. No sample carries any of these tags, so R1 needs fixtures of its own |
| R2 | `Claim`, declared `asserted` | Goal | Decided | Mapping table |
| R3 | `Claim`, declared `needsSupport` | Goal with the undeveloped decorator | Decided | "`undeveloped` occupies the declaration" above |
| R4 | `Claim`, declared `assumed` | Assumption | Decided | Mapping table |
| R5 | `Claim`, declared `axiomatic`, however it is reached | Justification | Decided in #493 | [R5](#r5-an-axiomatic-claim-is-a-justification) below |
| R6 | `Claim`, declared `asserted`, reached only by `AssertedContext` | Goal, by R2, with its `InContextOf` edge | Decided in #493 | [R6](#r6-a-goal-reached-as-context-stays-a-goal) below |
| R7 | `Claim`, declared `defeated` or `asCited` | Neutral, code AF-VIEW-002 | Decided | The declaration has taken the one slot that tells Goal, Assumption and Justification apart, so the GSN type is unknown. Challenges and citations are later steps |
| R8 | `ArgumentReasoning`, wherever it is: an inference's `reasoning`, the target of an `AssertedContext`, the source or target of an inference, or reached by nothing | Strategy | Decided | Mapping table; "Strategy is a node in GSN" above. A Strategy at an inference's end is drawn there, as the file says (D3), and well-formedness reports it |
| R9 | `ArtifactReference` reached only as the source of `AssertedEvidence`, for or against (a counter `AssertedEvidence` the view draws, E5) | Solution | Decided; counter-evidence on 27 November 2026 | Mapping table. SACM 2.3 §11.15 allows only an `ArtifactReference` as that source. Counter-evidence: [R9: counter-evidence is a Solution](#r9-counter-evidence-is-a-solution) below |
| R10 | `ArtifactReference` reached only as the source of `AssertedContext` | Context | Decided | v2.2 §2.1; "Context — undecidable by design" above |
| R11 | `ArtifactReference` reached as both, as neither, or in any other way | Neutral, code AF-VIEW-003 | Decided in #493 | [R11](#r11-an-artifactreference-that-is-not-plainly-evidence-or-context) below |
| R12 | Any node element with `isCitation` set | Neutral, code AF-VIEW-004 | Decided | Away elements (GSN3-MOD-003) come with modules |
| R13 | Any element with `isAbstract` set, node or relationship | Its kind by the other rules, with code AF-VIEW-005: the uninstantiated decorator is not drawn yet | Decided | Attribute table above, `uninstantiated` ↔ `isAbstract`. Patterns are a later step, and the code keeps the omission visible |
| R14 | `ArgumentGroup`, and any kind of the Argumentation package that no other rule names | Not drawn, code AF-VIEW-006. A group's members are references, not contents, and are drawn by their own rules | Decided | Groups are not core GSN. A kind a later SACM or libSACM adds is kept in view by its code until a rule names it |
| R15 | `ArgumentPackageInterface`, `ArgumentPackageBinding`, and the elements they contain at any depth; and the argument elements inside an `AssuranceCasePackageInterface` or `AssuranceCasePackageBinding` | Not drawn, code AF-VIEW-006 for each | Decided | Modules and contracts are a later step. An interface's elements stand for elements of another package, so drawing them now would draw those twice |
| R16 | A `Claim` tagged `assuranceForge.gsn.placeholder` = `subGoal` that says nothing more: no other child, no name content, no description, declared `asserted` or not at all, not cited, not abstract, no `metaClaim`; whose one referrer is an `AssertedInference`, asserted and not a counter, cited or abstract, holding it as its only source, with a target, and with a drawn `ArgumentReasoning` as its `reasoning` | Not drawn, code AF-VIEW-014. The inference draws its target's line to the Strategy (E1) and nothing below it. A placeholder that says anything more is drawn as R2 says | Decided on 25 November 2026 | W2: the place a Strategy holds for its first Goal. Drawing anything that says more keeps every statement of the file in view |
| R17 | A `Claim` tagged `assuranceForge.gsn.acp` = `description`, which W34 writes, declared asserted or not at all, and named only by `metaClaim` references, each of a relationship of its own argument package that E11 draws an ACP for (one that draws no edge draws none, and the Claim is then drawn as R2 says); not cited, not abstract, with no `metaClaim` of its own | Not drawn, code AF-VIEW-015: the ACP shows it, and its text is its description. Anything else that names it, a support or context line among them, makes it a Goal of the argument, drawn as R2 says | Decided (27 November 2026, ACP plan A3; the tag from a review, so that a confidence goal that loses its support is not taken for a description) | GSN v3 §1:5.2.3: the argument behind an ACP "could be located in a paragraph of accompanying text, a goal in the local argument, or a goal in a separate module": the text is the first, a Claim that is supported the second, a Claim of another package the third |

A recorded type agrees with the element when: Goal is a `Claim` declared
`asserted` or `needsSupport`; Assumption, `assumed`; Justification, `axiomatic`;
Strategy, an `ArgumentReasoning`; Solution and Context, an `ArtifactReference`.
Any other recorded type in a GSN namespace, including a Context recorded on a
`Claim`, an Away element, a Module or a Choice, and recorded types that disagree
with each other, leave the element to the other rules, with code AF-VIEW-001. A
type outside the GSN namespaces, or a role other than `Justification`, is not a
recorded GSN type and is ignored. Context on a Claim has no pure-SACM declaration (mapping table,
Context [N]), and the C++ importer maps `gsn:Context` to no SACM type
(`libs/sacm/src/io/name_tables.cpp`), so such a tag has no known writer.

### Edges

| Rule | SACM | Drawn as | Status | Basis |
|---|---|---|---|---|
| E1 | `AssertedInference` whose `reasoning` names an `ArgumentReasoning` | SupportedBy from the target to the Strategy, and from the Strategy to each source. Every edge carries the inference's id | Decided | Mapping table, endpoints swapped; "Strategy is a node in GSN" above |
| E2 | `AssertedInference` without `reasoning` | SupportedBy from the target to each source | Decided | Mapping table |
| E3 | `AssertedEvidence` | SupportedBy from the target to each source | Decided | Mapping table |
| E4 | `AssertedContext` | InContextOf from the target to each source. The target may be a `Claim` or an `ArgumentReasoning`, which is context for a Strategy | Decided | Mapping table. 18 of the samples' 45 `AssertedContext` target an `ArgumentReasoning` |
| E5 | A relationship with `isCounter` set, whatever else it says | A Challenge edge from each source to the target, pointing at what is challenged: the opposite of SupportedBy. The target may be a node, or a relationship the view draws an edge for, in which case the arrow points at that relationship's first edge. Never SupportedBy or InContextOf. Its `reasoning`, if any, is not linked: code AF-VIEW-009 for it | Decided on 21 November 2026 | [E5](#e5-a-counter-relationship-is-a-challenge) below. A challenge points at what it challenges (GSN v3 Dialectic extension; `notation.md`). A counter relationship drawn as support says the opposite of the file: the C++ SVG export once drew counter-evidence as evidence for a claim |
| E5a | A relationship declared `defeated` that is not a counter | The edges E1 to E4 would give, in the same GSN direction and through its Strategy, drawn neutral. Code AF-VIEW-011 | Decided | What is defeated is the relationship, not its target, so the edge keeps its direction and only stops looking like live support. GSN v3 has Defeated on SupportedBy and InContextOf (gap row 2), which the dialectic step will draw |
| E6 | A relationship declared `needsSupport`, `assumed`, `axiomatic` or `asCited` | Its edge by E1 to E4, code AF-VIEW-008: the declaration is not drawn | Decided | Core GSN has no edge decorator for these |
| E7 | A relationship whose target, or one of whose sources, names no element, names a relationship (but for the target of a challenge, E5), or names an element the view does not draw (R14, R15), or that has no target or no source; and a challenge whose target is itself, or a relationship the view draws no edge for | The edges to its ends that can be placed are drawn; code AF-VIEW-009 for each end that cannot, naming the id, or no id for a missing end. With no edge to draw, the relationship is not placed, also with AF-VIEW-009, and its ends are drawn by their own rules | Decided | A claim about an inference has no core GSN form; an unresolved end has nothing to draw. libSACM keeps a dangling reference as read (SACM-REF-001), so the case is reachable |
| E8 | `AssertedInference` whose `reasoning` names no element, or not an `ArgumentReasoning`; and a `reasoning` on any other relationship | Drawn without it (an inference by E2), code AF-VIEW-009 for the reasoning | Decided | The support is still in the file; only the Strategy cannot be placed. SACM 2.3 gives every asserted relationship a `reasoning` (§11.13), and GSN puts a Strategy only between a goal and its premises |
| E9 | A relationship with `isCitation` set that is not a counter | As E5a: the edges E1 to E4 would give, in the GSN direction, drawn neutral. Code AF-VIEW-004 | Decided | As R12. A cited relationship is still support or context, so it keeps its direction |
| E10 | `AssertedArtifactSupport`, `AssertedArtifactContext` | Not drawn, code AF-VIEW-010 | Decided | They relate artifacts to artifacts (§11.17, §11.18); GSN has no such edge |
| E11 | `metaClaim` of an assertion | An **assurance claim point**: a `metaClaim` of a relationship drawn as `SupportedBy` or `InContextOf` (E1 to E4, not cited, defeated or a counter) that names a `Claim` is drawn as a solid square on the relationship's first edge, halfway along it, labelled with the Claim's identifier (T1), and with `[package name]` after it when the Claim is in another argument package than the relationship. An abstract relationship (uninstantiated, R13) carries one as any other, as a pattern may. A `metaClaim` of a challenge, of a line drawn neutral, of a relationship with no edge, of a Claim, or naming anything but a `Claim` the view draws or keeps as a description (R17), is not drawn, code AF-VIEW-012 on the assertion, naming the element it names. The Claims named are drawn by their own rules, R17 among them | Decided (27 November 2026, ACP plan A1, A2, A4) | GSN v3 Table 1:5-1: ACPs are "applied to 'SupportedBy' and 'InContextOf' relationships"; §1:5.3.3: an inference through a Strategy carries it as a whole, "the placement of an ACP on an individual 'SupportedBy' relationship below the strategy is ambiguous", so on its first edge; §1:5.2.3: "ACP1[Confidence]" when the argument is in a separate module; §1:5.3.2: the identifier "may be a reference to a goal". SACM 2.3 `Assertion.metaClaim` (gap row 5). Not on a Goal, Assumption or Justification, which GSN v3 does not define; not on a Solution or Context, which SACM 2.3 cannot hold (gap row 7) |
| E12 | `structure` of an `ArgumentReasoning` | Not drawn, code AF-VIEW-013 on the Strategy | Decided | It names the package that details the reasoning (§11.12): modules, a later step |

An element reached by several relationships is still one node, with an edge for
each (D4). One `ArgumentReasoning` named by several inferences is one Strategy,
with each inference's edges. Two inferences with the same ends give two edges,
each carrying its own relationship's id. A source named twice in one
relationship is one premise and one edge. An edge from an element to itself is
drawn as the file says; well-formedness reports it.

#### E5: a counter relationship is a challenge

Until 21 November 2026 a counter relationship was drawn as a neutral edge with
code AF-VIEW-007, and one that targeted a relationship was not placed (E7). It
is now drawn as GSN's challenge: a dashed line with an open arrowhead, at the
element or the relationship it challenges.

- **The mapping is not new.** `isCounter` is what carries a challenge in SACM
  (mapping table, "ACP, Challenge, Defeated"; gap row 4), and the C++
  application wrote and drew it so (GSN3-DIA-001 to 005 in
  `docs/reference/cpp-gsn-v3-conformance-matrix.md`). Only the reading rule
  changed, from "not drawn as GSN yet" to "drawn".
- **A relationship as the target.** GSN v3 lets a challenge point at a
  relationship, and SACM allows it: an asserted relationship's target is an
  `ArgumentAsset`, which a relationship is. A relationship drawn as several
  edges (E1) is challenged at its first, the edge from the conclusion into the
  Strategy, because GSN v3 §1:6.3.12 puts a challenge of an inference with
  several premises at its Strategy. A relationship that is not a challenge and
  targets a relationship still has no GSN form (E7).
- **AF-VIEW-007 is retired.** It said a challenge was drawn differently from
  what it is, which is no longer so. Its number is kept, so the later codes
  keep theirs.
- **Counter-evidence is a Solution** (R9, decided 27 November 2026). The
  defeated decorator (E5a) and the in-doubt state are not drawn.

How the layout places a challenge is not a mapping: it is in `af-layout`.

### Node text

| Rule | Part | Read from | Status |
|---|---|---|---|
| T1 | Identifier | The `assuranceForge.gsn.identifier` tagged value, else the SACM id | Decided: C++ capability AF-GSN-015 (`GsnIdentifierFor`) |
| T2 | Name | The element's `name`, which holds one language (clause 8.6) | Decided in #493 |
| T3 | Statement | The first `description`, for every kind of element: of its values whose content is not empty, the one whose `lang` equals the interface language (ignoring case), else the first whose primary subtag equals the interface language's (`ja-JP` and `ja`, `en-GB` for `en-US`), else the first, with or without a `lang` | Decided in #493 |

See [T2 and T3](#t2-and-t3-which-text-a-node-shows) below.

### The rules decided in #493

#### R5: an axiomatic Claim is a Justification

The pure-SACM export rule gives Justification the declaration `axiomatic`
(mapping table, [M]), and no GSN element other than Justification is written
with it. GSN metamodel v2.2 §2.1 does say a Context may be "of type Axiomatic
Claim", but inside a GSN model v2.2 pins every declaration to `asserted`, so its
phrase names the kind of statement, not the literal, and a Context on a Claim
has no pure-SACM declaration (Context, [N]). The real case agrees: all 13
`axiomatic` Claims in `safety-case-sanitized-strict.sacm.xmi` are named `J_nnn`,
with the statement "Justification J_nnn". Eleven are reached by
`AssertedContext`, and two by nothing. That is one file, judged by its names.

The other readings:

- **Neutral, with a code**, as R11. Safe, but it would draw 13 neutral shapes in
  the one real case, where the file's intent is plain.
- **Justification, with an informational code** saying it could be a Context.
  It changes nothing drawn and puts a note on every Justification.
- **As the C++ application did.** An `axiomatic` Claim was a Justification only
  with its role tag or the legacy `justification` literal, otherwise a Goal, and
  a Context when an `AssertedContext` reached it. It drew the 11 justifications
  of the sanitized case as Context. That is not carried over.

#### R6: a Goal reached as context stays a Goal

A Claim declared `asserted` and reached only by `AssertedContext` is drawn as a
Goal with an `InContextOf` edge, which the well-formedness checks will report.
The C++ application drew it as Context. The rule rests on principle 1 and
D3: the declaration says Goal, and retyping it by its position would read more
into the file than it says. Nothing in a SACM 2.3 file can tell such a Goal
from a Context written as a Claim: under v2.2 both are declared `asserted`, and
R1 does not accept a Context recorded on a Claim. If a file that needs it turns
up, that is a new decision. No sample has the case.

#### R9: counter-evidence is a Solution

Decided by the user on 27 November 2026, for milestone 2, step 6. An
`ArtifactReference` that is the source of a counter `AssertedEvidence` the view
draws (E5), and is reached in no other way, is a Solution: counter-evidence.

- **GSN v3.** "GSN defines dialectic uses of the following core elements:
  Goal, Solution" (§1:6.2.2). Table 1:6-1: "A solution, (core element) can be
  used to present a reference to an evidence item that asserts a challenge to
  part of the argument", and §1:6.3.5 speaks of "Counter evidence (via a
  solution)". Counter-evidence is a Solution, not a kind of its own.
- **SACM.** The pair is the one SACM gives for evidence, `ArtifactReference`
  and `AssertedEvidence` (R9, §11.15), with `isCounter` set (§11.13): evidence
  against its target instead of for it. Nothing else in the file is read
  differently.
- **The C++ application** drew it as a Solution.
- **Until then** it was neutral by R11, which said only that it was not
  plainly evidence. Counter-evidence that is also reached as context, or an
  `ArtifactReference` that challenges by an `AssertedInference`, stays neutral.

#### R11: an ArtifactReference that is not plainly evidence or context

No sample reaches an `ArtifactReference` both by `AssertedEvidence` and by
`AssertedContext`. Twenty-two are reached by nothing: 20 in the sanitized case,
which were unconnected in its original too, and two in `sample.sacm.xmi`.
Drawing an unconnected reference as a Solution would show it as evidence, and as
a Context would show it as context; the file says neither. The neutral shape
keeps the element in view without claiming either.

#### T2 and T3: which text a node shows

The C++ application showed "identifier: name" with the statement below it
(`src/core/assurance_tree.cpp`). It took the text from the first description in
its first language (`src/sacm_adapter/case_projection.cpp`,
`MultiLangString::primary`): as the statement for a Claim or an
`ArgumentReasoning`, and as the description for other elements, which it showed
in the same place. A second language was shown on request. The rule keeps
that and puts the interface language first, so the Japanese text of
`oasc-ja.sacm.xmi` shows in a Japanese interface.

- A **second description** is not drawn. The converted samples carry a note
  there. The text of clause 8.6 allows one description, although the
  machine-readable model says `0..*` (libSACM reports it as SACM-MULT-001, a
  warning). Where the note belongs is not decided.
- **`sacm.import.name`**, where the C++ library kept a name's other languages,
  is not read as a name. libSACM keeps it as an ordinary tagged value and has not
  adopted the convention (its LIB-COMPAT-012). So in a Japanese interface the
  names in `oasc-ja` stay in English while their statements change.
- Choosing the diagram's language apart from the interface's is a later step.

### Codes

Each carries the id of the element or relationship as an argument.

| Code | Meaning |
|---|---|
| AF-VIEW-001 | A recorded GSN type was not used: it disagrees with the element, or names a type this step does not draw |
| AF-VIEW-002 | A Claim's declaration (`defeated`, `asCited`) leaves its GSN type unknown |
| AF-VIEW-003 | An `ArtifactReference` is neither plainly evidence nor plainly context |
| AF-VIEW-004 | A citation (an Away element or a cited relationship), drawn neutral until modules |
| AF-VIEW-005 | The uninstantiated decorator is not drawn yet |
| AF-VIEW-006 | An element kind this step does not draw |
| AF-VIEW-007 | Retired on 21 November 2026, and no longer sent. It said a counter relationship was drawn neutral; a counter relationship is drawn as a challenge (E5) |
| AF-VIEW-008 | A relationship's declaration is not drawn |
| AF-VIEW-009 | A relationship end, or an inference's reasoning, that cannot be placed |
| AF-VIEW-010 | A relationship between artifacts, not drawn |
| AF-VIEW-011 | A defeated relationship, drawn neutral |
| AF-VIEW-012 | An assertion's `metaClaim` link is not drawn |
| AF-VIEW-013 | A Strategy's `structure` link is not drawn |
| AF-VIEW-014 | The place a Strategy holds for its first Goal, which is not drawn (R16) |
| AF-VIEW-015 | A confidence Claim that an ACP shows as its description, not drawn as a node (R17) |

## Writing GSN as SACM

How the editor adds an element, and changes a node's text (W15 to W17
below): the inverse of the reading rules above, so
that what is added reads back, by R2 to R11 and E1 to E4, as the kind chosen,
joined to its parent as chosen. `af-gsn` (`commands`) applies these rules;
each add is one `sacm::edit::batch`, applied all or not at all. Decided on
21 November 2026 for milestone 2, step 1 (issue #512).

What may be added where follows GSN v3 Core. Its permitted `SupportedBy`
connections are goal-to-goal, goal-to-strategy, goal-to-solution and
strategy-to-goal: a Goal is supported by a Goal, a Strategy or a Solution, and
a Strategy only by Goals. `InContextOf` runs from a Goal or a Strategy to a
Context, an Assumption or a Justification; a Solution, a Context, an
Assumption and a Justification are leaves. Until 25 November 2026 this record
said a Strategy could also be supported by a Strategy or a Solution, as the
C++ application's rule (`CanAddChildElement`) and its conformance record
(GSN3-CORE-015) did; the standard does not allow either (W13, W14).

| Rule | Added | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W1 | Goal under a Goal | `CreateClaim` (asserted); `CreateAssertedInference` from the new claim to the goal | Decided | R2, E2 |
| W2 | Strategy under a Goal | `CreateArgumentReasoning`; `CreateClaim` (asserted, no identifier) with `CreateTaggedValue` `assuranceForge.gsn.placeholder` = `subGoal`, the place of its first Goal, which R16 does not draw; `CreateAssertedInference` from that Claim to the goal, with the Strategy as `reasoning` | Decided on 25 November 2026 (it was: the Strategy with its first sub-goal) | R8, R16, E1; [W2](#w2-a-strategy-is-added-alone) below |
| W3 | Goal under a Strategy | `CreateClaim` (asserted); `AddReference` of it to the `source` of the one inference whose `reasoning` is the Strategy and that E1 draws. With no such inference, or several, it is not offered (AF-EDIT-004). When that inference's one source is the place R16 does not draw, that Claim becomes the Goal: `DeleteElement` of its placeholder tag, and `CreateTaggedValue` of its identifier (W10) | Decided | E1: one Strategy's premises are one inference's sources; W2 |
| W4 | Solution under a Goal | `CreateArtifactReference`; `CreateAssertedEvidence` from it to the goal | Decided | R9 |
| W5 | Context under a Goal or a Strategy | `CreateArtifactReference`; `CreateAssertedContext` from it to the node | Decided | R10, E4. A Context could also be a Claim ("Context — undecidable by design" above); the editor writes the form R10 reads, an `ArtifactReference` |
| W6 | Assumption under a Goal or a Strategy | `CreateClaim`, then `SetAssertionDeclaration` `assumed`; `CreateAssertedContext` from it to the node | Decided | R4, E4 |
| W7 | Justification under a Goal or a Strategy | `CreateClaim`, then `SetAssertionDeclaration` `axiomatic`; `CreateAssertedContext` from it to the node | Decided | R5, E4. No `assuranceForge.gsn.role` tag: R5 reads the declaration alone |
| W8 | A top-level Goal (from the empty canvas) | `CreateClaim` (asserted), reached by nothing | Decided | R2 |
| W9 | Where the new elements go | The `ArgumentPackage` that holds the node, at any depth; a top-level Goal into the `ArgumentPackage` whose diagram is shown (ACP plan A5), or with the whole case shown into the first `ArgumentPackage` in document order. With none, the add is not offered (AF-EDIT-005) | Decided | The C++ application's `apply_add_child` |
| W10 | Its identifier | `CreateTaggedValue` `assuranceForge.gsn.identifier` on each new node: the kind's prefix (G, S, Sn, C, A, J) and the lowest number above every identifier in the file with that prefix and digits only after it: a drawn node's, and every element's tagged identifier and SACM id, drawn or not | Decided | T1; C++ capability AF-GSN-015 |
| W11 | Support under an undeveloped Goal | Also `SetAssertionDeclaration` `asserted` on the goal, in the same batch. The diagram then shows the goal developed; since 25 November 2026 the menu lists the kinds with no notes and does not say so beforehand | Decided | R3; GSN well-formedness: an undeveloped element has no `SupportedBy` children |
| W12 | Name and statement | An empty `name` on every new element and relationship, and no `description`: SACM requires a name (§8.6) and libSACM accepts an empty one. The text is the author's, so the editor invents none | Decided | §8.6 `name:LangString[1]` |
| W13 | A Solution under a Strategy | Not offered (AF-EDIT-003) | Decided on 25 November 2026 | GSN does not allow it: a Strategy is supported only by Goals (strategy-to-goal). A file that holds one is drawn as it says (R8, E8) |
| W14 | A Strategy under a Strategy | Not offered (AF-EDIT-003) | Decided on 25 November 2026 | As W13 |

No vendor tag records a GSN type: each kind is told by the reading rules from
standard SACM alone.

### Changing a node's text

How Element Properties changes a node's identifier, name and statement: the
inverse of T1 to T3, so that the text given reads back as given. `af-gsn`
(`commands::set_text`) applies these rules; each change is one
`sacm::edit::batch` and one undo step. The SACM id (`xmi:id`) is never
changed: relationships name it. Decided on 24 November 2026.

| Rule | Changed | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W15 | Identifier | Without the space around it. With an `assuranceForge.gsn.identifier` tagged value: `SetContent` on it, replacing the content of its first value, the one T1 reads, and keeping any others. With none: `CreateTaggedValue`, as W10. An empty identifier is refused (AF-EDIT-012): T1 would read it back as the SACM id. One another element has is refused (AF-EDIT-013), counted as W10 counts: a drawn node's identifier, and every element's tagged identifier and SACM id, drawn or not | Decided | T1, W10. GSN v3 identifies each element uniquely; a file may be read with duplicates, but the editor makes no new one |
| W16 | Name | `SetName` with the name it has, its content replaced: its `lang` and `xmi:id` stay. A name written where there was none (no `lang` and no content, as W12 leaves a new element) is tagged `en`. An empty name is allowed | Decided | T2; W12: SACM requires a name (§8.6) and libSACM accepts an empty one |
| W17 | Statement | In the first `description`: `SetContent`, replacing the content of the value T3 reads for the interface language, which is the one shown, and keeping every other value and each `lang`. When T3 reads none (no value, or only empty ones): the first value's content is set, or a value is added. With no `description`: `CreateDescription` with one value; an empty statement then makes nothing. A value written where there was none (added, or with no `lang` and no content) is tagged `en`. An emptied statement reads back by T3 from what the file still holds: another language's value, when there is one | Decided | T3. The value changed is the one on screen, so nothing the author did not see is rewritten. English is assumed for a text first written here (decided 24 November 2026): one language keeps the editor simple, and a text the file already holds keeps the `lang` it has, or none. Writing in several languages is a later setting |

A text that is an `ExpressionLangString` (§8.4) is not changed as text
(AF-EDIT-014): its content is the value of a terminology expression.
Setting a text to what it already is changes nothing and makes no undo step.

### W2: a Strategy is added alone

GSN builds a Strategy before the goals it organises. SACM cannot hold that
state: an `AssertedInference` needs a `source` (`source : SACMElement[1..*]`,
SACM 2.3 §11.13), and a Strategy is held only as an inference's `reasoning`
(the editing-time findings of `sacm-gsn-metamodel-gaps.md`).

Decided on 25 November 2026: the Strategy is added alone, as GSN and the
user build it, and the file holds the place of its first Goal with a Claim
tagged `assuranceForge.gsn.placeholder` = `subGoal`. The diagram does not
draw it (R16), and the first Goal added under the Strategy is that Claim (W3);
a Strategy that loses its last Goal holds a new one (W20). The file is valid
SACM at every step. Another SACM tool sees an empty, unnamed Claim under the
Strategy, which is what the Strategy is waiting for. Until then the Strategy
came with its first sub-goal, which the user had not asked for. The other
ways out each lose something:

- The SCSC transformation discards the inference of a Strategy with no
  sub-goals, which leaves an `ArgumentReasoning` joined to nothing and loses
  the goal it was added under.
- The C++ application kept that goal in a vendor tag (`strategyTarget`) until
  the first sub-goal arrived, so the file held a Strategy that any other SACM
  tool sees joined to nothing.

- Adding the Strategy with its first sub-goal, which this editor did until
  25 November 2026, adds a Goal the user did not ask for.

A Strategy with no Goal is not well-formed GSN (a Strategy must be
supported); that is for the Problems panel to say, not for the editor to
prevent. Other tools were not checked.

### Removing a node

How the editor removes a node and what is under it. `af-gsn`
(`commands::remove`) applies these rules as one `sacm::edit::batch`, one undo
step; `commands::removal` answers what would go, which the interface lists and
asks about before it removes more than the node chosen. Decided on 25 November
2026.

| Rule | Removed | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W18 | The node and what is under it | What goes: the node, then, until no more do, each node all of whose parents go. A node's parents are the nodes whose `SupportedBy`, `InContextOf` or neutral edges reach it, and what it challenges, a node or a relationship (E5); a node also under one that stays, stays. A line goes with the relationship that draws it, so a node whose every line goes, goes too and is listed: a premise of a removed Goal under a Strategy drawn under other Goals as well, or a Strategy whose inference goes with its last premise because W20 does not keep it. A relationship goes when its target or its `reasoning` goes, or every source does, until no more do, since a relationship can be another's target. A relationship that stays loses each source that goes (`RemoveReference`). Then `DeleteElement` of each relationship and node that goes, with `DeleteReferencingRelationships` and `ApplyReferencePolicy`; the batch is rolled back unless libSACM deletes exactly those, with what they hold. A Goal that loses all its support keeps its declaration | Decided | What is under a node is what the diagram draws below it. The decorator is the author's: removing support does not mark a Goal undeveloped |
| W19 | What the diagram does not draw | Refused (AF-EDIT-015) when anything but a relationship's source or `metaClaim` refers to what goes, such as a citation or a group's member, or a node holds more than utility elements. A relationship that stays loses the source, or, when E11 draws an ACP for it, the confidence Claim from its `metaClaim`: the ACP goes with it, and the removal lists it and asks first. A `metaClaim` the diagram does not draw still refuses. An ACP whose line goes while its confidence Claim stays goes with the line, and is listed too. A relationship that goes with a `metaClaim` the diagram does not draw as an ACP refuses, as does taking a source out of a relationship that carries an ACP (W25: what the ACP is about would change); adding a premise to such an inference is refused the same way (AF-EDIT-015). An ACP's description that only deleted relationships named goes too, and is listed (R17) | Decided | "Never silently change an argument": what the user is not shown is not changed for them |
| W20 | The last Goal of a Strategy | The inference whose `reasoning` is the Strategy stays: a placeholder (W2) becomes its source before the last Goal is taken out. Only an inference R16 would hide the placeholder in does so; any other (a counter, cited, abstract or not asserted) goes as W18 says, since its placeholder would be drawn as a Goal. A Strategy that goes takes its placeholder with it | Decided | W2, R16 |

### Moving a node

How the editor moves a node, with what is under it, to another place.
`af-gsn` (`commands::move_node`) applies these rules as one
`sacm::edit::batch`, one undo step; `commands::move_targets` answers where a
node may go and why not elsewhere, which the interface shows while the user
chooses. A move changes one line of the diagram: the node, its text and what
is under it are as they were. Decided on 26 November 2026 for milestone 2,
step 4.

| Rule | Moved | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W21 | Which node | A node of a known kind that the diagram draws under one node, by a `SupportedBy` or an `InContextOf` line, or under none. Under several, it is not moved (AF-EDIT-016): there is no one line to move, and the editor makes no second parent. A node that challenges something moves its challenge instead (W32). A node whose line is neutral (E5a, E9), and a relationship that names the node twice, are not moved (AF-EDIT-017). A Context, Assumption or Justification that the file joins by a `SupportedBy` line, which GSN does not allow, is moved with that line as it is: alone in its relationship, and to a Goal only (W23's `SetReferences`), and not to an undeveloped one (AF-EDIT-025), which the line kept would support; the move does not repair the line, which the problem list reports and where its repair belongs, and it develops no Goal (decided 26 November 2026). One of several sources of such a relationship is not moved (AF-EDIT-017): that would make a new line GSN does not allow. A Strategy moves with the one inference it is the `reasoning` of (AF-EDIT-004 otherwise); one that supports no Goal has no line to move it by (AF-EDIT-024) | Decided | The view: the line a node is drawn under is the relationship changed |
| W22 | Where | What may be added where (W1 to W8), with the node's join kept: what is reached by `SupportedBy` goes where support may, and what is reached by `InContextOf` goes where context may, whatever its kind (R6: a Goal reached as context stays context). Not under itself or what is drawn under it, challenges included (AF-EDIT-018), not where it is (AF-EDIT-019), and not into another `ArgumentPackage` (AF-EDIT-020). To the top level: a Goal only (AF-EDIT-006) | Decided | GSN v3 Core's permitted connections, as W1 to W8; a relationship across packages is the Modular Extension's, which is not built |
| W23 | The line it has | Where the node is the only `source` of its relationship and the new parent takes the same kind of relationship, the relationship is kept: `SetReferences` of its `target` to the new parent. A Strategy's inference is always kept so, with its Goals. Otherwise the node leaves the line: `RemoveReference` of it from the `source`, or, when it is the only source, `DeleteElement` of the relationship. A Goal that is the last under a Strategy leaves a placeholder behind, as W20 | Decided | The relationship's identity, name and `xmi:id` are kept where SACM can hold the result in the same element. An `AssertedInference` with a `reasoning` is a different shape from one without (E1, E2), so a Goal moving between a Goal and a Strategy changes relationship |
| W24 | The line it gets | Where W23 did not keep the relationship: as an add makes it. Under a Goal, `CreateAssertedInference` (a Goal), `CreateAssertedEvidence` (a Solution) or `CreateAssertedContext` (context), from the node to the Goal. A Goal under a Strategy: `AddReference` of it to the `source` of the Strategy's one inference; a placeholder held there (W2) is taken out and deleted, since the Goal moved has its own identity. Support moved under an undeveloped Goal develops it, as W11 | Decided | W1, W3 to W7, W11 |
| W25 | What the diagram does not draw | Otherwise the move is refused (AF-EDIT-015). A source is added to, or taken out of, a relationship only when nothing makes a claim about it: nothing refers to it (a challenge of it, a group's membership) and it refers to no `metaClaim`. A relationship is given a new `target` only when, besides, it holds nothing but its name. It is deleted only when, besides, it is declared `asserted` (or not declared), is not abstract and is not a citation | Decided | "Never silently change an argument": a challenge of, or a claim about, "A and B support C" is not one of "A supports C"; a description of a line is not carried to another line unseen; and a relationship's declaration, abstractness and citation are not lost with it. A description of an inference that gains or loses a premise stays with it, as it does when a premise is added or removed. A relationship a move deletes takes its name with it, as one a removal deletes does (W18): the name is of the link that goes, and the line the node is joined by afterwards is its own (decided 27 November 2026, from a review; refusing it would refuse almost every such move, since the samples name nearly every inference). Widened on 26 November 2026, from a review: a source was taken out of a challenged relationship, and a declared one deleted. To be opened again when challenges are edited |

A Goal that loses its support by a move keeps its declaration, as by a
removal (W18).

### The undeveloped mark

How the editor marks a Goal undeveloped and clears the mark: the inverse of
R3. `af-gsn` (`commands::set_undeveloped`) applies it as one
`sacm::edit::batch`, one undo step. Decided on 26 November 2026 for
milestone 2, step 5.

| Rule | Changed | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W26 | The undeveloped mark of a Goal | `SetAssertionDeclaration` on the Claim: `needsSupport` to mark it, `asserted` to clear the mark. Only a Claim drawn as a Goal whose declaration is `asserted` or `needsSupport` (AF-EDIT-021 otherwise): the declaration is the one slot, and on an Assumption or a Justification it says what the element is. A Goal that a `SupportedBy` line leaves is not marked (AF-EDIT-022); clearing is always allowed, so a mark the file left on a supported Goal (AF-GSN-010) is cleared. A Strategy is not marked: SACM 2.3 has no place for it (below) | Decided | R3; "`undeveloped` occupies the declaration" above; GSN well-formedness: an undeveloped element has no `SupportedBy` children; W11 clears the mark the same way when support is added |

**A Strategy is not marked undeveloped: SACM 2.3 cannot hold it.** Decided
on 26 November 2026, from the GSN Metamodel Specification v2.2 (July 2021),
§2.1, Figure 2:

- The metamodel gives `Strategy` its own attribute, `+undeveloped: boolean`,
  as it gives `Goal` one. It is an attribute of the GSN class, not of SACM.
  `Strategy` extends SACM `ArgumentReasoning`, which is an `ArgumentAsset`
  and not an `Assertion`, so it has no `assertionDeclaration`.
- The metamodel strikes `needsSupport` out of `AssertionDeclaration` and pins
  every `Assertion` to `asserted` (OCL `Context::Assertion`). The mapping onto
  pure SACM is left out: "If a model is transformed from GSN to SACM then the
  appropriate AssertionDeclaration must be applied to each SACM Claim to
  reflect the nature of the GSN element … These transformation rules are not
  included in this metamodel specification" (page 6).

A Goal's mark therefore has a SACM 2.3 carrier, `needsSupport` on its Claim
(R3, W26), and a Strategy's has none. The candidate considered, `needsSupport`
on the inference the Strategy is the `reasoning` of, says something else in
SACM: that the inference from the premises to the conclusion needs support,
not that the Strategy is undeveloped. No tool was found that reads it so, and
writing it would invent a mapping. A Strategy offers no mark, and one in a
file is not read from anything. The gap is row 15 of
[`sacm-gsn-metamodel-gaps.md`](sacm-gsn-metamodel-gaps.md), with row 3
(defeat on a Strategy), whose remedy would carry it too.

### Repairing a join

How the editor repairs a Context, an Assumption or a Justification that the
file joins by a `SupportedBy` line, where GSN joins it by `InContextOf`
(AF-GSN-004). `af-gsn` (`commands::repair_join`) applies it as one
`sacm::edit::batch`, one undo step; `commands::repairable` answers whether it
can be made, for the problem list to offer it. The repair is asked for by the
user, from the problem: it is never made by a read, a save or a move (W21).
Decided on 26 November 2026.

| Rule | Repaired | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W27 | A Context, Assumption or Justification that is a `source` of a relationship drawn as `SupportedBy`, with no `reasoning`, whose `target` is a Goal or a Strategy | The node alone in the relationship: `DeleteElement` of the relationship, then `CreateAssertedContext` from the node to the same `target`, in the relationship's package, with the relationship's `name`. One of several sources: `RemoveReference` of the node from the `source`, then `CreateAssertedContext` with an empty name (W12); the relationship stays for the others. The new relationship has a new `xmi:id`. It then reads by E4 as `InContextOf`, and the node as the kind it was (R4, R5, R10) | Decided | E4, W5 to W7: the form an add writes. SACM cannot change an element's class in place, so the relationship is replaced. The name is kept because it is the one thing of the old relationship a reader may know it by |
| W28 | What is not repaired, and a context already there | A relationship with a `reasoning` (AF-EDIT-017): the node is then one of a Strategy's premises, and whether it is context of the Strategy or of its Goal is not the editor's to say. A relationship W25 would not delete, when the node is its only source, or would not take a source out of, when it is one of several (AF-EDIT-015). Anything that is not such a join (AF-EDIT-023). When the file already has an `AssertedContext` from the node to the same target that the diagram draws as context, and that is plain (not cited, abstract or declared otherwise, and with no `reasoning`), the repair takes the support line away and makes no second one; one that is not plain is left as it is, and the repair makes a context line of its own | Decided | W25; "Preserve rather than reinterpret" |

### Adding a challenge

How the editor adds a challenge to a node or a line, and moves one
(milestone 2, step 6). `af-gsn` (`commands::challenge`) applies an add as one
`sacm::edit::batch`, one undo step; `commands::challenge_choices` answers,
for the menu, which of the two challengers may be added and why not.
`commands::move_challenge` moves a challenge onto a line, and
`commands::move_node` onto a node; `commands::move_targets` answers where it
may go, a node or a line. Decided on 27 November 2026. The
GSN Metamodel v2.2 forbids a counter relationship (`self.isCounter = false`,
Figure 2); GSN v3 requires challenges, and the gap is row 4 of
[`sacm-gsn-metamodel-gaps.md`](sacm-gsn-metamodel-gaps.md).

| Rule | Challenged, and by what | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W29 | What is challenged | A Goal, an Assumption, a Justification, a Solution or a Context node: the element is the `target`. A line drawn as `SupportedBy`, `InContextOf` or a challenge: the relationship is the `target`, since a challenge may point at a relationship and may itself be challenged. Only the relationship's first line offers it, where E5 draws a challenge of that relationship; its other lines, such as a premise's under a Strategy or one of several sources', have no relationship of their own in SACM and are refused (AF-EDIT-027), since the challenge would be drawn on another line than the one chosen. A Strategy: the one inference it is the `reasoning` of and that the view draws through it (AF-EDIT-004 otherwise), which E5 draws at the Strategy when it reads one. A neutral node or line is not challenged (AF-EDIT-002) | Decided | E5. GSN v3 Table 1:6-2: "Permitted connections are: goal-to-any element, solution-to-any element, goal-to-any relationship, solution-to-any relationship"; §1:6.3.5 names "goal, solution, strategy, context, assumption, justification", and §1:6.3.9 "SupportedBy, InContextOf, Challenges". §1:6.3.12: "it is only possible to challenge the inference relationship in its entirety. A challenge cannot be made directly to multiple SupportedBy relationships, so challenges to this inference require a strategy to be inserted", the challenge then applied to the Strategy. SACM 2.3 §11.13 lets the `target` of an `AssertedRelationship` be any `ArgumentAsset`, a relationship among them |
| W30 | What challenges | A counter goal (a Goal: "an evidenced counter argument (via a goal)") or counter evidence (a Solution); any other kind is refused (AF-EDIT-026). The menu says **Add Counter Goal** and **Add Counter Evidence** | Decided | GSN v3 §1:6.2.2: "GSN defines dialectic uses of the following core elements: Goal, Solution"; §1:6.3.5 and §1:6.3.14 ("a counter claim (via the goal)"); R9 |
| W31 | The challenge | The challenger as an add makes a node of its kind (W9), in the `ArgumentPackage` of what is challenged, with the next identifier of the prefix `CG` for a counter goal and `CSn` for counter evidence (W10's numbering); then, with `isCounter` set, `CreateAssertedInference` from a Goal or `CreateAssertedEvidence` from a Solution, to the `target` of W29. Nothing that is challenged is changed | Decided (prefixes on 27 November 2026, asked for by the user) | E5 reads it back as a challenge; R9 reads counter-evidence as a Solution. GSN v3's dialectic figures number them so: CSn1 (Figures 1:6-1, 1:6-3, 1:6-4), CG1 and CG2 (Figures 1:6-2, 1:6-5). An example, not a rule: §1:2.1.2 asks only that an identifier be unique in its module |
| W32 | Moving a challenge | A counter goal or counter evidence that challenges one thing, alone as the source of its counter relationship, and is drawn under nothing else, moves its challenge: the relationship gets the new `target` (`SetReferences`, `ReferenceEnd::Target`), a node or a line by W29's rules, and the challenger, what is under it and the relationship's `isCounter` stay as they are. The menu's Move… and a drag both make it, a drag dropped on a node or on a line. It is not moved onto itself, what is under it, its own line or a line that hangs from a node under it (AF-EDIT-018), onto what it challenges already (AF-EDIT-019), into another argument package (AF-EDIT-020), or to the top level (AF-EDIT-028): with nothing to challenge it is no longer a counter. A counter relationship that something else refers to, such as a challenge of the challenge, or that holds more than its name, stays (W25, AF-EDIT-015); a challenger of several things, or one that is also supported or is context, is not moved (AF-EDIT-017) | Decided (27 November 2026, from the spike) | W25: a challenge of a challenge is about that challenge's target, and is not carried to another one unseen. GSN v3 §1:6.2.2: the dialectic uses of a Goal and a Solution are counters, which have something to challenge. The findings are in [`docs/spikes/challenge-move.md`](../spikes/challenge-move.md) |

### Adding an assurance claim point

How the editor adds an ACP to a line (milestone 2, step 7). `af-gsn`
(`commands::add_acp`) applies it as one `sacm::edit::batch`, one undo step;
`commands::acp_choices` answers, for the line's menu, whether it may and
which identifier it takes. Decided on 27 November 2026 with the user (A1 to
A4 of [the ACP plan](../roadmap/milestone-2-acps.md)). The GSN Metamodel v2.2
has no ACP class and strikes `metaClaim` out (gap rows 5 and 8); SACM 2.4
removes `metaClaim` for a Claim's `subject` (SACM24-38, -50, -83).

| Rule | Where, and with what | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W33 | Where an ACP goes | A line drawn as `SupportedBy` or `InContextOf`, the first of its relationship: an inference through a Strategy takes it on its line into the Strategy, not on a premise's below it (AF-EDIT-027). A challenge, a line drawn neutral, or anything GSN v3 does not name, does not (AF-EDIT-029). A Solution or a Context does in GSN v3 but not in SACM 2.3, and its menu says it comes with SACM 2.4 (AF-EDIT-030) | Decided | GSN v3 Table 1:5-1 and §1:5.3.3; Table 1:5-2 for a Solution or a Context, which are `ArtifactReference`s without `metaClaim` in SACM 2.3 (gap row 7) |
| W34 | The ACP | A confidence `Claim` made as an add makes a Goal (W9), numbered with the next identifier of the prefix `ACP` (W10's numbering): **a description**, in the line's `ArgumentPackage`, which R17 then shows only as the ACP; or **a confidence argument**, in a new `ArgumentPackage` (`CreateArgumentPackage`) beside the line's, under what holds it, named as the interface words it ("Confidence ACP1"), of which the Claim is the top goal. A description is tagged `assuranceForge.gsn.acp` = `description` (R17). Then `SetReferences(relationship, MetaClaim, …)` adds the Claim to the relationship's `metaClaim`, after any it names. Nothing else changes | Decided | E11 reads it back as the ACP. GSN v3 §1:5.2.3: "Each ACP should have a unique identifier, e.g. 'ACP1'", and the argument is "a paragraph of accompanying text, a goal in the local argument, or a goal in a separate module"; §1:5.3.2: the identifier "may be a reference to a goal" |

### Removing an assurance claim point

How the editor removes an ACP from its line, which stays (milestone 2, step
7). `af-gsn` (`commands::remove_acp`) applies it as one `sacm::edit::batch`,
one undo step; `commands::acp_removal` answers what it deletes, which the
interface shows first. Decided on 28 November 2026 with the user: a
confidence argument in a package of its own is kept or deleted as the user
chooses each time. Removing a line, or the node it leaves, takes its ACPs as
W19 says.

| Rule | Where, and with what | SACM operations | Status | Evidence |
|---|---|---|---|---|
| W35 | Removing an ACP | An ACP E11 draws, chosen by its relationship and its confidence `Claim` (else AF-EDIT-031). `RemoveReference(relationship, MetaClaim, Claim)`. Then by what the Claim is: **a description** (R17) that no other relationship's `metaClaim` names is deleted with it (`DeleteElement`), since nothing else would show it, and one another ACP shows stays; **a goal of the line's own package** stays, now drawn by its own rules; **a confidence argument**, a Claim in another `ArgumentPackage` than the relationship's, is kept, or with the user's choice deleted with its package (`DeleteElement`, recursive). The package is deleted only when it holds nothing but what the diagram draws (its nodes and descriptions, their relationships, the placeholders W20 adds, utility elements), nothing outside it refers into it but this `metaClaim`, and nothing in it refers out but by `citedElement`: otherwise its deletion is refused (AF-EDIT-015) and the ACP alone may still go. What a deletion lists is exactly what goes, or the batch is rolled back | Decided | GSN v3 §1:5.2.3 places the argument behind an ACP in "accompanying text, a goal in the local argument, or a goal in a separate module": the text exists only for the ACP, the local goal is part of the argument, and the module is an argument of its own, which may stand without the ACP. "Never silently change an argument": what the dialog does not list is not deleted |

### Codes

Sent with a choice of the add menu that cannot be made, or with an edit that
was refused. Nothing changes.

| Code | Meaning |
|---|---|
| AF-EDIT-001 | The node is a leaf in GSN: a Solution, Context, Assumption or Justification |
| AF-EDIT-002 | The node's GSN kind is not known (a neutral node), or there is no such node |
| AF-EDIT-003 | GSN does not allow it: a Strategy is supported only by Goals (W13, W14) |
| AF-EDIT-004 | The Strategy is the reasoning of no inference, or of several (W3) |
| AF-EDIT-005 | There is no argument package to add to (W9) |
| AF-EDIT-006 | Only a Goal can be added at the top level (W8) |
| AF-EDIT-007 | Nothing is open |
| AF-EDIT-008 | There is nothing to undo, or nothing to redo |
| AF-EDIT-009 | The document changed after the add menu, or the removal shown, was made: what it offered may no longer hold |
| AF-EDIT-010 | libSACM refused the operations the writing rules made (a fault in the rules, not a mapping decision) |
| AF-EDIT-011 | The id is of no node the diagram draws: only a node's text is set, and only a node is removed |
| AF-EDIT-012 | An identifier cannot be empty (W15) |
| AF-EDIT-013 | Another element of the file has the identifier (W15) |
| AF-EDIT-014 | The text is the value of a terminology expression, which is not changed as text |
| AF-EDIT-015 | Something the diagram does not draw refers to what a removal would delete, or a node holds more than its text (W19); or refers to, or is held by, the line a move would replace, an ACP on it among them (W25) |
| AF-EDIT-016 | The node is drawn under several nodes (W21) |
| AF-EDIT-017 | The node is joined in a way a move does not carry (W21), or a repair does not mend (W28) |
| AF-EDIT-018 | The place is the node itself, or under it (W22) |
| AF-EDIT-019 | The node is there already (W22) |
| AF-EDIT-020 | The place is in another argument package (W22) |
| AF-EDIT-021 | Only a Goal is marked undeveloped (W26) |
| AF-EDIT-022 | The Goal is supported: an undeveloped element has no support (W26) |
| AF-EDIT-023 | The element is not joined in a way a repair mends (W28) |
| AF-EDIT-024 | The Strategy supports no Goal, so there is no line to move it by (W21) |
| AF-EDIT-025 | The element keeps a support line where GSN has context, and the Goal is undeveloped (W21) |
| AF-EDIT-026 | Only a Goal or a Solution challenges (W30) |
| AF-EDIT-027 | The line is not the first of the relationship it is drawn for, where a challenge or an ACP of it is drawn (W29, W33) |
| AF-EDIT-028 | A challenge is moved to another node or line, never to the top level (W32) |
| AF-EDIT-029 | GSN puts no ACP there: only on a `SupportedBy` or `InContextOf` line drawn as one, or a Solution or a Context (W33) |
| AF-EDIT-030 | An ACP on a Solution or a Context comes with SACM 2.4: SACM 2.3 cannot hold it (W33, gap row 7) |
| AF-EDIT-031 | The line carries no ACP the diagram draws for that Claim (W35) |
