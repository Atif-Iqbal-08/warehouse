param(
    [Parameter(Mandatory = $false)]
    [string]$SourceMarkdown = "docs/Warehouse_SKU_QR_Manager_User_Guide.md",

    [Parameter(Mandatory = $false)]
    [string]$OutputDocx = "docs/Warehouse_SKU_QR_Manager_User_Guide.docx"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SourceMarkdown)) {
    throw "Markdown source not found: $SourceMarkdown"
}

$sourcePath = (Resolve-Path -LiteralPath $SourceMarkdown).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDocx)
$outputDir = [System.IO.Path]::GetDirectoryName($outputPath)

if (-not [string]::IsNullOrWhiteSpace($outputDir) -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

function Escape-XmlText {
    param([string]$Value)
    if ($null -eq $Value) {
        return ""
    }
    return [System.Security.SecurityElement]::Escape($Value)
}

function New-ParagraphXml {
    param(
        [string]$Text,
        [string]$StyleId = "",
        [switch]$IsEmpty
    )

    if ($IsEmpty) {
        return "<w:p/>"
    }

    $escaped = Escape-XmlText $Text
    $styleXml = ""
    if (-not [string]::IsNullOrWhiteSpace($StyleId)) {
        $styleXml = "<w:pPr><w:pStyle w:val=`"$StyleId`"/></w:pPr>"
    }

    return "<w:p>$styleXml<w:r><w:t xml:space=`"preserve`">$escaped</w:t></w:r></w:p>"
}

$lines = Get-Content -LiteralPath $sourcePath
$paragraphs = New-Object System.Collections.Generic.List[string]

foreach ($rawLine in $lines) {
    $line = [string]$rawLine

    if ([string]::IsNullOrWhiteSpace($line)) {
        $paragraphs.Add((New-ParagraphXml -IsEmpty))
        continue
    }

    if ($line.StartsWith("### ")) {
        $paragraphs.Add((New-ParagraphXml -Text $line.Substring(4).Trim() -StyleId "Heading3"))
        continue
    }

    if ($line.StartsWith("## ")) {
        $paragraphs.Add((New-ParagraphXml -Text $line.Substring(3).Trim() -StyleId "Heading2"))
        continue
    }

    if ($line.StartsWith("# ")) {
        $paragraphs.Add((New-ParagraphXml -Text $line.Substring(2).Trim() -StyleId "Heading1"))
        continue
    }

    $paragraphs.Add((New-ParagraphXml -Text $line))
}

$paragraphXml = [string]::Join("", $paragraphs)

$documentXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    $paragraphXml
    <w:sectPr>
      <w:pgSz w:w="12240" w:h="15840"/>
      <w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/>
      <w:cols w:space="720"/>
      <w:docGrid w:linePitch="360"/>
    </w:sectPr>
  </w:body>
</w:document>
"@

$stylesXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:style w:type="paragraph" w:default="1" w:styleId="Normal">
    <w:name w:val="Normal"/>
    <w:qFormat/>
    <w:rPr>
      <w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/>
      <w:sz w:val="22"/>
      <w:szCs w:val="22"/>
    </w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Heading1">
    <w:name w:val="heading 1"/>
    <w:basedOn w:val="Normal"/>
    <w:next w:val="Normal"/>
    <w:qFormat/>
    <w:rPr>
      <w:b/>
      <w:sz w:val="36"/>
      <w:szCs w:val="36"/>
    </w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Heading2">
    <w:name w:val="heading 2"/>
    <w:basedOn w:val="Normal"/>
    <w:next w:val="Normal"/>
    <w:qFormat/>
    <w:rPr>
      <w:b/>
      <w:sz w:val="30"/>
      <w:szCs w:val="30"/>
    </w:rPr>
  </w:style>
  <w:style w:type="paragraph" w:styleId="Heading3">
    <w:name w:val="heading 3"/>
    <w:basedOn w:val="Normal"/>
    <w:next w:val="Normal"/>
    <w:qFormat/>
    <w:rPr>
      <w:b/>
      <w:sz w:val="26"/>
      <w:szCs w:val="26"/>
    </w:rPr>
  </w:style>
</w:styles>
'@

$contentTypesXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
  <Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
  <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
  <Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>
'@

$relsXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
  <Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>
'@

$documentRelsXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>
'@

$appXml = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"
            xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">
  <Application>Codex</Application>
</Properties>
'@

$nowUtc = (Get-Date).ToUniversalTime().ToString("s") + "Z"
$coreXml = @"
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties"
                   xmlns:dc="http://purl.org/dc/elements/1.1/"
                   xmlns:dcterms="http://purl.org/dc/terms/"
                   xmlns:dcmitype="http://purl.org/dc/dcmitype/"
                   xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <dc:title>Warehouse SKU and QR Code Manager - User Guide</dc:title>
  <dc:creator>Codex</dc:creator>
  <cp:lastModifiedBy>Codex</cp:lastModifiedBy>
  <dcterms:created xsi:type="dcterms:W3CDTF">$nowUtc</dcterms:created>
  <dcterms:modified xsi:type="dcterms:W3CDTF">$nowUtc</dcterms:modified>
</cp:coreProperties>
"@

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("docx_build_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $tempRoot "_rels") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $tempRoot "docProps") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $tempRoot "word") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $tempRoot "word\\_rels") -Force | Out-Null

Set-Content -LiteralPath (Join-Path $tempRoot "[Content_Types].xml") -Value $contentTypesXml -Encoding UTF8
Set-Content -LiteralPath (Join-Path $tempRoot "_rels\\.rels") -Value $relsXml -Encoding UTF8
Set-Content -LiteralPath (Join-Path $tempRoot "docProps\\app.xml") -Value $appXml -Encoding UTF8
Set-Content -LiteralPath (Join-Path $tempRoot "docProps\\core.xml") -Value $coreXml -Encoding UTF8
Set-Content -LiteralPath (Join-Path $tempRoot "word\\document.xml") -Value $documentXml -Encoding UTF8
Set-Content -LiteralPath (Join-Path $tempRoot "word\\styles.xml") -Value $stylesXml -Encoding UTF8
Set-Content -LiteralPath (Join-Path $tempRoot "word\\_rels\\document.xml.rels") -Value $documentRelsXml -Encoding UTF8

$zipPath = Join-Path ([System.IO.Path]::GetTempPath()) ("docx_package_" + [Guid]::NewGuid().ToString("N") + ".zip")
$finalOutputPath = $outputPath
if (Test-Path -LiteralPath $finalOutputPath) {
    try {
        Remove-Item -LiteralPath $finalOutputPath -Force -ErrorAction Stop
    } catch {
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($outputPath)
        $ext = [System.IO.Path]::GetExtension($outputPath)
        $stamp = (Get-Date).ToString("yyyyMMdd_HHmmss")
        $finalOutputPath = Join-Path $outputDir ("{0}_{1}{2}" -f $baseName, $stamp, $ext)
    }
}

Compress-Archive -Path (Join-Path $tempRoot "*") -DestinationPath $zipPath -Force
Copy-Item -LiteralPath $zipPath -Destination $finalOutputPath -Force

Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $tempRoot -Recurse -Force

Write-Output "DOCX generated: $finalOutputPath"
