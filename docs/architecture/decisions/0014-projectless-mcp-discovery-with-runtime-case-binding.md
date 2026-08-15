# 0014. Projectless MCP discovery with explicit runtime case binding

- Status: Accepted
- Date: 2026-08-15
- Deciders: Assurance Forge maintainers
- Supersedes in part:
  [ADR 0007](0007-mcp-server-consent.md) — the "per-machine and all-or-nothing"
  granularity consequence: a per-session, per-project grant is added on top of
  the master gate. The master gate itself, its fail-closed behaviour, the
  `returns_case_content` classification and the no-apply rule are unchanged.
  [ADR 0008](0008-one-owner-for-the-open-project.md) — the project-keyed
  endpoint record becomes an instance-keyed record, and the listener outlives
  the project. The single-owner rule, the IPC boundary, frame-thread execution
  and offline read-only behaviour are unchanged.

## Context

The generated MCP client configuration embeds a project path
(`--project`, with `AF_MCP_PROJECT` as a second supply route), so a user must
reconfigure their AI client for every project. The consequences run deeper than
setup friction:

- Bridge endpoint records are keyed by the normalized project path, and the
  application tears the listener down and recreates it on every project switch,
  so project selection is launch configuration rather than live context.
- The consent gate (ADR 0007) is machine-wide: once `mcp.enabled` is on, any
  local client reaches whichever project is open, with no distinction between
  clients, sessions, or projects. ADR 0007 anticipated this: "a per-project or
  per-client grant would be a compatible extension of this decision."
- Today a session cannot silently follow a project switch only *by accident* —
  it is pinned to its launch path and errors with "not reachable". Removing the
  path removes that accidental protection, so an explicit replacement is
  required: a conversation that began against Project A must never start
  reading Project B because the user opened it.
- Nothing enforces the single-owner rule between two application instances.
  Two instances opening the same project silently fight over the endpoint
  record today, and — worse since ADR 0010 — would both write
  `.af/drafts` recovery state.
- On Windows the endpoint record and named pipe rely on default DACLs; the
  explicit 0600 permission exists only on POSIX.

## Decision

**We will make the MCP client configuration projectless and bind each session
to a project at runtime, with the user's explicit approval.**

- **Configuration identifies the executable only.** No project path, no
  environment-variable route, no token, no port. At cutover, `--project` and
  `AF_MCP_PROJECT` fail with an actionable migration message. Explicit offline
  use becomes `--offline-project <path>`: deliberate, read-only, accepted SACM
  only.
- **The application publishes an instance record, not a project record.** The
  record is keyed by a runtime instance id, carries pid, address, token,
  protocol and coarse state, and contains no assurance-case content. The
  listener starts when MCP is enabled, survives project open/close/switch, and
  rotates its token per application start. Explicit user-only ACLs are added on
  Windows for both the record and the pipe rather than relying on default
  DACLs.
- **Two gates.** The master `mcp.enabled` setting (ADR 0007, unchanged) plus an
  ephemeral per-session grant. A session receives no project content before
  the user approves an access request in the running application.
- **The grant is project-scoped.** It binds a session, an application instance,
  and a project. The currently open argument file is reported in every
  response envelope; switching argument files within the granted project —
  whether the user switches in the UI or the agent calls `open_case_file` —
  does not invalidate the binding. Switching or closing the *project* does:
  reads return a stable context error, mutations are refused, and only an
  explicit new approval creates a new binding.
- **A monotonic context generation protects every call.** It increments on
  project open/close, document replacement, rebind, and revocation. Every
  content response carries a context envelope (snake_case, matching the
  existing wire vocabulary: `context_generation`, `working_revision`,
  `workspace_id`); every mutation names `expected_context_generation` alongside
  the existing `expected_working_revision`.
- **Grants do not survive an application restart; session identity does.** The
  MCP session id continues to own its draft groups (ADR 0009/0010), so after a
  restart the user re-approves and the same session reattaches to its groups.
  Nothing is orphaned; nothing is disclosed without the fresh approval.
- **Multiple instances are never auto-selected.** With several instances
  running, all candidates may display the access request; the first approval
  wins and cancels the request elsewhere. One session binds to one instance.
- **An advisory project lock makes the single-owner rule visible.** An
  instance records which project it has open; a second instance opening the
  same project is warned before proceeding. Advisory, because a crashed
  instance must not lock a user out of their own safety case — but the silent
  two-writer state on `.af/drafts` ends.

## Consequences

- One client configuration works for every project, written once per AI-client
  installation. The Preferences surface for it must work with no project open.
- The user approves access per application run per project. That is deliberate
  friction replacing "enable once, share forever" — and it is new UI: access
  requests, a connected-clients view, revocation. Client labels are
  attribution, not authentication; the token and OS access control remain the
  security boundary.
- The bridge protocol grows discovery, access-request and context messages,
  and the protocol version increments; the existing mismatch diagnostics carry
  the migration story.
- Discovery needs hygiene rules: heartbeats, liveness-checked pruning of stale
  records, and tests for crashed processes — an instance record is a claim,
  not proof, that an application is running.
- A session bound to a closed or switched-away project is a normal state, not
  an error to hide: the client is told exactly why content stopped and the
  application offers explicit rebinding.
