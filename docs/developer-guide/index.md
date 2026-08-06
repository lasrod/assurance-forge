# Developer Guide

This section contains development guidance for Assurance Forge contributors.

Start here:

- [Documentation map](../documentation-map.md) — which page is canonical for
  which policy, what each authority level means, and what the CI documentation
  gate enforces. Read this before adding a page.
- [Layers and ownership](../architecture/layers-and-ownership.md) — what each
  subsystem owns and which direction dependencies may run.
- [Generating class diagrams](generating-class-diagrams.md)
- [Releasing](../RELEASING.md) — how a release is cut, and what its notes must say.

## Repository quality

- [Repository quality baseline](../quality/repository-baseline.md) — a measured
  snapshot of size, tests, coverage, architecture gates, documentation reach, and
  tooling, with the gaps named. Regenerate it with
  `python tools/quality/collect_baseline.py`.
- [Code coverage](../COVERAGE.md) — why the project publishes several coverage
  views rather than one headline number.