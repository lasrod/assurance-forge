# SACM integration: what is preserved, and what is not

The long-form record behind four rows of the
[conformance matrix](sacm-conformance-matrix.md). It exists because those rows'
notes had grown to **41,400 characters inside four Markdown table cells** —
`SACM23-LIB-002` alone was 20,800, about 2,800 words in one cell — which is
unreadable in a table and therefore unread. #295 asks for a matrix whose detail
lives in linked records; this is that record for the integration rows.

!!! note "Moved, not rewritten"

    The text below was cut from the matrix cells verbatim and pasted here. It is
    dense, and in places it argues with earlier versions of itself — those
    corrections are the useful part and are deliberately preserved. Summarising
    would have meant deciding which measured loss no longer mattered, and none of
    these were written speculatively: each records something a verifier pass or a
    test measured on real bytes.

## How to read this

Every one of these rows concerns the same seam: the SACM library owns the
document, and Assurance Forge projects it into a legacy POD model to render and
edit. Content that the POD cannot represent is at risk at that seam, and the
history below is largely the discovery — six separate times, in six separate
sites — that a projection was being *reloaded* rather than merely *compared*, and
so was rebuilding the live document without whatever it could not carry.

Two properties make that history worth keeping rather than compressing:

- **The canonical hash cannot see this class of loss.** It projects through the
  same lossy projection on both sides, so anything the projection drops is absent
  from both and invisible by construction. Several of these defects passed every
  convergence test that existed.
- **Each fix is pinned on saved bytes, not on a hash**, for that reason. Where a
  note says "confirmed to fail before the fix", that was done.

The **library's** own interchange conformance is a separate claim and is not in
question here — see [compliance points](sacm-compliance-points.md). What follows
is about the application's integration.


## SACM23-LIB-002 — Source of truth — the library-owned document through Assurance Forge edit paths

The one row still at `implemented`. Its requirement is that loaded SACM data is owned by the library and that Assurance Forge projections never become the serialization source of truth.

Matrix row: [`SACM23-LIB-002`](sacm-conformance-matrix.md).

Phase 9 Stage 4: `AppState::load_file` loads through the library and retains the
`sacm_adapter::LibraryDocument`; `loaded_case` is projected from it. The legacy-parser
fallback this note used to describe was removed in f282f3a -- the library is the sole
load path, and a file it cannot read is a library import bug rather than a reason to
keep a parallel parser. Phase 9 **Stage 6** (in progress, Approach B):
`sacm_adapter::save_document` serializes the library document to XMI (proven lossless,
ACPs preserved); `core::project_library_package` projects a library document back to a
`sacm::AssuranceCasePackage` (argument + terminology + artifact); and **all nine audit
canonical-hash sites now derive the hash through the library**
(`core::library_canonical_hash*`), so the manifest cache, replayed model, on-disk model,
and snapshots converge on the same derivation. The save sites (command-bus autosave,
explicit save, audit restore) write **library SACM XMI** (tolerant, so vendor content
survives); the audit readers, already on the library, read it back and the full audit
suite stays green — save→verify converges with XMI on disk. Snapshots are byte-copies of
the live file, so they inherit XMI and the (library-derived) snapshot hashing handles
it. The library is the serialization source of truth for the save sites and for every
*flipped* command; what remains is stated at the end of this cell rather than claimed
away here. (This sentence used to read "end-to-end", which the same cell then
contradicted.) Phase 9 **Stage 7** closes the Stage-6 preservation caveat: every save
now serializes the **library-owned document** (`sacm_adapter::save_document`) instead of
round-tripping the projected legacy `sacm::AssuranceCasePackage`. `AppState::save_file`
serializes `library_document`; the command bus serializes `ctx.library_document`
whenever the library-primary flip engaged (gated on the flip, not merely on the
document's presence, because an unflipped command — a NodeOnly removal, an unsupported
seam — mutated the legacy package in place and the library does not hold that edit until
the Stage 5 net re-derives it); `RestoreSacmFromAudit` serializes the document the
replay produced. `core::library_xmi_from_package` survives as the (documented lossy)
fallback in `AppState::save_file` and `RestoreSacmFromAudit`, both guarded and both
surfacing a visible degradation warning; and -- not a fallback -- as the **routine**
command-bus path whenever the library-primary flip did not engage (a NodeOnly removal, a
dispatch with no document). In that last case the **file** is degraded but the
**document** is not: the Stage-5 net re-derives `ctx.library_document` through
`sacm_adapter::reload_document_keeping_compatibility_content`, as does
`AppState::sync_library_document` on the no-bus path, so preserved vendor content
survives in memory and the next explicit save -- which serializes the document -- writes
it back to disk. What remains is the unflipped autosave itself, which still writes
`core::library_xmi_from_package` projection bytes to the tracked file. Evidence for the
in-memory half is owned by SACM23-INT-001 (SaveFromLibrary.SACM23_INT_001_UnflippedBusCo
mmandPreservesUnknownContentInTheDocument); narrowing the autosave is the remaining work
on this row. Unknown/foreign XML that only a tolerant load preserved therefore survives
save, and for a **flipped** command all three save sites produce identical bytes for the
same state, so `manifest.last_known_raw_file_hash` stays valid across an
autosave/explicit-save mix (SaveFromLibrary.SACM23_LIB_002_*). After an **unflipped**
command they diverge, by construction rather than by accident: the autosave wrote
projection bytes while the document kept the preserved content, so the next explicit
save rewrites the file and the cached raw hash is stale until the next audited command.
Measured benign -- `VerifyProject` still succeeds, because audit verification converges
on the canonical hash and preserved content does not enter it -- but it is not the byte-
identity the flipped path has, and it disappears when the unflipped autosave is
narrowed. The residual gap this note used to record -- the tolerant writer not re-
declaring foreign namespace prefixes, and `semantic_compare` not covering
`preserved_attributes` -- was closed under SACM23-COMPAT-001
(`write_namespace_declarations`, and `preservedAttributes` in the compare snapshot); the
sentence outlived the fix and is removed.

**Bridged commands were the real gap, and are now fixed**:
`BridgeLegacyMutationToLibrary` rebuilt the LIVE document from `project_library_package`
-- the *audit* projection, whose own contract permits it to collapse packages and which
never restores vendor TaggedValues. Because that projection is reloaded rather than
merely compared, every bridged command (all text edits, terminology, ACP CRUD, package
removal, tree reorder) rebuilt the document without its tags: one rename erased all ten
`assuranceForge.acp` TaggedValues from an ACP-carrying case, and the repository's four-
argument-package flagship case collapsed into one, duplicating artifact-reference ids
until the re-derive was rejected and the command failed outright. The convergence tests
could not see any of it -- they compare canonical hashes computed through that same
projection on both sides, so the loss is invisible by construction. The bridge now
projects with `project_library_package_with_tags` and re-derives through
`sacm_adapter::reload_document_keeping_compatibility_content`, which restores SOME of
what no POD projection can carry -- preserved unknown XML and vendor attributes, via the
new library API `sacm::compat::adopt_preserved_content` -- but NOT typed SACM 2.3
outside the legacy POD subset (see the disclosure at the end of this cell). Canonical
hashing is unaffected: it re-projects from the document at hashing time, so both audit
sides still see the collapsed view they always did. The same defect existed twice:
`core::audit::BridgeViaLegacy` was a second copy of the same algorithm, so fixing the
live path left it live on the **restore** site -- a recovery destroyed every ACP and
reported no degradation, and the flagship case could not be restored at all.
`BridgeViaLegacy` now delegates to the one implementation, so the two cannot drift
again. Pinned on the saved BYTES, not on a hash (the hash drops the same tags on both
sides and cannot see this class of loss), by
SaveFromLibrary.SACM23_LIB_002_BridgedEditPreserves{UnknownContent,AcpTaggedValues},
...BridgedEditSucceedsOnMultiArgumentPackageCase, and the three
...RestoreAfterBridgedEdit* counterparts; plus
SaveFromLibrary.SACM23_LIB_002_UnknownContentSurvivesLoadEditSaveReload for the native
seam. `sacm::compat::adopt_preserved_content` also carries `preserved_element_ids`,
without which a rebuilt document reported a hard SACM-REF-001 against content its own
output still carried; covered library-side by
Sacm23RoundTrip.SACM23_LIB_002_AdoptPreservedContentRestoresWhatAProjectionDrops.

**Remaining work, disclosed and not blocking verification:** the command bus's UNFLIPPED
path (NodeOnly removal, no-document dispatch) still writes
`core::library_xmi_from_package` projection bytes to the tracked file, so an unflipped
command costs preserved vendor content **on disk** until the next save that serializes
the document; the live document keeps it, because the Stage-5 net re-derives through
`sacm_adapter::reload_document_keeping_compatibility_content` (SaveFromLibrary.SACM23_IN
T_001_UnflippedBusCommandPreservesUnknownContentInTheDocument); and a bridged edit
normalizes each claim's Description into the legacy two-slot form once (idempotent and
canonical-hash-neutral, but a rewrite the user did not ask for). The new-project seed
was another non-library producer of SACM bytes in the working-file path: new-project
seed files were a hand-written literal in the SACM **2.2** namespace using `id=` rather
than `xmi:id`, so every new project began as a document no strict consumer would take
and the first save silently rewrote it into a different dialect than the one on disk.
`ProjectService::AddSacmFile` now builds the seed through the library
(`sacm_adapter::new_case_document_xmi`) and serializes it **strict** -- a brand-new
document carries nothing to preserve, so anything strict would refuse is a defect in
that function. Pinned by a STRICT load (a tolerant load passes either way, which is why
the old dialect went unnoticed) plus a byte-comparison proving the seed on disk is
already what the writer would produce for the same model
(ProjectServiceTest.SACM23_LIB_002_NewProjectSeedIsStrictSacm23Xmi).

**Undo was a fourth instance of the same defect, on the audit path rather than the edit
one, and it reached disk.** `core::audit::ReconstructAtSequence` built its `ReplayState`
with `core::project_library_package` -- the audit projection again -- and
`UndoLastTransactionCommand` assigns that state straight over the live
`ctx.model`/`ctx.package`, which the bus then serializes to the tracked file. So every
undo wrote back a document with **no vendor TaggedValues at all**: measured on a real
project, 10 tags to 0 (all `assuranceForge.acp`, i.e. every Assurance Claim Point
destroyed, plus each bare strategy's `assuranceForge.gsn.strategyTarget`, which
permanently detaches it from its goal), and 2 argument packages collapsed into 1 (the
ACP confidence package merged into the main one). The bridge fix landed on
`library_bridge.cpp` and `BridgeViaLegacy`; this site was missed because it is reached
through history reconstruction rather than through a command's own edit.
`ReconstructAtSequence` now derives its state through
`core::RebuildDerivedViewsFromLibrary` -- the same derivation `AppState::load_file` uses
-- so the reconstructed state is shaped exactly like a loaded one, render passes
included (a bare strategy also lost its placement, so the history canvas drew it
detached). Canonical hashing is unaffected: `core::library_canonical_hash` re-projects
from the package through the tagless projection at hashing time, which is also precisely
why no existing test could see this -- the loss is dropped on both sides by
construction. Pinned on the saved BYTES by
UndoCommand.SACM23_LIB_002_UndoPreservesVendorTaggedValuesInTheSavedFile, plus the two
HistoryReconstruction.SACM23_LIB_002_* unit pins; all three confirmed to fail before the
fix.

**Undo is now library-primary**, which removes it from the unflipped set named above.
`ReconstructAtSequence` returns the replayed DOCUMENT alongside the derived views
(`ReconstructedState`), and `UndoLastTransactionCommand` move-assigns it into
`ctx.library_document` and sets `library_primary`, so the bus serializes the document
itself. That is what makes an undo preserve the unknown/foreign XML no projection
carries -- and the document is not rebuilt from a projection either: it was replayed
from the snapshot through the library, so it holds the preserved content of the state
being RESTORED rather than of the state being replaced. It also stops undo replacing
`ctx.model`/`ctx.package` mid-dispatch: it leaves the live views for the frame-boundary
re-derive like every other flipped command, retiring the last instance of the container-
teardown hazard the bus documents. Pinned by
UndoCommand.SACM23_LIB_002_LibraryPrimaryUndoPreservesUnknownVendorContent, which
asserts on the saved bytes and was confirmed to fail with the flip disabled (the vendor
element gone from the file, `library_primary` false).

**The row is back to `implemented`.** The verifier pass on these two slices
(docs/sacm/verification/2026-07-26-lib-002-undo-library-primary-round-1-FAIL.md) found
both of them sound and falsifiable, and then found a SIXTH instance of the projection-
rebuild defect that neither reached: `core::audit::MigrateStrategyEncodingIfNeeded`
loaded the tracked SACM into a library document, projected it, normalized the
PROJECTION, and wrote `core::library_xmi_from_package` over the tracked file plus
`sacm::serialize_sacm` as the promoted trusted baseline -- discarding the document it
was holding three lines above. Measured: a foreign-namespace element, its attribute and
its namespace declaration were all destroyed in both the working file and the baseline.
It ran silently at project open, and because `manifest.replay_root_snapshot_id` is
repointed at the degraded baseline, restore-from-audit replayed from it and could not
recover the content, while the baseline is also an undo wall -- so the loss was
unrecoverable from inside the application. That was this row's own requirement violated
on the working-file path, at a site the row cited with an in-code justification ("there
is no library document at this point in the migration") that was false -- the document
is loaded three lines above and was simply discarded.

**Finding 3 is now fixed.** The migration bridges the normalization onto the document
through `core::commands::BridgeLegacyMutationToLibrary` -- the same one implementation
every other legacy mutation goes through, so this cannot become a seventh site -- and
serializes `sacm_adapter::save_document` for BOTH writes, which makes the promoted
baseline a byte copy of the migrated working file (the relationship the initial snapshot
already has to the live file, and one that cannot drift). The false justification is
deleted, and with the legacy write gone every remaining `sacm::serialize_sacm` in the
working-file path is either a guarded fallback that surfaces a visible degradation
warning or an in-memory intermediate. Pinned on the BYTES of both the migrated file and
the promoted baseline -- element, attribute AND foreign namespace declaration, since a
re-emitted fragment under an undeclared prefix is lost on the next load -- by
StrategyMigration.SACM23_LIB_002_StrategyMigrationPreservesUnknownContent, with a non-
vacuity guard asserting the pre-migration file really carried the content; confirmed to
fail (all six assertions) with the write routed back through the projection. All four of
the record's re-verification conditions were met, and the round-2 pass confirmed each of
them independently -- and then **FAILED the row for a defect at the root of all six
sites: the shared bridge is itself lossy for standard, typed SACM 2.3.**
`core::commands::BridgeLegacyMutationToLibrary` re-derives the document from
`RebuildSacmArgumentPackageFromParser`, whose rebuild handles only six element kinds and
clears every list first. Measured end-to-end on the bytes the bus writes, a single goal
rename on a conforming SACM 2.3 document **deletes** `AssertedArtifactSupport` (11.17),
`AssertedArtifactContext` (11.18), `ArgumentGroup` (11.2) and a nested `ArgumentPackage`
(11.4); **drops** `ArgumentReasoning@structure` (11.12) and
`AssertedInference@metaClaim` (11.10); and **cleared `isCounter`** (11.13), re-
serializing a rebuttal as an inference SUPPORTING the claim it attacks. The load emits
no diagnostics, so none of it is announced. This is not vendor content or unknown XML --
it is standard SACM the library reads and writes correctly, so it is this row's
requirement text falsified by measurement, on every bridged command (text edits,
terminology, ACP, package removal, tree reorder, proposals).

**The `isCounter` half is fixed**: `RebuildSacmArgumentPackageFromParser` now copies it
onto all three Asserted* families, pinned on the saved BYTES by
SaveFromLibrary.SACM23_LIB_002_BridgedEditPreservesCounterRelationships and confirmed to
fail before the fix -- it mattered most because dropping it does not lose a decoration,
it reverses the relationship's meaning, which the project's "never silently modify or
reinterpret safety arguments" constraint forbids outright.

**The structural half is now GUARDED rather than fixed**:
`BridgeLegacyMutationToLibrary` compares the document's element inventory against what
the projected package accounts for and REFUSES the command when anything is
unrepresentable, naming the SACM classes at risk, leaving the case untouched. Refusing
is the deliberate half of a choice: preserving would mean growing the legacy POD to
cover all of SACM 2.3 -- the model this migration exists to retire -- or new library API
to extract and re-adopt typed elements across the round trip, whereas refusing turns a
silent, unannounced corruption of a safety argument into a visible error, which is the
trade this project makes everywhere else and what the "never silently modify or
reinterpret safety arguments" constraint requires. Measured cost on ordinary cases: none
-- every fixture under tests/data and every argument in the repository's sample projects
projects completely, so only a document carrying one of the kinds below is refused, and
for those the alternative was losing the content. Two scope limits, stated rather than
implied: the check compares ids against the DOCUMENT inventory
(`list_document_elements`, packages included), so it catches a lost container -- the
round-3 probe showed the earlier element-level sweep passing a document whose empty
nested `ArgumentPackage` the bridge then deleted silently, now pinned by
SaveFromLibrary.SACM23_LIB_002_BridgedEditRefusesRatherThanDropEmptyNestedArgumentPackage
-- but it cannot see a lost ATTRIBUTE on a surviving element; and refusing is not
repairing -- the underlying loss is unchanged, and the real fix is retiring the bridge as
commands go native. Pinned by SaveFromLibrary.SACM23_LIB_002_BridgedEditRefusesRatherThan
DeleteUnrepresentableElements, which asserts the refusal names the class AND that the
tracked file is byte-unchanged, and was confirmed to fail (edit applied, file rewritten)
with the guard disabled.

**The underlying representability gap is unchanged and disclosed here rather than
claimed away**: the projection still cannot carry `AssertedArtifactSupport` (11.17),
`AssertedArtifactContext` (11.18), `ArgumentGroup` (11.2), `TerminologyGroup` (10.3),
`Event` (12.9), `Artifact` (12.7), `Activity` (12.8), `Term` (10.7), `Category` (10.8)
or a nested `ArgumentPackage` (11.4) -- but a bridged edit on a document carrying any of
them is now REFUSED by the guard above rather than applied. The NodeOnly removal that
used to bypass the guard entirely (round-3 probe a: silent deletion from the tracked file
AND the live document, reachable from the context menu) was first routed through the same
bridge, and since slice 3c of #350 applies through native seams instead -- so it neither
bypasses nor needs the guard, pinned by
SaveFromLibrary.SACM23_LIB_002_NodeOnlyRemovalRunsNativelyAndKeepsUnrepresentable
Elements, which requires all four unrepresentable kinds to survive in the SAVED bytes and
the ArgumentGroup to survive in the live document (a test asserting only success would
re-admit probe a). `ArgumentReasoning@structure` (11.12) and
`Assertion@metaClaim` (11.10) came OFF the lost list in round 4: the legacy POD carries
both and the bridge round-trips them
(SaveFromLibrary.SACM23_LIB_002_BridgedEditPreservesMetaClaimAndReasoningStructure,
confirmed to fail with the rebuild copy removed). The kind list is no
longer maintained by hand:
ProjectionCoverage.SACM23_LIB_002_BridgeRoundTripLosesOnlyTheKnownKinds sweeps the
library's own conforming SACM 2.3 fixtures, round-trips each through the bridge's
projection, and compares the element inventory kind by kind -- failing in BOTH
directions, so a newly-lost kind cannot land silently and a kind that starts surviving
cannot stay on the list. It immediately found a kind no verifier pass had named
(`TerminologyGroup`) and a second, distinct failure mode: **the bridge re-derive REJECTS
`artifact-full-valid.sacm.xmi` outright**, so on a document carrying a full clause-12
artifact model no bridged edit is possible at all -- a rename simply fails. That is
visible rather than silent and therefore less bad, but it also MASKS loss, because the
round trip never completes and whatever that fixture would have dropped is never
measured. The lost-kind list can only be trusted to be complete once the rejected list
is empty. That warning was borne out a second time under #295: the interchange-unit
fixtures for SACM23-CP-002/003/004 are rooted at a bare
`ArgumentPackage`/`ArtifactPackage`/`TerminologyPackage`, which no earlier fixture was,
and immediately measured four more lost kinds -- `Artifact` (12.7), `Activity` (12.8),
`Term` (10.7) and `Category` (10.8). New disclosures rather than new regressions: the
legacy POD has never carried any of them, and the only artifact-rich fixture that would
have shown it (`artifact-full-valid`) is on the rejected list. The rejected list is
still not empty. That warning has now been borne out: a fixture carrying an `Event` in a
document the bridge CAN round-trip (`tolerant-shorthands-valid.sacm.xmi`, added for
SACM23-RT-001 attribute coverage) immediately measured `Event` (12.9) as lost -- a kind
no verifier pass had named, masked only because the single Event-bearing fixture was on
the rejected list. The lost-kind list above is correspondingly longer, this is a new
disclosure rather than a new regression (the projection has never carried Event), and
the rejected list is still not empty. The bridge now refuses ANY document its projection
cannot fully account for -- the whole document inventory, packages included -- so the
disclosure duty has moved below kind level: the sweep compares kinds, not attributes, so
per-attribute fidelity on surviving elements (isCounter, metaClaim, structure,
assertionDeclaration) is maintained by hand and pinned test by test, and extending the
sweep to attribute fingerprints is an open follow-up from the round-3 record. Assurance
Forge's own repository fixtures contain none of these constructs,
which is why the Stage-3 projection baseline reported the projection "field-complete and
lossless": that claim is true over that corpus and false in general (SACM23-INT-001
carries the same sentence and needs the same qualifier). Latest verifier round:
docs/sacm/verification/2026-08-08-lib-002-resolution-round-3-FAIL.md, whose two blocking
findings the fixes above close and whose remaining conditions govern the flip; earlier
records: docs/sacm/verification/2026-07-26-lib-002-strategy-migration-round-2-FAIL.md,
docs/sacm/verification/2026-07-25-lib-002-source-of-truth.md (round 3 PASS,
rounds 1 and 2 FAILED alongside it) and docs/sacm/verification/2026-07-26-lib-002-undo-
library-primary-round-1-FAIL.md.

**Attribute-level loss, measured for the first time (#347).** Every sweep on this row
until now compared element KINDS. A kind sweep is blind to the failure mode where the
element survives and its meaning does not — the inventory balances perfectly while an
attribute is gone — and that is not hypothetical here: `isCounter` was dropped by the
bridge once already, re-serializing a rebuttal of the top claim as an inference
*supporting* it, and the fix was one hand-written assertion for that one attribute. Which
is the arrangement that let it through in the first place.
`ProjectionCoverage.SACM23_LIB_002_BridgeRoundTripKeepsEveryAttributeOfASurvivingElement`
replaces the hand-maintained assertions with a sweep: it fingerprints every
meaning-bearing attribute and reference end of every element, round-trips the same
conforming fixtures through `project_library_package_with_tags` +
`reload_document_keeping_compatibility_content`, and compares each surviving element with
itself.

Writing it found four losses nobody had measured:

| Lost | Clause | What it means when it goes |
|---|---|---|
| `Claim@isCitation` + `@citedElement` | 8.2 | A citation of another package's claim becomes an original claim. The argument gains a proposition it never asserted. |
| `Claim@abstractForm` | 8.2 | A concrete element's link to the pattern element it instantiates. Pattern provenance is severed. |
| `AssuranceCasePackage@gid` | 8.2 | SACM's model-global identifier on the case package — the handle other tools key on. |
| `Expression@element` | 10.10 | The ExpressionElements a structured Expression is built from; the production rule is left naming things that resolve to nothing. |

These are **disclosures, not regressions** — the legacy POD has never had a field for any
of them. But they differ from the lost *kinds* in the way that decides whether a user is
protected: the bridge's guard sweeps ELEMENTS. A document that would lose only attributes
is not refused. It goes through, the command reports success, and the attribute is gone
from the tracked file. The refusal guarantee this row rests on therefore covers element
deletion and not attribute deletion, and that gap is what closing the bridge
([#350](https://github.com/lasrod/assurance-forge/issues/350)) removes. Until then the
sweep gates the known-lost list in both directions, and a *changed* attribute value fails
outright — that has never been disclosed and never will be, because an element that keeps
an attribute and changes what it says is the reinterpretation the project's own hard
constraint forbids.

**Slice 3d: the subtree move is native, with two disclosed fallbacks
([#350](https://github.com/lasrod/assurance-forge/issues/350)).** `MoveSubtree` runs the
same `core::MoveSubtree` on a scratch projection, diffs what it decided
(`core::PlanMoveSubtreeFromDiff`) and writes that through the seams -- a new
`apply_add_relationship` for the relationship it creates, `apply_set_relationship_ends`
for the one it rewires, `apply_delete_element` for what it emptied. The mutator remains
the only thing that decides what a move means, so the flip does not re-decide the GSN
reading.

**Two shapes still bridge, and the reason is SACM 11.13 multiplicity rather than a
missing operation.** An AssertedRelationship has source [1..*] and target [1]; the legacy
parser model enforces neither, so the mutator can produce (a) a created inference carrying
only a `reasoning` and no source -- a bare-placed Strategy moved to a new parent -- and
(b) a surviving relationship the move emptied, because an inference with a `reasoning` is
not "dangling" by `IsParserRelationshipDangling` and so is not deleted when its only
sub-goal leaves. Both are reported by the plan BEFORE any seam runs, so the fallback sees
an untouched document; a refusal discovered after the first write is a hard failure
instead, because by then the argument is half-moved. That ordering rule and the apply
order live in one shared function (`commands::ApplyMoveSubtreePlanToLibrary`) used by both
the live command and the replayer, so live/replay cannot drift into disagreeing about it.
Pinned by TreeEditingCommand.MoveSubtreePlanRefusesAMoveThatEmptiesTheOldInference, which
asserts the fixture really produces the shape before checking that the plan refuses it.

The audit event records `new_relationship_id` so replay reuses the id the document
actually got rather than re-deriving one; events written before the field existed replay
by re-deriving, as they always did.

**Slice 3c: the audit projection was not reloadable for any artifact-bearing document
([#350](https://github.com/lasrod/assurance-forge/issues/350)).** Found while flipping the
NodeOnly removal, and recorded here because it is a defect in this row's own machinery
rather than in the command that exposed it.

`core::project_library_package` -- the projection BOTH sides of a replay verification are
hashed from -- rebuilds the argument package flat, from every element the POD projection
lists. That list includes the Artifacts that live in an `ArtifactPackage`, so each came
out twice: once as `<artifact>` in the artifact package, and again as an
`<artifactReference>` in the argument package **reusing the artifact's own id**. The
projected package therefore held two elements under one id, and
`library_canonical_hash`, which serializes the projection and loads it back, could not
reload it. `project_library_package_with_tags` never had the defect because it filters
elements by argument-package shell membership instead.

It survived because the three sides of a verification treat an unhashable projection
differently: the snapshot side fell back via `value_or` to a *differently normalized*
hash, the on-disk side only appends a diagnostic, and the replayed side fails hard. A
project holding such a document would have reported divergence permanently, with the one
diagnostic saying only "Failed to load replayed SACM through the library for
normalization". Exposing it needed a test that both mutated such a document successfully
and then verified -- which none did until the NodeOnly flip. Reproducible with **no
mutation at all**: `SaveFromLibrary.AuditProjectionOfAnArtifactBearingCaseReloadsThrough`
`TheLibrary`, which fails without the fix and names the colliding id rather than only
reporting a missing hash.

Fixed by dropping, from the rebuilt argument packages, any id the terminology or artifact
package projections already carry. The filter runs AFTER the rebuild rather than on its
input, because the rebuild reads terminology elements to classify which artifact
references are terminology references, and pre-filtering would change that
classification as a side effect. The snapshot-side fallback now records a diagnostic when
it engages, so a recurrence says so instead of reporting divergence with nothing to read.

**Slice 3c: both `RemoveElement` modes are native.** `NodeOnly` reparents -- a child's
inference is RETARGETED onto the removed node's parent, and a strategy interposed as a
reasoning has that reasoning cleared -- which no set of per-id deletes expresses. It now
runs `core::ReparentChildrenToParent` (declared in `element_factory.h` for this caller)
on a scratch projection, mirrors the endpoint rewrites through
`apply_set_relationship_ends`, and only then applies the planned deletes, whose own
`ScrubReferences` policy handles the scrub. The reparent must be mirrored ALONE: diffing
after the whole of `core::RemoveElement` also picks up its scrub, and a context whose only
target was the removed node then comes back with an empty target list, which no
relationship may hold. Three commands still bridge.

**Phase 1 of the retirement: eleven commands off the bridge ([#350](https://github.com/lasrod/assurance-forge/issues/350)).**
The ten terminology commands and `RemoveArgumentPackage` now apply through the
`sacm_adapter` seams the audit replayer had already used for those same events since
Phase 2 slice 2a. Each keeps the guarded bridge as a fallback for shapes the seam does
not support, so the invariant that no command mutates the legacy package in place while a
document is present is unchanged; what changed is which route runs first. Fifteen
commands still bridge, listed on the
[migration plan](../architecture/legacy-bridge-migration-plan.md).

Proving a flip is harder than performing one, and the obvious assertions do not do it:
both routes set `library_primary`, and since the round-4 fix both preserve vendor
content, so neither `ctx.library_primary` nor a vendor-marker byte pin can tell them
apart. The one observable that can is the guard's refusal. `SaveFromLibrary.SACM23_LIB_002_FlippedTerminologyCommandsRunOnACaseTheBridgeRefuses`
runs all ten commands against `argumentation-full-valid.sacm.xmi` — the fixture the
bridge refuses outright for its ArgumentGroup, AssertedArtifactSupport,
AssertedArtifactContext and second ArgumentPackage — and re-checks all four markers in
the saved bytes after each one. A command routed back through the bridge fails it, and
disabling the flip wholesale was confirmed to fail it. `SACM23_LIB_002_NativeArgumentPackageRemovalPreservesUnknownContent`
does the same for the removal over a nested-ArgumentPackage fixture.

**The four lost attributes above are untouched by this phase.** They are lost in the POD
round trip the remaining fifteen bridged commands still perform; they come off
`KnownLostAttributes()` when phase 4 deletes the bridge, not before. What phase 1 does
shrink is *how often a user meets the refusal*: glossary work on a case carrying an
unrepresentable kind used to be impossible and now is not.

Two behaviours changed, disclosed rather than absorbed:

- **Deleting a term an argument package still references asks first, and removes the
  references on consent.** `core::DeleteTerminologyTerm` accepted such a delete and left
  the ArtifactReference naming an id that no longer resolved; the seam refuses it
  (SACM-CMD-007, the library's cross-package cascade guard). The refusal is what the
  *replayer* already did, so before the flip such a delete succeeded live and produced an
  audit log that could not be replayed — a latent defect the flip removes rather than
  introduces. But refusal is the wrong end state, because the term's contexts are exactly
  what the user is trying to be rid of, so the delete now previews and asks.

  Three things make that safe rather than merely convenient. **(1)** The library's own
  cross-package cascade is deliberately not used: it removes the *entry* — the term leaves
  the reference's `referencedArtifact` list — leaving an ArtifactReference that points at
  nothing and an AssertedContext still drawing a context node on the canvas. A husk, and
  arguably worse than the dangling id it replaces. `plan_terminology_delete_cascade`
  instead deletes each reference that exists *solely* to name the term, then the term; a
  reference that also points elsewhere survives, scrubbed, and is reported as modified
  rather than removed. Sparing that shared reference is what made the cross-package
  policy matter: with the default (reject) the term delete that follows was refused
  *after* the plan's earlier deletes had applied, reporting failure over a half-mutated
  document — found in review of
  [#360](https://github.com/lasrod/assurance-forge/pull/360) and pinned by
  `LibraryPrimaryEditFlip.TerminologyTermCascadeSparesASharedReferenceWithoutStranding`.
  The cascade reaches across instead, which hands the referrer to the scrub policy and
  produces exactly the outcome the confirmation described. **(2)** Preview and apply are the same plan in the same order under
  the same policy (`preview_delete_elements` on a scratch copy), so the dialog cannot
  promise one thing and the command do another; the recorded `removed_ids` filters clause
  8.7 attachments exactly as the preview does, so the audit entry and the confirmation
  count agree. **(3)** The consent is an audit payload field, never re-derived. A replay
  has nobody to ask, and deriving it from the document would let a later state answer a
  question the user answered differently; an event with no `cascade_references` field
  replays as `false`, the behaviour it was written under. The legacy replay branch, which
  has no cascade to offer, fails loudly on such an event rather than certifying a
  convergence that does not hold.

  Pinned by `LibraryPrimaryEditFlip.TerminologyTermDeleteRefusesWhileAnArgumentPackageStillReferencesIt`
  (the un-consented default, asserting the legacy behaviour it replaces),
  `...TerminologyTermDeleteWithConsentRemovesTheReferencesToo`,
  `SaveFromLibrary.SACM23_LIB_002_ConsentedTermDeleteRemovesTheReferencesFromTheSavedFile`
  (bytes — the canonical hash cannot distinguish a removed reference from a surviving
  husk), and `TerminologyActions.*DeleteTerm*` for the wiring that decides whether the user
  is asked at all. The last of those also pins that no cascade is offered without a command
  bus: a file opened outside a project reaches the commands with no library document
  (#347), so the dispatch could not honour a consent it had collected.
- **Gids are planned by the caller, not reconstructed by the seam.** The create and
  associate seams took an id and minted `gid-<id>`, on the reasoning that a fresh id's
  base gid is always free. Gid space is independent of id space, so it is not: a
  document already carrying `gid-TP1` on an unrelated element makes
  `core::GenerateUniqueGid` emit `gid-TP1-2`. The seams now take the gid alongside the
  id — the live command plans it with the legacy generator, the replayer passes the one
  the payload recorded — which closes a divergence that had existed on the replay side
  independently of this phase. `LibraryPrimaryEditFlip.TerminologyCreateKeepsTheLegacyGidWhenTheBaseFormIsTaken`
  pins it, and fails when the seam ignores the requested gid.

**Slice 2a of phase 2: the package removals and the gid.** `SetElementGid` and the two
remaining package removals (`RemoveTerminologyPackage`, `RemoveArtifactPackage`) now apply
through the seams, taking the bridged count from fifteen to twelve, with all three replay
branches moved off `BridgeViaLegacy` in the same change so live and replay run one code
path. `apply_set_gid` is new and is a single library operation — the app decides the value
and the seam stores it, so there is nothing for a bridge to reproduce. Routing is proven
the phase-1 way, by running the three on a nested-ArgumentPackage fixture a bridged edit is
refused on
(`SaveFromLibrary.SACM23_LIB_002_FlippedPackageAndGidCommandsRunOnACaseTheBridgeRefuses`).

Two disclosures, pointing opposite ways:

- **RemoveTerminologyPackage keeps its guard.** `core::DeleteTerminologyPackage` refuses a
  package that still holds categories, terms or expressions; `apply_delete_package` deletes
  recursively. Flipping without re-stating the guard would have converted "empty this
  first" into "the glossary is gone" on the same click, so it moved into the command,
  checked against the same projection the legacy mutator ran on. The REPLAY branch
  deliberately does not re-check it: the guard gated whether the event was ever recorded,
  and re-applying it during replay would refuse to reproduce history a user legitimately
  made. Pinned by
  `LibraryPrimaryEditFlip.RemoveTerminologyPackageStillRefusesANonEmptyPackage`.
- **RemoveArtifactPackage stops stranding references.** `core::DeleteArtifactPackage`
  erased the package and left every ArtifactReference citing its artifacts pointing at an
  id that no longer resolved; the seam scrubs the reference instead. The ArtifactReference
  itself survives either way — it is a drawn Solution node, and removing evidence from the
  argument is not what "delete this artifact package" asked for. Measured on both sides by
  `LibraryPrimaryEditFlip.RemoveArtifactPackageScrubsTheReferenceTheLegacyMutatorLeftDangling`.

That second one changes what the legacy replay oracle can certify, and the change is worth
naming. With `RemoveArtifactPackage` seam-mapped on both live and replay sides, the two
agree; the LEGACY replay now diverges for a cited package, because it still runs the
mutator that leaves the dangling reference. `LibraryReplayConvergence.RemoveArtifactPackageConverges`
therefore uses an empty package, and the cited case is verified where the requirement
actually lives — live against its own replay, via `VerifyProject`. The legacy oracle has no
production caller; treating its agreement as the goal, rather than live/replay agreement,
would have meant preserving a defect to keep a test green.

The app-level guards the legacy mutators carried — a required package name, a required
term value, a category still assigned to terms — are Assurance Forge editing rules, not
SACM invariants, so the seams do not enforce them. They moved into the commands, checked
against the same projection the legacy mutator would have run on, and refuse exactly what
they refused before with the same messages. A flip that had simply called the seams would
have dropped all three without a test noticing.

## SACM23-INT-001 — Assurance Forge adapter — load, project, edit, save through the library

The adapter seam itself: whether the application's load, projection, edit and save paths go through the library rather than around it.

Matrix row: [`SACM23-INT-001`](sacm-conformance-matrix.md).

Phase 9 **Stage 4**: the app **loads and projects** through the library-owned document
(`loaded_case` is projected from it). The projection is field-complete and lossless
**over the Stage-3 corpus** (slice 1) and preserves ACPs; the parallel-load baseline
holds all remaining differences, every one a case where the library is more correct than
the legacy parser. That corpus qualifier is load-bearing and was missing: the baseline
compares the library against the LEGACY PARSER over six Assurance Forge files, so it can
only speak for constructs those files contain -- and they contain no clause-11 group, no
AssertedArtifact* relationship and no TerminologyGroup. The unqualified sentence read as
a general claim, and the 2026-07-26 round-2 pass on SACM23-LIB-002 falsified the general
version by measurement. The completeness half is now evidenced beyond that corpus:
ProjectionCoverage.SACM23_INT_001_ProjectionEmitsEveryNonContainerElement sweeps the
library's own conforming SACM 2.3 fixtures and requires `project_case` to emit every
element the library read, allowing exactly two exclusions (packages, and clause-8.7
utility elements) and failing if it quietly makes a third. It carries a corpus guard
asserting the fixtures still exercise `ArgumentGroup`, `AssertedArtifactSupport` and
`TerminologyGroup`, so it cannot decay back into re-proving what the Stage-3 corpus
already proved.

**FIELD** completeness remains corpus-bound -- per-field fidelity is still measured only
against the legacy parser over those six files, and extending it needs a comparison that
does not route through a reader which cannot express most of SACM 2.3. Phase 9 **Stage
5** (in progress): the edit seam `sacm_adapter::apply_text_edit` routes text edits
through library operations — a rename via `SetName`, and a claim's `content` (its
clause-8.9 primary Description) via `SetDescription`, targeting the front Description's
stored language so a lang-less legacy statement is overwritten in place rather than
gaining a parallel entry. Tests prove each reproduces the legacy `SetElementTextField`
edit on the edited field, that a bad id fails unchanged, and that unmapped (field, kind)
combinations report `supported == false` instead of writing something the projection
would not read back. The `apply_add_child` seam routes the compound "add element +
relationship" edit through `CreateClaim`/`CreateArtifactReference` +
`CreateAssertedRelationship` (+ `SetAssertionDeclaration` for an assumption); a
structural test proves Goal/Solution/Context/Assumption reproduce the legacy
`AddChildElement` (ids differ between the generators, so it compares structure, not
ids).

**Stage 7 (step 1):** Strategy and Justification are now supported.
`apply_add_child(Justification)` creates a Claim with `assertionDeclaration = axiomatic`
(the pure-SACM mapping, not the legacy non-standard `justification` literal) plus a
vendor `assuranceForge.gsn.role = Justification` tag; `project_case` translates that
pair back to the app's internal justification role, and a round-trip test proves the tag
(and rendered role) survive an XMI save/load. `apply_add_child(Strategy)` creates the
`ArgumentReasoning` alone and tags the goal it will support
(`assuranceForge.gsn.strategyTarget`) — no inference yet, since a bare strategy
inference would violate SACM `source [1..*]` (an invalid transient state the legacy
model tolerated); the single `{target=goal, reasoning=strategy, source=sub-goal}`
inference is materialized when the first sub-goal is added (part 2, following
increment). The `apply_add_child`/`apply_challenge` seams also take optional caller-
supplied element and relationship ids (default library-generated) so a library-primary
audit replay can reproduce the exact ids the legacy generator recorded. See
docs/sacm/sacm-gsn-mapping.md and sacm-gsn-metamodel-gaps.md. The `apply_challenge` seam
routes a dialectic challenge (`isCounter = true`) through the same create + relationship
pattern; a structural test proves both counter-argument and counter-evidence, and a
challenge whose target is itself a relationship (challenging an inference), reproduce
the legacy `AddChallenge`. The `apply_add_acp` seam adds an Assurance Claim Point (a
must-support GSN v3 feature) by writing the `assuranceForge.acp` vendor TaggedValues via
`AddTaggedValue`, sharing core's deterministic `ACP<n>` id generator so the synthesized
record matches the legacy `core::acp::AddAcp` field-for-field, id included; scoped to
element ACPs on an `ArtifactReference` (the only kind `ElementEligibleForAcp` accepts),
refusing ineligible claims and (for now) relationships. The `apply_delete_element` seam
deletes one element via the library's `DeleteElement` with `ScrubReferences` -- scrub-
then-drop, matching the legacy `core::RemoveElement`; NOT
`DeleteReferencingRelationships`, which would cascade away a strategy's shared inference
when one of several sub-goals is removed (so no relationship is left dangling either
way); a test proves that on a leaf it leaves the same element set as the legacy
`RemoveElement`. Cascading a whole subtree composes this primitive over the removal
plan; the NodeOnly *reparent* case has no SACM operation (relationships cannot be
retargeted) and is recorded in docs/sacm/sacm-gsn-metamodel-gaps.md.

**Live-path flip (in progress):** `CommandContext` now carries the `LibraryDocument`,
and `UpdateElementTextCommand` is now **library-primary via a live bridge** (Phase 2
slice 2b-2): it applies the legacy `SetElementTextField` onto the library through
`core::commands::BridgeLegacyMutationToLibrary` (project the library to a scratch
package → mutate → reload the library from it), reproducing the legacy two-slot
content/description result so it converges with the identically bridged audit replay
with no migration, and leaves the caller's legacy views for the frame-boundary re-derive
(`SACM23_INT_001_UpdateElementTextIsLibraryPrimary`,
`LibraryPrimaryEditFlip.TextEditsMatchLegacyCanonicalHash`). Any audited command that
does not sync the library natively triggers a re-derive of `library_document` from the
authoritative package's serialization in `CommandBus::Execute`, so it never drifts; this
touches neither the audit log nor the saved package.

**Phase 2 slice 2c-1:** ACP record CRUD (add/remove/upsert) now routes through the
audited command bus as library-primary commands
(`core::commands::AddAcpCommand`/`RemoveAcpCommand`/`UpsertAcpCommand`), each flipping
via `ApplyLibraryPrimaryOrLegacy` around the legacy `core::acp::*` mutator; `AddAcp`
records its deterministic `ACP<n>` id so replay forces it via the new
`core::acp::AddAcpWithId`, and the audit replayer applies the three events on both the
legacy model (`ApplyEvent`) and the library (`ApplyEventToLibrary`, bridged), so an ACP
edit is a recorded, replayable transaction that leaves `library_document` consistent
(`AcpController.SACM23_INT_001_AuditedAcpAddIsLibraryPrimary`; convergence in
`LibraryReplayConvergence.AcpAddAndUpsertConverge`/`AcpRemoveConverge` and
`LibraryPrimaryEditFlip.AcpEditsMatchLegacyCanonicalHash`).

**Phase 2 slice 2c-2:** `CreateConfidenceArgumentTreeForAcp` (a compound op minting an
argument-package id + a top-goal id) is now audited too, via
`core::commands::CreateConfidenceArgumentTreeForAcpCommand` and the new
`core::acp::CreateConfidenceArgumentTreeForAcpWithIds` that replay forces with the two
recorded ids (`LibraryReplayConvergence.AcpCreateConfidenceTreeConverges`,
`LibraryPrimaryEditFlip.AcpConfidenceTreeMatchesLegacyCanonicalHash`). With that,
**every** ACP edit is a recorded, library-primary transaction and `AcpController` is a
pure dispatcher -- the `sync_library_document` re-derive callback is retired from it
(the no-bus dispatch path keeps the library in step generally). Making the
create/challenge commands route *natively* through the seams (rather than re-derive)
needs the seams to accept caller-supplied ids so library ids match the legacy generator;
that is a library-primary (Stage 7) refinement.

**Phase 2 slice 2a (library-primary replay seam parity):** the terminology
create/associate/visible-context seams now mint the legacy `gid-<id>`
(`GenerateUniqueGid`) via the new `SetGid` library op, so the library-primary audit
replay (`ApplyEventToLibrary`) routes the gid-minting terminology events through the
seams instead of the legacy bridge and converges on the RAW canonical hash
(`TerminologyCreateAndAssociateConverge`,
`AddTerminologyVisibleContextBridgeConverges`). The claim-note seam is completed:
`apply_text_edit(Description)` on a claim writes the SECOND Description (a note) via the
new `SetDescriptionAt` op at slot 1, leaving the front statement intact
(`SACM23_INT_001_DescriptionEditWritesClaimNoteToSecondDescription`; unsupported when
the claim has no statement to anchor it,
`SACM23_INT_001_DescriptionEditUnsupportedForStatementlessClaim`) — the live library-
primary edit path uses it natively. The `UpdateElementText` **Content/Description**
replay events stay bridged: a claim's content edit cannot reach legacy parity at the
seam because a content-only claim and a lone-`description=` claim collapse to a single
clause-8.9 Description in the library yet carry different legacy `content`/`description`
fields — a deliberate model difference, not a missing op (diagnosed with measured hashes
in `event_replayer.cpp`). `AppState::load_file` surfaces Warning-and-above library
diagnostics on the **success** path -- deduplicated by code (one real file emits 222
warnings, 220 of them "generated an id") and led by SACM-XMI-009 -- into
`status_message`, rendered by `sacm_viewer_panel`. Before this a non-conformant file (an
ODE container) opened silently and the first save rewrote it with nothing on screen
having said so.

**Every site that re-derives the library document from a projection of itself now
preserves compatibility content.** That pattern appeared in four places and lost
preserved vendor elements/attributes at each: the live bridge (`library_bridge.cpp`),
the audit replayer's twin (`event_replayer.cpp`, since deleted in favour of delegating
to the one implementation), `AppState::sync_library_document`, and the command bus's
Stage-5 net for unflipped commands. The last two were reachable without a project at all
-- a file opened standalone takes the no-bus dispatch branch, which syncs after every
command, and the next save wrote the degraded document. All four now use
`sacm_adapter::reload_document_keeping_compatibility_content`, which restores through
`sacm::compat::adopt_preserved_content` what no POD projection can carry. Pinned on
saved bytes -- never on a canonical hash, which drops the same content on both sides and
is structurally blind to this defect class -- by
SaveFromLibrary.SACM23_INT_001_NoBusEditPreservesUnknownContentThroughSync,
...UnflippedBusCommandPreservesUnknownContentInTheDocument (the Stage-5 net, whose only
prior coverage was a hash comparison that could not fail), and the SACM23-LIB-002
bridged/restore tests. The Stage-5-net test is cited here rather than on SACM23-LIB-002
although `command_bus.cpp` is cited on both rows, so neither row assumes the other owns
the evidence. This is the row's own edit clause, and the citation gap that hid it --
`library_bridge.cpp` was cited on LIB-002 but not here -- is closed above. Delete
confirmation is backed by library operation previews rather than the legacy removal plan
alone **for `NodeAndDescendants` removals**; `NodeOnly` reparents rather than deletes
and is deliberately not previewed (SACM23-INT-002). Verified by
docs/sacm/verification/2026-07-25-int-001-edit-path.md (round 5; rounds 1 and 4 FAILED
and are recorded alongside it). Save no longer serializes the legacy `sacm_package` —
Stage 7 routes every save site through the library-owned document (see SACM23-LIB-002).
See docs/sacm/sacm-stage3-projection-baseline.md.

## SACM23-INT-002 — Delete confirmation integration — library previews in the application UI

Whether the application's destructive-delete UI is driven by the library's own operation preview rather than by a second implementation of the same rules.

Matrix row: [`SACM23-INT-002`](sacm-conformance-matrix.md).

`sacm_adapter::preview_delete_elements` asks the library what deleting a set of elements
would do and flattens the answer to strings (`DeleteEffect{element_id, kind, name,
is_relationship, deleted}`), split into `requested` (what the user picked) and
`consequential` (what the delete reaches anyway). The preview is computed on a **scratch
copy** — serialize tolerantly, reload, apply the deletes there — because the
consequences of deleting a SET are not the union of the per-element consequences: under
`ScrubReferences` an inference with three sources survives losing one and dies when all
three go, so previewing element-by-element would *understate* the damage, the one
direction a delete confirmation must never err in
(`SACM23_INT_002_DeletePreviewUsesSetSemanticsNotPerElementUnion`). It uses the same
policy as `apply_delete_element`. Two tests keep preview and apply from drifting, at
different levels: `..._DeletePreviewMatchesWhatApplyDoes` compares the predicted
deletions against what the library seam removes, and
`ElementEditControllerTest.SACM23_INT_002_ConfirmedRemovalMatchesThePreviewExactly`
previews, confirms through the same dispatch path the UI uses, and requires the
resulting model to match the promise in both directions (nothing unannounced removed,
nothing announced-as-surviving gone).

**Scope: `NodeAndDescendants` only.** `RemoveMode::NodeOnly` REPARENTS the removed
node's children -- `core::ReparentChildrenToParent` retargets a child's inference rather
than deleting it -- so a delete-modelled preview would announce that inference as removed
when it survives. `BuildRemovalPreview` therefore declines to preview NodeOnly and the
modal says the preview is unavailable, rather than showing a confident wrong answer
(`..._NodeOnlyOffersNoPreviewRatherThanAWrongOne`, which also pins that the retargeted
inference really does survive).

**Still true after slice 3c of #350, and worth stating precisely because the apply side
moved.** The library now HAS a retarget (`SetRelationshipEnds`), and the NodeOnly *apply*
path uses it, so the reason for the gap is no longer "no operation exists". What remains
is that `preview_delete_elements` answers a delete question: its vocabulary is
`deleted` per element, with no way to say "this inference survives, pointing somewhere
else". Teaching the preview to report retargets is the remaining work, now unblocked
rather than done; recorded in docs/sacm/sacm-gsn-metamodel-gaps.md. Utility elements (clause 8.7
Description/Note/TaggedValue) are filtered out: they are attachments deleted with an
owner that is already listed, and listing them turned "this goal and its inference" into
a dozen rows of bookkeeping. Assurance Claim Points are the exception and are re-added
by `AppendAcpConsequences`: an ACP is stored as `assuranceForge.acp.*` TaggedValues but
is a first-class record with its own panel, and when its owner is a *consequential*
deletion the user never selected, the blanket filter meant an ACP and its confidence
argument package could be destroyed with nothing on screen naming them. Asserted against
the library document rather than the `loaded_case.acps` cache, which this path does not
rebuild. `ElementEditController::RemoveSelected` now builds the preview before deciding
whether to confirm, and confirms whenever the removal reaches past the selection —
**including a single-element plan with consequences**, which previously deleted with no
dialog at all (removing a sub-goal silently took its inference). A delete that genuinely
reaches nothing else still applies immediately; a dialog on every delete is a dialog on
none. The modal names each affected element and separates "Will be removed" from "Will
be modified (references removed)". Advisory, not blocking: the removal is applied by
`RemoveElementCommand` -- through the library seam when the flip engages, through
`core::RemoveElement` otherwise -- so library diagnostics are shown rather than used to
disable the button; when no library document is available the modal says the preview is
unavailable instead of presenting the legacy plan as library-confirmed. Verified by
docs/sacm/verification/2026-07-25-int-002-delete-preview.md.

## SACM23-COMPAT-002 — Third-party interoperability corpus

Files produced by independent SACM tools, and what they cost to accept.

Matrix row: [`SACM23-COMPAT-002`](sacm-conformance-matrix.md).

The EMF/GSN dialect now parses: `argumentationElement` role spelling, containment-path
references in place of absent `xmi:id`, and GSN types resolved to the SACM classes they
specialize (mapping taken from gsn.ecore's own eSuperTypes). Verified against a real
public file: 39 SACM elements parsed where previously 0. GSN `SupportedBy`/`InContextOf`
endpoints are swapped on import: GSN writes source=the supported goal, while clause
11.14 defines source as the premise that infers the target. Without the swap every
inference in the argument is reversed. An extension type whose SACM supertype is
*abstract* (`Context`/`ChoiceNode`/`Choice`/`AwayContext`, all specializing
`ArgumentAsset`) is now kept verbatim in its parent's preserved content and re-emitted
by a compatibility save; strict save refuses the document rather than writing it without
the fragment. Previously the reader emitted a "preserved as compatibility content"
diagnostic and then dropped the element: `read_xsi_type` returned `std::nullopt` both
for "no xsi:type" and for "preserve this subtree", so callers fell through to kind
inference on the abstract role name, failed, and returned having preserved nothing — a
diagnostic asserting the opposite of what happened. `read_xsi_type` now returns an
explicit `XsiTypeResult`, and SACM-extension namespaces are recorded as re-declarable so
the re-emitted prefix is declared on save (without that, the fragment's attributes were
lost on the next load, as for vendor prefixes under SACM23-COMPAT-001). Two defects this
row previously carried are now fixed. (1) A relationship endpoint naming a preserved
element no longer dangles: preserved subtree ids are recorded on
`Document::preserved_element_ids()`, and such a reference is reported as `SACM-REF-003`
(present but untyped, Warning) instead of `SACM-REF-001` (missing, Error) -- the old
behaviour failed validation for every GSN document containing so much as a Context,
reporting an intact argument as structurally broken
(`SACM23_COMPAT_002_ReferenceToPreservedElementIsNotDangling`). (2) A preserved fragment
is re-emitted in the sibling slot it occupied rather than appended after the typed
children. That is not cosmetic: the EMF dialect addresses elements by containment
position, so appending renumbered every later sibling and silently repointed any
positional reference into that package. `PreservedFragment` now carries `{xml, role,
index}`, the writer interleaves against a pre-insertion snapshot of the typed children,
and `semantic_compare` covers the slot so a round trip that moved a fragment is a
difference rather than a pass
(`SACM23_COMPAT_002_PreservedFragmentKeepsItsSiblingPosition`).

**CI now parses bytes produced by other people's tools.** Three files are committed
under libs/sacm/tests/data/interop-thirdparty/, unmodified -- two under EPL-2.0 and one
under MIT -- with source, upstream commit and the full licence text in that directory.
`.gitattributes` marks it `-text`, because line-ending conversion on checkout would
rewrite them and quietly falsify the byte-for-byte claim; all three staged blobs
sha256-match their upstream commits. `mobstr-safetycase.integration` (MobSTr automotive
dataset) is an ODE `DDIPackage` embedding a SACM assurance case: it imports with a SACM-
XMI-009 non-conformance warning, validates with zero errors, round-trips, and saves as
conformant SACM rather than the container. `sysmline-easyexample.assurancecase`
(EMF/ACME, omg.sacm/2.2 per-package + GSN 3.0) imports and round-trips while the
validator correctly reports its genuine rule violations -- it is template output with
placeholder values, and reading third-party content and calling it invalid is the
library working. `deis-etcs.model` (DEIS ETCS, MIT) is the third: an ODE container that
genuinely carries other models -- three `odeProductPackages` siblings, 503 non-SACM
elements -- so it is what makes the SACM-XMI-009 loss testable rather than theoretical
(MobSTr's container holds nothing but the assurance case). Two further files (ACEditor
MBAC output, which imports and validates with ZERO errors, and SysMini) carry no
declared licence, cannot be redistributed, and run through the opt-in
`SACM_INTEROP_CORPUS=<dir>` harness, which skips visibly when unset so a green run is
never mistaken for evidence it did not gather. Five real files across three previously
unseen dialects -- SACM 2.1 with `gid=`-only identity, the ODE merged namespaces, EMF
per-package + GSN -- all read with no further reader change in this slice beyond
foreign-container support. Remaining and stated as a gap rather than claimed away: three
projects is a floor, not breadth, and Papyrus (decision #20's original target) has still
produced nothing we have seen. Corpus, licence status and gap list: docs/sacm/sacm-
interop-corpus.md. Verified by docs/sacm/verification/2026-07-25-compat-002-third-party-
corpus.md.**

