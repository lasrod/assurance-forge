# GSN Community Standard v3 conformance matrix

This matrix records Assurance Forge coverage of the normative Goal Structuring
Notation Community Standard v3 (SCSC-141C, May 2021). It is the standards-facing
counterpart to the product [capability matrix](../features/feature-matrix.md):
this page asks whether the tool covers GSN, while the capability matrix asks
whether a user-facing feature is available.

The inventory follows the five normative areas of GSN v3:

1. Core GSN
2. Argument Pattern Extension
3. Modular Extension
4. Confidence Argument Extension
5. Dialectic Extension

Rows group closely related normative statements into testable implementation
slices. Sources are the normative standard, the clause analysis in
[`sacm-gsn-mapping.md`](../sacm/sacm-gsn-mapping.md), and the representation-gap
analysis in
[`sacm-gsn-metamodel-gaps.md`](../sacm/sacm-gsn-metamodel-gaps.md).

## Status vocabulary

Each operation is assessed independently:

| Status | Meaning |
|---|---|
| `supported` | Available through the tool and backed by automated tests. |
| `partial` | A useful subset exists, but the complete requirement is not available. |
| `preserved` | Data survives compatibility round-trip but is not fully understood or editable. |
| `absent` | No implementation exists. |
| `blocked` | The current SACM 2.3 representation cannot carry the requirement without an explicit extension. |
| `n/a` | The operation does not apply to this requirement. |

`supported` in one column never implies support in another. In particular,
rendering imported data is not authoring support, and preserving foreign XML is
not semantic interchange.

## Core GSN

| ID | Source | Requirement | Model/import | Create/edit | Render | Validate | Interchange | Evidence and notes |
|---|---|---|---|---|---|---|---|---|
| GSN3-CORE-001 | Part 1, Core GSN | Goal element | supported | supported | supported | partial | partial | `src/core/element_factory.cpp`; `src/ui/gsn/gsn_shapes.cpp`; `tests/test_element_factory.cpp`; GSN syntax is normalized to SACM on save. |
| GSN3-CORE-002 | Part 1, Core GSN | Strategy element and its inference role | supported | supported | supported | partial | partial | `src/core/element_factory.cpp`; `tests/test_strategy_migration.cpp`; represented by SACM `ArgumentReasoning` plus one inference. |
| GSN3-CORE-003 | Part 1, Core GSN | Solution as a reference to evidence | supported | supported | supported | partial | partial | `src/core/element_factory.cpp`; `tests/test_assurance_tree.cpp`; represented by `ArtifactReference` plus `AssertedEvidence`. |
| GSN3-CORE-004 | Part 1, Core GSN | Context element | partial | supported | supported | partial | partial | `src/core/element_factory.cpp`; `tests/test_element_factory.cpp`; ambiguous imported Context typing is preserved and diagnosed rather than guessed. |
| GSN3-CORE-005 | Part 1, Core GSN | Assumption element | supported | supported | supported | partial | partial | `src/core/element_factory.cpp`; `tests/test_assurance_tree.cpp`; maps to an assumed SACM Claim. |
| GSN3-CORE-006 | Part 1, Core GSN | Justification element | supported | supported | supported | partial | partial | `src/sacm_adapter/gsn_role_tag.h`; `tests/test_sacm_library_edit.cpp`; role requires an Assurance Forge tag because SACM has no distinct Justification class. |
| GSN3-CORE-007 | Part 1, Core GSN | SupportedBy relationship and direction | supported | supported | supported | partial | partial | `src/core/tree_editing.cpp`; `tests/test_tree_editing.cpp`; endpoints are reversed when translating between GSN and SACM direction. |
| GSN3-CORE-008 | Part 1, Core GSN | InContextOf relationship and direction | supported | supported | supported | partial | partial | `src/core/tree_editing.cpp`; `tests/test_tree_editing.cpp`; endpoints are reversed when translating between GSN and SACM direction. |
| GSN3-CORE-009 | Part 1, Core GSN | Undeveloped decorator | supported | supported | supported | partial | partial | `src/ui/panels/element_panel.cpp`; `src/ui/gsn/gsn_shapes.cpp`; `tests/test_gsn_svg_exporter.cpp`. |
| GSN3-CORE-010 | Part 1, Core GSN | Every GSN element has exactly one notation identifier | partial | partial | partial | absent | partial | New elements receive conventional prefixes, but the displayed identifier is the SACM storage `xmi:id`; opaque imported ids remain opaque and cannot be renamed independently. |
| GSN3-CORE-011 | Part 1, Core GSN | Element statements follow the semantics of their element type | supported | supported | supported | absent | partial | Text editing and round-trip are covered by `tests/test_element_factory.cpp` and `tests/test_sacm_library_edit.cpp`, but proposition/noun-phrase and single-statement rules are not validated. |
| GSN3-CORE-012 | Part 1, Core GSN | Off-diagram element/reference notation | absent | absent | absent | absent | absent | No `offDiagram` model or renderer exists. This is a Core GSN feature, not a pattern feature or merely a report-layout convenience. |
| GSN3-CORE-013 | Part 1, Core GSN | Top goals, including more than one top goal in a structure | supported | supported | supported | partial | partial | `core::AddTopGoal`; `tests/test_element_factory.cpp`; top status is derived from graph position rather than stored as an independent GSN property. |
| GSN3-CORE-014 | Part 1, Core GSN | Structured argument is a directed acyclic support graph | partial | supported | supported | partial | partial | Edits reject cycles and imported cycles are reported; `src/core/problems/argument_cycles.cpp`; `tests/test_argument_cycles.cpp`. There is no complete GSN well-formedness validator. |
| GSN3-CORE-015 | Part 1, Core GSN | Only permitted element/relationship combinations are authored | partial | partial | supported | partial | partial | `tests/test_tree_editing.cpp`; tree editing rejects several invalid combinations, but imported structures are not comprehensively checked against all GSN connection rules. |

## Argument Pattern Extension

| ID | Source | Requirement | Model/import | Create/edit | Render | Validate | Interchange | Evidence and notes |
|---|---|---|---|---|---|---|---|---|
| GSN3-PAT-001 | Part 1, Argument Pattern Extension | Uninstantiated decorator | supported | absent | supported | absent | partial | SACM `isAbstract` is projected to the hollow triangle; `tests/test_sacm_library_parallel_load.cpp`; `tests/test_gsn_svg_exporter.cpp`. The UI has no authoring control. |
| GSN3-PAT-002 | Part 1, Argument Pattern Extension | Combined undeveloped and uninstantiated decorator | partial | absent | supported | absent | partial | `tests/test_gsn_svg_exporter.cpp`; the standard bisected marker is rendered and exported, while authoring remains absent. |
| GSN3-PAT-003 | Part 1, Argument Pattern Extension | Multiplicity decorator | absent | absent | absent | absent | absent | No carrier for the GSN `multiple`/`isMany` property. |
| GSN3-PAT-004 | Part 1, Argument Pattern Extension | Optionality decorator | absent | absent | absent | absent | absent | No carrier for the GSN `optional`/`isOptional` property. |
| GSN3-PAT-005 | §1:3.2.2 | Choice structural abstraction | preserved | absent | absent | absent | preserved | GSN `Choice`/`ChoiceNode` XML is retained as compatibility content but has no SACM 2.3 semantic equivalent. It is part of the Pattern Extension, not Core GSN. |
| GSN3-PAT-006 | §1:3.2.2 | Choice minimum/maximum cardinality | absent | absent | absent | absent | absent | GSN v2.2 `Choice` has no attributes; SACM 2.4 draft `Join.lowerBound`/`upperBound` is the intended alignment point. |
| GSN3-PAT-007 | Part 1, Argument Pattern Extension | Pattern argument model | absent | absent | absent | absent | absent | No Pattern domain model exists. |
| GSN3-PAT-008 | Part 1, Argument Pattern Extension | Template argument model | absent | absent | absent | absent | absent | No Template domain model exists. |
| GSN3-PAT-009 | §1:3.4 | Pattern Definition and pattern identifier | absent | absent | absent | absent | absent | The normative concept has no class in the published GSN or SACM metamodels; the tool still needs an explicit representation. |
| GSN3-PAT-010 | Part 1, Argument Pattern Extension | Pattern Catalogue | absent | absent | absent | absent | absent | A product catalogue workflow must be kept separate from the normative catalogue data. |
| GSN3-PAT-011 | Part 1, Argument Pattern Extension | Pattern instantiation relationship | absent | absent | absent | absent | absent | No `instantiationOf` relationship exists. |
| GSN3-PAT-012 | Part 1, Argument Pattern Extension | Instantiation Data Reference attached to a Template | absent | absent | absent | absent | absent | No model, notation or attachment relationship exists. |
| GSN3-PAT-013 | Part 1, Argument Pattern Extension | Related-pattern references | absent | absent | absent | absent | absent | No `relatedTo` pattern relationship exists. |
| GSN3-PAT-014 | Part 1, Argument Pattern Extension | Published/final pattern and argument states | absent | absent | absent | absent | absent | No `published` or `final` state exists. |

## Modular Extension

| ID | Source | Requirement | Model/import | Create/edit | Render | Validate | Interchange | Evidence and notes |
|---|---|---|---|---|---|---|---|---|
| GSN3-MOD-001 | Part 1, Modular Extension | Module and Argument View notation | partial | partial | absent | absent | partial | SACM `ArgumentPackage` provides storage and package editing, but the GSN Module symbol and Argument View are not implemented. |
| GSN3-MOD-002 | Part 1, Modular Extension | Contract Module | partial | partial | absent | absent | partial | SACM `ArgumentPackageBinding` is available as substrate; GSN contract notation and behavior are absent. |
| GSN3-MOD-003 | Part 1, Modular Extension | Away Goal | partial | absent | absent | absent | partial | Import can map the type to a cited Claim, but there is no GSN authoring or rendering. |
| GSN3-MOD-004 | Part 1, Modular Extension | Away Solution | partial | absent | absent | absent | partial | Import can map the type to an `ArtifactReference`; authoring and notation are absent. |
| GSN3-MOD-005 | Part 1, Modular Extension | Away Context | preserved | absent | absent | absent | preserved | The published metamodel typing is contested; the source fragment is preserved rather than retyped. |
| GSN3-MOD-006 | Part 1, Modular Extension | Away Assumption | partial | absent | absent | absent | partial | Type provenance can be imported; authoring and notation are absent. |
| GSN3-MOD-007 | Part 1, Modular Extension | Away Justification | partial | absent | absent | absent | partial | Type provenance can be imported; authoring and notation are absent. |
| GSN3-MOD-008 | Part 1, Modular Extension | Module Reference | partial | absent | absent | absent | partial | Can map to an `ArtifactReference`, but its GSN role is not usable in the canvas. |
| GSN3-MOD-009 | Part 1, Modular Extension | Contract Module Reference | partial | absent | absent | absent | partial | Can map to an `ArtifactReference`, but its GSN role is not usable in the canvas. |
| GSN3-MOD-010 | §1:4.6 | Module Interface | partial | absent | absent | absent | partial | SACM `ArgumentPackageInterface` exists, while GSN v2.2 OCL disables it. |
| GSN3-MOD-011 | Part 1, Modular Extension | Architecture View | absent | absent | absent | absent | absent | Diagrammatic feature expected to align with SACM 2.4. |
| GSN3-MOD-012 | Part 1, Modular Extension | Public decorator | absent | absent | absent | absent | absent | No model or renderer exists. |
| GSN3-MOD-013 | Part 1, Modular Extension | To-be-supported-by-contract decorator | absent | absent | absent | absent | absent | No model or renderer exists. |
| GSN3-MOD-014 | Part 1, Modular Extension | Cross-module consistency, substitution, contract and dependency rules | absent | absent | absent | absent | absent | Context consistency, away-goal substitution, contract constraints and circular module dependency checks are not implemented. |

## Confidence Argument Extension

| ID | Source | Requirement | Model/import | Create/edit | Render | Validate | Interchange | Evidence and notes |
|---|---|---|---|---|---|---|---|---|
| GSN3-ACP-001 | Part 1, Confidence Argument Extension | ACP on SupportedBy | supported | supported | supported | partial | partial | `src/core/acp`; `tests/test_assurance_claim_point.cpp`; stored in vendor TaggedValues with a SACM meta-claim where representable. |
| GSN3-ACP-002 | Part 1, Confidence Argument Extension | ACP on InContextOf | supported | supported | supported | partial | partial | `src/core/acp/acp_relationship_index.cpp`; `tests/test_acp_relationship_index.cpp`. |
| GSN3-ACP-003 | §1:5.2.2 | ACP on an element that references an artefact, such as Solution or Context | supported | supported | supported | partial | partial | `src/core/acp/acp_editing.cpp`; `tests/test_acp_editing.cpp`; uses vendor TaggedValues because SACM 2.3 `ArtifactReference` has no `metaClaim`. |
| GSN3-ACP-004 | §1:5.2.3 | ACP has a unique identifier | supported | supported | supported | partial | partial | `tests/test_assurance_claim_point.cpp`; deterministic `ACP<n>` generation is tested, while standard module qualification is a separate requirement below. |
| GSN3-ACP-005 | Part 1, Confidence Argument Extension | ACP associates the risk argument point with a confidence argument | supported | supported | supported | partial | partial | Separate confidence argument packages can be created, linked, selected and opened; `tests/test_acp_editing.cpp`. |
| GSN3-ACP-006 | §1:5.2.3 | Module-qualified ACP notation when the confidence argument is elsewhere | partial | partial | absent | absent | partial | The package/top-goal link exists, but the canvas does not render notation such as `ACP1[Confidence]`. |
| GSN3-ACP-007 | Part 1, Confidence Argument Extension | Risk and confidence arguments are explicitly distinguished | partial | partial | partial | partial | partial | Confidence packages use an Assurance Forge purpose tag; there is no general GSN argument-type model or standard interchange. |

Confidence scoring, confidence stores and their panel are Assurance Forge
analysis features. They are deliberately excluded from this normative section.

## Dialectic Extension

| ID | Source | Requirement | Model/import | Create/edit | Render | Validate | Interchange | Evidence and notes |
|---|---|---|---|---|---|---|---|---|
| GSN3-DIA-001 | Part 1, Dialectic Extension | Counter-argument expressed by a Goal | supported | supported | supported | partial | partial | `core::AddChallenge`; `tests/test_dialectic_challenge.cpp`; uses SACM `isCounter`. |
| GSN3-DIA-002 | Part 1, Dialectic Extension | Counter-evidence expressed by a Solution | supported | supported | supported | partial | partial | `core::AddChallenge`; `tests/test_dialectic_challenge.cpp`. |
| GSN3-DIA-003 | Part 1, Dialectic Extension | Challenge targets a GSN element | supported | supported | supported | partial | partial | `tests/test_dialectic_challenge.cpp`; dashed open-arrow notation and side layout are tested. |
| GSN3-DIA-004 | Part 1, Dialectic Extension | Challenge targets a relationship | supported | supported | supported | partial | partial | `tests/test_dialectic_challenge.cpp`; canvas and SVG arrows land on the target relationship midpoint. |
| GSN3-DIA-005 | Part 1, Dialectic Extension | A challenge relationship can itself be challenged | supported | supported | supported | partial | partial | `tests/test_dialectic_challenge.cpp`; counter-challenges are supported, but generated counter identifiers are not nesting-aware. |
| GSN3-DIA-006 | Part 1, Dialectic Extension | Challenged elements can be marked in doubt pending review | absent | absent | absent | absent | absent | The canvas shows a non-normative attention cue, but no GSN `inDoubt` state exists. |
| GSN3-DIA-007 | Part 1, Dialectic Extension | Defeated decorator applies to GSN elements and relationships | partial | absent | absent | absent | partial | SACM can preserve `AssertionDeclaration::defeated` on assertions only. Strategy, Solution and Context are not SACM Assertions. |
| GSN3-DIA-008 | §1:6.3.12 and related rules | Challenge resolution propagates in-doubt/defeated semantics without reinterpreting support | blocked | absent | absent | absent | absent | Full coverage needs a GSN extension carrier for targets that SACM 2.3 cannot annotate. |

## Cross-cutting interchange and conformance

| ID | Source | Requirement | Model/import | Create/edit | Render | Validate | Interchange | Evidence and notes |
|---|---|---|---|---|---|---|---|---|
| GSN3-XMI-001 | Published GSN metamodels and project interoperability policy | Recognized GSN v2.2 and legacy types import without reversing argument meaning | partial | n/a | partial | partial | partial | `libs/sacm/src/io/name_tables.cpp`; `libs/sacm/tests/test_roundtrip.cpp`; abstract or contested types remain preserved fragments. |
| GSN3-XMI-002 | Project round-trip policy | Original GSN type provenance survives compatibility save and reload | supported | n/a | partial | partial | supported | `libs/sacm/src/io/xmi_reader.cpp`; `libs/sacm/tests/test_roundtrip.cpp`; provenance is retained as `sacm.import.extensionType`, while output syntax is SACM rather than GSN. |
| GSN3-XMI-003 | GSN v3 | Export a GSN-native representation of every supported v3 construct | absent | n/a | n/a | absent | absent | GSN v3 has no published complete metamodel. A project extension dialect must be explicit and must not be presented as normative GSN XMI. |
| GSN3-VAL-001 | GSN v3 Part 1 | Produce requirement-traceable GSN conformance diagnostics | partial | n/a | n/a | partial | n/a | Cycle and selected staged checks exist; no complete validator or GSN requirement-ID test suite exists. |

## Conformance conclusion

Assurance Forge currently provides substantial Core GSN, ACP and Challenge
functionality, limited Pattern rendering, and SACM package infrastructure that
can host future Modular GSN. It does **not** yet fully support GSN v3.

Full support requires an explicit GSN extension representation for normative
concepts that SACM 2.3 cannot carry. That representation must remain outside the
public API of the independent `libs/sacm` library, preserve SACM XML as the
project source of truth, and refuse rather than silently reinterpret data.

The next implementation tranche is:

1. independent GSN notation identifiers;
2. Core off-diagram notation; and
3. a GSN v3 well-formedness validator with requirement-ID-bearing tests.
