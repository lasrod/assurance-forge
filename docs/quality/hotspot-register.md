# Hotspot register

The ranked answer to "where does cleanup pay for itself", maintained for
[#293](https://github.com/lasrod/assurance-forge/issues/293). Companion to the
[repository quality baseline](repository-baseline.md), which measures the whole
tree at a point in time; this page ranks the places where size, change
frequency, dependency fan-in and domain risk coincide.

Rank is not size. `libs/sacm/src/commands/commands.cpp` is the second-largest
file in the repository and is not ranked; `src/core/commands/command_bus.cpp`
is 238 lines and is. The difference is the other three inputs.

| Field | Value |
|---|---|
| Measured at | commit `abcf65d`, tip of `main` |
| Measured on | 2026-08-08 |
| Churn windows | Full history (first commit 2026-04-17) and "recent" (since 2026-05-01) |

## What the ranking combines

Three measured inputs — physical lines, non-merge commits touching the file,
and which subsystems include its public header — and one judgement input: the
high-risk list from [#293](https://github.com/lasrod/assurance-forge/issues/293),
which names the code where a defect costs a safety argument rather than a
repaint:

- SACM document ownership and projection boundaries.
- Save/load, migration, and compatibility preservation.
- Audit replay, undo, recovery, and canonicalization.
- Command dispatch and the library-primary transition.
- AI proposal application and user-approval boundaries.
- MCP command validation and mutation authorization.

Domain risk dominates. That is why the audit replayer outranks the larger XMI
reader, and why the highest-churn `ui` file is not ranked at all. The ranking
is a judgement over published inputs, not a formula — published so it can be
contested by re-running the commands under [Reproducing](#reproducing).

## The register

Lines are physical lines (`wc -l`). Commits are non-merge commits touching the
file, full history / since 2026-05-01. Fan-in is which subsystems' files
`#include` the entry's public headers.

| # | Hotspot | Lines | Commits | Fan-in | Risk domain |
|---|---|---|---|---|---|
| 1 | [`src/app/app_runtime.*`](https://github.com/lasrod/assurance-forge/tree/main/src/app) (10-file cluster) | 5,302 | 95 / 81 | Confined to `src/app` | Save/load, undo, AI application, command handling |
| 2 | [`src/core/audit/event_replayer.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/audit/event_replayer.cpp) | 1,979 | 17 / 17 | `core`, tests | Audit replay, undo, recovery |
| 3 | [`src/sacm_adapter/document_edit.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/document_edit.cpp) + [`case_projection.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/sacm_adapter/case_projection.cpp) | 1,690 + 539 | 11 + 14 (full history, per file) | `app`, `core`, `sacm_adapter`, tests | Ownership and projection boundary, library-primary transition |
| 4 | [`libs/sacm/src/io/xmi_reader.cpp`](https://github.com/lasrod/assurance-forge/blob/main/libs/sacm/src/io/xmi_reader.cpp) | 2,303 | 12 / 12 | via `sacm/io/xmi.h`: `sacm_adapter`, library tests and tools | Save/load |
| 5 | [`src/core/element_factory.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/element_factory.cpp) | 1,604 | 20 / 17 | 37 files: `app`, `core`, `export`, `ui`, tests | Document ownership (creation semantics) |
| 6 | [`src/core/app_state.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/app_state.cpp) / `.h` | 443 + 110 | 26 / 23 | 19 files: `agent`, `app`, `core`, `mcp`, `ui`, tests | Ownership, save/load |
| 7 | [`src/core/commands/command_bus.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/commands/command_bus.cpp) | 238 | 13 / 13 | 26 files: `app`, `core`, tests | Command dispatch |
| 8 | [`src/app/controllers/ai_review_controller.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/app/controllers/ai_review_controller.cpp) | 688 | 14 / 12 | `app` | AI proposal application, user approval |
| 9 | [`src/mcp/tools.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/mcp/tools.cpp) | 569 | 12 / 12 | `mcp` executable | MCP validation and mutation authorization |
| 10 | [`src/core/project_service.cpp`](https://github.com/lasrod/assurance-forge/blob/main/src/core/project_service.cpp) | 781 | 17 / 13 | `app`, `core`, tests | Project save/load, migration |

**1. `app_runtime.*`** — one cluster concentrates project workflow, save/load
(`_io`, `_project`), undo (`_undo`), AI application (`_ai`) and command
handling, and 81 of its 95 commits are recent, so it is where mutations and
change pressure meet. Its low fan-in is by design — `ui` receives action
objects instead of including `app` — which concentrates risk here rather than
spreading it. Reduction: responsibilities leave for controllers with their own
tests; `app_runtime_project.cpp` (1,979 lines) exits the top-five size list;
the cluster's share of recent churn falls.

**2. `event_replayer.cpp`** — replays the audit log to rebuild, undo and
recover state, so a mis-applied event rewrites a safety argument silently: the
loss class the tool exists to prevent. Reduction: per-event-kind appliers
reviewable in isolation, each pinned by characterization tests that predate the
restructuring; the file leaves the top-five size list. Restructuring here
follows the
[legacy-bridge migration plan](../architecture/legacy-bridge-migration-plan.md),
which owns the replay seams' retirement order.

**3. The `sacm_adapter` seam** — where application edits become `libs/sacm`
commands and the library document is projected back for display; an ownership
or projection error shows the user an argument that differs from the XML that
will be saved. Reduction: seam functions narrow to one operation each behind
round-trip characterization tests, and the seam remains the only crossing
(`sacm/commands` is included by exactly two files today; that number staying
put is the observable).

**4. `xmi_reader.cpp`** — the largest file in the repository, on the path of
every import, and home of the only target-scoped warning suppression
([`/wd4456`](code-quality-policy.md#suppressions), sixteen shadowing instances
in its `dynamic_cast` chains). Ranked below smaller files because 84.6% library
line coverage and the [conformance matrix](../sacm/sacm-conformance-matrix.md)
already constrain it. Reduction: per-package readers; `/wd4456` retired;
conformance tests unchanged.

**5. `element_factory.cpp`** — defines element-creation semantics consumed by
four subsystems through 37 including files, so a wrong default propagates to
the canvas, the exporter and the tree at once. Reduction: creation logic split
by element family behind a narrower interface; the 37 falls.

**6. `app_state.*`** — the widest state fan-in in the repository, reaching even
the `agent` and `mcp` layers, and its central invariant — `current_project` is
assigned in two places and never reset — is held by convention, not by type;
twelve of [#306](https://github.com/lasrod/assurance-forge/issues/306)'s
optional-access findings rested on exactly that fact. Reduction: the invariant
becomes structural, so neither a reader nor an analyser has to re-derive it.

**7. `command_bus.cpp`** — every model mutation dispatches through these 238
lines, the highest churn-per-line in the register; a dispatch defect turns a
valid edit into a silent no-op or a wrong-target mutation. Reduction: the
interface settles — recent-window churn approaching zero — with dispatch
behavior pinned by characterization tests.

**8. `ai_review_controller.cpp`** — applies AI-proposed changes at the
user-approval boundary, where "the tool never silently modifies a safety
argument" is enforced in code rather than prose. Reduction: application
separated from presentation, and every apply path behind a test proving a
rejected proposal changes nothing.

**9. `mcp/tools.cpp`** — where an external agent's mutation requests are
validated and authorized before touching a case; all twelve of its commits are
recent, so the surface has not settled. Reduction: churn settles and every
tool's rejection paths are tested, not only its accept paths.

**10. `project_service.cpp`** — owns project save/load and manifest handling,
the path where a compatibility mistake destroys work on open rather than on
edit; the warning pass already caught drift here (two constants duplicating
ones `project_manifest.cpp` reads). Reduction: manifest knowledge has one
owner, and load behavior is covered by migration round-trip tests.

## Watched, not ranked

Listed so their omission reads as a decision, not an oversight.

| File | Signal | Why not ranked |
|---|---|---|
| `libs/sacm/src/commands/commands.cpp` | 1,923 lines, 6 commits | Near-lowest churn in this table and constrained by conformance tests. Moves up if the library-primary transition raises its churn. |
| `src/ui/gsn/gsn_canvas.cpp` | 916 lines, 30 / 26 commits | Highest-churn `ui` file, but the failure mode is visual, not data loss. The canvas/SVG dual-renderer split (see [layers and ownership](../architecture/layers-and-ownership.md)) still makes every GSN fix land twice. |
| `src/ui/ui_state.h` | 27 commits | Transient view state; no persistence path. |
| `src/ui/panels/element_panel.cpp` | 24 commits | Edits element fields, but mutations flow through the command path ranked above. |

## How this register is maintained

- **Regenerated when** a cleanup issue linked from a row closes — the closer
  re-measures the affected rows — and whenever the
  [repository baseline](repository-baseline.md) is regenerated, which re-ranks
  the whole table.
- **By whom**: whoever makes either of those changes. The commands below take
  about a minute; no tooling beyond git and grep is required.
- **Staleness criterion**: treat this page as stale when a cited path no longer
  exists, when re-running the commands moves a ranked measurement by more than
  roughly 20%, or when the recorded commit is more than three months old.
  Nothing gates the numbers, deliberately — the same argument the baseline
  makes for itself: a snapshot going stale is correct behavior, and gating it
  would force every unrelated change to regenerate it.

## Reproducing

```bash
# Churn, full history and recent window
git log --no-merges --format= --name-only -- 'src/*' 'libs/*' | sort | uniq -c | sort -rn
git log --no-merges --since=2026-05-01 --format= --name-only -- 'src/*' 'libs/*' | sort | uniq -c | sort -rn

# Size
wc -l src/core/audit/event_replayer.cpp libs/sacm/src/io/xmi_reader.cpp ...

# Fan-in, collapsed by eye to subsystems
grep -rl '#include "core/app_state.h"' src libs tests
```

## Limitations

- **No complexity measure.** clang-tidy is configured but no complexity check
  is in its enabled set, and no dedicated tool (lizard or equivalent) exists.
  The baseline lists this as an unavailable measurement; adding it is a
  follow-up to [#293](https://github.com/lasrod/assurance-forge/issues/293),
  and when it lands it becomes a ranking input here.
- **Churn counts commits, not diff size** — a typo fix and a rewrite weigh the
  same — and full-history churn favors files that existed early. The repository
  is under four months old, so the bias is mild; the recent column is the
  correction.
- **Fan-in is `#include` edges at file granularity**, collapsed to subsystems.
  It sees neither transitive inclusion nor CMake link dependencies — the gap
  [#340](https://github.com/lasrod/assurance-forge/issues/340) exists to close.
- **No defect-history weighting.** The tracker does not label defect fixes, so
  "changed often" standing in for "risky" is an assumption this page states
  rather than verifies.
- Cluster churn for row 1 deduplicates commits across the cluster (95 unique);
  row 3 lists per-file counts without deduplication.
