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

## Current Software State

### Core Capability Snapshot

| Capability | Status | Current State |
|---|---|---|
| SACM-first foundation | :material-check-circle:{ .status-stable } Stable | Import and export around SACM 2.3 XML with internal model alignment to SACM concepts. |
| GSN visualization and layout | :material-check-circle:{ .status-stable } Stable | Automatic rendering and layout for structured argument navigation without manual node positioning. |
| AI-assisted review with AI profiles | :material-check-circle:{ .status-stable } Stable | Manual and AI-assisted review workflows are available, including provider/profile-based setup and SCCG-guided feedback. |
| SCCG support | :material-check-circle:{ .status-stable } Stable | Safety Case Core Guidelines are integrated into review guidance and quality checks. |
| Project file view | :material-progress-wrench:{ .status-prototype } Prototype 2 | Current project-file view is usable but planned for a redesigned next-generation version. |
| Theme support | :material-progress-wrench:{ .status-prototype } Prototype 2 | Theme support is available now and expected to evolve into a new styling model. |

### GSN Support

| Feature | Status | Bucket | Notes |
|---|---|---|---|
| Core GSN | :material-check-circle:{ .status-stable } Stable | Now | Supports all core elements and relationships. |
| Dialectic Extension | :material-clock-outline:{ .status-planned } Planned | Next | Challenge and defeater resolution. |
| Guided development — top-down | :material-clock-outline:{ .status-planned } Planned | Next | Structured authoring guidance starting from goals. |
| Guided development — bottom-up | :material-clock-outline:{ .status-planned } Planned | Next | Structured authoring guidance starting from evidence. |
| Modular Extension | :material-clock-outline:{ .status-planned } Planned | Next / Later | Contract and away elements; core subset targeted for Next, full support Later. |
| Argument Pattern Extension | :material-lightbulb-outline:{ .status-candidate } Candidate | Later | Pattern library and instantiation support. |
| Confidence Argument Extension | :material-lightbulb-outline:{ .status-candidate } Candidate | Later | Confidence-annotated argument structures. |

## Important Compatibility Notices

!!! warning "Evidence Register is prototype"
    The current evidence register is a prototype implementation. It is expected to change into a SACM-based register model in a future iteration.

!!! warning "CSE register format transition"
    The CSE register will move to a new format. Backwards compatibility is expected to be lost during this migration.

!!! warning "Prototype modules may change"
    Project file view and theme mechanisms are currently at prototype maturity and will evolve. Expect structural and UX changes.

## Near Future (Now and Next)

| Feature | Bucket | Planned Direction |
|---|---|---|
| Evidence register replacement | Now | Replace prototype evidence register with SACM-based register model. |
| Project file view vNext | Next | Upgrade from Prototype 2 to a redesigned and more scalable project view. |
| Theme system refresh | Next | Move from current theme implementation to a more consistent and extensible theming approach. |
| AI review improvements | Next | Continue improving profile handling, review control, and feedback quality. |
| SCCG workflow refinements | Next | Improve practical guideline coverage and usability in review flow. |

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
