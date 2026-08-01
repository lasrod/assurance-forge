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
    [ValidateSet('launch', 'shot', 'click', 'scroll', 'key', 'state', 'quit', 'batch')]
    [string]$Action,

    [string]$Out,
    [int]$X = -1,
    [int]$Y = -1,
    [int]$Notches = -3,
    [string]$Keys,
    [int]$WaitMs = 1200,
    [int]$Width = 1600,
    [int]$Height = 1000,
    [string]$Exe = "build\Release\assurance-forge.exe",

    # batch only: ';'-separated steps, run in one process with one focus steal.
    [string]$Script
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

    public static bool IsForeground(IntPtr h) { return GetForegroundWindow() == h; }

    // Raise only when the window is not already on top. Inside a batch the app
    // stays foreground for the whole run, so this turns N focus steals into one.
    public static bool RaiseIfNeeded(IntPtr h) {
        if (IsForeground(h)) { return false; }
        Raise(h);
        return true;
    }

    public static IntPtr CurrentForeground() { return GetForegroundWindow(); }

    // Hand the user's window back when the batch is done, so an automated run
    // does not leave them staring at an application they did not switch to.
    public static void RestoreForeground(IntPtr h) {
        if (h == IntPtr.Zero || h == GetForegroundWindow()) { return; }
        Raise(h);
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

function Save-WindowPng($handle, $path, $crop = $null, [int]$zoom = 1) {
    # Settle only when the raise actually happened; inside a batch the window is
    # already foreground and the compositor has nothing to catch up on.
    if ([AfDriver]::RaiseIfNeeded($handle)) { Start-Sleep -Milliseconds 350 }

    $rect = Get-Rect $handle
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -le 0 -or $h -le 0) { throw "window has no area (w=$w h=$h)" }

    $bitmap = New-Object System.Drawing.Bitmap $w, $h
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    # CopyFromScreen, not PrintWindow: this window is OpenGL and PrintWindow
    # comes back black. That is why raising it is load-bearing.
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $graphics.Dispose()

    $directory = Split-Path -Parent $path
    if ($directory -and -not (Test-Path $directory)) { New-Item -ItemType Directory -Path $directory | Out-Null }

    $output = $bitmap
    $note = ""
    # Crop and zoom here rather than in a follow-up call. Reading a 1600x1000
    # screenshot to inspect a 300px panel wastes most of the pixels, and a
    # separate crop step is another process launch for no interaction value.
    if ($crop) {
        $cx = [Math]::Max(0, [int]$crop[0]); $cy = [Math]::Max(0, [int]$crop[1])
        $cw = [Math]::Min([int]$crop[2], $w - $cx); $ch = [Math]::Min([int]$crop[3], $h - $cy)
        if ($cw -le 0 -or $ch -le 0) { throw "crop $($crop -join ',') falls outside the $($w)x$($h) window" }
        $cropped = New-Object System.Drawing.Bitmap $cw, $ch
        $g2 = [System.Drawing.Graphics]::FromImage($cropped)
        $g2.DrawImage($bitmap, (New-Object System.Drawing.Rectangle 0, 0, $cw, $ch),
                      (New-Object System.Drawing.Rectangle $cx, $cy, $cw, $ch), [System.Drawing.GraphicsUnit]::Pixel)
        $g2.Dispose()
        $output = $cropped
        $note = " crop=$cx,$cy,$cw,$ch"
    }
    if ($zoom -gt 1) {
        $zw = $output.Width * $zoom; $zh = $output.Height * $zoom
        $scaled = New-Object System.Drawing.Bitmap $zw, $zh
        $g3 = [System.Drawing.Graphics]::FromImage($scaled)
        $g3.InterpolationMode = 'NearestNeighbor'
        $g3.DrawImage($output, 0, 0, $zw, $zh)
        $g3.Dispose()
        if ($output -ne $bitmap) { $output.Dispose() }
        $output = $scaled
        $note += " zoom=${zoom}x"
    }

    $output.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $result = "saved $path ($($output.Width)x$($output.Height))$note"
    if ($output -ne $bitmap) { $output.Dispose() }
    $bitmap.Dispose()
    return $result
}

function Start-App([int]$w, [int]$h) {
    $existing = Get-AppProcess
    if ($existing) {
        # Still pin the geometry: a window left at another size from a previous
        # run makes every recorded coordinate miss.
        [void][AfDriver]::RaiseIfNeeded($existing.MainWindowHandle)
        [void][AfDriver]::MoveWindow($existing.MainWindowHandle, 0, 0, $w, $h, $true)
        Start-Sleep -Milliseconds 250
        $r = Get-Rect $existing.MainWindowHandle
        return "already running pid=$($existing.Id) size=$($r.Right - $r.Left)x$($r.Bottom - $r.Top)"
    }
    if (-not (Test-Path $Exe)) { throw "$Exe not found. Build it: cmake --build --preset release" }

    Start-Process -FilePath (Resolve-Path $Exe) -WorkingDirectory (Split-Path -Parent (Resolve-Path $Exe)) | Out-Null

    # Poll tightly: the window usually appears in well under a second, and a
    # 500ms floor added most of a second to every launch for nothing.
    $process = $null
    for ($i = 0; $i -lt 120; $i++) {
        Start-Sleep -Milliseconds 100
        $process = Get-AppProcess
        if ($process) { break }
    }
    if (-not $process) { throw "app did not open a window within 12s" }

    [AfDriver]::Raise($process.MainWindowHandle)
    Start-Sleep -Milliseconds 250
    [void][AfDriver]::MoveWindow($process.MainWindowHandle, 0, 0, $w, $h, $true)
    Start-Sleep -Milliseconds 400

    $rect = Get-Rect $process.MainWindowHandle
    return "launched pid=$($process.Id) rect=$($rect.Left),$($rect.Top) size=$($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top)"
}

function Stop-App {
    $process = Get-AppProcess
    if (-not $process) { return "not running" }
    [void]$process.CloseMainWindow()
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 100
        if (-not (Get-Process -Name 'assurance-forge' -ErrorAction SilentlyContinue)) { return "closed" }
    }
    Get-Process -Name 'assurance-forge' -ErrorAction SilentlyContinue | Stop-Process -Force
    return "force-closed"
}

function Invoke-Click($handle, [int]$px, [int]$py, [int]$pause) {
    [void][AfDriver]::RaiseIfNeeded($handle)
    $rect = Get-Rect $handle
    $screenX = $rect.Left + $px
    $screenY = $rect.Top + $py
    if ($screenX -ge $rect.Right -or $screenY -ge $rect.Bottom) {
        throw "($px,$py) is outside the window ($($rect.Right - $rect.Left)x$($rect.Bottom - $rect.Top))"
    }
    [AfDriver]::ClickScreen($screenX, $screenY)
    Start-Sleep -Milliseconds $pause
    return "clicked ($px,$py)"
}

switch ($Action) {

    'launch' { Start-App $Width $Height }

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
        Invoke-Click $process.MainWindowHandle $X $Y $WaitMs
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

    'quit' { Stop-App }

    # Run a whole verification in one process, one focus steal, and hand the
    # user's window back at the end. Per-call C# compilation and a Raise() per
    # action were what made an interactive check cost minutes of someone else's
    # machine rather than seconds.
    #
    #   -Script "launch; click 130 952 wait=4000; shot a.png; key {F9}; shot b.png crop=16,140,320,420 zoom=3; quit"
    #
    # Steps (';'-separated, options are key=value):
    #   launch [w=INT] [h=INT]
    #   click X Y [wait=MS]
    #   scroll X Y [notches=INT] [wait=MS]
    #   key <SendKeys> [wait=MS]
    #   shot <path.png> [crop=X,Y,W,H] [zoom=INT]
    #   wait MS
    #   quit
    'batch' {
        if (-not $Script) { throw "-Script is required for -Action batch" }

        $userWindow = [AfDriver]::CurrentForeground()
        $clock = [System.Diagnostics.Stopwatch]::StartNew()
        $log = @()
        $handle = [IntPtr]::Zero

        function Resolve-Handle {
            $p = Get-AppProcess
            if (-not $p) { throw "app is not running; put 'launch' earlier in the script" }
            return $p.MainWindowHandle
        }

        try {
            foreach ($raw in ($Script -split ';')) {
                $step = $raw.Trim()
                if (-not $step) { continue }

                $tokens = $step -split '\s+'
                $verb = $tokens[0].ToLower()
                $positional = @($tokens[1..($tokens.Count - 1)] | Where-Object { $_ -notmatch '^\w+=' })
                $options = @{}
                foreach ($t in $tokens) {
                    if ($t -match '^(\w+)=(.+)$') { $options[$Matches[1].ToLower()] = $Matches[2] }
                }
                $pause = if ($options.ContainsKey('wait')) { [int]$options['wait'] } else { $WaitMs }

                switch ($verb) {
                    'launch' {
                        $w = if ($options.ContainsKey('w')) { [int]$options['w'] } else { $Width }
                        $h = if ($options.ContainsKey('h')) { [int]$options['h'] } else { $Height }
                        $log += Start-App $w $h
                        $handle = Resolve-Handle
                    }
                    'click' {
                        if ($handle -eq [IntPtr]::Zero) { $handle = Resolve-Handle }
                        $log += Invoke-Click $handle ([int]$positional[0]) ([int]$positional[1]) $pause
                    }
                    'scroll' {
                        if ($handle -eq [IntPtr]::Zero) { $handle = Resolve-Handle }
                        [void][AfDriver]::RaiseIfNeeded($handle)
                        $n = if ($options.ContainsKey('notches')) { [int]$options['notches'] } else { $Notches }
                        $r = Get-Rect $handle
                        [AfDriver]::ScrollScreen(($r.Left + [int]$positional[0]), ($r.Top + [int]$positional[1]), $n)
                        Start-Sleep -Milliseconds $pause
                        $log += "scrolled $n over ($($positional[0]),$($positional[1]))"
                    }
                    'key' {
                        if ($handle -eq [IntPtr]::Zero) { $handle = Resolve-Handle }
                        [void][AfDriver]::RaiseIfNeeded($handle)
                        [System.Windows.Forms.SendKeys]::SendWait($positional[0])
                        Start-Sleep -Milliseconds $pause
                        $log += "sent [$($positional[0])]"
                    }
                    'shot' {
                        if ($handle -eq [IntPtr]::Zero) { $handle = Resolve-Handle }
                        $crop = $null
                        if ($options.ContainsKey('crop')) {
                            $crop = @($options['crop'] -split ',' | ForEach-Object { [int]$_ })
                            if ($crop.Count -ne 4) { throw "crop needs X,Y,W,H (got '$($options['crop'])')" }
                        }
                        $zoom = if ($options.ContainsKey('zoom')) { [int]$options['zoom'] } else { 1 }
                        $log += Save-WindowPng $handle $positional[0] $crop $zoom
                    }
                    'wait' { Start-Sleep -Milliseconds ([int]$positional[0]); $log += "waited $($positional[0])ms" }
                    'quit' { $log += Stop-App; $handle = [IntPtr]::Zero }
                    default { throw "unknown step '$verb' in: $step" }
                }
            }
        }
        finally {
            # Always give focus back, including when a step threw partway.
            [AfDriver]::RestoreForeground($userWindow)
            $clock.Stop()
        }

        $log += "--- batch finished in $([Math]::Round($clock.Elapsed.TotalSeconds, 1))s, foreground restored"
        $log -join "`n"
    }
}
