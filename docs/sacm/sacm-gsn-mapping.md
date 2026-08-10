# GSN to SACM 2.3 mapping

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
