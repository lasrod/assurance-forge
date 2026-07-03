Use the sacm-conformance-verifier agent.

Verify the current SACM implementation slice. Do not assume correctness from passing UI behavior.

Check:

- Every changed behavior maps to requirement IDs in docs/sacm/sacm-conformance-matrix.md.
- Library public headers do not include Assurance Forge app, UI, core, parser, AI, review, project, GSN visualization, or layout headers.
- The core library API uses SACM terminology and does not expose Goal, Strategy, Solution, Canvas, TreeItem, coordinates, or layout concepts.
- Tests exist for create/edit/delete behavior where relevant.
- Delete operations provide operation previews for affected elements and relationships.
- Public mutations leave the document valid for the supported slice or fail unchanged.
- Tests exist for import, export, validation, and semantic round trip where relevant.
- Negative tests produce diagnostics rather than crashes, silent fixes, ignored data, or silent destructive cascades.
- Strict SACM 2.3 behavior is separated from compatibility behavior.
- Strict save/export does not include Assurance Forge layout metadata.
- Standard SACM data is not silently dropped.
- Assurance Forge, if touched, saves from the library model rather than projected UI state.

Return PASS or FAIL, a findings table, and the matrix rows that may or may not be marked verified.
