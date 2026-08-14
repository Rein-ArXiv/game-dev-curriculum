param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [switch]$SkipConfigure,
    [string]$BuildRoot = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$miniRoot = Join-Path $root "projects/mini"

if ($BuildRoot) {
    if (-not [System.IO.Path]::IsPathRooted($BuildRoot)) {
        $BuildRoot = Join-Path $root $BuildRoot
    }
    $BuildRoot = [System.IO.Path]::GetFullPath($BuildRoot)
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
}

function Get-BuildDirectory([string]$Source, [string]$Name) {
    if ($BuildRoot) {
        return Join-Path $BuildRoot $Name
    }
    return Join-Path $Source "build"
}

& python -X utf8 (Join-Path $PSScriptRoot "check_project_docs.py")
if ($LASTEXITCODE -ne 0) {
    throw "Project document checks failed."
}

# check_project_docs.py는 mini의 step 문서만 순회한다. mega 문서와 README의
# 스니펫 앵커까지 덮으려면 전체 대상으로 한 번 더 돌린다.
& python -X utf8 (Join-Path $PSScriptRoot "check_snippets.py")
if ($LASTEXITCODE -ne 0) {
    throw "Snippet drift checks failed."
}

$windowsLabs = @(
    "parts_perf_lab",
    "languages_lab",
    "math_lab",
    "physics_lab",
    "ai_lab",
    "graphics_gl_lab",
    "graphics_dx_lab",
    "graphics_vk_lab",
    "game_network_lab",
    "db_lab"
)

$results = @()

foreach ($name in $windowsLabs) {
    $source = Join-Path $miniRoot $name
    $build = Get-BuildDirectory $source $name

    Write-Host "`n=== $name ===" -ForegroundColor Cyan

    if (-not $SkipConfigure) {
        & cmake -S $source -B $build
        if ($LASTEXITCODE -ne 0) {
            throw "Configure failed: $name"
        }
    }

    & cmake --build $build --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $name"
    }

    $ctestFile = Join-Path $build "CTestTestfile.cmake"
    if (Test-Path $ctestFile) {
        & ctest --test-dir $build -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed: $name"
        }
        $testStatus = "CTest passed"
    }
    else {
        $testStatus = "No CTest tests"
    }

    $results += [PSCustomObject]@{
        Lab = $name
        Build = "passed"
        Tests = $testStatus
    }
}

$liveServiceProject = Join-Path $miniRoot "live_service_lab/src/LiveService.Api/LiveService.Api.csproj"
$liveServiceTests = Join-Path $miniRoot "live_service_lab/tests/LiveService.Tests/LiveService.Tests.csproj"

Write-Host "`n=== live_service_lab ===" -ForegroundColor Cyan
& dotnet build $liveServiceProject -c $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Build failed: live_service_lab"
}
& dotnet run --project $liveServiceTests -c $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Tests failed: live_service_lab"
}
$results += [PSCustomObject]@{
    Lab = "live_service_lab"
    Build = "passed"
    Tests = "Core contract tests passed"
}

$megaSource = Join-Path $root "projects/mega"
$megaBuild = Get-BuildDirectory $megaSource "mega"

Write-Host "`n=== mega ===" -ForegroundColor Cyan
if (-not $SkipConfigure) {
    & cmake -S $megaSource -B $megaBuild
    if ($LASTEXITCODE -ne 0) {
        throw "Configure failed: mega"
    }
}

& cmake --build $megaBuild --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "Build failed: mega"
}

& ctest --test-dir $megaBuild -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "Tests failed: mega"
}

& (Join-Path $PSScriptRoot "check_live_mega.ps1") `
    -Configuration $Configuration -MegaBuild $megaBuild
if ($LASTEXITCODE -ne 0) {
    throw "Live Service -> Mega admission smoke failed."
}

$results += [PSCustomObject]@{
    Lab = "mega"
    Build = "passed"
    Tests = "CTest + live admission smoke passed"
}

Write-Host "`n=== Summary ===" -ForegroundColor Green
$results | Format-Table -AutoSize
Write-Host "`nsystem_kernel_lab is Linux-only and is not verified by this Windows script." -ForegroundColor Yellow
