#!/usr/bin/env python3
"""Check that every CTest test carries a truthful label (issue #292).

Labels are how a developer selects a subset and how conformance evidence is
told apart from ordinary regression tests. Both uses are worthless if the
labels are wrong, and wrong labels are hard to notice: the build succeeds,
`ctest -N` counts every test, and only the labels are missing.

That is not hypothetical. `gtest_discover_tests(... PROPERTIES LABELS "a;b")`
looks correct and is not: GoogleTest.cmake expands the property list unquoted
into `set_tests_properties()`, so PROPERTIES pairs `LABELS=a` and drops `b`
without a word. It took a JSON dump of the test list to see it.

Three things are checked, all mechanical:

1. Every test has at least one label. An unlabelled test is invisible to every
   `ctest -L` selection, so it silently stops being run by anyone using them.

2. No label contains a semicolon. That is the signature of the expansion bug
   above -- "app;contract;slow" arriving as one label rather than three. This
   check caught the same mistake a second time, in its other form: escaping the
   list as "app\\;conformance" produced two labels on CMake 4.3 and one combined
   label on the CI runners, so the escape was version-dependent and the CI
   failure was the gate doing its job.

   Tests discovered by gtest therefore carry ONE compound label, `app.conformance`
   rather than `app` plus `conformance`. `ctest -L` matches labels as a regular
   expression, so the selection is identical and nothing has to survive a CMake
   list expansion.

3. `conformance` means exactly "the name embeds a requirement id", in BOTH
   directions. A test whose name carries an id must be labelled, or the
   evidence set is incomplete; and a labelled test must carry an id, or the
   evidence set is padded. Either way the label stops meaning anything.

Usage:
    python tools/repo/check_ctest_labels.py --build-dir build --config Release
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys

# A requirement id as the matrices spell it in a test name, e.g.
# SACM23_RT_001_..., SACM23_LIB_002_..., GSN3_CORE_002_...
REQUIREMENT_ID = re.compile(r"\b(SACM23|GSN3)_[A-Z]+_\d+")

# Tests that are conformance evidence but cannot say so in their name, because
# the name is fixed by something else. Each needs a reason; this is not a
# convenience list.
CONFORMANCE_WITHOUT_ID_IN_NAME = {
    # SACM23-CLI-001 rests on these four. They are add_test() invocations of the
    # real sacm_cli binary rather than gtest cases, so the requirement lives in
    # the matrix row and the CMake comment, not in a gtest name.
    "SacmCliVersionSmoke",
    "SacmCliRoundtripStrict",
    "SacmCliValidateRejectsDuplicateIds",
    "SacmCliValidateReportsDuplicateIdDiagnostic",
}


def load_tests(build_dir: str, config: str) -> list[dict]:
    ctest = shutil.which("ctest")
    if not ctest:
        print("error: ctest not found on PATH", file=sys.stderr)
        raise SystemExit(2)
    # Bytes, then an explicit UTF-8 decode. `text=True` would decode using the
    # platform locale, which on a Windows console is a legacy code page -- and
    # CTest emits UTF-8 regardless, so a non-ASCII test name would either raise
    # or arrive mangled.
    #
    # `utf-8-sig` consumes the byte-order mark CTest writes on Windows. Naming
    # the codec beats stripping the character, which puts an invisible BOM in
    # this file for a reader to trip over.
    result = subprocess.run(
        [ctest, "--test-dir", build_dir, "-C", config, "--show-only=json-v1"],
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace")
        print(f"error: ctest --show-only failed:\n{detail}", file=sys.stderr)
        raise SystemExit(2)
    return json.loads(result.stdout.decode("utf-8-sig"))["tests"]


def labels_of(test: dict) -> list[str]:
    collected: list[str] = []
    for prop in test.get("properties", []):
        if prop["name"] != "LABELS":
            continue
        value = prop["value"]
        collected += value if isinstance(value, list) else [value]
    return collected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()

    tests = load_tests(args.build_dir, args.config)
    if not tests:
        print("error: ctest reported no tests; refusing to call that a pass", file=sys.stderr)
        return 2

    # Non-vacuity. Test discovery runs the executables, so in a tree that has
    # been configured but not built, CTest reports only the add_test() entries --
    # every one of them correctly labelled. Everything below would pass while
    # having inspected none of the 1,200 gtest cases this exists to check.
    #
    # Both gtest executables must therefore have contributed. This is a
    # structural check rather than a threshold on the count, so it does not go
    # stale as tests are added.
    contributed = {label for test in tests for label in labels_of(test)}
    for family in ("app", "library"):
        if not any(label.startswith(family) for label in contributed):
            print(
                f"error: no test carries a `{family}` label, so the {family} test executable\n"
                f"       contributed nothing to the list. That is what a configured but\n"
                f"       unbuilt tree looks like; build first rather than trusting this pass.",
                file=sys.stderr,
            )
            return 2

    unlabelled: list[str] = []
    combined: list[str] = []
    missing_conformance: list[str] = []
    unearned_conformance: list[str] = []

    for test in tests:
        name = test["name"]
        labels = labels_of(test)
        if not labels:
            unlabelled.append(name)
            continue
        for label in labels:
            if ";" in label:
                combined.append(f"{name}: {label!r}")

        has_id = bool(REQUIREMENT_ID.search(name))
        # Substring, not equality: a gtest-discovered test carries the compound
        # label `app.conformance`, and `ctest -L conformance` selects it by the
        # same regex match this mirrors.
        is_conformance = any("conformance" in label for label in labels)
        if has_id and not is_conformance:
            missing_conformance.append(name)
        if is_conformance and not has_id and name not in CONFORMANCE_WITHOUT_ID_IN_NAME:
            unearned_conformance.append(name)

    problems = 0
    for title, offenders, explanation in (
        (
            "tests with no label at all",
            unlabelled,
            "An unlabelled test is invisible to every `ctest -L` selection.",
        ),
        (
            "labels containing a semicolon",
            combined,
            "A list arrived as one string. gtest_discover_tests cannot carry a "
            'multi-value LABELS portably: use one compound label ("app.conformance"), '
            "which `ctest -L` selects by regex either way.",
        ),
        (
            "requirement-id tests missing the `conformance` label",
            missing_conformance,
            "The conformance evidence set is incomplete.",
        ),
        (
            "tests labelled `conformance` with no requirement id in the name",
            unearned_conformance,
            "The conformance evidence set is padded. Add a reason to "
            "CONFORMANCE_WITHOUT_ID_IN_NAME if the name genuinely cannot carry the id.",
        ),
    ):
        if not offenders:
            continue
        problems += len(offenders)
        print(f"\n{len(offenders)} {title}:", file=sys.stderr)
        for offender in sorted(offenders)[:25]:
            print(f"  {offender}", file=sys.stderr)
        if len(offenders) > 25:
            print(f"  ... and {len(offenders) - 25} more", file=sys.stderr)
        print(f"  -> {explanation}", file=sys.stderr)

    if problems:
        return 1

    conformance = sum(1 for test in tests if any("conformance" in l for l in labels_of(test)))
    print(f"{len(tests)} tests, all labelled; {conformance} carry conformance evidence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
