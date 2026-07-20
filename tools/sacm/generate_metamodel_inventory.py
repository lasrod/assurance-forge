#!/usr/bin/env python3
"""Generate the SACM 2.3 metamodel inventory from the normative OMG UML XMI.

Parses third_party/sacm-2.3/SACM-2.3-ptc-22-03-13.xml (fetch with
scripts/fetch-sacm23-references.sh) and emits
docs/sacm/sacm-2.3-metamodel-inventory.md: per-package class tables with
generalizations, attributes, association ends, multiplicities, and
enumerations, plus a flat concrete-class -> containment-role table that seeds
the library's serializer name tables (libs/sacm/src/io/name_tables.cpp).

The generated markdown is committed; the C++ public API is hand-written and
checked against this inventory by libs/sacm tests (hybrid code-generation
policy in docs/sacm/sacm-library-architecture.md).

Usage:
  python tools/sacm/generate_metamodel_inventory.py \
      [--input third_party/sacm-2.3/SACM-2.3-ptc-22-03-13.xml] \
      [--output docs/sacm/sacm-2.3-metamodel-inventory.md]
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

XMI_NS = "{http://www.omg.org/spec/XMI/20131001}"

PACKAGE_ORDER = ["Base", "AssuranceCase", "Terminology", "Argumentation", "Artifact"]


def local(tag: str) -> str:
    return tag.split("}")[-1]


def xmi_type(el: ET.Element) -> str:
    return el.attrib.get(XMI_NS + "type", "")


def xmi_id(el: ET.Element) -> str:
    return el.attrib.get(XMI_NS + "id", "")


@dataclass
class End:
    name: str
    type_name: str
    lower: str
    upper: str
    aggregation: str  # "composite", "shared", or ""
    ordered: bool
    owner: str  # owning class name
    from_association: bool  # name resolved from the association, not the end


@dataclass
class Clazz:
    name: str
    package: str
    is_abstract: bool
    generals: list[str] = field(default_factory=list)
    attributes: list[End] = field(default_factory=list)  # primitive/enum typed
    references: list[End] = field(default_factory=list)  # non-composite class typed
    compositions: list[End] = field(default_factory=list)  # composite class typed
    doc: str = ""


@dataclass
class Enum:
    name: str
    package: str
    literals: list[str] = field(default_factory=list)


PRIMITIVE_HREF_NAMES = {
    "String": "String",
    "Boolean": "Boolean",
    "Integer": "Integer",
    "Real": "Real",
    "UnlimitedNatural": "UnlimitedNatural",
    # MagicDraw UML Standard Profile ids for primitives (observed in the OMG file).
    "eee_1045467100323_191782_59": "Boolean",
    "eee_1045467100322_143394_58": "String",
    "eee_1045467100323_136590_60": "Integer",
}

# The machine-readable model references this MagicDraw datatype id without
# defining it; the specification text types the affected attributes as `date`
# (clauses 12.7 Artifact, 12.8 Activity, 12.9 Event).
UNRESOLVED_TYPE_IDS = {
    "_be00301_1073305590699_364818_1": "date",
}

# Known defects in ptc/22-03-13 where the normative specification text
# (formal/23-05-08) disagrees with the machine-readable model. The text is the
# authoritative formal document; the library follows it, and the XMI reader
# additionally accepts the defective machine-readable names on import.
CLASS_RENAMES = {
    # Clause 12.14 names the class ArtifactAssetRelationship with superclass
    # ArtifactAsset; the machine-readable model calls it
    # ArtifactAssertedRelationship and omits the generalization.
    "ArtifactAssertedRelationship": "ArtifactAssetRelationship",
}
MISSING_GENERALIZATIONS = {
    "ArtifactAssetRelationship": ["ArtifactAsset"],
}
ATTRIBUTE_RENAMES = {
    # Clause 12.9 defines Event's attribute as `date`; the machine-readable
    # model spells it `occurece`.
    ("Event", "occurece"): "date",
}
MISSING_ABSTRACT = {
    # Clause 11.3 marks ArgumentationElement abstract; the machine-readable
    # model does not.
    "ArgumentationElement",
}


def resolve_type_name(prop: ET.Element, by_id: dict[str, ET.Element]) -> str:
    type_ref = prop.attrib.get("type")
    if type_ref:
        target = by_id.get(type_ref)
        if target is not None:
            name = target.attrib.get("name", type_ref)
            return CLASS_RENAMES.get(name, name)
        return UNRESOLVED_TYPE_IDS.get(type_ref, type_ref)
    for child in prop:
        if local(child.tag) == "type":
            href = child.attrib.get("href", "")
            fragment = href.split("#")[-1]
            return PRIMITIVE_HREF_NAMES.get(fragment, fragment or href)
    return "?"


def read_multiplicity(prop: ET.Element) -> tuple[str, str]:
    lower, upper = "1", "1"
    for child in prop:
        tag = local(child.tag)
        if tag == "lowerValue":
            lower = child.attrib.get("value", "0")
        elif tag == "upperValue":
            upper = child.attrib.get("value", "1")
    return lower, upper


def read_documentation(el: ET.Element) -> str:
    for child in el:
        if local(child.tag) == "ownedComment":
            body = child.attrib.get("body", "")
            if body:
                return " ".join(body.split())
    return ""


def parse_model(path: Path) -> tuple[dict[str, Clazz], list[Enum]]:
    tree = ET.parse(path)
    root = tree.getroot()
    model = next(c for c in root if local(c.tag) == "Model")

    by_id: dict[str, ET.Element] = {}

    def index(el: ET.Element) -> None:
        ident = xmi_id(el)
        if ident:
            by_id[ident] = el
        for child in el:
            index(child)

    index(model)

    classes: dict[str, Clazz] = {}
    enums: list[Enum] = []
    association_names: dict[str, str] = {}

    packages = [p for p in model if xmi_type(p) == "uml:Package"]

    for pkg in packages:
        for pe in pkg:
            if local(pe.tag) != "packagedElement":
                continue
            if xmi_type(pe) == "uml:Association" and pe.attrib.get("name"):
                association_names[xmi_id(pe)] = pe.attrib["name"]

    for pkg in packages:
        pkg_name = pkg.attrib.get("name", "?")
        for pe in pkg:
            if local(pe.tag) != "packagedElement":
                continue
            kind = xmi_type(pe)
            name = CLASS_RENAMES.get(pe.attrib.get("name", ""), pe.attrib.get("name", ""))
            if kind == "uml:Enumeration":
                literals = [
                    lit.attrib.get("name", "?")
                    for lit in pe
                    if local(lit.tag) == "ownedLiteral"
                ]
                enums.append(Enum(name=name, package=pkg_name, literals=literals))
            elif kind == "uml:Class":
                clazz = Clazz(
                    name=name,
                    package=pkg_name,
                    is_abstract=pe.attrib.get("isAbstract") == "true"
                    or name in MISSING_ABSTRACT,
                    doc=read_documentation(pe),
                )
                for general_name in MISSING_GENERALIZATIONS.get(name, []):
                    clazz.generals.append(general_name)
                for child in pe:
                    tag = local(child.tag)
                    if tag == "generalization":
                        general = by_id.get(child.attrib.get("general", ""))
                        general_name = (
                            general.attrib.get("name", "?") if general is not None else "?"
                        )
                        clazz.generals.append(CLASS_RENAMES.get(general_name, general_name))
                    elif tag == "ownedAttribute":
                        end_name = child.attrib.get("name", "")
                        end_name = ATTRIBUTE_RENAMES.get((name, end_name), end_name)
                        from_association = False
                        if not end_name:
                            assoc = child.attrib.get("association", "")
                            end_name = association_names.get(assoc, "")
                            from_association = bool(end_name)
                        if not end_name:
                            continue
                        type_name = resolve_type_name(child, by_id)
                        lower, upper = read_multiplicity(child)
                        end = End(
                            name=end_name,
                            type_name=type_name,
                            lower=lower,
                            upper=upper,
                            aggregation=child.attrib.get("aggregation", ""),
                            ordered=child.attrib.get("isOrdered") == "true",
                            owner=name,
                            from_association=from_association,
                        )
                        type_target = child.attrib.get("type")
                        is_class_type = False
                        if type_target and type_target in by_id:
                            is_class_type = xmi_type(by_id[type_target]) in (
                                "uml:Class",
                            )
                        if end.aggregation == "composite":
                            clazz.compositions.append(end)
                        elif is_class_type:
                            clazz.references.append(end)
                        else:
                            clazz.attributes.append(end)
                classes[name] = clazz
    return classes, enums


def base_chain(clazz: Clazz, classes: dict[str, Clazz]) -> list[str]:
    chain: list[str] = []
    current = clazz
    seen = set()
    while current.generals:
        general = current.generals[0]
        if general in seen or general not in classes:
            break
        seen.add(general)
        chain.append(general)
        current = classes[general]
    return chain


def multiplicity(end: End) -> str:
    if end.lower == end.upper:
        return end.lower
    return f"{end.lower}..{end.upper}"


def emit(classes: dict[str, Clazz], enums: list[Enum], output: Path, source_name: str) -> None:
    lines: list[str] = []
    push = lines.append
    push("# SACM 2.3 metamodel inventory")
    push("")
    push(
        f"Generated by `tools/sacm/generate_metamodel_inventory.py` from the "
        f"normative OMG machine-readable model `{source_name}` "
        "(OMG document ptc/22-03-13, fetched by `scripts/fetch-sacm23-references.sh`). "
        "Do not edit by hand; regenerate after reference updates."
    )
    push("")
    push("## XMI serialization conventions (pinned)")
    push("")
    push(
        "The normative machine-readable model is UML 2.5 XMI of the metamodel and "
        "declares **no instance-document namespace URI**, and the specification PDF "
        "(formal/23-05-08) prints none either. Clause 2 (Conformance) requires "
        "import/export of \"XMI documents that conform with the SACM XML Schema "
        "produced by applying XMI rules to the normative MOF metamodel\". The "
        "library therefore pins the following conventions in "
        "`libs/sacm/include/sacm/metadata/namespaces.h` (single source of truth; "
        "correcting them later is a one-file change plus golden-file refresh):"
    )
    push("")
    push("- Strict export namespace: `http://www.omg.org/spec/SACM/20220301` (OMG version URI for SACM 2.3), prefix `sacm`.")
    push("- XMI namespace: `http://www.omg.org/spec/XMI/20131001`, prefix `xmi`; `xmi:version=\"2.0\"`.")
    push("- XSI namespace: `http://www.w3.org/2001/XMLSchema-instance`, prefix `xsi` (declared only when `xsi:type` is emitted).")
    push("- Import accepts (tolerant mode): any namespace URI containing `/spec/SACM/`, the `http://omg.sacm/` reference-implementation family, plus `http://example.org/sacm/2.3` (repo fixtures); other URIs load with a warning in tolerant mode and an error in strict mode.")
    push("- Top interchange objects (Clause 2): `AssuranceCasePackage` (Assurance Case Model compliance point, mandatory), `ArgumentPackage` (Argumentation), `ArtifactPackage` (Artifact).")
    push("")
    push("### The pinned export namespace is a choice, not a normative value")
    push("")
    push(
        "This is worth stating plainly because it limits what any SACM 2.3 "
        "conformance claim can mean. Under XMI 2.5.1 an instance document's "
        "namespace derives from the `org.omg.xmi.nsURI` tag on the MOF package; "
        "SACM's normative model carries no such tag, and no `URI=` attribute "
        "appears on any package in `ptc/22-03-13`. Nothing in the specification "
        "determines the namespace, so **every SACM 2.3 producer necessarily "
        "invents one, and no two need agree.**"
    )
    push("")
    push(
        "The EMF reference implementation "
        "([github.com/wrwei/SACM](https://github.com/wrwei/SACM), Apache-2.0) "
        "demonstrates the divergence: it declares one namespace *per metamodel "
        "package* rather than one per document."
    )
    push("")
    push("| Package | `nsURI` | `nsPrefix` |")
    push("|---|---|---|")
    for pkg, prefix in (
        ("base", "base_"),
        ("assurancecase", "assuranceCase_"),
        ("argumentation", "argumentation_"),
        ("artifact", "artifact_"),
        ("terminology", "terminology_"),
    ):
        push(f"| {pkg} | `http://omg.sacm/2.2/{pkg}` | `{prefix}` |")
    push("")
    push(
        "The library therefore accepts that family on import (tolerant mode) and "
        "normalizes to the single pinned URI on strict export. Treat the pin as "
        "this project's canonical form, not as a conformance requirement, and do "
        "not report a file as non-conformant solely because its namespace differs."
    )
    push("")
    push("## Known machine-readable model defects (normalized here)")
    push("")
    push(
        "Where ptc/22-03-13 disagrees with the normative specification text "
        "(formal/23-05-08), this inventory follows the text; the library's XMI "
        "reader additionally accepts the machine-readable spellings on import:"
    )
    push("")
    push("- Clause 12.14: class is `ArtifactAssetRelationship` with superclass `ArtifactAsset`; the machine-readable model names it `ArtifactAssertedRelationship` and omits the generalization.")
    push("- Clause 12.9: `Event`'s attribute is `date: date[0..1]`; the machine-readable model spells it `occurece`.")
    push("- Clauses 12.7/12.8/12.9 type `date`, `startTime`, `endTime`, and Artifact `date` as `date`; the machine-readable model references an undefined MagicDraw datatype id for them.")
    push("- Clause 11.3 marks `ArgumentationElement` abstract; the machine-readable model does not.")
    push("")
    push("## Package summary")
    push("")
    push("| Package | Classes | Abstract | Enumerations |")
    push("| --- | --- | --- | --- |")
    for pkg in PACKAGE_ORDER:
        pkg_classes = [c for c in classes.values() if c.package == pkg]
        pkg_enums = [e for e in enums if e.package == pkg]
        push(
            f"| {pkg} | {len(pkg_classes)} | "
            f"{sum(1 for c in pkg_classes if c.is_abstract)} | {len(pkg_enums)} |"
        )
    push("")

    for pkg in PACKAGE_ORDER:
        pkg_classes = sorted(
            (c for c in classes.values() if c.package == pkg), key=lambda c: c.name
        )
        if not pkg_classes:
            continue
        push(f"## Package `{pkg}`")
        push("")
        for clazz in pkg_classes:
            abstract = " (abstract)" if clazz.is_abstract else ""
            push(f"### {clazz.name}{abstract}")
            push("")
            chain = base_chain(clazz, classes)
            if chain:
                push(f"Generalization chain: {' -> '.join(chain)}")
                push("")
            if clazz.doc:
                push(f"> {clazz.doc}")
                push("")
            if clazz.attributes:
                push("| Attribute | Type | Multiplicity |")
                push("| --- | --- | --- |")
                for end in clazz.attributes:
                    push(f"| {end.name} | {end.type_name} | {multiplicity(end)} |")
                push("")
            if clazz.references:
                push("| Reference end | Target type | Multiplicity | Ordered |")
                push("| --- | --- | --- | --- |")
                for end in clazz.references:
                    push(
                        f"| {end.name} | {end.type_name} | {multiplicity(end)} | "
                        f"{'yes' if end.ordered else 'no'} |"
                    )
                push("")
            if clazz.compositions:
                push("| Containment role | Child type | Multiplicity | Ordered |")
                push("| --- | --- | --- | --- |")
                for end in clazz.compositions:
                    push(
                        f"| {end.name} | {end.type_name} | {multiplicity(end)} | "
                        f"{'yes' if end.ordered else 'no'} |"
                    )
                push("")

    if enums:
        push("## Enumerations")
        push("")
        for enum in sorted(enums, key=lambda e: e.name):
            push(f"### {enum.name} (`{enum.package}`)")
            push("")
            push("Literals: " + ", ".join(f"`{lit}`" for lit in enum.literals))
            push("")

    push("## Concrete class -> containment roles (serializer dispatch table)")
    push("")
    push(
        "Effective containment for each concrete class, including roles inherited "
        "from abstract bases. This table seeds `libs/sacm/src/io/name_tables.cpp` "
        "and is asserted against the library by `test_metamodel_coverage.cpp`."
    )
    push("")
    push("| Concrete class | Package | Containment roles (role: child type) |")
    push("| --- | --- | --- |")
    for clazz in sorted(classes.values(), key=lambda c: (c.package, c.name)):
        if clazz.is_abstract:
            continue
        roles: list[End] = list(clazz.compositions)
        for general in base_chain(clazz, classes):
            roles.extend(classes[general].compositions)
        rendered = (
            "; ".join(f"`{end.name}`: {end.type_name}" for end in roles) or "(none)"
        )
        push(f"| {clazz.name} | {clazz.package} | {rendered} |")
    push("")

    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        default="third_party/sacm-2.3/SACM-2.3-ptc-22-03-13.xml",
        type=Path,
    )
    parser.add_argument(
        "--output",
        default="docs/sacm/sacm-2.3-metamodel-inventory.md",
        type=Path,
    )
    args = parser.parse_args()
    if not args.input.is_file():
        print(
            f"error: {args.input} not found; run scripts/fetch-sacm23-references.sh first",
            file=sys.stderr,
        )
        return 1
    classes, enums = parse_model(args.input)
    emit(classes, enums, args.output, args.input.name)
    concrete = sum(1 for c in classes.values() if not c.is_abstract)
    print(
        f"wrote {args.output}: {len(classes)} classes "
        f"({concrete} concrete), {len(enums)} enumerations"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
