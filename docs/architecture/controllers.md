# Controllers

Controllers sit between ImGui callbacks and core services. They mutate state, coordinate storage, and emit `AppEvents`.

```mermaid
flowchart LR
    Panels[UI callbacks] --> Runtime[AppRuntime]
    Runtime --> Controllers[Controllers]
    Controllers --> Services[Core services and managers]
    Controllers --> Events[AppEvents]
    Events --> Runtime
```

## Controller Reference

| Controller | Owns or coordinates | Main outputs |
| --- | --- | --- |
| `ElementEditController` | Add top goal, add child, remove selected, remove confirmation state. | `TreeDirtyEvent`, `DocumentDirtyEvent`, selection/status events. |
| `AiReviewController` | AI review request assembly, debug modal state, async task polling, response parsing. | AI `ProblemItem` records, status events. |
| `ReviewController` | Review item persistence, dirty state, delete/resolve workflow. | `ReviewItemsDirtyEvent`, review item file writes, and review-problem resync through AppRuntime. |
| `ProposalController` | Proposal manager, creator mode, preview model, generated id map. | Proposal draft/preview state. |
| `ProjectController` | File buffers, directory scan state, recent-project preferences, create/open modal state. | UI workflow state. |
| `ModalCoordinator` | Cross-cutting modal/window flags and close requests. | Modal visibility and close coordination. |

## Dependencies

```mermaid
flowchart TD
    AppRuntimeState --> ElementEditController
    AppRuntimeState --> AiReviewController
    AppRuntimeState --> ReviewController
    AppRuntimeState --> ProposalController
    AppRuntimeState --> ProjectController
    AppRuntimeState --> ModalCoordinator

    ElementEditController --> ElementFactory[core::ElementFactory functions]
    AiReviewController --> AiService[ai::AiService]
    AiReviewController --> AiTaskRunner[ai::AiTaskRunner]
    AiReviewController --> Problems[core::ProblemsManager]
    ReviewController --> ReviewItemManager[core::reviews::ReviewItemManager]
    ProposalController --> ProposalManager[core::reviews::ReviewProposalManager]
    ProjectController --> RecentProjects[recent project helpers]
    ModalCoordinator --> Events[app::AppEvents]
```

## Add Element Flow

```mermaid
sequenceDiagram
    participant UI as Tree/canvas/menu
    participant Runtime as AppRuntime
    participant Controller as ElementEditController
    participant Factory as core::AddChildElement
    participant Parser as parser::AssuranceCase
    participant Sacm as sacm::AssuranceCasePackage
    participant Events as AppEvents

    UI->>Runtime: add_child(kind)
    Runtime->>Controller: AddChildToSelected(model, package, selected_id, kind)
    Controller->>Factory: AddChildElement(...)
    Factory->>Parser: append element and relationship
    Factory->>Sacm: append typed element and relationship
    Controller->>Events: DocumentDirtyEvent
    Controller->>Events: TreeDirtyEvent
```

## Remove Element Flow

```mermaid
sequenceDiagram
    participant UI as Context menu
    participant Controller as ElementEditController
    participant Factory as core element factory
    participant Events as AppEvents

    UI->>Controller: RemoveSelected(id, mode)
    Controller->>Factory: PlanRemoval(model, id, mode)
    alt multiple affected elements
        Controller-->>UI: show remove confirmation
        UI->>Controller: ConfirmPendingRemoval()
    end
    Controller->>Factory: RemoveElement(model, package, id, mode)
    Controller->>Events: DocumentDirtyEvent
    Controller->>Events: TreeDirtyEvent
```

## Selection Flow

```mermaid
sequenceDiagram
    participant View as Tree/canvas/problems
    participant Events as AppEvents
    participant Runtime as Runtime subscriber
    participant State as ui::UiState

    View->>Events: SelectionChangedEvent(element_id)
    Events->>Runtime: handle event
    Runtime->>State: selected_element_id = element_id
    Runtime->>Events: CenterRequestEvent when needed
```

## AI Review Flow

```mermaid
sequenceDiagram
    participant UI as Problems panel / context action
    participant Controller as AiReviewController
    participant Catalog as SCCG profile catalog
    participant Builder as ai::BuildAiReviewPayload
    participant Data as ai::CollectAiReviewDataPackages
    participant Service as ai::AiService
    participant Runner as ai::AiTaskRunner
    participant Parser as ai::ParseAiReviewResponse
    participant Problems as ProblemsManager
    participant Draft as DraftWorkspaceStore

    UI->>Draft: materialize working model
    Draft-->>UI: working case + tree
    UI->>Controller: BeginReviewForSelection(working case, tree, element_id)
    Controller->>Catalog: select unique profile for element role
    Controller->>Builder: build selected, parent, and children payload
    Controller->>Data: collect profile-required data packages
    Controller-->>UI: request ready in AI Debug panel
    UI->>Controller: StartPendingRequest()
    Controller->>Runner: RunGenerate(service.Generate(request))
    Controller->>Controller: PollTask() each frame
    Runner-->>Controller: AiResponse
    Controller->>Parser: ParseAiReviewResponse(response, element_id)
    Parser-->>Controller: findings + suggested text
    Controller->>Problems: AddOrUpdateProblem(problem)
    Controller-->>Draft: stage ready SCCG correction groups
```

Controllers should stay thin. Core rules belong in `src/core`, parser rules in `src/parser`, SACM round-trip rules in `src/legacy_sacm`, and provider-specific AI behavior in `src/ai`.

Two current exceptions are worth calling out:

- `ui::panels::ShowElementPanel()` edits the active parser element directly, then AppRuntime emits dirty events.
- `ReviewItemsDirtyEvent` is not storage-only. AppRuntime listens for it and rebuilds review-derived problems in `ProblemsManager`.
