namespace LiveService.Core;

public enum ServerLifecycle
{
    Ready,
    Draining,
    Unhealthy,
}

public enum TicketStatus
{
    Waiting,
    Assigned,
    Cancelled,
    Completed,
}

public sealed record ServerRegistration(
    string ServerId,
    string Region,
    string Endpoint,
    int Capacity);

public sealed record QueueRequest(
    string PlayerId,
    int Rating,
    string Region,
    int PartySize = 1);

public sealed record TicketView(
    string TicketId,
    TicketStatus Status,
    string? MatchId,
    string? Endpoint,
    string? GameToken);

public sealed record MatchResultRequest(
    string ResultId,
    string MatchId,
    IReadOnlyDictionary<string, int> Scores);

public sealed record ResultReceipt(
    string ResultId,
    bool Duplicate,
    DateTimeOffset RecordedAt);

public sealed record GameAdmission(
    string PlayerId,
    string MatchId,
    string ServerId,
    string TokenId,
    DateTimeOffset ExpiresAt);

public sealed record ControlPlaneMetrics(
    int ReadyServers,
    int DrainingServers,
    int WaitingTickets,
    int ActiveMatches,
    int RecordedResults);

