Use the sacm-implementer agent.

The SACM library now owns the loaded SACM source of truth for the current slice. Integrate Assurance Forge as a client without changing the reusable library API to fit Assurance Forge internals.

Goals:

- Build or update adapter/projection code from the library model to current Assurance Forge UI/core views.
- Map Assurance Forge Goal to SACM Claim in the adapter, not the library.
- Keep deterministic layout in Assurance Forge code, outside the SACM library.
- Route create/edit/delete UI actions through SACM library commands or mutation APIs.
- Use library operation previews for delete confirmation UI.
- Route save/export through the library serializer for the covered slice.
- Add tests proving UI-hidden SACM data remains preserved after load/project/edit/save.
- Add tests proving strict save contains no layout metadata.
- Keep library headers independent of Assurance Forge.

Do not add ImGui, app runtime, UI i18n, AI review, project-specific fields, GSN display terminology, or layout fields to the library model.

Return the migration plan, files to change, tests to add, projection/layout behavior, delete-preview flow, and source-of-truth risks.
