#!/usr/bin/env python3
"""Generate the platform agent adapters from `.agents/agents/` (issue #294).

Before this existed, each role was a hand-written `.md` under `.claude/agents/`
and a hand-written `.toml` under `.codex/agents/`, with no generator and no
check. Eight of the nine shared roles were byte-identical copies; the ninth had
already drifted, in its write-authority clause.

Usage:
    python tools/agents/generate_adapters.py            # write the adapters
    python tools/agents/generate_adapters.py --check    # fail if they are stale
"""

from __future__ import annotations

import argparse
import sys

# Same directory as this script, which Python puts on sys.path[0] when running it.
from agent_defs import (
    REPO,
    DefinitionError,
    authority_section,
    load_agents,
    load_manifest,
)


def render_claude(agent: dict, manifest: dict) -> str:
    """Markdown with the frontmatter Claude reads.

    `tools:` is the mechanism, so it is emitted from the canonical `tools` field
    and never hand-edited in the output. `all` means "no restriction", which
    Claude spells by omitting the key entirely -- emitting `tools: all` would
    name a tool that does not exist.
    """
    fields = agent["fields"]
    lines = [
        "---",
        f"name: {fields['name']}",
        f"description: {fields['description']}",
        f"model: {fields['model']}",
        f"memory: {fields['memory']}",
        f"color: {fields['color']}",
    ]
    if agent["tools"] != ["all"]:
        lines.append(f"tools: {', '.join(agent['tools'])}")
    # A blank line after the closing `---`, which is what the hand-written files
    # had. Matching the existing convention keeps the migration diff to what
    # actually changed instead of burying it in whitespace.
    lines += ["---", "", ""]

    authority = authority_section(agent, "claude", manifest)
    body = f"{authority}\n{agent['body']}" if authority else agent["body"]
    return "\n".join(lines) + body.strip() + "\n"


def toml_basic_string(value: str) -> str:
    """Quote a value as a TOML basic string, escaping what TOML requires.

    Without this, a description containing a double quote emitted TOML that
    `tomllib` refuses to parse -- and the generator wrote it out happily, because
    nothing downstream tried to read the result back. A generator that can emit a
    file its own platform cannot load is worse than a hand-written one.
    """
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    for char, replacement in (("\n", "\\n"), ("\r", "\\r"), ("\t", "\\t"), ("\b", "\\b"), ("\f", "\\f")):
        escaped = escaped.replace(char, replacement)
    # Remaining control characters have no short escape and must be \uXXXX.
    escaped = "".join(c if c >= " " or c in "\\" else f"\\u{ord(c):04X}" for c in escaped)
    return f'"{escaped}"'


def render_codex(agent: dict, manifest: dict) -> str:
    """TOML with the required keys, plus the sandbox mode when writes are denied.

    A Codex agent file accepts `config.toml` keys beyond the three required ones,
    and `sandbox_mode = "read-only"` is the one that restricts writes -- the
    documented example of a read-only explorer agent has exactly this shape.

    The first version of this generator omitted it and told the reader Codex
    could not enforce anything, because the hand-written files it replaced
    carried only the three required keys. That was a fact about what somebody had
    written, not about what the format supports, and stating it as the latter
    made a real boundary look advisory.
    """
    fields = agent["fields"]
    authority = authority_section(agent, "codex", manifest)
    body = f"{authority}\n{agent['body']}" if authority else agent["body"]
    sandbox = 'sandbox_mode = "read-only"\n' if fields.get("writes") == "none" else ""

    if '"""' in body:
        raise DefinitionError(
            f"{agent['stem']}: body contains a TOML triple-quote delimiter and cannot be emitted"
        )
    # The closing delimiter sits directly after the body, as the hand-written
    # files had it. A newline before it would land inside the string: TOML trims
    # one after the opening `"""` and none before the closing one.
    return (
        f"name = {toml_basic_string(fields['name'])}\n"
        f"description = {toml_basic_string(fields['description'])}\n"
        f"{sandbox}"
        f'developer_instructions = """\n{body.strip()}"""\n'
    )


RENDERERS = {"claude": render_claude, "codex": render_codex}


def planned_outputs(manifest: dict) -> dict:
    """Map every adapter path to the content it should hold."""
    outputs = {}
    for agent in load_agents():
        for platform in agent["adapters"]:
            if platform not in manifest["platforms"]:
                raise DefinitionError(
                    f"{agent['stem']}: adapter {platform!r} is not declared in manifest.json"
                )
            spec = manifest["platforms"][platform]
            path = REPO / spec["output_dir"] / f"{agent['stem']}{spec['extension']}"
            outputs[path] = RENDERERS[platform](agent, manifest)
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; exit non-zero if any adapter differs from what would be generated",
    )
    args = parser.parse_args()

    manifest = load_manifest()
    try:
        outputs = planned_outputs(manifest)
    except DefinitionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    # An adapter file with no canonical definition behind it is as much a drift
    # as a stale one: it is a role that exists on one platform and nowhere else,
    # which is how `feature-matrix-steward` came to be Claude-only.
    orphans = []
    for platform, spec in manifest["platforms"].items():
        directory = REPO / spec["output_dir"]
        if directory.is_dir():
            orphans += [p for p in sorted(directory.glob(f"*{spec['extension']}")) if p not in outputs]

    if args.check:
        stale = [p for p, content in outputs.items() if not p.exists() or p.read_text(encoding="utf-8") != content]
        if not stale and not orphans:
            print(f"{len(outputs)} adapter(s) match the canonical definitions")
            return 0
        for path in stale:
            state = "missing" if not path.exists() else "differs from the canonical definition"
            print(f"  {path.relative_to(REPO).as_posix()}: {state}", file=sys.stderr)
        for path in orphans:
            print(f"  {path.relative_to(REPO).as_posix()}: no canonical definition under .agents/agents", file=sys.stderr)
        print(
            "\nAdapters are generated, never edited. Change the definition under "
            ".agents/agents/\nand run:\n\n  python tools/agents/generate_adapters.py",
            file=sys.stderr,
        )
        return 1

    for path, content in sorted(outputs.items()):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
    print(f"wrote {len(outputs)} adapter(s)")
    for path in orphans:
        print(f"note: {path.relative_to(REPO).as_posix()} has no canonical definition and was left alone")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
