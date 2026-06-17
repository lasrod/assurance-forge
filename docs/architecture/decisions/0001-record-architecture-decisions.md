# 0001. Record architecture decisions

- Status: Accepted
- Date: 2026-06-17
- Deciders: Assurance Forge maintainers

## Context

Assurance Forge has accumulated several load-bearing architectural choices — a
strict layering of `sacm/parser/core/ai/ui/app`, treating SACM XML as the source
of truth, a provider-agnostic AI design, and a MkDocs documentation site. These
are described as *current state* in `CLAUDE.md`, `CONTRIBUTING.md`, and
`docs/architecture/`, but the *reasoning* behind them — what alternatives were
weighed and what trade-offs were accepted — has lived only in maintainers' heads
and in pull-request discussions.

As the project grows and more contributors join, that reasoning is repeatedly
re-litigated, and decisions risk being silently reversed because the original
context is not written down anywhere durable.

## Decision

We will record significant architectural decisions as Architecture Decision
Records (ADRs) stored in `docs/architecture/decisions/`, using the lightweight
Michael Nygard format (Title, Status, Context, Decision, Consequences).

Each ADR is a numbered Markdown file, append-only once accepted. A decision is
changed by writing a new ADR that supersedes the old one rather than by editing
history. ADRs are published as part of the MkDocs site under
**Architecture → Decisions (ADRs)**.

## Consequences

- New contributors can read *why* the architecture is shaped the way it is, not
  just *what* it is.
- Decisions have a stable, citable identifier (ADR number) for use in reviews
  and discussions.
- Reversing or revisiting a decision becomes a deliberate, documented act.
- This adds a small amount of overhead: significant changes are now expected to
  come with an ADR, and the index and nav must be kept in sync when ADRs are
  added.
