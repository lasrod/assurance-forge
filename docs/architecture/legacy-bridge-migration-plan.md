# Migration plan: remaining legacy projections and bridges

The unchecked deliverable of
[#291](https://github.com/lasrod/assurance-forge/issues/291): which components
exist only to carry the migration from the legacy SACM model
(`src/legacy_sacm`, `parser::AssuranceCase`) to the reusable library
([`libs/sacm`](https://github.com/lasrod/assurance-forge/tree/main/libs/sacm)),
and what has to be observably true before each of them can be deleted.

Where the migration stands: the library-owned document is authoritative for
load, and every bus-dispatched edit reaches disk through it — natively or via
the guarded bridge (`SACM23-LIB-002` in the
[conformance matrix](../sacm/sacm-conformance-matrix.md)); the legacy parser
load fallback is gone, and the one save path outside the bus (the no-bus
dispatch for a file opened without a project) is a disclosed, tracked
exception ([#347](https://github.com/lasrod/assurance-forge/issues/347)).
Edits are split: some commands mutate the library
natively, the rest go through a bridge that projects the document into the
legacy model, mutates that, and re-derives the document. The bridge is lossy
for standard SACM outside the legacy POD subset and therefore *refuses* any
document its projection cannot fully represent rather than silently deleting —
the full history, including the six sites that each rebuilt
the live document from a lossy projection, is the
[integration preservation record](../sacm/sacm-integration-preservation.md).
The [historical integration plan](../sacm/sacm-assurance-forge-integration-plan.md)
records the stage framing this page inherits; it is not current guidance.

Two ground rules, inherited from that history:

- **Preservation evidence pins saved bytes, never a canonical hash.** The hash
  projects through the same lossy projection on both audit sides, so it is
  structurally blind to exactly the loss class this migration produces.
- **Every phase ends in something observable**: a component deleted, a test
  that exists and passes, or a matrix row whose note shrinks.

## Inventory: migration-era components

| Component | What it does | Why it still exists | Depended on by |
|---|---|---|---|
| [`library_bridge.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/library_bridge.cpp) — `BridgeLegacyMutationToLibrary`, `ApplyLibraryPrimaryOrLegacy` | Projects the document to legacy models, runs a legacy mutator, re-derives the document; refuses when the projection cannot represent the case | 6 commands have no native seam yet (26 before phase 1, 15 before 2a, 12 before 2b, 10 before 2c) | Every bridged command below; the audit replayer; [`strategy_migration.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/strategy_migration.cpp) |
| [`event_replayer.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/event_replayer.cpp) — `BridgeViaLegacy` + bridged replay branches | Library-primary replay for events with no seam parity; delegates to the one bridge implementation | Recorded history must replay convergently with how it was recorded | Audit verification, restore-from-audit, undo, history view |
| [`sacm_argument_sync.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/sacm_argument_sync.cpp) — `RebuildSacmArgumentPackageFromParser` | Rebuilds a legacy `sacm::ArgumentPackage` from the POD model (six element kinds, clears lists first) | The bridge and the audit-hash projection are built on it | `library_bridge.cpp`, `library_package_projection.cpp` |
| [`library_package_projection.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/library_package_projection.cpp) — `project_library_package[_with_tags]`, `library_canonical_hash*`, `library_xmi_from_package` | Document → legacy package, for canonical hashing (tagless) and for the bridge (tag-carrying); projection-bytes fallback serializer | The canonical hash is *defined* over the legacy package; unflipped commands still autosave projection bytes | Command bus, audit verifier, replay, guarded save fallbacks |
| [`library_load.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/library_load.cpp) — `reload_document_keeping_compatibility_content` | Re-derives the document from projection bytes while restoring preserved vendor content | Exists only because projections get *reloaded*; used by the bridge, the Stage-5 net, and `AppState::sync_library_document` | `library_bridge.cpp`, `command_bus.cpp`, `app_state.cpp` |
| [`command_bus.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/command_bus.cpp) flip plumbing — `library_primary` / `library_synced` / `allow_library_primary`, scratch rebuild, Stage-5 net, fallback serializations | Runs exactly one of two derivation directions per command | Both directions are still live | Every audited command |
| [`projection_diff`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/projection_diff.h) | Stage-3 parallel-load baseline: library projection vs legacy parser | Still the only per-field fidelity evidence (`SACM23-INT-001`'s corpus qualifier) | Baseline tests only |
| [`src/legacy_sacm`](https://github.com/lasrod/assurance-forge/tree/main/src/legacy_sacm) | Legacy model types, `parse_sacm` / `serialize_sacm` | The bridge serializes through it; the canonical hash hashes its types; guarded save fallbacks remain | `core`, `parser`, the bridge, hashing |
| [`src/parser`](https://github.com/lasrod/assurance-forge/tree/main/src/parser) — `parser::AssuranceCase` | The flat POD model the UI renders from; legacy XML parsing | The POD is now a *derived view* (projected via [`case_projection`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/case_projection.h)), no longer a parse result; the legacy parse path has no load caller | `core`, `ui`, `export`, `app` (render + tree building) |

Not migration-era, despite sitting on the same seam: `sacm_adapter`'s
`document_edit` seams, `library_load`'s load/save, and `case_projection` as the
*render* projection are the target architecture
([Layers and ownership](layers-and-ownership.md)). What retires is the
projection **as a write path**, not the projection.

## Which commands are bridged vs library-primary today

There is no central flip list. Each command opts in inside its `Apply`; the
kill switch is `CommandContext::allow_library_primary`
([`command_bus.h`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/command_bus.h),
default `true`, assigned nowhere under `src/app/`).

**Library-primary, native seams** (mutate the document directly, set
`ctx.library_primary`; the bus serializes the document):

| Command | Seam | Site |
|---|---|---|
| CreateTopGoal | `apply_add_top_goal` | [`element_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/element_commands.cpp) `CreateTopGoalCommand::Apply` |
| CreateChildElement | `apply_add_child` | `CreateChildElementCommand::Apply` |
| CreateChallenge | `apply_challenge` | `CreateChallengeCommand::Apply` |
| RemoveElement (`NodeAndDescendants` only) | `apply_delete_element` per planned id | `RemoveElementCommand::Apply` |
| RemoveRelationship | `apply_delete_element` | `RemoveRelationshipCommand::Apply` |
| Undo | move-assigns the replayed document; deliberately ignores the kill switch | [`undo_command.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/undo_command.cpp) `UndoLastTransactionCommand::Apply` |
| All ten terminology commands | `apply_create/update/delete_terminology_*`, `apply_associate_terminology_term`, `apply_add_terminology_visible_context` | [`terminology_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/terminology_commands.cpp) (phase 1) |
| RemoveArgumentPackage | `apply_delete_package` | [`package_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/package_commands.cpp) `RemoveArgumentPackageCommand::Apply` (phase 1) |
| RemoveTerminologyPackage, RemoveArtifactPackage | `apply_delete_package` | `package_commands.cpp` (slice 2a) |
| SetElementGid | `apply_set_gid` | [`gid_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/gid_commands.cpp) `EnsureElementGidCommand::Apply` (slice 2a) |
| UpdateGsnIdentifier | `apply_set_gsn_identifier` | `element_commands.cpp` `UpdateGsnIdentifierCommand::Apply` (slice 2b) |
| SetElementUndeveloped | `apply_set_undeveloped` | `element_commands.cpp` `SetElementUndevelopedCommand::Apply` (slice 2b) |
| AddAcp, RemoveAcp, UpsertAcp, CreateConfidenceArgumentTree | `apply_upsert_acp_tags`, `apply_remove_acp_tags`, `apply_set_meta_claims`, `apply_create_confidence_argument_package` | [`acp_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/acp_commands.cpp) (slice 2c) |

Every one of these keeps the guarded bridge as its *fallback*, for the shapes the
seam does not support (and for a ref that carries only a gid, which the seams
cannot address). No command mutates the legacy package in place while a document
is present.

**Bridged** (`ApplyLibraryPrimaryOrLegacy` → `BridgeLegacyMutationToLibrary`):

- Text and GSN state: UpdateElementText (all fields), DropRelationshipReference,
  MoveStrategyToReasoning
  ([`element_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/element_commands.cpp))
- Tree: ReorderSiblings, MoveSubtree ([`tree_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/tree_commands.cpp))
- ApplyProposal ([`proposal_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/proposal_commands.cpp))
- RemoveElement `NodeOnly` (reparent — the library has no retarget operation,
  see [GSN metamodel gaps](../sacm/sacm-gsn-metamodel-gaps.md)) and the
  seam-unsupported create fallbacks (CreateTopGoal / CreateChildElement /
  CreateChallenge on shapes the seams cannot express) — routed through the
  guarded bridge by the `SACM23-LIB-002` round-4/5 fixes, after the verifier's
  probes measured the earlier raw-mutator fallbacks silently degrading the
  tracked file ([verification records](../sacm/verification/README.md))

**Unflipped** (pure legacy): only a dispatch with
`ctx.library_document == nullptr` remains — the no-bus path
([#347](https://github.com/lasrod/assurance-forge/issues/347)). With a
document present, every bus command either applies through the seams or routes
through the guarded bridge; the Stage-5 net and its lossy
`library_xmi_from_package` autosave survive as machinery for that residual
path and for the `allow_library_primary` test seam only.

**Replay-side asymmetry, and why it mattered**: `ApplyEventToLibrary` in
[`event_replayer.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/event_replayer.cpp)
already replayed all ten terminology events, `RemoveArgumentPackage`
(`apply_delete_package`) and `UpdateElementText(Name)` through native seams,
while the corresponding *live* commands bridged. Phase 1 closed that gap for the
first two tranches: live and replay now run the same seams, so they agree by
construction rather than by the projection happening to be faithful. The one
replay branch that can never go native as-is is
`UpdateElementText` Content/Description: the two legacy text slots collapse to
one clause-8.9 Description in the library, an irreconcilable model difference
diagnosed with measured hashes in the `UpdateElementText` branch of
`event_replayer.cpp`.

## Retirement phases

Ordered by dependency: commands go native, then the bridge dies, then the
plumbing, then the legacy model's remaining jobs. Each phase updates the
`SACM23-LIB-002` / `SACM23-INT-001` matrix notes it touches.

### Phase 1 — Flip the seam-ready live commands — **done**

Terminology CRUD (ten commands) and RemoveArgumentPackage now apply through the
seams the replayer already used. Each keeps the guarded bridge as a fallback for
shapes the seam does not support, so the "never mutate the legacy package in
place while a document is present" invariant is unchanged.

*Exit criteria, as met.* The exit criterion as originally written — "no
`ApplyLibraryPrimaryOrLegacy` caller remains in `terminology_commands.cpp`" —
was wrong, and is restated here as what actually has to hold: no terminology
command *routes* through the bridge; the calls that remain are fallbacks for
unsupported shapes, which is what phase 4 deletes.

- **Native routing is measured, not asserted.** Both routes set
  `library_primary` and both preserve vendor content, so neither distinguishes
  them. The one observable that does is the bridge's refusal:
  `SaveFromLibrary.SACM23_LIB_002_FlippedTerminologyCommandsRunOnACaseTheBridgeRefuses`
  runs all ten commands against `argumentation-full-valid.sacm.xmi` (ArgumentGroup,
  AssertedArtifactSupport, AssertedArtifactContext, a second ArgumentPackage), where
  a bridged edit is refused outright. Same idea for the package removal, over a
  nested-ArgumentPackage fixture.
- **Byte pins.**
  `SaveFromLibrary.SACM23_LIB_002_NativeTerminologyEditsPreserveUnknownContent`
  and `...NativeArgumentPackageRemovalPreservesUnknownContent`.
- **Convergence.** `LibraryPrimaryEditFlip.FlippedTerminologyTrancheMatches-`
  `LegacyCanonicalHash` covers create/update/delete at all three levels plus both
  association forms; `...RemoveArgumentPackageMatchesLegacyCanonicalHash` covers
  the removal; `...FlippedTerminologyTrancheReplaysConvergently` verifies the log
  the flipped commands wrote. The pre-existing suites pass unchanged.

Two things the flip changed that the plan did not anticipate, both recorded
rather than absorbed:

- **Deleting a term an argument package still references now asks first, and
  removes the references on consent.** The legacy mutator accepted such a delete
  and left the ArtifactReference dangling; the seam refuses it (SACM-CMD-007),
  because crossing a package boundary is a cascade and the library's policy
  default is that cascades are opt-in. Refusal was the first landing and it was
  the wrong end state -- the term's contexts are exactly what the user means to
  be rid of. `DeleteTerminologyTermCommand` now takes a `cascade_references`
  input, `preview_delete_terminology_element` lists what it would remove, and the
  delete modal shows that list before the button. Three points worth keeping:
    - The library's own cross-package cascade is NOT what is used. It removes the
      *entry* -- the term leaves the reference's `referencedArtifact` list -- and
      the ArtifactReference survives pointing at nothing, with its AssertedContext
      still drawing a context node. The seam instead deletes each reference that
      exists solely to name the term, then the term; a reference that also points
      elsewhere survives, scrubbed, and the preview reports it as modified.
    - Preview and apply are the same plan, in the same order, under the same two
      policies, so the dialog cannot promise one thing and do another. The
      cross-package half of that pair is load-bearing: sparing the shared
      reference leaves a cross-package referrer at the moment the term is
      deleted, and rejecting there stranded the document part-way through the
      sequence (review of #360).
    - `preview_delete_terminology_element` reports `can_apply` as "the target
      itself would go", stricter than the sequence preview's "something applied",
      and the app refuses to offer a cascade without it.
    - The answer is recorded in the audit payload, never re-derived. A replay has
      nobody to ask, and deriving consent from a later document state would let
      the model answer a question the user answered differently. An event with no
      `cascade_references` field replays as `false`, which is the behaviour it was
      written under.
  Pinned by `LibraryPrimaryEditFlip.TerminologyTermDeleteRefusesWhileAnArgument-`
  `PackageStillReferencesIt` (the un-consented default, asserting the legacy
  behaviour it replaces so the difference cannot quietly disappear),
  `...TerminologyTermDeleteWithConsentRemovesTheReferencesToo`,
  `SaveFromLibrary.SACM23_LIB_002_ConsentedTermDeleteRemovesTheReferencesFromThe-`
  `SavedFile` (bytes, where the hash is blind to a surviving husk), and
  `TerminologyActions.*DeleteTerm*` for the app wiring that decides whether the
  user is asked at all.
- **Gids are planned, not reconstructed.** The seams took an id and minted
  `gid-<id>`, on the argument that a fresh id's base gid is always free. Gid space
  is independent of id space, so it is not: a document already carrying `gid-TP1`
  makes `core::GenerateUniqueGid` emit `gid-TP1-2`. The create/associate seams now
  take the gid alongside the id — the live command plans it with the legacy
  generator, the replayer passes the one the payload recorded — closing a
  divergence that existed on the replay side before this phase. Pinned by
  `LibraryPrimaryEditFlip.TerminologyCreateKeepsTheLegacyGidWhenTheBaseFormIsTaken`.

The app-level guards the legacy mutators carried (a required package name, a
required term value, a category still assigned to terms) are editing rules rather
than SACM invariants, so the seams do not enforce them; they moved into the
commands and refuse exactly what they refused before.

### Phase 2 — Seam the small remaining state edits

Sliced, because these nine commands are not one size. **Phase 2 is done.** 2a:
SetElementGid and the two remaining package removals. 2b: UpdateGsnIdentifier and
SetElementUndeveloped. 2c: the four ACP commands.

SetElementUndeveloped came with a live defect and a mapping decision; both are
resolved below.

*Slice 2a, as landed.* `apply_set_gid` is new and trivial (the app decides the
value, the seam stores it); `apply_delete_package` already covered all three
package kinds. All three replay branches moved off `BridgeViaLegacy` in the same
change, so live and replay run the same code. Routing is proven the phase-1 way —
`SaveFromLibrary.SACM23_LIB_002_FlippedPackageAndGidCommandsRunOnACaseTheBridge-`
`Refuses` — and `LibraryReplayConvergence.RemoveArtifactPackageConverges` is a
genuine differential now rather than the bridge agreeing with itself
(`RemoveTerminologyPackageBridgeConverges` was renamed for the same reason: it no
longer measures a bridge).

Two behaviours to know about, in opposite directions:

- **RemoveTerminologyPackage keeps its guard.** The legacy mutator refuses a
  package still holding terms; the seam deletes recursively. That guard is an
  Assurance Forge editing rule, not a SACM invariant, so flipping without
  re-stating it would have turned "empty this first" into "the glossary is gone"
  on the same click. Re-stated in the command, checked against the same
  projection, pinned by `LibraryPrimaryEditFlip.RemoveTerminologyPackage-`
  `StillRefusesANonEmptyPackage`. Offering an informed cascade instead — as the
  term delete now does — is a product decision rather than a migration one and is
  deliberately not part of the flip. The replay branch does **not** re-check the
  guard: it gated whether the event was ever recorded, and re-applying it on
  replay would refuse to reproduce history the user legitimately made.
- **RemoveArtifactPackage stops leaving wreckage.** The legacy mutator erased the
  package and left every ArtifactReference that cited its artifacts pointing at
  an id that no longer resolved; the seam scrubs the reference. The reference
  itself survives either way — it is a drawn Solution node, and removing evidence
  from the argument is not what the user asked for. Measured on both sides by
  `LibraryPrimaryEditFlip.RemoveArtifactPackageScrubsTheReferenceTheLegacy-`
  `MutatorLeftDangling`, which also runs `VerifyProject`, because with nothing
  replaying through the legacy mutator any more the requirement that matters is
  live-against-replay rather than live-against-legacy.

The remaining slices write
vendor TaggedValues or gids the library already has operations for
(`AddTaggedValue`, `SetGid`, recursive `DeleteElement`); what is missing is
thin `sacm_adapter` seams plus replay-branch parity.

*Exit criteria*: each command sets `library_primary` without the bridge; the
matching `BridgeViaLegacy` replay branch is replaced by a seam call; per-event
convergence tests extend `LibraryReplayConvergence`.

*Slice 2b, as landed.* `UpdateGsnIdentifier` writes the reserved
`assuranceForge.gsn.identifier` TaggedValue, which the library carries natively.
The library has `AddTaggedValue` and **no update operation**, so the seam
composes an upsert: preview the add, drop the existing tag for the key, then add.
Previewing first means a rejected add cannot leave the element with no identifier
at all, and a test asserts exactly one tag survives three renames — an additive
write would leave three, and the projection reads the first.

`SetGsnIdentifier`'s front half was split out as
`core::ValidateGsnIdentifierChange` rather than reimplemented, so both routes
refuse the same edits with the same messages. Those rules — non-empty, no
surrounding whitespace, unique among the nodes — are Assurance Forge's; SACM has
no notion of a diagram label, so the seam neither knows nor enforces them.

`RemoveRelationship`'s replay branch was seam-mapped in the same slice. It is not
part of phase 2's list: the LIVE command has applied `apply_delete_element`
natively since the Phase 2 slice 2b-1 element flip while the replay branch went on
bridging, so a relationship removal and its own replay ran different code and
agreed only because the seam's scrub-then-drop happens to reproduce
`core::RemoveRelationship`. Now structural rather than coincidental
(`LibraryReplayConvergence.RemoveRelationshipConverges`, over a strategy inference
with two sources — the shape where scrub and cascade differ most).

**SetElementUndeveloped: a defect, and the mapping decision that settles it.**
GSN `undeveloped` is SACM `assertionDeclaration = needsSupport`
([mapping](../sacm/sacm-gsn-mapping.md)) — a *substitution* into a single enum,
not an extra field. The legacy path instead kept `undeveloped` as a POD boolean
beside the declaration and wrote both, while the reader honours that shorthand
only when the declaration is still `asserted`. So marking a GSN Assumption
undeveloped **reported success and did nothing**, in memory and on disk, with the
status bar saying it had worked. Measured, not inferred.

Flipping onto `SetAssertionDeclaration` naively would have made it worse — the
write would land and silently turn an Assumption into an undeveloped Goal. The
decision, recorded with its reasoning in the mapping: **the decorator applies
only where the declaration is `asserted` or `needsSupport`, and is refused
otherwise.** That is not a workaround for the collision but what the notation
already says — undeveloped means "requires support that has not yet been
provided", and GSN reaches an Assumption or Justification by `InContextOf`, never
`SupportedBy`, so there is no support for them to be missing. The inspector now
offers the control only where it can be honoured, and no gap report is warranted:
no GSN v3 construct needs "assumed *and* undeveloped".

*Slice 2c, as landed.* The ACP tranche needed a library change first: an ACP on
a RELATIONSHIP is carried partly by SACM's own clause-11.6 `metaClaim`, and
`AddMetaClaim` could attach one while nothing could detach it. `SetMetaClaims`
(replace-the-list, mirroring `SetExpressionCategories`) closes that, and landed on
its own because it is the reusable library's public API rather than Assurance
Forge's.

The flip uses a pattern worth naming, because it resembles the bridge and is not
it. `core::acp` holds several hundred lines of rules — which targets are
eligible, what each resolution kind writes, which meta-claim a relationship ACP
implies — and a second copy beside the seams would drift. So each command runs the
SAME `core::acp` mutator on a **scratch** projection to work out the result, then
writes that result through the seams as targeted tag and meta-claim edits. The
bridge projects, mutates, and then *rebuilds the live document by reloading the
projection*, which is why it loses whatever the POD cannot carry; here the scratch
is read and discarded, and nothing outside the ACP is touched. The replay branches
use the same helper, so a live ACP edit and its own replay are one code path.

The ACP tag vocabulary moved to `core/sacm_model.h` beside `kGsnIdentifierTagKey`:
the seam has to write exactly the keys the projection reads, and two copies of a
key string is how a projection quietly stops seeing what an editor writes.

### Phase 3 — The hard residue

- **UpdateElementText Content/Description**: requires deciding the claim
  text model (two legacy slots vs clause-8.9 Description list) and, if the
  library semantics win, a *recorded, announced* on-open migration of existing
  data — the strategy-encoding migration
  ([`strategy_migration.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/strategy_migration.cpp),
  pinned by `StrategyMigration.SACM23_LIB_002_StrategyMigrationPreservesUnknownContent`)
  is the template. Silent reinterpretation is forbidden outright.
- **ReorderSiblings / MoveSubtree / RemoveElement NodeOnly**: need library
  relationship reorder and retarget operations. A native retarget also lets
  NodeOnly onto the delete-preview seam (`SACM23-INT-002`'s declared gap). New
  relationship operations must respect the family-typing caution in
  [#334](https://github.com/lasrod/assurance-forge/issues/334) and take any
  GSN mapping from [the mapping record](../sacm/sacm-gsn-mapping.md), never
  from code.
- **ApplyProposal, DropRelationshipReference**: compose the native primitives
  once they exist; drop-one-reference needs a scrub-style library operation.

*Exit criteria*: `BridgeViaLegacy` has no callers; the bridge's refusal
message is unreachable; `RemoveElement` has one code path.

### Phase 4 — Delete the bridge and the flip plumbing

Delete `library_bridge.cpp/.h`, `project_library_package_with_tags`,
`reload_document_keeping_compatibility_content`, the Stage-5 net, and the
`allow_library_primary` kill switch; audit whether any dispatch can still
carry a null `library_document`, and retire the routine
`library_xmi_from_package` autosave path with it.

*Exit criteria*: the files are gone;
`SaveFromLibrary.SACM23_LIB_002_AutosaveAndExplicitSaveProduceIdenticalBytes`
holds for **every** command, not only flipped ones; the "unflipped autosave"
remaining-work note on `SACM23-LIB-002` is deleted;
`ProjectionCoverage.SACM23_LIB_002_BridgeRoundTripLosesOnlyTheKnownKinds` and
the refusal test are retired *in the same change* as the bridge, with the
matrix note rewritten — they must outlive every bridged command, and not the
bridge itself.

### Phase 5 — Retire the legacy model as a hash and serialization substrate

Not scheduled; needs its own design. The canonical hash is currently defined
over the legacy package (`library_canonical_hash*` →
`project_library_package` → `CanonicalModelHash`), so `src/legacy_sacm` types and
`RebuildSacmArgumentPackageFromParser` stay load-bearing even after Phase 4.
Redefining the hash on the library model invalidates every stored
`last_known_canonical_model_hash` and replay baseline, so it requires a
versioned hash with a recorded migration. Only then can `serialize_sacm` and
the legacy package types be deleted, with the
[layer table](layers-and-ownership.md) updated in the same change.

*Exit criteria*: `sacm::serialize_sacm` has no caller in the working-file
path (including guarded fallbacks); `VerifyProject` passes on a project
recorded before the hash change; `src/legacy_sacm` shrinks to whatever the POD view
still needs, or disappears.

## Risks, and which tests guard them

The cautionary tale: six separate sites each rebuilt the live document from a
lossy projection, silently destroying ACP TaggedValues, GSN strategy
placement, and foreign XML — and every convergence test passed, because the
canonical hash drops the same content on both sides
([full record](../sacm/sacm-integration-preservation.md)).

| Risk | Bites in | Guarded by |
|---|---|---|
| Vendor/unknown-content loss on any re-derive | Phases 1–4 | `SaveFromLibrary.SACM23_LIB_002_*` and `SACM23_INT_001_*UnknownContent*` (byte-pinned; a hash test is *not* evidence here) |
| Live/replay divergence — old audit logs must replay convergently forever | Every flip | `LibraryReplayConvergence.*`, `LibraryPrimaryEditFlip.*MatchLegacyCanonicalHash` |
| Undo/restore rebuilding stale or degraded state | Phases 1–4 | `UndoCommand.SACM23_LIB_002_*`, `HistoryReconstruction.SACM23_LIB_002_*`, `SaveFromLibrary.SACM23_LIB_002_RestoreAfterBridgedEdit*` |
| Reinterpreting claim text (content/description collapse) | Phase 3 | No existing test can — requires the announced-migration pattern plus new byte pins; the "never silently modify" constraint is the acceptance bar |
| Deleting the refusal guard while a bridged command remains | Phase 4 ordering | `SaveFromLibrary.SACM23_LIB_002_BridgedEditRefusesRatherThanDeleteUnrepresentableElements`, `ProjectionCoverage.SACM23_LIB_002_BridgeRoundTripLosesOnlyTheKnownKinds` |
| New relationship ops encoding an unrecorded GSN mapping or violating 11.15-style typing | Phase 3 | [#334](https://github.com/lasrod/assurance-forge/issues/334) negatives (planned); mapping changes land in [sacm-gsn-mapping.md](../sacm/sacm-gsn-mapping.md) first |
| Canonical-hash redefinition invalidating stored manifests and baselines | Phase 5 | `VerifyProject` on a pre-change project; hash-migration tests to be written with the design |

## What this plan does not cover

- ~~**The `src/sacm` vs `libs/sacm/include/sacm` include-prefix ambiguity**~~ —
  closed by [#341](https://github.com/lasrod/assurance-forge/issues/341): the
  legacy tree moved to `src/legacy_sacm` and answers to the `legacy_sacm/`
  include prefix, so `sacm/` names only the library. Phase 5 still removes the
  tree; it no longer has to remove an ambiguity as well.
- **Per-layer include roots and CMake-enforced dependency direction** —
  [#340](https://github.com/lasrod/assurance-forge/issues/340).
- **The POD render model's final home.** `parser::AssuranceCase` stays as the
  derived render view after every phase here; whether it moves out of
  `src/parser` is a layering question, not a bridge question.
- **The GSN-vs-SACM vocabulary decision**
  ([#270](https://github.com/lasrod/assurance-forge/issues/270)) — it decides
  what the seams *call* things, not which model owns them.
- **Library-side conformance gaps** ([#334](https://github.com/lasrod/assurance-forge/issues/334),
  [#335](https://github.com/lasrod/assurance-forge/issues/335)) except where
  Phase 3 must not make them worse.
