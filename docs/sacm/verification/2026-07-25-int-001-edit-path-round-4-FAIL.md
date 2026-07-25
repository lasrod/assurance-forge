---
slice: phase-9-int-001-edit-path
date: 2026-07-25
verdict: FAIL
requirements: [SACM23-INT-001]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

FAIL. Superseded by
[2026-07-25-int-001-edit-path.md](2026-07-25-int-001-edit-path.md).

Round 4 exists because the implementer declined a pre-authorisation. The round-2
pass had conditionally cleared SACM23-INT-001, but the tree changed materially
afterwards, so the row was held and re-submitted instead of flipped.

That was correct, and for a reason stronger than the one offered. The round-2
pass audited the files the row *cites* — `app_state.cpp` and
`src/sacm_adapter/*` — and `library_bridge.cpp` was not among them. So the bridge
was never inspected, and at that moment it was silently erasing every vendor
TaggedValue on each bridged edit. Flipping on that record would have stamped
`verified` on a data-loss defect inside the row's own "edit" clause.

**A record cannot be stretched over a path it did not examine.**

## Scope

Files inspected: `src/core/commands/library_bridge.{h,cpp}`,
`src/core/audit/event_replayer.cpp` (`BridgeViaLegacy`),
`src/core/audit/strategy_migration.cpp`, `src/core/commands/command_bus.{h,cpp}`,
`src/core/app_state.cpp` (`sync_library_document`),
`src/sacm_adapter/library_load.{h,cpp}`,
`libs/sacm/include/sacm/compat/preserve.h`,
`tools/sacm/check_conformance_matrix.py`, `tests/test_save_from_library.cpp`.
Ran: Release build clean, `ctest -C Release` → 790/791 across the round's two
submissions, `check_conformance_matrix.py` → all five checks OK.

## Findings

| Severity | Requirement ID | Finding | Resolution |
|---|---|---|---|
| **Major** | SACM23-INT-001 | **The self-rebuild-through-a-lossy-projection pattern survived at two more sites**, neither using the preservation-restoring reload: `src/core/app_state.cpp` (`sync_library_document`) and `src/core/commands/command_bus.cpp` (the Stage-5 net for unflipped commands). Reachable **without a project**: a file opened standalone takes the no-bus dispatch branch, which calls `sync_library_document` after every command — one edit erases preserved foreign XML from the document, and the next `save_file` writes the degraded version to disk. In-project, any unflipped command (NodeOnly removal, undo, an unseamed command) does the same via the net. Narrower than the bridge defect — ACPs survive, because the live package is the tag-carrying projection — but preserved vendor elements/attributes and their namespace declarations do not. | **Fixed.** Both sites now use `reload_document_keeping_compatibility_content`. |
| **Major** (second submission) | SACM23-INT-001 | **The Stage-5 net fix was unpinned.** `command_bus.cpp` used the preserving reload, but no assertion anywhere could fail if it were reverted. The only test reaching the branch (`test_library_primary_edit_flip.cpp`) compares canonical hashes derived through `project_library_package`, which drops vendor tags and preserved content **on both sides** and therefore cannot distinguish the preserving reload from the plain one. Reverting the line left the suite green. Noted as the third occurrence of the pattern in one session — and twice already, the untested sibling is where the defect lived on. | **Fixed.** `SaveFromLibrary.SACM23_INT_001_UnflippedBusCommandPreservesUnknownContentInTheDocument`: drives a NodeOnly removal through the bus, asserts `!ctx.library_primary` so it must reach the branch, and asserts on the bytes of an explicit save (the bus autosave writes projection bytes for an unflipped command, so it would have measured the disclosed limitation instead). Falsification confirmed. |
| **Minor** | SACM23-INT-001 | **The row's cited files omitted the bridge that carries most of its "edit" claim.** `library_bridge.cpp` and `event_replayer.cpp` were cited on LIB-002, not INT-001. Not a bookkeeping nicety: the citation gap is the mechanism by which a Major defect in INT-001's own edit path escaped the round-2 pass, because the verifier audits the files the row points at. `check_conformance_matrix.py` check [3] validates that cited paths exist, never that relevant paths are cited. | **Fixed.** Both added, plus `command_bus.cpp`. |
| Info | SACM23-INT-001 | The note's bridge description ("project → mutate → reload") remained true at its level of abstraction but did not record that the projection is the tag-carrying one and the reload preserves compatibility content — the property the row now depends on. | **Fixed** in the note. |

### What was found good

The bridge fix is the right shape and correctly instrumented:
`BridgeLegacyMutationToLibrary` projects through
`project_library_package_with_tags` and re-derives via
`reload_document_keeping_compatibility_content`; `BridgeViaLegacy` is a
delegation rather than a twin, so live and restore cannot drift again;
`sacm::compat::adopt_preserved_content` is a clean SACM-native API with no app
vocabulary, no GSN, no layout, and it restores the `SACM-REF-003` id set so a
rebuilt document does not report its own re-emitted content as dangling.
Critically, the tests assert on **saved bytes** rather than canonical hashes —
the right instrument, since the hash is structurally blind to tag loss.

Two round-2 items were also closed: the `command_bus.h` flip-gate comment now
describes reality, and `check_conformance_matrix.py` was *strengthened* to scan
`tests/` as well, so check [1] is no longer vacuous for the integration rows.

## Matrix updates allowed

- **May mark verified:** none this round.
- **Must remain open:** `SACM23-INT-001` — `implemented`.
