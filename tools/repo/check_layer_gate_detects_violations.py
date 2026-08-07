#!/usr/bin/env python3
"""Prove the layer gate still rejects the dependencies it claims to reject.

`cmake/check_layer_gates.cmake` is the only mechanical guard on the architecture,
and it passes on every clean build -- which is exactly the state a silently broken
gate would also produce. A regex that stops matching, a layer dropped from
`_AF_LAYERS`, or an allow-list entry widened past its intent would each turn the
gate green forever, and nothing downstream would notice.

So this feeds it code it must reject, and fails if it does not.

The fixtures are synthetic trees under a temporary directory, never the real
source tree: a test that requires a genuine violation to exist in `src/` would
have to leave one there.

Usage:
    python tools/repo/check_layer_gate_detects_violations.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GATE = REPO / "cmake/check_layer_gates.cmake"

# (label, layer directory, header file, contents, must_fail)
CASES = [
    ("ui must not include app", "ui", "panel.h", '#include "app/runtime.h"\n', True),
    ("ui must not include ai", "ui", "panel.h", '#include "ai/ai_types.h"\n', True),
    ("core must not include ui", "core", "service.h", '#include "ui/theme.h"\n', True),
    ("core must not include app", "core", "service.h", '#include "app/runtime.h"\n', True),
    ("parser must not include sacm", "parser", "reader.h", '#include "sacm/sacm_model.h"\n', True),
    ("bridge must not include mcp", "bridge", "endpoint.h", '#include "mcp/server.h"\n', True),
    ("agent must not include mcp", "agent", "ops.h", '#include "mcp/server.h"\n', True),
    ("mcp must not include ui", "mcp", "server.h", '#include "ui/theme.h"\n', True),
    ("export must not include ai", "export", "svg.h", '#include "ai/ai_types.h"\n', True),
    # The gate must not fire on dependencies that are allowed, or it would be
    # useless in the other direction: a check that rejects everything teaches
    # people to bypass it.
    ("ui may include core", "ui", "panel.h", '#include "core/app_state.h"\n', False),
    ("app may include ui", "app", "runtime.h", '#include "ui/theme.h"\n', False),
    ("core may include parser", "core", "service.h", '#include "parser/xml_parser.h"\n', False),
    ("mcp may include agent", "mcp", "server.h", '#include "agent/operations.h"\n', False),
]


def run_gate(source_dir):
    """Run the gate standalone against a fixture tree.

    Returns (passed, output). The output is captured rather than inherited so
    that thirteen expected CMake errors do not bury the result, but it is kept
    and printed for any case that behaves unexpectedly -- a gate failure with no
    diagnostics is the hardest kind to debug from a CTest log.
    """
    result = subprocess.run(
        [
            "cmake",
            f"-DAF_SOURCE_DIR={source_dir}",
            f"-DAF_LIBS_SACM_DIR={source_dir}",
            "-P",
            str(GATE),
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def main():
    if shutil.which("cmake") is None:
        print("[1] SKIP: cmake is not on PATH, so the gate cannot be exercised")
        return 0
    if not GATE.is_file():
        print(f"[1] FAIL: {GATE} not found")
        return 1

    failures = []
    with tempfile.TemporaryDirectory() as temp:
        for label, layer, filename, contents, must_fail in CASES:
            root = Path(temp) / label.replace(" ", "_")
            (root / layer).mkdir(parents=True, exist_ok=True)
            (root / layer / filename).write_text(contents, encoding="utf-8")

            passed, output = run_gate(root)
            if must_fail and passed:
                failures.append((f"gate ACCEPTED a forbidden include: {label}", output))
            elif not must_fail and not passed:
                failures.append((f"gate REJECTED an allowed include: {label}", output))

    if failures:
        print(f"[1] FAIL: the layer gate no longer behaves as documented\n")
        for line, output in failures:
            print(f"  {line}")
            if output:
                for detail in output.splitlines():
                    print(f"      {detail}")
        print(
            "\nThe gate passing on a clean tree does not show that it works.\n"
            "Fix cmake/check_layer_gates.cmake, or update these cases if the\n"
            "architecture genuinely changed."
        )
        return 1

    rejected = sum(1 for case in CASES if case[4])
    allowed = len(CASES) - rejected
    print(f"[1] OK: layer gate rejects {rejected} forbidden dependencies and permits {allowed} allowed ones")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
