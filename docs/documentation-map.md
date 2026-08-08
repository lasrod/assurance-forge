# Documentation map

Where each kind of documentation lives, who it is for, and which document wins
when two disagree.

## Why this page exists

The repository had four documents describing the architecture — `docs/ARCHITECTURE.md`,
`docs/architecture/index.md`, `CLAUDE.md` and `AGENTS.md` — and no statement of
which one was authoritative. They had drifted, measurably:

- Each omitted a **different** subset of the eleven subsystems under `src/`,
  between three and five each. `src/sacm_adapter` and `src/mcp` were missing from
  all four, though the layer gate enforces rules for both.
- `AGENTS.md` did not mention the capability matrix, its CI gate, the GSN mapping
  document, the `gsn-expert` skill, the formatting hook, or the split between the
  canvas and the SVG exporter. `CLAUDE.md` covered all six. An agent following
  `AGENTS.md` would fail a CI gate it had never been told about.

Twenty-nine pages were reachable from neither the site navigation nor any other
page, so a normative project policy and a superseded plan were equally hard to
find, and equally easy to mistake for one another.

None of that is a writing problem. It is a missing statement about authority,
which is what this page supplies.

## Authority levels

Every maintained page has one. It answers the reader's real question — *can I
rely on this?*

| Level | Meaning | Changing it |
|---|---|---|
| **Normative** | Project policy. Binding on new work. | Deliberate decision; say why in the commit. |
| **Reference** | Describes what exists. Accurate, not a rule. | Update when the thing it describes changes. |
| **Generated** | Produced by a tool from a source of truth. | Never by hand. Regenerate. |
| **Evidence** | A record of what was verified, when, and by whom. | Append-only. Never rewrite a result. |
| **Historical** | A plan or investigation kept for its reasoning. Superseded. | Do not follow as current instruction. |

A page whose level is not obvious from its location says so in its own opening
lines.

## Canonical sources

One home per policy. Where a document repeats a rule for convenience, the
canonical source wins on conflict.

| Policy | Canonical source |
|---|---|
| Subsystem responsibilities, dependency direction, state ownership | [Layers and ownership](architecture/layers-and-ownership.md) |
| Component layout and data flow | [Architecture](architecture/index.md) and the pages under it |
| Why an architectural choice was made | [ADRs](architecture/decisions/index.md) |
| What the tool can do for a user | [Capability matrix](features/feature-matrix.md) |
| What SACM 2.3 requires and what is verified | [SACM conformance matrix](sacm/sacm-conformance-matrix.md) |
| Which SACM 2.3 compliance points are claimed | [SACM compliance points](sacm/sacm-compliance-points.md) |
| GSN → SACM mappings | [SACM–GSN mapping](sacm/sacm-gsn-mapping.md) |
| SACM library policy (compliance, editing, layout, tests) | [SACM index → Policy](sacm/index.md#policy) |
| Whether a verification actually passed | [Verification records](sacm/verification/README.md) |
| Project maturity and what is claimed | [README → Status and limitations](https://github.com/lasrod/assurance-forge#status-and-limitations) |
| How to contribute, and code style | [CONTRIBUTING.md](https://github.com/lasrod/assurance-forge/blob/main/CONTRIBUTING.md) |
| How to report a vulnerability | [SECURITY.md](https://github.com/lasrod/assurance-forge/blob/main/SECURITY.md) |
| What an AI development agent may do, and what it may write | [`.agents/`](https://github.com/lasrod/assurance-forge/blob/main/.agents/README.md) — `.claude/agents/` and `.codex/agents/` are generated from it |
| Where to get help | [SUPPORT.md](https://github.com/lasrod/assurance-forge/blob/main/SUPPORT.md) |
| How releases are cut and what release notes must say | [Releasing](RELEASING.md) |
| Measured repository state at a point in time | [Quality baseline](quality/repository-baseline.md) |
| Warning levels, suppressions, and what is not enforced | [Code quality policy](quality/code-quality-policy.md) |

## What belongs where

| Location | Audience | Holds | Does not hold |
|---|---|---|---|
| `README.md` | Someone deciding whether to use or trust the tool | What it is, status and limitations, build and test, where everything else lives | Design rationale, contributor process detail |
| `CONTRIBUTING.md` | Someone about to open a PR | Engineering principles, code style, PR expectations | Architecture reference, standards policy |
| `SECURITY.md`, `SUPPORT.md`, `CODE_OF_CONDUCT.md` | Reporters and users | Process and expectations | Anything technical |
| `docs/user-guide/` | People using the application | Task-oriented guidance | Internals |
| `docs/developer-guide/` | People changing the code | How to build, test, generate, and measure | Policy that belongs in an ADR |
| `docs/architecture/` | People changing the code | Current component layout, ownership, dependency rules | Rationale — that is an ADR |
| `docs/architecture/decisions/` | Anyone asking "why is it like this" | One decision per ADR, with the trade-off accepted | Current-state description |
| `docs/sacm/`, `docs/gsn/` | Standards reviewers | Conformance status, policy, mappings, evidence | Application feature claims |
| `docs/features/` | Everyone | What the tool can do, per capability, with status | Standards conformance claims |
| `docs/quality/` | Reviewers and maintainers | Measured state bound to a commit | Targets invented without a baseline |
| `docs/roadmap/` | Everyone | What is planned | What already exists |
| `CLAUDE.md`, `AGENTS.md` | AI coding tools | Working instructions for this repository | A second copy of policy that has a canonical home |

## AI-facing instruction files

Two different things live here, and only one of them is now under control.

### Agent definitions — canonical, generated

`.agents/agents/` is the canonical source for every maintained agent.
`.claude/agents/` and `.codex/agents/` are **generated** from it, and the
`agent_definition_check` CTest fails when one is hand-edited, when an adapter has
no definition behind it, or when an agent's authority is stated one way in
`writes` and granted another in `tools`.

This replaced nine roles hand-written twice with nothing comparing them. One had
already drifted — in the write-authority clause of the conformance verifier, for
the one role whose value is that it cannot fix what it judges. See
[`.agents/README.md`](https://github.com/lasrod/assurance-forge/blob/main/.agents/README.md).

### `CLAUDE.md` and `AGENTS.md` — still hand-maintained copies

These are overlapping instructions for different tools, maintained by hand. That
is the arrangement most likely to drift, and it has: see the measurements above.
[#294](https://github.com/lasrod/assurance-forge/issues/294) covered the agent
definitions and **not** these two files, so the interim rule stands:

- Neither file is a canonical source for anything in the table above. Each may
  restate a rule for convenience, but on conflict the canonical source wins.
- A change to a canonical policy that agents must follow goes into **both**
  files, or into neither.

## Generated documents

Never edited by hand. Each carries a banner naming its generator, and a CI gate
fails if the committed copy is stale.

| Document | Generator | Gate |
|---|---|---|
| [SACM metamodel inventory](sacm/sacm-2.3-metamodel-inventory.md) | `tools/sacm/generate_metamodel_inventory.py` | — |
| [Verification record index](sacm/verification/README.md) (table only) | `tools/docs/generate_verification_index.py` | `documentation_check` |
| `docs/features/feature-matrix.json` | `tools/features/export_feature_matrix.py` | `feature_matrix_check` |
| [Quality baseline](quality/repository-baseline.md) | `tools/quality/collect_baseline.py` | Deliberately ungated — it is a snapshot, and going stale is correct |
| [Generated class diagrams](architecture/generated-class-diagrams.md) | `clang-uml`, see [Generating class diagrams](developer-guide/generating-class-diagrams.md) | — |

## How this is enforced

`tools/docs/check_documentation.py`, registered as the `documentation_check`
CTest, fails on:

1. **Broken internal links** in any tracked markdown file.
2. **Unreachable pages** — anything under `docs/` in neither the navigation nor
   any other page. New pages must be linked or listed in the checker's
   exceptions with a reason.
3. **Unmarked generated pages** — a generated document must name its generator.
4. **Architecture drift** — [Layers and ownership](architecture/layers-and-ownership.md)
   must describe every subsystem the layer gate enforces. This is the check that
   would have caught `sacm_adapter` and `mcp` being absent from all four
   architecture documents.

A gate is not the same as good documentation. These catch the failures that are
mechanically detectable, which is the class that had silently accumulated.
