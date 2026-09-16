#!/usr/bin/env python3
"""Read the canonical agent definitions under `.agents/` (issue #294).

Shared by generate_adapters.py and check_agents.py so the generator and the gate
cannot disagree about what a definition means -- two parsers is how the drift
this package exists to prevent would reappear one level up.

Stdlib only, deliberately. This runs as a repository gate on three CI platforms,
and no other tool under `tools/` takes a third-party dependency; adding PyYAML
here would make the gate the only thing in the repository that needs an install
step before it can tell you the tree is sound.
"""

from __future__ import annotations

import json
import re
import textwrap
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
AGENTS_DIR = REPO / ".agents"
MANIFEST_PATH = AGENTS_DIR / "manifest.json"
SCHEMA_PATH = AGENTS_DIR / "schema" / "agent.schema.json"

# Every tool that creates or changes a file on its own: the tools the write hook
# refuses, `disallowedTools:` names, the Authority paragraph lists, and the schema's
# `write_denied_tools` must equal (the checker compares them). One list, because
# four hand-kept copies had already disagreed about MultiEdit. MultiEdit is listed
# although a release may not offer it: refusing a tool that does not exist costs
# nothing, and a release that brings it back must not open a hole.
WRITE_TOOLS = ("Write", "Edit", "MultiEdit", "NotebookEdit")

# Frontmatter is `key: value`, one per line, values never spanning lines. That is
# all the canonical format allows, which is why it can be parsed in six lines
# rather than depending on a YAML implementation.
FRONTMATTER_LINE = re.compile(r"^([a-z_]+):\s*(.*)$")


class DefinitionError(Exception):
    """A canonical definition that cannot be read at all, as opposed to one that
    parses but violates the schema. The distinction matters to the caller: the
    first stops the run, the second is collected and reported with the rest."""


def load_manifest() -> dict:
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def load_schema() -> dict:
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


def split_frontmatter(text: str, source: str) -> tuple[dict[str, str], str]:
    if not text.startswith("---\n"):
        raise DefinitionError(f"{source}: does not start with a `---` frontmatter block")
    parts = text.split("---", 2)
    if len(parts) < 3:
        raise DefinitionError(f"{source}: frontmatter block is not closed with `---`")
    fields: dict[str, str] = {}
    for number, line in enumerate(parts[1].strip().splitlines(), start=2):
        if not line.strip():
            continue
        match = FRONTMATTER_LINE.match(line)
        if not match:
            raise DefinitionError(f"{source}:{number}: not a `key: value` frontmatter line: {line!r}")
        key, value = match.group(1), match.group(2).strip()
        if key in fields:
            raise DefinitionError(f"{source}:{number}: duplicate frontmatter key {key!r}")
        fields[key] = value
    return fields, parts[2].strip()


def parse_list(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def load_agents() -> list[dict]:
    """Every canonical definition, sorted by name.

    The roster is the directory listing rather than a list in the manifest:
    adding an agent should be adding one file, and a manifest that also enumerates
    them is a second place to forget.
    """
    manifest = load_manifest()
    directory = REPO / manifest["canonical_dir"]
    if not directory.is_dir():
        raise DefinitionError(f"{manifest['canonical_dir']} does not exist")

    agents = []
    for path in sorted(directory.glob("*.md")):
        fields, body = split_frontmatter(path.read_text(encoding="utf-8"), path.name)
        agents.append(
            {
                "path": path,
                "stem": path.stem,
                "fields": fields,
                "body": body,
                "tools": parse_list(fields.get("tools", "")),
                "adapters": parse_list(fields.get("adapters", "")),
            }
        )
    if not agents:
        raise DefinitionError(f"no agent definitions under {manifest['canonical_dir']}")
    return agents


def authority_section(agent: dict, platform: str, manifest: dict) -> str:
    """The write-authority paragraph, generated per platform.

    This is generated rather than written by hand because it is the paragraph
    that drifted. `sacm-conformance-verifier` told Claude "you have no write or
    edit tools, and this is deliberate" and Codex "you must not create, edit,
    move, or delete any file" -- a mechanical fact on one platform and a request
    on the other, for the one role whose value is that it cannot fix what it
    judges. Two hand-written copies of a sentence about authority is two chances
    to be wrong about it.

    Only `writes: none` agents get a section. An agent that may write does not
    need a paragraph saying so, and adding one to all ten would train the reader
    to skip the heading that matters.
    """
    if agent["fields"].get("writes") != "none":
        return ""

    rationale = agent["fields"].get("writes_rationale", "").strip()
    tools = ", ".join(f"`{tool}`" for tool in agent["tools"])
    spec = manifest["platforms"][platform]
    if not spec["enforces_write_denial"]:
        scope = "none"
    elif spec.get("enforcement_scope") not in ("hook", "sandbox"):
        raise DefinitionError(
            f"manifest: platform {platform!r} claims to enforce write denial but its "
            f"`enforcement_scope` is {spec.get('enforcement_scope')!r}. Set it to 'hook' or 'sandbox' "
            "-- the generated paragraph has to state the boundary, and the two are not the same "
            "boundary."
        )
    else:
        scope = spec["enforcement_scope"]

    # What each platform actually stops, said separately, because they differ and
    # the difference is the part that matters. An earlier version of this
    # collapsed both into "applied by the platform", which was true of Codex's
    # sandbox and false of Claude's tool list -- `Bash` is granted there, so a
    # shell can still write a file. Overstating an enforcement boundary is the
    # same mistake as understating one, and this generator has now made both.
    #
    # And a third time, in #326: the Claude paragraph said the `tools:` list
    # removed the write tools, "so it holds whether or not you remember it", and a
    # probe showed a background subagent calling Write and Edit anyway. The
    # 'tools' scope is gone rather than kept for a platform that might honour it;
    # a scope this generator does not recognise now stops the run instead of
    # falling through to a paragraph that claims enforcement.
    if scope == "hook":
        mechanism = (
            "A Write, Edit, MultiEdit or NotebookEdit call from you is refused by a project hook "
            "(`tools/agents/deny_writes_hook.py`, wired in `.claude/settings.json`), which checks "
            "your canonical definition. Your `tools:` list does not do this on its own: a subagent "
            "here can be handed those tools regardless of it (#326).\n\n"
            "The hook does not cover `Bash`, which you do have, and it runs only while project "
            "hooks do -- with hooks disabled, or where Python cannot start, nothing refuses the "
            "call. Both remainders are prohibited by this paragraph rather than by the platform. "
            "Do not create, edit, move or delete a file, by tool or by shell command."
        )
    elif scope == "sandbox":
        mechanism = (
            "You run in a read-only sandbox. Creating, editing, moving or deleting a file is "
            "refused by the platform, including through a shell command, so the boundary does "
            "not depend on you remembering it. Do not spend attempts finding its edge."
        )
    else:
        mechanism = (
            "You must not create, edit, move or delete any file, and must not use a shell "
            "command to do so. On this platform that restriction is **instruction only** -- "
            "the agent format has no way to express it, so this paragraph is the whole of "
            "the enforcement. Treat it as binding."
        )

    if scope == "hook":
        # Not "your tools are": on this platform the list the agent is shown can be
        # longer than its definition (#326), and a sentence saying otherwise would
        # be the same false claim the paragraph above just corrected.
        closing = (
            f"Your definition grants {tools}. The platform may still show you more tools -- that is "
            "what #326 found -- and the hook above is what refuses the write tools among them. "
            "`Bash` is for building and running things -- you cannot judge what you have not "
            "executed -- and never for changing them."
        )
    else:
        closing = (
            f"Your tools are {tools}. `Bash` is for building and running things -- you cannot "
            "judge what you have not executed -- and never for changing them."
        )

    # `mechanism` may itself be more than one paragraph -- the tools-scoped one is
    # two, because what the platform enforces and what it leaves to you are
    # different claims and running them together is how the last version got it
    # wrong. Split before wrapping so the blank line survives.
    paragraphs = [*mechanism.split("\n\n"), rationale, closing]
    wrapped = "\n\n".join(textwrap.fill(p, width=88) for p in paragraphs if p)
    return f"## Authority\n\n{wrapped}\n"
