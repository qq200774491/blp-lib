param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot "..")
$buildDir = Join-Path $repoRoot "gui\build\$Config"
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
