# Connect an AI client

Assurance Forge ships a second program, `assurance-forge-mcp`, which speaks
[MCP](https://modelcontextprotocol.io) so an AI client you run yourself — Claude
Desktop, an IDE assistant, your own script — can read the open case and propose
changes to it.

Full reference: [MCP Server](../features/mcp-server.md).

## Turn it on

**Edit → Preferences → MCP Server → Allow AI clients to read and propose
changes.** It is off until you turn it on, and every content-bearing tool
refuses while it is off. That is the consent gate
([ADR 0007](../architecture/decisions/0007-mcp-server-consent.md)), not a
misconfiguration.

The same section shows a **Client configuration** block — the JSON to paste into
your client's MCP settings — and a **Copy Configuration** button. It appears
when a project is open and `assurance-forge-mcp` sits beside the application;
otherwise it says which of the two is missing. The configuration it copies names
the currently open project.

## Granting access

The first time a session touches project content, Assurance Forge raises an
**AI client access request** naming the client and the project, with **Allow
while open** and **Deny**. Until you answer, the client is refused.

A grant is keyed to that session, survives a reconnect, and ends on deny,
revoke, project close or switch, MCP disable, or restart. Nothing is shared
before you allow it.

## What a client can do

- **Read** the case: elements, relationships, terminology, assurance claim
  points, and where a new argument would belong (`suggest_placement`).
- **Propose** changes. Proposals do not touch the argument: they land in a
  **working draft** — one per argument file — with their provenance recorded.
- **Ask for guidance**: SCCG authoring doctrine and the mechanical checks are
  delivered into the session, so a client is told what a good claim looks like
  before it writes one, and its work is rehearsed against the checks before it
  submits.

## The working draft

Draft changes appear on the canvas as staged nodes with source badges, and in
the **Draft Changes** tab in the feedback dock. Nothing is in your argument
until you accept it.

- Accept changes selectively; the tool keeps dependent changes together, so you
  cannot accept a child whose parent you rejected.
- Machine-translated secondary-language text is held for explicit reviewer
  acceptance ([Edit the argument](edit-the-argument.md#two-languages)).
- The accept is recorded as its own audit transaction, and undoing it restores
  the draft it consumed.
- A draft survives a restart.

Drafts are a prototype: the design is settled
([ADR 0009](../architecture/decisions/0009-one-integrated-working-draft-per-argument.md),
[ADR 0016](../architecture/decisions/0016-the-draft-is-a-sacm-document.md)) but
the format and the UX are still expected to change. Check the matrix rows
`AF-AI-014` … `AF-AI-019` before relying on one.

## Data leaves the machine only when you send it

The MCP server is local; your AI client is what talks to a provider. What that
client sends is governed by the client, not by Assurance Forge — the consent
this application can enforce is whether the client may read the case at all.
