# 0013. Review methods are independent of inference providers

- Status: Accepted
- Date: 2026-08-15
- Deciders: Assurance Forge maintainers
- Clarifies in part: [ADR 0005](0005-provider-agnostic-ai-with-user-consent.md) —
  the "prompt construction" and "response parsing" that ADR 0005 places in the
  `ai` layer are split here: review-method behaviour moves to a new `review`
  layer, and `ai` keeps only provider-neutral inference. The consent and
  transparency rules of ADR 0005 are unchanged.

## Context

The `ai` layer currently owns two unrelated responsibilities. One is talking to
a provider: HTTP, credentials, request encoding. The other is the SCCG review
method itself: which guideline profile applies to an element, which data
packages an element contributes, how the review request is worded, what shape
the result must have, and whether a returned finding names real guideline and
element identifiers. Almost all of that lives in `src/ai/ai_claim_review.*`,
with the profile-selection step stranded in
`src/app/controllers/ai_review_controller.*`.

This coupling blocks two goals at once:

- **Provider neutrality.** A second provider adapter would have to either
  duplicate the SCCG behaviour or depend on OpenAI-shaped types that leak
  through the shared headers.
- **External review over MCP.** The layer gate forbids `mcp/` from including
  `ai/` (ADR 0007), which is correct — the MCP server must never touch provider
  credentials. But the SCCG review method is not provider behaviour, and an
  external AI client should be able to run the same review with the same
  validation that the built-in path uses.

A separate defect surfaced while designing the result flow: the draft
workspace's `working_revision` increases on every successful mutation
(ADR 0009). If a completed review is discarded whenever the revision moved
while the provider was thinking, then on an actively edited case nearly every
review completes stale — the user pays for the inference and gets nothing,
even when the edit touched an unrelated branch.

## Decision

**We will move review-method behaviour into a new `review` layer and reduce
`ai` to provider-neutral inference. Neither may depend on the other.**

- The `review` layer owns review methods: scope resolution, SCCG profile
  selection, data-package assembly, instruction and result-contract
  construction, local result validation, and mapping validated findings into
  suggested changes. It may depend on `core` and `parser`. It never calls a
  provider.
- The `ai` layer owns inference: provider profiles, credentials, provider
  adapters, and normalized generation requests and results. It knows nothing of
  SCCG, GSN, SACM, review items, or draft groups. It never parses a review
  result.
- `app` composes the two. `agent` may depend on `review` so external clients
  can be served the same method; `mcp` reaches review behaviour only through
  `agent`. No provider adapter may create findings or draft operations.
- **A review result is validated against the scope it reviewed, not the global
  revision.** The review plan records a deterministic semantic hash of the
  reviewed elements and included data packages. At commit time the result is
  stale only if that scope hash no longer matches, or the project/argument
  context itself changed. The working revision at review time is recorded as
  provenance but does not by itself invalidate the result. (Draft *mutations*
  keep their existing `expected_working_revision` optimistic-concurrency check
  from ADR 0009 — this decision is about review results, not draft writes.)
- **A failed, cancelled, or stale run commits nothing.** Findings, review
  items, draft groups, and provenance for one run are committed atomically or
  not at all. Cancellation must hold even when the provider responds after the
  user cancelled.

## Consequences

- Built-in review and externally executed review share one method, one result
  contract, and one validator, so their findings are equally trustworthy and
  equally constrained — neither path can accept its own suggestions.
- The layer gate grows a layer: `cmake/check_layer_gates.cmake`, its negative
  test (`tools/repo/check_layer_gate_detects_violations.py`), and
  `docs/architecture/layers-and-ownership.md` must all learn `review`, in both
  directions (`review` must not include `ai/`, and `ai` must not include
  `review/`).
- The scope-hash rule requires a deterministic semantic hash over the reviewed
  elements and packages, and a test that an unrelated edit does not invalidate
  a review while an in-scope edit does.
- Real cancellation is a rework, not a flag: today's task runner detaches
  threads and the HTTP client blocks in `curl_easy_perform` with only a hard
  timeout. Delivering the atomic-commit guarantee needs a cancellation token
  through the task runner and HTTP client, planned as its own design task.
- The extraction itself must be behaviour-preserving: existing review tests
  move with the code and must pass unchanged before any new capability lands.
