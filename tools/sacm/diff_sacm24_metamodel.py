#!/usr/bin/env python3
"""Class-level diff of the SACM 2.3 and SACM 2.4 Beta 1 normative metamodels.

Reads the two machine-readable OMG models and prints which classifiers were
removed, added and changed, with every feature's type and multiplicity. It is
the source of the class tables in docs/sacm/sacm-2.4-beta1-impact.md; re-run it
when OMG republishes the beta or issues the formal 2.4, and compare the output.

Fetch the inputs first:

    bash scripts/fetch-sacm23-references.sh
    bash scripts/fetch-sacm24-beta1-references.sh

With --inventory it also writes the committed inventory of the 2.4 Beta 1 model,
the baseline a later beta or the formal 2.4 is compared against:

    python tools/sacm/diff_sacm24_metamodel.py --inventory docs/sacm/sacm-2.4-beta1-metamodel-inventory.md

This is a study tool. It is not a CTest and nothing in the build reads it:
SACM 2.4 Beta 1 is informational, and SACM 2.3 stays the conformance target.
"""
import argparse
import collections
import pathlib
import sys
import xml.etree.ElementTree as ET

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OLD = REPO_ROOT / "third_party" / "sacm-2.3" / "SACM-2.3-ptc-22-03-13.xml"
DEFAULT_NEW = REPO_ROOT / "third_party" / "sacm-2.4-beta1" / "SACM-2.4-beta1-metamodel-ptc-26-05-28.xml"


def xmi_attr(element, name):
    for key, value in element.attrib.items():
        if key.endswith("}" + name):
            return value
    return element.get(name)


def local(tag):
    return tag.split("}")[-1]


def load(path):
    root = ET.parse(path).getroot()
    by_id = {}
    for element in root.iter():
        identifier = xmi_attr(element, "id")
        if identifier:
            by_id[identifier] = element
    parent = {child: element for element in root.iter() for child in element}
    classes = {}

    def package_path(element):
        names = []
        cursor = parent.get(element)
        while cursor is not None:
            if local(cursor.tag) == "packagedElement" and cursor.get("name"):
                names.append(cursor.get("name"))
            cursor = parent.get(cursor)
        return "/".join(reversed(names))

    def type_name(prop):
        ref = prop.get("type")
        if ref and ref in by_id:
            return (by_id[ref].get("name") or "").strip()
        for child in prop:
            if local(child.tag) == "type":
                href = child.get("href") or ""
                return href.split("#")[-1] or (xmi_attr(child, "idref") or "?")
        return ref or "?"

    def bound(prop, which, default):
        for child in prop:
            if local(child.tag) == which:
                return child.get("value", "0" if which == "lowerValue" else "*")
        return default

    for element in root.iter():
        kind = xmi_attr(element, "type") or ""
        if local(element.tag) != "packagedElement" or kind not in ("uml:Class", "uml:Enumeration", "uml:DataType"):
            continue
        name = (element.get("name") or "").strip()
        record = {
            "kind": kind,
            "package": package_path(element),
            "abstract": element.get("isAbstract") == "true",
            "supers": [],
            "features": {},
            "literals": [],
            "constraints": [],
        }
        for child in element:
            tag = local(child.tag)
            if tag == "generalization":
                general = child.get("general")
                if general in by_id:
                    record["supers"].append(by_id[general].get("name"))
            elif tag == "ownedAttribute":
                lower = bound(child, "lowerValue", "1")
                upper = bound(child, "upperValue", "1")
                flags = ""
                if child.get("isDerived") == "true":
                    flags += " derived"
                if child.get("aggregation") == "composite":
                    flags += " composite"
                if child.get("isReadOnly") == "true":
                    flags += " readonly"
                record["features"][child.get("name")] = f"{type_name(child)}[{lower}..{upper}]{flags}"
            elif tag == "ownedLiteral":
                record["literals"].append(child.get("name"))
            elif tag == "ownedRule":
                record["constraints"].append(child.get("name"))
        classes[name] = record
    return classes


parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--old", type=pathlib.Path, default=DEFAULT_OLD, help="SACM 2.3 normative model (ptc/22-03-13)")
parser.add_argument("--new", type=pathlib.Path, default=DEFAULT_NEW, help="SACM 2.4 Beta 1 metamodel (ptc/26-05-28)")
parser.add_argument(
    "--inventory",
    type=pathlib.Path,
    help="also write the 2.4 Beta 1 classifier inventory as markdown (docs/sacm/sacm-2.4-beta1-metamodel-inventory.md)",
)
arguments = parser.parse_args()
for required in (arguments.old, arguments.new):
    if not required.is_file():
        sys.exit(f"error: {required} not found; run the fetch scripts named in this tool's help")
sys.stdout.reconfigure(encoding="utf-8")

old = load(arguments.old)
new = load(arguments.new)
normative_new = {k: v for k, v in new.items() if "NonNormative" not in v["package"]}

print(f"2.3 classifiers: {len(old)}   2.4 classifiers: {len(new)} (outside NonNormative: {len(normative_new)})")
print("\n== 2.4 packages ==")
packages = collections.defaultdict(list)
for name, record in new.items():
    packages[record["package"]].append(name)
for package, names in sorted(packages.items()):
    print(f"  {package}: {', '.join(sorted(n or '?' for n in names))}")

print("\n== 2.3 packages ==")
packages = collections.defaultdict(list)
for name, record in old.items():
    packages[record["package"]].append(name)
for package, names in sorted(packages.items()):
    print(f"  {package}: {', '.join(sorted(n or '?' for n in names))}")

print("\n== Removed in 2.4 (present in 2.3, absent in 2.4) ==")
for name in sorted(set(old) - set(new)):
    print(f"  {name}  [{old[name]['package']}]  supers={old[name]['supers']}")

print("\n== Added in 2.4 ==")
for name in sorted(set(new) - set(old), key=lambda n: (new[n]["package"], n or "")):
    record = new[name]
    print(f"  {name}  [{record['package']}] {'abstract ' if record['abstract'] else ''}supers={record['supers']}")
    for feature, signature in record["features"].items():
        print(f"      {feature}: {signature}")
    if record["literals"]:
        print(f"      literals: {record['literals']}")

print("\n== Changed (same name in both) ==")
for name in sorted(set(old) & set(new)):
    before, after = old[name], new[name]
    lines = []
    if before["package"].split("/")[-1] != after["package"].split("/")[-1]:
        lines.append(f"package: {before['package']} -> {after['package']}")
    if before["abstract"] != after["abstract"]:
        lines.append(f"abstract: {before['abstract']} -> {after['abstract']}")
    if sorted(before["supers"]) != sorted(after["supers"]):
        lines.append(f"supers: {before['supers']} -> {after['supers']}")
    if before["literals"] != after["literals"]:
        lines.append(f"literals: {before['literals']} -> {after['literals']}")
    for feature in sorted(set(before["features"]) | set(after["features"]), key=str):
        was = before["features"].get(feature)
        now = after["features"].get(feature)
        if was != now:
            lines.append(f"{feature}: {was} -> {now}")
    if lines:
        print(f"  {name}")
        for line in lines:
            print(f"      {line}")
    else:
        print(f"  {name}  (unchanged)")


def write_inventory(path, classifiers, source_name):
    lines = [
        "# SACM 2.4 Beta 1 metamodel inventory",
        "",
        "Generated by `tools/sacm/diff_sacm24_metamodel.py --inventory` from the OMG machine-readable model "
        f"`{source_name}` (OMG document ptc/26-05-28, fetched by `scripts/fetch-sacm24-beta1-references.sh`). "
        "Do not edit by hand; regenerate after a reference update.",
        "",
        "**This is a beta, recorded as published.** Nothing in `libs/sacm` implements it, and no row here is a "
        "conformance requirement. Names are copied verbatim apart from surrounding whitespace, so the model's own "
        "misspellings appear as they are. The page exists so that a later beta or the formal SACM 2.4 can be "
        "compared against a fixed baseline; see the [impact analysis](sacm-2.4-beta1-impact.md).",
        "",
        "Features are a class's own; inherited ones are listed on the superclass. `composite` marks containment.",
        "",
    ]
    by_package = collections.defaultdict(list)
    for name, record in classifiers.items():
        by_package[record["package"]].append(name)
    lines += ["| Package | Classifiers |", "|---|---|"]
    for package in sorted(by_package):
        lines.append(f"| `{package}` | {len(by_package[package])} |")
    lines.append(f"| **Total** | **{len(classifiers)}** |")
    for package in sorted(by_package):
        lines += ["", f"## {package}", ""]
        for name in sorted(by_package[package]):
            record = classifiers[name]
            kind = record["kind"].split(":")[-1]
            if record["abstract"]:
                kind = "abstract " + kind
            heading = f"### {name}"
            lines += [heading, "", f"{kind}." + (f" Specializes: {', '.join(f'`{s}`' for s in record['supers'])}." if record["supers"] else "")]
            if record["literals"]:
                lines += ["", "Literals: " + ", ".join(f"`{literal}`" for literal in record["literals"]) + "."]
            if record["features"]:
                lines += ["", "| Feature | Type and multiplicity |", "|---|---|"]
                for feature, signature in record["features"].items():
                    lines.append(f"| `{feature}` | `{signature}` |")
            lines.append("")
    path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8", newline="\n")


if arguments.inventory:
    write_inventory(arguments.inventory, new, arguments.new.name)
    print(f"\nwrote {arguments.inventory}")
