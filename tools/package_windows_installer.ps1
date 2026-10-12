# ============================================================================
# Serein Scope Windows installer packaging script
#
# Packages the flat candidate delivery (out\yserein-scope) into a NSIS
# installer, mirroring the Serein (Tauri+NSIS) install experience:
#   - per-user install into %LOCALAPPDATA%\Serein\Serein Scope (no admin)
#   - the chosen location is remembered, so a reinstall never re-asks
#   - re-running the installer for the same version performs a one-step
#     reinstall that keeps the existing location
#
# Usage (from repo root):
#   powershell.exe -ExecutionPolicy Bypass -File tools\package_windows_installer.ps1
#   powershell.exe -ExecutionPolicy Bypass -File tools\package_windows_installer.ps1 `
#       -CandidateDir build\windows-release\out -OutputDir dist
# ============================================================================
param(
    [string]$CandidateDir = '',
    [string]$OutputDir = ''
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $CandidateDir) { $CandidateDir = Join-Path $repo 'out\yserein-scope' }
if (-not $OutputDir) { $OutputDir = Join-Path $repo 'out' }

# --- Resolve makensis.exe (Tauri ships NSIS under %LOCALAPPDATA%\tauri\NSIS) ---
function Get-MakensisPath {
    $candidates = @()
    if ($env:LOCALAPPDATA) {
        $candidates += Join-Path $env:LOCALAPPDATA 'tauri\NSIS\makensis.exe'
    }
    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }
    $cmd = Get-Command makensis -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }
    $found = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $found) {
        throw "makensis.exe not found. Install NSIS or run the Serein Tauri build once so that %LOCALAPPDATA%\tauri\NSIS\makensis.exe exists."
    }
    return $found
}

# --- Read the product version from CMakeLists.txt --------------------------
function Get-ProjectVersion {
    $cmake = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw -Encoding UTF8
    $m = [regex]::Match($cmake, 'project\(SereinScope\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
    if (-not $m.Success) {
        throw "Could not read project VERSION from CMakeLists.txt"
    }
    return $m.Groups[1].Value
}

if (-not (Test-Path -LiteralPath $CandidateDir)) {
    throw "Candidate directory not found: $CandidateDir`nRun windows_user_action.ps1 first to build the flat delivery."
}

$makensis = Get-MakensisPath
$version = Get-ProjectVersion

# --- Prepare a temporary working directory for the resolved .nsi ------------
$tmpDir = Join-Path $env:TEMP ('serein-scope-installer-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    $installerName = "SereinScope_{0}_x64-setup.exe" -f $version
    $installerPath = Join-Path $OutputDir $installerName

    $template = Get-Content (Join-Path $PSScriptRoot 'installer.nsi.in') -Raw -Encoding UTF8
    $template = $template -replace '@@VERSION@@', $version
    $template = $template -replace '@@SRC_DIR@@', $CandidateDir
    $template = $template -replace '@@OUT_FILE@@', $installerName

    $nsiPath = Join-Path $tmpDir 'installer.nsi'
    # NSIS 对无 BOM 的脚本按系统 ANSI 代码页解析；模板含中文（如第 47 行
    # "运行 Serein Scope"），必须写成 UTF-8 带 BOM，否则 makensis 报
    # "Bad text encoding"。
    $utf8WithBom = New-Object System.Text.UTF8Encoding -ArgumentList $true
    [System.IO.File]::WriteAllText($nsiPath, $template, $utf8WithBom)

    # --- Build the installer ---
    Push-Location $tmpDir
    try {
        $macroOut = & $makensis (Split-Path -Leaf $nsiPath) 2>&1
        $macroOut | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) {
            throw "makensis failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }

    $built = Join-Path $tmpDir $installerName
    if (-not (Test-Path -LiteralPath $built)) {
        # makensis uses OutFile as given; fall back to common output name.
        $built = Join-Path $tmpDir 'nsis-output.exe'
    }
    if (-not (Test-Path -LiteralPath $built)) {
        throw "Installer output not found in: $tmpDir"
    }

    if (-not (Test-Path -LiteralPath $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $built -Destination $installerPath -Force

    Write-Host ""
    Write-Host "[OK] Installer ready:"
    Write-Host "  $installerPath"
    Write-Host "  Version: $version"
    Write-Host "  Source:  $CandidateDir"
} finally {
    if (Test-Path -LiteralPath $tmpDir) {
        Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
