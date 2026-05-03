# Parser and SACM

Assurance Forge loads SACM XML into two synchronized models.

| Model | Shape | Used by |
| --- | --- | --- |
| `parser::AssuranceCase` | Flat list of `parser::SacmElement` records. Relationship elements are stored beside node elements. | UI panels, GSN tree, registers, review logic. |
| `sacm::AssuranceCasePackage` | Typed SACM package with argument, artifact, and terminology containers. | Save and round-trip behavior. |

```mermaid
flowchart LR
    Xml[SACM XML] --> Parser[parser::parse_sacm_xml]
    Xml --> SacmParser[sacm::parse_sacm]
    Parser --> Flat[parser::AssuranceCase]
    SacmParser --> Domain[sacm::AssuranceCasePackage]
    Flat --> Tree[core::AssuranceTree::Build]
    Flat --> Registers[CSE / evidence registers]
    Flat --> Review[Review and AI payloads]
    Domain --> Serializer[sacm::serialize_sacm_to_file]
    Serializer --> Saved[SACM XML]
```

## Parser Model

`parser::AssuranceCase` stores case metadata and `elements`.

`parser::SacmElement` stores:

| Field group | Fields |
| --- | --- |
| Identity | `id`, `name`, `type`, `description`, `content`, `undeveloped`. |
| Language maps | `name_langs`, `description_langs`, `content_langs`. |
| Relationship refs | `source_refs`, `target_refs`, `reasoning_ref`, `assertion_declaration`. |

The parser model is intentionally flat. A claim, strategy, evidence item, asserted inference, asserted context, and asserted evidence relationship can all appear as entries in the same vector.

## SACM Domain Model

`sacm::AssuranceCasePackage` owns:

| Container | Contents |
| --- | --- |
| `terminologyPackages` | `TerminologyPackage` and `Expression`. |
| `artifactPackages` | `ArtifactPackage` and `Artifact`. |
| `argumentPackages` | Argument elements and relationships. |

`sacm::ArgumentPackage` owns:

| Collection | Meaning |
| --- | --- |
| `claims` | Assertions and GSN goals/contexts/assumptions/justifications. |
| `argumentReasonings` | Strategies. |
| `artifactReferences` | Evidence references. |
| `assertedInferences` | Support relationships. |
| `assertedContexts` | Context relationships. |
| `assertedEvidences` | Evidence relationships. |

```mermaid
classDiagram
    class AssuranceCasePackage {
        string namespace_prefix
        string namespace_uri
        vector argumentPackages
        vector artifactPackages
        vector terminologyPackages
    }
    class ArgumentPackage {
        vector claims
        vector argumentReasonings
        vector artifactReferences
        vector assertedInferences
        vector assertedContexts
        vector assertedEvidences
    }
    class SacmElement {
        string id
        string name
        string description
        MultiLangText name_ml
        MultiLangText description_ml
    }
    class Claim {
        string content
        string assertionDeclaration
        bool undeveloped
    }
    class ArgumentReasoning {
        string content
        bool undeveloped
    }
    class ArtifactReference {
        string referencedArtifact
    }
    class AssertedRelationship {
        vector sources
        vector targets
        string assertionDeclaration
        bool isCounter
    }
    class AssertedInference {
        string reasoning
    }
    class AssertedContext
    class AssertedEvidence

    AssuranceCasePackage --> ArgumentPackage
    SacmElement <|-- ArgumentPackage
    SacmElement <|-- Claim
    SacmElement <|-- ArgumentReasoning
    SacmElement <|-- ArtifactReference
    SacmElement <|-- AssertedRelationship
    AssertedRelationship <|-- AssertedInference
    AssertedRelationship <|-- AssertedContext
    AssertedRelationship <|-- AssertedEvidence
    ArgumentPackage --> Claim
    ArgumentPackage --> ArgumentReasoning
    ArgumentPackage --> ArtifactReference
    ArgumentPackage --> AssertedInference
    ArgumentPackage --> AssertedContext
    ArgumentPackage --> AssertedEvidence
```

## Tree Model

`core::AssuranceTree` is derived from the parser model.

```mermaid
flowchart TD
    A[Flat parser elements] --> B[Create TreeNode for non-relationship elements]
    B --> C[Read AssertedInference]
    B --> D[Read AssertedContext]
    B --> E[Read AssertedEvidence]
    C --> F[Attach structural children]
    D --> G[Attach contextual side nodes]
    E --> F
    F --> H[Pick first parentless claim as root]
    G --> H
    H --> I[Collect orphans]
```

`core::TreeNode` fields used by the UI:

| Field | Meaning |
| --- | --- |
| `id` | Original SACM element id. |
| `label`, `label_secondary` | Primary and secondary display text. |
| `role` | Claim, Strategy, Solution, Context, Assumption, Justification, or Other. |
| `group` | `Group1` structural child or `Group2` contextual attachment. |
| `group1_children` | Child nodes rendered below the parent. |
| `group2_attachments` | Contextual nodes rendered beside the parent. |
| `parent` | Structural parent pointer. |

## Edit Synchronization

```mermaid
sequenceDiagram
    participant UI as Element panel / context menu
    participant Controller as ElementEditController
    participant Factory as core::ElementFactory functions
    participant Parser as parser::AssuranceCase
    participant Sacm as sacm::AssuranceCasePackage
    participant Events as AppEvents

    UI->>Controller: add, remove, or edit
    Controller->>Factory: mutate models
    Factory->>Parser: update flat elements
    Factory->>Sacm: update typed package
    Controller->>Events: DocumentDirtyEvent
    Controller->>Events: TreeDirtyEvent
```

Text edits in the element panel update the selected parser element and call `sync_to_sacm()` so the SACM package remains the save source.