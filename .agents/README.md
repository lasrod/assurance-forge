# Canonical agent definitions

One source of truth for every AI development agent this repository maintains.
The files under `.claude/agents/` and `.codex/agents/` are **generated** — edit
one and the build fails.

```
.agents/
  manifest.json          which platforms get adapters, and what each can enforce
  schema/agent.schema.json   the contract a definition must satisfy
  agents/<name>.md       the definitions
  evals/cases.json       the behaviours that must hold, and how each is checked
```

## Why this exists

Each role used to be hand-written twice: a `.md` for Claude and a `.toml` for
Codex, with no generator and nothing comparing them. Eight of the nine shared
roles were byte-identical copies. The ninth had already drifted — and it drifted
in the **write-authority clause of the conformance verifier**, the one role whose
entire value is that it cannot fix what it judges. Claude's copy said *"you have
no write or edit tools, and this is deliberate"*; Codex's said *"you must not
create, edit, move, or delete any file."* One states a mechanical fact. The other
asks nicely. Nothing reported the difference.

That sentence is now generated from the `writes` field, per platform, so the two
cannot disagree.

## Adding or changing an agent

1. Edit or add a file under `agents/`. One file is one agent; the roster is the
   directory listing, so there is nothing else to register.
2. Regenerate:
   ```bash
   python tools/agents/generate_adapters.py
   ```
3. Check before pushing — this is the `agent_definition_check` CTest:
   ```bash
   python tools/agents/check_agents.py
   ```

Commit the canonical definition **and** the regenerated adapters together. The
gate compares them, so a definition committed without its adapters fails.

## The frontmatter

`schema/agent.schema.json` is the contract, read by the checker rather than
duplicated in it. The fields that carry weight:

| Field | Meaning |
|---|---|
| `writes` | `none` or `repository`. The authority boundary. |
| `writes_rationale` | Required when `writes: none`. Why this agent is denied. |
| `tools` | The tools granted, or `all`. Must agree with `writes`. |
| `adapters` | Which platforms to generate for. |

`writes` and `tools` are checked against each other in both directions: a
`writes: none` agent may not list `Write`, `Edit` or `NotebookEdit`, and a
`writes: repository` agent must actually be granted one. A boundary stated in two
places that can disagree is a boundary nobody can rely on.

`writes_rationale` is required only for the restriction, because an unexplained
restriction is the one the next person relaxes when it becomes inconvenient.

## What each platform can actually enforce

| Platform | Enforces `writes: none`? | How |
|---|---|---|
| Claude | **yes** | The `tools:` frontmatter key. The harness applies it. |
| Codex | **no** | The format carries no tool-permission key. The generated paragraph is the whole enforcement. |

This asymmetry is real and the generator says so out loud in the Codex output
rather than papering over it. Inventing a `tools = [...]` key that Codex ignores
would be worse than omitting it — it would read as enforcement.

## Evals

`evals/cases.json` records the behaviours #294 names as critical. Each case is
one of two kinds, and the distinction is not decoration:

- **mechanical** — a property of the definitions, asserted by
  `check_agents.py` on every build. `verifier-cannot-write` is not something
  somebody remembers; it fails the gate.
- **reviewable** — a property of what a model *does* when prompted. This
  repository does not run one, so the case records the prompt and the expected
  behaviour for a human to run and judge. What the gate checks is that the case
  names agents that exist and carries the fields a reviewer needs — a case
  pointing at a deleted agent is worse than no case, because it reads as
  coverage.

Calling the reviewable ones tests would overstate them. The checker prints the
count of each so the suite cannot be read as more than it is.

## Known gaps

- **The reviewable cases are not executed.** Eight of the twelve. Running them
  needs a harness that invokes a model and judges free-text output, which does
  not exist here yet.
- **`feature-matrix-steward` is Claude-only.** It always was. Giving it a Codex
  adapter is now one word in `adapters:`, but it is a roster change rather than
  a migration, so it was left for a deliberate decision.
- **Skills and workflows are not yet canonical.** `.claude/skills/` still holds
  two hand-written skills with no equivalent for other platforms, and #294's
  workflow layer — sequencing and hand-offs between roles — is not modelled at
  all. Only agents are covered.
- **The role set is unchanged.** #294 asks whether ten roles should become
  roughly five. This package deliberately migrates what exists so the move is
  verifiable; the merge proposal is on the issue.
