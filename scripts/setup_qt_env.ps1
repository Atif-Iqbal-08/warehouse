param(
    [string]$QtBinDir = "",
    [string]$MinGwBinDir = "",
    [switch]$PersistUser
)

$ErrorActionPreference = "Stop"

function Resolve-UniquePath {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Add-ToPath {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }
    $entries = $env:Path -split ";"
    if ($entries -contains $Value) {
        return
    }
    $env:Path = "$Value;$env:Path"
}

if ([string]::IsNullOrWhiteSpace($QtBinDir)) {
    $QtBinDir = Resolve-UniquePath -Candidates @(
        "C:\Qt\6.9.3\mingw_64\bin",
        (Get-ChildItem -Path "C:\Qt\*\*\bin\qmake.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            ForEach-Object { Split-Path -Parent $_.FullName })
    )
}

if ([string]::IsNullOrWhiteSpace($MinGwBinDir)) {
    $MinGwBinDir = Resolve-UniquePath -Candidates @(
        "C:\Qt\Tools\mingw1310_64\bin",
        (Get-ChildItem -Path "C:\Qt\Tools\mingw*\bin\mingw32-make.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            ForEach-Object { Split-Path -Parent $_.FullName })
    )
}

if ([string]::IsNullOrWhiteSpace($QtBinDir) -or -not (Test-Path -LiteralPath (Join-Path $QtBinDir "qmake.exe"))) {
    throw "qmake.exe not found. Pass -QtBinDir explicitly (example: C:\Qt\6.9.3\mingw_64\bin)."
}

if ([string]::IsNullOrWhiteSpace($MinGwBinDir) -or -not (Test-Path -LiteralPath (Join-Path $MinGwBinDir "mingw32-make.exe"))) {
    throw "mingw32-make.exe not found. Pass -MinGwBinDir explicitly (example: C:\Qt\Tools\mingw1310_64\bin)."
}

if (-not (Test-Path -LiteralPath (Join-Path $MinGwBinDir "g++.exe"))) {
    throw "g++.exe was not found in MinGW bin directory: $MinGwBinDir"
}

Add-ToPath -Value $QtBinDir
Add-ToPath -Value $MinGwBinDir

$qtRoot = Split-Path -Parent $QtBinDir
$env:QTDIR = $qtRoot

if ($PersistUser) {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($null -eq $userPath) {
        $userPath = ""
    }
    $userEntries = @()
    if (-not [string]::IsNullOrWhiteSpace($userPath)) {
        $userEntries = $userPath -split ";"
    }
    foreach ($p in @($QtBinDir, $MinGwBinDir)) {
        if ($userEntries -notcontains $p) {
            $userEntries = @($p) + $userEntries
        }
    }
    $newUserPath = ($userEntries | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join ";"
    [Environment]::SetEnvironmentVariable("Path", $newUserPath, "User")
    [Environment]::SetEnvironmentVariable("QTDIR", $qtRoot, "User")
}

$qmakeResolved = (Get-Command qmake -ErrorAction SilentlyContinue).Source
$makeResolved = (Get-Command mingw32-make -ErrorAction SilentlyContinue).Source
$gppResolved = (Get-Command g++ -ErrorAction SilentlyContinue).Source

Write-Output "QtBinDir=$QtBinDir"
Write-Output "MinGwBinDir=$MinGwBinDir"
Write-Output "QTDIR=$env:QTDIR"
Write-Output "qmake=$qmakeResolved"
Write-Output "mingw32-make=$makeResolved"
Write-Output "g++=$gppResolved"
if ($PersistUser) {
    Write-Output "Persisted=UserEnvironment"
}
