param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$QtPrefix = ""
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot "..")
$buildDir = Join-Path $repoRoot "qt_gui\build\$Config"
$stageDir = Join-Path $scriptRoot "stage"
$issPath = Join-Path $scriptRoot "blp_viewer.iss"

if (-not (Test-Path -LiteralPath $buildDir)) {
    throw "Build output not found: $buildDir"
}

Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$exePath = Join-Path $buildDir "blp_viewer.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Missing blp_viewer.exe in $buildDir"
}

Copy-Item -LiteralPath $exePath -Destination $stageDir -Force

$blpLib = Join-Path $buildDir "blp_lib.dll"
if (Test-Path -LiteralPath $blpLib) {
    Copy-Item -LiteralPath $blpLib -Destination $stageDir -Force
}

$thumbnailDll = Join-Path $buildDir "blp_thumbnail.dll"
if (Test-Path -LiteralPath $thumbnailDll) {
    Copy-Item -LiteralPath $thumbnailDll -Destination $stageDir -Force
}

$windeployqt = $null
if (-not [string]::IsNullOrWhiteSpace($QtPrefix)) {
    $candidate = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (Test-Path -LiteralPath $candidate) {
        $windeployqt = $candidate
    }
}
if (-not $windeployqt) {
    $cmd = Get-Command "windeployqt" -ErrorAction SilentlyContinue
    if ($cmd) {
        $windeployqt = $cmd.Source
    }
}
if (-not $windeployqt) {
    throw "windeployqt not found. Provide -QtPrefix or add it to PATH."
}

Write-Host "Running windeployqt..."
& $windeployqt "$stageDir\blp_viewer.exe" --dir "$stageDir"
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed"
}

$issCmd = Get-Command "iscc" -ErrorAction SilentlyContinue
if (-not $issCmd) {
    Write-Warning "Inno Setup (iscc) not found in PATH. Install it to build the installer."
    Write-Host "Stage prepared at: $stageDir"
    exit 0
}

Write-Host "Building installer..."
& $issCmd.Source $issPath
if ($LASTEXITCODE -ne 0) {
    throw "Installer build failed"
}

Write-Host "Done. Output in $scriptRoot\dist"
