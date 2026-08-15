# 0007. Explicit consent for sharing assurance cases over MCP

- Status: Accepted
- Date: 2026-07-27
- Deciders: Assurance Forge maintainers
- Superseded in part by:
  [ADR 0009](0009-one-integrated-working-draft-per-argument.md) — the write
  surface described below as "`ReviewProposal` drafts" becomes the integrated
  draft workspace. The consent gate, its fail-closed behaviour, the
  `returns_case_content` classification and the rule that the server never
  applies its own changes are unchanged and still govern the new tools.
  [ADR 0014](0014-projectless-mcp-discovery-with-runtime-case-binding.md) — the
  "per-machine and all-or-nothing" granularity consequence: a per-session,
  per-project grant is layered on top of this gate, which itself is unchanged.

## Context

Assurance Forge runs an MCP server (`assurance-forge-mcp`) so a user can read and
reason about a safety case from their own AI client, and save proposed changes
back. This is a **second, independent** egress path, and ADR 0005 does not cover
it.

ADR 0005 governs a provider integration: Assurance Forge decides what to send, to
an endpoint the user configured, when the user takes an AI action. Every one of
those properties is different here.

- **The app does not choose what leaves.** A connected client asks, and the
  server answers. There is no per-action moment at which the user approves a
  particular payload.
- **The recipient is whatever launched the process.** The client is configured
  outside Assurance Forge, in a file the app does not own and cannot inspect.
- **There is no API key acting as an incidental gate.** The provider path is
  inert until the user supplies a credential; the MCP server needs none, so
  absent a deliberate gate it would be live the moment the binary ships.

The project's engineering principles state that user data belongs to the user at
all times and that no data is sent externally without explicit consent. A safety
case is exactly the kind of content those principles exist for: it is commercially
sensitive, often covered by a customer agreement, and frequently describes systems
that can hurt people.

The tension is that a consent surface for a server the user launches from another
application has no natural per-action prompt. The server has no UI, and by the
time a tool call arrives there is no one to ask.

## Decision

We will gate the entire MCP surface behind a single explicit setting,
`mcp.enabled`, which defaults to `false` and is written from Preferences.

- **Every tool that returns assurance-case content is refused while the gate is
  closed**, including tools added later. Tool definitions carry a
  `returns_case_content` flag, and a test asserts the refusal across the whole
  registry rather than a sampled tool, so a new tool that forgets the flag fails
  the build rather than leaking.
- **The gate fails closed.** A missing settings file, an unreadable one, a
  malformed document, a missing section, and a non-boolean flag all read as
  "not permitted". The failure we accept is MCP silently not working; the failure
  we refuse is publishing a safety argument to a client the user never approved.
- **The refusal explains how to grant permission**, so the failure is actionable
  rather than mysterious.
- **The server never edits the case.** Its only write surface is `ReviewProposal`
  drafts that a human accepts in the app, and drafts are attributed to the
  connecting client.
- **The two AI features stay independent.** `cmake/check_layer_gates.cmake`
  forbids `mcp/` from including `ai/`, so the MCP server and the in-app SCCG
  review cannot come to share an inference path, a credential, or a consent
  decision.

## Consequences

- Consent is a deliberate, revocable act the user performs in the application,
  rather than a side effect of installing a binary.
- Granularity is coarse. The gate is per-machine and all-or-nothing: it does not
  distinguish reading from proposing, nor one project from another, nor one
  client from another. Anything that can launch the binary and read the settings
  file inherits the user's grant. Finer scoping is possible later; a per-project
  or per-client grant would be a compatible extension of this decision.
- The gate is only as good as the settings file's integrity. Any component that
  writes that file must preserve sections it does not own —
  `core::UpdateUserSettingsSection` exists for exactly this, after a version that
  rewrote the whole document silently deleted the consent flag on every AI-settings
  save.
- Because consent is checked when a tool is called rather than when the server
  starts, revoking it takes effect on the next call rather than needing the
  client restarted.
- Every new MCP tool carries an obligation: classify whether it returns case
  content. Getting that wrong is the way this gate would be defeated, which is why
  it is asserted over the whole registry.
