# tests/smoke_test.ps1 — Windows UI smoke test for myterm.
# Launches myterm.exe, verifies it starts without crashing, checks for a
# visible window, takes a screenshot, and shuts it down cleanly.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tests/smoke_test.ps1 -ExePath build/myterm.exe

param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,

    [int]$StartupWaitSeconds = 5,

    [string]$ScreenshotPath = "smoke_test_screenshot.png"
)

$ErrorActionPreference = "Stop"

# ── Helpers ──────────────────────────────────────────────────────────────────

function Take-Screenshot {
    param([string]$Path)
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height)
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $screen.Size)
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose()
    $bmp.Dispose()
}

# ── Pre-flight ───────────────────────────────────────────────────────────────

if (-not (Test-Path $ExePath)) {
    Write-Host "FAIL: executable not found at $ExePath" -ForegroundColor Red
    exit 1
}

$resolvedExe = Resolve-Path $ExePath
Write-Host "Smoke test: launching $resolvedExe"

# ── Launch ───────────────────────────────────────────────────────────────────

$proc = Start-Process -FilePath $resolvedExe -PassThru

Write-Host "  PID: $($proc.Id)"
Write-Host "  Waiting $StartupWaitSeconds seconds for startup..."
Start-Sleep -Seconds $StartupWaitSeconds

# ── Check: process still alive (didn't crash on init) ───────────────────────

if ($proc.HasExited) {
    Write-Host "FAIL: myterm exited during startup with code $($proc.ExitCode)" -ForegroundColor Red
    exit 1
}
Write-Host "  PASS: process is running after ${StartupWaitSeconds}s" -ForegroundColor Green

# ── Check: a visible window exists ──────────────────────────────────────────

$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -ne [IntPtr]::Zero) {
    Write-Host "  PASS: main window found (handle: $hwnd, title: '$($proc.MainWindowTitle)')" -ForegroundColor Green
} else {
    Write-Host "  WARN: no main window handle detected (may be off-screen or minimized)" -ForegroundColor Yellow
}

# ── Screenshot ───────────────────────────────────────────────────────────────

try {
    Take-Screenshot -Path $ScreenshotPath
    Write-Host "  PASS: screenshot saved to $ScreenshotPath" -ForegroundColor Green
} catch {
    Write-Host "  WARN: screenshot failed: $_" -ForegroundColor Yellow
}

# ── Shutdown ─────────────────────────────────────────────────────────────────

Write-Host "  Sending close signal..."
# Try graceful close first, then force kill
try {
    $proc.CloseMainWindow() | Out-Null
    if (-not $proc.WaitForExit(5000)) {
        Write-Host "  Graceful close timed out, force killing..."
        $proc.Kill()
        $proc.WaitForExit(3000)
    }
} catch {
    # Process may have already exited
    Write-Host "  Process already exited"
}

if ($proc.HasExited) {
    Write-Host "  PASS: process exited (code: $($proc.ExitCode))" -ForegroundColor Green
} else {
    Write-Host "FAIL: process could not be stopped" -ForegroundColor Red
    exit 1
}

# ── Summary ──────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "Smoke test PASSED" -ForegroundColor Green
exit 0
