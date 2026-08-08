# SACM 2.3 compliance points

What Assurance Forge claims against the standard's own conformance clause, and
what it does not.

SACM 2.3 does not define a single "conformant" state. Clause 2.1 defines **five
compliance points**, and software claims each one separately. A claim that names
no compliance point says less than it appears to, which is why this page exists
before any public conformance statement does.

!!! warning "This is a self-assessment, not a certification"

    Nothing here has been assessed by OMG or by any certification body. It states
    what the project has evidence for, and links to that evidence. Standards
    conformance is not regulatory approval and is not a safety argument about
    your system. The specification's own compliance page additionally conditions
    claims on completing OMG-approved test suites where such suites exist; none
    is known to exist for SACM 2.3.

## The five points

Quoted from the normative specification, `formal/23-05-08` clause 2
(SHA-256 `8004a2ee…de3012`, pinned by `scripts/fetch-sacm23-references.sh`).

| Clause | Compliance point | Unit of interchange | Claimed |
|---|---|---|---|
| 2.4 | **Assurance Case Model** (mandatory) | `SACM::AssuranceCasePackage` | **yes** |
| 2.2 | **Argumentation Model** | `Argumentation::ArgumentPackage` | **yes** |
| 2.3 | **Artifact Model** | `Artifact::ArtifactPackage` | **yes** |
| 2.5 | **Terminology Model** | `Terminology::TerminologyPackage` | **yes** |
| 2.6 | **SACM UML Profile** | UML models carrying SACM stereotypes | **no — not claimed** |

Each point requires the same thing of an implementation:

> Software that conforms to the SACM specification at the … compliance point
> shall be able to **import and export XMI documents** that conform with the SACM
> XML Schema produced by applying XMI rules to the normative MOF metamodel …

and each names a different **top object** that "as a unit of interchange shall
be" that point's root. That last part is the one most easily missed: claiming the
Argumentation Model means a document whose root element *is* an `ArgumentPackage`
must import and export, not merely that argument elements are supported inside an
assurance case.

## Why the four are claimed

The metamodel itself is covered by requirement rows in the
[conformance matrix](sacm-conformance-matrix.md), and class and attribute
coverage against the normative model is gated bidirectionally by
`SACM23_LIB_001_ElementKindsMatchNormativeInventory` and
`SACM23_XMI_003_KnownAttributesCoverTheNormativeInventory`.

What was **not** evidenced until #295 is the units of interchange. The library
accepted all four roots — `load_xmi_file`'s own docstring says so — but every
committed fixture was rooted at `AssuranceCasePackage`, so three of the four
points rested on a capability nobody had exercised. That is now pinned:

| Point | Test | Fixture |
|---|---|---|
| 2.4 Assurance Case | `SACM23_CP_001_AssuranceCasePackageIsTheMandatoryInterchangeUnit` | `package-minimal-valid.sacm.xmi` |
| 2.2 Argumentation | `SACM23_CP_002_ArgumentPackageIsAnInterchangeUnit` | `argumentation-only-root-valid.sacm.xmi` |
| 2.3 Artifact | `SACM23_CP_003_ArtifactPackageIsAnInterchangeUnit` | `artifact-only-root-valid.sacm.xmi` |
| 2.5 Terminology | `SACM23_CP_004_TerminologyPackageIsAnInterchangeUnit` | `terminology-only-root-valid.sacm.xmi` |

Each asserts a **strict** load (a tolerant load accepts roots the standard does
not permit, so passing tolerantly would prove nothing), that the package actually
arrived under the kind the clause names, that export keeps that root, and that
the round trip preserves the model. The root assertion matters on its own: a
writer that quietly promoted an `ArgumentPackage` into an `AssuranceCasePackage`
would still compare equal semantically while emitting a document that is no
longer the clause 2.2 unit of interchange.

Clauses 2.2 and 2.3 both state that conformance at their point "does not entail"
support for the other subpackages. The three optional fixtures therefore carry
only their own package's content, so each test exercises the point in isolation
rather than through an assurance case that happens to contain one.

## Why the UML Profile is not claimed

Clause 2.6:

> A tool demonstrating SACM UML Profile interchange conformance can import and
> export conformant XMI for all valid SACM UML models

That is a different input language: UML models carrying SACM stereotypes, not
SACM metamodel instances. **The library implements no part of it** — there is no
profile, stereotype or extension handling anywhere in `libs/sacm`.

It is recorded as `out-of-scope` in the conformance matrix rather than left
unmentioned, because a compliance point nobody names reads as one that was met.
There is no plan to claim it; if that changes, this page and the matrix row
change with it.

## Limits of these claims

Stated here rather than left to be discovered:

- **The claims are about the library**, `libs/sacm`. What the Assurance Forge
  *application* can create and edit through its UI and command model is a
  narrower set, tracked separately — see `SACM23-LIB-002` and `SACM23-INT-001`
  in the matrix, which disclose the bridged-edit representability gap. A
  conformant importer and exporter does not require UI editing support for every
  metaclass, and the two must not be read as one claim.
- **The export namespace is a project choice, not a normative value.** SACM 2.3
  determines no instance-document namespace URI; see the
  [metamodel inventory](sacm-2.3-metamodel-inventory.md#the-pinned-export-namespace-is-a-choice-not-a-normative-value).
  Do not treat a differing namespace as non-conformance.
- **Strict output and compatibility mode are different things.** A strict save
  emits conformant SACM 2.3; compatibility mode re-emits preserved content that
  the standard does not define. Only the former is what these claims are about.
- **Completeness has been measured, and the exceptions are tracked.** The
  2026-08-08 [matrix-completeness audit](sacm-matrix-completeness-audit.md)
  confirmed the structural inventory chain behind these claims and found the
  matrix's gaps to be prose constraints, tracked in
  [#333](https://github.com/lasrod/assurance-forge/issues/333)–[#337](https://github.com/lasrod/assurance-forge/issues/337).
  Two findings touch import capability directly: `xmi:type` dispatch
  ([#336](https://github.com/lasrod/assurance-forge/issues/336)) and
  `Resource.location`
  ([#337](https://github.com/lasrod/assurance-forge/issues/337)).
- **No release binding yet.** [#295](https://github.com/lasrod/assurance-forge/issues/295)
  requires an evidence package bound to an exact release — version, commit,
  toolchain, machine-readable results. Until that exists, these claims describe
  `main` at the commit you are reading, which is weaker than a release claim and
  is said so deliberately.
