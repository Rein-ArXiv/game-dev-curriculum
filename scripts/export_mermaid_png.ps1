param(
    [switch]$Force,
    [int]$Scale = 2
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$series = @(
    "base", "algorithms", "languages", "parts", "system", "network",
    "math", "physics", "ai", "graphics", "db", "engine",
    "live_service", "platform", "tools"
)
$rendered = 0
$skipped = 0

foreach ($name in $series) {
    $directory = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $directory)) { continue }
    $outputDirectory = Join-Path $directory "diagrams"

    foreach ($document in Get-ChildItem $directory -Filter *.md -File | Sort-Object Name) {
        $lines = Get-Content -LiteralPath $document.FullName
        $inside = $false
        $block = [Collections.Generic.List[string]]::new()
        $diagramIndex = 0

        foreach ($line in $lines) {
            if (-not $inside -and $line -eq '```mermaid') {
                $inside = $true
                $block.Clear()
                continue
            }
            if (-not $inside) { continue }
            if ($line -ne '```') {
                $block.Add($line)
                continue
            }

            $inside = $false
            ++$diagramIndex
            $outputName = "{0}_diagram_{1:D2}.png" -f $document.BaseName, $diagramIndex
            $output = Join-Path $outputDirectory $outputName
            if ((Test-Path -LiteralPath $output) -and -not $Force) {
                ++$skipped
                continue
            }

            New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
            $temporary = Join-Path ([IO.Path]::GetTempPath()) (
                "velog-mermaid-{0}.mmd" -f [Guid]::NewGuid().ToString("N"))
            try {
                [IO.File]::WriteAllLines(
                    $temporary, $block,
                    [Text.UTF8Encoding]::new($false))
                & npx --yes '@mermaid-js/mermaid-cli' `
                    -i $temporary -o $output -b transparent -s $Scale
                if ($LASTEXITCODE -ne 0) {
                    throw "Mermaid render failed: $($document.FullName), block $diagramIndex"
                }
                ++$rendered
                Write-Host "rendered: $name/$outputName"
            }
            finally {
                if (Test-Path -LiteralPath $temporary) {
                    Remove-Item -LiteralPath $temporary -Force
                }
            }
        }

        if ($inside) {
            throw "Unclosed Mermaid block: $($document.FullName)"
        }
    }
}

Write-Host "Mermaid PNG export complete: rendered=$rendered skipped=$skipped" `
    -ForegroundColor Green
