# tests/smoke_test.ps1 — Windows UI smoke test for myterm.
# Launches myterm.exe, verifies it starts without crashing, checks for a
# visible window, verifies the configured shell actually executes a command,
# takes a screenshot, and shuts it down cleanly.
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

function Wait-ForFileContent {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Expected,

        [int]$TimeoutSeconds = 10
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $Path) {
            $content = (Get-Content $Path -Raw).Trim()
            if ($content -eq $Expected) {
                return $true
            }
        }
        Start-Sleep -Milliseconds 250
    }

    return $false
}

# ── Pre-flight ───────────────────────────────────────────────────────────────

if (-not (Test-Path $ExePath)) {
    Write-Host "FAIL: executable not found at $ExePath" -ForegroundColor Red
    exit 1
}

$resolvedExe = (Resolve-Path $ExePath).Path
Write-Host "Smoke test: launching $resolvedExe"

# Build a temporary smoke configuration. We place it in the real APPDATA path
# and restore any previous config afterwards so we can verify that myterm is
# actually loading and using its Windows config file.
$guid = [Guid]::NewGuid().ToString("N")
$smokeRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("myterm-smoke-" + $guid)
$realAppDataRoot = $env:APPDATA
$configDir = Join-Path $realAppDataRoot "myterm"
$configPath = Join-Path $configDir "config"
$configBackup = Join-Path $smokeRoot "config.backup"
$shellMarker = Join-Path $smokeRoot "shell-ready.txt"
$shellCmdMarker = Join-Path $smokeRoot "shell-cmd-ok.txt"
$shellHelperExe = Join-Path (Split-Path -Parent $resolvedExe) "smoke_shell_helper.exe"

New-Item -ItemType Directory -Path $smokeRoot -Force | Out-Null
New-Item -ItemType Directory -Path $configDir -Force | Out-Null
if (Test-Path $configPath) {
    Copy-Item -Path $configPath -Destination $configBackup -Force
}

if (-not (Test-Path $shellHelperExe)) {
    Write-Host "FAIL: smoke shell helper not found at $shellHelperExe" -ForegroundColor Red
    if (Test-Path $configBackup) {
        Copy-Item -Path $configBackup -Destination $configPath -Force
    } else {
        Remove-Item $configPath -Force -ErrorAction SilentlyContinue
    }
    exit 1
}

$shellCommand = $shellHelperExe
@"
font = C:\Windows\Fonts\CascadiaMono.ttf
font_size = 16.0
width = 960
height = 640
shell = $shellCommand
"@ | Set-Content -Path $configPath -Encoding ASCII

Write-Host "  Using config path: $configPath"
Write-Host "  Shell helper: $shellHelperExe"
Write-Host "  Shell ready marker: $shellMarker"
Write-Host "  Shell cmd marker: $shellCmdMarker"

# ── Launch ───────────────────────────────────────────────────────────────────

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $resolvedExe
$psi.UseShellExecute = $false
$psi.WorkingDirectory = (Split-Path -Parent $resolvedExe)
$psi.EnvironmentVariables["MYTERM_SMOKE_MARKER"] = $shellMarker
$psi.EnvironmentVariables["MYTERM_SMOKE_CMD_MARKER"] = $shellCmdMarker

$proc = [System.Diagnostics.Process]::Start($psi)
if (-not $proc) {
    Write-Host "FAIL: could not start myterm" -ForegroundColor Red
    exit 1
}

Write-Host "  PID: $($proc.Id)"
Write-Host "  Waiting $StartupWaitSeconds seconds for startup..."
Start-Sleep -Seconds $StartupWaitSeconds

# ── Check: process still alive (didn't crash on init) ───────────────────────

$proc.Refresh()
if ($proc.HasExited) {
    Write-Host "FAIL: myterm exited during startup with code $($proc.ExitCode)" -ForegroundColor Red
    exit 1
}
Write-Host "  PASS: process is running after ${StartupWaitSeconds}s" -ForegroundColor Green

# ── Check: a visible window exists ──────────────────────────────────────────

$hwnd = $proc.MainWindowHandle
if ($hwnd -ne [IntPtr]::Zero) {
    Write-Host "  PASS: main window found (handle: $hwnd, title: '$($proc.MainWindowTitle)')" -ForegroundColor Green
} else {
    Write-Host "  WARN: no main window handle detected (may be off-screen or minimized)" -ForegroundColor Yellow
}

# ── Check: shell process and shell command actually executed ────────────────

$helperReady = Wait-ForFileContent -Path $shellMarker -Expected "MYTERM_SMOKE_READY" -TimeoutSeconds 10
$shellCommandWorked = Wait-ForFileContent -Path $shellCmdMarker -Expected "MYTERM_SMOKE_OK" -TimeoutSeconds 10

if ($helperReady) {
    Write-Host "  PASS: configured shell helper executed successfully" -ForegroundColor Green
} else {
    Write-Host "FAIL: configured shell helper did not execute successfully" -ForegroundColor Red
}

if ($shellCommandWorked) {
    Write-Host "  PASS: shell command executed successfully" -ForegroundColor Green
} else {
    Write-Host "FAIL: shell command did not execute successfully" -ForegroundColor Red
}

if (-not $helperReady -or -not $shellCommandWorked) {
    if (Test-Path $shellMarker) {
        Write-Host "  Helper marker contents:" -ForegroundColor Yellow
        Get-Content $shellMarker
    } else {
        Write-Host "  Helper marker was not created" -ForegroundColor Yellow
    }

    if (Test-Path $shellCmdMarker) {
        Write-Host "  Shell command marker contents:" -ForegroundColor Yellow
        Get-Content $shellCmdMarker
    } else {
        Write-Host "  Shell command marker was not created" -ForegroundColor Yellow
    }

    try {
        if (-not $proc.HasExited) {
            $proc.Kill()
            $proc.WaitForExit(3000)
        }
    } catch {}

    if (Test-Path $configBackup) {
        Copy-Item -Path $configBackup -Destination $configPath -Force
    } else {
        Remove-Item $configPath -Force -ErrorAction SilentlyContinue
    }

    exit 1
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
try {
    $null = $proc.CloseMainWindow()
    if (-not $proc.WaitForExit(5000)) {
        Write-Host "  Graceful close timed out, force killing..."
        $proc.Kill()
        $proc.WaitForExit(3000)
    }
} catch {
    Write-Host "  Process already exited"
}

if ($proc.HasExited) {
    Write-Host "  PASS: process exited (code: $($proc.ExitCode))" -ForegroundColor Green
} else {
    Write-Host "FAIL: process could not be stopped" -ForegroundColor Red
    exit 1
}

# ── Cleanup ──────────────────────────────────────────────────────────────────

try {
    if (Test-Path $configBackup) {
        Copy-Item -Path $configBackup -Destination $configPath -Force
    } else {
        Remove-Item $configPath -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $smokeRoot -Recurse -Force -ErrorAction SilentlyContinue
} catch {}

# ── Summary ──────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "Smoke test PASSED" -ForegroundColor Green
exit 0
