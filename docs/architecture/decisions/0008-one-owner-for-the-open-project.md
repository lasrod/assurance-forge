# 8. One owner for the open project

- **Status:** Accepted
- **Date:** 2026-07-27
- **Supersedes in part:** the "the SACM file is read-only to the server, which
  removes the dual-writer problem" scope decision recorded in
  `docs/features/mcp-server.md`.

## Context

The first MCP design gave `assurance-forge-mcp` its own copy of the project. It
loaded the project at process start, answered reads from that copy, and wrote
review-proposal files into the project directory. The design document argued
that keeping SACM read-only to the server removed the dual-writer problem.

It did not. Three attempts to make an agent's proposal reach the user failed,
each in a different way, and each was a filesystem answer to a problem that is
not about files:

| Attempt | What it added | Why it failed |
|---|---|---|
| Reload review items | A poll for externally-written review items | Only helped when the app was not dirty, and never covered the manifest |
| Adopt orphaned proposals | The app claimed proposal files it found | Never fired in the running application; its own pull request says so |
| Session sidecar | A file recording the open argument | Worked, but existed only because the server could not ask the application anything |

The underlying defect is that **two processes each held a private copy of one
project and both wrote into it**. The adapter never reloaded, while the
application's command bus autosaves after every command. From that single fact:

- the agent reasoned about an argument the user had already changed;
- proposals computed `base_model_hash` against that stale copy and evaluated
  `Broken`;
- the manifest reported files "modified outside Assurance Forge";
- proposal manifest entries and review items vanished when the app next saved
  from its own older copy;
- nothing the agent did was visible until a finished file appeared.

## Decision

**The running application owns the open project. Nothing else does.**

- The MCP adapter owns the MCP transport and holds no model.
- Assurance Forge owns the model and every domain operation.
- A **versioned local IPC boundary** (`src/bridge`) connects them: Windows named
  pipe, POSIX AF_UNIX socket, user-private, with a token from an endpoint record
  in the user's runtime directory. No TCP.
- **Requests execute on the frame thread.** That thread owns the model, so an
  operation sees exactly what the user sees, cannot observe the argument halfway
  through an edit, and needs no lock.
- **Every mutation uses the same command, validation, undo, audit and
  notification path as an interactive change.** An agent's work becomes a
  `core::changesets::ChangeSet` carrying a `ReviewProposal`, and acceptance runs
  the unchanged `ApplyProposalCommand`. There is no agent-specific command type.
- **Offline operation is read-only.** With no application running, the adapter
  loads its own copy and serves reads. Changing a safety case needs a command
  bus, an audit log and a person to accept it; none exist in a headless process.

The operations themselves live in `src/agent`, linked by both binaries, so the
connected and offline paths cannot answer differently.

## Consequences

**Writes require the application to be running.** This is the deliberate cost.
An agent cannot edit a safety case unattended, which is the correct posture for
a tool whose output is a safety argument, but it does mean an MCP client
configured against a closed project can only read.

**Two binaries can be from different builds.** A client configuration written
months ago keeps pointing at whatever adapter path it recorded. Every frame
carries a protocol version and a mismatch is reported as one message naming both
versions and what to do, rather than as a parse failure the user experiences as
"the AI stopped working".

**The project directory has one writer again.** The endpoint record holding the
pipe name, pid and token lives in the user's runtime directory, not the project:
in the project it would be hash-tracked by `af.proj` and would reach a colleague
through version control. Change sets are held in memory for the same reason.

**A whole class of machinery was deleted**, not added: proposal adoption, the
active-argument sidecar, and the manifest bookkeeping that surrounded them.

**Consent is unchanged.** ADR 0007 still governs whether a project may be shared
at all, the flag still fails closed, and it is still re-read on every call so
revocation takes effect immediately. What moves is where the gate is enforced —
into the application, where the user is, which is also what lets a connected
client be shown in the Review panel rather than being invisible.

## Alternatives considered

**Keep two processes and fix the file protocol.** Cheapest, and MCP would keep
working with the application closed. Rejected because cross-process staleness
stays a permanent race rather than being designed out, and because an agent's
work could never be shown live — the application would learn of it only when a
file landed. This is also the option the three failed attempts were.

**Host a Streamable HTTP server inside the application.** Same single-owner
benefits and no second binary. Rejected because it needs a hand-rolled HTTP and
SSE implementation, port and token management, and a listening socket in a
safety-case tool invites a question that a named pipe does not — and because it
would change the client configuration every existing user already has.
