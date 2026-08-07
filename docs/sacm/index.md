# SACM

Everything the project maintains about OMG SACM 2.3: what the standard requires,
what `libs/sacm` implements, what has been verified, and the working material
behind those decisions.

Nineteen pages live in this section. Before this index existed, four were
reachable from the site and the rest could only be found by browsing the
repository tree — which meant a normative project policy and a superseded plan
were equally hard to find, and equally easy to mistake for each other.

Each page below carries an **authority level**, because that is the question a
reader actually has:

| Level | Meaning |
|---|---|
| **Normative** | Project policy. Binding on new work. Change it deliberately. |
| **Reference** | Describes what exists. Accurate, but not a rule. |
| **Generated** | Produced by a tool. Do not edit; regenerate. |
| **Evidence** | A record of what was verified, and when. |
| **Historical** | A plan or investigation kept for its reasoning. **Superseded — do not follow it as current instruction.** |

## Standards conformance

| Page | Authority | What it is |
|---|---|---|
| [SACM 2.3 conformance matrix](sacm-conformance-matrix.md) | Normative | The canonical source of requirement IDs. Test names embed them. `sacm_matrix_check` gates it. |
| [Verification records](verification/README.md) | Evidence | One record per `sacm-conformance-verifier` pass, failures included. |
| [SACM 2.3 metamodel inventory](sacm-2.3-metamodel-inventory.md) | Generated | Classes, attributes and containments derived from the normative OMG model. |
| [Interoperability corpus](sacm-interop-corpus.md) | Reference | Every dialect the library claims to read, with provenance. |
| [Diagnostics catalog](sacm-diagnostics-catalog.md) | Reference | Stable diagnostic codes emitted by `libs/sacm`. Once released, a code's meaning cannot change. |

## Policy

Binding on new work in and around the SACM library.

| Page | Authority | What it settles |
|---|---|---|
| [Compliance policy](sacm-compliance-policy.md) | Normative | Full SACM 2.3 compliance is the target; increments must not be presented as full compliance. |
| [Editing policy](sacm-editing-policy.md) | Normative | Editing belongs in the library, not bolted on afterwards. |
| [Layout policy](sacm-layout-policy.md) | Normative | Layout, coordinates and rendering are not SACM concerns and stay out of the library API. |
| [Test strategy](sacm-test-strategy.md) | Normative | Test at the model/edit/XMI boundary first; UI tests verify projection, they do not define compliance. |
| [Decisions and questions](sacm-decisions-and-questions.md) | Normative | Settled points, recorded so they are not silently reopened. |

## GSN and the standards landscape

| Page | Authority | What it is |
|---|---|---|
| [GSN to SACM 2.3 mapping](sacm-gsn-mapping.md) | Normative | Evidence-backed mappings. Never invent one in code. |
| [GSN / SACM metamodel gaps](sacm-gsn-metamodel-gaps.md) | Reference | Analysis prepared for the SCSC ACWG and the OMG SACM RTF. |
| [SACM 2.4 watch](sacm-2.4-watch.md) | Reference | Draft-only tracking. Nothing in it is normative or implementable. |
| [Research notes](sacm-research-notes.md) | Reference | Official references and where they came from. |

## Library design

| Page | Authority | What it is |
|---|---|---|
| [Library architecture](sacm-library-architecture.md) | Reference | The shape of `libs/sacm` and its public boundary. |
| [Requirement record template](templates/sacm-requirement-record.md) | Reference | Starting point for a new requirement record. |
| [Slice brief template](templates/sacm-slice-brief.md) | Reference | Starting point for a new implementation slice. |
| [Interop case template](templates/sacm-interop-case.md) | Reference | Starting point for a new interoperability case. |

## Historical

Kept for the reasoning they contain. **Superseded — do not follow them as
current instruction.** Where they conflict with a Normative page above, the
Normative page wins.

| Page | Why it is kept |
|---|---|
| [Library implementation plan](sacm-library-implementation-plan.md) | The staged plan the library was built to. Useful for why the phases split as they did. |
| [Assurance Forge integration plan](sacm-assurance-forge-integration-plan.md) | How the application was to become a client of the library. |
| [Stage 3 projection baseline](sacm-stage3-projection-baseline.md) | The measured projection parity result at Stage 3. |
| [Agent operating plan](sacm-agent-operating-plan.md) | The original agent workflow. Superseded by the agent architecture work in [#294](https://github.com/lasrod/assurance-forge/issues/294). |
| [Agent pack readme](agent-pack-readme.md) | How the SACM agent pack was installed. Same successor as above. |
| [Prompts](prompts/README.md) | The prompts the implementation was driven with. |

## Related

- [Layers and ownership](../architecture/layers-and-ownership.md) — where the
  library sits relative to the application, and what may depend on what.
- [ADR 0006](../architecture/decisions/0006-sacm-23-independent-library.md) — why
  SACM 2.3 is an independent reusable library.
- [ADR 0003](../architecture/decisions/0003-sacm-xml-as-source-of-truth.md) — why
  SACM XML is the source of truth.
