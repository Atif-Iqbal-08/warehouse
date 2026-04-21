param(
    [ValidateSet("patch", "minor", "major")]
    [string]$Bump = "patch",
    [string]$Version = "",
    [string]$BuildDir = "build-qmake-release",
    [string]$OutputDir = "dist-release",
    [ValidateSet("Release", "Debug")]
    [string]$BuildConfig = "Release",
    [bool]$CleanBuild = $true,
    [string]$QtBinDir = "",
    [switch]$SkipInstallerBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Split-Path -Parent $MyInvocation.MyCommand.Path)).Path
$IssPath = Join-Path $RepoRoot "installer\warehouse_sku_generator.iss"
$InstallerBuildScriptPath = Join-Path $RepoRoot "installer\build-installer.ps1"
$ReadmePath = Join-Path $RepoRoot "README.md"
$ChangelogPath = Join-Path $RepoRoot "CHANGELOG.md"

function Assert-SemVer {
    param([string]$Value, [string]$FieldName)

    if ($Value -notmatch '^\d+\.\d+\.\d+$') {
        throw "$FieldName must be in MAJOR.MINOR.PATCH format (example: 1.2.3)."
    }
}

function Get-CurrentVersionFromIss {
    param([string]$Path)

    $text = Get-Content -Raw $Path
    $match = [regex]::Match($text, '(?m)^#define\s+AppVersion\s+"(?<ver>\d+\.\d+\.\d+)"\s*$')
    if (-not $match.Success) {
        throw "Could not find AppVersion in $Path"
    }
    return $match.Groups["ver"].Value
}

function Get-BumpedVersion {
    param(
        [string]$CurrentVersion,
        [string]$Mode
    )

    Assert-SemVer -Value $CurrentVersion -FieldName "Current version"
    $parts = $CurrentVersion.Split(".")
    $major = [int]$parts[0]
    $minor = [int]$parts[1]
    $patch = [int]$parts[2]

    switch ($Mode) {
        "major" {
            $major += 1
            $minor = 0
            $patch = 0
        }
        "minor" {
            $minor += 1
            $patch = 0
        }
        default {
            $patch += 1
        }
    }

    return "$major.$minor.$patch"
}

function Trim-EdgeBlankLines {
    param([string[]]$Lines)

    if (-not $Lines -or $Lines.Count -eq 0) {
        return @()
    }

    $start = 0
    $end = $Lines.Count - 1

    while ($start -le $end -and [string]::IsNullOrWhiteSpace($Lines[$start])) {
        $start += 1
    }
    while ($end -ge $start -and [string]::IsNullOrWhiteSpace($Lines[$end])) {
        $end -= 1
    }

    if ($start -gt $end) {
        return @()
    }

    return $Lines[$start..$end]
}

function Write-TextFilePreserveNewlineStyle {
    param(
        [string]$Path,
        [string]$Text,
        [string]$Newline
    )

    $normalized = $Text -replace "`r?`n", $Newline
    if (-not $normalized.EndsWith($Newline)) {
        $normalized += $Newline
    }
    [System.IO.File]::WriteAllText($Path, $normalized)
}

function Update-VersionFiles {
    param(
        [string]$NewVersion,
        [string]$ReleaseDate
    )

    $issOriginal = Get-Content -Raw $IssPath
    $issNew = [regex]::Replace(
        $issOriginal,
        '(?m)^#define\s+AppVersion\s+"[^"]+"\s*$',
        "#define AppVersion ""$NewVersion"""
    )
    if ($issNew -eq $issOriginal) {
        throw "Failed to update AppVersion in $IssPath"
    }
    $issNl = if ($issOriginal.Contains("`r`n")) { "`r`n" } else { "`n" }
    Write-TextFilePreserveNewlineStyle -Path $IssPath -Text $issNew -Newline $issNl

    $buildOriginal = Get-Content -Raw $InstallerBuildScriptPath
    $buildNew = [regex]::Replace(
        $buildOriginal,
        '(?m)^(\s*\[string\]\$AppVersion\s*=\s*)"[^"]+"(\s*,?\s*)$',
        ('$1"' + $NewVersion + '"$2')
    )
    if ($buildNew -eq $buildOriginal) {
        throw "Failed to update AppVersion default in $InstallerBuildScriptPath"
    }
    $buildNl = if ($buildOriginal.Contains("`r`n")) { "`r`n" } else { "`n" }
    Write-TextFilePreserveNewlineStyle -Path $InstallerBuildScriptPath -Text $buildNew -Newline $buildNl

    if (Test-Path $ReadmePath) {
        $readmeOriginal = Get-Content -Raw $ReadmePath
        $readmeNew = [regex]::Replace(
            $readmeOriginal,
            '(?m)^- Current application version:\s+`[^`]+`$',
            ('- Current application version: `' + $NewVersion + '`')
        )
        $readmeNew = [regex]::Replace(
            $readmeNew,
            '(?m)^- Latest release baseline in repo history:\s+`[^`]+`\s+on\s+`[^`]+`$',
            ('- Latest release baseline in repo history: `' + $NewVersion + '` on `' + $ReleaseDate + '`')
        )
        $readmeNl = if ($readmeOriginal.Contains("`r`n")) { "`r`n" } else { "`n" }
        Write-TextFilePreserveNewlineStyle -Path $ReadmePath -Text $readmeNew -Newline $readmeNl
    }
}

function Update-Changelog {
    param(
        [string]$NewVersion,
        [string]$ReleaseDate
    )

    if (-not (Test-Path $ChangelogPath)) {
        throw "CHANGELOG.md not found at $ChangelogPath"
    }

    $raw = Get-Content -Raw $ChangelogPath
    $newline = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = $raw -split "`r?`n"

    $unreleasedIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -eq "## [Unreleased]") {
            $unreleasedIdx = $i
            break
        }
    }
    if ($unreleasedIdx -lt 0) {
        throw "Could not find '## [Unreleased]' in CHANGELOG.md"
    }

    $nextHeadingIdx = -1
    for ($i = $unreleasedIdx + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## \[[^\]]+\]') {
            $nextHeadingIdx = $i
            break
        }
    }

    $unreleasedBody = @()
    if ($nextHeadingIdx -gt ($unreleasedIdx + 1)) {
        $unreleasedBody = $lines[($unreleasedIdx + 1)..($nextHeadingIdx - 1)]
    } elseif ($nextHeadingIdx -lt 0 -and $unreleasedIdx + 1 -lt $lines.Count) {
        $unreleasedBody = $lines[($unreleasedIdx + 1)..($lines.Count - 1)]
    }
    $unreleasedBody = Trim-EdgeBlankLines -Lines $unreleasedBody
    if ($unreleasedBody.Count -eq 0) {
        $unreleasedBody = @(
            "### Changed",
            "- No user-facing changes recorded."
        )
    }

    $before = $lines[0..$unreleasedIdx]
    $after = @()
    if ($nextHeadingIdx -ge 0) {
        $after = $lines[$nextHeadingIdx..($lines.Count - 1)]
    }

    $updated = @()
    $updated += $before
    $updated += ""
    $updated += "## [$NewVersion] - $ReleaseDate"
    $updated += ""
    $updated += $unreleasedBody
    $updated += ""
    if ($after.Count -gt 0) {
        $updated += $after
    }

    $newText = [string]::Join($newline, $updated)
    Write-TextFilePreserveNewlineStyle -Path $ChangelogPath -Text $newText -Newline $newline
}

if (-not (Test-Path $IssPath)) {
    throw "Installer script not found: $IssPath"
}
if (-not (Test-Path $InstallerBuildScriptPath)) {
    throw "Installer build script not found: $InstallerBuildScriptPath"
}

$currentVersion = Get-CurrentVersionFromIss -Path $IssPath
$targetVersion = $Version
if ([string]::IsNullOrWhiteSpace($targetVersion)) {
    $targetVersion = Get-BumpedVersion -CurrentVersion $currentVersion -Mode $Bump
} else {
    Assert-SemVer -Value $targetVersion -FieldName "Version"
}

if ($targetVersion -eq $currentVersion) {
    throw "Target version ($targetVersion) is the same as current version. Pass -Version with a new value or use a different -Bump mode."
}

$releaseDate = Get-Date -Format "yyyy-MM-dd"

Write-Host "Current version : $currentVersion"
Write-Host "Target version  : $targetVersion"
Write-Host "Release date    : $releaseDate"

Update-VersionFiles -NewVersion $targetVersion -ReleaseDate $releaseDate
Update-Changelog -NewVersion $targetVersion -ReleaseDate $releaseDate

if (-not $SkipInstallerBuild) {
    $installerArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $InstallerBuildScriptPath,
        "-BuildDir", $BuildDir,
        "-OutputDir", $OutputDir,
        "-BuildConfig", $BuildConfig,
        "-AppVersion", $targetVersion
    )

    if ($CleanBuild) {
        $installerArgs += "-CleanBuild"
    }
    if (-not [string]::IsNullOrWhiteSpace($QtBinDir)) {
        $installerArgs += @("-QtBinDir", $QtBinDir)
    }

    & powershell @installerArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Installer build failed."
    }
}

Write-Host "Release preparation completed."
Write-Host "Updated version: $targetVersion"
if (-not $SkipInstallerBuild) {
    Write-Host "Installer output directory: $OutputDir"
}
