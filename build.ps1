param(
    [string]$BuildDir = "build-qmake",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$QtBinDir = "",
    [string]$MakeTool = "",
    [switch]$Clean,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Split-Path -Parent $MyInvocation.MyCommand.Path)).Path
$localQmakeFeatures = Join-Path $RepoRoot "qmake-overrides\features"
if (Test-Path $localQmakeFeatures) {
    if ([string]::IsNullOrWhiteSpace($env:QMAKEFEATURES)) {
        $env:QMAKEFEATURES = $localQmakeFeatures
    } else {
        $featureEntries = $env:QMAKEFEATURES -split ";"
        if ($featureEntries -notcontains $localQmakeFeatures) {
            $env:QMAKEFEATURES = "$localQmakeFeatures;$env:QMAKEFEATURES"
        }
    }
}

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $RepoRoot $Path
}

function Resolve-Executable {
    param(
        [string]$CommandName,
        [string[]]$Candidates = @()
    )

    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Prepend-ToPath {
    param([string]$DirectoryPath)

    if ([string]::IsNullOrWhiteSpace($DirectoryPath) -or -not (Test-Path $DirectoryPath)) {
        return
    }

    $currentEntries = $env:PATH -split ";"
    if ($currentEntries -contains $DirectoryPath) {
        return
    }

    $env:PATH = "$DirectoryPath;$env:PATH"
}

function Resolve-MakeTool {
    param(
        [string]$RequestedTool,
        [string]$QMakePath
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedTool)) {
        $requestedAsPath = Resolve-RepoPath $RequestedTool
        if (Test-Path $requestedAsPath) {
            return (Resolve-Path $requestedAsPath).Path
        }

        $requestedCmd = Get-Command $RequestedTool -ErrorAction SilentlyContinue
        if ($requestedCmd) {
            return $requestedCmd.Source
        }

        throw "Requested make tool was not found: $RequestedTool"
    }

    $qmakeSpec = (& $QMakePath -query QMAKE_SPEC 2>$null).Trim()
    $isMinGW = $false
    if ($qmakeSpec -match "mingw|g\+\+") {
        $isMinGW = $true
    } elseif ($QMakePath -match "mingw") {
        $isMinGW = $true
    }

    $mingwCandidates = @(
        (Get-ChildItem -Path "C:\Qt\Tools\mingw*\bin\mingw32-make.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            ForEach-Object { $_.FullName })
    )
    $jomCandidates = @(
        (Get-ChildItem -Path "C:\Qt\Tools\QtCreator\bin\jom\jom.exe" -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName })
    )

    if ($isMinGW) {
        $tool = Resolve-Executable -CommandName "mingw32-make" -Candidates $mingwCandidates
        if ($tool) { return $tool }

        $tool = Resolve-Executable -CommandName "jom" -Candidates $jomCandidates
        if ($tool) { return $tool }

        $tool = Resolve-Executable -CommandName "nmake"
        if ($tool) { return $tool }
    } else {
        $tool = Resolve-Executable -CommandName "jom" -Candidates $jomCandidates
        if ($tool) { return $tool }

        $tool = Resolve-Executable -CommandName "nmake"
        if ($tool) { return $tool }

        $tool = Resolve-Executable -CommandName "mingw32-make" -Candidates $mingwCandidates
        if ($tool) { return $tool }
    }

    throw "No make tool found. Install mingw32-make/jom/nmake or pass -MakeTool."
}

function Resolve-QMakePath {
    param([string]$QtBinDirPath)

    if (-not [string]::IsNullOrWhiteSpace($QtBinDirPath)) {
        $resolvedQtBinDir = Resolve-RepoPath $QtBinDirPath
        $qmakeFromQtBinDir = Join-Path $resolvedQtBinDir "qmake.exe"
        if (Test-Path $qmakeFromQtBinDir) {
            return (Resolve-Path $qmakeFromQtBinDir).Path
        }
        throw "qmake.exe was not found in QtBinDir: $resolvedQtBinDir"
    }

    $qmakeCandidates = @()
    if ($env:QTDIR) {
        $qmakeCandidates += Join-Path $env:QTDIR "bin\qmake.exe"
    }
    $qmakeCandidates += Get-ChildItem -Path "C:\Qt\*\*\bin\qmake.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "QtDesignStudio" } |
        Sort-Object FullName -Descending |
        ForEach-Object { $_.FullName }

    $qmakePath = Resolve-Executable -CommandName "qmake" -Candidates $qmakeCandidates
    if ($qmakePath) {
        return $qmakePath
    }

    throw "qmake.exe was not found. Install Qt and pass -QtBinDir (for example C:\Qt\6.9.3\mingw_64\bin)."
}

$resolvedBuildDir = Resolve-RepoPath $BuildDir

if ($Clean -and (Test-Path $resolvedBuildDir)) {
    Remove-Item -Recurse -Force $resolvedBuildDir
}
if (-not (Test-Path $resolvedBuildDir)) {
    New-Item -ItemType Directory -Path $resolvedBuildDir -Force | Out-Null
}

if (-not (Test-Path $resolvedBuildDir)) {
    throw "Unable to create build directory: $resolvedBuildDir"
}

if (-not (Test-Path (Join-Path $RepoRoot "warehouse_sku_generator.pro"))) {
    throw "Project file not found: $(Join-Path $RepoRoot "warehouse_sku_generator.pro")"
}

$qmakePath = Resolve-QMakePath -QtBinDirPath $QtBinDir
$makeToolPath = Resolve-MakeTool -RequestedTool $MakeTool -QMakePath $qmakePath

Prepend-ToPath -DirectoryPath (Split-Path -Parent $qmakePath)
Prepend-ToPath -DirectoryPath (Split-Path -Parent $makeToolPath)

$qmakeSpec = (& $qmakePath -query QMAKE_SPEC 2>$null).Trim()
$usesMinGW = $false
if ($qmakeSpec -match "mingw|g\+\+") {
    $usesMinGW = $true
} elseif ($qmakePath -match "mingw" -or $makeToolPath -match "mingw32-make") {
    $usesMinGW = $true
}

if ($usesMinGW) {
    $mingwBinCandidates = @()
    $makeToolDir = Split-Path -Parent $makeToolPath
    if ($makeToolDir) {
        $mingwBinCandidates += $makeToolDir
    }
    $mingwBinCandidates += Get-ChildItem -Path "C:\Qt\Tools\mingw*\bin" -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName }

    $mingwBinDir = $mingwBinCandidates |
        Select-Object -Unique |
        Where-Object {
            (Test-Path (Join-Path $_ "g++.exe")) -and
            (Test-Path (Join-Path $_ "gcc.exe"))
        } |
        Select-Object -First 1

    if (-not $mingwBinDir) {
        throw "MinGW toolchain not found. Install Qt MinGW tools or pass -MakeTool pointing to a working MinGW setup."
    }

    Prepend-ToPath -DirectoryPath $mingwBinDir
}

Push-Location $resolvedBuildDir
try {
    $qmakeArgs = @((Join-Path $RepoRoot "warehouse_sku_generator.pro"))
    if ($Config -eq "Release") {
        $qmakeArgs += @("CONFIG+=release", "CONFIG-=debug", "CONFIG-=debug_and_release")
    } else {
        $qmakeArgs += @("CONFIG+=debug", "CONFIG-=release", "CONFIG-=debug_and_release")
    }

    & $qmakePath @qmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "qmake step failed."
    }

    $makeArgs = @()
    if ($Verbose -and ($makeToolPath -match "mingw32-make")) {
        $makeArgs += "VERBOSE=1"
    }

    & $makeToolPath @makeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Build step failed."
    }
} finally {
    Pop-Location
}

$preferredConfigDir = "release"
if ($Config -eq "Debug") {
    $preferredConfigDir = "debug"
}

$exeCandidates = @(
    (Join-Path (Join-Path $resolvedBuildDir $preferredConfigDir) "warehouse_sku_generator.exe"),
    (Join-Path $resolvedBuildDir "warehouse_sku_generator.exe"),
    (Join-Path (Join-Path $resolvedBuildDir "release") "warehouse_sku_generator.exe"),
    (Join-Path (Join-Path $resolvedBuildDir "debug") "warehouse_sku_generator.exe")
)
$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($exePath) {
    Write-Host "qmake: $qmakePath"
    Write-Host "make : $makeToolPath"
    Write-Host "Build succeeded: $exePath"
} else {
    Write-Warning "Build completed, but executable was not found."
    Write-Host "Checked: $($exeCandidates -join ', ')"
}
