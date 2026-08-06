#!/usr/bin/env python3
"""Generate the verification-record index from the records' own front matter.

`docs/sacm/verification/README.md` explained the convention but listed two of
fourteen records, so the rest were reachable only by browsing the directory.

The index is generated rather than written by hand because the metadata that
matters -- which requirement a pass covers, and whether it passed -- is already
in each record's front matter, and a hand-copied table gets it wrong. It did:
`2026-07-20-phases-0-8.md` carries `verdict: FAIL` while its filename lacks the
`-FAIL` suffix every other failing record uses, so a table built by reading
filenames records a failed verification as a pass. That is the one error a
conformance evidence index cannot make.

Usage:
    python tools/docs/generate_verification_index.py           # rewrite the index
    python tools/docs/generate_verification_index.py --check   # non-zero if stale
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RECORDS_DIR = REPO / "docs/sacm/verification"
INDEX_PATH = RECORDS_DIR / "README.md"

BEGIN = "<!-- BEGIN GENERATED: verification records -->"
END = "<!-- END GENERATED: verification records -->"

# Files in the directory that are not verification records.
NOT_RECORDS = {"README.md", "TEMPLATE.md"}

_FRONT_MATTER = re.compile(r"\A---\r?\n(.*?)\r?\n---\r?\n", re.S)


def parse_front_matter(path):
    """Return {slice, date, verdict, requirements[]} or None if absent.

    Deliberately not a YAML parser: the front matter is a fixed four-key shape
    written by one agent, and adding a dependency to read it would make the gate
    harder to run than the thing it guards.
    """
    text = path.read_text(encoding="utf-8")
    match = _FRONT_MATTER.search(text)
    if not match:
        return None

    fields = {}
    for line in match.group(1).splitlines():
        if ":" not in line:
            continue
        key, _, value = line.partition(":")
        fields[key.strip()] = value.strip()

    requirements = []
    raw = fields.get("requirements", "")
    if raw.startswith("[") and raw.endswith("]"):
        requirements = [item.strip() for item in raw[1:-1].split(",") if item.strip()]
    elif raw:
        requirements = [raw]

    return {
        "slice": fields.get("slice", ""),
        "date": fields.get("date", ""),
        "verdict": fields.get("verdict", "").upper(),
        "requirements": requirements,
        "path": path.name,
    }


def collect():
    records = []
    malformed = []
    for path in sorted(RECORDS_DIR.glob("*.md")):
        if path.name in NOT_RECORDS:
            continue
        parsed = parse_front_matter(path)
        if parsed is None or not parsed["date"] or not parsed["verdict"]:
            malformed.append(path.name)
            continue
        records.append(parsed)
    records.sort(key=lambda r: (r["date"], r["path"]), reverse=True)
    return records, malformed


def humanize(slug):
    return slug.replace("-", " ").strip().capitalize()


def render(records):
    lines = [
        BEGIN,
        "",
        "| Date | Requirements | Verdict | Record |",
        "|---|---|---|---|",
    ]
    for record in records:
        requirements = ", ".join(f"`{r}`" for r in record["requirements"]) or "—"
        # Long requirement lists make the table unreadable; the record itself
        # carries the full list either way.
        if len(record["requirements"]) > 4:
            requirements = f"`{record['requirements'][0]}` and {len(record['requirements']) - 1} more"
        verdict = "**FAIL**" if record["verdict"] == "FAIL" else record["verdict"].title()
        lines.append(
            f"| {record['date']} | {requirements} | {verdict} | "
            f"[{humanize(record['slice'])}]({record['path']}) |"
        )
    lines += ["", END]
    return "\n".join(lines)


def apply(text, block):
    if BEGIN not in text or END not in text:
        raise SystemExit(
            f"error: {INDEX_PATH.relative_to(REPO).as_posix()} is missing the "
            f"generated-block markers.\nExpected {BEGIN} ... {END}"
        )
    start = text.index(BEGIN)
    stop = text.index(END) + len(END)
    return text[:start] + block + text[stop:]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="exit non-zero if the committed index is stale")
    args = parser.parse_args()

    records, malformed = collect()
    if malformed:
        print("[1] FAIL: verification records without parseable front matter:\n")
        for name in malformed:
            print(f"  {name}")
        print("\nEvery record needs `slice`, `date`, `verdict` and `requirements`.")
        print("Start from TEMPLATE.md.")
        return 1

    current = INDEX_PATH.read_text(encoding="utf-8")
    updated = apply(current, render(records))

    if args.check:
        if current != updated:
            print(
                "[1] FAIL: the verification index is out of date.\n"
                "Regenerate it with: python tools/docs/generate_verification_index.py"
            )
            return 1
        failed = sum(1 for r in records if r["verdict"] == "FAIL")
        print(f"[1] OK: verification index lists {len(records)} records ({failed} FAIL) and is up to date")
        return 0

    INDEX_PATH.write_text(updated, encoding="utf-8")
    print(f"wrote {len(records)} records to {INDEX_PATH.relative_to(REPO).as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
