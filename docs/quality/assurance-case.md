# Repository-quality assurance case

The argument that this repository's quality claims are credible — made with the
same discipline the tool exists to support, and challenged in the open. Required
by [#296](https://github.com/lasrod/assurance-forge/issues/296).

The **source model is SACM 2.3**:
[`assurance-case.sacm.xmi`](assurance-case.sacm.xmi), beside this page so the
model and its rendering stay pinned to the same commit, in this project's own
interchange format, strict-validated by this project's own library:

```bash
sacm_cli validate --strict docs/quality/assurance-case.sacm.xmi
```

This page is the human-readable rendering of that model; where the two
disagree, the model wins. Evidence identifiers resolve through the
[evidence index](evidence-index.md). One modelling convention, stated so it is
not mistaken for an oversight: every claim is deliberately declared
`needsSupport` — in this case's convention a claim graduates to `asserted`
only when review rounds retire its challenges, so the declarations are the
case's own maturity markers.

!!! warning "What this case is not"

    It is not a claim that the project proves its own correctness, and the
    assurance case is not evidence of itself. It is a reviewable statement of
    which quality claims are supported by which reproducible evidence, and
    which are still challenged. Nothing here implies certification, regulatory
    approval, or fitness of any safety argument built with the tool.

## Top claim and its boundary

> **The Assurance Forge repository provides credible evidence for the quality
> claimed for its declared maturity and intended use.**

**Context (the boundary):** a pre-1.0 open engineering tool for authoring and
reviewing structured assurance cases. Not certified, qualified, or approved for
any regulatory purpose. The [README's Status and limitations](https://github.com/lasrod/assurance-forge#status-and-limitations)
is the canonical maturity statement; this case argues quality *within* that
boundary, not beyond it.

The top claim is supported by an argument over eight strands — the dimensions
[#296](https://github.com/lasrod/assurance-forge/issues/296) requires — each
supported by mechanically gated evidence where a gate exists, and challenged
explicitly where one does not.

## The eight strands

| # | Claim | Key evidence | Open challenges |
|---|---|---|---|
| 1 | **Public claims are accurate and bounded** | [Capability matrix](../features/feature-matrix.md) (`supported` requires a cited test, gated); [compliance points](../sacm/sacm-compliance-points.md) with unit-of-interchange tests; [conformance statement](../sacm/sacm-conformance-statement.md); the [completeness audit](../sacm/sacm-matrix-completeness-audit.md) | — |
| 2 | **Architecture is controlled** | [Layers and ownership](../architecture/layers-and-ownership.md); configure-time layer gate plus its negative self-test; [migration plan](../architecture/legacy-bridge-migration-plan.md) for what remains transitional | Cross-layer includes still *compile*; only the source scan rejects them — [#340](https://github.com/lasrod/assurance-forge/issues/340) |
| 3 | **Safety-case data is protected** | Byte-pinned `SACM23_LIB_002` preservation and refusal tests; [integration preservation record](../sacm/sacm-integration-preservation.md) with every measured loss | Twenty-six commands still edit through the legacy bridge, which refuses what it cannot represent — [#350](https://github.com/lasrod/assurance-forge/issues/350); silent-loss protection is pinned only for probed shapes — [#347](https://github.com/lasrod/assurance-forge/issues/347) |
| 4 | **Verification is credible** | [Conformance matrix](../sacm/sacm-conformance-matrix.md) with `sacm_matrix_check`; [verification records](../sacm/verification/README.md) — 17 records, 10 of them FAIL verdicts at this update (current counts: the generated index); the [completeness audit](../sacm/sacm-matrix-completeness-audit.md) | Prose obligations without owning rows — [#333](https://github.com/lasrod/assurance-forge/issues/333)–[#337](https://github.com/lasrod/assurance-forge/issues/337); the suite twice passed while probes measured silent loss — [#347](https://github.com/lasrod/assurance-forge/issues/347) |
| 5 | **Changes are reviewable** | [Documentation map](../documentation-map.md) (one canonical home per policy, gated); [ADRs](../architecture/decisions/index.md); the [PR template's](https://github.com/lasrod/assurance-forge/blob/main/.github/pull_request_template.md) one-responsibility and disclosure checks; the [release-notes policy](../RELEASING.md) | No independent human review — [#348](https://github.com/lasrod/assurance-forge/issues/348) |
| 6 | **Releases are attributable** | [Release workflow](../RELEASING.md) attaching a per-release [evidence package](../sacm/sacm-conformance-statement.md) (version, commit, toolchain, test results, frozen matrix); release-notes policy requiring file-handling disclosure | No shipped release carries the package yet; the mechanism is merged but unexercised by a real tag — [#351](https://github.com/lasrod/assurance-forge/issues/351) |
| 7 | **AI development is human-controlled** | Canonical agent definitions with `agent_definition_check`; consent ADRs [0005](../architecture/decisions/0005-provider-agnostic-ai-with-user-consent.md) and [0007](../architecture/decisions/0007-mcp-server-consent.md); human promotion of drafts ([ADR 0010](../architecture/decisions/0010-draft-provenance-persistence-and-human-promotion.md)) | Runtime tool grants exceed declared sets — [#326](https://github.com/lasrod/assurance-forge/issues/326); agent evaluations defined but unexecuted — [#327](https://github.com/lasrod/assurance-forge/issues/327); the reviewing human is the one who directed the AI — [#348](https://github.com/lasrod/assurance-forge/issues/348) |
| 8 | **Security and dependency risks are managed** | [SECURITY.md](https://github.com/lasrod/assurance-forge/blob/main/SECURITY.md); XML hardening (`SACM23_SEC_001_RejectsDoctype`); warnings-as-errors, clang-tidy ratchet and sanitizers per the [code quality policy](code-quality-policy.md) | OS-backed secret store is Windows-only — [#53](https://github.com/lasrod/assurance-forge/issues/53); no automated dependency review — [#349](https://github.com/lasrod/assurance-forge/issues/349) |

The [repository baseline](repository-baseline.md) supports the top claim
directly: the numbers behind every strand, measured and bound to a commit.

## Challenge register

Every live counter-claim in the model, its target, and where it is tracked.
Recording a challenge here does not weaken the case; hiding it would.

| Challenge | Challenges | Tracked |
|---|---|---|
| Prose-constraint obligations lack owning matrix rows (constraints the spec states in prose that no matrix row yet owns) | Strand 4 (verification) | [#333](https://github.com/lasrod/assurance-forge/issues/333) [#334](https://github.com/lasrod/assurance-forge/issues/334) [#335](https://github.com/lasrod/assurance-forge/issues/335) [#336](https://github.com/lasrod/assurance-forge/issues/336) [#337](https://github.com/lasrod/assurance-forge/issues/337) |
| Bridged edits refuse rather than represent (the legacy edit path refuses edits on cases carrying elements it cannot express, instead of silently deleting them) | Strand 3 (data protection) | [#350](https://github.com/lasrod/assurance-forge/issues/350) |
| The automated suite twice passed in full while probes measured silent data loss; protection is pinned only for the probed shapes | Strands 3 and 4 | [#347](https://github.com/lasrod/assurance-forge/issues/347) |
| All review is by AI agents or the sole maintainer who directed them; no independent human review exists | Strands 5 and 7 | [#348](https://github.com/lasrod/assurance-forge/issues/348) |
| No automated dependency review; submodule pinning is manual | Strand 8 (security) | [#349](https://github.com/lasrod/assurance-forge/issues/349) |
| Cross-layer includes compile | Strand 2 (architecture) | [#340](https://github.com/lasrod/assurance-forge/issues/340) |
| Agent tool grants exceed declarations; evals unexecuted | Strand 7 (AI control) | [#326](https://github.com/lasrod/assurance-forge/issues/326), [#327](https://github.com/lasrod/assurance-forge/issues/327) |
| Secret store Windows-only | Strand 8 (security) | [#53](https://github.com/lasrod/assurance-forge/issues/53) |
| No release-bound evidence package shipped yet | Strand 6 (releases) | [#351](https://github.com/lasrod/assurance-forge/issues/351) |

A resolved challenge moves out of this table in the same PR that resolves it,
with the model updated to match.

## Review rounds

The case is reviewed with the method
[#296](https://github.com/lasrod/assurance-forge/issues/296) specifies —
comprehension, well-formedness, expressive sufficiency, criticism and evidence
audit. Rounds are recorded here; an unresolved challenge from review lands in
the register above, as an issue, not as prose.

| Round | Date | Reviewer | Outcome |
|---|---|---|---|
| 0 | 2026-08-08 | Author | Model authored; strict validation by `sacm_cli` passes; every evidence identifier resolves in the [evidence index](evidence-index.md) |
| 1 | 2026-08-09 | independent review agent, round 1 | Structure, strict validation and 18 of 19 evidence chains confirmed at 5cc8985; blocked on three findings: verification-record counts stale in model and rendering (17 records / 10 FAIL, not 14 / 8), and two unregistered counter-claims — the suite's measured blindness to silent loss (strand 4) and single-maintainer, AI-only review (strands 5/7); five should-fix items filed |

Round 1's blocking findings were resolved in the change that records it: the
counts corrected in model and rendering, and both counter-claims added to the
model and the register ([#347](https://github.com/lasrod/assurance-forge/issues/347),
[#348](https://github.com/lasrod/assurance-forge/issues/348)). Its should-fix
items landed alongside: the conformance statement and PR template are model
artifacts with evidence edges, the dependency-review gap is registered
([#349](https://github.com/lasrod/assurance-forge/issues/349)), the two
prose-tracked register rows became issues
([#350](https://github.com/lasrod/assurance-forge/issues/350),
[#351](https://github.com/lasrod/assurance-forge/issues/351)), the
`needsSupport` convention is stated above, and the register's two most
compressed phrasings are expanded in place. Accepted as-is from the round's
notes: the remaining jargon that resolves via links, the mutable URLs for
files with no in-docs home, and the absence of `assumed` claims — an assumed
claim for gate non-bypassability is a candidate for a future round.

## Update policy

- **When a strand's evidence changes** (a gate added or removed, a matrix
  status change, a verification round), the PR that changes it updates the
  model and this rendering together. The challenge register is part of the
  model: counter-claims are elements, not footnotes.
- **At each release**, the evidence package freezes the matrix and records this
  case's referenced evidence at that commit; the release-unbound challenge
  clears when the first package ships.
- **Ownership**: whoever changes the evidence owns the case update, the same
  both-or-neither rule the documentation map applies to policy copies.
