# Product Roadmap

Assurance Forge is built to make structured assurance work practical for engineering teams today, while staying aligned with open standards and future interoperability.

This page shows:

- what is available now,
- what is planned next,
- what is long-term direction.

For submission and automation details, see [Roadmap Workflow](workflow.md).

## Status Key

| Status | Meaning |
|---|---|
| :material-check-circle:{ .status-stable } Stable | Ready for normal usage in current releases. |
| :material-progress-wrench:{ .status-prototype } Prototype 2 | Integrated and usable, but not final. APIs, format, or UX may change. |
| :material-flask-outline:{ .status-prototype } Prototype 1 | Early concept validation. Not production-stable. |
| :material-clock-outline:{ .status-planned } Planned | Approved for roadmap but not yet available. |
| :material-lightbulb-outline:{ .status-candidate } Candidate | Under consideration. Scope and timing may change. |
| :material-check-circle-outline:{ .status-stable } Partial | Part of the area is delivered and part is not. The notes say which is which. |

Each row names the [capability matrix](../features/feature-matrix.md) IDs behind
it. The matrix is the canonical record — where this page and the matrix
disagree, the matrix wins.

## Current Software State

### Core Capability Snapshot

| Capability | Status | Current State |
|---|---|---|
| SACM-first foundation | :material-check-circle:{ .status-stable } Stable | Import and export around SACM 2.3 XML through an independent, reusable SACM library. An edit the standard cannot express is refused with the file unchanged rather than approximated. (`AF-STD-001`…`AF-STD-011`) |
| GSN visualization and layout | :material-check-circle:{ .status-stable } Stable | Automatic rendering and layout for structured argument navigation without manual node positioning. (`AF-GSN-*`, `AF-ENG-017`) |
| AI-assisted review with AI profiles | :material-check-circle:{ .status-stable } Stable | Manual and AI-assisted review workflows, including provider/profile-based setup and SCCG-guided feedback. Nothing is sent without an explicit user action. (`AF-AI-001`…`AF-AI-006`) |
| SCCG support | :material-check-circle:{ .status-stable } Stable | Safety Case Core Guidelines are integrated into review guidance, into mechanical checks that need no AI at all, and into the authoring doctrine given to an external AI client. (`AF-AI-006`, `AF-AI-023`, `AF-AI-024`) |
| Audit, history and undo | :material-check-circle:{ .status-stable } Stable | Every edit is a replayable audited transaction, with undo boundaries, baselines, snapshots, and a timeline that reconstructs the argument as it stood. (`AF-ENG-001`…`AF-ENG-003`, `AF-ENG-006`, `AF-ENG-007`, `AF-ENG-021`) |
| Evidence and CSE registers | :material-check-circle:{ .status-stable } Stable | Both registers are derived views over the SACM document, and their columns and assessments are stored in the document itself rather than in a sidecar. (`AF-ENG-008`, `AF-ENG-009`) |
| Two-language safety cases | :material-check-circle:{ .status-stable } Stable | Arguments carry a second language end to end, and machine-translated text is held for an explicit reviewer acceptance. (`AF-ENG-012`) |
| Your own AI client, over MCP | :material-progress-wrench:{ .status-prototype } Prototype 2 | An MCP client you run yourself can read the case and propose changes, behind an explicit per-session consent gate. Reads are stable; proposal, draft and projectless-session behaviour is still expected to change. (`AF-AI-007`…`AF-AI-025`) |
| Working drafts and human promotion | :material-progress-wrench:{ .status-prototype } Prototype 2 | Proposed changes land in one working draft per argument file and reach the argument only when a human accepts them. (`AF-AI-014`…`AF-AI-018`) |
| Confidence arguments and ACPs | :material-flask-outline:{ .status-prototype } Prototype 1 | Assurance claim points with their own confidence argument, and a per-element confidence assessment. (`AF-ENG-019`, `AF-ENG-020`) |
| Case Explorer | :material-progress-wrench:{ .status-prototype } Prototype 2 | Redesigned in 2026-07 around assurance workflows rather than the directory layout; the raw file and package views remain under **Advanced**. (`AF-PLAT-002`) |
| Theme support | :material-progress-wrench:{ .status-prototype } Prototype 2 | Dark and light themes are available and expected to evolve into a new styling model. (`AF-PLAT-005`) |

### GSN Support

| Feature | Status | Bucket | Notes |
|---|---|---|---|
| Core GSN | :material-check-circle:{ .status-stable } Stable | Now | All core elements and relationships. |
| Dialectic Extension | :material-check-circle-outline:{ .status-stable } Partial | Now / Next | **Delivered:** challenge relationships, counter argument and counter evidence, a challenge aimed at a relationship, and challenge-to-challenge (`AF-DIA-001`…`AF-DIA-004`). **Not yet:** the defeated decorator, defeat on strategy/solution/context, in-doubt state, and defeat propagation semantics (`AF-DIA-005`…`AF-DIA-008`). |
| Argument Pattern Extension | :material-check-circle-outline:{ .status-stable } Partial | Now / Later | **Delivered:** the uninstantiated decorator and the combined undeveloped-and-uninstantiated decorator import, render on the canvas and export to SVG (`AF-PAT-001`, `AF-PAT-002`). **Not yet:** authoring that state, multiplicity and optionality, and the pattern/template model, catalogue and instantiation (`AF-PAT-003`…`AF-PAT-010`). |
| Confidence Argument Extension | :material-flask-outline:{ .status-prototype } Prototype 1 | Next | Assurance claim points carrying their own confidence argument, and a per-element assessment with a Jøsang-opinion method (`AF-ENG-019`, `AF-ENG-020`). |
| Guided development — top-down | :material-clock-outline:{ .status-planned } Planned | Next | Structured authoring guidance starting from goals (`AF-METH-005`). |
| Guided development — bottom-up | :material-clock-outline:{ .status-planned } Planned | Next | Structured authoring guidance starting from evidence (`AF-METH-006`). |
| Modular Extension | :material-clock-outline:{ .status-planned } Planned | Next / Later | Modules, away elements, contracts and interfaces. None delivered (`AF-MOD-001`…`AF-MOD-014`); SACM package infrastructure underneath it is supported. |

## Important Compatibility Notices

!!! success "Registers now store their data in SACM (2026-08)"
    Both registers were prototypes that kept their columns and assessments in a
    project sidecar. They now write into the SACM document — evidence columns as
    vendor `TaggedValue`s and provenance on the `Artifact`, CSE assessments on
    the `AssertedEvidence` that carries the support being judged. An older
    project's sidecar assessments are still read and edited, and the register's
    **Move into SACM** action imports them as one audited transaction. Nothing is
    rewritten when a project is opened.

!!! warning "Register migration under a working draft"
    Moving assessments into SACM while a working draft is open stages the import
    as a draft edit but releases the project-file copy immediately. Discarding
    that draft and then saving loses those assessments. Accept the draft, or
    migrate with no draft open.

!!! warning "Prototype modules may change"
    The Case Explorer, the theme mechanism, working drafts, confidence analysis
    and the MCP proposal path are at prototype maturity. Expect structural and
    UX changes; the [capability matrix](../features/feature-matrix.md) records
    which rows those are.

## Near Future (Now and Next)

| Feature | Bucket | Planned Direction |
|---|---|---|
| Registers as SACM package views | Next | The registers are derived tables today; make them views over an `ArtifactPackage` in its own right. |
| Connect an AI client once | Next | A projectless client configuration, and a way to choose between running instances instead of refusing. |
| Guided development methods | Next | Structured top-down and bottom-up authoring guidance (`AF-METH-005`, `AF-METH-006`). |
| Dialectic defeat semantics | Next | The defeated decorator, in-doubt state, and defeat propagation on top of the delivered challenge structures. |
| Theme system refresh | Next | Move to a more consistent and extensible theming approach. |
| AI review improvements | Next | Continue improving profile handling, review control, and feedback quality. |
| SCCG workflow refinements | Next | Widen the mechanical checks that need judgement calibration, and improve usability in the review flow. |
| Safety case report | Next / Later | Produce a report document from the case (`AF-ENG-004`); LaTeX and PDF below. |

## Long-Term Direction (Later)

| Feature | Bucket | Direction |
|---|---|---|
| LaTeX export | Later | Add export pipeline for document-quality technical reports in LaTeX. |
| PDF export | Later | Support direct PDF generation for safety case reporting and distribution. |
| Extended GSN capabilities | Later | Expand support for advanced GSN extensions and richer argument patterns. |
| CAE notation | Later | Support for Claim Argument Evidence notation as an alternative to GSN. |
| Broader interoperability | Later | Continue work toward stronger standards and ecosystem integrations. |

## How Roadmap Tracking Works

This page is aligned with the roadmap issue workflow and project metadata.

1. Contributors create a **Roadmap Epic Request** issue.
2. Maintainers approve with `roadmap-approved`.
3. Automation generates epic/task structure and updates roadmap project fields.
4. Maintainers update maturity and bucket values as work progresses.
5. This page is refreshed from that planning state.

### What to fill in for each roadmap item

Use the roadmap issue form fields consistently:

- Area
- Initial maturity
- Size Points
- Priority
- Public roadmap bucket (`Now`, `Next`, `Later`, or `Not public yet`)
- Target Release (if known)
- Summary
- Epic acceptance criteria
- Suggested subtasks

Keeping those fields current is the fastest way to keep roadmap communication clear for both internal planning and public documentation.
