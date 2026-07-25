---
slice: phase-9-int-002-delete-preview-integration
date: 2026-07-25
verdict: PASS
requirements: [SACM23-INT-002]
commit: 2da697d + working tree (branch sacm23/libsacm-gsn-context-preservation)
verifier: sacm-conformance-verifier
---

## Verification result

PASS — for SACM23-INT-002. Two passes were run; the first returned FAIL with four
Major findings, all of which were fixed before the second pass. Both records are
kept: see [2026-07-25-int-002-delete-preview-round-1-FAIL.md](2026-07-25-int-002-delete-preview-round-1-FAIL.md).

SACM23-INT-001 was reviewed in the same passes and remains `implemented`; its
outstanding condition is recorded below.

## Scope

- **Requirement IDs:** SACM23-INT-002 (certified), SACM23-INT-001 (reviewed, not
  certified). Explicitly out of scope, and this record must not be cited for
  them: SACM23-LIB-002, SACM23-COMPAT-001, SACM23-COMPAT-002.
- **Library files inspected:** `libs/sacm/src/commands/commands.cpp`
  (`relationship_invalid_after_scrub`, `check_delete` effect emission),
  `libs/sacm/include/sacm/commands/mutation.h`,
  `libs/sacm/include/sacm/commands/policies.h`,
  `libs/sacm/include/sacm/io/options.h`.
- **CLI/tooling files inspected:** `tools/sacm/check_conformance_matrix.py`
  (run), `cmake/check_layer_gates.cmake` (passed at configure),
  `tools/i18n/regenerate_ja_po.py`.
- **Adapter files inspected:** `src/sacm_adapter/document_edit.{h,cpp}`,
  `src/sacm_adapter/library_load.{h,cpp}`,
  `src/sacm_adapter/case_projection.cpp`.
- **App files inspected:** `src/app/controllers/element_edit_controller.{h,cpp}`,
  `src/app/areas/modal_host.cpp`, `src/app/commands/dispatch.cpp`,
  `src/app/app_runtime_frame.cpp`, `src/app/app_runtime_ai.cpp`,
  `src/app/actions/ai_review_actions.cpp`,
  `src/core/commands/element_commands.cpp`,
  `src/core/commands/command_bus.{h,cpp}`, `src/core/element_factory.cpp`,
  `src/ui/element_context_menu.cpp`.
- **Tests run or reviewed:** full `ctest --test-dir build -C Release` →
  **781/781 passed**; `--gtest_filter=*INT_002*` → 7/7; all 18 INT-001 test names
  cited in the matrix confirmed present and passing (49 `SACM23_INT_001_*` total);
  `ElementFactoryRemove.RemoveNodeOnly_*` reviewed to establish the reparent
  behaviour the preview must not misdescribe.

## Findings

Round-1 Major findings and their resolutions:

| Severity | Requirement ID | Finding | Resolution |
|---|---|---|---|
| Major | SACM23-INT-002 | Preview described a different operation than the app performs for `RemoveMode::NodeOnly`. `BuildRemovalPreview` modelled every removal as N × `DeleteElement{ScrubReferences}`, but `RemoveElementCommand` restricts the library seam to `NodeAndDescendants`; `NodeOnly` falls through to `core::RemoveElement`, which REPARENTS children — retargeting a child's inference onto the grandparent. The dialog would have stated "Will be removed: &lt;the child's inference&gt;" for an inference that survives, and this was a *new* dialog where previously there was none. | Fixed. `BuildRemovalPreview` takes `core::RemoveMode` and declines `NodeOnly`; the modal shows its "could not preview" branch. `SACM23_INT_002_NodeOnlyOffersNoPreviewRatherThanAWrongOne` is non-vacuous on three axes and its closing assertions confirm the retarget empirically. |
| Major | SACM23-INT-002 | `..._DeletePreviewMatchesWhatApplyDoes` was weaker than the matrix claimed: it compared against the library seam for one id on a fixture whose answer was trivially `{G2, R1}`, never dispatching the command the UI runs. | Fixed. `ElementEditControllerTest.SACM23_INT_002_ConfirmedRemovalMatchesThePreviewExactly` previews, confirms through `DispatchAuditedCommand`, and requires equality in both directions. The verifier noted it is stronger than requested: with no command bus in the test, the delete is applied by the *legacy* `core::RemoveElement`, so the library preview is pinned against the riskier of the two apply paths. Matrix sentence rewritten to state what each test proves and the `NodeAndDescendants`-only scope. |
| Major | SACM23-INT-001 | The matrix note and the `apply_delete_element` header comment both named `DeleteReferencingRelationships`; the code uses `ScrubReferences`. | Fixed in both places, with the reason the distinction matters (cascade would detach a strategy's shared inference). |
| Major | SACM23-INT-002 | The utility-element filter hid ACP destruction. ACPs are `assuranceForge.acp.*` TaggedValues; deleting `G2` cascades `R1`, destroying `ACP2` and orphaning its confidence package, with nothing on screen naming it. | Fixed. `AppendAcpConsequences` re-adds ACPs whose target is doomed. Tests assert `ACP2` is disclosed, `ACP1` (on surviving `G1`) is not, and that `ACP2` is really gone from a fresh `project_case` of the library document. |
| Minor | SACM23-INT-002 | An all-unresolvable preview was presented as a successful preview with an empty consequence list, reading as "nothing else is affected". | Fixed: `preview_available = !targets.empty()`. |

Open findings carried forward (none blocking):

| Severity | Requirement ID | Finding | Required fix |
|---|---|---|---|
| Minor | SACM23-INT-002 | `AppendAcpConsequences` reads `loaded_case->acps` (a UI cache) while the rest of the preview reads the library. No reachable wrong disclosure was constructible, but the row's claim is that the library is the source of truth for delete implications, and for ACPs it currently is not. | Source ACPs from `project_case(*library_document)`, or move ACP awareness into the adapter. |
| Minor | SACM23-INT-002 | The "modified survives" direction of the end-to-end test runs over an empty set on `fixture_acp_parity` (no `Modified` effects there). Main equality assertion still covers over-reporting. | Re-run the same body against the 3-source-inference fixture. |
| Minor | SACM23-INT-002 | `DeletePreview` carries no `document_revision`, so the library's SACM-CMD-003 staleness guarantee is dropped at the seam. Verifier checked the only in-frame mutation pump (`PollAiReviewTask` → review items only) and found no reachable mutation between preview and confirm; the invariant holds by circumstance, not by check. | Carry the revision and reject/rebuild on mismatch. |
| Minor | SACM23-INT-002 | Preview skips unresolvable ids; `RemoveElementCommand` hard-fails on them mid-cascade. The seam comment's rationale ("the plan legitimately contains ids with no library counterpart") is unsupported — `PlanRemoval` draws from the projection. | Restate as a defensive skip, or report as a diagnostic. |
| Info | SACM23-INT-001 | `loaded_case.acps` is not pruned by a legacy removal, so after an unflipped `NodeOnly` delete the ACP panel can list a record whose target is gone. Pre-existing and independent of this slice. | Projection-freshness follow-up. |
| Info | SACM23-INT-001 | `command_bus.h:71-77` still says "The interactive app sets this false"; `dispatch.cpp` no longer does. This comment caused the round-1 mis-modelling of the apply path. | Correct the comment. |
| Info | SACM23-INT-002 | `RemovalEffect::kind` is documented as a SACM class name but now also carries the vendor pseudo-kind `AssuranceClaimPoint`. App-layer only; `DeleteEffect.kind` at the adapter boundary still carries SACM class names exclusively. | One-line comment update. |

## Matrix updates allowed

- **May mark verified:** `SACM23-INT-002`.
- **Must remain open:**
  - `SACM23-INT-001` — `implemented`. The verifier set three conditions; the
    first two (the `ScrubReferences` corrections) were applied. The third was
    applied after the second pass reported it: the INT-002 cross-link in the
    INT-001 note now reads "for `NodeAndDescendants` removals; `NodeOnly`
    reparents rather than deletes and is deliberately not previewed". The
    verifier stated that with that clause in place INT-001 may be flipped
    citing this record, with no code change and no further pass — **but that
    flip has not been made here**, because the clause landed after the verdict
    was issued and no verifier has seen the final text. INT-001 also leans on
    LIB-002 for its "save through the library-owned document" clause, which
    this pass did not audit.
  - `SACM23-LIB-002`, `SACM23-COMPAT-001`, `SACM23-COMPAT-002` — out of scope.

## Follow-up slice suggestions

1. Library-sourced ACPs in the preview, closing the last place where the delete
   disclosure reads a UI cache.
2. Carry `document_revision` through `DeletePreview` so SACM-CMD-003 survives
   the seam instead of relying on "nothing else mutates the model right now".
3. Native retarget operation in `libs/sacm` (already in
   `sacm-gsn-metamodel-gaps.md`) — brings `NodeOnly` onto the seam, at which
   point the preview covers every removal mode and converges with apply by
   construction.
4. Decide whether legacy mutators must prune cached derived vectors, or whether
   every command should set the re-derive flag.
5. Verify SACM23-LIB-002 and the save path, which INT-001 leans on.
