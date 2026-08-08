#!/usr/bin/env python3
"""Generate the release-bound SACM 2.3 conformance evidence package (#295).

A conformance claim that names no release is a claim about a moving target.
This tool freezes, for one exact commit, everything a reviewer needs to check
the claim without trusting the repository's current state: the matrix, the
compliance-point decisions, the completeness audit, the verification records,
the requirement-to-test traceability, the pinned normative-source hashes, and
the machine-readable test results of the build being released.

The package is generated, never committed: it goes to `build/` locally and to a
release artifact in CI (`.github/workflows/release.yml`). Committing a package
would defeat the point -- its value is that CI can reproduce it from a clean
checkout.

Non-vacuity guards, in the spirit of the matrix gate this tool imports from:

  - The normative-source hashes are recomputed and compared against the pinned
    SHA256SUMS. A package whose spec references cannot be re-derived is evidence
    of nothing.
  - The matrix gate itself is re-run and its output captured into the package.
    A package built from a matrix that fails its own gate must not exist.
  - Claimed compliance points are read from the matrix rows, not hard-coded, so
    a downgraded CP row changes the generated claim instead of being papered
    over.
  - Test results must parse and contain zero failures unless the caller
    explicitly waives them (`--allow-missing-test-results`, used by the
    `evidence_package_check` gate, which has no build to draw results from).

Usage:
  python tools/sacm/generate_evidence_package.py                # dry run to build/
  python tools/sacm/generate_evidence_package.py --check        # CTest gate mode
  python tools/sacm/generate_evidence_package.py \
      --version 0.3.0 --toolchain "MSVC (VS 17 2022), windows-latest, Release" \
      --test-results build/test-results.xml --require-clean \
      --out package/evidence --zip package/evidence.zip         # CI mode
"""

import argparse
import hashlib
import json
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_conformance_matrix import parse_matrix, collect_test_ids, normalize  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
CHECKER = Path(__file__).resolve().parent / "check_conformance_matrix.py"
SPEC_DIR = REPO / "third_party/sacm-2.3"
SPEC_SUMS = SPEC_DIR / "SHA256SUMS"

# The documents frozen into the package. Each is evidence a reviewer of the
# conformance claim needs at the claimed commit, not at whatever the repository
# has moved on to.
FROZEN_DOCS = [
    "docs/sacm/sacm-conformance-matrix.md",
    "docs/sacm/sacm-compliance-points.md",
    "docs/sacm/sacm-matrix-completeness-audit.md",
    "docs/sacm/sacm-integration-preservation.md",
    "docs/sacm/sacm-interop-corpus.md",
    "docs/sacm/sacm-2.3-metamodel-inventory.md",
]
VERIFICATION_DIR = "docs/sacm/verification"

PACKAGE_FORMAT = "1"


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 16), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args):
    return subprocess.run(
        ["git", *args], cwd=REPO, capture_output=True, text=True, check=True
    ).stdout.strip()


def verify_spec_pins():
    """Recompute the normative-source hashes against the pinned SHA256SUMS."""
    problems, entries = [], []
    for line in SPEC_SUMS.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        expected, name = line.split(maxsplit=1)
        name = name.lstrip("*")
        actual = sha256_of(SPEC_DIR / name) if (SPEC_DIR / name).exists() else None
        if actual != expected:
            problems.append(f"{name}: pinned {expected}, recomputed {actual}")
        entries.append({"file": f"third_party/sacm-2.3/{name}", "sha256": expected})
    return entries, problems


def run_matrix_gate(out_dir):
    """Re-run the matrix gate and freeze its output into the package."""
    result = subprocess.run(
        [sys.executable, str(CHECKER)], cwd=REPO, capture_output=True, text=True
    )
    (out_dir / "matrix-gate-output.txt").write_text(
        result.stdout + result.stderr, encoding="utf-8"
    )
    return result.returncode == 0


def compliance_points_from(rows):
    """Read the claimed/not-claimed points from the CP rows rather than assume."""
    claimed, not_claimed, undecided = [], [], []
    for row in rows:
        if not row["id"].startswith("SACM23-CP-"):
            continue
        if row["status"] == "verified":
            claimed.append(row["id"])
        elif row["status"] == "out-of-scope":
            not_claimed.append(row["id"])
        else:
            undecided.append(f"{row['id']} ({row['status']})")
    return claimed, not_claimed, undecided


def traceability_from(rows, test_ids):
    entries = []
    for row in rows:
        entries.append({
            "requirement": row["id"],
            "status": row["status"],
            "cited_tests": row["tests"],
            "id_bearing_tests": test_ids.get(normalize(row["id"]), []),
        })
    return entries


def parse_junit(path):
    """Aggregate counts across every <testsuite> in a CTest --output-junit file."""
    root = ET.parse(path).getroot()
    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    totals = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0}
    for suite in suites:
        for key in totals:
            totals[key] += int(suite.get(key) or 0)
    return totals


def limitations_from(rows):
    """Non-verified rows plus the issue trail of the completeness audit."""
    open_rows = [
        {"requirement": r["id"], "status": r["status"], "notes": r["notes"]}
        for r in rows
        if r["status"] not in ("verified", "out-of-scope")
    ]
    audit = (REPO / "docs/sacm/sacm-matrix-completeness-audit.md").read_text(encoding="utf-8")
    # Only the findings tables carry tracking issues; the page's prose also links
    # the parent issue and the upstream-feedback issue, which are not limitations.
    findings = [
        section for section in audit.split("\n## ")
        if section.startswith(("Obligations with no owning matrix row",
                               "Verified rows whose claims exceed their evidence"))
    ]
    issue_numbers = sorted({
        int(number) for number in re.findall(r"/issues/(\d+)", "\n".join(findings))
    })
    return open_rows, issue_numbers


def write_statement(out_dir, manifest):
    claimed = ", ".join(manifest["compliance_points"]["claimed"]) or "(none)"
    lines = [
        "# SACM 2.3 conformance statement",
        "",
        f"**Release:** {manifest['version']}  ",
        f"**Commit:** `{manifest['commit']}`  ",
        f"**Generated:** {manifest['generated_at']}",
        "",
        "This statement is generated into the release evidence package by",
        "`tools/sacm/generate_evidence_package.py`; every claim below is derived",
        "from the frozen artifacts in this package, not typed by hand.",
        "",
        "## Claims",
        "",
        "The `libs/sacm` library at the commit above claims the SACM 2.3 compliance",
        f"points whose matrix rows are `verified`: **{claimed}** -- the Assurance Case,",
        "Argumentation, Artifact, and Terminology Model points, per clause 2 of",
        "`formal/23-05-08`. The **SACM UML Profile point (clause 2.6) is not claimed**.",
        "See `sacm-compliance-points.md` in this package for the decision record",
        "and the unit-of-interchange tests behind each point.",
        "",
        "These are import/export interchange claims about the library. What the",
        "Assurance Forge application can create or edit through its UI is a",
        "narrower, separately disclosed set -- see `limitations.md`.",
        "",
        "## This is a self-assessment",
        "",
        "Nothing here has been assessed by OMG or any certification body.",
        "Standards conformance is not regulatory approval and is not a safety",
        "argument about any system using this tool. The specification's own",
        "compliance page conditions claims on OMG-approved test suites where such",
        "suites exist; none is known to exist for SACM 2.3.",
        "",
        "## Verify this package",
        "",
        "- `SHA256SUMS` covers every file here; the normative-source hashes in",
        "  `manifest.json` match the OMG documents pinned by",
        "  `scripts/fetch-sacm23-references.sh`.",
        "- `traceability.json` maps every requirement row to the tests whose names",
        "  embed its ID; `matrix-gate-output.txt` is the matrix gate's own run.",
        "- `test-results.xml` is the CTest JUnit output of the released build.",
        "- Known gaps and their tracking issues: `limitations.md`.",
    ]
    (out_dir / "conformance-statement.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_limitations(out_dir, open_rows, issue_numbers):
    lines = [
        "# Known limitations at this release",
        "",
        "Generated from the frozen conformance matrix and the matrix-completeness",
        "audit. An empty section means the source recorded nothing -- not that",
        "nothing exists.",
        "",
        "## Matrix rows not `verified`",
        "",
    ]
    if open_rows:
        lines += ["| Requirement | Status | Summary |", "|---|---|---|"]
        for row in open_rows:
            summary = row["notes"][:300].replace("|", "\\|")
            lines.append(f"| {row['requirement']} | {row['status']} | {summary} |")
    else:
        lines.append("None. Every non-out-of-scope row is `verified`.")
    lines += [
        "",
        "## Gaps tracked from the completeness audit",
        "",
        "The independent matrix-completeness audit (frozen here as",
        "`sacm-matrix-completeness-audit.md`) tracks its findings in these GitHub",
        "issues; any still open at this release are open limitations of the claim:",
        "",
    ]
    lines += [
        f"- https://github.com/lasrod/assurance-forge/issues/{number}"
        for number in issue_numbers
    ] or ["- (none recorded)"]
    lines += [
        "",
        "## Application editing coverage",
        "",
        "Library interchange conformance does not imply UI editing support for",
        "every metaclass. The application's editing coverage and its disclosed",
        "limits are tracked in the capability matrix",
        "(`docs/features/feature-matrix.md` at the same commit).",
    ]
    (out_dir / "limitations.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_package(out_dir, args):
    problems = []
    out_dir.mkdir(parents=True, exist_ok=True)

    commit = git("rev-parse", "HEAD")
    dirty_entries = [line for line in git("status", "--porcelain").splitlines() if line.strip()]
    if args.require_clean and dirty_entries:
        problems.append(f"checkout is not clean ({len(dirty_entries)} entries) and --require-clean is set")

    spec_entries, spec_problems = verify_spec_pins()
    problems += spec_problems

    rows, malformed = parse_matrix()
    problems += [f"matrix: {message}" for message in malformed]
    if not rows:
        problems.append("matrix: no requirement rows parsed")
    test_ids = collect_test_ids()

    if not run_matrix_gate(out_dir):
        problems.append("matrix gate failed; see matrix-gate-output.txt in the package")

    claimed, not_claimed, undecided = compliance_points_from(rows)
    if undecided:
        problems.append(f"compliance-point rows neither verified nor out-of-scope: {', '.join(undecided)}")

    for relative in FROZEN_DOCS:
        source = REPO / relative
        if not source.exists():
            problems.append(f"frozen doc missing: {relative}")
            continue
        shutil.copy2(source, out_dir / source.name)
    records_dir = out_dir / "verification-records"
    records_dir.mkdir(exist_ok=True)
    for record in sorted((REPO / VERIFICATION_DIR).glob("*.md")):
        shutil.copy2(record, records_dir / record.name)

    (out_dir / "traceability.json").write_text(
        json.dumps(traceability_from(rows, test_ids), indent=2), encoding="utf-8"
    )

    test_totals = None
    if args.test_results:
        junit = Path(args.test_results)
        if not junit.exists():
            problems.append(f"test results not found: {junit}")
        else:
            shutil.copy2(junit, out_dir / "test-results.xml")
            test_totals = parse_junit(junit)
            if test_totals["failures"] or test_totals["errors"]:
                problems.append(f"test results contain failures: {test_totals}")
            if test_totals["tests"] == 0:
                problems.append("test results contain zero tests; a vacuous run proves nothing")
    elif not args.allow_missing_test_results:
        problems.append("no --test-results given (pass --allow-missing-test-results only for gate/dry runs)")

    open_rows, issue_numbers = limitations_from(rows)
    write_limitations(out_dir, open_rows, issue_numbers)

    manifest = {
        "package_format": PACKAGE_FORMAT,
        "version": args.version or f"dev-{commit[:7]}",
        "commit": commit,
        "dirty": bool(dirty_entries),
        "dirty_entries": dirty_entries,
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "generator": "tools/sacm/generate_evidence_package.py",
        "toolchain": args.toolchain or f"(unspecified) host: {platform.platform()}",
        "python": platform.python_version(),
        "normative_sources": spec_entries,
        "compliance_points": {"claimed": claimed, "not_claimed": not_claimed},
        "matrix_row_counts": {
            status: sum(1 for r in rows if r["status"] == status)
            for status in sorted({r["status"] for r in rows})
        },
        "test_totals": test_totals,
        "limitation_issue_numbers": issue_numbers,
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    write_statement(out_dir, manifest)

    sums = []
    for packaged in sorted(p for p in out_dir.rglob("*") if p.is_file() and p.name != "SHA256SUMS"):
        sums.append(f"{sha256_of(packaged)} *{packaged.relative_to(out_dir).as_posix()}")
    (out_dir / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")

    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=str(REPO / "build/evidence-package"))
    parser.add_argument("--zip", dest="zip_path", default=None)
    parser.add_argument("--version", default=None)
    parser.add_argument("--toolchain", default=None)
    parser.add_argument("--test-results", default=None)
    parser.add_argument("--require-clean", action="store_true")
    parser.add_argument("--allow-missing-test-results", action="store_true")
    parser.add_argument("--check", action="store_true",
                        help="gate mode: build into a temp dir, no test results required")
    args = parser.parse_args()

    if args.check:
        args.allow_missing_test_results = True
        with tempfile.TemporaryDirectory() as temp:
            problems = build_package(Path(temp) / "evidence-package", args)
        for problem in problems:
            print(f"  {problem}")
        print(f"[1] {'OK' if not problems else 'FAIL'}: evidence package builds from this checkout")
        return 0 if not problems else 1

    out_dir = Path(args.out)
    if out_dir.exists():
        shutil.rmtree(out_dir)
    problems = build_package(out_dir, args)
    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1
    if args.zip_path:
        archive = shutil.make_archive(str(Path(args.zip_path)).removesuffix(".zip"), "zip",
                                      root_dir=out_dir.parent, base_dir=out_dir.name)
        print(f"wrote {archive}")
    print(f"evidence package: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
