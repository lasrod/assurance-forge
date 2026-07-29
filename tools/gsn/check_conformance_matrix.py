#!/usr/bin/env python3
"""CI gate for the GSN v3 conformance matrix.

Checks that:

1. Requirement IDs are unique and live in the correct normative section.
2. Every operation status comes from the matrix vocabulary.
3. Every `supported` row cites an existing automated test.
4. Every repository path cited by a row exists.
5. Every `GSN3-*` ID referenced by the product feature matrix exists here.

The GSN matrix separates model/import, create/edit, render, validate, and
interchange support. Keeping those columns machine-checked prevents a
render-only feature from drifting into an end-to-end conformance claim.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MATRIX_PATH = REPO / "docs/gsn/gsn-v3-conformance-matrix.md"
FEATURE_MATRIX_PATH = REPO / "docs/features/feature-matrix.md"

VALID_STATUSES = {"supported", "partial", "preserved", "absent", "blocked", "n/a"}
SECTION_AREAS = {
    "Core GSN": {"CORE"},
    "Argument Pattern Extension": {"PAT"},
    "Modular Extension": {"MOD"},
    "Confidence Argument Extension": {"ACP"},
    "Dialectic Extension": {"DIA"},
    "Cross-cutting interchange and conformance": {"XMI", "VAL"},
}

_ID_RE = re.compile(r"GSN3-([A-Z]+)-[0-9]{3}")
_PATH_RE = re.compile(r"\b(?:libs|cmake|src|docs|tools|tests)/[A-Za-z0-9_./-]+")
_TEST_PATH_RE = re.compile(r"\b(?:libs/sacm/tests|tests)/[A-Za-z0-9_./-]+")


def cells(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def strip_trailing_punctuation(path):
    return path.rstrip(".,;:)")


def parse_rows():
    rows = []
    section = ""
    for lineno, line in enumerate(MATRIX_PATH.read_text(encoding="utf-8-sig").splitlines(), 1):
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        if not line.startswith("| GSN3-"):
            continue
        fields = cells(line)
        if len(fields) != 9:
            rows.append({
                "id": fields[0] if fields else "(unparsed)",
                "line": lineno,
                "section": section,
                "malformed": True,
            })
            continue
        rows.append({
            "id": fields[0],
            "source": fields[1],
            "requirement": fields[2],
            "statuses": fields[3:8],
            "evidence": fields[8],
            "line": lineno,
            "section": section,
            "malformed": False,
        })
    return rows


def check_ids_and_sections(rows):
    seen = {}
    offenders = []
    for row in rows:
        if row["malformed"]:
            offenders.append((row, "row does not have exactly nine cells"))
            continue
        match = _ID_RE.fullmatch(row["id"])
        if not match:
            offenders.append((row, "malformed requirement ID"))
            continue
        if row["id"] in seen:
            offenders.append((row, f"duplicate ID first seen on line {seen[row['id']]}"))
        else:
            seen[row["id"]] = row["line"]
        allowed = SECTION_AREAS.get(row["section"])
        if allowed is None:
            offenders.append((row, f"row is under unknown section {row['section']!r}"))
        elif match.group(1) not in allowed:
            offenders.append(
                (row, f"area {match.group(1)!r} does not belong under {row['section']!r}")
            )
    for row, reason in offenders:
        print(f"  line {row['line']}: {row['id']} {reason}")
    ok = not offenders
    print(f"[1] {'OK' if ok else 'FAIL'}: IDs are unique and use the section taxonomy")
    return ok


def check_statuses(rows):
    offenders = []
    for row in rows:
        if row["malformed"]:
            continue
        for status in row["statuses"]:
            if status not in VALID_STATUSES:
                offenders.append((row, status))
    for row, status in offenders:
        print(f"  line {row['line']}: {row['id']} has unknown operation status {status!r}")
    ok = not offenders
    print(f"[2] {'OK' if ok else 'FAIL'}: operation statuses use the documented vocabulary")
    return ok


def check_supported_rows_cite_tests(rows):
    offenders = []
    for row in rows:
        if row["malformed"] or "supported" not in row["statuses"]:
            continue
        test_paths = [
            strip_trailing_punctuation(path)
            for path in _TEST_PATH_RE.findall(row["evidence"])
        ]
        if test_paths and any((REPO / path).exists() for path in test_paths):
            continue
        offenders.append(row)
    for row in offenders:
        print(f"  line {row['line']}: {row['id']} claims supported behavior "
              "without citing an existing automated test")
    ok = not offenders
    print(f"[3] {'OK' if ok else 'FAIL'}: supported rows cite automated tests")
    return ok


def check_cited_paths(rows):
    offenders = []
    for row in rows:
        if row["malformed"]:
            continue
        for raw in _PATH_RE.findall(row["evidence"]):
            path = strip_trailing_punctuation(raw)
            if not (REPO / path).exists():
                offenders.append((row, path))
    for row, path in offenders:
        print(f"  line {row['line']}: {row['id']} cites missing path {path}")
    ok = not offenders
    print(f"[4] {'OK' if ok else 'FAIL'}: cited repository paths exist")
    return ok


def check_feature_cross_references(rows):
    known = {row["id"] for row in rows if not row["malformed"]}
    referenced = set()
    for match in _ID_RE.finditer(FEATURE_MATRIX_PATH.read_text(encoding="utf-8-sig")):
        referenced.add(match.group(0))
    offenders = sorted(referenced - known)
    for requirement_id in offenders:
        print(f"  feature matrix references unknown GSN requirement {requirement_id}")
    ok = not offenders
    print(f"[5] {'OK' if ok else 'FAIL'}: feature-matrix GSN references resolve")
    return ok


def main():
    if not MATRIX_PATH.exists():
        print(f"error: matrix not found at {MATRIX_PATH}", file=sys.stderr)
        return 1
    if not FEATURE_MATRIX_PATH.exists():
        print(f"error: feature matrix not found at {FEATURE_MATRIX_PATH}", file=sys.stderr)
        return 1

    rows = parse_rows()
    if not rows:
        print("error: no GSN requirement rows parsed", file=sys.stderr)
        return 1

    print(f"GSN conformance rows: {len(rows)}")
    results = [
        check_ids_and_sections(rows),
        check_statuses(rows),
        check_supported_rows_cite_tests(rows),
        check_cited_paths(rows),
        check_feature_cross_references(rows),
    ]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
