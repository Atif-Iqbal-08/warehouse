param(
    [Parameter(Mandatory = $false)]
    [string]$SourceMarkdown = "docs/Warehouse_SKU_QR_Manager_User_Guide.md",

    [Parameter(Mandatory = $false)]
    [string]$OutputPdf = "docs/Warehouse_SKU_QR_Manager_User_Guide.pdf",

    [Parameter(Mandatory = $false)]
    [int]$FontSize = 11
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SourceMarkdown)) {
    throw "Markdown source not found: $SourceMarkdown"
}

$sourcePath = (Resolve-Path -LiteralPath $SourceMarkdown).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputPdf)
$outputDir = [System.IO.Path]::GetDirectoryName($outputPath)

if (-not [string]::IsNullOrWhiteSpace($outputDir) -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

function Escape-PdfText {
    param([string]$Value)

    if ($null -eq $Value) {
        return ""
    }

    $escaped = $Value.Replace("\", "\\")
    $escaped = $escaped.Replace("(", "\(")
    $escaped = $escaped.Replace(")", "\)")
    return $escaped
}

function Add-WrappedLine {
    param(
        [System.Collections.Generic.List[string]]$Target,
        [string]$Text,
        [int]$MaxChars
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        $Target.Add("")
        return
    }

    $remaining = $Text.TrimEnd()
    while ($remaining.Length -gt $MaxChars) {
        $searchStart = [Math]::Min($MaxChars, $remaining.Length - 1)
        $breakIndex = $remaining.LastIndexOf(" ", $searchStart)
        if ($breakIndex -lt 20) {
            $breakIndex = $MaxChars
        }

        $Target.Add($remaining.Substring(0, $breakIndex).TrimEnd())
        $remaining = $remaining.Substring($breakIndex).TrimStart()
    }

    $Target.Add($remaining)
}

$rawLines = Get-Content -LiteralPath $sourcePath
$formattedLines = New-Object System.Collections.Generic.List[string]

foreach ($rawLine in $rawLines) {
    $line = [string]$rawLine
    $trimmed = $line.Trim()

    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        $formattedLines.Add("")
        continue
    }

    if ($trimmed.StartsWith("### ")) {
        $formattedLines.Add("")
        Add-WrappedLine -Target $formattedLines -Text ($trimmed.Substring(4).Trim()) -MaxChars 92
        $formattedLines.Add("")
        continue
    }

    if ($trimmed.StartsWith("## ")) {
        $formattedLines.Add("")
        Add-WrappedLine -Target $formattedLines -Text ($trimmed.Substring(3).Trim()) -MaxChars 92
        $formattedLines.Add("")
        continue
    }

    if ($trimmed.StartsWith("# ")) {
        $formattedLines.Add("")
        Add-WrappedLine -Target $formattedLines -Text ($trimmed.Substring(2).Trim().ToUpperInvariant()) -MaxChars 92
        $formattedLines.Add("")
        continue
    }

    Add-WrappedLine -Target $formattedLines -Text $trimmed -MaxChars 92
}

$pageWidth = 595
$pageHeight = 842
$marginLeft = 50
$marginTop = 50
$marginBottom = 50
$lineHeight = 14
$startY = $pageHeight - $marginTop
$linesPerPage = [Math]::Max(1, [int][Math]::Floor(($pageHeight - $marginTop - $marginBottom) / $lineHeight))

$pages = New-Object System.Collections.Generic.List[System.Collections.Generic.List[string]]
$currentPage = New-Object System.Collections.Generic.List[string]

foreach ($line in $formattedLines) {
    if ($currentPage.Count -ge $linesPerPage) {
        $pages.Add($currentPage)
        $currentPage = New-Object System.Collections.Generic.List[string]
    }
    $currentPage.Add($line)
}

if ($currentPage.Count -gt 0) {
    $pages.Add($currentPage)
}

if ($pages.Count -eq 0) {
    $emptyPage = New-Object System.Collections.Generic.List[string]
    $emptyPage.Add("")
    $pages.Add($emptyPage)
}

function New-ContentStream {
    param([System.Collections.Generic.List[string]]$Lines)

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append("BT`n")
    [void]$builder.AppendFormat("/F1 {0} Tf`n", $FontSize)
    [void]$builder.AppendFormat("{0} TL`n", $lineHeight)
    [void]$builder.AppendFormat("{0} {1} Td`n", $marginLeft, $startY)

    $first = $true
    foreach ($line in $Lines) {
        if (-not $first) {
            [void]$builder.Append("T*`n")
        }

        [void]$builder.AppendFormat("({0}) Tj`n", (Escape-PdfText -Value $line))
        $first = $false
    }

    [void]$builder.Append("ET")
    return $builder.ToString()
}

$pageCount = $pages.Count
$totalObjects = 3 + ($pageCount * 2)
$objects = New-Object string[] ($totalObjects + 1)
$pageObjectNumbers = New-Object System.Collections.Generic.List[int]
$contentObjectNumbers = New-Object System.Collections.Generic.List[int]

for ($i = 0; $i -lt $pageCount; $i++) {
    $pageObjectNumbers.Add(4 + ($i * 2))
    $contentObjectNumbers.Add(5 + ($i * 2))
}

$kids = ($pageObjectNumbers | ForEach-Object { "{0} 0 R" -f $_ }) -join " "

$objects[1] = "<< /Type /Catalog /Pages 2 0 R >>"
$objects[2] = "<< /Type /Pages /Count $pageCount /Kids [ $kids ] >>"
$objects[3] = "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>"

for ($i = 0; $i -lt $pageCount; $i++) {
    $pageObjectNumber = $pageObjectNumbers[$i]
    $contentObjectNumber = $contentObjectNumbers[$i]
    $stream = New-ContentStream -Lines $pages[$i]
    $streamLength = [System.Text.Encoding]::ASCII.GetByteCount($stream)

    $objects[$pageObjectNumber] =
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 $pageWidth $pageHeight] " +
        "/Resources << /Font << /F1 3 0 R >> >> /Contents $contentObjectNumber 0 R >>"

    $objects[$contentObjectNumber] =
        "<< /Length $streamLength >>`nstream`n$stream`nendstream"
}

$pdfChunks = New-Object System.Collections.Generic.List[string]
$header = "%PDF-1.4`n"
$pdfChunks.Add($header)

$offsets = New-Object System.Collections.Generic.List[int]
$currentOffset = [System.Text.Encoding]::ASCII.GetByteCount($header)

for ($objNum = 1; $objNum -le $totalObjects; $objNum++) {
    $offsets.Add($currentOffset)
    $objectChunk = "{0} 0 obj`n{1}`nendobj`n" -f $objNum, $objects[$objNum]
    $pdfChunks.Add($objectChunk)
    $currentOffset += [System.Text.Encoding]::ASCII.GetByteCount($objectChunk)
}

$xrefOffset = $currentOffset
$xrefBuilder = New-Object System.Text.StringBuilder
[void]$xrefBuilder.Append("xref`n")
[void]$xrefBuilder.AppendFormat("0 {0}`n", $totalObjects + 1)
[void]$xrefBuilder.Append("0000000000 65535 f `n")

foreach ($offset in $offsets) {
    [void]$xrefBuilder.AppendFormat("{0:0000000000} 00000 n `n", $offset)
}

$pdfChunks.Add($xrefBuilder.ToString())
$pdfChunks.Add("trailer`n<< /Size $($totalObjects + 1) /Root 1 0 R >>`nstartxref`n$xrefOffset`n%%EOF`n")

$pdfContent = [string]::Concat($pdfChunks.ToArray())
[System.IO.File]::WriteAllBytes($outputPath, [System.Text.Encoding]::ASCII.GetBytes($pdfContent))

Write-Output "PDF generated: $outputPath"
