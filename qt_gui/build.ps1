param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$BuildDir = "",
    [string]$Generator = "",
    [string]$QtPrefix = "",
    [switch]$BuildBlp,
    [switch]$CopyBlp,
    [switch]$BlpStatic
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "qt_gui/build"
} elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repoRoot $BuildDir
}

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required command: $Name"
    }
}

function Normalize-QtPrefix {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }

    $full = (Resolve-Path -LiteralPath $Path).Path.TrimEnd("\", "/")
    if ($full -match ".*[\\/]lib[\\/]cmake[\\/]Qt6$") {
        return (Resolve-Path -LiteralPath (Join-Path $full "..\\..\\..")).Path
    }
    if ($full -match ".*[\\/]lib[\\/]cmake[\\/]Qt5$") {
        return (Resolve-Path -LiteralPath (Join-Path $full "..\\..\\..")).Path
    }

    return $full
}

function Find-QtPrefix {
    param([string]$Override)

    $prefix = Normalize-QtPrefix $Override
    if ($prefix) {
        return $prefix
    }

    foreach ($var in @("Qt6_DIR", "Qt5_DIR", "QTDIR")) {
        $val = [Environment]::GetEnvironmentVariable($var)
        $prefix = Normalize-QtPrefix $val
        if ($prefix) {
            return $prefix
        }
    }

    foreach ($cmd in @("qtpaths6", "qtpaths")) {
        if (Get-Command $cmd -ErrorAction SilentlyContinue) {
            $candidate = & $cmd --install-prefix 2>$null
            if ($LASTEXITCODE -eq 0) {
                $prefix = Normalize-QtPrefix $candidate
                if ($prefix) {
                    return $prefix
                }
            }
        }
    }

    if (Get-Command "qmake" -ErrorAction SilentlyContinue) {
        $candidate = & qmake -query QT_INSTALL_PREFIX 2>$null
        if ($LASTEXITCODE -eq 0) {
            $prefix = Normalize-QtPrefix $candidate
            if ($prefix) {
                return $prefix
            }
        }
    }

    return ""
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

$qtPrefix = Find-QtPrefix $QtPrefix
if (-not $qtPrefix) {
    throw "Qt not found. Set -QtPrefix or define Qt6_DIR/Qt5_DIR/QTDIR."
}

Write-Host "Qt prefix: $qtPrefix"
Write-Host "Build dir: $BuildDir"

New-Item -ItemType Directory -Force $BuildDir | Out-Null
Push-Location $repoRoot

$cmakeArgs = @("-S", "qt_gui", "-B", $BuildDir, "-DCMAKE_PREFIX_PATH=$qtPrefix", "-DCMAKE_BUILD_TYPE=$Config")
if ($BlpStatic) {
    $cmakeArgs += "-DBLP_STATIC_LINK=ON"
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
