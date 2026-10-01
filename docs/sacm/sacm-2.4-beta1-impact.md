# SACM 2.4 Beta 1: impact analysis

**Status: analysis, 2026-10-02. Nothing here changes what the project claims.**
SACM 2.3 (`formal/23-05-08`) remains the only version `libs/sacm` implements and
the only one the conformance matrix records. Raised by
[#474](https://github.com/lasrod/assurance-forge/issues/474).

## Summary

OMG published SACM 2.4 Beta 1 in September 2026. It is a rewrite of the
metamodel, not a revision of it, and it cannot be implemented as an interchange
format yet.

- **It is not the standard.** OMG labels the beta "for informational purposes"
  and says the formal version "is the version that should be followed for
  compliance". 2.3 is still that version.
- **Almost nothing carries over unchanged.** Of the 55 classifiers in the 2.3
  model, 23 are gone and 30 of the remaining 32 changed. Only `Participant` and
  `Technique` are identical. The 2.4 model has 131 classifiers.
- **The file format is not defined.** Every compliance point requires XMI that
  conforms to "the SACM XML Schema", and no schema, namespace URI or native
  example document is published. The only example in the specification is a UML
  model with a MagicDraw profile applied.
- **The specification text and its normative model disagree** on identity
  (`gid` against `elementId`), on the assertion-declaration literals, and on
  several types and multiplicities. A conformance test cannot be written against
  a requirement the two sources state differently.
- **The direction is good for us.** The two changes
  [#200](https://github.com/lasrod/assurance-forge/issues/200) asked the RTF for
  both landed: defeat now applies to every argument element, and `Claim.subject`
  reaches any element. GSN Choice cardinality, Challenge and undeveloped all get
  a standard home.

**Recommendation:** do not implement 2.4 against Beta 1. Report the defects below
to OMG while finalization is open, keep the 2.3 claim as it is, and revisit when
one of the [triggers](#when-to-revisit) fires. A 2.4 implementation is a second
metamodel beside the first, on the scale of the original library build.

## What was reviewed

Fetched with `bash scripts/fetch-sacm24-beta1-references.sh` into
`third_party/sacm-2.4-beta1/` (git-ignored). The script fails if OMG republishes
a file, because this page would then describe something else.

| Document | OMG ID | Standing | SHA-256 |
|---|---|---|---|
| Specification PDF, 161 pages | `ptc/26-06-34` | Normative | `25f32d1c…c39997ff5` |
| `SACM2.4_Metamodel.xml` | `ptc/26-05-28` | Normative, machine readable | `0581dfa0…23db110830` |
| Specification with change bars, 181 pages | `ptc/26-06-35` | Informative | `6840a0f6…ef1fe5f96` |
| XMI for the SACM 2.4 UML Profile | `ptc/26-05-21` | Informative, machine readable | `9149a041…1918838b` |

Source: <https://www.omg.org/spec/SACM/2.4/Beta1/About-SACM>.

**State of the revision.** All 59 `SACM24-*` issues, which were all `open` when
[the watch list](sacm-2.4-watch.md) was written in July, are now `closed`. The
public list of issues against 2.4 Beta 1 is empty. The specification page links
a `SACM25` tracker that is members-only, and a public "report an issue" form.
No date for the formal 2.4 is published.

**How the class tables were produced.** `python tools/sacm/diff_sacm24_metamodel.py`
compares the two machine-readable models and prints every removed, added and
changed classifier with its features. Every class-level statement on this page
comes from that output, not from reading diagrams.

**Limit of the text review.** Both PDFs embed their fonts without a Unicode map,
so ordinary text extraction returns nothing readable. The text was recovered by
matching each embedded glyph outline against the installed Times New Roman,
Arial and Courier New fonts, which recovered 98% of the characters. The
remaining 2% is set in Aptos, which was not available to match, and is almost
entirely labels inside figures. Clause prose, attribute lists, constraints and
the Annex B listing were all read; the figures were not.

## What changed in the metamodel

### Structure

| | 2.3 | 2.4 Beta 1 |
|---|---|---|
| Classifiers | 55 | 131 |
| Packages | Base, AssuranceCase, Terminology, Argumentation, Artifact | Foundation, Base, Packaging, Terminology, Argument, Artifact, Mapping (GSN, CAE), NonNormative (seven sub-packages) |
| Mandatory compliance point | Assurance case | Packaging |
| Optional compliance points | 4 | 10 |
| Model form | UML model exported from MagicDraw | The same, with the classes nested inside a `uml:Profile` |

The 131 classifiers split as 66 in the six core packages, 30 in the GSN and CAE
mapping packages, and 35 under `NonNormative`.

- **New `Foundation` package.** `Element`, `NamedElement`, `Namespace`,
  `Package`, `PackageableElement`, `DirectedRelationship` and `VisibilityKind`,
  copied from UML. `SACMElement` now extends `PackageableElement`, so every
  element has UML visibility and ownership.
- **Multiple inheritance throughout.** `Artifact` is both an `ArtifactAsset`
  and an `ArtifactReference`. `Join` is both a `Claim` and an
  `ArtifactReference`. `SACMModel`, the base of every package and group, is a
  `ModelElement`, an `Assertion` and an `Assessment`. The library's model is a
  single-inheritance C++ hierarchy that mirrors 2.3.
- **Optional capabilities are added onto the core classes.** Clause 15 defines
  seven optional capabilities as "additions to the existing items". In the model
  they are already merged in: seventeen features of core classes are typed by
  `NonNormative` classifiers, and `SACMModel` specializes `Assessment`. This is
  the authors' design, not an error. For an implementer it means the core
  classes cannot be read from the model file without also meeting those types.

### Removed and renamed

All of these were predicted by the watch list from the draft issue text, and all
of them landed.

| 2.3 | 2.4 Beta 1 | Watch row |
|---|---|---|
| `AssertionDeclaration` (5 literals) | `AssertionDeclarationKind`: `axiomatic`, `assumed`, `asserted`, `byRule` | 001 |
| `defeated` literal | `ArgumentConcept.isDefeated : Boolean[0..1]` | 002 |
| `Assertion.metaClaim` | removed | 003 |
| — | `Claim.subject : SACMElement[0..*]`, `Claim.whole : SACMModel[0..*]` | 004 |
| `AssertedArtifactSupport`, `AssertedArtifactContext` | removed; `AssertedEvidence` and `AssertedContext` cover them | 005 |
| Relationship ends typed `ArgumentAsset`, restricted by OCL | New abstract `Targetable`; each relationship redefines its own ends | 006 |
| `gid : String[0..1]` | `elementId : UID[1]` | 007 |
| `isAbstract`, `citedElement`, settable `isCitation` | `isSACMAbstract`, `cited` plus `citedIRI`, derived `isCitation` | 007 |
| `UtilityElement`, `Note`, `TaggedValue`, `ImplementationConstraint` | `BaseElement`, `SACMComment`, `NamedValue`, `SACMConstraint` | 008 |
| `Description` class | `SACMElement.description` attribute | 008 |
| `LangString` | merged into `MultiLangString`, moved to Terminology | 009 |
| `ModelElement.name : LangString[1]` | `elementName : ExpressionLangString[0..1]` plus derived `/name` | 010 |
| `ArgumentationElement` | `ArgumentElement` | 011 |
| `XPackageInterface`, three `XPackageBinding` classes | `XInterfacePackage`, one `BindingPackage` | 012 |
| `ArtifactElement` in Base, parent of everything | moved to Artifact; `ModelElement` is the shared base | 013 |
| `ArgumentGroup`, `ArtifactGroup`, `TerminologyGroup` | one `Group` | 016 |
| `Property` | removed; `NamedValue` | 017 |
| Package content rules as prose | ownership rules made part of the mandatory point | 018 |

Further changes the watch list did not have:

- **`AssertedRelationship.reasoning` is gone.** The link is inverted:
  `ArgumentReasoning.relationship : AssertedRelationship[0..*]`.
- **`needsSupport` is replaced by `SACMElement.needsDevelopment`**, available on
  every element, not only assertions.
- **Assets nest.** `ArgumentAsset.ownedArgumentAsset` and
  `ArtifactAsset.ownedArtifactAsset` let an asset contain assets.
- **`ArtifactReference.reference` is typed `SACMElement`**, not
  `ArtifactElement`, so an argument can cite a whole package as evidence.
- **`Artifact.date` became `versionReleaseDate`**, with new
  `externalDocumentIRI` and `internalReference`.
- **`Resource.location` is now in the model.** Its absence from `ptc/22-03-13`
  is one of the [2.3 defects](sacm-23-specification-defects.md) we recorded.
- **New `SACMDependency`**, and `SACMModel.isPattern` / `isModel` on every
  package and group.

### Annex G does not give a migration

Annex G is titled "Transformation from SACM 2.3 to SACM 2.4" and marked
normative. It is two pages of numbered remarks ("TagValue is renamed
NamedValue"). It covers most renames and merges, but it does not mention
`metaClaim`, `gid`, `AssertedRelationship.reasoning`, `participantPackage`,
`Property` or `Description`, all of which are removed. For those, a 2.3-to-2.4
converter would be our own design, and the specification gives no way to check
it.

## Why Beta 1 cannot be implemented yet

### No interchange format is determined

Clause 2 requires each compliance point to "import and export XMI documents that
conform with the SACM XML Schema produced by applying XMI rules to the normative
MOF metamodel". Three things are missing.

1. **No schema is published.** The specification page lists four documents and
   none is an XSD.
2. **The normative model declares no namespace URI.** No package in
   `SACM2.4_Metamodel.xml` has a `URI`. This was already true of 2.3, and is why
   our namespace is a project pin. 2.4 does not fix it.
3. **The model's names are not ready to be element names.** Like the 2.3 model
   it is a UML export from MagicDraw, now with the classes nested inside a
   `uml:Profile` named `SACM2.4`. Under XMI rules a class name becomes an element
   name, and several are visibly unedited: `Asssessment`,
   `DefeationtMechanismKind`, `StructuredAssuraceArtifactDiagram`,
   `RealNameValue`, and `GSNContext` with a trailing line break inside the name.

The one example document, Annex B, is still headed "Examples of Assurance Cases
in SACM 2.0 XMI". It is a `uml:Model` of `uml:Class` and `uml:Dependency`
elements with stereotypes applied from
`http://www.magicdraw.com/schemas/Profile.xmi`. That is a vendor namespace, and
it is the UML Profile dialect, which is the one compliance point we do not claim
in 2.3 either.

So two tools that each implement 2.4 Beta 1 natively have no basis for reading
each other's files. For 2.3 we had the EMF reference implementation and
third-party files to build an [interoperability corpus](sacm-interop-corpus.md)
from. For 2.4 there is no reference implementation and no file to test against.

### The text and the model contradict each other

Each row is a place where a test would have to pick one source and fail the
other.

| Subject | Specification text | `SACM2.4_Metamodel.xml` |
|---|---|---|
| Element identity | 9.2 lists `gid : String[0..1]` | `elementId : UID[1]`; no `gid` |
| Declaration literals | 13.6: `axiomatic`, `assumed`, `asserted`, `needsSupport` | `axiomatic`, `assumed`, `asserted`, plus `byRule` from clause 15; no `needsSupport` |
| `AssertedEvidence.evidence` | 13.13: `[0..*]` | `[1..*]` |
| `Claim.value` | 13.9: `MultiLangString` | `ExpressionLangString` |
| `BindingPackage` superclass | 10.5: `SACMPackageWithBinding`, `ScopedPackage`; 11.3: `ModelElement` | `ScopedPackage`, `SACMPackageWithBinding` |
| `SACMElement.abstraction` | 9.2: `[0..1]` | `[0..*]` |
| `AssertedRelationship.isCounter` | 13.11: `Boolean [1]` | `Boolean [0..1]` |

Other signs that the text has not been through editing:

- Clause 2.1 announces "eleven compliance points". Clause 2 then numbers them
  2.2 to 2.5 and 6.6 to 6.12.
- Six compliance points require conformance to a "non-normative MOF metamodel
  defined in the informative" clause 15, so it is unclear whether that clause
  binds a tool claiming one of them.
- Annex C still gives concrete syntax for `needsSupport`, `defeated` and
  `asCited` claims and relationships. The model has none of the three as a
  declaration.
- 13.4 describes `ArgumentPackageInterface`, `ArgumentPackageBinding` and
  `ArgumentationElement`, which are the 2.3 names.

The full list, limited to what can be checked from the documents alone, is in
[Beta 1 inconsistencies to report](sacm-24-beta1-specification-defects.md).
It was reviewed a second time on 2026-10-02 to remove anything that reflected
our GSN-only use of SACM or that the authors plainly intended.

## Effect on GSN support

The canvas and the SVG export draw GSN, and 2.4 changes no GSN symbol. The effect
is on how a GSN argument is stored, so the issue's "GSN canvas and
visualization" area is the least affected part of the application.

Annex A adds a GSN mapping as classes (`Mapping/GSN`, 19 classifiers). It is
marked informative in the text, although the classes sit in the normative model
file. It cites GSN v2 (SCSC-141B, 2018), not v3, and it differs from
[the mapping we implement](sacm-gsn-mapping.md), which follows the SCSC GSN
Metamodel v2.2.

| GSN | Ours today (2.3) | 2.4 Beta 1 Annex A |
|---|---|---|
| Goal | `Claim` | `GSNGoal`, a `Claim` |
| Strategy | `ArgumentReasoning` | `GSNStrategy`: `ArgumentReasoning` and `Join`, so also a `Claim` |
| Solution | `ArtifactReference` | `GSNSolution`, an `Artifact` (itself now an `ArtifactReference`) |
| Context | no SACM class; preserved | `GSNContext`: `Context` in the model, `Artifact` in the text |
| Assumption, Justification | `Claim`, `assumed` / `axiomatic` | the same |
| Undeveloped | `assertionDeclaration = needsSupport` | `needsDevelopment`, on any element |
| Uninstantiated | `isAbstract` | `isSACMAbstract` |
| Choice, and "m of n" | no SACM class; cardinality has no carrier | `GSNChoice`, a `Join` with `lowerBound` / `upperBound` |
| Challenge | partial; on assertions only | `isChallenge`, redefining `isCounter`, on every GSN relationship |
| Defeated | declaration literal, assertions only | `isDefeated`, any argument element |
| Assurance Claim Point | `metaClaim`, plus `assuranceForge.acp` tagged values where `metaClaim` cannot reach | no ACP class; `Claim.subject` can reference any element |
| Away elements | `isCitation` / `citedElement` | `cited`, or `citedIRI` across models |
| Contract module | `ArgumentPackageBinding` | the single `BindingPackage` |
| Architecture view | planned (AF-MOD-010) | `GSNArchitectureView` diagram class |

What this means:

- **Three of our recorded gaps would close.** Defeat on Solutions and
  Strategies, ACPs on Solutions and Contexts, and Choice cardinality are all
  expressible in 2.4. The first two are what #200 was going to ask the RTF for.
- **Our ACP storage would have to move.** `metaClaim` is deleted, so the ACPs we
  write today have no 2.4 form except through `Claim.subject`. The vendor
  `assuranceForge.acp` tagged values would become unnecessary.
- **GSN v3 is still not covered.** Annex A has no ACP class and no dialectic
  vocabulary beyond `isChallenge`. The SCSC metamodel remains at v2.2. Neither
  body has published a GSN v3 metamodel, so our v3 features would still rest on
  our own reading.
- **Strategy changes meaning.** Under Annex A a strategy is a claim. Under the
  SCSC mapping it is reasoning attached to an inference. A file written one way
  does not read as the other.

### Layout

[Layout policy](sacm-layout-policy.md) keeps coordinates out of the library, and
that still holds: `SACMView` is an optional compliance point. The watch list
said SACM would define no geometry itself. Beta 1 does: `NonNormative/SACMView/DI`
contains `Bounds`, `Point`, `Shape`, and `Edge` with `waypoint`, copied from OMG
Diagram Interchange. If layout ever has to travel inside a SACM file, this is the
standard target, and our deterministic layout output already has that shape.

## Exposure in this repository

| Area | Size today | What 2.4 would require |
|---|---|---|
| `libs/sacm` model, XMI reader and writer, validators, commands | about 10,400 lines, 38 element kinds | A second model. The class hierarchy, the name tables, containment rules and every validator are 2.3-shaped. |
| `libs/sacm` tests and fixtures | about 5,400 lines, 34 fixtures | New fixtures, with nothing external to derive them from |
| Conformance matrix | 38 rows, 37 verified, all `SACM23-*` | A separate `SACM24-*` matrix; the 2.3 one must not be edited to fit |
| `src/sacm_adapter` | about 5,500 lines | New projection for strategy, context, undeveloped, ACP and defeat |
| Legacy `sacm` and `parser` layers | being retired ([#350](https://github.com/lasrod/assurance-forge/issues/350)) | Should be gone before 2.4 work starts, or the work is done twice |
| Projects users already have | SACM 2.3 files | A 2.3-to-2.4 converter that Annex G does not specify, and a decision on whether saving upgrades a file |

Two constraints from the project's own rules apply. Round-trip integrity means a
2.3 file must still save as the same 2.3 file, so 2.4 support is additive: a
version the document declares, not a replacement. And the library's
`StandardVersion` seam was added for exactly this; `V2_4` is one enum value, but
everything behind it is new.

## Options

| | Option | Cost | Result |
|---|---|---|---|
| A | **Stay on 2.3, report defects, watch** | Small | Conformance claim stays true. We influence the formal text. |
| B | Read-only 2.4 import | Medium | Not possible today: there is no file format to read. |
| C | Dual-version library | Large | The eventual shape, once the format exists. |
| D | Move to 2.4 and drop 2.3 | Large | Breaks every existing project file and the only conformance claim we can evidence. |

A is recommended. C is where this ends up, and the preparation for it is cheap
discipline, not code: keep version-specific vocabulary (`metaClaim`,
`needsSupport`, `TaggedValue`) from spreading through `src/` as user-level
concepts, as the watch list already asks.

## Decision and preparation

**Decided 2026-10-02: implementation is postponed.** Too much of the beta is
missing or unclear to build against. What could be prepared without the missing
parts has been:

| Prepared | Where | Use |
|---|---|---|
| Pinned copies of the four beta documents | `scripts/fetch-sacm24-beta1-references.sh` | Detects a republished beta |
| Class-level comparison with 2.3 | `tools/sacm/diff_sacm24_metamodel.py` | Re-run against a Beta 2 or the formal version |
| Baseline of the beta model | [metamodel inventory](sacm-2.4-beta1-metamodel-inventory.md) | A later version is diffed against this page |
| List of inconsistencies for OMG | [Beta 1 inconsistencies to report](sacm-24-beta1-specification-defects.md) | Input to OMG while finalization is open |
| Where 2.3 vocabulary sits in our code | [below](#where-the-23-vocabulary-sits-today) | Scope of the eventual migration |

Not prepared, deliberately:

- **No `StandardVersion::V2_4` and no detection of a 2.4 document.** With no
  namespace to detect, any rule would be a guess, and a change under `libs/sacm`
  needs a conformance row that could not be verified.
- **No 2.4 classes, fixtures or matrix rows.** The 2.3 claim is untouched.
- **No refactoring of the ACP code ahead of need.** See below.

Sending the defect list to OMG is a human action, tracked in #200.

### Where the 2.3 vocabulary sits today

The watch list asked for two disciplines so that a later migration stays small.
Checked on 2026-10-02:

- **Branching on declaration literals is contained.** Outside `libs/sacm`, only
  `src/sacm_adapter` tests `AssertionDeclaration::` values (two files). That is
  the intended seam.
- **`metaClaim` has spread into `core`.** ACP editing and its commands
  (`src/core/acp/acp_editing.cpp`, `src/core/commands/acp_commands.cpp`), audit
  replay (`src/core/audit/event_replayer.cpp`), the argument sync and the
  adapter all call `meta_claims` operations by name. This is the largest single
  piece of migration work: in 2.4 the reference points the other way, from the
  confidence claim to its subject.
- **The audit hash names it.** `src/core/audit/canonical_model_hash.cpp` feeds
  the literal `"metaClaims"` into the canonical hash. Renaming the field would
  change the hash of every existing audit record, so the label has to stay even
  after the storage moves.
- **One user-visible string names a 2.3 feature:** "Could not assign a SACM gid
  for confidence storage." 2.4 has no `gid`.

None of this is worth changing now. Rewriting working ACP code against a target
that may still move would add risk for no present benefit. It is recorded so the
work is scoped when a trigger fires.

### Order of work once a trigger fires

1. Re-run the fetch script and the diff tool; update this page and the inventory.
2. Finish the legacy-bridge retirement (#350) first, so one model is migrated,
   not two.
3. Decide the GSN mapping: Annex A or the SCSC metamodel, per construct, in
   [the mapping page](sacm-gsn-mapping.md).
4. Start a separate `SACM24-*` conformance matrix from the formal clauses.
5. Add the 2.4 model beside the 2.3 one behind `StandardVersion`, reader first,
   with fixtures taken from whatever external files then exist.
6. Design the 2.3-to-2.4 conversion as an explicit, user-invoked action. Opening
   and saving a 2.3 file must keep producing the same 2.3 file.

### When to revisit

Re-run the fetch script and the diff tool, and re-read this page, when any of
these happens:

- OMG publishes the formal SACM 2.4, or a Beta 2.
- A namespace URI, an XSD or an Ecore for 2.4 appears, from OMG or from the
  maintainers of the 2.3 EMF implementation.
- Any other tool writes a native 2.4 file we can obtain.
- SCSC publishes a GSN metamodel for v3, which would settle the Strategy and
  ACP mappings independently of Annex A.

## Open questions

1. Is `needsSupport` meant to survive? The text keeps it and the model replaces
   it with `byRule`. `needsDevelopment` appears to be the intended replacement.
2. Is Annex A's GSN mapping meant to supersede the SCSC metamodel's? The two
   disagree on Strategy, Solution and Context.
3. Does clause 15 bind a tool that claims one of its compliance points? It is
   headed informative, and clause 2 uses "shall" about it.
4. Will the formal version define a native XMI namespace, or is the UML Profile
   the intended interchange?
