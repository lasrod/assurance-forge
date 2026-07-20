# SACM 2.3 library-first implementation plan

## Goal

Implement OMG SACM 2.3 as an independent reusable C++23 library inside the Assurance Forge repository, then migrate Assurance Forge to use that library as the loaded safety-case source of truth.

The final goal is SACM 2.3 compliance. Work should proceed in small verified increments, but the target is not merely a subset parser. Each increment should move toward complete SACM 2.3 model, XMI, validation, editing, and interoperability support.

## Architectural position

The library must be useful outside Assurance Forge. It should support other tools that need to import, validate, transform, inspect, edit, or export SACM 2.3 assurance cases.

Assurance Forge may use GSN vocabulary and deterministic visual layout in its UI. Those are projection concerns. The SACM library must use SACM terminology and own SACM data.

Target relationship:

```text
SACM 2.3 XMI file
  -> sacm library parser
  -> sacm library document/model  (source of truth)
  -> validation diagnostics
  -> Assurance Forge adapter/projection
  -> UI tree, deterministic GSN layout, terminology/evidence views, reviews

User edit in Assurance Forge
  -> UI command
  -> Assurance Forge adapter maps UI/GSN concept to SACM command
  -> sacm library preview/mutation
  -> validation and mutation result
  -> projection rebuild
  -> sacm library serializer
  -> strict SACM 2.3 XMI file
```

## Core design rules

- No Assurance Forge namespaces, class names, UI data structures, parser flat models, app state, AI review concepts, ImGui types, GSN display terms, or layout concepts in the reusable library API.
- The library uses strict SACM terminology: `AssuranceCasePackage`, `ArgumentPackage`, `Claim`, asserted relationships, artifacts, terminology, etc.
- The library owns SACM identity, references, containment, validation, editing semantics, and XMI serialization.
- Assurance Forge owns UI workflows, GSN terminology, visualization, deterministic layout, review assistance, project UX, and convenience projections.
- A standard SACM element may be invisible in the UI but must remain in the library model and exported output.
- Strict mode and compatibility mode must be separate. Strict SACM 2.3 export must not include Assurance Forge layout metadata.
- Public mutation operations should either succeed and leave the document valid for the supported slice or fail without changing the document.

## Phases

### Phase 0: Install agent workflow and pin references

Deliverables:

- Project agents installed under `.claude/agents/`.
- Plans and prompts installed under `docs/sacm/`.
- Decisions from the human review recorded in `docs/sacm/sacm-decisions-and-questions.md`.
- Optional local SACM reference folder with official PDF/XML and checksums.
- Initial conformance matrix reviewed.

Exit criteria:

- Agents can be invoked from Claude Code.
- The team agrees that the first vertical slice is editable, SACM-native, and layout-free.

### Phase 1: Independent library skeleton in the monorepo

Deliverables:

- Neutral library directory under `libs/sacm` unless repository conventions require another path.
- CMake target such as `sacm::sacm`.
- Public headers under `include/sacm/`.
- Private implementation under `src/`.
- Library-level tests.
- CLI test utility target for import/export/validate smoke tests.

Exit criteria:

- The library builds and tests without linking Assurance Forge UI/app/core/AI layers.
- A smoke test proves the library exposes SACM 2.3 version metadata.
- Include/dependency checks prevent Assurance Forge API leakage into public library headers.

### Phase 2: Minimal editable SACM document slice

Deliverables:

- Create an empty SACM 2.3 document.
- Create `AssuranceCasePackage` as the file/project root.
- Create `ArgumentPackage` as an argument module/folder.
- Create `Claim` as the SACM representation of a GSN goal.
- Preview delete `Claim` and `ArgumentPackage` operations.
- Apply delete operations with explicit policy choices.
- Structured mutation results with affected element IDs and diagnostics.
- XMI save/load for the implemented slice.
- Semantic import -> export -> import round-trip tests.

Exit criteria:

- A new minimal assurance case can be created through the SACM library, saved as strict SACM 2.3 XMI, reloaded, validated, and semantically compared.
- Delete preview identifies affected elements and relationships before mutation.
- Public mutation operations leave the document valid or fail unchanged.

### Phase 3: XMI and identity foundation

Deliverables:

- Namespace and root-object handling for strict SACM 2.3.
- Stable ID handling and global resolver/index.
- Caller-provided IDs and generated IDs.
- Duplicate ID and dangling reference diagnostics.
- Deterministic export ordering policy.
- Semantic comparison helpers.

Exit criteria:

- Minimal fixtures with caller-provided IDs and generated IDs round-trip reliably.
- Invalid ID/reference fixtures produce machine-readable diagnostics.

### Phase 4: Base model completeness

Deliverables:

- Common SACM element/model element fields.
- Language strings, descriptions, notes, tagged values, citations, and implementation constraints as required by the standard.
- Strict preservation rules for supported standard content.
- Unsupported valid content preservation or safe rejection policy.

Exit criteria:

- Every implemented concrete element can carry inherited metadata without loss.
- Unsupported content is not silently dropped.

### Phase 5: Assurance case package completeness

Deliverables:

- Nested packages.
- Package interfaces and bindings.
- Participant packages.
- Cross-package references and validation.
- Package deletion previews including recursive and cross-package effects.

Exit criteria:

- Nested package fixtures round-trip.
- Invalid binding/reference fixtures produce diagnostics.
- Destructive package edits are previewable and explicit.

### Phase 6: Terminology model

Deliverables:

- Terminology packages and groups.
- Terms, categories, expressions, expression elements, external references, and origin references.
- Interfaces and bindings where applicable.
- Edit operations needed for basic terminology creation/deletion after load/save behavior is proven.

Exit criteria:

- Terminology-heavy SACM fixtures round-trip without relying on Assurance Forge UI support.

### Phase 7: Argumentation model

Deliverables:

- Argument packages, claims, argument reasoning, artifact references.
- Assertion declarations and asserted relationships.
- Inference, evidence, context, artifact support, artifact context, and counter-argument behavior.
- Source/target typing validation.
- Create/delete/edit operations for the argumentation subset needed by Assurance Forge.

Exit criteria:

- GSN-visible subset projects correctly into Assurance Forge, while non-GSN SACM argumentation data remains preserved.
- Relationship delete previews accurately identify consequences.

### Phase 8: Artifact model and provenance

Deliverables:

- Artifact packages, groups, assets, artifacts.
- Activities, events, resources, techniques, participants, properties, relationships.
- Provenance and external evidence metadata.
- Edit operations needed by Assurance Forge evidence workflows.

Exit criteria:

- Evidence/artifact information can be imported, validated, exported, edited through SACM-native APIs, and projected independently of GSN rendering.

### Phase 9: Assurance Forge source-of-truth migration

Deliverables:

- Application state holds the library document as canonical safety-case state.
- UI tree and deterministic GSN layout are projections.
- Save/export uses library serializer.
- Edit commands mutate library state through SACM-native commands, then projections rebuild.
- Delete confirmation UI uses library operation previews.

Exit criteria:

- Assurance Forge can create, open, edit, delete, save, and reopen the covered SACM slice via the library without losing standard SACM data.

### Phase 10: Interoperability corpus and compliance gate

Deliverables:

- Papyrus-style SACM fixtures or minimized reproductions.
- OASC investigation and fixtures if usable.
- Corpus documentation with license status.
- CI/test gate for library-level conformance and adapter source-of-truth integrity.

Exit criteria:

- Compatibility behavior is documented per tool/source.
- Conformance claims are backed by matrix rows, tests, and verifier review.

## First implementation slice

Start with this editable vertical slice:

Requirement IDs below are the canonical rows of `docs/sacm/sacm-conformance-matrix.md`:

```text
SACM23-LIB-001: neutral library boundary and no Assurance Forge API leakage
SACM23-CMD-001: command/mutation result shape for SACM-native edits
SACM23-CMD-002: create SACM 2.3 document containing an AssuranceCasePackage
SACM23-CMD-003: create ArgumentPackage and Claim, preserved through save/load
SACM23-CMD-004: delete previews with affected elements and diagnostics
SACM23-CMD-005: delete apply with explicit policies, valid-or-unchanged
SACM23-CMD-006: mutation results carry audit/undo-ready metadata
SACM23-XMI-001: strict SACM 2.3 root and namespace behavior for the slice
SACM23-VAL-001: structured diagnostics
SACM23-VAL-002: mutations leave the document valid or fail unchanged
SACM23-RT-002: created document saves, reloads, and semantically matches
SACM23-CLI-001: CLI smoke workflow (version/validate/round-trip)
```

Do not integrate full UI layout in this slice. A small Assurance Forge adapter smoke test may follow only after the library tests pass.

## Risks

- Building from current Assurance Forge classes would speed early progress but create a library that is not reusable.
- Using GSN terms such as Goal in the core library would make the model less SACM-compliant and less reusable.
- Deletion without preview or explicit policy can cause surprising data loss.
- Over-generating all classes from the metamodel can create an awkward public API before usage is understood.
- Under-generating can miss standard features and make compliance claims fragile.
- Exact XML textual round trip is expensive and unnecessary as a baseline; semantic round trip plus deterministic export is required.
- Vendor extensions and older SACM versions need explicit policy. Silent acceptance is not compliance.
