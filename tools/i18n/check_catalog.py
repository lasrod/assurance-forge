#!/usr/bin/env python3
"""Internal-review / CI gate for the translation catalog. Two checks:

  1. Every msgid used in source (AF_TR / trf / trn / ...) exists in the .po.
     A miss means that string silently renders English in Japanese mode.
  2. The committed .mo is in sync with the .po (logical message maps match).
     A mismatch means someone edited the .po without recompiling the .mo, so
     the runtime catalog is stale.

Exits non-zero (and prints the offending entries) when either check fails, so
it works as a CTest test / CI step. Pure stdlib, no build artifacts needed.

Usage: python tools/i18n/check_catalog.py
"""

import ast
import importlib.util
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PO_PATH = REPO / "assets/locale/ja/LC_MESSAGES/assurance_forge.po"
MO_PATH = REPO / "assets/locale/ja/LC_MESSAGES/assurance_forge.mo"


def load_module(name, relpath):
    spec = importlib.util.spec_from_file_location(name, REPO / relpath)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_source(ex):
    plain, ctx, plural = set(), set(), set()
    exts = ("*.cpp", "*.h", "*.hpp", "*.cc", "*.cxx", "*.hh", "*.hxx")
    paths = []
    for pattern in exts:
        paths.extend((REPO / "src").rglob(pattern))
    for path in paths:
        for kind, a, b in ex.extract_from_file(path):
            if kind == "plain":
                plain.add(a)
            elif kind == "ctx":
                ctx.add((a, b))
            elif kind == "plural":
                plural.add((a, b))
    return plain, ctx, plural


_MSGID_RE = re.compile(r'^msgid "((?:[^"\\]|\\.)*)"', re.M)
_MSGID_PLURAL_RE = re.compile(r'^msgid_plural "((?:[^"\\]|\\.)*)"', re.M)
# A context entry is `msgctxt "ctx"` immediately followed by `msgid "id"`.
_MSGCTXT_RE = re.compile(
    r'^msgctxt "((?:[^"\\]|\\.)*)"\s*\nmsgid "((?:[^"\\]|\\.)*)"', re.M)


def load_po_keys():
    po = (REPO / "assets/locale/ja/LC_MESSAGES/assurance_forge.po").read_text(encoding="utf-8")
    decode = lambda s: ast.literal_eval('"' + s + '"')
    plain = {decode(m) for m in _MSGID_RE.findall(po)}
    plural = {decode(m) for m in _MSGID_PLURAL_RE.findall(po)}
    ctx = {(decode(c), decode(m)) for c, m in _MSGCTXT_RE.findall(po)}
    return plain, plural, ctx


def read_mo(path):
    """Read a gettext .mo into the same {key: value} map compile_po.py produces
    (keys may embed \\x04 context / \\x00 plural separators). Little-endian and
    big-endian magic are both handled."""
    data = path.read_bytes()
    if len(data) < 28:
        raise ValueError(f"{path} is too small to be a .mo file")
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic == 0x950412DE:
        endian = "<"
    elif magic == 0xDE120495:
        endian = ">"
    else:
        raise ValueError(f"{path} has a bad .mo magic ({magic:#x})")
    count, orig_off, trans_off = struct.unpack_from(endian + "III", data, 8)
    messages = {}
    for i in range(count):
        o_len, o_ptr = struct.unpack_from(endian + "II", data, orig_off + i * 8)
        t_len, t_ptr = struct.unpack_from(endian + "II", data, trans_off + i * 8)
        key = data[o_ptr:o_ptr + o_len].decode("utf-8")
        value = data[t_ptr:t_ptr + t_len].decode("utf-8")
        messages[key] = value
    return messages


def check_source_in_po(ex):
    src_plain, src_ctx, src_plural = extract_source(ex)
    po_plain, po_plural, po_ctx = load_po_keys()

    missing = sorted(s for s in src_plain if s not in po_plain)
    print(f"[1] source plain msgids: {len(src_plain)}  |  catalog plain: {len(po_plain)}")
    for s in missing:
        print("  MISSING from catalog: " + repr(s))
    plural_missing = [(sing, plur) for sing, plur in sorted(src_plural)
                      if sing not in po_plain or plur not in po_plural]
    for sing, plur in plural_missing:
        print("  MISSING plural pair: " + repr(sing) + " / " + repr(plur))
    # Context-qualified msgids (AF_TR_CTX / trc / trcf) must exist as a
    # matching msgctxt+msgid pair in the .po, or they render English at runtime.
    ctx_missing = [(ctx, msgid) for ctx, msgid in sorted(src_ctx) if (ctx, msgid) not in po_ctx]
    for ctx, msgid in ctx_missing:
        print("  MISSING context entry: msgctxt " + repr(ctx) + " msgid " + repr(msgid))
    ok = not missing and not plural_missing and not ctx_missing
    print(f"[1] {'OK' if ok else 'FAIL'}: all source msgids present in .po")
    return ok


# A real printf conversion spec (e.g. %s %d %zu %llu %.2f), but NOT "% Frame"
# (percent-space, which is a literal percent in a non-format label).
_PRINTF_RE = re.compile(r'%[-+0#]*[0-9]*\.?[0-9]*(?:hh|h|ll|l|z|j|t|L)?[sdiuxXofeEgGc]')


def check_no_printf_in_msgids(ex):
    """AF_TR / trc / trn msgids must not contain printf conversion specifiers.
    Those strings are passed to ImGui as format strings only via trf/trnf with
    positional {0}/{1} placeholders; a %s/%d in an AF_TR msgid means a call site
    is (mis)using a translated string as a printf format, which is fragile for
    translators and blocks placeholder reordering."""
    plain, ctx, plural = extract_source(ex)
    offenders = sorted(s for s in plain if _PRINTF_RE.search(s))
    offenders += sorted(f"{c}|{m}" for c, m in ctx if _PRINTF_RE.search(m))
    for sing, plur in sorted(plural):
        if _PRINTF_RE.search(sing) or _PRINTF_RE.search(plur):
            offenders.append(f"{sing} | {plur}")
    for s in offenders:
        print("  printf specifier in msgid (use trf/trnf with {0}): " + repr(s))
    ok = not offenders
    print(f"[3] {'OK' if ok else 'FAIL'}: no printf specifiers in translated msgids")
    return ok


def check_mo_matches_po(compile_po):
    expected = compile_po.parse_po(PO_PATH.read_text(encoding="utf-8"))
    actual = read_mo(MO_PATH)
    # Drop the empty-msgid header entry from both: compile_po keeps it, the .mo
    # also carries it, so they cancel — but guard in case either omits it.
    expected.pop("", None)
    actual.pop("", None)
    only_po = sorted(set(expected) - set(actual))
    only_mo = sorted(set(actual) - set(expected))
    changed = sorted(k for k in expected.keys() & actual.keys() if expected[k] != actual[k])
    for k in only_po:
        print("  in .po but not .mo (stale .mo — run regenerate_ja_po.py): " + repr(k))
    for k in only_mo:
        print("  in .mo but not .po (stale .mo — run regenerate_ja_po.py): " + repr(k))
    for k in changed:
        print("  translation differs between .po and .mo: " + repr(k))
    ok = not only_po and not only_mo and not changed
    print(f"[2] {'OK' if ok else 'FAIL'}: committed .mo matches .po ({len(actual)} entries)")
    return ok


def main():
    ex = load_module("ex", "tools/i18n/extract_msgids.py")
    compile_po = load_module("compile_po", "tools/i18n/compile_po.py")
    ok1 = check_source_in_po(ex)
    ok2 = check_no_printf_in_msgids(ex)
    ok3 = check_mo_matches_po(compile_po)
    return 0 if (ok1 and ok2 and ok3) else 1


if __name__ == "__main__":
    raise SystemExit(main())
