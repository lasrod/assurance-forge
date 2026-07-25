# Third-party interoperability fixtures

**These files were produced by other people's tools and are redistributed here
under their own licences.** They are not Assurance Forge material, they are not
reconstructions, and they must not be edited — their value is precisely that
nobody here chose what is in them.

Every other SACM fixture in this repository is our own reconstruction of a
dialect, which can only prove the reader handles the shape we *believe* a tool
emits. These are the bytes themselves. See
[docs/sacm/sacm-interop-corpus.md](../../../../../docs/sacm/sacm-interop-corpus.md)
for the wider corpus, including files that could **not** be vendored.

## Contents

### `mobstr-safetycase.integration`

| | |
|---|---|
| Source | [panorama-research/mobstr-dataset](https://github.com/panorama-research/mobstr-dataset), `org.panorama-research.mobstr.safetycase/mobstr-safetycase.integration` |
| Upstream commit | `14f86e018ee6` |
| Licence | **EPL-2.0** |
| Modified | No — byte-for-byte as published |

The MobSTr dataset demonstrates model-based safety assurance for a
safety-critical automotive system. The file is an **ODE `DDIPackage`** whose sole
child is the SACM assurance case: it declares the `architecture_` and
`failureLogic_` prefixes but does not use them, so what the foreign-root path
drops here is the container element and its `Id`/`name`/`description` — not a
sibling model. `deis-etcs.model` below is the file that exercises real
container loss.

Exercises: foreign container root (`SACM-XMI-009`), the DEIS/ODE merged
namespaces, the legacy `http://www.omg.org/XMI` URI, `gid=`-based identity with
no `xmi:id` anywhere, plural containment roles (`assuranceCasePackages`), and
the nested `<description><content><value lang= content=/></content></description>`
MultiLangString form.

### `sysmline-easyexample.assurancecase`

| | |
|---|---|
| Source | [Ruizhe-Yang/SysMLine](https://github.com/Ruizhe-Yang/SysMLine), `dut.cs.sysmline/model/SACM/EasyExample.assurancecase` |
| Upstream commit | `2658f087c5cd` |
| Licence | **EPL-2.0** |
| Modified | No — byte-for-byte as published |

EMF/ACME output rooted at a SACM `AssuranceCasePackage`, in the
`http://omg.sacm/2.2/*` per-package dialect with GSN 3.0 types layered via
`xsi:type`.

Note this file **does not validate cleanly**, and that is deliberate: it is EMF
template output carrying placeholder values, so it genuinely violates SACM rules
(`implementationConstraint` on non-abstract elements, duplicate language tags).
The tests assert that the library *reports* those violations rather than
accepting them — reading third-party content and correctly calling it invalid is
the library working, not failing.

### `deis-etcs.model`

| | |
|---|---|
| Source | [DEIS-Project-EU/DDI-Scripting-Tools](https://github.com/DEIS-Project-EU/DDI-Scripting-Tools), `Examples/ETCS/etcs.model` |
| Upstream commit | `a234b7f25500` |
| Licence | **MIT** |
| Modified | No — byte-for-byte as published |

An ODE `DDIPackage` for an ETCS (European Train Control System) example, and the
one fixture where a foreign container genuinely carries other models: alongside
the assurance case sit three `odeProductPackages` siblings totalling **503**
non-SACM elements. Loading it drops all of them, which is exactly the loss
`SACM-XMI-009` exists to disclose — so this file is what makes that warning
testable rather than theoretical.

## Licence compliance

`mobstr-safetycase.integration` and `sysmline-easyexample.assurancecase` are
**EPL-2.0**; the full licence text is in
[`LICENSE.EPL-2.0.txt`](LICENSE.EPL-2.0.txt), included per EPL-2.0 §3.2(b),
which requires a copy of the agreement to accompany each copy of the program.
`deis-etcs.model` is **MIT**; its text is in [`LICENSE.MIT.txt`](LICENSE.MIT.txt).

Source availability (EPL-2.0 §3.1(a)) is satisfied by the repository URLs and
upstream commits above. No file has been modified.

Files whose licence does **not** permit redistribution are not here. In
particular the ACEditor MBAC output — the single most useful file found, because
it imports and validates with zero errors — carries no declared licence, so it
can only be exercised through the opt-in `SACM_INTEROP_CORPUS` harness.

## Adding to this directory

A file may be vendored here only if its licence permits redistribution **and**
that licence is recorded above with a source URL and upstream commit. Anything
else belongs in the opt-in corpus instead.
