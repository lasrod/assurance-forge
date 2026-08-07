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


def analyze_one(exe: str, flags: list[str], source: str, checks: str | None = None) -> list[dict]:
    command = [exe, "--quiet"]
    if checks:
        command.append(f"--checks={checks}")
    proc = subprocess.run(
        command + [str(REPO / source), "--"] + flags,
        cwd=REPO,
        capture_output=True,
        text=True,
    )
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
    return findings


def analyze(
    exe: str, flags: list[str], sources: list[str], jobs: int, checks: str | None = None
) -> list[dict]:
    collected: list[dict] = []
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {pool.submit(analyze_one, exe, flags, s, checks): s for s in sources}
        for future in concurrent.futures.as_completed(futures):
            collected.extend(future.result())
            done += 1
            if done % 25 == 0 or done == len(sources):
                print(f"  analyzed {done}/{len(sources)} translation units", flush=True)

    # A finding in a header is reported by every translation unit that includes
    # it. Deduplicate so the count describes the code, not the include graph.
    unique = {(f["file"], f["line"], f["check"], f["message"]): f for f in collected}
    return sorted(unique.values(), key=lambda f: (f["file"], f["line"], f["check"]))


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

    exe = find_clang_tidy()
    version = tool_version(exe)
    flags = build_compiler_flags()
    sources = list_sources()
    if args.filter:
        sources = [s for s in sources if args.filter in s]
    if not sources:
        fail("no sources matched: refusing to report an empty run as clean")

    # The four families .clang-tidy enables, with none of its exclusions.
    all_checks = "-*,bugprone-*,clang-analyzer-*,performance-*,misc-*" if args.all_checks else None

    print(f"clang-tidy {version} over {len(sources)} translation units, {args.jobs} at a time")
    findings = analyze(exe, flags, sources, args.jobs, all_checks)
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
        # baseline recorded. Say so loudly: an unexplained regression list is
        # how a gate gets dismissed as flaky and then switched off.
        if baseline.get("tool_version") != version:
            print(
                f"\nwarning: baseline was generated by clang-tidy {baseline.get('tool_version')}, "
                f"this is {version}.\n"
                "         Differences below may be version differences rather than new code.",
                file=sys.stderr,
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
        if args.filter:
            print(f"\nNo new findings in paths matching '{args.filter}'.")
            print("Partial run: the full sweep is what gates a merge.")
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
