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

Each item was checked twice, the second time by a reviewer instructed to refute
it. The rows in items 2 to 4 were also read from page images, not only from
extracted text.

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

**Question.** Which namespace URIs should the metamodel and the profile carry,
and will the schema be published?

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

Annex C still gives concrete syntax for `needsSupport`, `defeated` and `asCited`
claims and relationships (Figures C5, C7, C8, C13, C15, C16, C21, C23, C24, C29,
C31, C32). None of the three is a literal in the model.

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

## 9. Clause 15 is informative but carries requirements

Clause 15 is headed "(informative)" and its classes are in a model package named
`NonNormative`. Six compliance points in clause 2 say that software "shall be
able to import and export" documents conforming to that "non‒normative MOF
metamodel".

**Question.** Is clause 15 normative for a tool that claims one of those points?

## 10. The PDFs have no usable text layer

A note for the document editors, not about SACM itself. Both PDFs embed their
fonts without a Unicode mapping, so the text cannot be searched or copied, and a
screen reader cannot read it.
