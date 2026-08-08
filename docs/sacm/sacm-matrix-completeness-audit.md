# SACM 2.3 matrix-completeness audit

**Date:** 2026-08-08 · **Matrix audited at:** `43ed94f` (main) · **Findings tracked:**
[#333](https://github.com/lasrod/assurance-forge/issues/333),
[#334](https://github.com/lasrod/assurance-forge/issues/334),
[#335](https://github.com/lasrod/assurance-forge/issues/335),
[#336](https://github.com/lasrod/assurance-forge/issues/336),
[#337](https://github.com/lasrod/assurance-forge/issues/337)

[#295](https://github.com/lasrod/assurance-forge/issues/295) requires an audit
that answers a different question from the `sacm_matrix_check` gate: not "do the
existing rows have tests," but **"is a normative obligation missing from the
[conformance matrix](sacm-conformance-matrix.md) entirely?"** This record is that
audit. Before it, [the repository baseline](../quality/repository-baseline.md)
listed matrix completeness against the specification as unmeasured.

## Method

Four independent readers each audited an assigned slice of the pinned normative
sources — the specification PDF (`formal/23-05-08`) and machine-readable model
(`ptc/22-03-13`) under `third_party/sacm-2.3/`, hash-pinned by
`scripts/fetch-sacm23-references.sh`:

1. Clauses 1–2 (conformance), the annexes, and the XMI obligations the
   compliance points entail
2. Clause 8 (Base) and clause 9 (AssuranceCase)
3. Clause 10 (Terminology) and clause 12 (Artifact)
4. Clause 11 (Argumentation)

Each reader was instructed to judge from the specification text, **not** from
the matrix's description of itself, and to enumerate prose obligations — shall
statements, OCL constraints, semantic rules — that a purely structural
inventory cannot capture. The readers were AI agents; every load-bearing code
claim below was afterwards re-verified by hand against the cited source before
being recorded here. The audit checked matrix *coverage* (does a row own the
obligation) and, where a gap hinged on behavior, the implementation; it did not
re-verify the strength of every cited test — that remains the
[verification records'](verification/README.md) job.

## Verdict

**The structural coverage holds; the gaps are prose constraints.** Spot-checks
of ~20 classes across all five packages confirmed the
[generated inventory](sacm-2.3-metamodel-inventory.md) is faithful to the pinned
machine-readable model, so the bidirectional gates
(`SACM23_LIB_001_ElementKindsMatchNormativeInventory`,
`SACM23_XMI_003_KnownAttributesCoverTheNormativeInventory`) rest on a sound
chain. What the matrix does not capture is a specific, now-bounded set:
obligations the specification states in prose — validation constraints, one
attribute the machine model dropped, and one XMI import form — plus three
`verified` rows whose claim text is broader than their evidence.

None of the findings invalidates the four
[compliance-point claims](sacm-compliance-points.md), which are import/export
claims pinned by the `SACM23_CP_*` tests — but two findings touch import
capability directly (`xmi:type` dispatch, `Resource.location`), and the release
evidence package #295 requires must not freeze the matrix while these are
neither fixed nor explicitly waived.

## Obligations with no owning matrix row

| Clause | Obligation | State in the implementation | Tracked |
|---|---|---|---|
| 8.2 | gid is "unique within the scope of the model instance" | `gid` appears nowhere in `validate.cpp`; duplicates validate clean | [#335](https://github.com/lasrod/assurance-forge/issues/335) |
| 8.4 | ExpressionLangString: "If expression is not empty, then +content should be empty" | No check, no test | [#335](https://github.com/lasrod/assurance-forge/issues/335) |
| 9.3 OCL | An AssuranceCasePackageInterface may only contain interface-typed children | Interface children flow through ordinary containment roles unchecked | [#333](https://github.com/lasrod/assurance-forge/issues/333) |
| 9.4 OCL | Participants are exactly AssuranceCasePackage or its Interface (`oclIsTypeOf`) | `dynamic_cast` accepts an AssuranceCasePackageBinding as participant | [#333](https://github.com/lasrod/assurance-forge/issues/333) |
| 10.5, 10.6, 12.4, 12.5 | Terminology/Artifact interface and binding content-citation constraints ("must only contain … isCitation = true citing …") | Unenforced; the four classes are instantiated by no test beyond the kind-existence sweep; no row cites these clauses | [#333](https://github.com/lasrod/assurance-forge/issues/333) |
| 10.10 OCL | A concrete Expression references only concrete ExpressionElements | Unenforced; mechanically checkable | [#335](https://github.com/lasrod/assurance-forge/issues/335) |
| 11.4 | "If an ArgumentPackage has nested ArgumentPackages, then it is only allowed to contain ArgumentPackages" | Unenforced, untested, undecided | [#334](https://github.com/lasrod/assurance-forge/issues/334) |
| 11.5 OCL, 11.6 | ArgumentPackageBinding/Interface citation constraints | Unenforced; PKG-002 cites only clauses 9.3/9.4 | [#333](https://github.com/lasrod/assurance-forge/issues/333) |
| 12.10 | `Resource.location: MultiLangString` — the prose declares Resource's only payload; the machine model omits it | Exists in no inventory entry, no library type, no row; a text-conformant producer's `<location>` becomes preserved content that strict save refuses | [#337](https://github.com/lasrod/assurance-forge/issues/337) |
| XMI (entailed by clause 2) | `xmi:type` as type discriminator — the form the pinned normative model file itself is serialized with | `read_xsi_type` accepts XSI-namespace `type` only; `xmi:type` falls through to role/class-name inference | [#336](https://github.com/lasrod/assurance-forge/issues/336) |
| XMI (entailed) | `xmi:uuid`/`xmi:label`/`xmi:Extension` tolerance; cross-document `href` scope | Tolerated-by-accident at best; href rejection (SACM-XMI-007) has no recorded rationale | [#336](https://github.com/lasrod/assurance-forge/issues/336) |
| Clause 2 (literal) | Exports conform to "the SACM XML Schema produced by applying XMI rules" — the conformance object is an XSD | Approximated by strict emission, golden bytes, and reload-validates-clean; the substitution is reasonable but stated nowhere | #295 evidence package |
| Clauses 2.2/2.3 | Optional points include "the common elements defined in the Common and Predefined diagrams" | Base-class interchange under `ArgumentPackage`/`ArtifactPackage` roots is asserted by no fixture (likely works; root-agnostic reader) | #295 evidence package |

## Verified rows whose claims exceed their evidence

Disclosed in the affected rows' Notes as of this audit; each stays `verified`
for what its evidence actually covers, with the bound stated.

| Row | Claim text implies | Evidence actually covers | Tracked |
|---|---|---|---|
| `SACM23-ARG-002` | Source/target typing and multiplicities of 11.13–11.18 validated | Generic ArgumentAsset end typing and `[1..*]` lower bounds. The family-specific rules (11.15: AssertedEvidence source must be ArtifactReference; 11.17/11.18: both ends ArtifactReference) and the target `[1]` upper bound are unenforced — `reference_target_kind_ok` admits any ArgumentAsset, and a two-target relationship validates clean | [#334](https://github.com/lasrod/assurance-forge/issues/334) |
| `SACM23-BASE-002` | Abstract/citation/implementation-constraint behavior validated per 8.2 | The citation and implementation-constraint invariants. The abstractForm constraints (citing element concrete, referred element abstract, same type) have zero checks and zero tests | [#335](https://github.com/lasrod/assurance-forge/issues/335) |
| `SACM23-PKG-002` | Package interfaces/bindings preserve participants, references, multiplicities, validation | The clause 9 shapes: participant `[2..*]`, end typing (kind-of, not the OCL's exact-type), serialization. The content-citation constraints and the 10.x/11.x/12.x parallels are elsewhere ([#333](https://github.com/lasrod/assurance-forge/issues/333)) | [#333](https://github.com/lasrod/assurance-forge/issues/333) |

## Structural spot-checks

Verified attribute-by-attribute against the PDF prose, the figures, and the
machine-readable XML: SACMElement, LangString, Description, MultiLangString,
UtilityElement, ModelElement, ExpressionLangString (clause 8);
AssuranceCasePackage, AssuranceCasePackageBinding (9); Term, Category,
ExpressionElement, TerminologyPackageInterface (10); Claim, ArgumentReasoning,
Assertion, AssertedRelationship, AssertedInference, AssertedContext,
ArgumentGroup, ArgumentPackage, ArtifactReference, AssertionDeclaration (11);
Event, ArtifactAssetRelationship, Resource (12).

The inventory is faithful to the XML everywhere, including the two defects it
already records (ArtifactAssetRelationship name+superclass, Event.date). The
audit found **seven further prose-vs-model divergences the inventory resolved
silently**, in both directions, despite its stated text-wins policy —
enumerated in [#337](https://github.com/lasrod/assurance-forge/issues/337).
`Resource.location` is the consequential one; the others are multiplicity and
naming divergences already absorbed behaviorally. #337 also collects the
matrix's six wrong clause citations (e.g. `SACM23-LIB-002` citing Term as 10.2
where the formal PDF has 10.11).

## Specification defects observed

Candidates for the upstream standards feedback tracked in
[#200](https://github.com/lasrod/assurance-forge/issues/200); none creates an
implementation obligation, but unrecorded resolutions read as omissions:

- Clause 11.5's OCL types participants as `ArgumentPackageInterface`
  (`oclIsTypeOf`) while its prose Constraints say "only ArgumentPackages"; 9.4
  uses `oclIsTypeOf` where 10.6 uses `oclIsKindOf` for the same shape.
- Clause 2.5 scopes the Terminology point's schema to "this entire
  specification" (the mandatory point's wording), unlike 2.2/2.3 which scope to
  their subpackage; the fixtures follow the evident intent (terminology-only).
- Clause 2.3 still calls its top object "the Evidence package" (a SACM 1.x
  name); clause 2.6 is missing from the PDF's table of contents.
- Figure 10.1 misspells `externalRefernce`; Figure 12.1 spells Event's
  attribute `occurence` where the XML has `occurece` and the prose has `date`.
- Clause 12.11's semantics text names classes that exist nowhere in the
  metamodel ("ArtifactActivityRelationships", "ActivityRelationships").
- The front-matter COMPLIANCE page conditions claims on OMG-approved test
  suites where they exist; none is known for SACM 2.3. Now stated on the
  [compliance points](sacm-compliance-points.md) page.

## Fully covered areas

For the record's completeness claim, the areas audited and found owned: the five
compliance points and their units of interchange (CP-001..005, including the
strict-load and root-preservation assertions); document structure, namespaces,
id identity and reference forms (XMI-001..003); strict/compatibility mode
separation in both directions (XMI-004, COMPAT-001); base identity, naming,
descriptions, notes, LangStrings, TaggedValues (BASE-001); the 8.2
citation/implementation-constraint invariants (BASE-002); package nesting and
interface/binding structure of clause 9 (PKG-001..003); terminology and
artifact representation and round-trip (TERM-001, ART-001, including the
recorded ptc-defect normalizations); the six AssertionDeclaration literals with
default and the invalid-literal negative, isCounter, metaClaims, reasoning
structure (ARG-001); relationship lower bounds and generic end typing
(ARG-002); XML safety (SEC-001). Group semantics (10.3, 11.2, 12.3 — "no
structural purpose") are interpretation guidance with no checkable rule beyond
the reference-not-containment representation, which round-trips.

## Limitations

- **XMI 2.5.1 (`formal/2015-06-07`) is a dated normative reference of SACM 2.3
  but is not pinned in-repo.** The audit could verify *that* clause 2 delegates
  to XMI rules and *which* forms the implementation accepts, not the XMI rules
  themselves. Whether `xmi:type` acceptance is mandatory for importers is
  therefore argued from the pinned metamodel file as a concrete specimen, not
  from the XMI text. Pinning is a decision item in
  [#336](https://github.com/lasrod/assurance-forge/issues/336).
- MOF 2.5.1, UML 2.5.1 and ISO/IEC 15026-1/-2 are likewise unpinned; nothing in
  the audited clauses turned on them.
- The audit verified row *existence and scope*, and implementation behavior
  where a finding hinged on it; it ran no test suites and did not re-verify
  every cited test's assertions.
- The PDF's table of contents has drifted from its body (clause 2.6, annex page
  numbers); annex normative status was read from the annex header pages
  directly. Annexes A–E are informative; F (UML Profile definition) is
  normative but reachable only through the unclaimed 2.6 point.

## What this changes for #295

- "Every applicable normative obligation is mapped to a requirement row or
  explicitly justified" — now **measured**: the exceptions are the tables above,
  each tracked ([#333](https://github.com/lasrod/assurance-forge/issues/333)–[#337](https://github.com/lasrod/assurance-forge/issues/337)).
- "Matrix completeness is reviewed independently from implementation" — this
  record is that review.
- The release-bound evidence package must either land after #333–#337 (and the
  `SACM23-LIB-002` resolution), or list what remains open of them in its known
  limitations. Freezing the matrix without one of those two would rebuild the
  gap this audit closed.
