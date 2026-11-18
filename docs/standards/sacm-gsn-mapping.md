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

### `undeveloped` occupies the declaration, so it is confined to Goal and Strategy

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
not make it less of a Solution.

**Order.** For a node: R14 and R15 first, since what is not drawn is not
drawn whatever else it says; then R12, since a citation is neutral; then R1, a
recorded type; then R2 to R11, by the element's class and declaration. R13 adds
its code to whatever kind those give. For a relationship: R15 and E10 first;
then E9, then E5 (before E5a: a defeated challenge is drawn as a challenge, with both codes), then E5a; then E1 to E4; with the codes of E6, E7, E8, E11 and R13 added.

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
| R9 | `ArtifactReference` reached only as the source of `AssertedEvidence` | Solution | Decided | Mapping table. SACM 2.3 §11.15 allows only an `ArtifactReference` as that source |
| R10 | `ArtifactReference` reached only as the source of `AssertedContext` | Context | Decided | v2.2 §2.1; "Context — undecidable by design" above |
| R11 | `ArtifactReference` reached as both, as neither, or in any other way | Neutral, code AF-VIEW-003 | Decided in #493 | [R11](#r11-an-artifactreference-that-is-not-plainly-evidence-or-context) below |
| R12 | Any node element with `isCitation` set | Neutral, code AF-VIEW-004 | Decided | Away elements (GSN3-MOD-003) come with modules |
| R13 | Any element with `isAbstract` set, node or relationship | Its kind by the other rules, with code AF-VIEW-005: the uninstantiated decorator is not drawn yet | Decided | Attribute table above, `uninstantiated` ↔ `isAbstract`. Patterns are a later step, and the code keeps the omission visible |
| R14 | `ArgumentGroup` | Not drawn, code AF-VIEW-006. Its members are references, not contents, and are drawn by their own rules | Decided | Groups are not core GSN |
| R15 | `ArgumentPackageInterface`, `ArgumentPackageBinding`, and the elements they contain at any depth | Not drawn, code AF-VIEW-006 for each | Decided | Modules and contracts are a later step. An interface's elements stand for elements of another package, so drawing them now would draw those twice |

A recorded type agrees with the element when: Goal is a `Claim` declared
`asserted` or `needsSupport`; Assumption, `assumed`; Justification, `axiomatic`;
Strategy, an `ArgumentReasoning`; Solution and Context, an `ArtifactReference`.
Any other recorded type, including a Context recorded on a `Claim`, an Away
element, a Module or a Choice, is drawn by the other rules, with code
AF-VIEW-001. Context on a Claim has no pure-SACM declaration (mapping table,
Context [N]), and the C++ importer maps `gsn:Context` to no SACM type
(`libs/sacm/src/io/name_tables.cpp`), so such a tag has no known writer.

### Edges

| Rule | SACM | Drawn as | Status | Basis |
|---|---|---|---|---|
| E1 | `AssertedInference` whose `reasoning` names an `ArgumentReasoning` | SupportedBy from the target to the Strategy, and from the Strategy to each source. Every edge carries the inference's id | Decided | Mapping table, endpoints swapped; "Strategy is a node in GSN" above |
| E2 | `AssertedInference` without `reasoning` | SupportedBy from the target to each source | Decided | Mapping table |
| E3 | `AssertedEvidence` | SupportedBy from the target to each source | Decided | Mapping table |
| E4 | `AssertedContext` | InContextOf from the target to each source. The target may be a `Claim` or an `ArgumentReasoning`, which is context for a Strategy | Decided | Mapping table. 18 of the samples' 45 `AssertedContext` target an `ArgumentReasoning` |
| E5 | A relationship with `isCounter` set | A neutral edge from each source to the target, pointing at what is challenged: the opposite of SupportedBy. Code AF-VIEW-007. Never SupportedBy or InContextOf. Its `reasoning`, if any, is not linked: code AF-VIEW-009 for it | Decided | A challenge points at what it challenges (GSN v3 Dialectic extension; `notation.md`). A counter relationship drawn as support says the opposite of the file: the C++ SVG export once drew counter-evidence as evidence for a claim |
| E5a | A relationship declared `defeated` | The edges E1 to E4 would give, in the same GSN direction and through its Strategy, drawn neutral. Code AF-VIEW-011 | Decided | What is defeated is the relationship, not its target, so the edge keeps its direction and only stops looking like live support. GSN v3 has Defeated on SupportedBy and InContextOf (gap row 2), which the dialectic step will draw |
| E6 | A relationship declared `needsSupport`, `assumed`, `axiomatic` or `asCited` | Its edge by E1 to E4, code AF-VIEW-008: the declaration is not drawn | Decided | Core GSN has no edge decorator for these |
| E7 | A relationship whose target, or one of whose sources, names no element, names a relationship, or names an element the view does not draw (R14, R15) | The edges to its ends that can be placed are drawn; code AF-VIEW-009 for each end that cannot. With no target that can be placed, no edge is drawn, and its sources are drawn by their own rules | Decided | A claim about an inference has no core GSN form; an unresolved end has nothing to draw. libSACM keeps a dangling reference as read (SACM-REF-001), so the case is reachable |
| E8 | `AssertedInference` whose `reasoning` names no element, or not an `ArgumentReasoning` | Drawn by E2, code AF-VIEW-009 for the reasoning | Decided | The support is still in the file; only the Strategy cannot be placed |
| E9 | A relationship with `isCitation` set | As E5a: the edges E1 to E4 would give, in the GSN direction, drawn neutral. Code AF-VIEW-004 | Decided | As R12. A cited relationship is still support or context, so it keeps its direction |
| E10 | `AssertedArtifactSupport`, `AssertedArtifactContext` | Not drawn, code AF-VIEW-010 | Decided | They relate artifacts to artifacts (§11.17, §11.18); GSN has no such edge |
| E11 | `metaClaim` of an assertion | Not drawn, code AF-VIEW-012 on the assertion. The Claims it names are drawn by their own rules | Decided | It is how SACM holds an assurance claim point (gap rows 5 and 6), a later step |

An element reached by several relationships is still one node, with an edge for
each (D4). One `ArgumentReasoning` named by several inferences is one Strategy,
with each inference's edges. Two inferences with the same ends give two edges,
each carrying its own relationship's id.

### Node text

| Rule | Part | Read from | Status |
|---|---|---|---|
| T1 | Identifier | The `assuranceForge.gsn.identifier` tagged value, else the SACM id | Decided: C++ capability AF-GSN-015 (`GsnIdentifierFor`) |
| T2 | Name | The element's `name`, which holds one language (clause 8.6) | Decided in #493 |
| T3 | Statement | The first `description`, for every kind of element: its value whose `lang` equals the interface language (ignoring case), else one whose primary subtag does (`ja-JP` for `ja`), else its first value, with or without a `lang` | Decided in #493 |

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
| AF-VIEW-007 | A counter relationship, drawn neutral |
| AF-VIEW-008 | A relationship's declaration is not drawn |
| AF-VIEW-009 | A relationship end, or an inference's reasoning, that cannot be placed |
| AF-VIEW-010 | A relationship between artifacts, not drawn |
| AF-VIEW-011 | A defeated relationship, drawn neutral |
| AF-VIEW-012 | An assertion's `metaClaim` link is not drawn |
