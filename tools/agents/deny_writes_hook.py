#!/usr/bin/env python3
"""Refuse file-writing tools to write-denied agents (issue #326).

A Claude Code PreToolUse hook, wired in `.claude/settings.json`. It exists because
the `tools:` frontmatter key did not do what the generated Authority paragraph
said it did: a probe in #326 had `sacm-conformance-verifier` -- declared
`tools: Read, Grep, Glob, Bash` -- call `Write` and `Edit` successfully.
Subagents here run in the background by default, and a background subagent is
given Write and Edit whatever its `tools:` list says.

The hook input names the calling agent in `agent_type` when the call comes from
a subagent, and carries no such field for the main session. A write tool called
by an agent whose canonical definition says `writes: none` is refused. Every
other call is left to the normal permission flow, so the hook never *grants*
anything.

What it cannot do, stated so nobody reads more into it:

- `Bash` is not covered. A read-only role still has to build and run things, and
  a shell command that writes a file cannot be reliably recognised from its text.
- It runs only while project hooks do. A session with hooks disabled, or a
  machine where Python cannot be started, refuses nothing. The generated
  Authority paragraph says so.

Stdlib only, like the rest of `tools/agents/`. It reads the canonical definitions
through `agent_defs`, so the hook and the gate cannot disagree about which agents
are write-denied.

Usage (by Claude Code, with the hook input on stdin):
    python tools/agents/deny_writes_hook.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from agent_defs import WRITE_TOOLS, load_agents  # noqa: E402


def write_denied_agent_names() -> frozenset[str]:
    names: set[str] = set()
    for agent in load_agents():
        if agent["fields"].get("writes") == "none":
            names.add(agent["stem"])
            names.add(agent["fields"].get("name", agent["stem"]))
    return frozenset(names)


def deny(reason: str) -> dict:
    return {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }


def calling_subagent_write(payload: dict) -> tuple[str, str] | None:
    """(tool, agent) when a subagent calls a write tool; None for anything else."""
    tool = payload.get("tool_name")
    agent = payload.get("agent_type")
    if tool not in WRITE_TOOLS or not isinstance(agent, str) or not agent:
        return None
    return tool, agent


def decide(payload: dict, denied: frozenset[str] | None) -> dict | None:
    """The decision for one tool call, or None to leave it to the normal flow.

    `denied` is None when the definitions could not be read. A write by a
    subagent is then refused: the hook cannot tell whether this agent may write,
    and the agents that may not are the ones whose value is that they cannot.
    """
    call = calling_subagent_write(payload)
    if call is None:
        return None
    tool, agent = call
    if denied is None:
        return deny(
            f"{tool} refused for subagent {agent}: tools/agents/deny_writes_hook.py could not read "
            ".agents/agents/, so it cannot tell whether this agent may write (#326)."
        )
    if agent not in denied:
        return None
    return deny(
        f"{tool} refused: {agent} is write-denied (`writes: none` in .agents/agents/{agent}.md). "
        "It reports findings and does not change files. The refusal comes from "
        "tools/agents/deny_writes_hook.py (#326)."
    )


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, UnicodeDecodeError):
        # Input the hook cannot read names no tool and no agent, so there is
        # nothing to decide. Refusing here would refuse every write in every
        # session over a harness defect; say so on stderr instead.
        print("deny_writes_hook: unreadable hook input; no decision made", file=sys.stderr)
        return 0
    if not isinstance(payload, dict) or calling_subagent_write(payload) is None:
        # The common case -- the main session, or a tool that writes nothing --
        # decided without reading a single definition.
        return 0

    denied: frozenset[str] | None
    try:
        denied = write_denied_agent_names()
    except Exception:  # noqa: BLE001 -- fail closed on any unreadable roster
        # Deliberately broad. A malformed manifest raises JSONDecodeError, an
        # undecodable definition UnicodeDecodeError, a definition missing a key
        # KeyError; naming the ones foreseen is how the unforeseen one would exit
        # without a decision and let a write-denied agent write.
        denied = None
    decision = decide(payload, denied)
    if decision is not None:
        print(json.dumps(decision))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
