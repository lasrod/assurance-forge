# Repository-quality assurance case

The argument that this repository's quality claims are credible — made with the
same discipline the tool exists to support, and challenged in the open. Required
by [#296](https://github.com/lasrod/assurance-forge/issues/296).

The **source model is SACM 2.3**:
[`assurance-case.sacm.xmi`](https://github.com/lasrod/assurance-forge/blob/main/docs/quality/assurance-case.sacm.xmi),
in this project's own interchange format, strict-validated by this project's own
library:

```bash
sacm_cli validate --strict docs/quality/assurance-case.sacm.xmi
```

This page is the human-readable rendering of that model; where the two
disagree, the model wins. Evidence identifiers resolve through the
[evidence index](evidence-index.md).

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
| 1 | **Public claims are accurate and bounded** | [Capability matrix](../features/feature-matrix.md) (`supported` requires a cited test, gated); [compliance points](../sacm/sacm-compliance-points.md) with unit-of-interchange tests; [conformance statement](../sacm/sacm-conformance-statement.md) | — |
| 2 | **Architecture is controlled** | [Layers and ownership](../architecture/layers-and-ownership.md); configure-time layer gate plus its negative self-test; [migration plan](../architecture/legacy-bridge-migration-plan.md) for what remains transitional | Cross-layer includes still *compile*; only the source scan rejects them — [#340](https://github.com/lasrod/assurance-forge/issues/340) |
| 3 | **Safety-case data is protected** | Byte-pinned `SACM23_LIB_002` preservation and refusal tests; [integration preservation record](../sacm/sacm-integration-preservation.md) with every measured loss | Twenty-six commands still edit through the legacy bridge, which refuses what it cannot represent — [migration plan](../architecture/legacy-bridge-migration-plan.md) phases 1–4 |
| 4 | **Verification is credible** | [Conformance matrix](../sacm/sacm-conformance-matrix.md) with `sacm_matrix_check`; [verification records](../sacm/verification/README.md) including 8 recorded FAIL rounds; the [completeness audit](../sacm/sacm-matrix-completeness-audit.md) | The audit's own findings: prose obligations without owning rows — [#333](https://github.com/lasrod/assurance-forge/issues/333)–[#337](https://github.com/lasrod/assurance-forge/issues/337) |
| 5 | **Changes are reviewable** | [Documentation map](../documentation-map.md) (one canonical home per policy, gated); [ADRs](../architecture/decisions/index.md); one-responsibility PR discipline | — |
| 6 | **Releases are attributable** | [Release workflow](../RELEASING.md) attaching a per-release [evidence package](../sacm/sacm-conformance-statement.md) (version, commit, toolchain, test results, frozen matrix); release-notes policy requiring file-handling disclosure | No shipped release carries the package yet; the mechanism is merged but unexercised by a real tag |
| 7 | **AI development is human-controlled** | Canonical agent definitions with `agent_definition_check`; consent ADRs [0005](../architecture/decisions/0005-provider-agnostic-ai-with-user-consent.md) and [0007](../architecture/decisions/0007-mcp-server-consent.md); human promotion of drafts ([ADR 0010](../architecture/decisions/0010-draft-provenance-persistence-and-human-promotion.md)) | Runtime tool grants exceed declared sets — [#326](https://github.com/lasrod/assurance-forge/issues/326); agent evaluations defined but unexecuted — [#327](https://github.com/lasrod/assurance-forge/issues/327) |
| 8 | **Security and dependency risks are managed** | [SECURITY.md](https://github.com/lasrod/assurance-forge/blob/main/SECURITY.md); XML hardening (`SACM23_SEC_001_RejectsDoctype`); pinned submodules; warnings-as-errors, clang-tidy ratchet and sanitizers per the [code quality policy](code-quality-policy.md) | OS-backed secret store is Windows-only — [#53](https://github.com/lasrod/assurance-forge/issues/53) |

The [repository baseline](repository-baseline.md) supports the top claim
directly: the numbers behind every strand, measured and bound to a commit.

## Challenge register

Every live counter-claim in the model, its target, and where it is tracked.
Recording a challenge here does not weaken the case; hiding it would.

| Challenge | Challenges | Tracked |
|---|---|---|
| Prose-constraint obligations lack owning matrix rows | Strand 4 (verification) | [#333](https://github.com/lasrod/assurance-forge/issues/333) [#334](https://github.com/lasrod/assurance-forge/issues/334) [#335](https://github.com/lasrod/assurance-forge/issues/335) [#336](https://github.com/lasrod/assurance-forge/issues/336) [#337](https://github.com/lasrod/assurance-forge/issues/337) |
| Bridged edits refuse rather than represent | Strand 3 (data protection) | [Migration plan](../architecture/legacy-bridge-migration-plan.md) phases 1–4 |
| Cross-layer includes compile | Strand 2 (architecture) | [#340](https://github.com/lasrod/assurance-forge/issues/340) |
| Agent tool grants exceed declarations; evals unexecuted | Strand 7 (AI control) | [#326](https://github.com/lasrod/assurance-forge/issues/326), [#327](https://github.com/lasrod/assurance-forge/issues/327) |
| Secret store Windows-only | Strand 8 (security) | [#53](https://github.com/lasrod/assurance-forge/issues/53) |
| No release-bound evidence package shipped yet | Strand 6 (releases) | First tag after the release-workflow change |

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
