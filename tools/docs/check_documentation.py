#!/usr/bin/env python3
"""Documentation gates: links resolve, pages are reachable, generated docs are marked.

The repository accumulated twenty-nine pages reachable from neither the site
navigation nor any other page, and four architecture documents that each omitted
a different part of the application. Both classes of failure are mechanically
detectable, and neither was being detected.

What this does not do is judge whether documentation is any good. It catches the
failures a script can see, so that review attention goes to the ones it cannot.

Usage:
    python tools/docs/check_documentation.py
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DOCS = REPO / "docs"
MKDOCS = REPO / "mkdocs.yml"
LAYER_GATE = REPO / "cmake/check_layer_gates.cmake"
ARCHITECTURE_PAGE = DOCS / "architecture/layers-and-ownership.md"

# Pages under docs/ that are deliberately not reachable from the navigation or
# any other page. Each needs a reason: an exception list without one becomes the
# place unreachable pages quietly collect.
REACHABILITY_EXCEPTIONS = {
    # e.g. "docs/foo.md": "rendered only inside the release bundle",
}

# Committed documents produced by a tool. The value is the string that must
# appear somewhere in the page, naming what regenerates it.
GENERATED_DOCUMENTS = {
    "docs/sacm/sacm-2.3-metamodel-inventory.md": "tools/sacm/generate_metamodel_inventory.py",
    "docs/sacm/verification/README.md": "tools/docs/generate_verification_index.py",
    "docs/quality/repository-baseline.md": "tools/quality/collect_baseline.py",
}

_MD_LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
_EXTERNAL = re.compile(r"^(https?:|mailto:|#|<)")


def run(*args):
    result = subprocess.run(args, cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return result.stdout if result.returncode == 0 else None


def tracked_markdown():
    out = run("git", "ls-files", "*.md")
    if out is None:
        print("error: could not list tracked files (is this a git checkout?)", file=sys.stderr)
        raise SystemExit(2)
    return [line.strip() for line in out.splitlines() if line.strip()]


def check_links(files):
    """Every relative markdown link resolves to something on disk."""
    broken = []
    for rel in files:
        path = REPO / rel
        if not path.is_file():
            continue
        for target in _MD_LINK.findall(path.read_text(encoding="utf-8", errors="replace")):
            if _EXTERNAL.match(target):
                continue
            clean = target.split("#", 1)[0]
            if not clean:
                continue
            if not (path.parent / clean).resolve().exists():
                broken.append(f"{rel} -> {target}")
    return broken


def nav_entries():
    if not MKDOCS.is_file():
        return set()
    text = MKDOCS.read_text(encoding="utf-8", errors="replace")
    return {match.group(0) for match in re.finditer(r"[A-Za-z0-9_./-]+\.md", text)}


def check_reachability(files):
    """Every page under docs/ is in the nav or linked from another page."""
    linked = set()
    for rel in files:
        path = REPO / rel
        if not path.is_file():
            continue
        for target in _MD_LINK.findall(path.read_text(encoding="utf-8", errors="replace")):
            if _EXTERNAL.match(target):
                continue
            clean = target.split("#", 1)[0]
            if not clean:
                continue
            resolved = (path.parent / clean).resolve()
            try:
                linked.add(resolved.relative_to(REPO).as_posix())
            except ValueError:
                continue

    nav = nav_entries()
    unreachable = []
    for rel in files:
        if not rel.startswith("docs/"):
            continue
        if rel in REACHABILITY_EXCEPTIONS or rel in linked:
            continue
        if rel[len("docs/"):] in nav or rel in nav:
            continue
        unreachable.append(rel)
    return sorted(unreachable)


def check_generated_marked():
    """A generated page names the tool that regenerates it."""
    unmarked = []
    for rel, generator in sorted(GENERATED_DOCUMENTS.items()):
        path = REPO / rel
        if not path.is_file():
            unmarked.append(f"{rel} (listed as generated but missing)")
            continue
        if generator not in path.read_text(encoding="utf-8", errors="replace"):
            unmarked.append(f"{rel} (does not name {generator})")
    return unmarked


def enforced_layers():
    """Subsystems the layer gate checks, plus every directory under src/."""
    layers = set()
    if LAYER_GATE.is_file():
        match = re.search(r"set\(_AF_LAYERS ([^)]*)\)", LAYER_GATE.read_text(encoding="utf-8", errors="replace"))
        if match:
            layers.update(match.group(1).split())
    src = REPO / "src"
    if src.is_dir():
        layers.update(child.name for child in src.iterdir() if child.is_dir())
    return sorted(layers)


def check_architecture_covers_subsystems():
    """The canonical architecture page describes every subsystem that exists.

    Four architecture documents each omitted a different three-to-five of the
    eleven subsystems, and two were absent from all four. Nothing reported it,
    because a document that fails to mention something reads exactly like one
    that has nothing to say about it.
    """
    if not ARCHITECTURE_PAGE.is_file():
        return [f"{ARCHITECTURE_PAGE.relative_to(REPO).as_posix()} is missing"]
    text = ARCHITECTURE_PAGE.read_text(encoding="utf-8", errors="replace")
    return [
        subsystem
        for subsystem in enforced_layers()
        if not re.search(r"\b" + re.escape(subsystem) + r"\b", text)
    ]


def report(index, label, failures, remedy):
    if failures:
        print(f"[{index}] FAIL: {label}\n")
        for item in failures:
            print(f"  {item}")
        print(f"\n{remedy}\n")
        return 1
    print(f"[{index}] OK: {label}")
    return 0


def main():
    files = tracked_markdown()
    failures = 0

    failures += report(
        1,
        f"all relative links in {len(files)} markdown files resolve",
        check_links(files),
        "Fix the link, or the file it points at.",
    )
    failures += report(
        2,
        "every page under docs/ is reachable from the nav or another page",
        check_reachability(files),
        "Add it to mkdocs.yml, link it from a page that is itself reachable, or\n"
        "add it to REACHABILITY_EXCEPTIONS in this script with the reason why.",
    )
    failures += report(
        3,
        "generated documents name their generator",
        check_generated_marked(),
        "Add a line naming the tool that regenerates the page.",
    )
    failures += report(
        4,
        "the architecture page describes every subsystem the build enforces",
        check_architecture_covers_subsystems(),
        "Describe the subsystem in docs/architecture/layers-and-ownership.md.\n"
        "A subsystem the build enforces rules for, but no document mentions, is\n"
        "invisible to everyone who has not read the CMake.",
    )

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
