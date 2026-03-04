param(
    [string]$BuildDir = "build-qmake",
    [string]$OutputDir = "dist",
    [string]$AppName = "Warehouse SKU Generator",
    [string]$AppVersion = "1.0.0",
    [string]$Publisher = "Skylark Drones Pvt. Ltd",
    [string]$QtBinDir = "",
    [ValidateSet("Release", "Debug")]
    [string]$BuildConfig = "Release",
    [switch]$CleanBuild,
    [switch]$SkipBuild
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

if (-not $SkipBuild) {
    $buildScriptPath = Join-Path $RepoRoot "build.ps1"
    if (-not (Test-Path $buildScriptPath)) {
        throw "Build script not found: $buildScriptPath"
    }

    $buildArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $buildScriptPath,
        "-BuildDir", $BuildDir,
        "-Config", $BuildConfig
    )

    if ($CleanBuild) {
        $buildArgs += "-Clean"
    }
    if ($QtBinDir) {
        $buildArgs += @("-QtBinDir", $QtBinDir)
    }

    & powershell @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Application build failed. Installer build stopped."
    }
}

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
$exeCandidates = @(
    (Join-Path $resolvedBuildDir $exeName),
    (Join-Path (Join-Path $resolvedBuildDir "release") $exeName),
    (Join-Path (Join-Path $resolvedBuildDir "Release") $exeName),
    (Join-Path (Join-Path $resolvedBuildDir "RelWithDebInfo") $exeName),
    (Join-Path (Join-Path $resolvedBuildDir "debug") $exeName),
    (Join-Path (Join-Path $resolvedBuildDir "Debug") $exeName)
)
$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exePath) {
    throw "Executable not found. Checked: $($exeCandidates -join ', ')"
}
$exeDir = Split-Path -Parent $exePath

$stagingDir = Join-Path $resolvedOutputDir "staging"
if (Test-Path $stagingDir) {
    try {
        Remove-Item -Recurse -Force $stagingDir -ErrorAction Stop
    } catch {
        $fallbackStamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $stagingDir = Join-Path $resolvedOutputDir "staging_$fallbackStamp"
        Write-Warning "Unable to clean existing staging directory. Using fallback: $stagingDir"
    }
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

Copy-Item $exePath $stagingDir -Force

$assetsDir = Join-Path $RepoRoot "Assets"
if (Test-Path $assetsDir) {
    Copy-Item $assetsDir (Join-Path $stagingDir "Assets") -Recurse -Force
}

$skuReferenceImages = @(
    (Join-Path $stagingDir "Assets\\SKU Ref 1.png"),
    (Join-Path $stagingDir "Assets\\SKU Ref 2.png")
)
foreach ($assetPath in $skuReferenceImages) {
    if (-not (Test-Path $assetPath)) {
        throw "Missing required installer asset: $assetPath"
    }
}

$docsDir = Join-Path $RepoRoot "docs"
$docsToStage = @(
    "Warehouse_SKU_QR_Manager_User_Guide.md",
    "Warehouse_SKU_QR_Manager_Technical_Documentation.md",
    "Warehouse_SKU_QR_Manager_User_Guide.docx",
    "Warehouse_SKU_QR_Manager_Technical_Documentation.docx"
)
if (Test-Path $docsDir) {
    $stagingDocsDir = Join-Path $stagingDir "docs"
    New-Item -ItemType Directory -Path $stagingDocsDir -Force | Out-Null
    foreach ($docName in $docsToStage) {
        $sourceDocPath = Join-Path $docsDir $docName
        if (Test-Path $sourceDocPath) {
            Copy-Item $sourceDocPath (Join-Path $stagingDocsDir $docName) -Force
        }
    }
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
    $deployMode = if ($BuildConfig -eq "Debug") { "--debug" } else { "--release" }
    & $windeployqt $deployMode --no-translations --compiler-runtime $stagingExe | Out-Host
} else {
    $qtRuntimeFound = (Test-Path (Join-Path $exeDir "Qt6Core.dll")) -or (Test-Path (Join-Path $exeDir "Qt5Core.dll"))
    if (-not $qtRuntimeFound) {
        throw "windeployqt.exe not found and Qt runtime not detected in build output."
    }

    Copy-Item (Join-Path $exeDir "*") $stagingDir -Recurse -Force
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
