# Migration plan: remaining legacy projections and bridges

The unchecked deliverable of
[#291](https://github.com/lasrod/assurance-forge/issues/291): which components
exist only to carry the migration from the legacy SACM model
(`src/sacm`, `parser::AssuranceCase`) to the reusable library
([`libs/sacm`](https://github.com/lasrod/assurance-forge/tree/main/libs/sacm)),
and what has to be observably true before each of them can be deleted.

Where the migration stands: the library-owned document is authoritative for
load and for every save site (`SACM23-LIB-002` in the
[conformance matrix](../sacm/sacm-conformance-matrix.md)); the legacy parser
load fallback is gone. Edits are split: some commands mutate the library
natively, the rest go through a bridge that projects the document into the
legacy model, mutates that, and re-derives the document. The bridge is lossy
for standard SACM outside the legacy POD subset and now *refuses* rather than
silently deletes — the full history, including the six sites that each rebuilt
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
| [`library_bridge.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/library_bridge.cpp) — `BridgeLegacyMutationToLibrary`, `ApplyLibraryPrimaryOrLegacy` | Projects the document to legacy models, runs a legacy mutator, re-derives the document; refuses when the projection cannot represent the case | 26 commands have no native seam yet | Every bridged command below; the audit replayer; [`strategy_migration.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/strategy_migration.cpp) |
| [`event_replayer.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/event_replayer.cpp) — `BridgeViaLegacy` + bridged replay branches | Library-primary replay for events with no seam parity; delegates to the one bridge implementation | Recorded history must replay convergently with how it was recorded | Audit verification, restore-from-audit, undo, history view |
| [`sacm_argument_sync.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/sacm_argument_sync.cpp) — `RebuildSacmArgumentPackageFromParser` | Rebuilds a legacy `sacm::ArgumentPackage` from the POD model (six element kinds, clears lists first) | The bridge and the audit-hash projection are built on it | `library_bridge.cpp`, `library_package_projection.cpp` |
| [`library_package_projection.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/library_package_projection.cpp) — `project_library_package[_with_tags]`, `library_canonical_hash*`, `library_xmi_from_package` | Document → legacy package, for canonical hashing (tagless) and for the bridge (tag-carrying); projection-bytes fallback serializer | The canonical hash is *defined* over the legacy package; unflipped commands still autosave projection bytes | Command bus, audit verifier, replay, guarded save fallbacks |
| [`library_load.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/library_load.cpp) — `reload_document_keeping_compatibility_content` | Re-derives the document from projection bytes while restoring preserved vendor content | Exists only because projections get *reloaded*; used by the bridge, the Stage-5 net, and `AppState::sync_library_document` | `library_bridge.cpp`, `command_bus.cpp`, `app_state.cpp` |
| [`command_bus.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/command_bus.cpp) flip plumbing — `library_primary` / `library_synced` / `allow_library_primary`, scratch rebuild, Stage-5 net, fallback serializations | Runs exactly one of two derivation directions per command | Both directions are still live | Every audited command |
| [`projection_diff`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/projection_diff.h) | Stage-3 parallel-load baseline: library projection vs legacy parser | Still the only per-field fidelity evidence (`SACM23-INT-001`'s corpus qualifier) | Baseline tests only |
| [`src/sacm`](https://github.com/lasrod/assurance-forge/tree/main/src/sacm) | Legacy model types, `parse_sacm` / `serialize_sacm` | The bridge serializes through it; the canonical hash hashes its types; guarded save fallbacks remain | `core`, `parser`, the bridge, hashing |
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

**Bridged** (`ApplyLibraryPrimaryOrLegacy` → `BridgeLegacyMutationToLibrary`):

- Text and GSN state: UpdateElementText (all fields), UpdateGsnIdentifier,
  SetElementUndeveloped, DropRelationshipReference, MoveStrategyToReasoning
  ([`element_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/element_commands.cpp));
  SetElementGid ([`gid_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/gid_commands.cpp))
- Tree: ReorderSiblings, MoveSubtree ([`tree_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/tree_commands.cpp))
- ACP: AddAcp, RemoveAcp, UpsertAcp, CreateConfidenceArgumentTree
  ([`acp_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/acp_commands.cpp))
- Packages: RemoveTerminologyPackage, RemoveArgumentPackage,
  RemoveArtifactPackage ([`package_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/package_commands.cpp))
- All ten terminology commands ([`terminology_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/terminology_commands.cpp))
- ApplyProposal ([`proposal_commands.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/proposal_commands.cpp))

**Unflipped** (pure legacy; the Stage-5 net re-derives the document, and the
autosave writes lossy `library_xmi_from_package` projection bytes until the
next document-serializing save): RemoveElement `NodeOnly` (reparent — the
library has no retarget operation, see
[GSN metamodel gaps](../sacm/sacm-gsn-metamodel-gaps.md)), and any dispatch
with `ctx.library_document == nullptr`.

**Replay-side asymmetry, and why it matters**: `ApplyEventToLibrary` in
[`event_replayer.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/event_replayer.cpp)
already replays all ten terminology events, `RemoveArgumentPackage`
(`apply_delete_package`) and `UpdateElementText(Name)` through native seams —
the seams exist, mint the legacy `gid-<id>`, and are convergence-proven —
while the corresponding *live* commands still bridge. That tranche is
seam-ready. The one replay branch that can never go native as-is is
`UpdateElementText` Content/Description: the two legacy text slots collapse to
one clause-8.9 Description in the library, an irreconcilable model difference
diagnosed with measured hashes in the `UpdateElementText` branch of
`event_replayer.cpp`.

## Retirement phases

Ordered by dependency: commands go native, then the bridge dies, then the
plumbing, then the legacy model's remaining jobs. Each phase updates the
`SACM23-LIB-002` / `SACM23-INT-001` matrix notes it touches.

### Phase 1 — Flip the seam-ready live commands

Terminology CRUD (ten commands) and RemoveArgumentPackage move onto the seams
the replayer already uses. Audit payloads are unchanged (planned ids are
passed to the seams verbatim; gids are deterministic `gid-<id>`).

*Exit criteria*: no `ApplyLibraryPrimaryOrLegacy` caller remains in
`terminology_commands.cpp`; each flipped command gains a byte-pinned test that
a vendor-content case survives the edit natively (pattern:
`SaveFromLibrary.SACM23_LIB_002_BridgedEditPreservesUnknownContent`);
`LibraryReplayConvergence.*` and `LibraryPrimaryEditFlip.*` pass unchanged.

### Phase 2 — Seam the small remaining state edits

UpdateGsnIdentifier, SetElementUndeveloped, SetElementGid, the four ACP
commands, and RemoveTerminologyPackage / RemoveArtifactPackage. These write
vendor TaggedValues or gids the library already has operations for
(`AddTaggedValue`, `SetGid`, recursive `DeleteElement`); what is missing is
thin `sacm_adapter` seams plus replay-branch parity.

*Exit criteria*: each command sets `library_primary` without the bridge; the
matching `BridgeViaLegacy` replay branch is replaced by a seam call; per-event
convergence tests extend `LibraryReplayConvergence`.

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
`project_library_package` → `CanonicalModelHash`), so `src/sacm` types and
`RebuildSacmArgumentPackageFromParser` stay load-bearing even after Phase 4.
Redefining the hash on the library model invalidates every stored
`last_known_canonical_model_hash` and replay baseline, so it requires a
versioned hash with a recorded migration. Only then can `serialize_sacm` and
the legacy package types be deleted, with the
[layer table](layers-and-ownership.md) updated in the same change.

*Exit criteria*: `sacm::serialize_sacm` has no caller in the working-file
path (including guarded fallbacks); `VerifyProject` passes on a project
recorded before the hash change; `src/sacm` shrinks to whatever the POD view
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

- **The `src/sacm` vs `libs/sacm/include/sacm` include-prefix ambiguity** —
  [#341](https://github.com/lasrod/assurance-forge/issues/341), also recorded
  in [Layers and ownership](layers-and-ownership.md) and the
  [quality baseline](../quality/repository-baseline.md). If Phase 5 lands
  first, that issue dissolves with the legacy tree.
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
