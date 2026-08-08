# SACM 2.3 conformance statement

What Assurance Forge claims against OMG SACM 2.3, and how each claim is bound
to an exact release rather than to whatever `main` currently contains.

!!! warning "This is a self-assessment, not a certification"

    Nothing here has been assessed by OMG or by any certification body.
    Standards conformance is not regulatory approval and is not a safety
    argument about any system using this tool.

## The claims

The reusable `libs/sacm` library claims four of the five SACM 2.3 compliance
points defined by clause 2 of `formal/23-05-08`:

- **Assurance Case Model** (clause 2.4, the mandatory point)
- **Argumentation Model** (clause 2.2)
- **Artifact Model** (clause 2.3)
- **Terminology Model** (clause 2.5)

The **SACM UML Profile point (clause 2.6) is not claimed** — no part of it is
implemented. The decision record, the unit-of-interchange tests behind each
claimed point, and the limits of all five decisions are on the
[compliance points](sacm-compliance-points.md) page.

These are **library interchange claims**: strict import and export of XMI whose
unit of interchange is the package each clause names. What the Assurance Forge
*application* can create and edit through its UI is a narrower, separately
disclosed set — see the [capability matrix](../features/feature-matrix.md).
Strict SACM output and compatibility-mode re-emission of preserved third-party
content are different things; only the former is claimed.

## How a claim is bound to a release

Every release built by the [release workflow](../RELEASING.md) generates and
attaches a **conformance evidence package**
(`assurance-forge.<tag>-evidence-package.zip`), produced by
`tools/sacm/generate_evidence_package.py` from the released commit. It contains:

- `manifest.json` — version, commit SHA, toolchain, the SHA-256 pins of the
  normative OMG sources, the claimed and not-claimed compliance points read
  from the frozen matrix, and test totals.
- `conformance-statement.md` — this statement, generated for that exact
  release, with every claim derived from the frozen artifacts beside it.
- The frozen [conformance matrix](sacm-conformance-matrix.md),
  [compliance points](sacm-compliance-points.md) decision page,
  [completeness audit](sacm-matrix-completeness-audit.md),
  [integration preservation record](sacm-integration-preservation.md),
  [interoperability corpus](sacm-interop-corpus.md) provenance, and every
  [verification record](verification/README.md) — failures included.
- `traceability.json` — every requirement row mapped to the tests whose names
  embed its ID.
- `test-results.xml` — the CTest JUnit output of the released build.
- `limitations.md` — the non-`verified` rows and the completeness audit's open
  tracking issues at that release.
- `SHA256SUMS` over the package contents.

The `evidence_package_check` CTest gate proves on every run that the generator
still works against the current checkout, so a release tag is never the first
time it runs.

## Which releases carry a package

Releases are listed on the
[GitHub releases page](https://github.com/lasrod/assurance-forge/releases).
**No release cut before this mechanism existed carries an evidence package**,
and no conformance claim is made for those releases; the first release after
the workflow change is the first release-bound claim. For anything newer than
the latest release, the matrix and compliance-points pages describe `main` at
the commit you are reading — a weaker statement than a release-bound one, and
said so deliberately.
