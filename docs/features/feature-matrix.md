# Assurance Forge capability matrix

This is the canonical record of **what Assurance Forge can actually do**, and it
lives here rather than in the marketing site because this is the repository where
capabilities are implemented, tested, and changed. The site
(`assurance-forge-site`) is a *downstream consumer*: it renders
`feature-matrix.json`, which is generated from this page.

For SACM 2.3 *specification* conformance — requirement-level obligations of the
standard — see [`docs/sacm/sacm-conformance-matrix.md`](../sacm/sacm-conformance-matrix.md).
That matrix answers "does the library implement the standard". This one answers
"can a user do the thing". Rows here may cite a `SACM23-*` requirement ID to link
the two.

## Why this is a gated artifact

A capability matrix nobody can check drifts into fiction. The version this
replaced claimed SACM XMI import/export was "planned" and dialectic arguments
were "planned" — both had shipped. Claims about what a safety-case tool supports
are read by people deciding whether to trust it with a safety argument, so drift
here is not cosmetic.

`tools/features/check_feature_matrix.py` runs as the `feature_matrix_check`
CTest and enforces:

1. IDs are unique and well-formed (`AF-<AREA>-<NNN>`), with a documented area code.
2. Status values come from the vocabulary below.
3. `supported` and `prototype` rows cite at least one code path.
4. `supported` rows cite at least one test that resolves in the test tree.
5. `planned`, `candidate`, and `not-planned` rows cite **no** tests — a planned
   row with tests is a status that reality has already overtaken.
6. Every repo path cited in any column exists on disk.
7. Every `SACM23-*` ID referenced exists in the SACM conformance matrix.
8. `feature-matrix.json` is in sync with this page.

Regenerate the JSON after any edit:

```bash
python tools/features/export_feature_matrix.py
python tools/features/check_feature_matrix.py
```

## Status vocabulary

| Status | Meaning | Public roadmap equivalent |
|---|---|---|
| `supported` | Available and intended for normal use. Backed by code and tests. | Stable |
| `prototype` | Usable, but format or UX is expected to change. Backed by code. | Prototype 1 / 2 |
| `in-development` | Actively being built; not yet usable end to end. | — |
| `planned` | Committed to the roadmap, not started. | Planned |
| `candidate` | Under consideration. Scope and timing may change. | Candidate |
| `not-planned` | Deliberately out of scope. Recorded so it is not re-proposed. | — |

`supported` is a claim about a *user-facing capability*, not about specification
coverage. A row may be `supported` while the underlying standard support is
partial — say so in Notes rather than downgrading the row.

## Areas

| Code | Area |
|---|---|
| `STD` | Standards foundation (SACM) |
| `GSN` | Core GSN |
| `PAT` | GSN Pattern Extension |
| `MOD` | GSN Modular Extension |
| `ACP` | Assurance Claim Points and confidence arguments |
| `DIA` | Dialectic arguments |
| `METH` | Guided development methods |
| `ENG` | Assurance case engineering |
| `AI` | AI assistance |
| `PLAT` | Platform and application |

---

## STD — Standards foundation

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-STD-001 | SACM 2.3 as the source of truth for assurance data | supported | libs/sacm/src, src/sacm_adapter/library_load.cpp | tests/test_save_from_library.cpp | ADR 0003. The app saves from the library model, not from projected UI state. |
| AF-STD-002 | SACM 2.3 XMI import | supported | libs/sacm/src/io, src/sacm_adapter/library_load.cpp | libs/sacm/tests, tests/test_sacm_roundtrip.cpp | SACM23-XMI-001..004 verified. Both namespace dialects accepted on import. |
| AF-STD-003 | SACM 2.3 XMI export | supported | libs/sacm/src/io, src/sacm/sacm_serializer.cpp | tests/test_sacm_roundtrip.cpp | Strict export normalizes to the pinned namespace; the pin is a project choice, not normative. |
| AF-STD-004 | Import → export round-trip integrity | supported | libs/sacm/src/io, src/sacm_adapter/projection_diff.cpp | tests/test_sacm_roundtrip.cpp, tests/test_projection_coverage.cpp | SACM23-RT-001, SACM23-RT-002. |
| AF-STD-005 | Argumentation package model | supported | libs/sacm/src, src/sacm/sacm_package_tree.cpp | tests/test_sacm_package_tree.cpp, tests/test_package_commands.cpp | SACM23-ARG-001..003, SACM23-PKG-001..003. |
| AF-STD-006 | Artifact model | supported | libs/sacm/src, src/ui/register_views.cpp | tests/test_sacm_roundtrip.cpp | SACM23-ART-001. Surfaced to users through the evidence register, which is still prototype (AF-ENG-008). |
| AF-STD-007 | Terminology model | supported | libs/sacm/src, src/core/terminology_package_service.cpp | tests/test_terminology_package_service.cpp | SACM23-TERM-001. |
| AF-STD-008 | Assurance Case Package | in-development | libs/sacm/src, src/core/library_package_projection.cpp | | Package containment exists; a full ACP-level container with interfaces does not. |
| AF-STD-009 | SACM model validation and diagnostics | supported | libs/sacm/src | libs/sacm/tests | SACM23-VAL-001, SACM23-VAL-002. Diagnostics catalog: docs/sacm/sacm-diagnostics-catalog.md. |
| AF-STD-010 | Editing through SACM-native commands | supported | libs/sacm/src/commands, src/sacm_adapter/document_edit.cpp | tests/test_sacm_library_edit.cpp, tests/test_library_primary_edit_flip.cpp | SACM23-CMD-001..006. |
| AF-STD-011 | Refusal to lose unrepresentable SACM on edit | supported | src/sacm_adapter/projection_diff.cpp | tests/test_projection_coverage.cpp | SACM23-LIB-002. Bridged edits that would delete unrepresentable SACM are refused, not silently applied. |
| AF-STD-012 | GSN-typed SACM files read as GSN | supported | libs/sacm/src/io/name_tables.cpp | libs/sacm/tests | SACM23-COMPAT-001. See docs/sacm/sacm-gsn-mapping.md for the transformation and its evidence. |
| AF-STD-013 | Preserve the original GSN type through round-trip | supported | libs/sacm/src/io/xmi_reader.cpp, libs/sacm/src/io/name_tables.cpp | libs/sacm/tests/test_roundtrip.cpp | SACM23-COMPAT-002. The GSN type is recorded as `sacm.import.extensionType` (`{ns}Goal`) and Assumption/Justification get their SACM declaration, so they no longer collapse into identical Claims. A save normalizes to SACM types — the information round-trips, the GSN syntax does not. |
| AF-STD-014 | Standalone SACM library, reusable outside the app | supported | libs/sacm/CMakeLists.txt, cmake/check_layer_gates.cmake | cmake/check_layer_gates.cmake, libs/sacm/tests | SACM23-LIB-001. The reverse gate scans quoted and angle includes and bans pugixml from public headers; it runs at configure time, so it cannot be skipped. |
| AF-STD-015 | SACM 2.4 alignment | candidate | | | Tracked in docs/sacm/sacm-2.4-watch.md. RTF active; `metaClaim` and `defeated` removal would affect AF-ACP-* and AF-DIA-*. |

## GSN — Core GSN

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-GSN-001 | Goal | supported | src/core/element_factory.cpp, src/ui/gsn/gsn_shapes.cpp | tests/test_element_factory.cpp | |
| AF-GSN-002 | Strategy | supported | src/core/element_factory.cpp, src/ui/gsn/gsn_shapes.cpp | tests/test_element_factory.cpp, tests/test_strategy_migration.cpp | Maps to SACM `ArgumentReasoning`; see docs/sacm/sacm-gsn-mapping.md. |
| AF-GSN-003 | Solution | supported | src/core/element_factory.cpp, src/ui/gsn/gsn_shapes.cpp | tests/test_element_factory.cpp | |
| AF-GSN-004 | Context | supported | src/core/element_factory.cpp, src/ui/gsn/gsn_shapes.cpp | tests/test_element_factory.cpp | Claim-versus-ArtifactReference typing is preserved and diagnosed, not guessed. |
| AF-GSN-005 | Assumption | supported | src/core/element_factory.cpp, src/ui/gsn/gsn_shapes.cpp | tests/test_element_factory.cpp | |
| AF-GSN-006 | Justification | supported | src/core/element_factory.cpp, src/ui/gsn/gsn_shapes.cpp | tests/test_element_factory.cpp | |
| AF-GSN-007 | SupportedBy relationship | supported | src/ui/gsn/gsn_edge_renderer.cpp, src/core/tree_editing.cpp | tests/test_tree_editing.cpp, tests/test_assurance_tree.cpp | Endpoints are swapped against SACM `AssertedInference` on import. |
| AF-GSN-008 | InContextOf relationship | supported | src/ui/gsn/gsn_edge_renderer.cpp, src/core/tree_editing.cpp | tests/test_tree_editing.cpp | Endpoints swapped against SACM `AssertedContext`. |
| AF-GSN-009 | Undeveloped decorator | supported | src/ui/gsn/gsn_model.h, src/ui/gsn/gsn_shapes.cpp | tests/test_assurance_tree.cpp | |
| AF-GSN-010 | Automatic diagram layout | supported | src/core/gsn_layout.cpp, src/ui/gsn/gsn_layout.cpp | tests/test_layout.cpp | Deterministic; no manual node positioning. Policy: docs/sacm/sacm-layout-policy.md. |
| AF-GSN-011 | Canvas navigation, selection and hit testing | supported | src/ui/gsn/gsn_canvas.cpp, src/ui/gsn/gsn_hit_tester.cpp | tests/test_ui_state.cpp | |
| AF-GSN-012 | GSN element identifier (G1, S1, Sn1 …) | supported | src/core/element_factory.cpp, src/core/assurance_tree.cpp | tests/test_element_factory.cpp | GSN v3 makes the identifier mandatory. New elements are minted with the GSN prefix for their node type, unique within the case, and rendered as `id: name` on the canvas and in exported SVG. See AF-GSN-015 for the remaining limitation. |
| AF-GSN-015 | Identifier independent of the storage id | planned | | | The identifier *is* the SACM `xmi:id`, so it cannot be renumbered without changing element identity and every reference to it, and a file imported with opaque ids (`generated_3`) shows those on the diagram instead of GSN identifiers. |
| AF-GSN-013 | Off-diagram decorators for reports | planned | | | Needed by AF-ENG-004; a report page cannot show an unbounded diagram. |
| AF-GSN-014 | Choice node | planned | | | v2.2 models `Choice` with no attributes; "m of n" cardinality is a GSN v3 concept with no metamodel. See docs/sacm/sacm-gsn-metamodel-gaps.md row 9. |

## PAT — GSN Pattern Extension

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-PAT-001 | Uninstantiated decorator | planned | | | Maps to SACM `isAbstract`. |
| AF-PAT-002 | Undeveloped-and-uninstantiated decorator | planned | | | |
| AF-PAT-003 | Multiplicity decorator | planned | | | GSN `isMany`; no SACM feature exists to carry it. |
| AF-PAT-004 | Optionality decorator | planned | | | GSN `isOptional`; no SACM feature exists to carry it. |
| AF-PAT-005 | Pattern template arguments | planned | | | |
| AF-PAT-006 | Instantiation data reference | planned | | | |
| AF-PAT-007 | Pattern catalogue and instantiation workflow | candidate | | | Pattern Definition has no GSN or SACM class; see docs/sacm/sacm-gsn-metamodel-gaps.md row 13. |

## MOD — GSN Modular Extension

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-MOD-001 | Argument modules as packages | supported | src/sacm/sacm_package_tree.cpp, src/core/library_package_projection.cpp | tests/test_sacm_package_tree.cpp, tests/test_package_commands.cpp | Modules exist and are editable; the modular *notation* below is not yet drawn. |
| AF-MOD-002 | Package details and navigation | supported | src/ui/panels/package_details_panel.cpp, src/ui/panels/project_explorer_panel.cpp | tests/test_argument_package_projection.cpp | |
| AF-MOD-003 | Away Goal | planned | | | |
| AF-MOD-004 | Away Solution | planned | | | |
| AF-MOD-005 | Away Context | planned | | | Metamodel is contested; decided 2026-07-20 to preserve rather than retype. See docs/sacm/sacm-gsn-mapping.md. |
| AF-MOD-006 | Away Assumption and Away Justification | planned | | | |
| AF-MOD-007 | Module reference element | planned | | | |
| AF-MOD-008 | Contract module and contract reference | planned | | | Maps to SACM `ArgumentPackageBinding`, not `ArgumentPackage`. |
| AF-MOD-009 | Module interface | planned | | | SACM `ArgumentPackageInterface` exists; GSN v2.2 OCL disables it. See docs/sacm/sacm-gsn-metamodel-gaps.md row 10. |
| AF-MOD-010 | Architecture view | planned | | | Diagrammatic; lands in the SACM 2.4 draft. |
| AF-MOD-011 | Public decorator | planned | | | |
| AF-MOD-012 | To-be-supported-by-contract decorator | planned | | | |

## ACP — Assurance Claim Points and confidence

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-ACP-001 | ACP on a SupportedBy relationship | supported | src/core/acp/assurance_claim_point.cpp, src/ui/gsn/gsn_acp_decorator.cpp | tests/test_assurance_claim_point.cpp, tests/test_acp_editing.cpp | |
| AF-ACP-002 | ACP on an InContextOf relationship | supported | src/core/acp/acp_relationship_index.cpp, src/ui/gsn/gsn_acp_decorator.cpp | tests/test_acp_relationship_index.cpp | |
| AF-ACP-003 | ACP identifiers | supported | src/core/acp/assurance_claim_point.cpp | tests/test_assurance_claim_point.cpp | GSN v3 §1:5.2.3. Carried in a TaggedValue; v2.2 Base OCL forbids TaggedValue, so this is not GSN-metamodel-conformant. |
| AF-ACP-004 | ACP editing and management panel | supported | src/ui/panels/acp_panel.cpp, src/app/controllers | tests/test_acp_controller.cpp, tests/test_acp_problem_sync.cpp | |
| AF-ACP-005 | ACP on a Solution or Context | not-planned | | | Structurally unrepresentable in SACM 2.3 — those extend `ArgumentAsset`, which has no `metaClaim`. Open at OMG as SACM24-83. Revisit when 2.4 lands. |
| AF-ACP-006 | Confidence model and store | prototype | src/core/confidence/confidence_store.cpp, src/ui/confidence_model.cpp | tests/test_confidence_store.cpp, tests/test_confidence_model.cpp | Prototype: see docs/features/confidence-panel-prototype.md. Scoring model is not final. |
| AF-ACP-007 | Confidence panel | prototype | src/ui/panels/confidence_panel.cpp | tests/test_confidence_problem_sync.cpp | |
| AF-ACP-008 | Confidence argument module linking | planned | | | Links an ACP to the module holding its confidence argument. |

## DIA — Dialectic arguments

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-DIA-001 | Challenge relationship | supported | src/ui/gsn/gsn_model.h, src/ui/gsn/gsn_edge_renderer.cpp | tests/test_dialectic_challenge.cpp | Rests on SACM `AssertedRelationship.isCounter`, which GSN v2.2 OCL forbids — see docs/sacm/sacm-gsn-metamodel-gaps.md row 4. |
| AF-DIA-002 | Counter-argument and counter-evidence rendering | supported | src/ui/gsn/gsn_edge_renderer.cpp, src/ui/gsn/gsn_layout.cpp | tests/test_dialectic_challenge.cpp | Dashed open-arrow challenge edge; side-stacked layout. |
| AF-DIA-003 | Challenge targeting a relationship | supported | src/ui/gsn/gsn_model.h, src/ui/gsn/gsn_edge_renderer.cpp | tests/test_dialectic_challenge.cpp | Arrow lands on the target relationship's midpoint. |
| AF-DIA-004 | Challenge-to-challenge | supported | src/ui/gsn/gsn_model.h | tests/test_dialectic_challenge.cpp | A challenge relationship is itself challengeable. |
| AF-DIA-005 | Defeated decorator | planned | | | SACM `AssertionDeclaration::defeated` exists but SACM24-12 proposes removing it. |
| AF-DIA-006 | Defeat on Strategy, Solution or Context | not-planned | | | Normatively required by GSN v3 §1:6.3.12 but structurally unrepresentable in SACM 2.3. See docs/sacm/sacm-gsn-metamodel-gaps.md row 3. |

## METH — Guided development methods

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-METH-001 | Problem detection and attention model | supported | src/core/problems/problems_manager.cpp, src/ui/panels/problems_panel.cpp | tests/test_problems_manager.cpp, tests/test_problem_attention.cpp | The substrate every quality check below reports through. |
| AF-METH-002 | SCCG guideline catalog | supported | src/parser/sccg_dist_parser.cpp, src/parser/guidelines_parser.cpp | tests/test_guideline_catalog.cpp, tests/test_guidelines_parser.cpp | Safety Case Core Guidelines, from the `external/safety-case-core-guidelines` submodule. |
| AF-METH-003 | Manual and AI-assisted review workflow | supported | src/core/reviews/review_item_manager.cpp, src/ui/panels/review_panel.cpp | tests/test_review_controller.cpp, tests/test_review_item.cpp | |
| AF-METH-004 | Review proposals with applyable patches | supported | src/core/reviews/review_proposal_patch_service.cpp | tests/test_review_proposal_patch_service.cpp, tests/test_review_proposal.cpp | |
| AF-METH-005 | Guided top-down development method | planned | | | Structured authoring starting from goals. |
| AF-METH-006 | Guided bottom-up development method | planned | | | Structured authoring starting from evidence. |
| AF-METH-007 | Missing-support detection | planned | | | |
| AF-METH-008 | Circular-argument detection | supported | src/core/problems/argument_cycles.cpp, src/app/structure_problem_sync.cpp | tests/test_argument_cycles.cpp | Reports every distinct loop in the support graph as an error, naming the strategy on it. InContextOf and GSN v3 challenges are excluded — both point "backwards" without being support, and following either would flag sound arguments. |
| AF-METH-009 | Common GSN mistake detection | planned | | | Would report through AF-METH-001. |

## ENG — Assurance case engineering

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-ENG-001 | Audit register with a replayable event log | supported | src/core/audit/event_store.cpp, src/core/audit/audit_store.cpp | tests/test_event_store_jsonl.cpp, tests/test_reconcile_audit_store.cpp | |
| AF-ENG-002 | Baselines | supported | src/core/audit/audit_baseline.cpp | tests/test_audit_baseline.cpp | |
| AF-ENG-003 | Snapshots | supported | src/core/audit/audit_snapshot.cpp, src/core/audit/audit_manifest.cpp | tests/test_audit_user_snapshot.cpp, tests/test_audit_manifest.cpp | |
| AF-ENG-004 | Report creation | planned | | | Depends on AF-GSN-013. |
| AF-ENG-005 | SVG diagram export | supported | src/export/gsn_svg_exporter.cpp, src/export/svg_writer.cpp | tests/test_gsn_svg_exporter.cpp | Core GSN nodes, SupportedBy/InContextOf/Challenges edges, and decorators (AF-ENG-015). Nodes and edges carry `gsn-*` CSS classes for restyling. |
| AF-ENG-006 | History timeline and reconstruction | supported | src/core/audit/history_reconstruction.cpp, src/ui/panels/history_timeline_panel.cpp | tests/test_history_reconstruction.cpp, tests/test_timeline_model_builder.cpp | |
| AF-ENG-007 | Undo with explicit boundaries | supported | src/core/audit/undo_boundary.cpp, src/core/audit/undo_resolver.cpp | tests/test_undo_command.cpp | |
| AF-ENG-008 | Evidence register | prototype | src/core/registers/register_model.cpp, src/ui/register_views.cpp | tests/test_register_model.cpp | Row derivation is in core and tested; evidence nothing cites is listed rather than hidden. Assessments persist with the project (AF-ENG-016). Expected to migrate to a SACM Artifact-backed register; see docs/roadmap/public.md. |
| AF-ENG-009 | CSE register | prototype | src/core/registers/register_model.cpp, src/ui/register_views.cpp | tests/test_register_model.cpp | Claim/evidence pairings are derived in core and tested, with a pinned CSE id format. Assessments persist with the project (AF-ENG-016). Format transition is expected to break backwards compatibility. |
| AF-ENG-010 | Terminology assist and controlled vocabulary | supported | src/core/terminology_scope_service.cpp, src/ui/panels/terminology_package_panel.cpp | tests/test_terminology_scope_service.cpp, tests/test_terminology_commands.cpp | Documented in docs/features/terminology-assist.md. |
| AF-ENG-011 | Term usage detection and promotion to context | supported | src/core/terminology_term_usage.cpp, src/core/terminology_context_projection.cpp | tests/test_terminology_context_projection.cpp, tests/test_terminology_visible_term_context.cpp | |
| AF-ENG-012 | Dual-language safety case content | supported | src/core/sacm_model.h, src/core/translation_review_store.cpp | tests/test_translation_review_sync.cpp | Per-element `name_langs` / `content_langs` maps carried through the model. |
| AF-ENG-013 | Deterministic model hashing for change detection | supported | src/core/audit/canonical_model_hash.cpp | tests/test_canonical_model_hash.cpp | |
| AF-ENG-014 | Conformance tracking surfaced in the app | candidate | | | This matrix and the SACM one are repo artifacts today, not app features. |
| AF-ENG-016 | Register assessments persist with the project | supported | src/app/controllers/register_controller.cpp, src/app/register_problem_sync.cpp, src/core/project_service.cpp | tests/test_register_controller.cpp, tests/test_register_problem_sync.cpp | Owners, criteria, assessment status and notes are held by `RegisterController`, saved to `registers/register-assessments.af.json` on project save, and tracked in the manifest. A file that fails to load is reported in the register tab, editing is disabled and saving is refused, so a bad read cannot overwrite what is on disk. Only rows a user edited are stored. An assessment whose CSE or evidence id leaves the argument is kept, never auto-deleted, and raised as a warning in the Problems panel carrying the stored content — the row is gone from the table, so the problem is the only place left to read it. Its "Discard assessment" quick fix drops it in memory; the file is only rewritten on save, so closing without saving takes it back. |
| AF-ENG-015 | SVG export renders decorators and challenge edges | supported | src/export/gsn_projection.cpp, src/export/svg_writer.cpp | tests/test_gsn_svg_exporter.cpp | Undeveloped diamonds, ACP badges and dashed open-arrow Challenges edges now export. Previously a counter relationship projected as a `SupportedBy` edge, drawing counter-evidence as support for the claim it attacks. |

## AI — AI assistance

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-AI-001 | Provider-agnostic AI integration with explicit consent | supported | src/ai/ai_service.cpp, src/ai/ai_provider.h | tests/test_ai_service.cpp | ADR 0005. No data leaves the machine without user action. |
| AF-AI-002 | AI profiles and settings | supported | src/ai/ai_settings.cpp | tests/test_ai_settings.cpp | |
| AF-AI-003 | OpenAI-compatible provider | supported | src/ai/openai_provider.cpp | tests/test_openai_provider.cpp | |
| AF-AI-004 | Local secret storage for API keys | supported | src/ai/secret_store.cpp | tests/test_ai_service.cpp | Tests exercise the `ISecretStore` seam through a fake, including the store-unavailable path. The platform-backed implementation in `src/ai/secret_store.cpp` has no direct test. |
| AF-AI-005 | Background AI task execution | supported | src/ai/ai_task_runner.cpp | tests/test_ai_task_runner.cpp | |
| AF-AI-006 | SCCG-guided AI claim review | supported | src/ai/ai_claim_review.cpp | tests/test_ai_claim_review.cpp, tests/test_ai_review_controller.cpp | |
| AF-AI-007 | MCP server — build safety cases from an external AI client | planned | | | Design recorded in docs/features/mcp-server.md. |

## PLAT — Platform and application

| ID | Capability | Status | Evidence | Tests | Notes |
|---|---|---|---|---|---|
| AF-PLAT-001 | Project model, load and save | supported | src/core/project_service.cpp, src/core/project_file_io.cpp | tests/test_project_service.cpp, tests/test_project_model.cpp | |
| AF-PLAT-002 | Project explorer | prototype | src/ui/panels/project_explorer_panel.cpp | tests/test_project_controller.cpp | Prototype 2 per docs/roadmap/public.md; a redesign is expected. |
| AF-PLAT-003 | Recent projects | supported | src/core/project_manifest.cpp | tests/test_recent_projects.cpp | |
| AF-PLAT-004 | Multi-language UI (English, Japanese) | supported | src/ui/i18n/localization.cpp, src/ui/i18n/mo_catalog.cpp | tests/test_localization.cpp | Catalog consistency is gated by the `i18n_catalog_check` CTest. |
| AF-PLAT-005 | Dark and light themes | prototype | src/ui/theme.cpp | tests/test_theme.cpp | Prototype 2; the styling model is expected to evolve. |
| AF-PLAT-006 | Command bus and undo-aware mutation | supported | src/core/commands, src/app/app_runtime.cpp | tests/test_command_bus.cpp, tests/test_app_events.cpp | |
| AF-PLAT-007 | Layered architecture enforced at build time | supported | cmake/check_layer_gates.cmake | cmake/check_layer_gates.cmake | ADR 0002. The gate is its own evidence: a configure-time FATAL_ERROR that runs on every build, not a lint. |
| AF-PLAT-008 | Crash-resilient project recovery | supported | src/core/audit/audit_recovery.cpp | tests/test_audit_recovery.cpp | |
