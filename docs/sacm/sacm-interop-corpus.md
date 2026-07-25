# SACM interoperability corpus

Phase 10 deliverable. This is the register of every dialect `libs/sacm` claims
to interoperate with: where each one came from, what its licence permits, which
test exercises it, and — the part that matters most — what it does **not**
prove.

`docs/sacm/sacm-conformance-matrix.md` remains the source of truth for
requirement status. This file exists so a compatibility claim can be traced back
to evidence rather than to a fixture whose origin nobody recorded.

## The redistribution rule

**Third-party bytes are committed only when their licence permits redistribution
and that licence is recorded.**

`libs/sacm/tests/data/interop-thirdparty/` holds files produced by other
people's tools, unmodified, with source URL, upstream commit and licence in its
[NOTICE.md](../../libs/sacm/tests/data/interop-thirdparty/NOTICE.md). Those run
in CI. Anything whose licence does not permit redistribution — including the
single most useful file found — stays out and is reachable only through the
opt-in harness below.

Everything in `libs/sacm/tests/data/sacm23/` remains our own: a *reduced
reproduction* of a dialect's structural shape, or a fixture *authored from a
specification figure*. Those prove the reader handles the shape we **believe** a
tool emits; only the third-party directory proves it handles what a tool
actually emitted.

## Corpus register

| Fixture | Dialect modelled | Provenance | Licence status | What it establishes |
|---|---|---|---|---|
| `interop-emf-gsn-noids-valid.sacm.xmi` | EMF-produced SACM with GSN types layered via `xsi:type`; **no `xmi:id` anywhere**, references by containment path (`//@argumentPackage.0/@argumentationElement.18`); `argumentationElement` role spelling; per-package namespaces `http://omg.sacm/2.3/*`; GSN namespace `http://acwg.org/3.0/gsn` | Reduced reproduction of the shape of a public EMF example. No third-party bytes. | N/A — nothing copied. Upstream EMF implementation (`github.com/wrwei/SACM`) is Apache-2.0, recorded for reference only. | Positional references resolve; GSN types resolve to the SACM classes they specialize; `SupportedBy`/`InContextOf` endpoints are swapped to SACM's clause-11.14 direction |
| `interop-emf-reference-dialect-valid.sacm.xmi` | EMF reference dialect: one namespace **per metamodel package** (`http://omg.sacm/2.2/base`, `.../argumentation`, …) inside an `xmi:XMI` wrapper | Hand-authored reconstruction of the dialect. No third-party bytes. | N/A — nothing copied. | The per-package dialect imports, is not reported as an unknown namespace, and strict export normalizes to our pinned URI |
| `interop-gsn20-current-namespace-valid.sacm.xmi` | Current GSN metamodel namespace `http://scsc.acwg.gsn/2.0` (supersedes `http://acwg.org/3.0/gsn` despite the lower number) | Authored from GSN Metamodel Specification v2.2, SCSC ACWG, July 2021, Figure 2. Structure taken from the figure; not copied from any tool output. | Specification is CC-BY-4.0. Only the *structure* described by a figure was used; no specification text or file is reproduced here. | `AwayAssumption`/`AwayJustification` resolve; a `Context` (whose SACM supertype is abstract) is preserved verbatim, re-emitted in position, its namespace re-declared, and survives two round trips |
| `vendor-extension-valid.sacm.xmi` | Arbitrary vendor extension: foreign child element and foreign attribute under a prefix only the source declares | Synthetic. | N/A. | Unknown content is preserved on a tolerant load, re-emitted by a compatibility save, and refused by a strict save (SACM-XMI-006) |
| `data/open-autonomy-safety-case.sacm.xml`, `data/oasc-ja.xml`, `tests/data/fixture_roundtrip_*.sacm.xml` | Assurance Forge's own legacy app dialect (SACM 2.2 namespace, attribute-style `id=`) | Authored by this project. The OASC files are *this project's* SACM rendering of Open Autonomy Safety Case subject matter, not a copy of any OASC artifact. | Project-owned. | Real-size documents load and semantically round-trip through the library (`test_repo_fixture_interop.cpp`), including a multi-language (ja/en) document |

## Tests

| Test | Fixture |
|---|---|
| `SACM23_COMPAT_002_EmfGsnFileWithoutIdsParsesIntoSacmElements` | emf-gsn-noids |
| `SACM23_COMPAT_002_GsnSupportedByEndpointsAreSwappedToSacmDirection` | emf-gsn-noids |
| `SACM23_COMPAT_001_EmfReferenceDialectImportsAndNormalizes` | emf-reference-dialect |
| `SACM23_COMPAT_002_CurrentGsnNamespaceAndAwayTypesAreRecognized` | gsn20-current-namespace |
| `SACM23_COMPAT_002_ExtensionTypedElementIsPreservedNotDropped` | gsn20-current-namespace |
| `SACM23_COMPAT_002_PreservedExtensionFragmentDeclaresItsNamespace` | gsn20-current-namespace |
| `SACM23_COMPAT_002_PreservedExtensionContentSurvivesTwoRoundTrips` | gsn20-current-namespace |
| `SACM23_COMPAT_002_PreservedFragmentKeepsItsSiblingPosition` | gsn20-current-namespace |
| `SACM23_COMPAT_002_ReferenceToPreservedElementIsNotDangling` | inline document |
| `SACM23_COMPAT_001_VendorContentPreservedAndStrictSaveRefuses` | vendor-extension |
| `SACM23_COMPAT_001_PreservedForeignAttributeSurvivesTwoRoundTrips` | inline document |
| `SACM23_RT_001_Repo*RoundTrips`, `SACM23_BASE_001_JapaneseMultiLanguageFixtureRoundTrips` | repository fixtures |
| `SACM23_COMPAT_002_ThirdPartyOdeContainerImportsWithNonConformanceWarning` | **third-party** `mobstr-safetycase.integration` |
| `SACM23_COMPAT_002_ThirdPartyEmfFileImportsAndReportsItsRealViolations` | **third-party** `sysmline-easyexample.assurancecase` |
| `SACM23_COMPAT_002_ThirdPartyFiles{ParseIntoSacmElements,SemanticallyRoundTrip}` | opt-in `SACM_INTEROP_CORPUS` directory |

## Third-party corpus

Files produced by other tools, located by public search. The two EPL-2.0 files
are **committed** under `libs/sacm/tests/data/interop-thirdparty/` and run in CI;
the rest cannot be redistributed and are reachable only through the opt-in
harness.

Point `SACM_INTEROP_CORPUS` at a directory containing them and
`Sacm23InteropCorpus.SACM23_COMPAT_002_*` (`libs/sacm/tests/test_interop_corpus.cpp`)
runs against every file in it. Unset, those tests **skip visibly**, so a green CI
run is never mistaken for third-party evidence.

```bash
SACM_INTEROP_CORPUS=/path/to/corpus ctest --test-dir build -C Release -R Sacm23InteropCorpus
```

| Source | Licence | File | Dialect | Measured result |
|---|---|---|---|---|
| [LuisFelipeAN/aceditor-mbac-results](https://github.com/LuisFelipeAN/aceditor-mbac-results) — output of the SACM **ACEditor MBAC** tool | **none declared** (do not redistribute) | `HBS/default.sacm2` | `http://SACM_ACEditor/sacm/2.1`, legacy XMI URI, `gid=` identity, no `xmi:id` | **Imports, validates with 0 errors, semantically round-trips.** The one file that satisfies COMPAT-002's requirement text end to end |
| [Ruizhe-Yang/SysMLine](https://github.com/Ruizhe-Yang/SysMLine) — EMF/ACME | EPL-2.0 | `dut.cs.sysmline/model/SACM/EasyExample.assurancecase` | `http://omg.sacm/2.2/*` per-package + `http://acwg.org/3.0/gsn` | **Committed** under `interop-thirdparty/`. Imports and round-trips. 80 validation errors, all genuine rule violations in the source: `implementationConstraint` on non-abstract elements (clause 8.6) and duplicate language tags — it is EMF template output with placeholder values |
| [Ruizhe-Yang/SysMini](https://github.com/Ruizhe-Yang/SysMini) | none declared | `org.omg.sysmini/model/SACM/quan.assurancecase` | same, plus `artifact_` | Imports and round-trips; 45 errors, same two causes |
| [panorama-research/mobstr-dataset](https://github.com/panorama-research/mobstr-dataset) — MobSTr automotive safety dataset | EPL-2.0 | `org.panorama-research.mobstr.safetycase/mobstr-safetycase.integration` | ODE `DDIPackage` embedding SACM, `http://www.deis-project.eu/ode/mergedODE/sacm/*` | **Imports** with a `SACM-XMI-009` non-conformance warning (see below); validates with 0 errors; round-trips. **Committed** under `interop-thirdparty/` |
| [DEIS-Project-EU/DDI-Scripting-Tools](https://github.com/DEIS-Project-EU/DDI-Scripting-Tools) | MIT | `Examples/ETCS/etcs.model`, `trackside.model` | same | Imports the same way. Not committed — one ODE container in CI is enough, and this one is 100 KB |

Two findings worth keeping:

- The reader handles **three previously unseen dialects** without change —
  including SACM 2.1 with `gid=`-only identity, which no fixture in this
  repository models.
- The validation errors are the library **working**, not failing: it read
  third-party content and correctly identified real SACM rule violations, while
  still round-tripping the documents semantically.

### Foreign container roots

Real toolchains ship SACM *embedded in a larger container* — an ODE
`DDIPackage` carrying architecture and failure-logic models alongside the
assurance case. Clause 2.4 defines the SACM interchange roots and such a
container is not one, so **strict refuses these files**.

Refusing them on the tolerant path too would mean a user holding a real vendor
file simply cannot open their own assurance case. So a tolerant load reads the
SACM out and reports `SACM-XMI-009`, which states all three things the user
needs in one diagnostic:

- the file **does not conform** to SACM 2.3 as an interchange document;
- content outside the SACM packages is **not represented** in the model;
- **saving writes a conformant SACM document**, not the original container.

That last point is why silence would have been the worst option: the argument
loads cleanly, so nothing else would warn the user that the rest of their file
is about to disappear on save.

## Known gaps

These are the limits of what the register above supports. They are the reason
`SACM23-COMPAT-002` is not claimed beyond what its matrix row says.

1. **Coverage is two tools, not the field.** CI now parses real third-party
   bytes, which closes the gap that held SACM23-COMPAT-002 — but two EPL-2.0
   files from two projects is a floor, not breadth. Papyrus in particular has
   still produced nothing we have seen (decision #20 named it as the original
   target).

   The strongest single piece of evidence — ACEditor MBAC output, which imports
   and validates with **zero** errors — carries no declared licence and so runs
   only through `SACM_INTEROP_CORPUS`. If its author ever states a licence, it
   should be vendored.

   Superseded history: an earlier one-off developer check parsed a public
   EMF/GSN file and yielded 39 SACM elements where the previous reader yielded 0.
   It is recorded here as history, not evidence; the committed fixtures and the
   opt-in harness replace it.

2. **No Papyrus output.** Papyrus was the original interop target (decision
   #20). Nothing from it has been obtained or modelled.

3. **Coverage is per-dialect, not per-tool.** "The EMF dialect imports" is not
   "files from tool X import". Two tools sharing an EMF backend can still differ
   in role spelling, id policy, and extension namespaces.

4. **Relationships into preserved content resolve but stay untyped.** A
   reference to an element only preserved compatibility content can represent is
   reported as `SACM-REF-003` (untyped) rather than `SACM-REF-001` (missing), and
   the endpoint survives a round trip — but the target cannot be kind-checked,
   so `reference_target_kind_ok` never runs on it.

5. **Positional fidelity is document order, not EMF path numbering.** A
   preserved fragment is re-emitted in the sibling slot it occupied, so document
   order survives a compatibility save. The *numbering an EMF path would compute
   over our output* does not: `assign_emf_path_ids` indexes by the raw feature
   name while the writer emits typed siblings under the normalized role
   (`argumentElement`) and re-emits the fragment under its source spelling
   (`argumentationElement`). A path like
   `//@argumentPackage.0/@argumentationElement.5` therefore does not resolve
   against our compatibility output even though the elements are all present and
   in order. Harmless for our own round trip — our output is id-addressed, and
   paths inside preserved fragments were rewritten to ids before capture — but it
   means compatibility output is not guaranteed re-readable *positionally* by
   the dialect it came from. Normalizing the re-emitted tag would fix it at the
   cost of no longer preserving the fragment byte-for-byte; that trade deserves
   a decision record rather than a silent change.

6. **Role-less vendor fragments are still appended.** An unknown vendor element
   whose tag is no containment role has no sibling sequence to hold a slot in,
   so it is recorded with an empty role and appended after the typed children.
   Positional fidelity covers containment-role fragments only.

## Adding a source

1. Obtain a real file from the tool. Do **not** commit it.
2. Record the tool, version, and the file's licence in the register above.
3. Write the smallest fixture that reproduces the structural feature the real
   file exercises, and say in a fixture comment what it is a reproduction *of*.
4. Add an ID-bearing test (`SACM23_COMPAT_002_…`) and list it in Tests.
5. If the real file's licence permits redistribution, note that explicitly —
   a future decision to vendor it should not require re-researching the licence.
6. Update `docs/sacm/sacm-conformance-matrix.md` if the compatibility claim
   changes, and run `python tools/sacm/check_conformance_matrix.py`.

## CI gate

- `sacm_matrix_check` (CTest) fails on matrix rot: a `verified` row with no
  ID-bearing test, a test naming a requirement that does not exist, or a cited
  path that has moved.
- The interop tests above run in the standard suite. Repository-fixture tests
  `GTEST_SKIP` in standalone library builds where those files are absent — a
  skip there is expected, not a silent pass.
