# Project Storage

An Assurance Forge project is a directory with an `af.proj` JSON manifest and tracked files.

```mermaid
flowchart TD
    Root[Project root]
    Root --> Manifest[af.proj]
    Root --> Arguments[arguments]
    Root --> Registers[registers]
    Root --> Reviews[reviews]
    Reviews --> ReviewItems[items JSON]
    Reviews --> Proposals[proposals]
    Root --> Conformance[conformance]
    Root --> Exports[exports]
    Root --> Internal[.af]
    Internal --> Cache[cache]
    Internal --> Backups[backups]
    Internal --> Snapshots[snapshots]
    Internal --> History[history]
```

## Manifest Model

`core::AssuranceProject` is the in-memory manifest.

| Field | Meaning |
| --- | --- |
| `id`, `name`, `description` | Project identity. |
| `formatVersion` | Manifest schema version. |
| `createdUtc`, `modifiedUtc` | Project timestamps. |
| `defaultLanguage` | Default language for project content. |
| `validationMode` | Validation strictness. |
| `rootPath` | Project directory. |
| `files` | Tracked files and their current state. |

`core::ProjectFileEntry` tracks each file.

| Field | Meaning |
| --- | --- |
| `id` | Stable tracked-file id. |
| `relativePath` | Path from the project root. |
| `role` | SACM argument, evidence register, review items, review proposal, conformance sheet, exported report, or unknown. |
| `state` | Clean, modified, missing, moved, parse error, unsupported version, or generated outdated. |
| `rawHash` | SHA-256 of raw file bytes. |
| `semanticHash` | Content hash independent of formatting where supported. |
| `elementIndexHash` | Hash for element identity/index changes. |
| `relationshipGraphHash` | Hash for graph relationship changes. |
| `parseStatus`, `lastError` | Last parse or validation result. |

## Project Service

`core::ProjectService` owns manifest and tracked-file operations.

```mermaid
flowchart LR
    AppState[core::AppState] --> ProjectService[core::ProjectService]
    ProjectService --> Manifest[af.proj]
    ProjectService --> Files[Tracked files]
    ProjectService --> Hashes[Hashes and file state]
    ProjectService --> Report[ProjectLoadReport]
```

| Operation | Effect |
| --- | --- |
| `CreateEmptyProject` | Creates directory layout and initial manifest. |
| `OpenProject` | Reads `af.proj`, scans files, and returns a load report. |
| `AddSacmFile` | Adds a SACM argument file to the project. |
| `AddEvidenceRegister` | Adds an evidence register. |
| `AddJ3377CaeRegister` | Adds a conformance register. |
| `AddReviewItemsFile` | Adds review item storage. |
| `SaveReviewItemsFile` | Writes review item JSON and tracks it. |
| `AddReviewProposalFile` / `SaveReviewProposalFile` | Writes proposal JSON under reviews/proposals. |
| `RemoveTrackedFile` | Removes a manifest entry and optionally deletes the file. |
| `WriteManifestSafely` | Writes `af.proj`. |
| `RefreshFileStatus` | Recomputes state and hashes for tracked files. |

## Open and Save Flow

```mermaid
sequenceDiagram
    participant UI as UI command
    participant AppState as core::AppState
    participant ProjectService as core::ProjectService
    participant Parser as parser::parse_sacm_xml
    participant Sacm as sacm::parse_sacm
    participant Serializer as sacm::serialize_sacm_to_file

    UI->>AppState: open_project(path)
    AppState->>ProjectService: OpenProject(path)
    ProjectService-->>AppState: AssuranceProject + ProjectLoadReport
    UI->>AppState: open_project_file(entry)
    AppState->>Parser: parse_sacm_xml(file)
    AppState->>Sacm: parse_sacm(file)
    Parser-->>AppState: loaded_case
    Sacm-->>AppState: sacm_package

    UI->>AppState: save_project()
    AppState->>Serializer: serialize_sacm_to_file(sacm_package, active file)
    AppState->>ProjectService: RefreshFileStatus(project)
    AppState->>ProjectService: WriteManifestSafely(project)
```

## File Roles

| Role | Stored data |
| --- | --- |
| `SacmArgument` | SACM XML argument model. |
| `EvidenceRegister` | Evidence register data. |
| `J3377CaeRegister` | J3377/CAE conformance register data. |
| `ReviewItems` | Review item JSON. |
| `ReviewProposal` | Individual review proposal JSON. |
| `ConformanceSheet` | Conformance artifacts. |
| `ExportedReport` | Generated reports. |
| `Unknown` | Tracked but not classified. |

Project state is independent of the active UI selection. Opening a project loads the manifest; opening a project file loads a specific SACM model.