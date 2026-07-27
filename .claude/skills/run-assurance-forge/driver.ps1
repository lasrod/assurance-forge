# Assurance Forge GUI driver.
#
# Drives the ImGui/GLFW desktop app from an agent: launch it at a deterministic
# size, click at window-relative coordinates, send keys, and capture the window
# to a PNG.
#
# Why this exists rather than a few inline PowerShell lines: three things about
# this app defeat the obvious approach, and all three are handled here.
#
#   1. DPI. On a scaled display an unaware process gets virtualized coordinates,
#      so GetWindowRect disagrees with what CopyFromScreen captures and every
#      click lands somewhere else. SetProcessDpiAwarenessContext fixes it, and it
#      must be called before any coordinate call in the process.
#   2. Foreground. Windows refuses SetForegroundWindow from a background process,
#      so the app stays behind whatever is on top -- and because capture reads the
#      screen, the PNG then shows the *other* application, not this one. The
#      AttachThreadInput dance below is what actually raises it.
#   3. Geometry drift. The app restores its window size from hello_imgui.ini, so
#      it comes up maximized one run and 1294x757 the next, and coordinates from
#      a previous session miss. `launch` pins it to a fixed rectangle.
#
# The app renders through OpenGL, so PrintWindow returns black. Capture has to go
# through the screen, which is why raising the window is mandatory rather than
# nice to have.
#
# Usage (paths relative to the repo root):
#   pwsh -File .claude/skills/run-assurance-forge/driver.ps1 -Action launch
#   pwsh -File .claude/skills/run-assurance-forge/driver.ps1 -Action shot  -Out shot.png
#   pwsh -File .claude/skills/run-assurance-forge/driver.ps1 -Action click -X 120 -Y 300
#   pwsh -File .claude/skills/run-assurance-forge/driver.ps1 -Action key   -Keys "%f"
#   pwsh -File .claude/skills/run-assurance-forge/driver.ps1 -Action state
#   pwsh -File .claude/skills/run-assurance-forge/driver.ps1 -Action quit
#
# X/Y are WINDOW-relative (0,0 = top-left of the window, including its title
# bar), which is the same coordinate space as the PNGs this takes. Read a
# screenshot, take the pixel you want, click it.

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('launch', 'shot', 'click', 'scroll', 'key', 'state', 'quit')]
    [string]$Action,

    [string]$Out,
    [int]$X = -1,
    [int]$Y = -1,
    [int]$Notches = -3,
    [string]$Keys,
    [int]$WaitMs = 1200,
    [int]$Width = 1600,
    [int]$Height = 1000,
    [string]$Exe = "build\Release\assurance-forge.exe"
)

$ErrorActionPreference = 'Stop'

$typeDefinition = @"
using System;
using System.Runtime.InteropServices;

public class AfDriver {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    // Must run before anything reads coordinates. -4 is
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2; the older entry point is the
    // fallback for hosts where that is unavailable.
    public static void MakeDpiAware() {
        try { if (SetProcessDpiAwarenessContext(new IntPtr(-4))) return; } catch {}
        try { SetProcessDPIAware(); } catch {}
    }

    // SetForegroundWindow alone is refused for a background process. Attaching to
    // the current foreground thread's input queue lifts that restriction for the
    // duration of the call.
    public static void Raise(IntPtr h) {
        ShowWindow(h, 9); // SW_RESTORE
        uint foreground = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
        uint current = GetCurrentThreadId();
        bool attached = (foreground != current) && AttachThreadInput(foreground, current, true);
        BringWindowToTop(h);
        SetForegroundWindow(h);
        if (attached) { AttachThreadInput(foreground, current, false); }
    }

    public static void ClickScreen(int x, int y) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(180);
        mouse_event(0x0002, 0, 0, 0, IntPtr.Zero); // LEFTDOWN
        System.Threading.Thread.Sleep(90);
        mouse_event(0x0004, 0, 0, 0, IntPtr.Zero); // LEFTUP
    }

    // ImGui routes wheel input to whatever is under the cursor, so the pointer
    // must be parked over the target region first. One notch is 120; negative
    // scrolls down.
    public static void ScrollScreen(int x, int y, int notches) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(180);
        for (int i = 0; i < System.Math.Abs(notches); i++) {
            mouse_event(0x0800, 0, 0, (uint)(notches < 0 ? -120 : 120), IntPtr.Zero); // WHEEL
            System.Threading.Thread.Sleep(60);
        }
    }
}
"@

Add-Type -TypeDefinition $typeDefinition -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
[AfDriver]::MakeDpiAware()

function Get-AppProcess {
    return Get-Process -Name 'assurance-forge' -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } |
        Select-Object -First 1
}

function Get-Rect($handle) {
    $rect = New-Object AfDriver+RECT
    [void][AfDriver]::GetWindowRect($handle, [ref]$rect)
    return $rect
}

function Save-WindowPng($handle, $path) {
    [AfDriver]::Raise($handle)
    Start-Sleep -Milliseconds 700

    $rect = Get-Rect $handle
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -le 0 -or $h -le 0) { throw "window has no area (w=$w h=$h)" }

    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    # CopyFromScreen, not PrintWindow: this window is OpenGL and PrintWindow
    # comes back black. That is why Raise() above is load-bearing.
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $directory = Split-Path -Parent $path
    if ($directory -and -not (Test-Path $directory)) { New-Item -ItemType Directory -Path $directory | Out-Null }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    return "saved $path ($($w)x$($h)) from window at $($rect.Left),$($rect.Top)"
}

switch ($Action) {

    'launch' {
        if (Get-AppProcess) { "already running"; break }
        if (-not (Test-Path $Exe)) { throw "$Exe not found. Build it: cmake --build --preset release" }

        Start-Process -FilePath (Resolve-Path $Exe) -WorkingDirectory (Split-Path -Parent (Resolve-Path $Exe)) | Out-Null

        $process = $null
        for ($i = 0; $i -lt 40; $i++) {
            Start-Sleep -Milliseconds 500
            $process = Get-AppProcess
            if ($process) { break }
        }
        if (-not $process) { throw "app did not open a window within 20s" }

        # Pin the geometry. Without this the window comes back at whatever size
        # hello_imgui.ini remembers, so coordinates are not reproducible between
        # runs -- which silently sends clicks outside the window.
        [AfDriver]::Raise($process.MainWindowHandle)
        Start-Sleep -Milliseconds 500
        [void][AfDriver]::MoveWindow($process.MainWindowHandle, 0, 0, $Width, $Height, $true)
        Start-Sleep -Milliseconds 900

        $rect = Get-Rect $process.MainWindowHandle
        "launched pid=$($process.Id) rect=$($rect.Left),$($rect.Top) size=$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top)"
    }

    'state' {
        $process = Get-AppProcess
        if (-not $process) { "not running"; break }
        $rect = Get-Rect $process.MainWindowHandle
        "running pid=$($process.Id) responding=$($process.Responding) rect=$($rect.Left),$($rect.Top) size=$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top)"
    }

    'shot' {
        $process = Get-AppProcess
        if (-not $process) { throw "app is not running; -Action launch first" }
        if (-not $Out) { throw "-Out <path.png> is required" }
        Save-WindowPng $process.MainWindowHandle $Out
    }

    'click' {
        $process = Get-AppProcess
        if (-not $process) { throw "app is not running; -Action launch first" }
        if ($X -lt 0 -or $Y -lt 0) { throw "-X and -Y are required (window-relative)" }

        [AfDriver]::Raise($process.MainWindowHandle)
        Start-Sleep -Milliseconds 500
        $rect = Get-Rect $process.MainWindowHandle
        $screenX = $rect.Left + $X
        $screenY = $rect.Top + $Y
        if ($screenX -ge $rect.Right -or $screenY -ge $rect.Bottom) {
            throw "($X,$Y) is outside the window ($($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top))"
        }
        [AfDriver]::ClickScreen($screenX, $screenY)
        Start-Sleep -Milliseconds $WaitMs
        "clicked window($X,$Y) = screen($screenX,$screenY)"
    }

    'scroll' {
        $process = Get-AppProcess
        if (-not $process) { throw "app is not running; -Action launch first" }
        if ($X -lt 0 -or $Y -lt 0) { throw "-X and -Y are required (window-relative point to scroll over)" }

        [AfDriver]::Raise($process.MainWindowHandle)
        Start-Sleep -Milliseconds 500
        $rect = Get-Rect $process.MainWindowHandle
        [AfDriver]::ScrollScreen(($rect.Left + $X), ($rect.Top + $Y), $Notches)
        Start-Sleep -Milliseconds $WaitMs
        "scrolled $Notches notches over window($X,$Y)"
    }

    'key' {
        $process = Get-AppProcess
        if (-not $process) { throw "app is not running; -Action launch first" }
        if (-not $Keys) { throw "-Keys is required (System.Windows.Forms.SendKeys syntax)" }
        [AfDriver]::Raise($process.MainWindowHandle)
        Start-Sleep -Milliseconds 400
        [System.Windows.Forms.SendKeys]::SendWait($Keys)
        Start-Sleep -Milliseconds $WaitMs
        "sent [$Keys]"
    }

    'quit' {
        $process = Get-AppProcess
        if (-not $process) { "not running"; break }
        [void]$process.CloseMainWindow()
        Start-Sleep -Seconds 2
        $remaining = Get-Process -Name 'assurance-forge' -ErrorAction SilentlyContinue
        if ($remaining) { $remaining | Stop-Process -Force; "force-closed" } else { "closed" }
    }
}
