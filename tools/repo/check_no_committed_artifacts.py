#!/usr/bin/env python3
"""Fail if machine-produced or single-use files are committed to the repository.

Captured build logs, test output, CTest working directories, IDE state and
scratch files for composing issues all share a property: they are specific to one
machine at one moment, they are never read by the build or the tests, and a stale
one actively misleads. The repository carried a committed CTest log claiming 383
tests long after the suite reached 1207.

`.gitignore` stops these appearing accidentally, but it does nothing about a file
already tracked -- git keeps honouring the index -- and it is silent when someone
adds one with `git add -f`. This gate closes both gaps by checking what is
actually tracked.

Scope note: this checks the whole tree, not just the repository root. The first
version of the quality baseline scanned only root-level paths and therefore
missed `Testing/Temporary/LastTest.log`, which is exactly the class of file the
check exists to catch.

Usage:
    python tools/repo/check_no_committed_artifacts.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# (compiled pattern, why this class of file does not belong in git)
FORBIDDEN = [
    (re.compile(r"^Testing/"), "CTest working directory, recreated by every local test run"),
    (re.compile(r"(^|/)LastTest\.log$"), "CTest log from one local run"),
    (re.compile(r"(^|/)build[-_]?out(put)?\.txt$"), "captured build output"),
    (re.compile(r"(^|/)[a-z0-9_-]*_?tests?\.txt$", re.I), "captured test output"),
    (re.compile(r"(^|/)[a-z0-9_-]*build[a-z0-9_-]*\.log$", re.I), "captured build log"),
    # Machine-written report formats. Matched by name rather than by extension so
    # that genuine XML content -- SACM fixtures, the normative model -- is never
    # caught. `.gitignore` names test-results.xml; the two lists are maintained by
    # hand and had already drifted apart by the time this was noticed.
    (
        re.compile(r"(^|/)(test-results|test_results|junit|coverage|cobertura|gcovr)[a-z0-9._-]*\.xml$", re.I),
        "captured test or coverage report",
    ),
    (re.compile(r"(^|/)(issue-body|pr-body|commit-msg)\.(md|txt)$"), "scratch file for composing an issue, PR or commit"),
    (re.compile(r"(^|/)compile_commands\.json$"), "generated compilation database"),
    (re.compile(r"(^|/)cmake_test_discovery_.*\.json$"), "generated CMake test-discovery output"),
    (re.compile(r"(^|/)imgui\.ini$"), "ImGui window state from one local session"),
    (re.compile(r"(^|/)CMakeCache\.txt$"), "CMake cache bound to one local build tree"),
    (re.compile(r"\.(obj|o|a|lib|dll|exe|pdb|pyc)$"), "compiled binary artifact"),
    (re.compile(r"(^|/)(coverage_full|coverage_core|coverage_logic)/"), "generated coverage report"),
    (re.compile(r"(^|/)\.DS_Store$"), "macOS directory metadata"),
]

# Paths that match a rule above but are legitimately tracked. Each entry needs a
# reason: an allow-list without one becomes a place to hide things.
ALLOWED = {
    # e.g. "tests/data/sample_tests.txt": "fixture consumed by the parser tests",
}


# Names the patterns must catch, and names they must not. A gate is only worth
# the confidence placed in it if it has been shown to fail on the right inputs,
# and an over-broad pattern that swallows a real fixture is as bad as a gap.
# Checked on every run: it costs microseconds and turns a one-off manual check
# into something that keeps holding.
MUST_CATCH = [
    "build_out.txt",
    "build_output.txt",
    "full_tests.txt",
    "Testing/Temporary/LastTest.log",
    "issue-body.md",
    "pr-body.md",
    "test-results.xml",
    "junit.xml",
    "coverage.xml",
    "nested/dir/build.log",
    "compile_commands.json",
    "cmake_test_discovery_abc123.json",
    "imgui.ini",
    "CMakeCache.txt",
    "build/foo.obj",
    "coverage_full/index.html",
    ".DS_Store",
]

MUST_NOT_CATCH = [
    "data/sample.sacm.xml",
    "tests/data/argument_with_evidence.xml",
    "third_party/SACM-2.3-ptc-22-03-13.xml",
    "docs/features/feature-matrix.json",
    "docs/quality/repository-baseline.json",
    "assets/locale/ja/LC_MESSAGES/assurance_forge.mo",
    "assets/locale/ja/LC_MESSAGES/assurance_forge.po",
    "src/app/app_runtime.cpp",
    "libs/sacm/include/sacm/model/document.h",
    "CMakeLists.txt",
    "CMakePresets.json",
    "tools/i18n/check_catalog.py",
    "README.md",
    "docs/RELEASING.md",
]


def first_match(path):
    for pattern, reason in FORBIDDEN:
        if pattern.search(path):
            return reason
    return None


def self_test():
    """Return a list of pattern regressions, empty when the patterns behave."""
    failures = []
    for path in MUST_CATCH:
        if first_match(path) is None:
            failures.append(f"should be caught but is not: {path}")
    for path in MUST_NOT_CATCH:
        reason = first_match(path)
        if reason is not None:
            failures.append(f"must not be caught but matched '{reason}': {path}")
    return failures


def tracked_files():
    result = subprocess.run(
        ["git", "ls-files"], cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace"
    )
    if result.returncode != 0:
        print("error: could not list tracked files (is this a git checkout?)", file=sys.stderr)
        raise SystemExit(2)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main():
    regressions = self_test()
    if regressions:
        print(f"[0] FAIL: the artifact patterns no longer behave as intended\n")
        for line in regressions:
            print(f"  {line}")
        print("\nFix FORBIDDEN, or update MUST_CATCH / MUST_NOT_CATCH if the intent changed.")
        return 2
    print(f"[0] OK: patterns catch {len(MUST_CATCH)} known artifacts and spare {len(MUST_NOT_CATCH)} real files")

    offenders = []
    for path in tracked_files():
        if path in ALLOWED:
            continue
        reason = first_match(path)
        if reason is not None:
            offenders.append((path, reason))

    if offenders:
        print(f"[1] FAIL: {len(offenders)} committed artifact(s) found\n")
        for path, reason in offenders:
            print(f"  {path}\n      {reason}")
        print(
            "\nRemove each with:\n"
            "    git rm --cached <path>\n"
            "and confirm .gitignore covers it. If a file genuinely belongs in the\n"
            "repository, add it to ALLOWED in this script with the reason why."
        )
        return 1

    print(f"tracked files: {len(tracked_files())}")
    print("[1] OK: no committed build logs, test output, generated files or scratch files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
