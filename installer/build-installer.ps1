param(
    [string]$BuildDir = "build-qmake",
    [string]$OutputDir = "dist",
    [string]$AppName = "Warehouse SKU Generator",
    [string]$AppVersion = "1.1.0",
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

function Resolve-QtBinDirPath {
    param([string]$RequestedQtBinDir)

    if (-not [string]::IsNullOrWhiteSpace($RequestedQtBinDir)) {
        $resolvedQtBinDir = Resolve-RepoPath $RequestedQtBinDir
        if (-not (Test-Path $resolvedQtBinDir)) {
            throw "Qt bin directory not found: $resolvedQtBinDir"
        }
        return (Resolve-Path $resolvedQtBinDir).Path
    }

    $windeployqtCmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($windeployqtCmd) {
        return (Split-Path -Parent $windeployqtCmd.Source)
    }

    $qmakeCmd = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmakeCmd) {
        $qtBinDir = (& $qmakeCmd.Source -query QT_INSTALL_BINS 2>$null).Trim()
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($qtBinDir) -and (Test-Path $qtBinDir)) {
            return (Resolve-Path $qtBinDir).Path
        }
    }

    $qtpathsCmd = Get-Command qtpaths -ErrorAction SilentlyContinue
    if ($qtpathsCmd) {
        $qtBinDir = (& $qtpathsCmd.Source --query QT_INSTALL_BINS 2>$null).Trim()
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($qtBinDir) -and (Test-Path $qtBinDir)) {
            return (Resolve-Path $qtBinDir).Path
        }
    }

    throw "Qt bin directory could not be resolved. Pass -QtBinDir or ensure qmake/windeployqt is in PATH."
}

function Resolve-MingwBinDir {
    param([string]$QtBinDirPath)

    $candidateDirs = @()

    $gxxCmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gxxCmd) {
        $candidateDirs += (Split-Path -Parent $gxxCmd.Source)
    }

    $objdumpCmd = Get-Command objdump -ErrorAction SilentlyContinue
    if ($objdumpCmd) {
        $candidateDirs += (Split-Path -Parent $objdumpCmd.Source)
    }

    $qtToolsCandidates = Get-ChildItem -Path "C:\Qt\Tools\mingw*\bin" -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName }
    $candidateDirs += $qtToolsCandidates

    if (-not [string]::IsNullOrWhiteSpace($QtBinDirPath)) {
        $qtRoot = Split-Path -Parent $QtBinDirPath
        if ($qtRoot) {
            $candidateDirs += Get-ChildItem -Path (Join-Path (Split-Path -Parent $qtRoot) "Tools\mingw*\bin") -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName }
        }
    }

    foreach ($candidate in $candidateDirs | Select-Object -Unique) {
        if ([string]::IsNullOrWhiteSpace($candidate) -or -not (Test-Path $candidate)) {
            continue
        }
        if ((Test-Path (Join-Path $candidate "g++.exe")) -and
            (Test-Path (Join-Path $candidate "libstdc++-6.dll")) -and
            (Test-Path (Join-Path $candidate "libgcc_s_seh-1.dll")) -and
            (Test-Path (Join-Path $candidate "libwinpthread-1.dll"))) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "MinGW runtime directory could not be resolved. Install Qt MinGW tools or add them to PATH."
}

function Resolve-ObjdumpPath {
    param([string]$MingwBinDir)

    $objdumpCandidates = @()
    if (-not [string]::IsNullOrWhiteSpace($MingwBinDir)) {
        $objdumpCandidates += (Join-Path $MingwBinDir "objdump.exe")
    }
    $objdumpCandidates += Get-ChildItem -Path "C:\Qt\Tools\mingw*\bin\objdump.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        ForEach-Object { $_.FullName }

    $objdumpPath = Resolve-Executable -CommandName "objdump" -Candidates $objdumpCandidates
    if (-not $objdumpPath) {
        throw "objdump.exe not found. It is required to verify packaged DLL dependencies."
    }
    return $objdumpPath
}

function Get-ImportedDllNames {
    param(
        [string]$BinaryPath,
        [string]$ObjdumpPath
    )

    $objdumpOutput = & $ObjdumpPath -p $BinaryPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to inspect PE imports for $BinaryPath"
    }

    $imports = @()
    foreach ($line in $objdumpOutput) {
        if ($line -match 'DLL Name:\s+(.+)$') {
            $imports += $matches[1].Trim()
        }
    }

    return @($imports | Sort-Object -Unique)
}

function Should-StageRedistributable {
    param([string]$DllName)

    $normalized = $DllName.Trim().ToLowerInvariant()
    if ($normalized -match '^(qt|lib).+\.dll$') {
        return $true
    }

    return @(
        "d3dcompiler_47.dll",
        "opengl32sw.dll"
    ) -contains $normalized
}

function Resolve-DependencySource {
    param(
        [string]$DllName,
        [string[]]$SearchRoots
    )

    foreach ($root in $SearchRoots | Select-Object -Unique) {
        if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path $root)) {
            continue
        }

        $candidate = Join-Path $root $DllName
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Copy-DependencyTree {
    param(
        [string[]]$RootBinaryPaths,
        [string]$DestinationDir,
        [string[]]$SearchRoots,
        [string]$ObjdumpPath
    )

    $queue = [System.Collections.Queue]::new()
    $visited = @{}
    $missing = @()

    foreach ($binaryPath in $RootBinaryPaths) {
        if ([string]::IsNullOrWhiteSpace($binaryPath) -or -not (Test-Path $binaryPath)) {
            continue
        }
        $queue.Enqueue((Resolve-Path $binaryPath).Path)
    }

    while ($queue.Count -gt 0) {
        $currentPath = [string]$queue.Dequeue()
        $currentKey = $currentPath.ToLowerInvariant()
        if ($visited.ContainsKey($currentKey)) {
            continue
        }
        $visited[$currentKey] = $true

        foreach ($dllName in Get-ImportedDllNames -BinaryPath $currentPath -ObjdumpPath $ObjdumpPath) {
            if (-not (Should-StageRedistributable -DllName $dllName)) {
                continue
            }

            $stagedDllPath = Join-Path $DestinationDir $dllName
            if (-not (Test-Path $stagedDllPath)) {
                $sourcePath = Resolve-DependencySource -DllName $dllName -SearchRoots $SearchRoots
                if (-not $sourcePath) {
                    $missing += "$dllName (required by $(Split-Path -Leaf $currentPath))"
                    continue
                }
                Copy-Item $sourcePath $stagedDllPath -Force
            }

            $queue.Enqueue((Resolve-Path $stagedDllPath).Path)
        }
    }

    if ($missing.Count -gt 0) {
        throw "Failed to copy redistributable DLLs: $($missing | Sort-Object -Unique -join ', ')"
    }
}

function Get-StagedPluginBinaryPaths {
    param([string]$StagingDir)

    $pluginDirs = @("platforms", "sqldrivers", "imageformats", "styles")
    $pluginBinaries = @()
    foreach ($pluginDir in $pluginDirs) {
        $resolvedPluginDir = Join-Path $StagingDir $pluginDir
        if (-not (Test-Path $resolvedPluginDir)) {
            continue
        }
        $pluginBinaries += Get-ChildItem -Path $resolvedPluginDir -Recurse -Filter *.dll -File -ErrorAction SilentlyContinue |
            ForEach-Object { $_.FullName }
    }

    return @($pluginBinaries | Sort-Object -Unique)
}

function Get-MissingStagedDependencies {
    param(
        [string[]]$RootBinaryPaths,
        [string]$StagingDir,
        [string]$ObjdumpPath
    )

    $queue = [System.Collections.Queue]::new()
    $visited = @{}
    $missing = @()

    foreach ($binaryPath in $RootBinaryPaths) {
        if ([string]::IsNullOrWhiteSpace($binaryPath)) {
            continue
        }
        if (-not (Test-Path $binaryPath)) {
            $missing += "$binaryPath (missing packaged binary)"
            continue
        }
        $queue.Enqueue((Resolve-Path $binaryPath).Path)
    }

    while ($queue.Count -gt 0) {
        $currentPath = [string]$queue.Dequeue()
        $currentKey = $currentPath.ToLowerInvariant()
        if ($visited.ContainsKey($currentKey)) {
            continue
        }
        $visited[$currentKey] = $true

        foreach ($dllName in Get-ImportedDllNames -BinaryPath $currentPath -ObjdumpPath $ObjdumpPath) {
            if (-not (Should-StageRedistributable -DllName $dllName)) {
                continue
            }

            $stagedDllPath = Join-Path $StagingDir $dllName
            if (-not (Test-Path $stagedDllPath)) {
                $missing += "$dllName (required by $(Split-Path -Leaf $currentPath))"
                continue
            }

            $queue.Enqueue((Resolve-Path $stagedDllPath).Path)
        }
    }

    return @($missing | Sort-Object -Unique)
}

function Copy-QtPluginSet {
    param(
        [string]$QtPluginsDir,
        [string]$StagingDir
    )

    $pluginSpecs = @(
        @{ RelativePath = "platforms\qwindows.dll"; Required = $true },
        @{ RelativePath = "sqldrivers\qsqlite.dll"; Required = $true },
        @{ RelativePath = "imageformats\qgif.dll"; Required = $false },
        @{ RelativePath = "imageformats\qico.dll"; Required = $false },
        @{ RelativePath = "imageformats\qjpeg.dll"; Required = $false },
        @{ RelativePath = "styles\qmodernwindowsstyle.dll"; Required = $false }
    )

    $copiedPaths = @()
    foreach ($pluginSpec in $pluginSpecs) {
        $sourcePath = Join-Path $QtPluginsDir $pluginSpec.RelativePath
        if (-not (Test-Path $sourcePath)) {
            if ($pluginSpec.Required) {
                throw "Required Qt plugin not found: $sourcePath"
            }
            continue
        }

        $destinationPath = Join-Path $StagingDir $pluginSpec.RelativePath
        $destinationDir = Split-Path -Parent $destinationPath
        if (-not (Test-Path $destinationDir)) {
            New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
        }

        Copy-Item $sourcePath $destinationPath -Force
        $copiedPaths += (Resolve-Path $destinationPath).Path
    }

    return @($copiedPaths)
}

function Copy-OptionalGraphicsRuntimes {
    param(
        [string]$QtBinDir,
        [string]$StagingDir
    )

    foreach ($dllName in @("d3dcompiler_47.dll", "opengl32sw.dll")) {
        $sourcePath = Join-Path $QtBinDir $dllName
        if (-not (Test-Path $sourcePath)) {
            continue
        }

        $destinationPath = Join-Path $StagingDir $dllName
        if (-not (Test-Path $destinationPath)) {
            Copy-Item $sourcePath $destinationPath -Force
        }
    }
}

function Invoke-WindeployQt {
    param(
        [string]$WindeployQtPath,
        [string]$QtBinDir,
        [string]$StagingExePath,
        [string]$BuildConfig
    )

    if ([string]::IsNullOrWhiteSpace($WindeployQtPath) -or -not (Test-Path $WindeployQtPath)) {
        return $false
    }

    $deployArgs = @()
    $qtpathsPath = Join-Path $QtBinDir "qtpaths.exe"
    if (Test-Path $qtpathsPath) {
        $deployArgs += @("--qtpaths", $qtpathsPath)
    }

    $deployArgs += if ($BuildConfig -eq "Debug") { "--debug" } else { "--release" }
    $deployArgs += @("--no-translations", "--compiler-runtime", $StagingExePath)

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $WindeployQtPath `
            -ArgumentList $deployArgs `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        if ($process.ExitCode -ne 0) {
            $windeployMessages = @()
            if (Test-Path $stdoutPath) {
                $windeployMessages += Get-Content $stdoutPath -ErrorAction SilentlyContinue
            }
            if (Test-Path $stderrPath) {
                $windeployMessages += Get-Content $stderrPath -ErrorAction SilentlyContinue
            }
            $windeployMessages = @($windeployMessages | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
            if ($windeployMessages.Count -gt 0) {
                Write-Warning "windeployqt output: $($windeployMessages -join ' | ')"
            }
        }

        return ($process.ExitCode -eq 0)
    } catch {
        Write-Warning "windeployqt execution failed: $($_.Exception.Message)"
        return $false
    } finally {
        Remove-Item $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }
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

$resolvedQtBinDir = Resolve-QtBinDirPath -RequestedQtBinDir $QtBinDir
$qtRootDir = Split-Path -Parent $resolvedQtBinDir
$qtPluginsDir = Join-Path $qtRootDir "plugins"
if (-not (Test-Path $qtPluginsDir)) {
    throw "Qt plugins directory not found: $qtPluginsDir"
}

$resolvedMingwBinDir = Resolve-MingwBinDir -QtBinDirPath $resolvedQtBinDir
$objdumpPath = Resolve-ObjdumpPath -MingwBinDir $resolvedMingwBinDir
$windeployqt = Resolve-Executable -CommandName "windeployqt" -Candidates @((Join-Path $resolvedQtBinDir "windeployqt.exe"))
$stagingExe = Join-Path $stagingDir $exeName
$requiredPluginPaths = @(
    (Join-Path $stagingDir "platforms\qwindows.dll"),
    (Join-Path $stagingDir "sqldrivers\qsqlite.dll")
)

$windeploySucceeded = $false
if ($windeployqt) {
    $windeploySucceeded = Invoke-WindeployQt -WindeployQtPath $windeployqt -QtBinDir $resolvedQtBinDir -StagingExePath $stagingExe -BuildConfig $BuildConfig
    if (-not $windeploySucceeded) {
        Write-Warning "windeployqt failed. Falling back to manual Qt/MinGW deployment."
    }
}

$stagedPluginBinaryPaths = Get-StagedPluginBinaryPaths -StagingDir $stagingDir
$missingRequiredPlugins = @($requiredPluginPaths | Where-Object { -not (Test-Path $_) })
$missingStagedDependencies = Get-MissingStagedDependencies -RootBinaryPaths (@($stagingExe) + $stagedPluginBinaryPaths) -StagingDir $stagingDir -ObjdumpPath $objdumpPath

if (-not $windeploySucceeded -or $missingRequiredPlugins.Count -gt 0 -or $missingStagedDependencies.Count -gt 0) {
    if ($missingRequiredPlugins.Count -gt 0) {
        Write-Warning "Packaged Qt plugins are incomplete: $($missingRequiredPlugins -join ', ')"
    }
    if ($missingStagedDependencies.Count -gt 0) {
        Write-Warning "Packaged redistributable DLLs are incomplete: $($missingStagedDependencies -join ', ')"
    }

    $copiedPluginPaths = Copy-QtPluginSet -QtPluginsDir $qtPluginsDir -StagingDir $stagingDir
    Copy-OptionalGraphicsRuntimes -QtBinDir $resolvedQtBinDir -StagingDir $stagingDir

    $rootBinariesForCopy = @($stagingExe) + $copiedPluginPaths + (Get-StagedPluginBinaryPaths -StagingDir $stagingDir)
    $dependencySearchRoots = @($stagingDir, $resolvedQtBinDir, $resolvedMingwBinDir, $exeDir)
    Copy-DependencyTree -RootBinaryPaths $rootBinariesForCopy -DestinationDir $stagingDir -SearchRoots $dependencySearchRoots -ObjdumpPath $objdumpPath

    $stagedPluginBinaryPaths = Get-StagedPluginBinaryPaths -StagingDir $stagingDir
    $missingRequiredPlugins = @($requiredPluginPaths | Where-Object { -not (Test-Path $_) })
    $missingStagedDependencies = Get-MissingStagedDependencies -RootBinaryPaths (@($stagingExe) + $stagedPluginBinaryPaths) -StagingDir $stagingDir -ObjdumpPath $objdumpPath
}

if ($missingRequiredPlugins.Count -gt 0) {
    throw "Installer staging is missing required Qt plugins: $($missingRequiredPlugins -join ', ')"
}

if ($missingStagedDependencies.Count -gt 0) {
    throw "Installer staging is missing redistributable DLLs: $($missingStagedDependencies -join ', ')"
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
if ($LASTEXITCODE -ne 0) {
    throw "Installer compilation failed."
}

Write-Host "Installer built in: $resolvedOutputDir"
