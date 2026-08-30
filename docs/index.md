# Assurance Forge

Assurance Forge is an open-source tool for creating, reviewing, and maintaining structured assurance cases and safety cases.

The application is built in C++ using Dear ImGui and is designed around structured argumentation concepts such as SACM and GSN.

![The Assurance Forge window: case explorer, GSN canvas and element inspector](screenshot/dark.png)

!!! warning "Alpha software"
    Assurance Forge is under active development. SACM XML is the source of truth
    and preserving it is a primary design constraint, but keep your assurance
    data in version control and keep backups. What is claimed, per capability
    and with the tests behind it, is the [capability matrix](features/feature-matrix.md);
    if a capability is not there, treat it as not claimed.

<div class="grid cards" markdown>

-   :material-file-document-check-outline:{ .lg .middle } **SACM-First Foundation**

    ---

    Consumes and produces SACM 2.3 XML through an independent, reusable SACM library. An edit the standard cannot express is refused, not approximated.

-   :material-graph-outline:{ .lg .middle } **GSN Visualization**

    ---

    Structured safety arguments rendered automatically — including pattern, dialectic and ACP notation. No manual node positioning.

-   :material-robot-outline:{ .lg .middle } **AI-Assisted Review**

    ---

    Provider-agnostic AI with configurable profiles evaluates arguments and identifies weaknesses. Nothing is sent without your explicit action.

-   :material-shield-check-outline:{ .lg .middle } **SCCG Integration**

    ---

    Safety Case Core Guidelines built into review, into mechanical checks, and into the guidance an external AI client is given.

-   :material-source-branch:{ .lg .middle } **Your Own AI Client, Over MCP**

    ---

    An AI client you run yourself can read the case and propose changes — landing in a working draft a human accepts, never applied behind your back.

-   :material-history:{ .lg .middle } **Audited by Construction**

    ---

    Every edit is a replayable transaction with undo boundaries, baselines, snapshots, and a timeline that reconstructs the argument as it stood.

-   :material-translate:{ .lg .middle } **Two Languages, Reviewed**

    ---

    Arguments carry a second language end to end; machine-translated text is held for an explicit reviewer acceptance.

-   :material-map-outline:{ .lg .middle } **[View the Roadmap &rarr;](roadmap/public.md)**

    ---

    See what is stable, what is planned next, and what is on the long-term horizon.

</div>

## Documentation

- **[User Guide](user-guide/index.md)** — open a project, navigate and edit the argument, review it, work with evidence, export, and connect an AI client.
- **[Features](features/index.md)** — the [capability matrix](features/feature-matrix.md) plus per-feature documentation: [terminology assist](features/terminology-assist.md), the [MCP server](features/mcp-server.md), the [confidence panel](features/confidence-panel-prototype.md).
- **[Standards](sacm/index.md)** — [SACM 2.3 conformance](sacm/sacm-conformance-matrix.md), [GSN v3 conformance](gsn/gsn-v3-conformance-matrix.md), the [SACM–GSN mapping](sacm/sacm-gsn-mapping.md), and the verification records behind them.
- **[Architecture](architecture/index.md)** — component layout, [layers and ownership](architecture/layers-and-ownership.md), and the [decision records](architecture/decisions/index.md).
- **[Developer Guide](developer-guide/index.md)** — building, testing, generating diagrams, and the repository's own [quality evidence](quality/evidence-index.md).

Not sure which page is authoritative? The [documentation map](documentation-map.md) says which document wins on conflict.
