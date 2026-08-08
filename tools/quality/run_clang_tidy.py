#!/usr/bin/env python3
"""Run clang-tidy over the first-party sources and ratchet the result (issue #293).

Writes a machine-readable snapshot to `docs/quality/clang-tidy-baseline.json`
and, with --check, fails when a finding appears that the baseline does not
account for.

Design rules, because a static-analysis gate that cries wolf gets deleted:

- The check selection lives in `.clang-tidy`, not here, so an editor or IDE
  pointed at this repository reports exactly what CI reports.
- Third-party code is passed with -isystem, never -I. Without that, the report
  is dominated by nlohmann/json and hello_imgui and the project's own findings
  are invisible.
- The ratchet keys on (file, check) counts, not line numbers. Line numbers move
  whenever anything above them is edited, so a line-keyed baseline would fail
  on unrelated changes and teach people to regenerate it without reading it.
- A missing prerequisite is reported, never silently skipped. A run that
  analyzed nothing must not look like a run that found nothing.
- The tool version must match the one the baseline records, and --check refuses
  to compare across a mismatch. Different versions implement different checks:
  a comparison across two of them reports findings nobody wrote and passes over
  findings somebody did. Both happened (#317) while this was only a warning.

Usage:
    python tools/quality/run_clang_tidy.py            # analyze, write JSON, print summary
    python tools/quality/run_clang_tidy.py --check    # non-zero exit if findings grew
    python tools/quality/run_clang_tidy.py --report   # print every finding
    python tools/quality/run_clang_tidy.py --jobs 8   # override parallelism
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
JSON_PATH = REPO / "docs/quality/clang-tidy-baseline.json"

# First-party include roots, quoted with -I so their headers are analyzed.
FIRST_PARTY_INCLUDES = ["src", "libs/sacm/include", "libs/sacm/src"]

# Third-party include roots, quoted with -isystem so their headers are not.
# These mirror the compiler sweep documented in docs/quality/code-quality-policy.md.
THIRD_PARTY_INCLUDES = [
    "external/hello_imgui/src",
    "external/hello_imgui/external/imgui",
    "external/hello_imgui/external/imgui/backends",
    "external/hello_imgui/external/imgui/misc/cpp",
    "external/pugixml/src",
    "external/picosha2",
    "external/nativefiledialog-extended/src/include",
]

# FetchContent dependencies land here once the project has been configured.
DEPS_GLOBS = ["build/_deps/*-src/include", "build/_deps/*-src/single_include"]

# Analyze production code only. Tests are excluded from the first baseline
# deliberately: they are a different risk profile and would triple the runtime
# for findings nobody ships. Revisit under #292.
EXCLUDED_PREFIXES = ("external/", "tests/", "libs/sacm/tests/")

FINDING_RE = re.compile(
    r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?:warning|error): (?P<message>.*?) \[(?P<check>[a-zA-Z0-9.,\-]+)\]$"
)


def fail(message: str) -> "None":
    sys.stderr.write(f"error: {message}\n")
    raise SystemExit(2)


def find_clang_tidy() -> str:
    exe = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
    if not exe:
        fail(
            "clang-tidy not found on PATH. Install LLVM, or set CLANG_TIDY to its path.\n"
            "  Windows: winget install LLVM.LLVM\n"
            "  Ubuntu:  sudo apt install clang-tidy"
        )
    return exe


def tool_version(exe: str) -> str:
    out = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout
    match = re.search(r"version\s+(\S+)", out)
    return match.group(1) if match else "unknown"


def resolve_dependency_includes() -> list[str]:
    """Locate FetchContent include roots, which exist only after a configure."""
    found: list[str] = []
    for pattern in DEPS_GLOBS:
        parent, _, leaf = pattern.rpartition("/")
        base = REPO / parent.replace("*-src", "").rstrip("/")
        if not base.exists():
            continue
        for entry in sorted(base.glob("*-src")):
            candidate = entry / leaf
            if candidate.is_dir():
                found.append(str(candidate.relative_to(REPO)).replace("\\", "/"))
    return found


def build_compiler_flags() -> list[str]:
    deps = resolve_dependency_includes()
    if not deps:
        fail(
            "no FetchContent include directories under build/_deps.\n"
            "Configure the project first so the dependencies are present:\n"
            "  cmake --preset default"
        )
    flags = ["-std=c++23", "-D_CRT_SECURE_NO_WARNINGS"]
    for path in FIRST_PARTY_INCLUDES:
        flags += ["-I", str(REPO / path)]
    for path in THIRD_PARTY_INCLUDES + deps:
        flags += ["-isystem", str(REPO / path)]
    return flags


def list_sources() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "*.cpp"], cwd=REPO, capture_output=True, text=True, check=True
    ).stdout
    sources = [line.strip() for line in out.splitlines() if line.strip()]
    return [s for s in sources if not s.startswith(EXCLUDED_PREFIXES)]


def normalize(path: str) -> str | None:
    """Repo-relative and forward-slashed, or None when the path is outside the repo.

    Returning None matters: clang-tidy reports findings in toolchain headers that
    -isystem does not cover (MSVC's own <filesystem> implementation contributed
    three). Those describe someone else's code, differ per machine and per
    compiler version, and would pin the baseline to whoever generated it.
    """
    candidate = Path(path)
    try:
        candidate = (candidate if candidate.is_absolute() else REPO / candidate).resolve()
        return str(candidate.relative_to(REPO)).replace("\\", "/")
    except (ValueError, OSError):
        return None


def analyze_one(
    exe: str, flags: list[str], source: str, checks: str | None = None
) -> tuple[list[dict], str | None]:
    """Return (findings, failure). A non-None failure means this TU was not analyzed.

    clang-tidy reports a translation unit it could not compile on stderr and
    exits non-zero, while stdout stays empty. Parsing stdout alone would read
    that as "no findings here" and let the ratchet pass on code nothing looked
    at, which is the one way a green gate can be actively misleading.
    """
    command = [exe, "--quiet"]
    if checks:
        command.append(f"--checks={checks}")
    proc = subprocess.run(
        command + [str(REPO / source), "--"] + flags,
        cwd=REPO,
        capture_output=True,
        text=True,
    )

    failure = None
    if proc.returncode != 0:
        # Prefer the diagnostic that says what went wrong ("'x.h' file not
        # found") over clang-tidy's trailing "Error while processing <file>",
        # which only repeats the name already being reported. The cause can
        # land on either stream depending on the diagnostic.
        cause = next(
            (
                line.strip()
                for line in proc.stderr.splitlines() + proc.stdout.splitlines()
                if "error:" in line
            ),
            None,
        )
        if cause is None:
            remainder = proc.stderr.strip().splitlines()
            cause = remainder[0].strip() if remainder else "no diagnostic on either stream"
        failure = f"{source}\n      exit {proc.returncode}: {cause}"

    findings = []
    for line in proc.stdout.splitlines():
        match = FINDING_RE.match(line.strip())
        if not match:
            continue
        location = normalize(match.group("file"))
        if location is None:
            continue  # toolchain or system header, not this repository's code
        for check in match.group("check").split(","):
            findings.append(
                {
                    "file": location,
                    "line": int(match.group("line")),
                    "check": check,
                    "message": match.group("message"),
                }
            )
    return findings, failure


def analyze(
    exe: str, flags: list[str], sources: list[str], jobs: int, checks: str | None = None
) -> tuple[list[dict], list[str]]:
    collected: list[dict] = []
    failures: list[str] = []
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(analyze_one, exe, flags, s, checks): s for s in sources}
        for future in concurrent.futures.as_completed(futures):
            findings, failure = future.result()
            collected.extend(findings)
            if failure:
                failures.append(failure)
            done += 1
            if done % 25 == 0 or done == len(sources):
                print(f"  analyzed {done}/{len(sources)} translation units", flush=True)

    # A finding in a header is reported by every translation unit that includes
    # it. Deduplicate so the count describes the code, not the include graph.
    unique = {(f["file"], f["line"], f["check"], f["message"]): f for f in collected}
    return sorted(unique.values(), key=lambda f: (f["file"], f["line"], f["check"])), sorted(failures)


def tally(findings: list[dict]) -> dict[str, int]:
    """Ratchet key: (file, check). Deliberately excludes the line number."""
    counter: Counter[str] = Counter()
    for finding in findings:
        counter[f"{finding['file']}|{finding['check']}"] += 1
    return dict(sorted(counter.items()))


def snapshot(findings: list[dict], version: str, sources: list[str]) -> dict:
    return {
        "tool": "clang-tidy",
        "tool_version": version,
        "platform": platform.system(),
        "config": ".clang-tidy",
        "translation_units": len(sources),
        "total_findings": len(findings),
        "by_check": dict(Counter(f["check"] for f in findings).most_common()),
        "counts": tally(findings),
    }


def compare(current: dict, baseline: dict) -> list[str]:
    regressions = []
    base_counts = baseline.get("counts", {})
    for key, count in current["counts"].items():
        was = base_counts.get(key, 0)
        if count > was:
            path, check = key.split("|", 1)
            regressions.append(f"{path}: {check} went from {was} to {count}")
    return sorted(regressions)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if findings grew")
    parser.add_argument("--report", action="store_true", help="print every finding")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    parser.add_argument(
        "--filter",
        metavar="SUBSTRING",
        help="analyze only paths containing SUBSTRING. For iterating on one file; "
        "narrows coverage, so it must not be used to produce a baseline or gate a merge.",
    )
    parser.add_argument(
        "--paths",
        nargs="+",
        metavar="FILE",
        help="analyze only these files. Used by the pull-request job to check what a "
        "branch touched; the full sweep on main is what covers everything. Like --filter, "
        "it narrows coverage and cannot write a baseline.",
    )
    parser.add_argument(
        "--ignore-tool-version",
        action="store_true",
        help="compare against a baseline generated by a different clang-tidy. For measuring "
        "what a version change would cost; the result is not a basis for a merge, which is why "
        "--check refuses the mismatch by default.",
    )
    parser.add_argument(
        "--all-checks",
        action="store_true",
        help="ignore the exclusions in .clang-tidy and enable every check in the four "
        "enabled families. Measures what the exclusions cost, so the rationale recorded "
        "in .clang-tidy can be re-derived rather than taken on trust. Never writes a baseline.",
    )
    args = parser.parse_args()

    if args.all_checks and not args.report:
        print("note: --all-checks measures the excluded checks; it will not write a baseline\n")

    if args.filter and not args.check:
        fail("--filter narrows coverage; combine it with --check, never use it to write a baseline")
    if args.paths and not args.check:
        fail("--paths narrows coverage; combine it with --check, never use it to write a baseline")

    exe = find_clang_tidy()
    version = tool_version(exe)
    flags = build_compiler_flags()
    sources = list_sources()
    if args.filter:
        sources = [s for s in sources if args.filter in s]
    if args.paths:
        requested = {p.replace("\\", "/").lstrip("./") for p in args.paths}
        sources = [s for s in sources if s in requested]
        if not sources:
            # Not a failure: a branch touching only docs or tests analyzes
            # nothing, and that is a correct outcome rather than a broken run.
            print("no analyzable production sources among the given paths; nothing to check")
            return 0
    if not sources:
        fail("no sources matched: refusing to report an empty run as clean")

    # The four families .clang-tidy enables, with none of its exclusions.
    all_checks = "-*,bugprone-*,clang-analyzer-*,performance-*,misc-*" if args.all_checks else None

    print(f"clang-tidy {version} over {len(sources)} translation units, {args.jobs} at a time")
    findings, failures = analyze(exe, flags, sources, args.jobs, all_checks)

    # Before anything else. A translation unit clang-tidy could not compile
    # contributes no findings, which is indistinguishable from a clean one
    # unless it is said out loud. Refusing to continue is the point: a baseline
    # written from a partly-failed run would record unanalyzed code as clean,
    # and every later run would compare against that.
    if failures:
        print(f"\n{len(failures)} translation unit(s) could not be analyzed:\n", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        print(
            "\nThese contribute no findings, so continuing would report unanalyzed code\n"
            "as clean. Fix the analysis (usually a missing include path or define) and\n"
            "run again.",
            file=sys.stderr,
        )
        return 2

    current = snapshot(findings, version, sources)

    print(f"\n{current['total_findings']} findings")
    for check, count in current["by_check"].items():
        print(f"  {count:5d}  {check}")

    if args.report:
        print()
        for finding in findings:
            print(f"{finding['file']}:{finding['line']}: {finding['message']} [{finding['check']}]")

    if args.check:
        # Flush first: the failure list goes to stderr, and an unflushed stdout
        # would print the summary after it, making the report read backwards.
        sys.stdout.flush()
        if not JSON_PATH.exists():
            fail(f"no baseline at {JSON_PATH.relative_to(REPO)}. Run without --check to create one.")
        baseline = json.loads(JSON_PATH.read_text(encoding="utf-8"))

        # A different clang-tidy runs different checks and finds different
        # things, so a mismatch means the comparison below is not the one the
        # baseline recorded -- in both directions. This was a warning until
        # #317, and both failure modes happened under it: a pull request failed
        # on `misc-redundant-expression` that the baseline's version does not
        # report at all, and a `misc-use-internal-linkage` finding reached main
        # because the version CI ran does not implement that check the same way.
        #
        # The second is the reason this is now fatal rather than louder. A
        # phantom failure gets diagnosed because somebody is blocked by it. A
        # phantom pass is indistinguishable from a clean run, and the next
        # baseline regeneration adopts whatever it let through.
        baseline_version = baseline.get("tool_version")
        if baseline_version != version and not args.ignore_tool_version:
            fail(
                f"clang-tidy version mismatch: the baseline was generated by {baseline_version}, "
                f"this is {version}.\n"
                "Comparing across versions reports findings nobody introduced and hides findings\n"
                "somebody did, so this is refused rather than warned about.\n\n"
                f"  pip install clang-tidy=={baseline_version}\n\n"
                "Or pass --ignore-tool-version to compare anyway, which is useful for measuring\n"
                "what a version change would cost and is never a basis for a merge."
            )
        if baseline.get("platform") != current["platform"]:
            print(
                f"\nwarning: baseline platform is {baseline.get('platform')}, this is "
                f"{current['platform']}.\n"
                "         Platform-specific code compiles differently; see the known gaps in\n"
                "         docs/quality/static-analysis.md.",
                file=sys.stderr,
            )

        regressions = compare(current, baseline)
        if regressions:
            print(f"\n{len(regressions)} new static-analysis finding(s):\n", file=sys.stderr)
            for line in regressions:
                print(f"  {line}", file=sys.stderr)
            print(
                "\nFix them, or (if the finding is wrong for this codebase) disable the check\n"
                "in .clang-tidy with a rationale and regenerate the baseline.",
                file=sys.stderr,
            )
            return 1
        if args.filter or args.paths:
            print(f"\nNo new findings in the {len(sources)} source(s) analyzed.")
            print("Partial run: the full sweep on main is what covers everything.")
            return 0
        removed = current["total_findings"] - baseline.get("total_findings", 0)
        if removed < 0:
            print(f"\n{-removed} fewer than baseline. Regenerate it to lock the improvement in.")
        print("\nNo new findings.")
        return 0

    if args.all_checks:
        print("\nmeasurement run (--all-checks): baseline not written")
        return 0

    JSON_PATH.write_text(json.dumps(current, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    print(f"\nwrote {JSON_PATH.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
