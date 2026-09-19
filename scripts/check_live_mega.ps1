param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$MegaBuild = "",
    [string]$ApiPort = "",
    [string]$GamePort = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $MegaBuild) { $MegaBuild = Join-Path $root "projects/mega/build" }
elseif (-not [System.IO.Path]::IsPathRooted($MegaBuild)) {
    $MegaBuild = Join-Path $root $MegaBuild
}
$MegaBuild = [System.IO.Path]::GetFullPath($MegaBuild)

function Get-FreeTcpPort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Get-FreeUdpPort {
    $client = [Net.Sockets.UdpClient]::new(0)
    try { return ([Net.IPEndPoint]$client.Client.LocalEndPoint).Port }
    finally { $client.Dispose() }
}

if (-not $ApiPort) { $ApiPort = [string](Get-FreeTcpPort) }
if (-not $GamePort) { $GamePort = [string](Get-FreeUdpPort) }

$apiDll = Join-Path $root "projects/mini/live_service_lab/src/LiveService.Api/bin/$Configuration/net10.0/LiveService.Api.dll"
$megaServer = Join-Path $MegaBuild "bin/$Configuration/mega_defense_server.exe"
$probe = Join-Path $MegaBuild "bin/$Configuration/mega_live_admission_probe.exe"
if (-not (Test-Path -LiteralPath $apiDll)) { throw "Missing API build: $apiDll" }
if (-not (Test-Path -LiteralPath $megaServer)) { throw "Missing Mega server build: $megaServer" }
if (-not (Test-Path -LiteralPath $probe)) { throw "Missing Mega admission probe build: $probe" }

$secret = [Convert]::ToBase64String(
    [Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$serverKey = [Guid]::NewGuid().ToString("N")
$serverId = "mega-cross"
$baseUrl = "http://127.0.0.1:$ApiPort"
$environment = @{
    LIVE_SERVICE_TOKEN_SECRET = $secret
    LIVE_SERVICE_SERVER_KEY = $serverKey
}

$api = Start-Process dotnet `
    -ArgumentList @("`"$apiDll`"", "--urls", $baseUrl) `
    -Environment $environment -WindowStyle Hidden -PassThru
$server = $null

try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        Start-Sleep -Milliseconds 100
        try {
            Invoke-RestMethod "$baseUrl/metrics/state" -TimeoutSec 2 | Out-Null
            $ready = $true
            break
        }
        catch {}
    }
    if (-not $ready) { throw "Live Service API did not become ready." }

    $serverHeaders = @{ "X-Server-Key" = $serverKey }
    $registration = @{
        serverId = $serverId
        region = "kr"
        endpoint = "127.0.0.1:$GamePort"
        capacity = 1
    } | ConvertTo-Json
    Invoke-RestMethod "$baseUrl/servers/register" -Method Post `
        -Headers $serverHeaders -ContentType "application/json" `
        -Body $registration | Out-Null

    $loginA = Invoke-RestMethod "$baseUrl/login" -Method Post `
        -ContentType "application/json" `
        -Body (@{ playerId = "cross-a" } | ConvertTo-Json)
    $loginB = Invoke-RestMethod "$baseUrl/login" -Method Post `
        -ContentType "application/json" `
        -Body (@{ playerId = "cross-b" } | ConvertTo-Json)
    $ticketA = Invoke-RestMethod "$baseUrl/queue" -Method Post `
        -Headers @{ Authorization = "Bearer $($loginA.accessToken)" } `
        -ContentType "application/json" `
        -Body (@{ rating = 1000; region = "kr" } | ConvertTo-Json)
    $ticketB = Invoke-RestMethod "$baseUrl/queue" -Method Post `
        -Headers @{ Authorization = "Bearer $($loginB.accessToken)" } `
        -ContentType "application/json" `
        -Body (@{ rating = 1010; region = "kr" } | ConvertTo-Json)
    $assignmentA = Invoke-RestMethod "$baseUrl/assignments/$($ticketA.ticketId)" `
        -Headers @{ Authorization = "Bearer $($loginA.accessToken)" }
    $assignmentB = Invoke-RestMethod "$baseUrl/assignments/$($ticketB.ticketId)" `
        -Headers @{ Authorization = "Bearer $($loginB.accessToken)" }
    if (-not $assignmentA.matchId -or $assignmentA.matchId -ne $assignmentB.matchId -or
        -not $assignmentA.gameToken -or -not $assignmentB.gameToken) {
        throw "Players were not assigned to the same match."
    }

    $server = Start-Process $megaServer `
        -ArgumentList @("--port", $GamePort, "--server-id", $serverId,
                        "--run-seconds", "15", "--match-id", $assignmentA.matchId,
                        "--player-id", "cross-a", "--player-id", "cross-b") `
        -Environment @{ LIVE_SERVICE_TOKEN_SECRET = $secret } `
        -WindowStyle Hidden -PassThru

    # This local controller owns both queued identities and the server launch.
    # The probe uses the product codec/content cooker and retries until a bounded deadline.
    @($assignmentA.gameToken, $assignmentB.gameToken) | & $probe $GamePort
    if ($LASTEXITCODE -ne 0) {
        throw "Mega admission probe failed (exit $LASTEXITCODE)."
    }

    Write-Host "Live Service -> Mega token admission smoke passed." -ForegroundColor Green
}
finally {
    if ($server) {
        if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
        $server.Dispose()
    }
    if (-not $api.HasExited) { Stop-Process -Id $api.Id -Force }
    $api.Dispose()
}
