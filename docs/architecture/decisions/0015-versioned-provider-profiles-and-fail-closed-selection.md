# 0015. Versioned AI provider profiles and fail-closed selection

- Status: Accepted
- Date: 2026-08-15
- Deciders: Assurance Forge maintainers
- Implements: the "configurable provider profiles" promise of
  [ADR 0005](0005-provider-agnostic-ai-with-user-consent.md), which the current
  implementation does not yet keep.

## Context

The provider abstraction exists in name only. `AiProviderId` is an enum with
one enumerator; the runtime constructs `OpenAiProvider` directly; the shared
`ai_types.h` header carries OpenAI constants (model, endpoint, secret account)
into every file that includes it; and `AiProviderIdFromString` returns
`AiProviderId::OpenAI` for *any* input — an unrecognized provider string in the
settings file silently becomes OpenAI. For a tool whose payloads are safety
arguments, "I don't recognize this destination, so I'll pick one" is the wrong
default in the most literal sense.

The settings model has one provider, one model, one credential — the secure
store is called with a single hard-coded `(service, account)` pair — and no
schema version. Users who must use an enterprise gateway, a local model, or a
different vendor for policy reasons cannot, despite ADR 0005 promising exactly
that.

## Decision

**We will select providers by versioned, named profiles, and refuse rather
than guess.**

- **Stable string type ids, no enum, no default.** Provider implementations
  register in a registry under ids such as `openai-responses`,
  `anthropic-messages`, `openai-compatible`. An unknown id is an error before
  any network activity. There is no fallback provider, ever.
- **Named profiles in versioned settings.** The `ai` settings section gains a
  schema version (an absent version reads as version 1, the legacy shape).
  Version 2 holds a list of profiles — id, provider type, display name,
  endpoint, model, credential reference — plus task-to-profile defaults such
  as which profile runs SCCG review.
- **Credentials stay in the platform secure store, referenced by profile.** A
  profile's credential reference resolves to a secure-store
  `(service, account)` pair. Settings files, project files, logs, and
  provenance records never contain a secret. There is no plaintext fallback.
- **Fail closed before the first byte leaves.** Unknown provider type, unknown
  profile, missing model, invalid endpoint, unsupported credential type,
  absent credential, missing task default, or insufficient structured-output
  capability: each refuses with an actionable message and makes no request.
- **Structured output is a checked capability.** Strict JSON schema and
  tool-argument mapping are acceptable for SCCG review; prompted JSON is an
  explicit degraded mode; unsupported disables review for that profile.
  (Today's review path *is* prompted JSON, while the OpenAI adapter already
  implements strict schema — the capability check makes that gap visible
  instead of implicit.) Provider-side structure never replaces local
  validation.
- **Migration preserves, and never touches the secret store.** First load of a
  legacy section creates one `openai-responses` profile with the configured
  model exactly, whose credential reference maps to the existing
  `(AssuranceForge, openai)` secure-store entry — the secret is neither read
  nor rewritten. The write to version 2 is atomic, keeps a backup, and fails
  closed if the provider or credential reference cannot be preserved. No model
  is invented.

## Consequences

- Enterprise gateways, local OpenAI-compatible servers, and a second vendor
  protocol become configuration, not code changes — restoring ADR 0005's
  promise.
- Misconfiguration that today silently ran against OpenAI becomes a visible
  refusal. That is the intended behaviour change: the failure we accept is a
  review that does not run; the failure we refuse is a safety argument sent to
  an endpoint the user did not choose.
- Every provider adapter owes the same contract-test suite — request shape,
  auth header, structured-output mapping, timeout, error normalization,
  redaction — so "supported provider" means the same thing for each.
- The Preferences UI becomes a profile list with an add-connection flow
  instead of one fixed form, and connection tests are invalidated by material
  profile changes.
- Two settings schemas exist at read time forever (version 1 is only ever
  migrated, never written), which is the price of not losing a working
  configuration on upgrade.
