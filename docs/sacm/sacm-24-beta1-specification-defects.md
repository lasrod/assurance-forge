# SACM 2.4 Beta 1: inconsistencies to report

Places where the SACM 2.4 Beta 1 documents disagree with each other, or refer
to something that is not there. This page is **input to an OMG submission**,
tracked in [#200](https://github.com/lasrod/assurance-forge/issues/200). Sending
it is a human action.

## Scope

Only one kind of finding is listed: the published documents contradict
themselves. Each item can be checked from the documents alone, without knowing
how any tool uses SACM.

Left out on purpose:

- **Design choices.** The split between core and optional capabilities, the UML
  Profile as an interchange form, and how GSN maps onto SACM are the authors'
  decisions. Clause 15 states that its features are additions to the core
  classes, so they are not listed as conflicts.
- **Anything specific to GSN** or to how Assurance Forge stores arguments.

**Status: preliminary.** The findings were produced by AI agents and checked
twice, the second time by a reviewer instructed to refute each one. The rows in
items 2 to 4 were also read from page images, not only from extracted text. No
person has yet read every cited passage, because the PDFs cannot be searched
(item 10). Corrections are welcome.

Facts about the model can be checked in the XML file itself.
[The last section](#where-to-find-each-model-fact) gives the package path, the
`xmi:id` and the line number for each one.

| Called here | Document |
|---|---|
| the text | Specification PDF, `ptc/26-06-34` |
| the model | `SACM2.4_Metamodel.xml`, `ptc/26-05-28` |
| the profile | XMI for the SACM 2.4 UML Profile, `ptc/26-05-21` |

Clause numbers are as printed.

## 1. No XML schema or namespace is published

Ten of the eleven compliance points require XMI documents "that conform with the
SACM XML Schema produced by applying XMI rules to the normative MOF metamodel"
(clause 2).

- No schema is among the published documents.
- No package in the model or the profile declares a URI.
- The Annex B example takes its stereotypes from
  `http://www.magicdraw.com/schemas/Profile.xmi`, a tool vendor's namespace.

**Question.** Which namespace URIs are authoritative for the metamodel and the
profile, and how is the schema meant to be generated or published?

## 2. Element identity: `gid` in the text, `elementId` in the model

| Source | Says |
|---|---|
| Clause 9.2, Attributes | `gid : String [0..1]` |
| Figure 9.1 | `-elementId : UID [1] {id}` |
| The model | `elementId : UID [1]`; no `gid` |

`elementId` and `UID` do not appear in the prose of any clause.

## 3. `needsSupport` is still listed as a declaration literal

| Source | Literals of `AssertionDeclarationKind` |
|---|---|
| Clause 13.6 and Figure 13.1 | `axiomatic`, `assumed`, `asserted`, `needsSupport` |
| Annex G.6, item 6 | `axiomatic`, `assumed`, `asserted` |
| The model | `axiomatic`, `assumed`, `asserted`, and `byRule` (added by 15.2.1) |

Annex C also still gives concrete syntax for a `needsSupport` claim and
relationship (Figures C5, C13, C21, C29).

**Question.** Annex C presents `defeated` and `asCited` in the same series as the
declaration states (Figures C7, C8, C15, C16, C23, C24, C31, C32). In the model
they are no longer declaration literals. Are those figures now meant to depict
`isDefeated` and `isCitation`?

## 4. Clause text that differs from the model

| Feature | The text | The model |
|---|---|---|
| `SACMElement.abstraction` | 9.2: `[0..1]` | `[0..*]` (Figure 9.1 agrees with the model) |
| `AssertedEvidence.evidence` | 13.13: `[0..*]` | `[1..*]` (Figure 13.1 agrees with the model) |
| `AssertedRelationship.isCounter` | 13.11: `Boolean [1]` | `Boolean [0..1]` |
| `Claim.value` | 13.9: `MultiLangString` | `ExpressionLangString` |
| `BindingPackage` superclass | 11.3: `ModelElement` | `ScopedPackage`, `SACMPackageWithBinding` (10.5 agrees with the model) |
| `SACMModel` superclass | 9.10: `ModelElement` | also `Assertion` (Annex G.6 item 5 agrees with the model) |
| `SACMDependency` superclass | 9.11: `DirectedRelationship` | also `BaseElement` |
| `Rule` superclass | 15.2.3: `BaseElement` | also `ModelElement` |

## 5. Misspelled names in the machine-readable files

| File | Name as published | Elsewhere in the specification |
|---|---|---|
| The model | package `Asssessment` | Assessment |
| The model | `DefeationtMechanismKind` | DefeationMechanismKind |
| The model | `StructuredAssuraceArtifactDiagram` | StructuredAssuranceArtifactDiagram |
| The model | `RealNameValue` | RealNamedValue |
| The model | `GSNContext` followed by a line break (`name="GSNContext&#xA;"`) | GSNContext |
| The profile | `SACMStructuredAssuranceArtifactDiagam` | |
| The profile | `SACMStructureAssuranceCaseDiagram`, `SACMStructureAssuranceTerminologyDiagram` | the argument diagram stereotype is spelled `Structured` |

All of these are in the optional capabilities or the Annex A mapping.

## 6. Names from SACM 2.3 that remain in the text

- 13.4 describes `ArgumentPackageInterface`, `ArgumentPackageBinding` and
  `ArgumentationElement`.
- 13.1 describes `ArgumentBindingPackage`. The model has one `BindingPackage`.
- 14.1 refers to `Property`, which the model no longer has.
- 2.2 and 2.3 refer to "the Common and Predefined diagrams" and "the Evidence
  subpackage".
- Annex B is titled "Examples of Assurance Cases in SACM 2.0 XMI".

## 7. Numbering and cross-references

- Clause 2 numbers its sub-clauses 2.1 to 2.5 and then 6.6 to 6.12. The
  non-normative references are numbered 6.13.
- The "[Additions to …]" headings in clause 15 point at the wrong clause:

| Heading | Points at | The class is defined in |
|---|---|---|
| 15.2.1 AssertionDeclarationKind | 13.7 | 13.6 |
| 15.2.2 Assertion | 13.9 | 13.8 |
| 15.2.5 AssertedInference | 13.13 | 13.12 |
| 15.3.12 BindingPackage | 10.3 | 10.5 |
| ArtifactElement (unnumbered, after 15.3.13) | 9.7 | 14.13 |
| 15.4.8 ArgumentConcept | 13.18 | 13.15 |
| 15.6.1 NamedValue | "xxx" | 9.7 |

## 8. Annex G is silent on six removed features

Annex G is headed "Transformation from SACM 2.3 to SACM 2.4 (normative)". It
covers most renames and merges. It does not mention these, which are in the
2.3 model and not in the 2.4 model:

- `Assertion.metaClaim`
- `SACMElement.gid`
- `AssertedRelationship.reasoning`
- `participantPackage` on the package bindings
- `Property`
- `Description`

**Question.** What should a tool do with each when it reads a 2.3 document?

## 9. Is clause 15 normative once an optional point is claimed?

Clause 15 is headed "(informative)" and its classes are in a model package named
`NonNormative`. Six optional compliance points in clause 2 say that software
"shall be able to import and export" documents conforming to that
"non‒normative MOF metamodel".

An optional capability can carry mandatory requirements once a tool claims it,
so this is not a contradiction in itself. The wording leaves it open.

**Question.** Does the relevant part of clause 15 become normative for a tool
that claims one of those points?

## 10. The text in the PDFs cannot be searched or copied

A note for the document editors, not about SACM itself.

Both published PDFs render correctly on screen, but text extraction produces
garbled characters and text search does not work in the tools tested. For
example, "Copyright" on page 2 extracts as `IJSLCABN`: the capital is lost and
every other letter is replaced. In the files,
the fonts are embedded without a character mapping (no `ToUnicode` entry).
Assistive technology was not tested.

**Request.** Please provide versions with correctly encoded, searchable text.

## Where to find each model fact

Line numbers are for `SACM2.4_Metamodel.xml` as published (SHA-256
`0581dfa0…23db110830`). All paths start at `SACM2.4::Metamodel`.

| Item | Fact | Path | `xmi:id` ends | Line of the `xmi:id` |
|---|---|---|---|---|
| 1 | No `URI` attribute and no `nsURI` anywhere | whole file; also the profile file | | |
| 2 | `elementId : UID [1]` | `Base::SACMElement` | `814687_2677` | 4203 |
| 2 | No attribute named `gid` | whole file | | |
| 3 | Literals `axiomatic`, `assumed`, `asserted`, `byRule` | `Argument::AssertionDeclarationKind` | `908763_2427` | 5876 |
| 4 | `abstraction [0..*]` | `Base::SACMElement` | `102939_2678` | 4275 |
| 4 | `evidence [1..*]` | `Argument::AssertedEvidence` | `813853_2790` | 5606 |
| 4 | `isCounter [0..1]` | `Argument::AssertedRelationship` | `872801_2795` | 5714 |
| 4 | `value : ExpressionLangString` | `Argument::Claim` | `407753_2800` | 5842 |
| 4 | Generalizations `ScopedPackage`, `SACMPackageWithBinding` | `Packaging::BindingPackage` | `973164_2091` | 2993 |
| 4 | Generalizations `ModelElement`, `Assertion`, `Assessment` | `Base::SACMModel` | `390054_2340` | 4423 |
| 4 | Generalizations `DirectedRelationship`, `BaseElement` | `Base::SACMDependency` | `657213_2330` | 4014 |
| 4 | Generalizations `ModelElement`, `BaseElement` | `NonNormative::Rule` | `442249_2189` | 38 |
| 5 | Package named `Asssessment` | `NonNormative` | `200657_4355` | 1765 |
| 5 | `DefeationtMechanismKind` | `NonNormative::Asssessment` | `336823_2193` | 2011 |
| 5 | `StructuredAssuraceArtifactDiagram` | `NonNormative::SACMView` | `786415_2275` | 608 |
| 5 | `RealNameValue` | `NonNormative::NamedValueTypes` | `596689_2320` | 366 |
| 5 | `name="GSNContext&#xA;"` | `Mapping::GSN` | `767598_3597` | 6630 |

For item 5, the three profile names are at lines 1112, 1133 and 1172 of
`ptc/26-05-21`.

For item 8, the six features are in the SACM 2.3 model (`ptc/22-03-13`) under
these names, and no element with any of these names exists in the 2.4 model:
`metaClaim`, `gid`, `reasoning`, `participantPackage`, `Property`, `Description`.
