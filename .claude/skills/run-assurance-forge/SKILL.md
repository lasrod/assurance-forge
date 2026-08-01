---
name: run-assurance-forge
description: Build, launch, drive and screenshot Assurance Forge. Use when asked to run or start the app, take a screenshot of the GUI, click through Preferences or the GSN canvas, verify a UI change in the real app rather than in tests, or drive the headless assurance-forge-mcp server over stdio.
---

# Running Assurance Forge

Two binaries ship from this repo:

- **`assurance-forge.exe`** — the ImGui/GLFW/OpenGL desktop app. Driven by
  `.claude/skills/run-assurance-forge/driver.ps1`, which launches it at a fixed
  size and gives you `click` / `scroll` / `key` / `shot`.
- **`assurance-forge-mcp.exe`** — a headless JSON-RPC 2.0 server on stdin/stdout.
  Driven by piping newline-delimited JSON at it. No driver needed.

All paths below are relative to the repo root. Verified on **Windows 11** with
Visual Studio 2022 and CMake 4.3. The GUI driver is Windows-only; the MCP server
and the test suite are cross-platform (CI builds all three OSes).

## Build

```bash
cmake --preset default
cmake --build build --config Release --target assurance-forge assurance-forge-mcp tests
```

Configure takes ~2 minutes (it fetches curl, yaml-cpp, GoogleTest and nlohmann).
The layer gate runs at configure time, so a cross-layer include fails here rather
than at link.

**Not exercised in this session:** a fresh clone also needs
`git submodule update --init --recursive`. This checkout was already initialized
and the submodule had local modifications, so running it would have discarded
them. Treat it as required-but-unverified for a clean machine.

## Run: the desktop app (agent path)

**This app runs on someone's desktop while they are using it.** Capture has to go
through the screen (see Gotchas), so every driver action steals focus. Treat
foreground time as a borrowed resource: batch the whole check into one run,
finish in seconds, and give the window back.

### Use `batch` — one process, one focus steal

```powershell
$d = ".claude\skills\run-assurance-forge\driver.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action batch -Script `
  "launch; click 130 952 wait=4000; shot shots\main.png; shot shots\panel.png crop=16,140,320,420 zoom=3; quit"
```

That whole sequence takes **~8s** and restores the user's foreground window when
it finishes — including if a step throws. The same thing as separate `-Action`
calls costs minutes: PowerShell recompiles the interop C# on *every* invocation,
and every action raises the window again.

Steps are `;`-separated; options are `key=value`:

| Step | Form | Notes |
|---|---|---|
| `launch` | `launch [w=1600] [h=1000]` | Pins the rect. Idempotent. |
| `click` | `click X Y [wait=MS]` | Window-relative. |
| `scroll` | `scroll X Y [notches=-3] [wait=MS]` | Pointer must be over the target region. |
| `key` | `key {F9} [wait=MS]` | SendKeys syntax: `%f` = Alt+F, `^z` = Ctrl+Z. |
| `shot` | `shot path.png [crop=X,Y,W,H] [zoom=N]` | Crop/zoom happen in-process. |
| `wait` | `wait 500` | |
| `quit` | `quit` | |

`crop`/`zoom` matter more than they look: reading a 1600x1000 screenshot to
inspect a 300px panel wastes most of the pixels, and cropping afterwards is
another process launch for no interaction value.

### Budget the whole verification, not each step

The expensive mistake is interleaving builds with app runs — the app must be
closed for the link step anyway, so each interleave is another launch, another
raise, another minute. Instead:

1. Make **all** the code changes.
2. Build once (`cmake --build build --config Release --target assurance-forge`).
3. Run **one** batch that exercises everything you need to see.
4. Read the PNGs and decide.

### Window size: review logical space, not pixels

`launch` pins the window at 0,0 and derives its size from the display's DPI
scaling, targeting a **1080p-equivalent** (1920x1080 logical). On a 3840x2160
display at 175% that is 3360x1890 physical; `launch` prints what it used.

This matters more than it sounds. The driver is DPI-aware, so it pins *physical*
pixels, while ImGui renders fonts scaled by DPI. A 1600x1000 window on a 175%
display is only ~914x571 logical pixels — a quarter of a 1080p screen — with
text still drawn at 1.75x. Panels are then hopelessly cramped and labels
truncate for reasons the application is not responsible for.

**A UI review done in an undersized window will manufacture findings.** Reviewing
this app at 1600x1000 produced "the Problems table columns are badly
proportioned, Message collapses to `Me…`" — which is simply untrue at
1080p-equivalent, where Message gets the majority of the width and renders a
full sentence. Three attempts were spent "fixing" a non-problem before the
window size was checked. Check `launch`'s printed size against the display
before concluding anything is too narrow.

Pass `-Width`/`-Height` to pin a different size deliberately — e.g. to test how
panels behave when genuinely cramped. Coordinates are only reproducible at a
given pinned size.

### Never send input to a project you did not create

**A batch containing `click` or `key` must run against a throwaway copy.**
Screenshot-only batches may open anything.

This is not a caution, it is a rule, because the obvious mitigations have both
failed in practice:

* *"Click the project I want"* — the welcome screen's recent list **reorders by
  last-opened**, so a remembered coordinate opens a different project on the
  next run.
* *"Verify the name first, then type"* — verifying happens in one batch and the
  typing in the next, by which time the list has reordered again.

Twice this corrupted a real safety case: a `key XYZ` and a `key Z` intended for
a scratch fixture landed in `MySafetyCase`, and the command bus autosaved both
to disk before anyone saw a screenshot.

Make a copy first, and open it by path rather than by clicking a list:

```powershell
$scratch = "$env:TEMP\claude\af-scratch"
Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
Copy-Item -Recurse "C:\development\assurance-forge-projects\MySafetyCase" $scratch
```

Then drive `File > Open Project` to `$scratch`, or seed the recent list by
launching once against it. Verify the project name in the status bar in the
**same batch** that sends the input, and prefer `tests/data` fixtures over any
copy of the user's work.

### Single actions

`-Action launch|shot|click|scroll|key|state|quit` still exist, with `-Out -X -Y
-Notches -Keys -WaitMs -Width -Height`. Use them only when exploring somewhere
you have no coordinates for yet and genuinely need to look between each step —
then collapse what you learned into one `batch` for the actual verification.

**Look at the screenshot.** If it shows a different application, the window was
not raised — see Gotchas.

## Run: the MCP server (agent path)

Headless, so just pipe at it. It speaks one JSON object per line.

```bash
SETTINGS=/tmp/mcp-on.json
echo '{"mcp":{"enabled":true}}' > "$SETTINGS"
printf '%s\n%s\n%s\n' \
 '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"clientInfo":{"name":"probe","version":"1"}}}' \
 '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
 '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_case_overview","arguments":{}}}' \
 | ./build/Release/assurance-forge-mcp.exe \
     --project tests/data/fixture_roundtrip_core_argument.sacm.xml \
     --settings "$SETTINGS" 2>/dev/null
```

`--settings` exists so a probe never reads the developer's real consent flag.
Without `{"mcp":{"enabled":true}}` every content-bearing tool refuses by design —
that is the consent gate (ADR 0007), not a misconfiguration.

`--project` takes a project directory, a manifest, or a bare `.sacm`/`.xml` file.
A bare file has no project directory, so proposal tools refuse against it.

## Run: the desktop app (human path)

`build\Release\assurance-forge.exe`, double-clicked. Restores its window geometry
from `hello_imgui.ini`, which is why the driver re-pins it.

## Test

```bash
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

923 tests, ~40s. Two `Sacm23InteropCorpus` tests skip unless the third-party
corpus is present — that is expected, not a failure.

## Gotchas

- **DPI virtualization silently breaks every coordinate.** A process that is not
  DPI-aware gets scaled values from `GetWindowRect`, so captures and clicks land
  somewhere else entirely. The driver calls
  `SetProcessDpiAwarenessContext(-4)` before anything else. Any new tooling must
  do the same or it will disagree with the driver.
- **`SetForegroundWindow` is refused from a background process**, and because
  capture reads the *screen*, an unraised window means the PNG shows whatever
  application is in front. This is the failure mode most likely to waste your
  time: the screenshot looks fine, it is just of the wrong program. The driver's
  `Raise()` uses `AttachThreadInput` against the current foreground thread, which
  is what actually works.
- **`PrintWindow` returns black.** The window is OpenGL, so capturing without
  raising is not an option — hence the point above being mandatory rather than
  cosmetic.
- **Window geometry is restored from `hello_imgui.ini`**, so the app comes up
  maximized one run and 1294x757 the next. Coordinates recorded from a previous
  session will miss, sometimes landing outside the window entirely. `launch`
  pins the rect for this reason; do not skip it.
- **The Preferences dialog scrolls.** Sections are Appearance → Review → AI →
  MCP Server, and at 1600x1000 only the first two and a half are visible. Use
  `scroll` before concluding a section is missing.
- **ImGui routes wheel events to whatever is under the cursor**, not to the
  focused widget, so `scroll` needs `-X -Y` over the region you mean.
- **The app can die when the shell that launched it exits.** Observed once
  between two tool calls. `Start-Process` as the driver does it survives; if the
  app vanishes between steps, re-`launch` rather than assuming a crash — check
  `state` first.
- **For the MCP server, stdout is the transport.** Any stray write corrupts the
  stream and presents as a broken client. `cmake/run_mcp_smoke_test.cmake`
  asserts the exact response line count and the absence of carriage returns; if
  you add logging, send it to stderr.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Screenshot shows a different application | Window was not raised | Use the driver's `shot`; it raises first. If it persists, check nothing is forcing itself topmost. |
| `clicked window(x,y)` but nothing happened | Window not at the pinned rect | `-Action state` to print the actual rect; re-`launch`. |
| `(X,Y) is outside the window` | Coordinates from a differently-sized run | Re-`launch` to pin 1600x1000 and re-read a fresh screenshot. |
| `app is not running; -Action launch first` | App exited between calls | `-Action state`, then `launch`. |
| MCP tool returns `has not been given permission` | Consent gate closed | Point `--settings` at a file containing `{"mcp":{"enabled":true}}`. |
| `assurance-forge-mcp not found next to Assurance Forge` in Preferences | Only the GUI target was built | Build `assurance-forge-mcp` too. |
| Configure fails on missing `sccg.full.yaml` | SCCG submodule not populated | Regenerate `dist/` in `external/safety-case-core-guidelines`. |
