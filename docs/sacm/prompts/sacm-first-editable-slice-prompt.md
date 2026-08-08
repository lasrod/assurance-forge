Use the sacm-implementation-lead agent to run the first SACM 2.3 editable library slice.

Slice goal:

Create the minimal independent library behavior needed to create, edit, validate, export, import, and semantically round-trip a strict SACM 2.3 document with an AssuranceCasePackage, an ArgumentPackage, and a Claim.

Required behavior:

- Create a new SACM 2.3 document.
- Create `AssuranceCasePackage` as the file/project root.
- Create `ArgumentPackage` as an argument module/folder.
- Create `Claim` as the SACM element that Assurance Forge may display as a GSN Goal.
- Preview deletion of a Claim and report affected elements/relationships.
- Apply deletion of a Claim only with explicit policy and validity preservation.
- Preview deletion of an ArgumentPackage and report contained/cross-reference effects.
- Apply deletion of an ArgumentPackage only with explicit policy and validity preservation.
- Validate with machine-readable diagnostics.
- Save strict SACM 2.3 XMI without Assurance Forge layout metadata.
- Load the saved SACM 2.3 XMI.
- Verify semantic round-trip.
- Provide a small CLI smoke path for version/validate/round-trip if the scaffold exists.

Required sequence:

1. Ask sacm-researcher, in **specification-analysis mode**, to refine requirement rows for SACM23-LIB-001, SACM23-LIB-003, SACM23-CMD-001 through SACM23-CMD-005, SACM23-PKG-001, SACM23-ARG-001, SACM23-XMI-001, SACM23-VAL-001, SACM23-VAL-002, SACM23-RT-002, and SACM23-CLI-001.
2. Ask sacm-researcher, in **metamodel-cartography mode**, to identify the exact minimal metamodel structure, namespace/root expectations, Claim containment expectations, and unresolved questions.
3. Ask sacm-library-architect to confirm the library directory, target, namespace, command API, preview API, delete policies, CLI target, and public API boundary for this slice.
4. Ask sacm-xmi-test-engineer to write failing library-level tests and fixtures before production code.
5. Only after tests fail for the expected reason, ask sacm-implementer, in **library scope**, to implement the minimum behavior.
6. Ask sacm-conformance-verifier to verify tests, source traceability, editing behavior, XMI behavior, no layout leakage, and library independence.

Do not integrate Assurance Forge UI layout in this slice. A small adapter smoke test may follow after the library test slice is passing and verified.
