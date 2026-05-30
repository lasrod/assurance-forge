#!/usr/bin/env python3
"""Compile a gettext .po catalog into a binary .mo file.

This is a dependency-free stand-in for `msgfmt` so the build never requires
GNU gettext tools to be installed. It supports msgctxt, msgid_plural and
msgstr[n]; untranslated entries (empty msgstr) are omitted, matching msgfmt.

Usage:
    python compile_po.py <input.po> <output.mo>
"""

import ast
import struct
import sys


def _unquote(line):
    # A .po quoted string; reuse Python's parser for escape handling.
    return ast.literal_eval(line.strip())


def parse_po(text):
    messages = {}
    ctxt = None
    msgid = None
    msgid_plural = None
    plurals = {}
    current = None  # one of: 'ctxt', 'msgid', 'msgid_plural', ('msgstr', index)

    def flush():
        nonlocal ctxt, msgid, msgid_plural, plurals
        if msgid is not None:
            # Context applies to plural keys too: the gettext key for a
            # context+plural entry is "ctxt\x04singular\x00plural".
            base_key = msgid if ctxt is None else ctxt + "\x04" + msgid
            if msgid_plural is not None:
                forms = [plurals.get(i, "") for i in range(max(plurals) + 1)] if plurals else [""]
                key = base_key + "\x00" + msgid_plural
                value = "\x00".join(forms)
            else:
                key = base_key
                value = plurals.get(0, "")
            # Keep the header (empty msgid); skip other untranslated entries.
            if msgid == "" or value != "":
                messages[key] = value
        ctxt = None
        msgid = None
        msgid_plural = None
        plurals = {}

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            if not line:
                continue
            continue
        if line.startswith("msgctxt "):
            flush()
            current = "ctxt"
            ctxt = _unquote(line[len("msgctxt "):])
        elif line.startswith("msgid_plural "):
            current = "msgid_plural"
            msgid_plural = _unquote(line[len("msgid_plural "):])
        elif line.startswith("msgid "):
            if current in ("msgstr", "msgid_plural") or (isinstance(current, tuple)):
                flush()
            current = "msgid"
            msgid = _unquote(line[len("msgid "):])
        elif line.startswith("msgstr["):
            close = line.index("]")
            index = int(line[len("msgstr["):close])
            current = ("msgstr", index)
            plurals[index] = _unquote(line[close + 2:])
        elif line.startswith("msgstr "):
            current = ("msgstr", 0)
            plurals[0] = _unquote(line[len("msgstr "):])
        elif line.startswith('"'):
            piece = _unquote(line)
            if current == "ctxt":
                ctxt += piece
            elif current == "msgid":
                msgid += piece
            elif current == "msgid_plural":
                msgid_plural += piece
            elif isinstance(current, tuple):
                plurals[current[1]] += piece
    flush()
    return messages


def generate_mo(messages):
    keys = sorted(messages.keys())
    offsets = []
    ids = b""
    strs = b""
    for key in keys:
        value = messages[key]
        key_bytes = key.encode("utf-8")
        value_bytes = value.encode("utf-8")
        offsets.append((len(ids), len(key_bytes), len(strs), len(value_bytes)))
        ids += key_bytes + b"\x00"
        strs += value_bytes + b"\x00"

    count = len(keys)
    key_table_offset = 7 * 4
    value_table_offset = key_table_offset + count * 8
    ids_start = value_table_offset + count * 8
    strs_start = ids_start + len(ids)

    key_table = b""
    value_table = b""
    for id_off, id_len, str_off, str_len in offsets:
        key_table += struct.pack("<II", id_len, ids_start + id_off)
        value_table += struct.pack("<II", str_len, strs_start + str_off)

    header = struct.pack(
        "<IIIIIII",
        0x950412DE,  # magic
        0,           # revision
        count,       # number of strings
        key_table_offset,
        value_table_offset,
        0,           # hash table size
        0,           # hash table offset
    )
    return header + key_table + value_table + ids + strs


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: compile_po.py <input.po> <output.mo>\n")
        return 2
    with open(argv[1], "r", encoding="utf-8") as f:
        text = f.read()
    messages = parse_po(text)
    data = generate_mo(messages)
    with open(argv[2], "wb") as f:
        f.write(data)
    sys.stderr.write(f"Wrote {argv[2]} ({len(messages)} entries)\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
