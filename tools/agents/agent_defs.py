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
    elif "enforcement_scope" not in spec:
        raise DefinitionError(
            f"manifest: platform {platform!r} claims to enforce write denial but does not say "
            "what that covers. Set `enforcement_scope` to 'tools' or 'sandbox' -- the generated "
            "paragraph has to state the boundary, and the two are not the same boundary."
        )
    else:
        scope = spec["enforcement_scope"]

    # What each platform actually stops, said separately, because they differ and
    # the difference is the part that matters. An earlier version of this
    # collapsed both into "applied by the platform", which was true of Codex's
    # sandbox and false of Claude's tool list -- `Bash` is granted there, so a
    # shell can still write a file. Overstating an enforcement boundary is the
    # same mistake as understating one, and this generator has now made both.
    if scope == "tools":
        mechanism = (
            "You have no write, edit or notebook-edit tools. The harness applies that, so it "
            "holds whether or not you remember it.\n\n"
            "It does not cover `Bash`, which you do have. Writing a file through a shell "
            "command is therefore prohibited by this paragraph rather than by the platform -- "
            "the one part of your boundary that depends on you. Do not create, edit, move or "
            "delete a file that way."
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
