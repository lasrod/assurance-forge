# Runtime and State

`app::AppRuntime` is the frame-level orchestrator. `app::AppRuntimeState` owns the long-lived state, controllers, services, derived tree, and UI layout values.

```mermaid
flowchart TD
    Main[main.cpp / Hello ImGui runner] --> Runtime[app::AppRuntime]
    Runtime --> Render[RenderFrame]
    Runtime --> State[app::AppRuntimeState]

    State --> AppState[core::AppState]
    State --> Events[app::AppEvents]
    State --> Problems[core::ProblemsManager]
    State --> Tree[core::AssuranceTree]
    State --> Controllers[Controllers]
    State --> Ai[AI service objects]
    State --> Guidelines[GuidelineCatalog]
```

## Runtime State

| Field | Purpose |
| --- | --- |
| `app_state` | Loaded case, SACM package, active project, active file, dirty flag, status message. |
| `events` | Dispatches typed app events to subscribers. |
| `problems_manager` | Holds manual, validation, import/export, guideline, and AI problems. |
| `*_controller` | Orchestrates UI actions and core services. |
| `guideline_catalog` | SCCG guideline data used by review workflows and AI prompts. |
| `ai_*` | AI settings, secret storage, HTTP client, provider, service, and task runner. |
| `current_tree` | Derived hierarchy built from `app_state.loaded_case`. |
| `tree_needs_rebuild` | Marks `current_tree` stale after model changes. |
| layout ratios | Persisted frame layout sizes for side panels and bottom panel. |

`core::AppState` owns the loaded data:

| Field | Purpose |
| --- | --- |
| `loaded_case` | Flat parser model for UI and derived views. |
| `sacm_package` | Typed SACM package used by save. |
| `current_project` | Open project manifest and tracked file state. |
| `active_project_file_path` | Active file inside a project. |
| `loaded_file_path` | Loaded standalone or project SACM file. |
| `has_unsaved_changes` | Document dirty state. |

## Frame Flow

```mermaid
flowchart TD
    A[RenderFrame] --> B[Poll async work]
    B --> C[Process menus and modal requests]
    C --> D{tree_needs_rebuild?}
    D -- yes --> E[Build core::AssuranceTree from parser model]
    D -- no --> F[Keep current tree]
    E --> G[Render left panels]
    F --> G
    G --> H[Render center tabs]
    H --> I[Render problems panel]
    I --> J[Render right panels]
    J --> K[Render modals]
```

## Event Bus

`app::AppEvents` stores subscribers and dispatches `std::variant` events.

```mermaid
flowchart LR
    UI[Panel callback] --> Controller[Controller]
    Controller --> Core[Core state or service]
    Controller --> Emit[AppEvents::Emit]
    Emit --> Runtime[AppRuntime subscriber]
    Emit --> UIState[ui::UiState update]
    Emit --> Modal[ModalCoordinator]
    Runtime --> Rebuild[Dirty flags and redraw]
```

| Event | Meaning |
| --- | --- |
| `StatusMessageEvent` | Replace the status message shown by the app. |
| `TreeDirtyEvent` | Rebuild `current_tree` from the parser model. |
| `DocumentDirtyEvent` | Mark document state dirty. |
| `ReviewItemsDirtyEvent` | Mark review item storage dirty. |
| `ProjectFilesChangedEvent` | Project manifest or tracked file state changed. |
| `ActiveModelChangedEvent` | A new model is loaded and derived views must refresh. |
| `SelectionChangedEvent` | Selected element changed. |
| `CenterRequestEvent` | Focus the GSN canvas or register tab. |
| `ProposalModeChangedEvent` | Proposal creator or preview mode changed. |
| `ProposalHighlightEvent` | Highlight or dim proposal-related nodes. |
| `ModalRequestEvent` | Open or close a modal/window. |

## Dirty State

```mermaid
sequenceDiagram
    participant UI as UI panel
    participant Controller as Controller
    participant Events as AppEvents
    participant Runtime as AppRuntime
    participant Tree as AssuranceTree

    UI->>Controller: user action
    Controller->>Events: DocumentDirtyEvent
    Controller->>Events: TreeDirtyEvent
    Events->>Runtime: set dirty flags
    Runtime->>Tree: Build(loaded_case)
    Runtime->>UI: render updated views
```

Only the parser model and SACM package are durable editing state. Tree, GSN layout, register rows, and proposal preview state are derived.