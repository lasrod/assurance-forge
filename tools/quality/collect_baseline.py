#!/usr/bin/env python3
"""Collect the repository quality baseline (issue #288).

Measures the repository so that later cleanup decisions and quality claims rest
on evidence rather than impressions. Writes a machine-readable snapshot to
`docs/quality/repository-baseline.json` and prints markdown tables that the
human-readable report `docs/quality/repository-baseline.md` embeds.

Design rules this script follows, because a baseline that overstates itself is
worse than no baseline:

- Every number is either measured here from the working tree / git history, or
  it is reported as unavailable with a reason. Nothing is estimated silently.
- Vendored, generated, and first-party content are counted separately and never
  summed into a single headline total.
- Measurements that need a build, a network call, or a toolchain this script
  cannot assume are reported as `unavailable` with the command that would
  produce them, rather than omitted.

Usage:
    python tools/quality/collect_baseline.py            # write JSON + print markdown
    python tools/quality/collect_baseline.py --json-only
    python tools/quality/collect_baseline.py --check    # non-zero exit if JSON is stale
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
JSON_PATH = REPO / "docs/quality/repository-baseline.json"

sys.path.insert(0, str(REPO / "tools"))

# --------------------------------------------------------------------------
# Classification. Order matters: the first matching rule wins.
# --------------------------------------------------------------------------

# There is no directory skip list because nothing here walks the filesystem for
# content: every measurement enumerates `git ls-files`. Build trees, virtualenvs,
# caches, and submodule working copies are therefore excluded by construction
# rather than by a list that could drift out of step with .gitignore. The one
# exception is collect_root_surface(), which lists the repository root
# deliberately, untracked residue included, because that residue is a finding.

CODE_SUFFIXES = {".cpp", ".cc", ".h", ".hpp", ".inl"}
SCRIPT_SUFFIXES = {".py", ".sh", ".ps1"}

# (category, label, [path prefixes relative to repo root])
CLASSIFICATION = [
    ("first_party_tests", "First-party tests", ["tests/", "libs/sacm/tests/"]),
    ("first_party_production", "First-party production", ["src/", "libs/sacm/include/", "libs/sacm/src/"]),
    ("first_party_tooling", "First-party tooling", ["tools/", "scripts/", "cmake/", ".githooks/", "libs/sacm/tools/"]),
    # Retained so that vendored code would be classified rather than silently
    # falling into "other" if any is ever committed. Today nothing matches:
    # third_party/ is gitignored reference material, reported separately by
    # collect_vendored_reference().
    ("vendored", "Vendored / third-party (in tree)", ["third_party/"]),
    ("documentation", "Documentation", ["docs/"]),
    ("assets_data", "Assets and data", ["assets/", "data/"]),
    ("ci_config", "CI and repository config", [".github/", ".vscode/"]),
]

# Files that are produced by a generator and committed. Counted apart from
# hand-written content so totals are not inflated by machine output.
GENERATED_PATTERNS = [
    re.compile(r"^assets/locale/.*\.mo$"),
    re.compile(r"^docs/features/feature-matrix\.json$"),
    re.compile(r"^docs/sacm/sacm-2\.3-metamodel-inventory\.md$"),
    re.compile(r"^docs/clang-uml/"),
    re.compile(r"^docs/diagrams/"),
]


def run(*args, cwd=REPO):
    """Run a command and return stdout, or None if it fails or is unavailable."""
    try:
        completed = subprocess.run(
            args, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace"
        )
    except (OSError, ValueError):
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout


def tracked_files():
    """Every path tracked by git, repo-relative, forward slashes."""
    out = run("git", "ls-files")
    if out is None:
        return []
    return [line.strip() for line in out.splitlines() if line.strip()]


def classify(rel_path):
    for category, _label, prefixes in CLASSIFICATION:
        for prefix in prefixes:
            if rel_path.startswith(prefix):
                return category
    return "other"


def is_generated(rel_path):
    return any(pattern.search(rel_path) for pattern in GENERATED_PATTERNS)


def count_lines(path):
    """Return (physical, code) lines. `code` strips blanks and C/C++ comments.

    The comment stripper is deliberately simple: it does not understand string
    literals containing comment markers. It is used only for C++ files and its
    output is labelled an approximation in the report.
    """
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0, 0
    physical = 0
    code = 0
    in_block = False
    for raw in text.splitlines():
        physical += 1
        line = raw.strip()
        if in_block:
            if "*/" in line:
                in_block = False
                line = line.split("*/", 1)[1].strip()
            else:
                continue
        while "/*" in line:
            before, _, after = line.partition("/*")
            if "*/" in after:
                line = (before + " " + after.split("*/", 1)[1]).strip()
            else:
                line = before.strip()
                in_block = True
                break
        if not line or line.startswith("//"):
            continue
        code += 1
    return physical, code


# --------------------------------------------------------------------------
# Collectors
# --------------------------------------------------------------------------


def collect_identity():
    sha = (run("git", "rev-parse", "HEAD") or "").strip()
    date = (run("git", "log", "-1", "--format=%cI") or "").strip()
    branch = (run("git", "rev-parse", "--abbrev-ref", "HEAD") or "").strip()
    # A baseline cannot cite its own commit -- recording the SHA changes it, and
    # amending to fix that changes it again. Cite the point it branched from
    # instead, which is stable no matter how many commits the baseline itself
    # adds. Falls back to HEAD when origin/main is not fetched.
    base = (run("git", "merge-base", "HEAD", "origin/main") or "").strip() or sha
    # Ask for the root commit directly. `git log --reverse` would walk the whole
    # history to yield a first line, which gets slower every year for one date.
    roots = sorted(
        line.strip()
        for line in (run("git", "log", "--format=%cI", "--max-parents=0", "HEAD") or "").splitlines()
        if line.strip()
    )
    commits = (run("git", "rev-list", "--count", "HEAD") or "").strip()
    status = run("git", "status", "--porcelain") or ""
    dirty = [line for line in status.splitlines() if line.strip()]
    return {
        "commit": sha,
        "commit_date": date,
        "base_commit": base,
        "branch": branch,
        "first_commit_date": roots[0] if roots else None,
        "total_commits": int(commits) if commits.isdigit() else None,
        "working_tree_dirty_entries": len(dirty),
    }


def collect_code_size(files):
    """File counts and line counts per category, split by generated status."""
    buckets = defaultdict(lambda: {"files": 0, "physical_lines": 0, "code_lines": 0})
    per_subsystem = defaultdict(lambda: {"files": 0, "physical_lines": 0, "code_lines": 0})
    generated = {"files": 0, "physical_lines": 0}

    for rel in files:
        path = REPO / rel
        if not path.is_file():
            continue  # gitlink (submodule) or deleted in working tree
        if is_generated(rel):
            physical, _ = count_lines(path) if path.suffix not in {".mo"} else (0, 0)
            generated["files"] += 1
            generated["physical_lines"] += physical
            continue
        category = classify(rel)
        if path.suffix not in CODE_SUFFIXES | SCRIPT_SUFFIXES:
            # Non-code files still count toward file totals, not line totals.
            buckets[category]["files"] += 1
            continue
        physical, code = count_lines(path)
        bucket = buckets[category]
        bucket["files"] += 1
        bucket["physical_lines"] += physical
        bucket["code_lines"] += code

        if category in ("first_party_production", "first_party_tests"):
            subsystem = subsystem_of(rel)
            entry = per_subsystem[subsystem]
            entry["files"] += 1
            entry["physical_lines"] += physical
            entry["code_lines"] += code

    return {
        "by_category": {k: dict(v) for k, v in sorted(buckets.items())},
        "by_subsystem": {k: dict(v) for k, v in sorted(per_subsystem.items())},
        "generated_committed": generated,
    }


def subsystem_of(rel):
    """Map a first-party production/test path to an owning subsystem."""
    if rel.startswith("libs/sacm/tests/"):
        return "libs/sacm (tests)"
    if rel.startswith("libs/sacm/"):
        return "libs/sacm"
    if rel.startswith("tests/"):
        return "tests (app suite)"
    parts = rel.split("/")
    if parts[0] == "src" and len(parts) > 2:
        return f"src/{parts[1]}"
    if parts[0] == "src":
        return "src (root)"
    return parts[0]


_TEST_RE = re.compile(r"^\s*TEST(?:_F|_P)?\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)", re.M)
_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)

# Production include prefixes attributed to a subsystem, longest first so that
# libs/sacm wins over a bare `sacm/` match.
def owner_of_include(header):
    """Resolve an include to the subsystem that actually provides the header.

    `src/sacm` and `libs/sacm/include/sacm` both answer to the `sacm/` prefix, so
    prefix matching alone silently credits one subsystem's tests to the other.
    Resolve against the filesystem instead; the ambiguity is recorded as a
    finding rather than papered over with a hardcoded preference.
    """
    if (REPO / "src" / header).is_file():
        top = header.split("/")[0]
        return f"src/{top}"
    if (REPO / "libs/sacm/include" / header).is_file():
        return "libs/sacm"
    return None


def ambiguous_include_prefixes():
    """Include prefixes served by more than one subsystem's include root."""
    roots = {"src": REPO / "src", "libs/sacm/include": REPO / "libs/sacm/include"}
    seen = defaultdict(set)
    for label, root in roots.items():
        if not root.is_dir():
            continue
        for child in root.iterdir():
            if child.is_dir():
                seen[child.name + "/"].add(label)
    return {prefix: sorted(owners) for prefix, owners in sorted(seen.items()) if len(owners) > 1}


def collect_tests(files):
    """gtest case counts per file, attributed to subsystems by their includes.

    Attribution is by which production headers a test file includes, not by its
    filename, so the mapping is measured rather than guessed. A test file that
    includes headers from several subsystems is counted under each of them; the
    per-subsystem counts therefore overlap and must not be summed.
    """
    per_file = {}
    total_cases = 0
    suites = set()
    attribution = defaultdict(lambda: {"files": 0, "cases": 0})
    unattributed = []

    test_files = [f for f in files if subsystem_of(f) in ("tests (app suite)", "libs/sacm (tests)")]
    test_files = [f for f in test_files if f.endswith(".cpp")]

    for rel in sorted(test_files):
        path = REPO / rel
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        cases = _TEST_RE.findall(text)
        per_file[rel] = len(cases)
        total_cases += len(cases)
        suites.update(name for name, _ in cases)

        owners = {
            owner
            for owner in (owner_of_include(h) for h in _INCLUDE_RE.findall(text))
            if owner
        }
        if owners:
            for owner in owners:
                attribution[owner]["files"] += 1
                attribution[owner]["cases"] += len(cases)
        elif cases:
            unattributed.append(rel)

    largest = sorted(per_file.items(), key=lambda kv: -kv[1])[:15]
    return {
        "test_source_files": len(per_file),
        "test_cases": total_cases,
        "test_suites": len(suites),
        "cases_by_subsystem_overlapping": {k: dict(v) for k, v in sorted(attribution.items())},
        "unattributed_test_files": sorted(unattributed),
        "largest_test_files": [{"path": p, "cases": c} for p, c in largest],
        "ambiguous_include_prefixes": ambiguous_include_prefixes(),
        "ctest_registered": ctest_count(),
    }


def ctest_count():
    """Registered CTest tests, if a configured build tree is present.

    Multi-config generators (Visual Studio) list nothing without `-C`, so try the
    configurations explicitly before giving up.
    """
    for build in ("build", "build-coverage"):
        directory = REPO / build
        if not (directory / "CTestTestfile.cmake").exists():
            continue
        for config in (None, "Debug", "Release"):
            command = ["ctest", "--test-dir", str(directory), "-N"]
            if config:
                command += ["-C", config]
            out = run(*command)
            if out is None:
                continue
            match = re.search(r"Total Tests:\s*(\d+)", out)
            if match and int(match.group(1)) > 0:
                return {
                    "available": True,
                    "build_dir": build,
                    "config": config,
                    "count": int(match.group(1)),
                }
    return {
        "available": False,
        "reason": "no configured build tree with registered tests was present",
        "command": "cmake --preset default && ctest --test-dir build -C Debug -N",
    }


def collect_hotspots(files):
    """Largest files, most-changed files, and include fan-in / fan-out."""
    production = [
        f for f in files
        if classify(f) == "first_party_production"
        and (REPO / f).is_file()
        and (REPO / f).suffix in CODE_SUFFIXES
    ]

    sizes = []
    fan_out = {}
    fan_in = Counter()
    for rel in production:
        path = REPO / rel
        physical, code = count_lines(path)
        sizes.append({"path": rel, "physical_lines": physical, "code_lines": code})
        headers = _INCLUDE_RE.findall(path.read_text(encoding="utf-8", errors="replace"))
        first_party = [h for h in headers if (REPO / "src" / h).exists() or (REPO / "libs/sacm/include" / h).exists()]
        fan_out[rel] = len(first_party)
        for header in first_party:
            fan_in[header] += 1

    churn = Counter()
    log = run("git", "log", "--format=%H", "--name-only", "--no-merges")
    if log:
        for line in log.splitlines():
            line = line.strip()
            if not line or re.fullmatch(r"[0-9a-f]{40}", line):
                continue
            churn[line] += 1

    production_set = set(production)
    top_churn = [
        {"path": p, "commits": c}
        for p, c in churn.most_common()
        if p in production_set
    ][:15]

    return {
        "largest_production_files": sorted(sizes, key=lambda s: -s["physical_lines"])[:15],
        "highest_fan_out": [
            {"path": p, "first_party_includes": n}
            for p, n in sorted(fan_out.items(), key=lambda kv: -kv[1])[:15]
        ],
        "highest_fan_in": [{"header": h, "included_by": n} for h, n in fan_in.most_common(15)],
        "most_changed_production_files": top_churn,
        "churn_window": "full history, merge commits excluded",
    }


def collect_layer_gates():
    gate = REPO / "cmake/check_layer_gates.cmake"
    if not gate.is_file():
        return {"available": False, "reason": "cmake/check_layer_gates.cmake not found"}
    text = gate.read_text(encoding="utf-8", errors="replace")
    allow_block = re.search(r"set\(_AF_ALLOWLIST(.*?)\)", text, re.S)
    exceptions = []
    if allow_block:
        exceptions = re.findall(r'"([^"]+)"', allow_block.group(1))
    forbidden = {
        m.group(1): [p for p in m.group(2).split(";") if p]
        for m in re.finditer(r'set\(_AF_FORBIDDEN_(\w+)\s+"([^"]*)"\)', text)
    }
    layers = re.search(r"set\(_AF_LAYERS ([^)]*)\)", text)
    return {
        "available": True,
        "layers_checked": layers.group(1).split() if layers else [],
        "forbidden_rules": forbidden,
        "exceptions": exceptions,
        "exception_count": len(exceptions),
        "enforced_at": "configure time (FATAL_ERROR)",
    }


def collect_matrices():
    result = {}
    try:
        from sacm import check_conformance_matrix as sacm_check

        rows = sacm_check.parse_matrix()
        result["sacm_conformance"] = {
            "available": True,
            "rows": len(rows),
            "by_status": dict(sorted(Counter(r["status"] for r in rows).items())),
            "non_verified_ids": sorted(r["id"] for r in rows if r["status"] != "verified"),
        }
    except Exception as exc:  # noqa: BLE001 - report, do not abort the whole baseline
        result["sacm_conformance"] = {"available": False, "reason": f"{type(exc).__name__}: {exc}"}

    try:
        from features import feature_matrix

        parsed = feature_matrix.parse()
        features = parsed["features"]
        result["capability"] = {
            "available": True,
            "rows": len(features),
            "by_status": dict(sorted(Counter(f["status"] for f in features).items())),
            "by_area": dict(sorted(Counter(f["area"] for f in features).items())),
            "rows_without_tests": sorted(f["id"] for f in features if not f["tests"]),
        }
    except Exception as exc:  # noqa: BLE001
        result["capability"] = {"available": False, "reason": f"{type(exc).__name__}: {exc}"}

    return result


_MD_LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def collect_documentation(files):
    docs = [f for f in files if f.endswith(".md")]
    by_dir = Counter(str(Path(f).parent).replace("\\", "/") for f in docs)

    broken = []
    linked = set()
    for rel in docs:
        path = REPO / rel
        if not path.is_file():
            continue
        for target in _MD_LINK_RE.findall(path.read_text(encoding="utf-8", errors="replace")):
            if re.match(r"^(https?:|mailto:|#)", target):
                continue
            clean = target.split("#", 1)[0]
            if not clean:
                continue
            resolved = (path.parent / clean).resolve()
            try:
                rel_target = resolved.relative_to(REPO).as_posix()
            except ValueError:
                continue  # points outside the repository
            linked.add(rel_target)
            if not resolved.exists():
                broken.append({"from": rel, "target": target})

    nav = set()
    mkdocs = REPO / "mkdocs.yml"
    if mkdocs.is_file():
        text = mkdocs.read_text(encoding="utf-8", errors="replace")
        nav = {m.group(0) for m in re.finditer(r"[A-Za-z0-9_./-]+\.md", text)}

    site_docs = [f for f in docs if f.startswith("docs/")]
    orphans = sorted(
        f for f in site_docs
        if f not in linked and f[len("docs/"):] not in nav and f not in nav
    )

    return {
        "markdown_files": len(docs),
        "by_directory": dict(sorted(by_dir.items(), key=lambda kv: (-kv[1], kv[0]))),
        "broken_relative_links": broken,
        "broken_link_count": len(broken),
        "mkdocs_nav_entries": len(nav),
        "orphan_pages": orphans,
        "orphan_page_count": len(orphans),
    }


def collect_root_surface(files):
    """Classify every entry in the repository root."""
    tracked_root = {f for f in files if "/" not in f}
    entries = []
    for path in sorted(REPO.iterdir()):
        name = path.name
        if name == ".git":
            continue
        tracked = name in tracked_root or any(f.startswith(name + "/") for f in files)
        entries.append({
            "name": name + ("/" if path.is_dir() else ""),
            "kind": "directory" if path.is_dir() else "file",
            "tracked": tracked,
        })

    # Committed artifacts that look machine-produced or single-use. Flagged, not
    # deleted: removal is issue #289's scope, this baseline only records them.
    suspicious_re = re.compile(
        r"^(build_out\.txt|full_tests\.txt|issue-body\.md|cmake_test_discovery_.*\.json|imgui\.ini|.*\.log)$"
    )
    suspicious = sorted(f for f in tracked_root if suspicious_re.match(f))

    return {
        "root_entries": entries,
        "tracked_root_files": len(tracked_root),
        "committed_artifact_candidates": suspicious,
        "committed_artifact_candidate_count": len(suspicious),
    }


def collect_ai_assets():
    def stems(directory, suffix):
        d = REPO / directory
        if not d.is_dir():
            return []
        return sorted(p.stem for p in d.glob(f"*{suffix}"))

    claude = stems(".claude/agents", ".md")
    codex = stems(".codex/agents", ".toml")
    skills = []
    skills_dir = REPO / ".claude/skills"
    if skills_dir.is_dir():
        skills = sorted(p.name for p in skills_dir.iterdir() if p.is_dir())

    instruction_files = [
        f for f in ("AGENTS.md", "CLAUDE.md", "CONTRIBUTING.md", "README.md")
        if (REPO / f).is_file()
    ]
    return {
        "claude_agents": claude,
        "codex_agents": codex,
        "duplicated_agent_roles": sorted(set(claude) & set(codex)),
        "claude_only": sorted(set(claude) - set(codex)),
        "codex_only": sorted(set(codex) - set(claude)),
        "skills": skills,
        "top_level_instruction_files": instruction_files,
        "canonical_source": None,
        "note": "No generator or drift check exists; each platform directory is hand-maintained.",
    }


def collect_build_and_analysis():
    text = ""
    for rel in ("CMakeLists.txt", "src/CMakeLists.txt", "libs/sacm/CMakeLists.txt"):
        path = REPO / rel
        if path.is_file():
            text += path.read_text(encoding="utf-8", errors="replace")

    warning_flags = sorted(set(re.findall(r"[-/](?:W\w+|Wno-[\w-]+|Werror|permissive-)", text)))
    workflows = sorted(p.name for p in (REPO / ".github/workflows").glob("*.yml"))
    workflow_text = "".join(
        (REPO / ".github/workflows" / w).read_text(encoding="utf-8", errors="replace") for w in workflows
    )

    tools_present = {
        "clang-format": (REPO / ".clang-format").is_file(),
        "clang-tidy": (REPO / ".clang-tidy").is_file(),
        "cppcheck": "cppcheck" in workflow_text,
        "sanitizers": bool(re.search(r"-fsanitize", text + workflow_text)),
        "fuzzing": "fuzz" in workflow_text.lower(),
        "coverage": (REPO / "gcovr-logic.cfg").is_file(),
    }

    return {
        "warning_flags_in_cmake": warning_flags,
        "warnings_as_errors": any("Werror" in f or "WX" in f for f in warning_flags),
        "workflows": workflows,
        "quality_tooling_present": tools_present,
        "compiler_warning_count": {
            "available": False,
            "reason": "requires a clean build per compiler; not reproducible from a source scan",
            "command": "cmake --build --preset release 2>&1 | grep -c warning",
        },
        "coverage": collect_coverage(),
    }


_SUMMARY_RE = re.compile(r"(lines|functions|branches|conditions):\s*([\d.]+)%\s*\((\d+) out of (\d+)\)")


def collect_coverage():
    """Coverage percentages from the most recent successful Coverage workflow run.

    The workflow publishes HTML artifacts and prints a gcovr summary per scope;
    no number is committed to the repository. Rather than declare coverage
    unmeasurable, read the summaries back out of the run log and bind them to the
    commit that produced them — a reader can then tell whether the figures still
    describe the tree in front of them.

    Scope labels come from the step names in coverage.yml, in order, so a
    reordered workflow relabels correctly instead of silently mislabelling.
    """
    workflow = REPO / ".github/workflows/coverage.yml"
    scopes = []
    if workflow.is_file():
        scopes = re.findall(
            r"- name: Generate coverage report \(([^)]*)\)",
            workflow.read_text(encoding="utf-8", errors="replace"),
        )

    listing = run(
        "gh", "run", "list", "--workflow", "coverage.yml", "--status", "success",
        "--limit", "1", "--json", "databaseId,headSha,createdAt",
    )
    if listing is None:
        return {
            "available": False,
            "reason": "GitHub CLI unavailable or not authenticated",
            "command": "gh run list --workflow coverage.yml --status success --limit 1",
        }
    try:
        runs = json.loads(listing)
    except json.JSONDecodeError:
        runs = []
    if not runs:
        return {"available": False, "reason": "no successful Coverage run found"}

    entry = runs[0]
    log = run("gh", "run", "view", str(entry["databaseId"]), "--log")
    if log is None:
        return {
            "available": False,
            "reason": "could not download the Coverage run log",
            "run_id": entry["databaseId"],
        }

    measures = []
    current = {}
    for name, percent, covered, total in _SUMMARY_RE.findall(log):
        if name in current:
            measures.append(current)
            current = {}
        current[name] = {"percent": float(percent), "covered": int(covered), "total": int(total)}
    if current:
        measures.append(current)

    if len(measures) != len(scopes):
        return {
            "available": False,
            "reason": f"parsed {len(measures)} summaries but coverage.yml declares {len(scopes)} scopes",
            "run_id": entry["databaseId"],
        }

    return {
        "available": True,
        "run_id": entry["databaseId"],
        "run_commit": entry["headSha"],
        "run_date": entry["createdAt"],
        "compiled_files_changed_since_run": None,  # filled in by collect()
        "by_scope": {scope: measure for scope, measure in zip(scopes, measures)},
        "note": "Linux / GCC 14 only. No threshold gates on these numbers.",
    }


def collect_ci_matrix():
    ci = REPO / ".github/workflows/ci.yml"
    if not ci.is_file():
        return {"available": False, "reason": "ci.yml not found"}
    text = ci.read_text(encoding="utf-8", errors="replace")
    names = re.findall(r"- name:\s*(\w+)\n\s+os:\s*([\w.-]+)", text)
    durations = ci_durations()
    return {
        "available": True,
        "platforms": [{"name": n, "runner": o} for n, o in names],
        "build_type": "Debug" if "CMAKE_BUILD_TYPE=Debug" in text else "unspecified",
        "observed_durations": durations,
    }


def ci_durations():
    """Wall-clock durations of recent CI runs on main, via the GitHub CLI."""
    out = run(
        "gh", "run", "list", "--workflow", "ci.yml", "--branch", "main",
        "--limit", "20", "--json", "createdAt,updatedAt,conclusion",
    )
    if out is None:
        return {
            "available": False,
            "reason": "GitHub CLI unavailable or not authenticated",
            "command": "gh run list --workflow ci.yml --branch main --limit 20 --json createdAt,updatedAt",
        }
    try:
        runs = json.loads(out)
    except json.JSONDecodeError:
        return {"available": False, "reason": "unparseable gh output"}

    from datetime import datetime

    minutes = []
    for entry in runs:
        if entry.get("conclusion") != "success":
            continue
        try:
            start = datetime.fromisoformat(entry["createdAt"].replace("Z", "+00:00"))
            end = datetime.fromisoformat(entry["updatedAt"].replace("Z", "+00:00"))
        except (KeyError, ValueError):
            continue
        minutes.append((end - start).total_seconds() / 60.0)
    if not minutes:
        return {"available": False, "reason": "no successful runs returned"}
    minutes.sort()
    return {
        "available": True,
        "successful_runs_sampled": len(minutes),
        "median_minutes": round(minutes[len(minutes) // 2], 1),
        "min_minutes": round(minutes[0], 1),
        "max_minutes": round(minutes[-1], 1),
        "note": "whole-workflow wall clock including queueing, not per-job CPU time",
    }


def collect_vendored_reference():
    """Reference material present in a working copy but absent from the repository.

    `third_party/` holds the normative SACM sources every conformance claim is
    checked against, but it is gitignored and fetched by script, so it appears in
    no tracked-file count. Reporting it only as "vendored" alongside the
    submodules would imply it sits in the tree; reporting nothing would hide a
    dependency the build and the conformance work both rely on.
    """
    directory = REPO / "third_party"
    ignored = run("git", "check-ignore", "third_party/") is not None
    present = directory.is_dir()
    files = sorted(p.name for p in directory.rglob("*") if p.is_file()) if present else []
    return {
        "path": "third_party/",
        "tracked": False,
        "gitignored": ignored,
        "present_in_this_working_copy": present,
        "file_count_on_disk": len(files),
        "fetch_command": "bash scripts/fetch-sacm23-references.sh",
        "note": (
            "Normative SACM 2.3 specification and machine-readable model. Not committed, "
            "so it contributes to no file or line count in this report."
        ),
    }


def collect_submodules():
    out = run("git", "submodule", "status")
    if out is None:
        return {"available": False, "reason": "git submodule status failed"}
    modules = []
    for line in out.splitlines():
        parts = line.strip().split()
        if len(parts) >= 2:
            modules.append({"commit": parts[0].lstrip("+-U"), "path": parts[1]})
    return {
        "available": True,
        "count": len(modules),
        "modules": modules,
        "note": "Submodule contents are upstream code and are excluded from all line counts.",
    }


# --------------------------------------------------------------------------
# Report rendering
# --------------------------------------------------------------------------


def table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join(["---"] * len(headers)) + "|"]
    for row in rows:
        out.append("| " + " | ".join(str(c) for c in row) + " |")
    return "\n".join(out)


def render_markdown(data):
    parts = []
    identity = data["identity"]
    parts.append("### Snapshot identity\n")
    parts.append(table(
        ["Field", "Value"],
        [
            ["Base commit", f"`{identity['base_commit']}`"],
            ["HEAD at collection", f"`{identity['commit']}`"],
            ["HEAD date", identity["commit_date"]],
            ["First commit", identity["first_commit_date"]],
            ["Commits in history", identity["total_commits"]],
        ],
    ))

    size = data["code_size"]
    parts.append("\n### Size by category\n")
    labels = {key: label for key, label, _ in CLASSIFICATION}
    parts.append(table(
        ["Category", "Files", "Physical lines", "Code lines"],
        [
            [labels.get(k, k), v["files"], v["physical_lines"] or "—", v["code_lines"] or "—"]
            for k, v in size["by_category"].items()
        ],
    ))

    parts.append("\n### Size by subsystem (code files only)\n")
    parts.append(table(
        ["Subsystem", "Files", "Physical lines", "Code lines"],
        [[k, v["files"], v["physical_lines"], v["code_lines"]] for k, v in size["by_subsystem"].items()],
    ))

    tests = data["tests"]
    parts.append("\n### Tests\n")
    ctest = tests["ctest_registered"]
    parts.append(table(
        ["Measure", "Value"],
        [
            ["Test source files", tests["test_source_files"]],
            ["gtest cases", tests["test_cases"]],
            ["gtest suites", tests["test_suites"]],
            [
                "CTest registered tests",
                ctest["count"] if ctest["available"] else f"unavailable — {ctest['reason']}",
            ],
        ],
    ))

    parts.append("\n### Test attribution by subsystem (overlapping — do not sum)\n")
    parts.append(table(
        ["Subsystem", "Test files including it", "gtest cases in those files"],
        [[k, v["files"], v["cases"]] for k, v in tests["cases_by_subsystem_overlapping"].items()],
    ))

    if tests["ambiguous_include_prefixes"]:
        parts.append("\n### Ambiguous include prefixes\n")
        parts.append(table(
            ["Prefix", "Served by"],
            [
                [f"`{prefix}`", ", ".join(f"`{o}`" for o in owners)]
                for prefix, owners in tests["ambiguous_include_prefixes"].items()
            ],
        ))

    gates = data["layer_gates"]
    parts.append("\n### Architecture gates\n")
    parts.append(table(
        ["Measure", "Value"],
        [
            ["Layers checked", len(gates.get("layers_checked", []))],
            ["Explicit exceptions", gates.get("exception_count", "—")],
            ["Enforcement point", gates.get("enforced_at", "—")],
        ],
    ))
    if gates.get("exceptions"):
        parts.append("\nCurrent exceptions:\n")
        parts.append("\n".join(f"- `{e}`" for e in gates["exceptions"]))

    matrices = data["matrices"]
    sacm = matrices.get("sacm_conformance", {})
    if sacm.get("available"):
        parts.append("\n### SACM 2.3 conformance matrix\n")
        parts.append(table(
            ["Status", "Rows"],
            [[f"`{k}`", v] for k, v in sacm["by_status"].items()] + [["**Total**", sacm["rows"]]],
        ))
    capability = matrices.get("capability", {})
    if capability.get("available"):
        parts.append("\n### Capability matrix\n")
        parts.append(table(
            ["Status", "Rows"],
            [[f"`{k}`", v] for k, v in capability["by_status"].items()] + [["**Total**", capability["rows"]]],
        ))

    docs = data["documentation"]
    parts.append("\n### Documentation\n")
    parts.append(table(
        ["Measure", "Value"],
        [
            ["Markdown files tracked", docs["markdown_files"]],
            ["Broken relative links", docs["broken_link_count"]],
            ["Pages in no nav and linked from nowhere", docs["orphan_page_count"]],
        ],
    ))

    hotspots = data["hotspots"]
    parts.append("\n### Ten largest production files\n")
    parts.append(table(
        ["File", "Physical lines"],
        [[f"`{h['path']}`", h["physical_lines"]] for h in hotspots["largest_production_files"][:10]],
    ))
    parts.append("\n### Ten most-changed production files\n")
    parts.append(table(
        ["File", "Commits"],
        [[f"`{h['path']}`", h["commits"]] for h in hotspots["most_changed_production_files"][:10]],
    ))

    coverage = data["build_and_analysis"].get("coverage", {})
    if coverage.get("available"):
        parts.append("\n### Coverage by scope\n")
        parts.append(table(
            ["Scope", "Lines", "Functions", "Branches", "Conditions (MC/DC)"],
            [
                [
                    scope,
                    f"{m['lines']['percent']}%",
                    f"{m['functions']['percent']}%",
                    f"{m['branches']['percent']}%",
                    f"{m['conditions']['percent']}%",
                ]
                for scope, m in coverage["by_scope"].items()
            ],
        ))

    analysis = data["build_and_analysis"]
    parts.append("\n### Quality tooling present\n")
    parts.append(table(
        ["Tool", "Configured"],
        [[name, "yes" if present else "no"] for name, present in analysis["quality_tooling_present"].items()]
        + [["explicit warning level / `-Werror`", "yes" if analysis["warnings_as_errors"] else "no"]],
    ))

    root = data["root_surface"]
    parts.append("\n### Repository root\n")
    parts.append(table(
        ["Measure", "Value"],
        [
            ["Tracked files in root", root["tracked_root_files"]],
            ["Committed build/scratch artifacts", root["committed_artifact_candidate_count"]],
        ],
    ))
    if root["committed_artifact_candidates"]:
        parts.append("\n".join(f"- `{name}`" for name in root["committed_artifact_candidates"]))

    ai = data["ai_assets"]
    parts.append("\n### AI development assets\n")
    parts.append(table(
        ["Measure", "Value"],
        [
            ["Claude agent definitions", len(ai["claude_agents"])],
            ["Codex agent definitions", len(ai["codex_agents"])],
            ["Roles defined in both (hand-duplicated)", len(ai["duplicated_agent_roles"])],
            ["Skills", len(ai["skills"])],
        ],
    ))

    return "\n".join(parts) + "\n"


# --------------------------------------------------------------------------


def collect():
    files = tracked_files()
    data = {
        "schema": 1,
        "generator": "tools/quality/collect_baseline.py",
        "identity": collect_identity(),
        "code_size": collect_code_size(files),
        "tests": collect_tests(files),
        "hotspots": collect_hotspots(files),
        "layer_gates": collect_layer_gates(),
        "matrices": collect_matrices(),
        "documentation": collect_documentation(files),
        "root_surface": collect_root_surface(files),
        "ai_assets": collect_ai_assets(),
        "build_and_analysis": collect_build_and_analysis(),
        "ci": collect_ci_matrix(),
        "submodules": collect_submodules(),
        "vendored_reference": collect_vendored_reference(),
    }

    # Coverage describes this tree only if nothing compiled has moved since the run
    # that produced it. Comparing SHAs would answer a narrower and less useful
    # question -- a commit touching only documentation cannot invalidate a coverage
    # figure, but it would make an equality check say otherwise. Diff the working
    # tree against the run's commit and report which compiled files actually differ.
    coverage = data["build_and_analysis"].get("coverage", {})
    if coverage.get("available") and coverage.get("run_commit"):
        diff = run("git", "diff", "--name-only", coverage["run_commit"])
        changed = []
        if diff is not None:
            changed = sorted(
                path.strip()
                for path in diff.splitlines()
                if path.strip() and Path(path.strip()).suffix in CODE_SUFFIXES
            )
        coverage["compiled_files_changed_since_run"] = changed
        coverage["describes_current_tree"] = not changed

    return data


def volatile_free(data):
    """Strip fields that change without the repository content changing.

    CI durations and coverage come from the GitHub API, the CTest count needs a
    build tree, the dirty count reflects uncommitted edits, and the branch name is
    a property of the checkout rather than the tree — the same commit examined
    from two branches must not read as stale. None of these describes committed
    content, so `--check` must not fail on them.
    """
    copy = json.loads(json.dumps(data))
    copy.get("ci", {}).pop("observed_durations", None)
    copy.get("identity", {}).pop("working_tree_dirty_entries", None)
    copy.get("identity", {}).pop("branch", None)
    copy.get("tests", {}).pop("ctest_registered", None)
    copy.get("build_and_analysis", {}).pop("coverage", None)
    # Whether the untracked reference material has been fetched is a property of
    # this working copy, not of the committed tree.
    copy.get("vendored_reference", {}).pop("present_in_this_working_copy", None)
    copy.get("vendored_reference", {}).pop("file_count_on_disk", None)
    return copy


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json-only", action="store_true", help="write the JSON snapshot, print nothing")
    parser.add_argument("--markdown-only", action="store_true", help="print markdown, do not write the JSON")
    parser.add_argument("--check", action="store_true", help="exit non-zero if the committed JSON is stale")
    args = parser.parse_args()

    data = collect()

    if args.check:
        if not JSON_PATH.is_file():
            print(f"missing {JSON_PATH.relative_to(REPO).as_posix()}; run tools/quality/collect_baseline.py")
            return 1
        committed = json.loads(JSON_PATH.read_text(encoding="utf-8"))
        if volatile_free(committed) != volatile_free(data):
            print(
                "repository-baseline.json is out of date.\n"
                "Regenerate it with: python tools/quality/collect_baseline.py"
            )
            return 1
        print("repository-baseline.json is up to date")
        return 0

    if not args.markdown_only:
        JSON_PATH.parent.mkdir(parents=True, exist_ok=True)
        JSON_PATH.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    if not args.json_only:
        sys.stdout.reconfigure(encoding="utf-8")
        print(render_markdown(data))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
