#!/usr/bin/env python3
"""Extract translatable msgids from Assurance Forge C++ source.

Scans src/ for AF_TR("..."), AF_TR_CTX("ctx","msg"), and
ui::i18n::tr/trc/trn/trf/trnf("...") calls. Handles single-line and
multi-line adjacent-string-literal concatenation. Outputs a sorted
list of unique msgids to stdout.

Usage:
    python extract_msgids.py [src_root]
"""

import ast
import os
import re
import sys
from pathlib import Path


# Match AF_TR( / AF_TR_CTX( / ui::i18n::tr( / trc( / trn( / trf( / trnf(
# We look at "(" position then consume balanced parens to capture the args.
CALL_RE = re.compile(
    r"\b(?:AF_TR_CTX|AF_TR|ui::i18n::trnf|ui::i18n::trcf|ui::i18n::trf|ui::i18n::trn|ui::i18n::trc|ui::i18n::tr)\s*\("
)


def find_call_arg_block(text, open_paren_idx):
    """Return the substring between matched parens after open_paren_idx (which
    points at '('). Returns (inner, end_idx) or (None, None)."""
    depth = 0
    i = open_paren_idx
    in_str = False
    esc = False
    while i < len(text):
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return text[open_paren_idx + 1:i], i
        i += 1
    return None, None


# Match adjacent quoted-string literals (C++ concatenation), tolerating
# whitespace and newlines.
STRING_LITERAL_RE = re.compile(r'"((?:[^"\\]|\\.)*)"', re.DOTALL)


def join_adjacent_literals(text):
    """Find all quoted literals in text and concatenate adjacent ones.

    Returns a list of concatenated string values, in order. Two literals
    are considered adjacent when only whitespace (and no other token)
    separates them.
    """
    results = []
    i = 0
    current = None  # accumulated string, or None
    last_end = -1
    pos = 0
    while pos < len(text):
        m = STRING_LITERAL_RE.search(text, pos)
        if not m:
            if current is not None:
                results.append(current)
                current = None
            break
        between = text[last_end:m.start()] if last_end >= 0 else ""
        is_adjacent = current is not None and between.strip() == ""
        try:
            value = ast.literal_eval('"' + m.group(1) + '"')
        except Exception:
            value = m.group(1)
        if is_adjacent:
            current += value
        else:
            if current is not None:
                results.append(current)
            current = value
        last_end = m.end()
        pos = m.end()
    if current is not None:
        results.append(current)
    return results


def extract_from_file(path):
    msgids = []
    text = path.read_text(encoding="utf-8")
    for m in CALL_RE.finditer(text):
        # CALL_RE allows whitespace before "(", so normalise before suffix checks.
        call_name = re.sub(r"\s+", "", m.group(0))
        open_paren = m.end() - 1
        inner, end = find_call_arg_block(text, open_paren)
        if inner is None:
            continue
        literals = join_adjacent_literals(inner)
        if not literals:
            continue
        # AF_TR_CTX(ctx, msg)  -> take 2nd literal as msgid (and remember ctx)
        # trc / trcf(ctx, msg, ...) -> same
        # trn / trnf(sing, plur, count, ...) -> singular and plural
        # AF_TR(msg) / tr(msg) / trf(msg, ...) -> first literal
        if "AF_TR_CTX" in call_name or call_name.endswith("trc(") or call_name.endswith("trcf("):
            if len(literals) >= 2:
                msgids.append(("ctx", literals[0], literals[1]))
        elif call_name.endswith("trn(") or call_name.endswith("trnf("):
            if len(literals) >= 2:
                msgids.append(("plural", literals[0], literals[1]))
        else:
            msgids.append(("plain", literals[0], None))
    return msgids


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else Path("src")
    plain = set()
    ctx_pairs = set()  # (ctx, msgid)
    plural_pairs = set()  # (singular, plural)
    for path in root.rglob("*.cpp"):
        for kind, a, b in extract_from_file(path):
            if kind == "plain":
                plain.add(a)
            elif kind == "ctx":
                ctx_pairs.add((a, b))
            elif kind == "plural":
                plural_pairs.add((a, b))

    print(f"# {len(plain)} plain, {len(ctx_pairs)} context, {len(plural_pairs)} plural")
    for s in sorted(plain):
        print(f"PLAIN\t{s!r}")
    for ctx, msg in sorted(ctx_pairs):
        print(f"CTX\t{ctx!r}\t{msg!r}")
    for sing, plur in sorted(plural_pairs):
        print(f"PLURAL\t{sing!r}\t{plur!r}")


if __name__ == "__main__":
    main(sys.argv)
