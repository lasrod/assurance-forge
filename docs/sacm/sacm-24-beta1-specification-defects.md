# SACM 2.4 Beta 1 specification defects

Defects in the SACM 2.4 Beta 1 text (`ptc/26-06-34`) and its normative
machine-readable model (`SACM2.4_Metamodel.xml`, `ptc/26-05-28`), found while
assessing what it would take to implement the beta. See the
[impact analysis](sacm-2.4-beta1-impact.md) for how they were found and what the
review could not read.

This page is **input to an OMG submission**, not a description of Assurance
Forge. It is written so each section can be filed as one issue through the
public form linked from <https://www.omg.org/spec/SACM/2.4/Beta1/About-SACM>.
Sending it is a human action, tracked in
[#200](https://github.com/lasrod/assurance-forge/issues/200).

Every quotation is from the published PDF. "The model" means
`SACM2.4_Metamodel.xml`. Class-level facts about the model can be reproduced
with `python tools/sacm/diff_sacm24_metamodel.py` and are recorded in the
[metamodel inventory](sacm-2.4-beta1-metamodel-inventory.md).

The defects are ordered by how much they block an implementation.

## B1 — No instance-document format is determined

Clause 2 requires, for every compliance point, import and export of "XMI
documents that conform with the SACM XML Schema produced by applying XMI rules to
the normative MOF metamodel".

- No XML Schema is published with the beta.
- No package in the model carries a `URI`, so XMI rules produce no namespace.
- The only example document, Annex B, is a `uml:Model` with stereotypes applied
  from `http://www.magicdraw.com/schemas/Profile.xmi`. That is a tool vendor's
  namespace, and the example is in the UML Profile dialect, which is a separate
  and optional compliance point.

**Why it matters.** Two tools that implement the mandatory Packaging point have
no basis for reading each other's files. SACM 2.3 had the same gap, and the
result is that the EMF reference implementation and independent implementations
use different namespaces and need dialect-specific code to interoperate.

**Suggested resolution.** Declare a namespace URI on the metamodel package,
publish the generated XSD as a normative machine-readable document, and add a
native (non-profile) XMI example to Annex B.

## B2 — Element identity differs between the text and the model

Clause 9.2 lists as an attribute of `SACMElement`:

> gid : String [0..1] ‒ a unique identifier that is unique within the scope of
> the model instance

The model has no `gid`. It has `elementId : UID [1]`, and the class diagram in
9.1 shows `-elementId : UID [1] {id}`.

**Why it matters.** Identity is what every reference in a document resolves
against. The two sources differ in name, type and multiplicity: optional in the
text, mandatory in the model.

**Suggested resolution.** Replace the `gid` line in 9.2 with `elementId`, and
state how a 2.3 `gid` maps to it in Annex G.

## B3 — The assertion-declaration literals are stated three ways

| Source | Literals |
|---|---|
| Clause 13.6 | `axiomatic`, `assumed`, `asserted`, `needsSupport` |
| Annex G.6 item 6 | "one of axiomatic, assumed, asserted" |
| The model | `axiomatic`, `assumed`, `asserted`, `byRule` |

Annex C additionally gives concrete syntax for `needsSupport`, `defeated` and
`asCited` claims and for the same three states of each asserted relationship
(Figures C5, C7, C8, C13, C15, C16, C21, C23, C24, C29, C31, C32). None of the
three is a declaration literal in the model. `defeated` became
`ArgumentConcept.isDefeated`, `asCited` became the derived `isCitation`, and
`needsSupport` appears to have become `SACMElement.needsDevelopment`.

**Why it matters.** A reader cannot tell whether `needsSupport` is a valid value,
or what `byRule` means: 13.6 does not describe it.

**Suggested resolution.** Make 13.6 list the model's four literals with a
definition of `byRule`, state in Annex G what each removed 2.3 literal becomes,
and rewrite the Annex C captions in terms of the attributes that replaced them.

## B4 — Normative classes depend on non-normative ones

Clause 15 is headed informative and its classes sit under `NonNormative` in the
model. Seventeen features of normative classes are typed by those classes, and
one normative class specializes one:

| Normative class | Feature | Non-normative type |
|---|---|---|
| `SACMElement` | `changeType`, `consistencyStatus`, `validityDuration` | `ChangeTypeKind`, `ConsistencyStatusKind`, `Duration` |
| `SACMModel` | (generalization) | `Assessment` |
| `Assertion` | `assertionValue`, `assertionDeclarationRule` | `Rule` |
| `ArgumentConcept` | `defeationMechanism`, `defeationRule` | `DefeationtMechanismKind`, `Rule` |
| `AssertedInference` | `inferenceType`, `inferenceRule` | `InferenceTypeKind`, `Rule` |
| `Activity`, `Event` | `duration`, `dateTimeOccurrence` | `Duration`, `DateTime` |
| `StringNamedValue` | `numberLiteral` | `NumericalNamedValue` |
| `AssuranceCasePackage`, `BindingPackage`, `ArgumentElement`, `ArtifactElement`, `TerminologyElement` | diagram containments | `SACMDiagram` and its specializations |

None of these features is listed in clauses 9 to 14. Clause 2 then defines six
compliance points (Join, Advanced Argument, SACMView, Assessment,
DateTimeDuration, NamedValue Types) as conformance to a "non‒normative MOF
metamodel defined in the informative" subpackage.

**Why it matters.** An implementation of the mandatory point alone cannot tell
whether it must read and write these features. The text says no and the model
says yes.

**Suggested resolution.** Either make clause 15 normative-but-optional and say
so, or move these features onto the clause 15 classes so the core packages stand
alone.

## B5 — Further text and model divergences

| Subject | Text | Model |
|---|---|---|
| `AssertedEvidence.evidence` multiplicity | 13.13: `[0..*]` | `[1..*]` |
| `Claim.value` type | 13.9: `MultiLangString` | `ExpressionLangString` |
| `BindingPackage` superclass | 11.3: `ModelElement` (10.5 agrees with the model) | `ScopedPackage`, `SACMPackageWithBinding` |
| `SACMModel` superclass | 9.10: `ModelElement` | `ModelElement`, `Assertion`, `Assessment` |
| `GSNContext` superclass | A.1.11: `GSNAsset`, `Artifact::Artifact` | `Context` |
| `SACMElement.abstraction` multiplicity | 9.2: `[0..1]` | `[0..*]` |

**Suggested resolution.** Regenerate the clause text for these classes from the
model, or correct the model, and state which source governs on conflict.

## B6 — Classifier names in the model are misspelled

Under XMI rules a class name becomes an element or type name, so these would be
fixed into every conforming file:

| In the model | Presumably intended |
|---|---|
| package `Asssessment` | `Assessment` |
| `DefeationtMechanismKind` | `DefeationMechanismKind` |
| `StructuredAssuraceArtifactDiagram` | `StructuredAssuranceArtifactDiagram` |
| `RealNameValue` | `RealNamedValue` |
| `GSNContext` followed by a line break (`name="GSNContext&#xA;"`) | `GSNContext` |

The UML Profile (`ptc/26-05-21`) has its own: `SACMStructuredAssuranceArtifactDiagam`
and `SACMStructureAssuranceCaseDiagram`.

## B7 — Annex G does not define a transformation

Annex G is headed "Transformation from SACM 2.3 to SACM 2.4 (normative)". It is a
list of remarks such as "TagValue is renamed NamedValue". It does not define what
a conforming tool does with:

- an `Assertion.metaClaim`, which is removed with no stated replacement (the
  apparent one is `Claim.subject`, pointing the other way);
- `assertionDeclaration` values `needsSupport`, `defeated` and `asCited`;
- a `gid`;
- `ArgumentPackageBinding`, `ArtifactPackageBinding` and
  `TerminologyPackageBinding` and their `participantPackage` references, now
  one `BindingPackage` with no such reference;
- `AssertedRelationship.reasoning`, now inverted to
  `ArgumentReasoning.relationship`;
- `Property`, `Description`, and the three per-domain group classes.

**Why it matters.** A normative transformation that cannot be executed leaves
every 2.3 file's migration to each implementer, and the results will differ.

**Suggested resolution.** Give a class-by-class and feature-by-feature table, or
mark the annex informative.

## B8 — Editorial

- Clause 2.1 announces "eleven compliance points". The sub-clauses that follow
  are numbered 2.2 to 2.5 and then 6.6 to 6.12, and the non-normative references
  are numbered 6.13.
- Annex B is titled "Examples of Assurance Cases in SACM 2.0 XMI".
- Clause 13.4 describes `ArgumentPackageInterface`, `ArgumentPackageBinding` and
  `ArgumentationElement`, which are 2.3 names.
- Clause 13.1 refers to `ArgumentBindingPackage`, which does not exist in the
  model; the single `BindingPackage` replaced it.
- The non-normative references cite the GSN Community Standard v2 (SCSC-141B,
  2018). Version 3 (SCSC-141C) is current, and the Annex A mapping has no class
  for its Assurance Claim Points.
- Both published PDFs embed their fonts without a Unicode mapping, so the text
  cannot be searched, copied or read by assistive technology.

## Related

- [SACM 2.3 specification defects](sacm-23-specification-defects.md). D5
  (`Resource.location` missing from the model) is fixed in the beta. D7 (no
  namespace) is not: it is B1 above.
- [GSN / SACM metamodel gaps](sacm-gsn-metamodel-gaps.md).
