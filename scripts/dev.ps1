param(
    [string]$ExePath = "build/myterm.exe"
)

$ErrorActionPreference = "Stop"

$resolvedExe = (Resolve-Path $ExePath).Path
$repoRoot = Split-Path -Parent $PSScriptRoot
$devRoot = Join-Path $repoRoot ".dev"
$devAppData = Join-Path $devRoot "appdata"
$configDir = Join-Path $devAppData "myterm"
$configPath = Join-Path $configDir "config"

New-Item -ItemType Directory -Path $configDir -Force | Out-Null

if (-not (Test-Path $configPath)) {
    @"
font = C:\Windows\Fonts\CascadiaMono.ttf
font_size = 16.0
width = 960
height = 640
shell = powershell.exe -NoLogo -NoProfile
copy_on_select = false
confirm_close = true
"@ | Set-Content -Path $configPath -Encoding ASCII
}

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $resolvedExe
$psi.WorkingDirectory = Split-Path -Parent $resolvedExe
$psi.UseShellExecute = $false
$psi.EnvironmentVariables["APPDATA"] = $devAppData

$proc = [System.Diagnostics.Process]::Start($psi)
if (-not $proc) {
    throw "Failed to start $resolvedExe"
}

Write-Host "Launched myterm (PID: $($proc.Id)) using dev config: $configPath"
