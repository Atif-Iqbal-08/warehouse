param(
    [string]$BuildDir = "build-qmake\\release",
    [string]$OutputDir = "dist",
    [string]$AppName = "Warehouse SKU Generator",
    [string]$AppVersion = "1.0.0",
    [string]$Publisher = "Skylark Drones Pvt. Ltd",
    [string]$QtBinDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $RepoRoot $Path
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..")

$resolvedBuildDir = Resolve-RepoPath $BuildDir
if (-not (Test-Path $resolvedBuildDir)) {
    throw "Build output not found: $resolvedBuildDir"
}
$resolvedBuildDir = (Resolve-Path $resolvedBuildDir).Path

$resolvedOutputDir = Resolve-RepoPath $OutputDir
if (-not (Test-Path $resolvedOutputDir)) {
    New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null
}
$resolvedOutputDir = (Resolve-Path $resolvedOutputDir).Path

$exeName = "warehouse_sku_generator.exe"
$exePath = Join-Path $resolvedBuildDir $exeName
if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath"
}

$stagingDir = Join-Path $resolvedOutputDir "staging"
if (Test-Path $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

Copy-Item $exePath $stagingDir -Force

$assetsDir = Join-Path $RepoRoot "Assets"
if (Test-Path $assetsDir) {
    Copy-Item $assetsDir (Join-Path $stagingDir "Assets") -Recurse -Force
}

$windeployqt = $null
if ($QtBinDir) {
    $candidate = Join-Path $QtBinDir "windeployqt.exe"
    if (Test-Path $candidate) {
        $windeployqt = $candidate
    } else {
        throw "windeployqt.exe not found in QtBinDir: $QtBinDir"
    }
} else {
    $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($cmd) {
        $windeployqt = $cmd.Source
    }
}

if ($windeployqt) {
    $stagingExe = Join-Path $stagingDir $exeName
    & $windeployqt --release --no-translations --compiler-runtime $stagingExe | Out-Host
} else {
    $qtRuntimeFound = (Test-Path (Join-Path $resolvedBuildDir "Qt6Core.dll")) -or (Test-Path (Join-Path $resolvedBuildDir "Qt5Core.dll"))
    if (-not $qtRuntimeFound) {
        throw "windeployqt.exe not found and Qt runtime not detected in build output."
    }

    Copy-Item (Join-Path $resolvedBuildDir "*") $stagingDir -Recurse -Force
    $stagingData = Join-Path $stagingDir "data"
    if (Test-Path $stagingData) {
        Remove-Item -Recurse -Force $stagingData
    }

    $cleanupPatterns = @("*.o", "*.obj", "*.cpp", "*.h", "*.ui", "*.pro", "*.cmake")
    foreach ($pattern in $cleanupPatterns) {
        Get-ChildItem -Path $stagingDir -Recurse -Filter $pattern -File -ErrorAction SilentlyContinue | Remove-Item -Force
    }
}

$iscc = $null
$isccCmd = Get-Command iscc -ErrorAction SilentlyContinue
if ($isccCmd) {
    $iscc = $isccCmd.Source
} else {
    $commonIscc = @(
        (Join-Path $env:ProgramFiles "Inno Setup 6\\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\\Inno Setup 6\\ISCC.exe")
    )
    foreach ($candidate in $commonIscc) {
        if ($candidate -and (Test-Path $candidate)) {
            $iscc = $candidate
            break
        }
    }
}

if (-not $iscc) {
    Write-Warning "Inno Setup Compiler (iscc.exe) not found in PATH."
    Write-Host "Staging ready at: $stagingDir"
    Write-Host "Install Inno Setup and run the .iss file to build the installer."
    exit 0
}

$issPath = Join-Path $ScriptDir "warehouse_sku_generator.iss"
if (-not (Test-Path $issPath)) {
    throw "Installer script not found: $issPath"
}

& $iscc "/DAppName=$AppName" "/DAppVersion=$AppVersion" "/DPublisher=$Publisher" "/DSourceDir=$stagingDir" "/DOutputDir=$resolvedOutputDir" $issPath | Out-Host

Write-Host "Installer built in: $resolvedOutputDir"
