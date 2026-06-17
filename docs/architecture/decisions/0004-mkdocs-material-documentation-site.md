# 0004. MkDocs Material documentation site

- Status: Accepted
- Date: 2026-06-17
- Deciders: Assurance Forge maintainers

## Context

Assurance Forge needs project documentation that covers user-facing features,
the architecture, a developer guide, and the roadmap. The documentation should
be versioned alongside the source, easy to write in Markdown, browsable on the
web, and able to render the Mermaid diagrams and generated class diagrams the
project uses to explain its structure.

## Decision

We will document the project as a [MkDocs](https://www.mkdocs.org/) site using
the [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/) theme,
configured in `mkdocs.yml` with content under `docs/`.

The site is organized into top-level sections (Features, Roadmap, Architecture,
Developer Guide), uses `index.md` landing pages per section (the
`navigation.indexes` feature is enabled), and renders Mermaid diagrams via
`pymdownx.superfences`. Class diagrams are generated into
`docs/diagrams/generated/` and surfaced through the Architecture section. Python
dependencies are pinned in `requirements-docs.txt`.

## Consequences

- Documentation lives in the repository and is reviewed and versioned with code
  changes.
- Contributors write plain Markdown; the theme provides navigation, search, and
  light/dark palettes.
- The build can be validated in CI with `mkdocs build --strict`, which fails on
  broken internal links and pages missing from the nav.
- New documentation pages must be added to the `nav` in `mkdocs.yml` to appear,
  which is an extra step but keeps navigation explicit and ordered.
- This ADR section itself is published as part of this site.
