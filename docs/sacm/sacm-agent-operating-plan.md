# SACM agent operating plan

## Agent workflow

Use specialized agents rather than asking one agent to do everything. The goal is to reduce drift, force traceability, and prevent implementation from outrunning the standard.

```text
sacm-implementation-lead
  -> sacm-researcher
  -> sacm-researcher
  -> sacm-library-architect
  -> sacm-xmi-test-engineer
  -> sacm-implementer
  -> sacm-conformance-verifier
  -> sacm-implementer
  -> sacm-researcher when needed
```

## Slice rules

A slice should be small enough to complete through tests and verification.

Good slices:

- Minimal editable document: create `AssuranceCasePackage`, `ArgumentPackage`, `Claim`, preview/apply deletion, save/load, semantic round trip.
- Base `LangString` and multilingual strings.
- IDs and reference resolution.
- One asserted relationship family.
- One artifact provenance cluster.
- One adapter projection from library `Claim` to Assurance Forge Goal display node.

Bad slices:

- Implement all of SACM argumentation at once.
- Rewrite the entire parser before tests exist.
- Add UI features while the library edit/model/XMI behavior is untested.
- Import a third-party example and call success compliance without matrix coverage.

## Required artifacts per slice

- Slice brief under `docs/sacm/slices/` or in the issue/PR description.
- Conformance matrix entries.
- Metamodel inventory for the classes/associations in scope.
- Positive and negative fixtures.
- Library-level tests.
- Adapter tests if Assurance Forge behavior changes.
- Verification report.

## Suggested branch naming

```text
sacm23/<area>-<short-slice-name>
```

Examples:

```text
sacm23/editable-minimal-package-claim
sacm23/base-lang-string
sacm23/argument-claim
sacm23/af-adapter-source-of-truth
```

## Review checklist

Before merging a slice:

- Does the library remain independent of Assurance Forge?
- Is the SACM library model the source of truth?
- Do tests fail without implementation?
- Do tests verify edit behavior and semantics, not just parser success?
- Are unsupported standard features visible in the matrix?
- Are compatibility behaviors separated from strict SACM 2.3 behavior?
- Are destructive edits previewable and explicit?
- Is strict save free of Assurance Forge layout metadata?
- Are external fixtures license-compatible or minimized?

## Coordination prompts

Use the prompts in `docs/sacm/prompts/`.

The most important one is `sacm-library-master-prompt.md`. It gives the lead agent the library-first constraints and tells it not to write production code before tests exist.
