namespace LiveService.Core;

public sealed class ControlPlane
{
    private static readonly TimeSpan ServerHealthTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan GameTokenLifetime = TimeSpan.FromSeconds(45);
    private readonly object gate = new();
    private readonly TokenService tokens;
    private readonly Dictionary<string, ServerState> servers = new(StringComparer.Ordinal);
    private readonly Dictionary<string, TicketState> tickets = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> playerTickets = new(StringComparer.Ordinal);
    private readonly Dictionary<string, MatchState> matches = new(StringComparer.Ordinal);
    private readonly Dictionary<string, RecordedResult> results = new(StringComparer.Ordinal);

    public ControlPlane(TokenService tokens)
    {
        this.tokens = tokens;
    }

    public bool RegisterServer(ServerRegistration registration, DateTimeOffset now)
    {
        if (!Identifiers.IsValid(registration.ServerId, 64) ||
            !Identifiers.IsValid(registration.Region, 32) ||
            string.IsNullOrWhiteSpace(registration.Endpoint) ||
            registration.Endpoint.Length > 255 ||
            registration.Capacity <= 0) return false;

        lock (gate)
        {
            if (servers.TryGetValue(registration.ServerId, out ServerState? existing) &&
                existing.ActiveMatches > 0 &&
                (existing.Region != registration.Region ||
                 existing.Endpoint != registration.Endpoint))
            {
                return false;
            }
            servers[registration.ServerId] = new ServerState(
                registration.ServerId,
                registration.Region,
                registration.Endpoint,
                registration.Capacity,
                existing?.ActiveMatches ?? 0,
                ServerLifecycle.Ready,
                now);
            TryMatch(now);
            return true;
        }
    }

    public bool Heartbeat(string serverId, DateTimeOffset now)
    {
        lock (gate)
        {
            if (!servers.TryGetValue(serverId, out ServerState? server)) return false;
            server.LastHeartbeat = now;
            return true;
        }
    }

    public bool DrainServer(string serverId)
    {
        lock (gate)
        {
            if (!servers.TryGetValue(serverId, out ServerState? server)) return false;
            server.Lifecycle = ServerLifecycle.Draining;
            return true;
        }
    }

    public string Enqueue(QueueRequest request, DateTimeOffset now)
    {
        if (!Identifiers.IsValid(request.PlayerId, 64) ||
            !Identifiers.IsValid(request.Region, 32) ||
            request.PartySize != 1 || request.Rating is < 0 or > 10_000)
        {
            throw new ArgumentException("Invalid queue request.", nameof(request));
        }

        lock (gate)
        {
            MarkUnhealthyServers(now);
            if (playerTickets.TryGetValue(request.PlayerId, out string? existingId) &&
                tickets.TryGetValue(existingId, out TicketState? existing) &&
                existing.Status is TicketStatus.Waiting or TicketStatus.Assigned)
            {
                return existing.Id;
            }

            string ticketId = Guid.NewGuid().ToString("N");
            tickets[ticketId] = new TicketState(
                ticketId, request.PlayerId, request.Rating, request.Region, now);
            playerTickets[request.PlayerId] = ticketId;
            TryMatch(now);
            return ticketId;
        }
    }

    public TicketView? GetTicket(string ticketId, string playerId, DateTimeOffset now)
    {
        lock (gate)
        {
            MarkUnhealthyServers(now);
            TryMatch(now);
            if (!tickets.TryGetValue(ticketId, out TicketState? ticket) ||
                !string.Equals(ticket.PlayerId, playerId, StringComparison.Ordinal)) return null;
            return new TicketView(
                ticket.Id,
                ticket.Status,
                ticket.MatchId,
                ticket.Endpoint,
                ticket.GameToken);
        }
    }

    public ResultReceipt RecordResult(
        MatchResultRequest request,
        string serverId,
        DateTimeOffset now)
    {
        if (!Identifiers.IsValid(request.ResultId, 128) ||
            !Identifiers.IsValid(request.MatchId, 64) ||
            request.Scores is null || request.Scores.Count == 0)
        {
            throw new ArgumentException("Result ID and match ID are required.", nameof(request));
        }

        lock (gate)
        {
            string fingerprint = ResultFingerprint(request);
            if (results.TryGetValue(request.ResultId, out RecordedResult? existing))
            {
                if (!string.Equals(existing.ServerId, serverId, StringComparison.Ordinal))
                {
                    throw new UnauthorizedAccessException(
                        "Server does not own this result receipt.");
                }
                if (!string.Equals(existing.Fingerprint, fingerprint, StringComparison.Ordinal))
                {
                    throw new InvalidOperationException(
                        "Idempotency key was reused with a different payload.");
                }
                return existing.Receipt with { Duplicate = true };
            }
            if (!matches.TryGetValue(request.MatchId, out MatchState? match))
            {
                throw new KeyNotFoundException("Unknown match.");
            }
            if (!string.Equals(match.ServerId, serverId, StringComparison.Ordinal))
            {
                throw new UnauthorizedAccessException("Server does not own this match.");
            }
            if (match.Completed)
            {
                throw new InvalidOperationException("Match already has a result.");
            }
            if (!match.Players.SetEquals(request.Scores.Keys))
            {
                throw new InvalidOperationException("Result players do not match allocation.");
            }

            var receipt = new ResultReceipt(request.ResultId, false, now);
            results[request.ResultId] = new RecordedResult(
                receipt, fingerprint, serverId);
            match.Completed = true;
            CompleteTickets(request.MatchId);
            if (servers.TryGetValue(match.ServerId, out ServerState? server) &&
                server.ActiveMatches > 0)
            {
                server.ActiveMatches--;
            }
            return receipt;
        }
    }

    public ControlPlaneMetrics Metrics(DateTimeOffset now)
    {
        lock (gate)
        {
            MarkUnhealthyServers(now);
            return new ControlPlaneMetrics(
                servers.Values.Count(server => server.Lifecycle == ServerLifecycle.Ready),
                servers.Values.Count(server => server.Lifecycle == ServerLifecycle.Draining),
                tickets.Values.Count(ticket => ticket.Status == TicketStatus.Waiting),
                matches.Values.Count(match => !match.Completed),
                results.Count);
        }
    }

    private void TryMatch(DateTimeOffset now)
    {
        List<TicketState> waiting = tickets.Values
            .Where(ticket => ticket.Status == TicketStatus.Waiting)
            .OrderBy(ticket => ticket.EnqueuedAt)
            .ToList();

        for (int i = 0; i < waiting.Count; ++i)
        {
            TicketState first = waiting[i];
            if (first.Status != TicketStatus.Waiting) continue;
            for (int j = i + 1; j < waiting.Count; ++j)
            {
                TicketState second = waiting[j];
                if (second.Status != TicketStatus.Waiting ||
                    !string.Equals(first.Region, second.Region, StringComparison.Ordinal)) continue;

                double waitedSeconds = Math.Max(0, Math.Min(
                    (now - first.EnqueuedAt).TotalSeconds,
                    (now - second.EnqueuedAt).TotalSeconds));
                int allowedGap = 100 + Math.Min(400, (int)(waitedSeconds * 10));
                if (Math.Abs(first.Rating - second.Rating) > allowedGap) continue;

                ServerState? server = servers.Values
                    .Where(candidate =>
                        candidate.Lifecycle == ServerLifecycle.Ready &&
                        candidate.Region == first.Region &&
                        now - candidate.LastHeartbeat <= ServerHealthTimeout &&
                        candidate.ActiveMatches < candidate.Capacity)
                    .OrderBy(candidate => candidate.ActiveMatches)
                    .ThenBy(candidate => candidate.Id, StringComparer.Ordinal)
                    .FirstOrDefault();
                if (server is null) break;

                string matchId = Guid.NewGuid().ToString("N");
                server.ActiveMatches++;
                matches[matchId] = new MatchState(
                    matchId, server.Id, [first.PlayerId, second.PlayerId]);
                Assign(first, matchId, server, now);
                Assign(second, matchId, server, now);
                break;
            }
        }
    }

    private void Assign(
        TicketState ticket,
        string matchId,
        ServerState server,
        DateTimeOffset now)
    {
        var admission = new GameAdmission(
            ticket.PlayerId,
            matchId,
            server.Id,
            Guid.NewGuid().ToString("N"),
            now.Add(GameTokenLifetime));
        ticket.Status = TicketStatus.Assigned;
        ticket.MatchId = matchId;
        ticket.Endpoint = server.Endpoint;
        ticket.GameToken = tokens.IssueGameToken(admission);
    }

    private void CompleteTickets(string matchId)
    {
        foreach (TicketState ticket in tickets.Values)
        {
            if (ticket.Status == TicketStatus.Assigned &&
                string.Equals(ticket.MatchId, matchId, StringComparison.Ordinal))
            {
                ticket.Status = TicketStatus.Completed;
            }
        }
    }

    private void MarkUnhealthyServers(DateTimeOffset now)
    {
        foreach (ServerState server in servers.Values)
        {
            if (server.Lifecycle == ServerLifecycle.Ready &&
                now - server.LastHeartbeat > ServerHealthTimeout)
            {
                server.Lifecycle = ServerLifecycle.Unhealthy;
            }
        }
    }

    private static string ResultFingerprint(MatchResultRequest request)
    {
        string scores = string.Join(",", request.Scores
            .OrderBy(pair => pair.Key, StringComparer.Ordinal)
            .Select(pair => $"{pair.Key}:{pair.Value}"));
        return request.MatchId + "|" + scores;
    }

    private sealed class ServerState(
        string id,
        string region,
        string endpoint,
        int capacity,
        int activeMatches,
        ServerLifecycle lifecycle,
        DateTimeOffset lastHeartbeat)
    {
        public string Id { get; } = id;
        public string Region { get; } = region;
        public string Endpoint { get; } = endpoint;
        public int Capacity { get; } = capacity;
        public int ActiveMatches { get; set; } = activeMatches;
        public ServerLifecycle Lifecycle { get; set; } = lifecycle;
        public DateTimeOffset LastHeartbeat { get; set; } = lastHeartbeat;
    }

    private sealed class TicketState(
        string id,
        string playerId,
        int rating,
        string region,
        DateTimeOffset enqueuedAt)
    {
        public string Id { get; } = id;
        public string PlayerId { get; } = playerId;
        public int Rating { get; } = rating;
        public string Region { get; } = region;
        public DateTimeOffset EnqueuedAt { get; } = enqueuedAt;
        public TicketStatus Status { get; set; } = TicketStatus.Waiting;
        public string? MatchId { get; set; }
        public string? Endpoint { get; set; }
        public string? GameToken { get; set; }
    }

    private sealed class MatchState(
        string id,
        string serverId,
        IEnumerable<string> players)
    {
        public string Id { get; } = id;
        public string ServerId { get; } = serverId;
        public HashSet<string> Players { get; } = new(players, StringComparer.Ordinal);
        public bool Completed { get; set; }
    }

    private sealed record RecordedResult(
        ResultReceipt Receipt,
        string Fingerprint,
        string ServerId);
}
