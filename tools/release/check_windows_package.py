#!/usr/bin/env python3
"""Check that an installed or unzipped Windows package can run on a clean machine.

A build that runs on the developer's machine can still fail on a tester's: the
exe may import a DLL that only exists here. Two did -- the VC++ runtime, which
the release zip never shipped, and zlib/libssh2 from Strawberry Perl, which curl
picked up from PATH. Neither shows up in any test, because the test machine has
both.

This reads the import tables of every exe and DLL in the package root and fails
unless each imported DLL is either part of Windows or shipped in the package.
It also checks the files the application looks for beside itself, and with
--run starts the MCP server's --version, which fails fast on a missing DLL.

Usage:
    python tools/release/check_windows_package.py <package-dir> [--run]
    python tools/release/check_windows_package.py --self-test
"""

import argparse
import struct
import subprocess
import sys
from pathlib import Path

# Files the executables look for beside themselves. A package missing one
# starts, then fails later: no fonts, no Japanese UI, no guideline review. The
# fonts are the ones src/app/app_ui_bootstrap.cpp loads, plus HelloImGui's.
REQUIRED_FILES = [
    "assurance-forge.exe",
    "assurance-forge-mcp.exe",
    "assets/app_settings/icon.png",
    "assets/fonts/NotoSansJP-Regular.otf",
    "assets/fonts/NotoSansJP-Bold.otf",
    # HelloImGui's default assets, merged into the build's assets folder rather
    # than living in ours: the UI font and the icon fonts. A package without
    # them starts fine and shows "?" for every icon.
    "assets/fonts/DroidSans.ttf",
    "assets/fonts/Font_Awesome_6_Free-Solid-900.otf",
    "assets/fonts/fontawesome-webfont.ttf",
    "assets/locale/ja/LC_MESSAGES/assurance_forge.mo",
    "data/sccg/dist/sccg.full.json",
]

# DLLs every supported Windows (10 and later) provides. Anything else must be
# in the package. Compared case-insensitively. Deliberately not "whatever is in
# System32": on a build machine that holds the VC++ runtime too, which is the
# very thing a tester's machine may lack.
WINDOWS_DLLS = {
    "advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll",
    "dbghelp.dll", "dwmapi.dll", "gdi32.dll", "imm32.dll", "iphlpapi.dll",
    "kernel32.dll", "normaliz.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "opengl32.dll", "secur32.dll", "setupapi.dll", "shcore.dll", "shell32.dll",
    "shlwapi.dll", "ucrtbase.dll", "user32.dll", "uxtheme.dll", "version.dll",
    "winmm.dll", "wldap32.dll", "ws2_32.dll",
}
# API sets resolve inside Windows (the Universal CRT among them).
WINDOWS_DLL_PREFIXES = ("api-ms-win-", "ext-ms-win-")


def _read_c_string(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("ascii")


def imported_dlls(path):
    """Names of the DLLs a PE file imports, normally or delay-loaded."""
    data = Path(path).read_bytes()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE file")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path}: no PE signature")
    coff = pe_offset + 4
    section_count = struct.unpack_from("<H", data, coff + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff + 16)[0]
    optional = coff + 20
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    sections = optional + optional_size

    def rva_to_offset(rva):
        for index in range(section_count):
            header = sections + index * 40
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, header + 8)
            if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
                return rva - virtual_address + raw_pointer
        raise ValueError(f"{path}: RVA {rva:#x} is in no section")

    names = []
    # Data directory 1 is the import table: 20-byte descriptors, name RVA at +12.
    # Data directory 13 is the delay-load table: 32-byte descriptors, name RVA at +4.
    for directory, descriptor_size, name_field in ((1, 20, 12), (13, 32, 4)):
        table_rva = struct.unpack_from("<I", data, directories + directory * 8)[0]
        if table_rva == 0:
            continue
        descriptor = rva_to_offset(table_rva)
        while True:
            fields = data[descriptor:descriptor + descriptor_size]
            if fields == b"\0" * descriptor_size:
                break
            name_rva = struct.unpack_from("<I", fields, name_field)[0]
            names.append(_read_c_string(data, rva_to_offset(name_rva)))
            descriptor += descriptor_size
    return names


def is_windows_dll(name):
    lowered = name.lower()
    return lowered in WINDOWS_DLLS or lowered.startswith(WINDOWS_DLL_PREFIXES)


def check_package(root, run):
    root = Path(root)
    problems = []

    for relative in REQUIRED_FILES:
        if not (root / relative).is_file():
            problems.append(f"missing {relative}")

    shipped = {entry.name.lower() for entry in root.iterdir() if entry.is_file()}
    # unins000.exe is Inno Setup's own uninstaller, written by the installer; its
    # imports are Inno Setup's business, not the package's.
    binaries = sorted(
        entry for entry in root.iterdir()
        if entry.suffix.lower() in (".exe", ".dll") and not entry.name.lower().startswith("unins")
    )
    for binary in binaries:
        for dll in imported_dlls(binary):
            if not is_windows_dll(dll) and dll.lower() not in shipped:
                problems.append(f"{binary.name} imports {dll}, which is neither part of Windows nor in the package")

    mcp = root / "assurance-forge-mcp.exe"
    if run and mcp.is_file():
        result = subprocess.run([str(mcp), "--version"], capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            problems.append(f"assurance-forge-mcp.exe --version exited {result.returncode:#x}: {result.stderr.strip()}")

    for problem in problems:
        print(f"FAIL: {problem}")
    if not problems:
        print(f"OK: {root} holds every required file and every import resolves "
              f"({len(binaries)} binaries checked)")
    return not problems


def run_self_test():
    """The import reader must find imports that are there, or it passes everything."""
    names = [name.lower() for name in imported_dlls(sys.executable)]
    if "kernel32.dll" not in names and not any(name.startswith("python") for name in names):
        print(f"FAIL: self-test read {names!r} from {sys.executable}; the import parser is broken")
        return False
    if is_windows_dll("vcruntime140.dll") or is_windows_dll("zlib1__.dll"):
        print("FAIL: self-test: a DLL a clean machine lacks is classed as part of Windows")
        return False
    print("OK: self-test")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("package", nargs="?", help="installed or unzipped package directory")
    parser.add_argument("--run", action="store_true", help="also start assurance-forge-mcp.exe --version")
    parser.add_argument("--self-test", action="store_true", help="check the checker, on this Python's own exe")
    args = parser.parse_args()
    if args.self_test:
        return 0 if run_self_test() else 1
    if not args.package:
        parser.error("a package directory is required")
    return 0 if check_package(args.package, args.run) else 1


if __name__ == "__main__":
    sys.exit(main())
