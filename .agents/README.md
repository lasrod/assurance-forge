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

## The roles

Seven. There were ten until issue #294 consolidated overlapping roles — a change
to the roster, not a git merge. The test applied was whether a role needs its own
**authority boundary**, not whether it does a distinguishable job.

| Role | Writes | Covers |
|---|---|---|
| `sacm-implementation-lead` | repository | Sequences the rest |
| `sacm-library-architect` | repository | Decides boundaries; does not implement them |
| `sacm-implementer` | repository | The library, and the adapter that consumes it |
| `sacm-xmi-test-engineer` | repository | The failing test, written before the fix |
| `sacm-researcher` | **none** | Specification, metamodel, interoperability |
| `sacm-conformance-verifier` | **none** | Judges whether a slice can honestly be called verified |
| `feature-matrix-steward` | **none** | Audits capability claims |

Three merged into `sacm-researcher` because they shared a boundary exactly: all
read-only, all producing a document, differing only in **what they read**. Two
merged into `sacm-implementer` for the same reason — the adapter engineer's rule
about not contaminating the library with Assurance Forge vocabulary is a
constraint the merged role carries, not a separate authority.

The verifier was not merged into anything: it is the only role whose value comes
from what it *cannot* do. The test engineer stayed separate from the implementer
because that seam is what makes test-first observable — whoever writes the
failing test should not also be the one who decides it was too hard to write.

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

| Platform | Mechanism | Covers | Left to the prompt |
|---|---|---|---|
| Claude | `tools:` frontmatter | Write, Edit, NotebookEdit | Writing via `Bash`, which is granted |
| Codex | `sandbox_mode = "read-only"` | All writes, shell included | — |

The two are **not the same boundary**, and the generated paragraph says which is
which on each platform. Claude's tool list cannot stop a shell command, because a
read-only role still needs `Bash` to build and run things; Codex's sandbox can.

Both are emitted by the generator and both are checked: `check_agents.py` parses
each generated `.toml` and fails if a `writes: none` agent's file does not carry
the read-only sandbox. Asserting `writes: none` in the definition says what was
intended; parsing the artifact says what the platform will actually load.

> **This table said "no" for Codex when the package landed**, on the strength of
> the hand-written `.toml` files carrying only `name`, `description` and
> `developer_instructions`. That was a fact about what somebody had written, not
> about what the format supports — and the generator turned it into a paragraph
> telling four agents their restriction was advisory. If a platform looks unable
> to enforce something, check its documentation rather than its existing files.

Where a platform genuinely cannot express a restriction, say so in the output
rather than papering over it. Emitting a key the platform ignores would be worse
than omitting it — it would read as enforcement.

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

Each is tracked, because a gap recorded only as prose is one nobody is assigned
to close. #294 is closed on what it delivered; these are what it did not.

- **The reviewable cases are not executed** — [#327](https://github.com/lasrod/assurance-forge/issues/327).
  Eight of the twelve. Running them needs a harness that invokes a model and
  judges free-text output, and a judge that errs permissively about safety
  behaviours is worse than no result.
- **Skills and workflows are not canonical** — [#328](https://github.com/lasrod/assurance-forge/issues/328).
  `.claude/skills/` still holds two hand-written skills with no equivalent for
  other platforms, and the workflow layer — sequencing and hand-offs between
  roles — is not modelled at all. Only agents are covered.
  This is also where the merged modes belong: `sacm-researcher` carries three
  modes and `sacm-implementer` two scopes, and under #294's own principle those
  are knowledge rather than authority. They are prompt sections because there is
  nowhere better yet.
- **`feature-matrix-steward` is Claude-only** — [#329](https://github.com/lasrod/assurance-forge/issues/329).
  It always was. Adding a Codex adapter is one word in `adapters:`, but that is
  a roster change rather than a migration, so it is a decision to make rather
  than a default to take.
- **The runtime may grant more than the definitions declare** —
  [#326](https://github.com/lasrod/assurance-forge/issues/326). The Claude
  session roster has listed write-denied agents with `Write` and `Edit`,
  including one created in the same session, which rules out a stale file. If
  that is real, the `tools:` mechanism this package treats as enforcement is
  weaker than the generated paragraph claims — the same error as #324, in the
  opposite direction. It needs an experiment, not more reading.
