#!/usr/bin/env python3
"""Fail when a status-bar message in src/app is an untranslated literal (issue #252).

The status bar shows whatever `StatusMessageEvent{...}` carries, and it does no
translation of its own, so every message must be translated where it is set.
`src/app` was swept once and drifted back to about 220 raw literals without
anything noticing; this is the check that notices.

It flags a call whose argument contains a string literal that is not inside
AF_TR / AF_TR_NOOP / ui::i18n::tr* -- `"Added " + id` or `"Saved."`. An argument
with no literal at all (a variable, a value built elsewhere) is left alone: the
text came from somewhere this check cannot follow, and lower layers that cannot
translate are tracked separately.

Usage:
    python tools/i18n/check_status_messages.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO / "src/app"

# `SetStatus(state, <message>)`, `AppRuntime::SetStatus(<message>)` and
# `StatusMessageEvent{<message>}`. For SetStatus the message is the last
# argument, whichever overload it is.
CALL = re.compile(r"\b(?:SetStatus\(|StatusMessageEvent\{)")
TRANSLATING = re.compile(r"\b(?:AF_TR_NOOP|AF_TR_CTX|AF_TR|tr|trc|trn|trf|trcf|trnf)\s*\(")


def argument(text: str, start: int) -> str:
    """The call's argument text, up to the bracket that closes the call."""
    depth, i, in_string = 1, start, False
    while i < len(text):
        c = text[i]
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
            if depth == 0:
                return text[start:i]
        i += 1
    return text[start:]


def last_argument(args: str) -> str:
    """The text after the last top-level comma of an argument list."""
    depth, i, in_string, last = 0, 0, False, 0
    while i < len(args):
        c = args[i]
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        elif c == "," and depth == 0:
            last = i + 1
        i += 1
    return args[last:]


def untranslated_literal(arg: str) -> bool:
    """True when a string literal in `arg` is not inside a translating call."""
    covered = [False] * len(arg)
    for m in TRANSLATING.finditer(arg):
        inner = argument(arg, m.end())
        for i in range(m.start(), m.end() + len(inner) + 1):
            if i < len(covered):
                covered[i] = True
    for m in re.finditer(r'"(?:[^"\\]|\\.)*"', arg):
        if not covered[m.start()]:
            return True
    return False


def violations(root: Path = ROOT) -> list[str]:
    found = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        text = path.read_text(encoding="utf-8")
        for m in CALL.finditer(text):
            # A helper's own definition: `void SetStatus(AppRuntimeState& state, ...`.
            if text[max(0, m.start() - 12) : m.start()].rstrip().endswith("void"):
                continue
            arg = argument(text, m.end())
            if m.group(0).startswith("SetStatus"):
                arg = last_argument(arg)
            if untranslated_literal(arg):
                line = text.count("\n", 0, m.start()) + 1
                found.append(f"{path.relative_to(REPO).as_posix()}:{line}: {' '.join(arg.split())[:120]}")
    return found


def main() -> int:
    found = violations()
    if found:
        print(f"{len(found)} status message(s) in src/app reach the status bar untranslated:\n")
        for row in found:
            print(f"  {row}")
        print(
            "\n  -> Wrap the text: AF_TR(\"...\") for a fixed message, ui::i18n::trf(\"... {0}\", value)\n"
            "     for one with runtime parts. Then add the msgid to tools/i18n/regenerate_ja_po.py."
        )
        return 1
    print("OK: every literal status message in src/app is translated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
