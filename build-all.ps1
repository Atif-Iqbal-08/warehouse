param(
    [string]$BuildDir = "build-qmake",
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$QtBinDir = "",
    [string]$MakeTool = "",
    [string]$OutputDir = "dist",
    [string]$AppVersion = "1.0.0",
    [string]$Publisher = "Skylark Drones Pvt. Ltd",
    [switch]$Clean,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Split-Path -Parent $MyInvocation.MyCommand.Path)).Path
$buildScriptPath = Join-Path $RepoRoot "build.ps1"
$installerScriptPath = Join-Path $RepoRoot "installer\build-installer.ps1"

if (-not (Test-Path $buildScriptPath)) {
    throw "Build script not found: $buildScriptPath"
}
if (-not (Test-Path $installerScriptPath)) {
    throw "Installer build script not found: $installerScriptPath"
}

$buildArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $buildScriptPath,
    "-BuildDir", $BuildDir,
    "-Config", $Config
)

if ($Clean) {
    $buildArgs += "-Clean"
}
if ($QtBinDir) {
    $buildArgs += @("-QtBinDir", $QtBinDir)
}
if ($MakeTool) {
    $buildArgs += @("-MakeTool", $MakeTool)
}
if ($Verbose) {
    $buildArgs += "-Verbose"
}

& powershell @buildArgs
if ($LASTEXITCODE -ne 0) {
    throw "Application build failed."
}

$installerArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $installerScriptPath,
    "-BuildDir", $BuildDir,
    "-BuildConfig", $Config,
    "-OutputDir", $OutputDir,
    "-AppVersion", $AppVersion,
    "-Publisher", $Publisher,
    "-SkipBuild"
)

if ($QtBinDir) {
    $installerArgs += @("-QtBinDir", $QtBinDir)
}

& powershell @installerArgs
if ($LASTEXITCODE -ne 0) {
    throw "Installer build failed."
}

$installerPath = Join-Path $RepoRoot (Join-Path $OutputDir "warehouse_installer.exe")
if (Test-Path $installerPath) {
    Write-Host "Installer ready: $installerPath"
} else {
    throw "Installer output not found: $installerPath"
}
