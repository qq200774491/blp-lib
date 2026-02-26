param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = "",
    [string]$Generator = "",
    [switch]$BuildBlp,
    [switch]$CopyBlp,
    [switch]$BlpStatic
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "gui/build"
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repoRoot $BuildDir
}

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required command: $Name"
    }
}

function Find-BlpLibrary {
    $candidates = @(
        "target/release/blp_lib.dll",
        "target/release/libblp_lib.dylib",
        "target/release/libblp_lib.so",
        "dist/blp-windows.dll",
        "dist/libblp-macos.dylib",
        "dist/libblp-linux.so"
    )
    foreach ($candidate in $candidates) {
        $path = Join-Path $repoRoot $candidate
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }
    return ""
}

Require-Command "cmake"

if ($BuildBlp) {
    Require-Command "cargo"
    Write-Host "Building BLP library (cargo build --release)..."
    Push-Location $repoRoot
    & cargo build --release
    Pop-Location
    if ($LASTEXITCODE -ne 0) {
        throw "BLP library build failed"
    }
}

Write-Host "Build dir: $BuildDir"

New-Item -ItemType Directory -Force $BuildDir | Out-Null
Push-Location $repoRoot

$cmakeArgs = @("-S", "gui", "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$Config")
if ($BlpStatic) {
    $cmakeArgs += "-DBLP_STATIC_LINK=ON"
} else {
    $cmakeArgs += "-DBLP_STATIC_LINK=OFF"
}
if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $cmakeArgs += @("-G", $Generator)
}

Write-Host "Configuring CMake..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    throw "CMake configure failed"
}

Write-Host "Building..."
& cmake --build $BuildDir --config $Config
Pop-Location
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed"
}

if (($BuildBlp -or $CopyBlp) -and -not $BlpStatic) {
    $blpLib = Find-BlpLibrary
    if ($blpLib) {
        $targetDir = Join-Path $BuildDir $Config
        if (-not (Test-Path -LiteralPath $targetDir)) {
            $targetDir = $BuildDir
        }
        Copy-Item -LiteralPath $blpLib -Destination $targetDir -Force
        Write-Host "Copied BLP library to: $targetDir"
    } else {
        Write-Warning "BLP library not found. Set BLP_LIB_PATH when running."
    }
} elseif ($BlpStatic) {
    Write-Host "BLP static link enabled; skip copying BLP DLL."
}
