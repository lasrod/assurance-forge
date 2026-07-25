#!/usr/bin/env python3
"""CI gate for the SACM 2.3 conformance matrix. Five checks:

  1. Every `verified` row is backed by a test whose name embeds its requirement
     ID. A row that claims verification without an ID-bearing test is a status
     nobody can reproduce.
  2. Every SACM23_* ID used in a test name exists as a matrix row. Catches
     orphan tests left behind when a row is renamed or dropped.
  3. Every fully-qualified repo path cited in a row exists on disk. Catches
     matrix rot when library files move.
  4. Status values come from the vocabulary the matrix documents.
  5. No `verified` row rests on manual evidence.

The matrix is the canonical source of requirement IDs (test names embed them),
so drift between it and the tests silently degrades every compliance claim
built on it. This check reads sources only -- no build required -- so it runs
identically in CI, in a fresh clone, and locally.

Exits non-zero and prints the offending rows when any check fails.

Usage: python tools/sacm/check_conformance_matrix.py
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MATRIX_PATH = REPO / "docs/sacm/sacm-conformance-matrix.md"

# Both test trees are scanned. `libs/sacm/tests` holds the library's own
# conformance tests; `tests` holds Assurance Forge's, which is where the
# evidence for the *integration* rows (SACM23-INT-*, and the app half of
# SACM23-LIB-002) necessarily lives -- those rows are about the application
# consuming the library, so they cannot be proven from inside the library.
# Scanning only the library tree made check 1 unsatisfiable for every
# integration row: the row could never reach `verified` no matter how well it
# was tested, because the gate could not see its tests.
TESTS_DIRS = (REPO / "libs/sacm/tests", REPO / "tests")

# Status vocabulary, per the matrix preamble.
VALID_STATUSES = {
    "not-started", "analyzed", "tests-failing",
    "implemented", "verified", "deferred", "out-of-scope",
}

# Rows may be verified by machinery that is not a gtest case. Each entry must
# name a real, CI-executed mechanism -- keep this list short and reviewable,
# because every entry is a hole in check 1.
#   SacmCliVersionSmoke  -> add_test() in libs/sacm/CMakeLists.txt
#   configure-time gate  -> cmake/check_layer_gates.cmake, also an ALL target
NON_GTEST_EVIDENCE = ("SacmCliVersionSmoke", "configure-time gate")

# Statuses that do not need test evidence yet.
UNPROVEN_STATUSES = {"not-started", "analyzed", "tests-failing", "deferred", "out-of-scope"}

_ID_RE = re.compile(r"SACM23[-_][A-Z]+[-_][0-9]+")
_TEST_RE = re.compile(r"^\s*TEST(?:_F)?\(\s*[A-Za-z0-9_]+\s*,\s*([A-Za-z0-9_]+)\s*\)", re.M)
# A path we can resolve. Bare filenames are ambiguous shorthand (rows write
# `src/io/xmi_reader.cpp, xmi_writer.cpp`) so only prefixed paths are checked.
_PATH_RE = re.compile(r"\b(?:libs|cmake|src|docs|tools)/[A-Za-z0-9_./-]+")

# Rows abbreviate library paths once the row is already rooted in the library,
# writing `libs/sacm/include/sacm/commands/*, src/commands/*`. Resolve a cited
# path against the repo root first, then against the library root.
PATH_ROOTS = (REPO, REPO / "libs/sacm")


def normalize(requirement_id):
    """SACM23-CMD-001 and SACM23_CMD_001 name the same requirement."""
    return requirement_id.replace("-", "_")


def parse_matrix():
    """Return [{id, status, files, tests, line}] for each data row."""
    rows = []
    for lineno, line in enumerate(MATRIX_PATH.read_text(encoding="utf-8").splitlines(), 1):
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 9 or not cells[0].startswith("SACM23"):
            continue  # header, separator, or a non-matrix table
        rows.append({
            "id": cells[0],
            "status": cells[5],
            "files": cells[6],
            "tests": cells[7],
            "line": lineno,
        })
    return rows


def collect_test_ids():
    """Requirement IDs embedded in gtest names, mapped to the names using them."""
    by_id = {}
    for tests_dir in TESTS_DIRS:
        if not tests_dir.is_dir():
            continue
        for path in sorted(tests_dir.glob("*.cpp")):
            for test_name in _TEST_RE.findall(path.read_text(encoding="utf-8")):
                for found in _ID_RE.findall(test_name):
                    by_id.setdefault(normalize(found), []).append(f"{path.name}:{test_name}")
    return by_id


def check_verified_rows_have_tests(rows, test_ids):
    offenders = []
    for row in rows:
        if row["status"] != "verified":
            continue
        if normalize(row["id"]) in test_ids:
            continue
        if any(token in row["tests"] for token in NON_GTEST_EVIDENCE):
            continue
        offenders.append(row)
    for row in offenders:
        print(f"  line {row['line']}: {row['id']} is 'verified' but no test name "
              f"embeds that ID (cites: {row['tests']!r})")
    ok = not offenders
    print(f"[1] {'OK' if ok else 'FAIL'}: every verified row has an ID-bearing test")
    return ok


def check_no_orphan_tests(rows, test_ids):
    known = {normalize(row["id"]) for row in rows}
    offenders = sorted(set(test_ids) - known)
    for requirement_id in offenders:
        print(f"  test references unknown requirement {requirement_id}: "
              f"{', '.join(test_ids[requirement_id])}")
    ok = not offenders
    print(f"[2] {'OK' if ok else 'FAIL'}: every test's requirement ID exists in the matrix")
    return ok


def check_cited_paths_exist(rows):
    offenders = []
    for row in rows:
        for candidate in _PATH_RE.findall(row["files"]):
            if any((root / candidate).exists() for root in PATH_ROOTS):
                continue
            offenders.append((row, candidate))
    for row, candidate in offenders:
        print(f"  line {row['line']}: {row['id']} cites missing path {candidate}")
    ok = not offenders
    print(f"[3] {'OK' if ok else 'FAIL'}: cited library paths exist on disk")
    return ok


def check_status_vocabulary(rows):
    offenders = [r for r in rows if r["status"] not in VALID_STATUSES]
    for row in offenders:
        print(f"  line {row['line']}: {row['id']} has unknown status {row['status']!r} "
              f"(expected one of: {', '.join(sorted(VALID_STATUSES))})")
    ok = not offenders
    print(f"[4] {'OK' if ok else 'FAIL'}: all status values are from the documented vocabulary")
    return ok


def check_no_manual_verification(rows):
    offenders = [r for r in rows if r["status"] == "verified" and "manual" in r["tests"].lower()]
    for row in offenders:
        print(f"  line {row['line']}: {row['id']} is 'verified' but cites manual evidence "
              f"({row['tests']!r}); automate it or lower the status")
    ok = not offenders
    print(f"[5] {'OK' if ok else 'FAIL'}: no verified row rests on manual evidence")
    return ok


def main():
    if not MATRIX_PATH.exists():
        print(f"error: matrix not found at {MATRIX_PATH}", file=sys.stderr)
        return 1
    # Every configured tree must exist. A missing one would silently shrink the
    # evidence the gate can see, turning check 1 into a weaker test rather than
    # a failing one.
    for tests_dir in TESTS_DIRS:
        if not tests_dir.is_dir():
            print(f"error: test directory not found at {tests_dir}", file=sys.stderr)
            return 1

    rows = parse_matrix()
    if not rows:
        # Guards against a silently-passing gate if the table format changes.
        print("error: no requirement rows parsed from the matrix", file=sys.stderr)
        return 1
    test_ids = collect_test_ids()
    verified = sum(1 for r in rows if r["status"] == "verified")
    print(f"matrix rows: {len(rows)} ({verified} verified)  |  "
          f"requirement IDs found in tests: {len(test_ids)}")

    results = [
        check_verified_rows_have_tests(rows, test_ids),
        check_no_orphan_tests(rows, test_ids),
        check_cited_paths_exist(rows),
        check_status_vocabulary(rows),
        check_no_manual_verification(rows),
    ]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
