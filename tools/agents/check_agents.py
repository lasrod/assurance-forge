#!/usr/bin/env python3
"""Validate the canonical agent definitions and the generated adapters (issue #294).

Runs as the `agent_definition_check` CTest. Three failures it is for:

1. A definition that violates the contract in `.agents/schema/agent.schema.json` --
   a missing field, an unknown adapter, a description too short to tell a model
   when to delegate.
2. An authority boundary stated in one field and contradicted in another: a
   `writes: none` agent that also lists `Write`. Two fields that can disagree
   are a boundary nobody can rely on.
3. An adapter that no longer matches what the canonical definition generates,
   including one with no definition behind it at all.

The third is the reason this exists. Before #294 each role was hand-written per
platform with nothing comparing them, and `sacm-conformance-verifier` had already
drifted -- in its write-authority clause, for the one role whose value is that it
cannot fix what it judges.

Usage:
    python tools/agents/check_agents.py
"""

from __future__ import annotations

import json
import re
import sys
import tomllib

from agent_defs import (
    REPO,
    DefinitionError,
    load_agents,
    load_manifest,
    load_schema,
    parse_list,
)
from generate_adapters import planned_outputs


def check_frontmatter(agent: dict, schema: dict, manifest: dict) -> list[str]:
    rules = schema["frontmatter"]
    fields = agent["fields"]
    source = agent["path"].relative_to(REPO).as_posix()
    problems = []

    for key, rule in rules.items():
        required = rule.get("required", False)
        when = rule.get("required_when")
        if when and all(fields.get(k) == v for k, v in when.items()):
            required = True
        if key not in fields:
            if required:
                problems.append(f"{source}: missing required frontmatter key `{key}`")
            continue

        value = fields[key]
        if not value:
            problems.append(f"{source}: `{key}` is empty")
            continue
        if "enum" in rule and value not in rule["enum"]:
            problems.append(f"{source}: `{key}` is {value!r}, expected one of {rule['enum']}")
        if "pattern" in rule and not re.match(rule["pattern"], value):
            problems.append(f"{source}: `{key}` is {value!r}, which does not match {rule['pattern']}")
        if "min_length" in rule and len(value) < rule["min_length"]:
            problems.append(
                f"{source}: `{key}` is {len(value)} characters, at least {rule['min_length']} required"
            )
        if rule.get("must_match_filename") and value != agent["stem"]:
            problems.append(f"{source}: `{key}` is {value!r} but the file is named {agent['stem']!r}")
        # A list field is checked after parsing, not before. `tools: ,` is a
        # non-empty string that parses to no entries, and it reached the
        # generator, which emitted a bare `tools:` line into the Claude adapter
        # and "Your tools are ." into the authority section.
        if rule.get("type") == "list" and not parse_list(value):
            problems.append(f"{source}: `{key}` is {value!r}, which lists nothing")

    for key in fields:
        if key not in rules:
            problems.append(f"{source}: unknown frontmatter key `{key}`")

    for platform in agent["adapters"]:
        if platform not in manifest["platforms"]:
            known = ", ".join(sorted(manifest["platforms"]))
            problems.append(f"{source}: adapter {platform!r} is not a known platform ({known})")

    if len(agent["body"]) < schema["body"]["min_length"]:
        problems.append(
            f"{source}: body is {len(agent['body'])} characters, at least "
            f"{schema['body']['min_length']} required"
        )
    return problems


def check_authority_is_consistent(agent: dict, schema: dict) -> list[str]:
    """`writes` and `tools` must agree.

    Stated in two places, they can disagree, and a boundary that can disagree
    with itself is one nobody can rely on. The check runs in both directions so
    neither field can be relaxed on its own.
    """
    source = agent["path"].relative_to(REPO).as_posix()
    denied = set(schema["write_denied_tools"])
    writes = agent["fields"].get("writes")
    tools = set(agent["tools"])
    problems = []

    if writes == "none":
        if "all" in tools:
            problems.append(
                f"{source}: `writes: none` but `tools: all`, which grants every write tool"
            )
        offending = sorted(tools & denied)
        if offending:
            problems.append(
                f"{source}: `writes: none` but `tools` lists {', '.join(offending)}"
            )
    elif writes == "repository" and "all" not in tools and not (tools & denied):
        problems.append(
            f"{source}: `writes: repository` but `tools` grants no write tool "
            f"({', '.join(sorted(tools))}) -- say `writes: none` and give it a rationale, "
            "or grant the tool"
        )
    return problems


def check_evals(agents: list[dict]) -> tuple[list[str], int, int]:
    """Assert the mechanical eval cases, and that every case names real agents.

    The mechanical cases are properties of the definitions, so they are checked
    on every build: `verifier-cannot-write` stops being a statement somebody
    remembers and becomes one the gate enforces.

    The reviewable cases cannot be asserted here -- they are about what a model
    does when prompted, and this repository does not run one. What *is* checked
    is that each names agents that exist and carries the fields a reviewer needs.
    A case pointing at a deleted agent is worse than no case: it reads as
    coverage.
    """
    path = REPO / ".agents/evals/cases.json"
    if not path.exists():
        return [f"{path.relative_to(REPO).as_posix()}: missing"], 0, 0

    cases = json.loads(path.read_text(encoding="utf-8"))["cases"]
    by_name = {agent["stem"]: agent for agent in agents}
    problems: list[str] = []
    mechanical = reviewable = 0

    for case in cases:
        where = f"cases.json[{case.get('id', '<no id>')}]"
        for field in ("id", "kind", "behaviour", "why", "applies_to"):
            if not case.get(field):
                problems.append(f"{where}: missing `{field}`")
        if case.get("kind") not in ("mechanical", "reviewable"):
            problems.append(f"{where}: `kind` is {case.get('kind')!r}, expected mechanical or reviewable")
            continue

        for name in case.get("applies_to", []):
            if name not in by_name:
                problems.append(f"{where}: names agent {name!r}, which has no canonical definition")

        if case["kind"] == "reviewable":
            reviewable += 1
            for field in ("prompt", "expected"):
                if not case.get(field):
                    problems.append(f"{where}: reviewable case has no `{field}`")
            continue

        mechanical += 1
        expected = case.get("assert")
        if not expected:
            problems.append(f"{where}: mechanical case has no `assert` block")
            continue
        for name in case.get("applies_to", []):
            agent = by_name.get(name)
            if agent is None:
                continue
            if "writes" in expected and agent["fields"].get("writes") != expected["writes"]:
                problems.append(
                    f"{where}: expects {name} to have `writes: {expected['writes']}`, "
                    f"found {agent['fields'].get('writes')!r}"
                )
            if expected.get("has_rationale") and not agent["fields"].get("writes_rationale"):
                problems.append(f"{where}: expects {name} to state a `writes_rationale`, and it does not")

    return problems, mechanical, reviewable


def main() -> int:
    try:
        manifest = load_manifest()
        schema = load_schema()
        agents = load_agents()
    except DefinitionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    problems: list[str] = []
    for agent in agents:
        problems += check_frontmatter(agent, schema, manifest)
        problems += check_authority_is_consistent(agent, schema)

    if problems:
        print(f"{len(problems)} problem(s) in the canonical agent definitions:\n", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    try:
        outputs = planned_outputs(manifest)
    except DefinitionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    stale = []
    for path, content in sorted(outputs.items()):
        if not path.exists():
            stale.append(f"{path.relative_to(REPO).as_posix()}: missing")
        elif path.read_text(encoding="utf-8") != content:
            stale.append(f"{path.relative_to(REPO).as_posix()}: differs from its canonical definition")
        # Read the generated file back with the platform's own parser. Comparing
        # a string against a string says the adapter matches the definition; it
        # says nothing about whether the platform can load it. An unescaped quote
        # in a description produced TOML `tomllib` rejects, and the generator
        # wrote it out without complaint because nothing tried to read it.
        if path.suffix == ".toml":
            try:
                parsed = tomllib.loads(content)
            except tomllib.TOMLDecodeError as error:
                stale.append(
                    f"{path.relative_to(REPO).as_posix()}: generated content is not valid TOML ({error})"
                )
                continue
            # The restriction must be in the artifact, not only in the definition
            # it was generated from. Asserting `writes: none` in `.agents/` says
            # what was intended; this says the file Codex will actually load
            # carries it. The two came apart once already -- the first version of
            # this generator emitted no sandbox key at all, and told the agent its
            # restriction was advisory.
            denied = {a["stem"] for a in agents if a["fields"].get("writes") == "none"}
            if path.stem in denied and parsed.get("sandbox_mode") != "read-only":
                stale.append(
                    f"{path.relative_to(REPO).as_posix()}: `writes: none` but the generated "
                    f"file has sandbox_mode={parsed.get('sandbox_mode')!r}, not 'read-only'"
                )

    for platform, spec in manifest["platforms"].items():
        directory = REPO / spec["output_dir"]
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob(f"*{spec['extension']}")):
            if path not in outputs:
                stale.append(
                    f"{path.relative_to(REPO).as_posix()}: no canonical definition under "
                    f"{manifest['canonical_dir']}"
                )

    if stale:
        print(f"{len(stale)} adapter problem(s):\n", file=sys.stderr)
        for line in stale:
            print(f"  {line}", file=sys.stderr)
        print(
            "\nAdapters are generated, never hand-edited. Change the definition and run:\n"
            "\n  python tools/agents/generate_adapters.py",
            file=sys.stderr,
        )
        return 1

    eval_problems, mechanical, reviewable = check_evals(agents)
    if eval_problems:
        print(f"{len(eval_problems)} eval problem(s):\n", file=sys.stderr)
        for problem in eval_problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    write_denied = [a["stem"] for a in agents if a["fields"].get("writes") == "none"]
    print(
        f"OK: {len(agents)} agent definition(s) valid, {len(outputs)} adapter(s) in sync, "
        f"{len(write_denied)} write-denied, {mechanical} mechanical eval(s) asserted, "
        f"{reviewable} reviewable eval(s) recorded"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
