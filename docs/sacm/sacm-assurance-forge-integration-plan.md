# Assurance Forge integration plan

## Intent

Assurance Forge should use the SACM library as the source of truth for loaded assurance cases. Existing tree, deterministic GSN layout, terminology, evidence, review, and project workflows become clients of the library model.

The SACM library owns data and editing semantics. Assurance Forge owns visual representation and user interaction.

## Current-state risk

If Assurance Forge keeps separate parser, domain, and UI models that can each serialize or mutate SACM differently, conformance will drift. The migration must converge on one canonical loaded document: the library document.

## Target data flow

```text
File open
  -> sacm::io::load_xmi_file
  -> sacm::model::Document
  -> sacm::validation::validate
  -> af adapter builds projections
  -> AF computes deterministic layout
  -> UI renders projections

User creates goal in AF
  -> UI command
  -> adapter maps Goal to SACM Claim
  -> sacm::commands::CreateClaim
  -> library mutation result
  -> validation diagnostics update
  -> projections and deterministic layout rebuild

User deletes visible element in AF
  -> UI command
  -> adapter maps UI selection to SACM ID
  -> library preview(DeleteClaim/DeletePackage/etc.)
  -> AF shows affected elements to user
  -> user confirms or rejects
  -> adapter applies explicit library command/preview
  -> projections rebuild

Save/export
  -> sacm::io::save_xmi_file(library document, strict SACM 2.3 mode)
```

## Mapping policy

Use SACM terminology in the library and GSN/UI terminology in Assurance Forge:

```text
Assurance Forge file/project root -> SACM AssuranceCasePackage
Assurance Forge package/folder/module -> SACM ArgumentPackage, where appropriate
Assurance Forge Goal -> SACM Claim
Assurance Forge Strategy-like display -> SACM argumentation relationships/reasoning as defined by SACM
Assurance Forge Solution/Evidence display -> SACM artifact/evidence-related elements as defined by SACM
```

The adapter owns these mappings. The library must not expose `Goal`, `Strategy`, `Solution`, `Canvas`, or layout names.

## Migration stages

### Stage 1: Add neutral library and CLI

- Add the `libs/sacm` target.
- Add a simple CLI test utility.
- Ensure public headers do not include Assurance Forge app/UI/core/parser/AI/review/project headers.
- Do not change save/export yet.

### Stage 2: Editable library slice

- Implement create document, create `AssuranceCasePackage`, create `ArgumentPackage`, create `Claim`.
- Implement delete preview and apply for `Claim` and package.
- Implement strict save/load/semantic round-trip for the slice.
- Run library tests before UI integration.

### Stage 3: Passive library load in Assurance Forge

- Load a file through the library in parallel with existing behavior.
- Compare visible projection against current parser behavior.
- Keep save/export unchanged during this comparison stage.

### Stage 4: Library-backed application state

- Store the library document in application state.
- Keep current UI models as derived views.
- Add invalidation/rebuild hooks when library state changes.
- Compute deterministic layout from the projection, not inside the library.

### Stage 5: Library-backed edit operations

- Route create/update/delete operations through library commands.
- Use library previews for delete confirmation UI.
- Rebuild projections after mutation.
- Surface validation diagnostics in existing problem panels.

### Stage 6: Library-backed save/export

- Route strict SACM export through the library serializer.
- Keep legacy serializer only for compatibility tests until removed.
- Add tests proving UI-hidden standard data survives load/project/edit/save.

### Stage 7: Retire duplicate source-of-truth models

- Remove or downgrade parser flat model once tests prove projection coverage.
- Keep convenience view models only when they are obviously derived.

## Adapter responsibilities

- ID mapping between UI selection and library elements.
- Projection from SACM claims/reasoning/artifacts to GSN-like nodes.
- Deterministic layout computation outside the library.
- Projection from artifact model to evidence registers.
- Projection from terminology model to terminology UI.
- Diagnostic mapping to Assurance Forge problem managers.
- Command translation from UI actions to library mutations.
- Delete confirmation UX based on library operation previews.

## Tests

Required integration tests:

- Create minimal assurance case through the library and project expected visible nodes in Assurance Forge.
- Load minimal SACM package through library and project expected visible nodes.
- Load fixture with hidden terminology/artifact data, project GSN view, save, and verify hidden data remains.
- Edit visible goal title and verify library `Claim` and export changed exactly that element.
- Delete visible goal and verify library preview is shown/used before mutation.
- Delete package and verify containment/reference consequences are validated.
- Import invalid reference and verify diagnostics surface without crash.
- Verify strict save contains no Assurance Forge layout metadata.

## Anti-patterns to reject

- Saving SACM from `core::AssuranceTree` or any UI-only projection.
- Adding fields to library classes solely for ImGui convenience.
- Adding layout information to strict SACM output.
- Storing standard SACM content only in review or project JSON sidecars.
- Treating GSN as the complete SACM argumentation model.
- Translating user-visible localized text into stored SACM standard fields.
- Using Goal/Strategy/Solution names in the core SACM library API.
