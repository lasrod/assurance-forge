# 0011. Panels own their view types, and the layer gate keeps no allow-list

- Status: Accepted
- Date: 2026-08-07
- Deciders: Assurance Forge maintainers
- Relates to: ADR 0002 (layered architecture with build-time gates)

## Context

ADR 0002 established that dependencies run one way and that
`cmake/check_layer_gates.cmake` enforces it at configure time. The gate also
carried an allow-list, and two entries had accumulated in it:

```
ui:ui/panels/preferences_panel.h=ai/
ui:ui/panels/welcome_modal.h=app/
```

Both were individually reasonable. `preferences_panel.h` held an
`ai::AiProviderSettings*` and an `ai::AiConnectionStatus` because those are the
things the preferences screen edits. `welcome_modal.h` aliased
`app::RecentProjectEntry` because that is the record the welcome screen lists.
Neither was a hack; each was the shortest path from a panel to the data it draws.

Collectively they meant something worse than either: the gate no longer described
the architecture. `ui` depends on `ai` and on `app` — except where a file in a
list says otherwise. A rule with exceptions is a rule you have to read the
exceptions to understand, and the natural response to the third violation is a
third entry.

The same file already contained the answer. Its MCP fields carry this comment:

> The panel receives plain data rather than reaching into the `mcp` layer, which
> it must not depend on.

The MCP fields were added under the convention; the AI fields predate it.

## Decision

**A panel owns the type it draws. The layer above maps its own model onto it.**

`ui::panels::RecentProjectEntry` is now a `ui` struct, and `app` copies its
`app::RecentProjectEntry` values into it at the call site.
`PreferencesPanelModel` carries `bool aiEnabled`, a provider name string, a
`ui::panels::AiStatusSeverity` and a message, instead of pointers into `ai`
types; `app` collapses `ai::AiConnectionStatus` — state plus error code — into
the severity the panel renders.

**The allow-list stays empty.** Removing an exception means inverting the
dependency, extracting an interface, or relocating the type — not rewording the
rule. If an entry ever becomes genuinely unavoidable it needs its own ADR and an
issue to remove it.

## Consequences

**A small amount of mapping code now exists in `app`.** Two loops and a switch.
That is the price, and it is paid where the vocabulary is already understood: the
translation from `AiErrorCode` to "draw this in red" belongs with the code that
knows what an `AiErrorCode` means, not in a renderer.

**Panels no longer render a live pointer into application state.** The
preferences panel previously held `ai::AiProviderSettings*` and wrote through it
mid-frame, so a render pass mutated the settings record directly. It now reports
edits through callbacks. This is a behavioural improvement that came free with
the boundary, and it is the change most worth watching in review.

**Duplication is accepted deliberately.** `ui::panels::RecentProjectEntry` has
the same six fields as `app::RecentProjectEntry`. Sharing them by relocating the
type to `core` was the alternative, and it was rejected: recent-projects
preference handling is not reusable assurance-case domain logic, and `core` is
already the largest subsystem in the repository under a standing instruction to
keep it small. Six duplicated fields are cheaper than a wrongly-placed type.

**The gate is now tested.** `layer_gate_negative_check` feeds it nine forbidden
dependencies it must reject and four allowed ones it must not, because a gate
that passes on a clean tree looks identical to a gate that has stopped working.

## What this does not address

`af_common` is an INTERFACE target that gives every subsystem the whole `src/`
include path and the full third-party link surface. Measured against what each
subsystem actually includes:

| Subsystem | Actually uses | Also links |
|---|---|---|
| `core` | picosha2, nlohmann_json | imgui, pugixml, nfd, yaml-cpp, curl |
| `parser` | pugixml, nlohmann_json, yaml-cpp | imgui, nfd, picosha2, curl |
| `ui` | imgui | pugixml, nfd, picosha2, nlohmann_json, yaml-cpp, curl |
| `export`, `sacm_adapter` | *(none)* | all seven |

So the source-level gate is the only thing preventing a cross-layer include:
every layer can already *compile* against every other layer's headers.

Narrowing this touches all eleven targets and can fail differently per platform,
so it is deliberately not bundled with a change that also moves panel interfaces.
It remains open under
[#291](https://github.com/lasrod/assurance-forge/issues/291).
