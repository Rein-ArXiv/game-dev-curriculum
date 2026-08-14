using System.Security.Cryptography;
using LiveService.Core;

byte[] secret = LoadSecret("LIVE_SERVICE_TOKEN_SECRET");
string serverKey = Environment.GetEnvironmentVariable("LIVE_SERVICE_SERVER_KEY")
    ?? "local-development-server-key";

var builder = WebApplication.CreateBuilder(args);
builder.Services.AddSingleton(new TokenService(secret));
builder.Services.AddSingleton<ControlPlane>();
var app = builder.Build();

app.MapPost("/login", (LoginRequest request, TokenService tokens) =>
{
    if (!Identifiers.IsValid(request.PlayerId, 64)) return Results.BadRequest();
    string token = tokens.IssueAccessToken(
        request.PlayerId, DateTimeOffset.UtcNow, TimeSpan.FromMinutes(30));
    return Results.Ok(new { accessToken = token, expiresInSeconds = 1800 });
});

app.MapPost("/servers/register", (
    HttpRequest http,
    ServerRegistration registration,
    ControlPlane control) =>
{
    if (!IsServer(http, serverKey)) return Results.Unauthorized();
    return control.RegisterServer(registration, DateTimeOffset.UtcNow)
        ? Results.Ok()
        : Results.Conflict();
});

app.MapPost("/servers/{serverId}/heartbeat", (
    HttpRequest http,
    string serverId,
    ControlPlane control) =>
{
    if (!IsServer(http, serverKey)) return Results.Unauthorized();
    return control.Heartbeat(serverId, DateTimeOffset.UtcNow)
        ? Results.Ok()
        : Results.NotFound();
});

app.MapPost("/servers/{serverId}/drain", (
    HttpRequest http,
    string serverId,
    ControlPlane control) =>
{
    if (!IsServer(http, serverKey)) return Results.Unauthorized();
    return control.DrainServer(serverId) ? Results.Ok() : Results.NotFound();
});

app.MapPost("/queue", (
    HttpRequest http,
    QueueBody body,
    TokenService tokens,
    ControlPlane control) =>
{
    if (!TryPlayer(http, tokens, out string playerId)) return Results.Unauthorized();
    try
    {
        string ticketId = control.Enqueue(
            new QueueRequest(playerId, body.Rating, body.Region),
            DateTimeOffset.UtcNow);
        return Results.Ok(new { ticketId });
    }
    catch (ArgumentException)
    {
        return Results.BadRequest();
    }
});

app.MapGet("/assignments/{ticketId}", (
    HttpRequest http,
    string ticketId,
    TokenService tokens,
    ControlPlane control) =>
{
    if (!TryPlayer(http, tokens, out string playerId)) return Results.Unauthorized();
    TicketView? ticket = control.GetTicket(ticketId, playerId, DateTimeOffset.UtcNow);
    return ticket is null ? Results.NotFound() : Results.Ok(ticket);
});

app.MapPost("/results", (
    HttpRequest http,
    MatchResultRequest result,
    ControlPlane control) =>
{
    // snippet-begin: results-endpoint
    if (!IsServer(http, serverKey)) return Results.Unauthorized();
    if (!http.Headers.TryGetValue("X-Server-Id", out var serverId) ||
        string.IsNullOrWhiteSpace(serverId)) return Results.Unauthorized();
    try
    {
        return Results.Ok(control.RecordResult(
            result, serverId.ToString(), DateTimeOffset.UtcNow));
    }
    catch (ArgumentException)
    {
        return Results.BadRequest();
    }
    catch (KeyNotFoundException)
    {
        return Results.NotFound();
    }
    catch (UnauthorizedAccessException)
    {
        return Results.Unauthorized();
    }
    catch (InvalidOperationException)
    {
        return Results.Conflict();
    }
    // snippet-end: results-endpoint
});

app.MapGet("/metrics/state", (ControlPlane control) =>
    Results.Ok(control.Metrics(DateTimeOffset.UtcNow)));

app.Run();

static byte[] LoadSecret(string name)
{
    string? configured = Environment.GetEnvironmentVariable(name);
    if (!string.IsNullOrWhiteSpace(configured))
    {
        try
        {
            byte[] decoded = Convert.FromBase64String(configured);
            if (decoded.Length >= 32) return decoded;
        }
        catch (FormatException)
        {
        }
        throw new InvalidOperationException($"{name} must be base64 for at least 32 bytes.");
    }
    return SHA256.HashData("live-service-lab-local-secret"u8);
}

static bool IsServer(HttpRequest request, string expected) =>
    request.Headers.TryGetValue("X-Server-Key", out var supplied) &&
    CryptographicOperations.FixedTimeEquals(
        System.Text.Encoding.UTF8.GetBytes(supplied.ToString()),
        System.Text.Encoding.UTF8.GetBytes(expected));

static bool TryPlayer(HttpRequest request, TokenService tokens, out string playerId)
{
    playerId = string.Empty;
    string authorization = request.Headers.Authorization.ToString();
    const string prefix = "Bearer ";
    return authorization.StartsWith(prefix, StringComparison.Ordinal) &&
        tokens.TryValidateAccessToken(
            authorization[prefix.Length..], DateTimeOffset.UtcNow, out playerId);
}

public sealed record LoginRequest(string PlayerId);
public sealed record QueueBody(int Rating, string Region);
