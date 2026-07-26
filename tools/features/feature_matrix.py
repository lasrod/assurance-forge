#!/usr/bin/env python3
"""Parser for `docs/features/feature-matrix.md`.

Shared by `check_feature_matrix.py` (the CI gate) and
`export_feature_matrix.py` (the JSON the documentation site consumes). Keeping
one parser means the gate validates exactly what the export publishes -- two
parsers would eventually disagree, and the disagreement would surface as a
support claim on the website that no test backs.
"""

import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MATRIX_PATH = REPO / "docs/features/feature-matrix.md"
JSON_PATH = REPO / "docs/features/feature-matrix.json"
SACM_MATRIX_PATH = REPO / "docs/sacm/sacm-conformance-matrix.md"

_ROW_ID_RE = re.compile(r"^AF-([A-Z]+)-([0-9]{3})$")
_BACKTICKED_RE = re.compile(r"^`([^`]+)`$")

# Only path prefixes that are always present in a clone are checked. `external/`
# is deliberately excluded: those are submodules, and an uninitialized submodule
# would fail the gate for a reason that has nothing to do with matrix accuracy.
_PATH_RE = re.compile(r"\b(?:libs|cmake|src|docs|tools|tests|scripts)/[A-Za-z0-9_./-]+")

_SACM_ID_RE = re.compile(r"SACM23-[A-Z]+-[0-9]+")

# Statuses whose rows are claims about shipped behaviour, so they must point at
# code that exists.
IMPLEMENTED_STATUSES = {"supported", "prototype"}
# Statuses that assert nothing is built yet. A test citation on one of these is
# the signature of a row reality has overtaken.
UNBUILT_STATUSES = {"planned", "candidate", "not-planned"}


def _cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def _unbacktick(text):
    match = _BACKTICKED_RE.match(text)
    return match.group(1) if match else text


def _split_citations(cell):
    """Evidence and Tests cells are comma-separated path lists (possibly empty)."""
    return [part.strip() for part in cell.split(",") if part.strip()]


def strip_trailing_punctuation(path):
    """`docs/x.md.` at the end of a sentence is a citation, not a missing file."""
    return path.rstrip(".,;:)")


def cited_paths(*cells):
    """Every resolvable repo path mentioned across the given cells."""
    found = []
    for cell in cells:
        for raw in _PATH_RE.findall(cell):
            candidate = strip_trailing_punctuation(raw)
            if candidate and candidate not in found:
                found.append(candidate)
    return found


def parse(matrix_path=MATRIX_PATH):
    """Return {statuses, areas, features} parsed from the matrix markdown.

    `statuses` and `areas` come from the document's own vocabulary tables, so the
    matrix defines its own valid values rather than the checker hardcoding a copy
    that can drift out of step with the prose.
    """
    statuses = []
    areas = []
    features = []
    section = ""

    text = matrix_path.read_text(encoding="utf-8-sig")
    for lineno, line in enumerate(text.splitlines(), 1):
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        if not line.startswith("|"):
            continue
        cells = _cells(line)

        if section == "Status vocabulary" and len(cells) == 3:
            key = _unbacktick(cells[0])
            if key in ("Status", "---") or key.startswith("--"):
                continue
            roadmap = cells[2].strip()
            statuses.append({
                "key": key,
                "label": key.replace("-", " ").capitalize(),
                "meaning": cells[1].strip(),
                "roadmapEquivalent": None if roadmap in ("", "—", "-") else roadmap,
            })
            continue

        if section == "Areas" and len(cells) == 2:
            code = _unbacktick(cells[0])
            if code in ("Code", "---") or code.startswith("--"):
                continue
            areas.append({"code": code, "label": cells[1].strip()})
            continue

        if len(cells) != 6:
            continue
        match = _ROW_ID_RE.match(cells[0])
        if not match:
            continue
        features.append({
            "id": cells[0],
            "area": match.group(1),
            "name": cells[1],
            "status": cells[2],
            "evidence": _split_citations(cells[3]),
            "tests": _split_citations(cells[4]),
            "notes": cells[5],
            "line": lineno,
        })

    return {"statuses": statuses, "areas": areas, "features": features}


def to_json_document(parsed):
    """The published shape. `line` is a checker detail and is dropped here."""
    return {
        "$comment": (
            "Generated from docs/features/feature-matrix.md by "
            "tools/features/export_feature_matrix.py. Do not edit by hand."
        ),
        "source": "docs/features/feature-matrix.md",
        "statuses": parsed["statuses"],
        "areas": parsed["areas"],
        "features": [
            {key: value for key, value in feature.items() if key != "line"}
            for feature in parsed["features"]
        ],
    }


def render_json(parsed):
    return json.dumps(to_json_document(parsed), indent=2, ensure_ascii=False) + "\n"


def sacm_requirement_ids(path=SACM_MATRIX_PATH):
    if not path.exists():
        return set()
    return set(_SACM_ID_RE.findall(path.read_text(encoding="utf-8-sig")))


def referenced_sacm_ids(feature):
    return set(_SACM_ID_RE.findall(" ".join([feature["notes"], feature["name"]])))
