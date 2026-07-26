#!/usr/bin/env python3
"""CI gate for the Assurance Forge capability matrix. Eight checks:

  1. Row IDs are well-formed, unique, and use a documented area code.
  2. Status values come from the vocabulary the matrix documents.
  3. `supported` and `prototype` rows cite at least one code path -- a shipped
     capability that points at no code is a claim nobody can locate.
  4. `supported` rows cite at least one test path that exists. Same discipline
     as the SACM conformance matrix: the status has to be reproducible.
  5. `planned` / `candidate` / `not-planned` rows cite no tests. This is the rot
     detector that matters most in practice -- the matrix this replaced marked
     dialectic arguments and SACM XMI import/export "planned" long after both
     shipped, and a test citation is the earliest visible sign of that drift.
  6. Every repo path cited anywhere in a row exists on disk.
  7. Every SACM23-* requirement referenced exists in the SACM conformance
     matrix, so the two matrices cannot drift apart silently.
  8. `feature-matrix.json` matches what the markdown would generate.

This page is read by people deciding whether to trust the tool with a safety
argument, so an overstated row is a correctness bug, not a documentation nit.
Reads sources only -- no build required.

Exits non-zero and prints the offending rows when any check fails.

Usage: python tools/features/check_feature_matrix.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from feature_matrix import (  # noqa: E402
    IMPLEMENTED_STATUSES,
    JSON_PATH,
    MATRIX_PATH,
    REPO,
    UNBUILT_STATUSES,
    cited_paths,
    parse,
    referenced_sacm_ids,
    render_json,
    sacm_requirement_ids,
)


def _report(index, ok, description):
    print(f"[{index}] {'OK' if ok else 'FAIL'}: {description}")
    return ok


def check_ids(parsed):
    valid_areas = {area["code"] for area in parsed["areas"]}
    seen = {}
    offenders = []
    for feature in parsed["features"]:
        if feature["area"] not in valid_areas:
            offenders.append((feature, f"undocumented area code {feature['area']!r}"))
        if feature["id"] in seen:
            offenders.append((feature, f"duplicate ID, first seen on line {seen[feature['id']]}"))
        else:
            seen[feature["id"]] = feature["line"]
    for feature, reason in offenders:
        print(f"  line {feature['line']}: {feature['id']} {reason}")
    return _report(1, not offenders, "row IDs are unique and use documented area codes")


def check_status_vocabulary(parsed):
    valid = {status["key"] for status in parsed["statuses"]}
    offenders = [f for f in parsed["features"] if f["status"] not in valid]
    for feature in offenders:
        print(f"  line {feature['line']}: {feature['id']} has unknown status "
              f"{feature['status']!r} (expected one of: {', '.join(sorted(valid))})")
    return _report(2, not offenders, "all status values are from the documented vocabulary")


def check_implemented_rows_cite_code(parsed):
    offenders = [
        f for f in parsed["features"]
        if f["status"] in IMPLEMENTED_STATUSES and not f["evidence"]
    ]
    for feature in offenders:
        print(f"  line {feature['line']}: {feature['id']} is {feature['status']!r} "
              f"but cites no code")
    return _report(3, not offenders, "supported/prototype rows cite code")


def check_supported_rows_cite_tests(parsed):
    offenders = []
    for feature in parsed["features"]:
        if feature["status"] != "supported":
            continue
        if any((REPO / path).exists() for path in feature["tests"]):
            continue
        offenders.append(feature)
    for feature in offenders:
        print(f"  line {feature['line']}: {feature['id']} is 'supported' but cites no "
              f"test that exists (cites: {feature['tests']!r}); add one or lower the status")
    return _report(4, not offenders, "supported rows cite an existing test")


def check_unbuilt_rows_cite_no_tests(parsed):
    offenders = [
        f for f in parsed["features"]
        if f["status"] in UNBUILT_STATUSES and f["tests"]
    ]
    for feature in offenders:
        print(f"  line {feature['line']}: {feature['id']} is {feature['status']!r} but cites "
              f"tests ({feature['tests']!r}); the status is out of date")
    return _report(5, not offenders, "planned/candidate/not-planned rows cite no tests")


def check_cited_paths_exist(parsed):
    offenders = []
    for feature in parsed["features"]:
        cells = [" ".join(feature["evidence"]), " ".join(feature["tests"]), feature["notes"]]
        for path in cited_paths(*cells):
            if not (REPO / path).exists():
                offenders.append((feature, path))
    for feature, path in offenders:
        print(f"  line {feature['line']}: {feature['id']} cites missing path {path}")
    return _report(6, not offenders, "cited repo paths exist on disk")


def check_sacm_cross_references(parsed):
    known = sacm_requirement_ids()
    if not known:
        print("  warning: SACM conformance matrix produced no requirement IDs")
    offenders = []
    for feature in parsed["features"]:
        for requirement_id in sorted(referenced_sacm_ids(feature) - known):
            offenders.append((feature, requirement_id))
    for feature, requirement_id in offenders:
        print(f"  line {feature['line']}: {feature['id']} references {requirement_id}, "
              f"which is not a row in the SACM conformance matrix")
    return _report(7, not offenders, "SACM requirement cross-references resolve")


def check_json_in_sync(parsed):
    expected = render_json(parsed)
    if not JSON_PATH.exists():
        print(f"  {JSON_PATH.relative_to(REPO)} is missing; "
              f"run python tools/features/export_feature_matrix.py")
        return _report(8, False, "feature-matrix.json is in sync with the markdown")
    actual = JSON_PATH.read_text(encoding="utf-8")
    ok = actual == expected
    if not ok:
        print(f"  {JSON_PATH.relative_to(REPO)} is stale; "
              f"run python tools/features/export_feature_matrix.py")
    return _report(8, ok, "feature-matrix.json is in sync with the markdown")


def _synthetic(**overrides):
    row = {
        "id": "AF-STD-001", "area": "STD", "name": "x", "status": "planned",
        "evidence": [], "tests": [], "notes": "", "line": 0,
    }
    row.update(overrides)
    return {
        "statuses": [{"key": s} for s in ("supported", "prototype", "planned")],
        "areas": [{"code": "STD"}],
        "features": [row],
    }


# Each entry is a defect the corresponding check exists to catch. Running these
# on every invocation means CI proves the gate still fails on bad input rather
# than assuming it -- a check that silently stopped rejecting anything would
# otherwise read as a passing build forever.
_SELF_TESTS = (
    (check_ids, _synthetic(area="NOPE"), "undocumented area code"),
    (check_status_vocabulary, _synthetic(status="shipped"), "status outside the vocabulary"),
    (check_implemented_rows_cite_code, _synthetic(status="supported"), "supported row with no code"),
    (check_supported_rows_cite_tests,
     _synthetic(status="supported", evidence=["src"], tests=["tests/does_not_exist.cpp"]),
     "supported row whose test does not exist"),
    (check_unbuilt_rows_cite_no_tests,
     _synthetic(status="planned", tests=["tests/test_layout.cpp"]),
     "planned row that already has tests"),
    (check_cited_paths_exist, _synthetic(evidence=["src/nonexistent_file.cpp"]), "missing cited path"),
    (check_sacm_cross_references, _synthetic(notes="SACM23-BOGUS-999"), "unknown SACM requirement"),
)


def run_self_test():
    """Verify every check rejects the defect it is responsible for."""
    toothless = []
    for check, parsed, description in _SELF_TESTS:
        import io
        import contextlib
        with contextlib.redirect_stdout(io.StringIO()):
            passed = check(parsed)
        if passed:
            toothless.append(f"{check.__name__} accepted a {description}")
    for failure in toothless:
        print(f"  {failure}", file=sys.stderr)
    if toothless:
        print("[0] FAIL: gate self-test -- some checks no longer reject bad input")
    return not toothless


def main():
    if not MATRIX_PATH.exists():
        print(f"error: matrix not found at {MATRIX_PATH}", file=sys.stderr)
        return 1
    if not run_self_test():
        return 1

    parsed = parse()
    if not parsed["features"]:
        # Guards against a silently-passing gate if the table format changes.
        print("error: no capability rows parsed from the matrix", file=sys.stderr)
        return 1
    if not parsed["areas"] or not parsed["statuses"]:
        print("error: the Areas or Status vocabulary table did not parse", file=sys.stderr)
        return 1

    counts = {}
    for feature in parsed["features"]:
        counts[feature["status"]] = counts.get(feature["status"], 0) + 1
    summary = ", ".join(f"{count} {status}" for status, count in sorted(counts.items()))
    print(f"capability rows: {len(parsed['features'])} across {len(parsed['areas'])} areas "
          f"({summary})")

    results = [
        check_ids(parsed),
        check_status_vocabulary(parsed),
        check_implemented_rows_cite_code(parsed),
        check_supported_rows_cite_tests(parsed),
        check_unbuilt_rows_cite_no_tests(parsed),
        check_cited_paths_exist(parsed),
        check_sacm_cross_references(parsed),
        check_json_in_sync(parsed),
    ]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
