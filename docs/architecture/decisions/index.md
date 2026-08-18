# Architecture Decisions

This section records the significant architectural decisions made for Assurance
Forge as **Architecture Decision Records (ADRs)**.

An ADR captures a single decision: the context that forced it, the choice that
was made, and the consequences accepted as a result. It documents *why* the
codebase looks the way it does — the reasoning that the source code and the
current-state architecture docs cannot express on their own.

We use the lightweight [Michael Nygard
format](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions):
**Title, Status, Context, Decision, Consequences**.

## Lifecycle

An ADR moves through these statuses:

| Status | Meaning |
| --- | --- |
| `Proposed` | Under discussion; not yet agreed. |
| `Accepted` | Agreed and in effect. |
| `Superseded` | Replaced by a later ADR. Note it as `Superseded by ADR-NNNN`. |
| `Deprecated` | No longer relevant, but kept for the historical record. |

ADRs are **append-only**. Once an ADR is `Accepted`, do not rewrite it. To change
a decision, write a *new* ADR that supersedes it and update the old ADR's status
to point at the replacement. This keeps the decision history intact.

## Adding an ADR

1. Copy [`adr-template.md`](adr-template.md).
2. Name it with the next number and a kebab-case title, e.g.
   `0006-use-x-for-y.md`.
3. Fill in Context, Decision, and Consequences. Set the status (usually
   `Proposed` while in review, `Accepted` once merged).
4. Add it to the table below and to the `Architecture → Decisions (ADRs)` nav in
   `mkdocs.yml`.

## Records

| ADR | Title | Status |
| --- | --- | --- |
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-layered-architecture-with-build-time-gates.md) | Layered architecture with build-time gates | Accepted |
| [0003](0003-sacm-xml-as-source-of-truth.md) | SACM XML as the source of truth | Accepted |
| [0004](0004-mkdocs-material-documentation-site.md) | MkDocs Material documentation site | Accepted |
| [0005](0005-provider-agnostic-ai-with-user-consent.md) | Provider-agnostic AI with explicit user consent | Accepted (clarified in part by 0013) |
| [0006](0006-sacm-23-independent-library.md) | SACM 2.3 as an independent reusable library | Accepted |
| [0007](0007-mcp-server-consent.md) | Explicit consent for sharing assurance cases over MCP | Accepted (superseded in part by 0009, 0014) |
| [0008](0008-one-owner-for-the-open-project.md) | One owner for the open project | Accepted (superseded in part by 0009, 0010, 0014) |
| [0009](0009-one-integrated-working-draft-per-argument.md) | One integrated working draft per argument file | Accepted (superseded in part by 0016) |
| [0010](0010-draft-provenance-persistence-and-human-promotion.md) | Draft provenance, persistence, and human-controlled promotion | Accepted (superseded in part by 0016) |
| [0011](0011-panels-own-their-view-types.md) | Panels own their view types | Accepted |
| [0012](0012-a-claim-carries-one-description.md) | A claim carries one Description | Accepted |
| [0013](0013-review-methods-independent-of-inference-providers.md) | Review methods are independent of inference providers | Accepted |
| [0014](0014-projectless-mcp-discovery-with-runtime-case-binding.md) | Projectless MCP discovery with explicit runtime case binding | Accepted |
| [0015](0015-versioned-provider-profiles-and-fail-closed-selection.md) | Versioned AI provider profiles and fail-closed selection | Accepted |
| [0016](0016-the-draft-is-a-sacm-document.md) | The working draft is a SACM document, not a list of operations | Accepted |
