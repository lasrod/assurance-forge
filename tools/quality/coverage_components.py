#!/usr/bin/env python3
"""Per-component coverage, and a ratchet that does not invent a target (issue #292).

The Coverage workflow publishes four fixed scopes. Each is a useful view and
none of them answers "did the subsystem I just changed get worse", because a
drop in `src/parser` is invisible inside a number averaged over `src/core`.

This aggregates one gcovr JSON run into per-component figures and compares them
against a committed baseline.

Design rules, because a coverage gate is easy to make either useless or hated:

- The threshold for each component is **its own measured value**, not a number
  someone picked. #292 asks for ratchets "based on the baseline rather than an
  arbitrary repository-wide number", and a single repository-wide floor would
  be met by `libs/sacm` at 84% while `src/ui` fell through the floor unnoticed.
- Only decreases fail. Coverage that rises updates nothing automatically: the
  baseline is regenerated deliberately, so the improvement is a commit someone
  made rather than a number that drifted.
- A component that vanishes from the report is a failure, not an absence. That
  is what a build which stopped compiling a subsystem looks like, and it would
  otherwise read as "nothing to report here".

Usage:
    python tools/quality/coverage_components.py --gcovr-json coverage.json
    python tools/quality/coverage_components.py --gcovr-json coverage.json --check
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BASELINE_PATH = REPO / "docs/quality/coverage-baseline.json"

# Components in the order the architecture lists them, low layer to high. The
# prefix is matched against the repo-relative path, longest first, so
# `libs/sacm` wins over `libs`.
COMPONENTS = [
    "libs/sacm",
    "src/sacm",
    "src/parser",
    "src/sacm_adapter",
    "src/core",
    "src/ai",
    "src/export",
    "src/bridge",
    "src/agent",
    "src/mcp",
    "src/ui",
    "src/app",
]

# A drop smaller than this is not reported. Line coverage is deterministic for a
# deterministic suite, so this is deliberately tight: it absorbs a line moving
# between files, not a subsystem losing tests.
TOLERANCE_POINTS = 0.2


def fail(message: str) -> None:
    sys.stderr.write(f"error: {message}\n")
    raise SystemExit(2)


def component_of(path: str) -> str | None:
    normalized = path.replace("\\", "/").lstrip("./")
    for component in sorted(COMPONENTS, key=len, reverse=True):
        if normalized.startswith(component + "/"):
            return component
    return None


def measure(gcovr_json: Path) -> dict[str, dict]:
    """Aggregate gcovr's per-file line data into per-component totals."""
    try:
        report = json.loads(gcovr_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read gcovr JSON at {gcovr_json}: {error}")

    files = report.get("files")
    if not files:
        fail(f"{gcovr_json} lists no files; refusing to derive a baseline from an empty report")

    totals: dict[str, list[int]] = {name: [0, 0] for name in COMPONENTS}
    for entry in files:
        component = component_of(entry.get("file", ""))
        if component is None:
            continue  # external/, build/, tests/ -- not a component
        for line in entry.get("lines", []):
            # Every entry in gcovr's `lines` array is a coverable line -- it
            # does not emit blanks, comments or declarations -- so the count of
            # entries is the denominator, and `count > 0` is the numerator.
            totals[component][0] += 1
            if line.get("count", 0) > 0:
                totals[component][1] += 1

    measured = {}
    for name, (total, covered) in totals.items():
        if total == 0:
            continue  # a component with no instrumented lines in this build
        measured[name] = {
            "lines": total,
            "covered": covered,
            # Display only. Comparisons use `lines` and `covered` directly, so
            # no rounding sits between a real regression and the tolerance: a
            # 0.25-point drop rounded to 0.2 would otherwise slip through a
            # 0.2-point threshold.
            "percent": round(100.0 * covered / total, 2),
        }
    if not measured:
        fail("no component matched any file in the report; the path prefixes are wrong")
    return measured


def render(measured: dict[str, dict]) -> str:
    width = max(len(name) for name in measured)
    rows = [f"{'component'.ljust(width)}   lines  covered  percent"]
    for name in COMPONENTS:
        if name not in measured:
            continue
        entry = measured[name]
        rows.append(
            f"{name.ljust(width)}  {entry['lines']:6d}  {entry['covered']:7d}  {entry['percent']:6.2f}%"
        )
    return "\n".join(rows)


def load_baseline() -> dict[str, dict]:
    """Read the committed baseline, failing with one line rather than a traceback.

    This runs as a workflow gate. A stack trace in the log says "the tool broke"
    when the actual message is "the file you are gating on is not usable", and
    the two want different responses.
    """
    relative = BASELINE_PATH.relative_to(REPO)
    try:
        document = json.loads(BASELINE_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"{relative} is not readable JSON: {error}")

    components = document.get("components") if isinstance(document, dict) else None
    if not isinstance(components, dict) or not components:
        fail(f"{relative} has no 'components' object; regenerate it without --check")

    for name, entry in components.items():
        if not isinstance(entry, dict) or not {"lines", "covered"} <= entry.keys():
            fail(f"{relative}: component '{name}' is missing 'lines' or 'covered'")
        if not isinstance(entry["lines"], int) or entry["lines"] <= 0:
            fail(f"{relative}: component '{name}' has a non-positive line count")
    return components


def exact_percent(entry: dict) -> float:
    """Percentage from the raw counts, so no rounding precedes a comparison."""
    return 100.0 * entry["covered"] / entry["lines"]


def compare(measured: dict[str, dict], baseline: dict[str, dict]) -> tuple[list[str], list[str]]:
    regressions: list[str] = []
    missing: list[str] = []
    for name, was in baseline.items():
        now = measured.get(name)
        if now is None:
            missing.append(name)
            continue
        before, after = exact_percent(was), exact_percent(now)
        drop = before - after
        if drop > TOLERANCE_POINTS:
            regressions.append(f"{name}: {before:.2f}% -> {after:.2f}% ({drop:.2f} points)")
    return regressions, missing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcovr-json", required=True, type=Path)
    parser.add_argument("--check", action="store_true", help="fail if a component regressed")
    args = parser.parse_args()

    measured = measure(args.gcovr_json)
    print(render(measured))

    if not args.check:
        payload = {
            "tolerance_points": TOLERANCE_POINTS,
            "components": measured,
        }
        BASELINE_PATH.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"\nwrote {BASELINE_PATH.relative_to(REPO)}")
        return 0

    if not BASELINE_PATH.exists():
        # Not a pass. The first run has nothing to compare against, and saying
        # so beats exiting zero on a check that checked nothing.
        print(
            f"\nno baseline at {BASELINE_PATH.relative_to(REPO)}.\n"
            "Run without --check, commit the result, and this becomes a ratchet.",
            file=sys.stderr,
        )
        return 1

    baseline = load_baseline()
    regressions, missing = compare(measured, baseline)

    if missing:
        print(f"\n{len(missing)} component(s) absent from this report:\n", file=sys.stderr)
        for name in missing:
            print(f"  {name}", file=sys.stderr)
        print(
            "  -> A component with no instrumented lines is what a build that stopped\n"
            "     compiling it looks like. Absence is not zero coverage; it is no answer.",
            file=sys.stderr,
        )
    if regressions:
        print(f"\n{len(regressions)} component(s) lost coverage:\n", file=sys.stderr)
        for line in regressions:
            print(f"  {line}", file=sys.stderr)
        print(
            f"  -> Each component is held to its own measured value, not a repository-wide\n"
            f"     floor. Add tests, or regenerate the baseline if the drop is intended.",
            file=sys.stderr,
        )

    if missing or regressions:
        return 1

    # Same exact-count comparison as the regression path, so a gain and a drop
    # are judged on the same footing.
    gains = [
        f"{name}: {exact_percent(baseline[name]):.2f}% -> {exact_percent(measured[name]):.2f}%"
        for name in baseline
        if exact_percent(measured[name]) - exact_percent(baseline[name]) > TOLERANCE_POINTS
    ]
    if gains:
        print(f"\n{len(gains)} component(s) improved. Regenerate the baseline to lock this in:")
        for line in gains:
            print(f"  {line}")
    print("\nNo component lost coverage.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
