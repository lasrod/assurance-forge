Use the sacm-implementation-lead agent.

We are implementing OMG SACM 2.3 as an independent reusable C++23 library inside the Assurance Forge repository, not as Assurance Forge-specific data structures. The library must use strict SACM terminology, own the loaded SACM source of truth, support SACM-native editing, perform SACM 2.3 XMI import/export and validation, and expose an API that other tools can use. Assurance Forge must be migrated to consume this library through an adapter/projection layer.

Hard constraints:

- Start inside the repository, preferably under libs/sacm, but keep the library independent enough to split later.
- Do not use Assurance Forge naming, parser classes, core tree classes, UI classes, app runtime classes, AI/review classes, GSN visualization classes, deterministic layout code, or project-file structures as the public SACM library model.
- Do not expose Goal, Strategy, Solution, Canvas, TreeItem, coordinate, or layout concepts in the core library.
- Use SACM terminology such as AssuranceCasePackage, ArgumentPackage, Claim, asserted relationships, artifact, and terminology.
- Do not let Assurance Forge projections become the source of truth.
- Do not write production implementation before the specification analyst and XMI/edit test engineer have produced matrix entries and failing tests.
- Do not silently drop standard SACM data that the UI does not yet render.
- Separate strict SACM 2.3 behavior from compatibility behavior.
- Strict SACM 2.3 save/export must not include Assurance Forge layout metadata.
- Public mutations should leave the document valid for the supported slice or fail unchanged.
- Destructive operations should support previews showing affected elements before mutation.

Start by reading:

- docs/sacm/sacm-library-implementation-plan.md
- docs/sacm/sacm-library-architecture.md
- docs/sacm/sacm-editing-policy.md
- docs/sacm/sacm-layout-policy.md
- docs/sacm/sacm-conformance-matrix.md
- docs/sacm/sacm-test-strategy.md
- docs/sacm/sacm-decisions-and-questions.md
- docs/architecture/decisions/0006-sacm-23-independent-library.md

Then produce:

1. A concise current-state assessment of the repository.
2. The first three implementation slices.
3. The exact first slice brief for an editable minimal SACM 2.3 document: create AssuranceCasePackage, create ArgumentPackage, create Claim, preview/apply delete Claim/package, validate, save, load, semantic round-trip.
4. The agents to invoke next, in order.
5. The files that should be created or changed first.

Stop before production code. The next step should be conformance matrix entries and failing tests.
