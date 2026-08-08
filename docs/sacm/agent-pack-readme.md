# SACM 2.3 editable library-first agent pack

This pack installs a Claude Code multi-agent workflow for implementing OMG SACM 2.3 as an independent, editable C++23 library inside the Assurance Forge repository and then connecting Assurance Forge to that library.

The design goal is not to make the library a mirror of Assurance Forge internals. The library owns the SACM source of truth, SACM-native editing, operation previews, XMI import/export, validation, model identity, and standard conformance behavior. Assurance Forge becomes a client that projects the library model into GSN canvas, deterministic layout, tree, review, terminology, evidence, and project workflows.

## Project decisions encoded in this pack

- Initial library location: inside the Assurance Forge repository, preferably `libs/sacm`.
- Long-term goal: full SACM 2.3 compliance, reached through smaller verified increments.
- First real slice: editable minimal SACM document, not read-only parser only.
- Package mapping: project/file root -> `AssuranceCasePackage`; argument module/folder -> `ArgumentPackage`; GSN goal -> SACM `Claim`.
- Core library terminology: strictly SACM. No Goal, Strategy, Solution, Canvas, TreeItem, coordinates, or layout in the core library API.
- Source of truth: SACM library document.
- Round-trip target: semantic round-trip plus deterministic export.
- Save mode: strict SACM 2.3 by default, with compatibility mode separate.
- Layout: Assurance Forge computes deterministic layout outside the SACM library.
- Diagnostics: machine-readable.
- API target: C++23 API plus CLI test utility.

## What gets installed

The installer copies `repo-files/` into a target repository root:

- `.claude/agents/*.md` project subagents for SACM specification analysis, metamodel mapping, library architecture, XMI/edit tests, implementation, Assurance Forge integration, verification, and interoperability research.
- `docs/sacm/*.md` plans, architecture notes, editing policy, layout policy, conformance matrix, test strategy, research notes, answered decisions, prompts, and templates.
- `docs/sacm/prompts/*.md` ready-to-use AI prompts.
- `docs/sacm/templates/*.md` repeatable slice, requirement, and interoperability templates.
- `docs/architecture/decisions/0006-sacm-23-independent-library.md` proposed architecture decision.
- `scripts/*.sh` helper scripts for installation verification, optional SACM reference download, and optional library scaffolding.

The OMG SACM PDF and normative machine-readable XML are not bundled. The pack includes references and an optional helper script so the repository owner can download and pin official copies locally if licensing and project policy allow it.

## Install

From the extracted pack directory:

```bash
bash scripts/sacm-agent-bootstrap.sh --repo /path/to/assurance-forge
```

For a dry run:

```bash
bash scripts/sacm-agent-bootstrap.sh --repo /path/to/assurance-forge --dry-run
```

To install and also create a neutral library skeleton under `libs/sacm`:

```bash
bash scripts/sacm-agent-bootstrap.sh --repo /path/to/assurance-forge --scaffold-library
```

The scaffold option is intentionally off by default. It creates a minimal, standalone CMake target, placeholder SACM command types, and a CLI smoke utility only; the agents should still write tests and implement behavior slice by slice.

## Recommended first prompt

After installation, open Claude Code at the repository root and run the prompt in:

```text
docs/sacm/prompts/sacm-library-master-prompt.md
```

For the first implementation task, use:

```text
docs/sacm/prompts/sacm-first-editable-slice-prompt.md
```

## Suggested agent order per slice

1. `sacm-implementation-lead`
2. `sacm-researcher`
3. `sacm-researcher`
4. `sacm-library-architect`
5. `sacm-xmi-test-engineer`
6. `sacm-implementer`
7. `sacm-conformance-verifier`
8. `sacm-implementer`, when the library slice needs application integration
9. `sacm-researcher`, when external examples or tool behavior are needed

## Core policy

- Library first, but editable from the first real vertical slice.
- SACM 2.3 XMI is the interchange truth.
- Assurance Forge UI models are projections, not the canonical safety-case state.
- Deterministic layout is an Assurance Forge concern, not a SACM library concern.
- No Assurance Forge namespace, class naming, GSN display term, UI data structure, or layout state leaks into the library API.
- No standard SACM data is silently discarded.
- Every feature slice is traceable: specification requirement -> tests -> implementation -> verification -> matrix update.
