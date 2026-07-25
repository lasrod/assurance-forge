# SACM interoperability corpus

Phase 10 deliverable. This is the register of every dialect `libs/sacm` claims
to interoperate with: where each one came from, what its licence permits, which
test exercises it, and — the part that matters most — what it does **not**
prove.

`docs/sacm/sacm-conformance-matrix.md` remains the source of truth for
requirement status. This file exists so a compatibility claim can be traced back
to evidence rather than to a fixture whose origin nobody recorded.

## The redistribution rule

**No bytes produced by another tool are committed to this repository.**

Every interop fixture in `libs/sacm/tests/data/sacm23/` is either a *reduced
reproduction* (hand-authored to reproduce a dialect's structural shape, from a
public example that is not itself copied in) or *authored from a specification
figure*. That keeps the repository free of third-party licensing obligations we
would otherwise have to track, vendor, and re-check on every release.

It also caps what the corpus can prove, and that limit is stated explicitly in
[Known gaps](#known-gaps) rather than left for a reader to infer.

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

## Known gaps

These are the limits of what the register above supports. They are the reason
`SACM23-COMPAT-002` is not claimed beyond what its matrix row says.

1. **No bytes produced by an independent tool are parsed in CI.** Every dialect
   fixture is our reconstruction of a dialect. A reconstruction proves the
   reader handles the shape we *believe* a tool emits; it cannot catch a detail
   we did not know to reproduce. This is the single largest gap in the
   compatibility claim.

   One manual check has been done outside CI: a real public EMF/GSN file was
   parsed during the SACM23-COMPAT-002 work and yielded 39 SACM elements where
   the previous reader yielded 0. That was a one-off developer check, is not
   reproducible from this repository, and is recorded here as history rather
   than as evidence.

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
