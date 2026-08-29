# 배경색 기본값이 white 인 이유 — 저장소에 이미 있는 PNG 들이 그렇게 만들어졌다.
# 이 스크립트는 원래 `-b transparent` 를 넘겼지만, mermaid-cli 11.x 로 그렇게 렌더하면
# RGBA(알파 채널 있음) PNG 가 나온다. 반면 저장소의 기존 PNG 는 전부 RGB(알파 없음) +
# 흰 배경이다. 실제로 대조해 보면 차이가 분명하다.
#
#   ai/diagrams/part01_fsm_hfsm_diagram_03.png  RGB  1568x206  18484 bytes
#   `-b white -s 2` 로 재렌더                    RGB  1568x206  18484 bytes  ← 바이트 동일
#   `-b transparent -s 2` 로 재렌더              RGBA 1568x206  20540 bytes  ← 불일치
#
# `-b white` 는 기존 PNG 를 sha256 까지 똑같이 재현한다. 투명 배경으로 새 그림만 뽑으면
# mermaid 기본 테마의 어두운 글자가 velog 다크 테마에서 읽히지 않고, 같은 시리즈 안에서
# 배경이 섞인다. 투명 배경이 필요하면 `-Background transparent` 로 넘긴다.
param(
    [switch]$Force,
    [int]$Scale = 2,
    [string]$Background = "white"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$series = @(
    "base", "algorithms", "languages", "parts", "system", "network",
    "math", "physics", "ai", "graphics", "db", "engine",
    "live_service", "platform", "tools"
)
# 프로젝트 글도 같은 규약(문서 옆 diagrams/{stem}_diagram_NN.png)을 쓴다.
# 랩이 늘어나도 목록을 손보지 않도록 디렉터리에서 찾아 붙인다.
$projects = Join-Path $root "projects"
if (Test-Path -LiteralPath $projects) {
    $series += Get-ChildItem (Join-Path $projects "mini") -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path (Join-Path "projects/mini" $_.Name) "articles" } |
        Where-Object { Test-Path -LiteralPath (Join-Path $root $_) }
    if (Test-Path -LiteralPath (Join-Path $projects "mega/articles")) {
        $series += "projects/mega/articles"
    }
}
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
                    -i $temporary -o $output -b $Background -s $Scale
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
