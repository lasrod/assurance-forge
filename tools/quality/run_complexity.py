#!/usr/bin/env python3
"""Measure cyclomatic complexity over the first-party sources (issue #343).

Writes a machine-readable snapshot to `docs/quality/complexity-baseline.json`
and, with --check, fails when a function's complexity grows past what the
baseline records.

**This control is REPORT MODE.** CI runs it without --check, so it measures and
prints and does not gate. That is the arrangement `docs/quality/code-quality-policy.md`
requires of a new control: report first, ratchet only after the numbers have been
triaged. --check exists so that flipping it on later is a CI edit rather than a
tooling project, and so this file can be tested now rather than when it matters.

Design rules, shared deliberately with `run_clang_tidy.py` -- two quality tools
that disagree about how to be run is how one of them stops being run:

- The tool version is pinned by the baseline and --check refuses to compare
  across a mismatch. Complexity counting is not standardised; two lizard
  versions can legitimately return different numbers for the same function, and
  a comparison across them reports growth nobody wrote (#317 learned this the
  expensive way with clang-tidy).
- The ratchet keys on (file, function name), never on line numbers, which move
  whenever anything above them is edited.
- A missing prerequisite is reported, never silently skipped. A run that
  analyzed nothing must not look like a run that found nothing.
- Production code only. Test bodies are long and branchy by nature and would
  dominate the ranking with functions nobody ships.

Usage:
    python tools/quality/run_complexity.py           # measure, write JSON, print summary
    python tools/quality/run_complexity.py --report  # list every function over the threshold
    python tools/quality/run_complexity.py --check   # non-zero exit if complexity grew
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
JSON_PATH = REPO / "docs/quality/complexity-baseline.json"

# Roots analysed, mirroring run_clang_tidy.py's first-party set.
SOURCE_ROOTS = ["src", "libs/sacm/src", "libs/sacm/include"]

# Excluded for the same reason clang-tidy excludes them: a different risk
# profile, and they would triple the runtime for findings nobody ships.
EXCLUDED_PREFIXES = ("external/", "tests/", "libs/sacm/tests/", "libs/sacm/tools/")

SOURCE_SUFFIXES = (".cpp", ".cc", ".h", ".hpp")

# lizard's own default warning threshold. Chosen because it is the tool's
# default rather than a number invented here: an arbitrary project-specific
# threshold is the kind of target the policy forbids setting without a baseline.
CCN_THRESHOLD = 15


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(2)


def load_lizard():
    try:
        import lizard  # noqa: PLC0415  (imported here so the error message can be ours)
    except ImportError:
        fail(
            "lizard is not installed. It is the pinned complexity tool: "
            "pip install lizard==<version in docs/quality/complexity-baseline.json>"
        )
    return lizard


def tool_version() -> str:
    from importlib import metadata

    try:
        return metadata.version("lizard")
    except metadata.PackageNotFoundError:  # pragma: no cover - install is checked above
        fail("lizard is installed but reports no version; cannot pin a baseline to it")
        return ""


def normalize(path: Path) -> str:
    return path.relative_to(REPO).as_posix()


def list_sources() -> list[Path]:
    sources: list[Path] = []
    for root in SOURCE_ROOTS:
        base = REPO / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            rel = normalize(path)
            if rel.startswith(EXCLUDED_PREFIXES):
                continue
            sources.append(path)
    return sources


def measure(sources: list[Path]) -> dict:
    lizard = load_lizard()
    functions: dict[str, dict[str, int]] = {}
    total_nloc = 0
    total_functions = 0

    for path in sources:
        analysis = lizard.analyze_file(str(path))
        rel = normalize(path)
        total_nloc += analysis.nloc
        for function in analysis.function_list:
            total_functions += 1
            if function.cyclomatic_complexity < CCN_THRESHOLD:
                continue
            # Keyed by name rather than line so an edit above a function does
            # not read as a change to it. A name that appears twice in one file
            # (an overload set) collapses to its worst case, which is the one
            # the ranking cares about.
            key = f"{rel}::{function.name}"
            existing = functions.get(key)
            if existing is None or function.cyclomatic_complexity > existing["ccn"]:
                functions[key] = {
                    "ccn": function.cyclomatic_complexity,
                    "nloc": function.nloc,
                    "parameters": function.parameter_count,
                }

    per_file: dict[str, int] = {}
    for key, record in functions.items():
        file_part = key.split("::", 1)[0]
        per_file[file_part] = max(per_file.get(file_part, 0), record["ccn"])

    return {
        "tool": "lizard",
        "tool_version": tool_version(),
        "ccn_threshold": CCN_THRESHOLD,
        "files_analyzed": len(sources),
        "functions_analyzed": total_functions,
        "production_nloc": total_nloc,
        "over_threshold": len(functions),
        "functions": dict(sorted(functions.items())),
        "worst_ccn_per_file": dict(sorted(per_file.items(), key=lambda kv: (-kv[1], kv[0]))),
    }


def compare(current: dict, baseline: dict) -> list[str]:
    if current["tool_version"] != baseline.get("tool_version"):
        fail(
            f"lizard {current['tool_version']} does not match the baseline's "
            f"{baseline.get('tool_version')}. Different versions count differently, so the "
            "comparison would report growth nobody wrote. Install the pinned version or "
            "regenerate the baseline deliberately."
        )
    if current["ccn_threshold"] != baseline.get("ccn_threshold"):
        fail("the threshold changed; regenerate the baseline rather than comparing across two of them")

    regressions: list[str] = []
    baseline_functions = baseline.get("functions", {})
    for key, record in current["functions"].items():
        previous = baseline_functions.get(key)
        if previous is None:
            regressions.append(f"{key}: new function at complexity {record['ccn']} (threshold {CCN_THRESHOLD})")
        elif record["ccn"] > previous["ccn"]:
            regressions.append(f"{key}: complexity {previous['ccn']} -> {record['ccn']}")
    return regressions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if complexity grew past the baseline")
    parser.add_argument("--report", action="store_true", help="list every function at or over the threshold")
    args = parser.parse_args()

    sources = list_sources()
    if not sources:
        fail("no first-party sources found; the run would report a clean tree it never looked at")

    current = measure(sources)

    print(
        f"lizard {current['tool_version']}: {current['functions_analyzed']} function(s) in "
        f"{current['files_analyzed']} file(s); {current['over_threshold']} at or over CCN {CCN_THRESHOLD}"
    )

    if args.report:
        for key, record in sorted(current["functions"].items(), key=lambda kv: (-kv[1]["ccn"], kv[0])):
            print(f"  CCN {record['ccn']:>3}  nloc {record['nloc']:>4}  {key}")

    if args.check:
        if not JSON_PATH.exists():
            fail(f"no baseline at {normalize(JSON_PATH)}; run without --check first and commit the result")
        baseline = json.loads(JSON_PATH.read_text(encoding="utf-8"))
        regressions = compare(current, baseline)
        if regressions:
            print(f"complexity grew in {len(regressions)} place(s):", file=sys.stderr)
            for line in regressions:
                print(f"  {line}", file=sys.stderr)
            return 1
        print("complexity is within the baseline")
        return 0

    JSON_PATH.write_text(json.dumps(current, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    print(f"wrote {normalize(JSON_PATH)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
