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

```powershell
$d = ".claude\skills\run-assurance-forge\driver.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action launch
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action shot -Out "shots\01.png"
```

`launch` pins the window to **0,0 at 1600x1000**, so coordinates are reproducible
between runs. Then read the PNG, find what you want, and click it — screenshot
pixels and click coordinates are the same space (window-relative, `0,0` = the
window's top-left including its title bar).

| Action | Arguments | Notes |
|---|---|---|
| `launch` | `-Width -Height -Exe` | Idempotent; prints the pinned rect. |
| `shot` | `-Out <path.png>` | Raises the window first, then captures it. |
| `click` | `-X -Y [-WaitMs]` | Window-relative. Errors if outside the window. |
| `scroll` | `-X -Y [-Notches]` | Negative scrolls down. Pointer must be over the target region. |
| `key` | `-Keys <SendKeys>` | e.g. `"%f"` = Alt+F, `"^z"` = Ctrl+Z. |
| `state` | | Running / responding / rect. |
| `quit` | | CloseMainWindow, then force. |

A worked flow — open a project, then Preferences, then the section below the fold
(this is exactly the sequence used to verify the MCP settings UI):

```powershell
$d = ".claude\skills\run-assurance-forge\driver.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action launch
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action click -X 130 -Y 818 -WaitMs 4000  # a recent project
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action click -X 186 -Y 75               # Edit menu
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action click -X 237 -Y 175              # Preferences...
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action scroll -X 790 -Y 600 -Notches -10
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action shot -Out "shots\prefs.png"
powershell -NoProfile -ExecutionPolicy Bypass -File $d -Action quit
```

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
