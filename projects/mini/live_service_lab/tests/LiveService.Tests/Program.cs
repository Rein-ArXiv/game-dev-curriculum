using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using LiveService.Core;

var t0 = DateTimeOffset.Parse("2026-01-01T00:00:00Z");
var tokens = new TokenService(Encoding.UTF8.GetBytes(
    "0123456789abcdef0123456789abcdef"));

// snippet-begin: check-table
// step 문서 한 편이 실행 단위 하나다. 인자 없이 돌리면 전체, `step03` 처럼 이름을 주면
// 그 step 이 추가한 검사만 돈다. 검사 이름 하나만 주는 것도 같은 필터로 처리된다.
(string Step, string Name, Action Run)[] checks =
[
    ("step01", "TicketOwnership", TicketOwnership),
    ("step01", "AtomicTransitions", AtomicTransitions),
    ("step01", "InjectedClock", InjectedClock),
    ("step02", "AccessTokenContract", AccessTokenContract),
    ("step02", "TokenAudience", TokenAudience),
    ("step02", "TokenExpiry", TokenExpiry),
    ("step02", "IdentifierContract", IdentifierContract),
    ("step03", "RangeGuards", RangeGuards),
    ("step03", "MatchAllocation", MatchAllocation),
    ("step03", "ServerHealth", ServerHealth),
    ("step03", "ProgressiveWidening", ProgressiveWidening),
    ("step03", "RegionAffinity", RegionAffinity),
    ("step03", "ServerDrain", ServerDrain),
    ("step04", "IdempotentResult", IdempotentResult),
    ("step04", "PayloadConflict", PayloadConflict),
    ("step04", "TicketLifecycle", TicketLifecycle),
    ("step05", "HttpAuthBoundary", HttpAuthBoundary),
    ("step05", "HttpErrorMapping", HttpErrorMapping),
];
// snippet-end: check-table

string filter = args.Length > 0 ? args[0].Trim() : string.Empty;
(string Step, string Name, Action Run)[] selected = checks
    .Where(check => filter.Length == 0 ||
        string.Equals(check.Step, filter, StringComparison.OrdinalIgnoreCase) ||
        string.Equals(check.Name, filter, StringComparison.OrdinalIgnoreCase))
    .ToArray();

if (selected.Length == 0)
{
    Console.Error.WriteLine(
        $"live_service_tests: `{filter}` 에 해당하는 검사가 없습니다. " +
        "step01~step05 또는 검사 이름을 넣으십시오.");
    return 1;
}

string label = filter.Length == 0 ? "(all)" : filter;
Console.WriteLine($"live_service_tests: filter={label} -> {selected.Length} check(s)");
int passed = 0;
foreach ((string step, string name, Action run) in selected)
{
    run();
    ++passed;
    Console.WriteLine($"  {step} {name,-19} ok");
}

Console.WriteLine(filter.Length == 0
    ? "live_service_tests: all checks passed"
    : $"live_service_tests: {label} {passed}/{selected.Length} checks passed");
return 0;

// ── step01: 제어 평면 상태 기계 ─────────────────────────────────────────────

// assignment는 ticket 소유자만 조회한다.
void TicketOwnership()
{
    const string name = "TicketOwnership";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("owner-1", "kr", "10.7.0.1:41701", 2), t0),
        name + ": server 등록");
    string mine = plane.Enqueue(new QueueRequest("owner-a", 1500, "kr"), t0);
    string yours = plane.Enqueue(new QueueRequest("owner-b", 1500, "kr"), t0.AddSeconds(1));
    var when = t0.AddSeconds(1);
    // snippet-begin: ticket-ownership
    Require(plane.GetTicket(mine, "owner-a", when) is not null,
        name + ": 소유자는 자기 ticket을 읽는다");
    Require(plane.GetTicket(mine, "owner-b", when) is null,
        name + ": 다른 player는 남의 ticket을 읽지 못한다");
    Require(plane.GetTicket(yours, "owner-a", when) is null,
        name + ": 반대 방향도 거부된다");
    Require(plane.GetTicket("00000000000000000000000000000000", "owner-a", when) is null,
        name + ": 없는 ticket ID는 null이다");
    // snippet-end: ticket-ownership
}

// 여러 dictionary의 전이가 하나의 lock 안에서 함께 일어난다.
void AtomicTransitions()
{
    const string name = "AtomicTransitions";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("atomic-1", "kr", "10.12.0.1:42201", 3), t0),
        name + ": capacity 3 server 등록");

    const int players = 40;
    var ticketIds = new string[players];
    // snippet-begin: atomic-transitions
    Parallel.For(0, players, index =>
    {
        ticketIds[index] = plane.Enqueue(
            new QueueRequest($"atomic-{index:D2}", 1500, "kr"), t0);
    });
    ControlPlaneMetrics metrics = plane.Metrics(t0);
    Require(metrics.ActiveMatches == 3,
        name + ": 동시 요청 40건도 capacity 3을 넘겨 match를 만들지 않는다");
    Require(metrics.WaitingTickets == players - 6,
        name + ": 배정된 6명을 뺀 34명이 대기로 남는다");
    Require(ticketIds.Distinct(StringComparer.Ordinal).Count() == players,
        name + ": 40명이 각자 다른 ticket을 받는다");
    // snippet-end: atomic-transitions
    int assigned = ticketIds
        .Select((ticketId, index) => plane.GetTicket(ticketId, $"atomic-{index:D2}", t0))
        .Count(view => view?.Status == TicketStatus.Assigned);
    Require(assigned == 6, name + ": 정확히 세 쌍만 배정 상태다");
}

// core는 wall clock을 읽지 않는다. 주입한 시각만이 결과를 결정한다.
void InjectedClock()
{
    const string name = "InjectedClock";

    (TicketStatus Status, int ReadyAfterGap, bool FreshToken, bool StaleToken) Run(
        DateTimeOffset epoch)
    {
        var plane = NewPlane();
        Require(plane.RegisterServer(
            new ServerRegistration("clock-1", "kr", "10.13.0.1:42301", 2), epoch),
            name + ": server 등록");
        string ticketId = plane.Enqueue(new QueueRequest("clock-a", 1500, "kr"), epoch);
        plane.Enqueue(new QueueRequest("clock-b", 1500, "kr"), epoch.AddSeconds(1));
        TicketView view = plane.GetTicket(ticketId, "clock-a", epoch.AddSeconds(1))
            ?? throw new InvalidOperationException(name + ": ticket이 없다");
        string token = view.GameToken ?? string.Empty;
        return (
            view.Status,
            plane.Metrics(epoch.AddSeconds(16)).ReadyServers,
            tokens.TryValidateGameToken(token, "clock-1", epoch.AddSeconds(2), out _),
            tokens.TryValidateGameToken(token, "clock-1", epoch.AddSeconds(60), out _));
    }

    var past = Run(DateTimeOffset.Parse("1990-01-01T00:00:00Z"));
    var present = Run(t0);
    var future = Run(DateTimeOffset.Parse("2099-01-01T00:00:00Z"));
    Require(past == present && present == future,
        name + ": 1990년·2026년·2099년에서 같은 결과가 나온다");
    Require(present.Status == TicketStatus.Assigned,
        name + ": 주입 시각 기준으로 배정이 일어난다");
    Require(present.ReadyAfterGap == 0,
        name + ": heartbeat timeout이 주입 시각 기준으로 판정된다");
    Require(present.FreshToken && !present.StaleToken,
        name + ": game token 만료도 주입 시각 기준이다");
}

// ── step02: token과 식별자 계약 ─────────────────────────────────────────────

// 접근 token은 서명·만료·변조를 모두 검사한다.
void AccessTokenContract()
{
    const string name = "AccessTokenContract";
    string access = tokens.IssueAccessToken("p1", t0, TimeSpan.FromMinutes(1));
    Require(tokens.TryValidateAccessToken(access, t0, out string player) && player == "p1",
        name + ": 정상 access token이 통과한다");
    Require(!tokens.TryValidateAccessToken(access, t0.AddMinutes(2), out _),
        name + ": 만료된 access token은 거부된다");
    string tampered = access[..^1] + (access[^1] == 'A' ? 'B' : 'A');
    Require(!tokens.TryValidateAccessToken(tampered, t0, out _),
        name + ": 변조된 access token은 거부된다");
}

// game token은 발급 대상 server에서만 열린다.
void TokenAudience()
{
    const string name = "TokenAudience";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("audience-a", "kr", "10.1.0.1:41101", 2), t0),
        name + ": 첫 server 등록");
    Require(plane.RegisterServer(
        new ServerRegistration("audience-z", "kr", "10.1.0.2:41102", 2), t0),
        name + ": 둘째 server 등록");

    string ticketA = plane.Enqueue(new QueueRequest("aud-a", 1500, "kr"), t0);
    string ticketB = plane.Enqueue(new QueueRequest("aud-b", 1500, "kr"), t0.AddSeconds(1));
    TicketView viewA = plane.GetTicket(ticketA, "aud-a", t0.AddSeconds(1))
        ?? throw new InvalidOperationException(name + ": 첫 ticket이 없다");
    TicketView viewB = plane.GetTicket(ticketB, "aud-b", t0.AddSeconds(1))
        ?? throw new InvalidOperationException(name + ": 둘째 ticket이 없다");
    Require(viewA.Status == TicketStatus.Assigned && viewB.Status == TicketStatus.Assigned,
        name + ": 두 ticket이 배정된다");
    Require(viewA.GameToken is not null && viewB.GameToken is not null,
        name + ": 배정에 game token이 실린다");

    // snippet-begin: game-token-audience
    Require(tokens.TryValidateGameToken(
            viewA.GameToken!, "audience-a", t0, out GameAdmission? admissionA) &&
        admissionA!.PlayerId == "aud-a",
        name + ": 소유 server가 game token을 연다");
    Require(!tokens.TryValidateGameToken(viewA.GameToken!, "audience-z", t0, out _),
        name + ": 다른 server ID로는 game token이 열리지 않는다");
    // snippet-end: game-token-audience
    Require(tokens.TryValidateGameToken(
            viewB.GameToken!, "audience-a", t0, out GameAdmission? admissionB) &&
        admissionB!.PlayerId == "aud-b",
        name + ": 같은 match의 둘째 token도 소유 server가 연다");
    GameAdmission grantA = admissionA!;
    GameAdmission grantB = admissionB!;
    Require(grantA.MatchId == grantB.MatchId,
        name + ": 두 admission이 같은 match를 가리킨다");
    Require(grantA.TokenId != grantB.TokenId,
        name + ": admission마다 token ID가 다르다");
    Require(grantA.ServerId == "audience-a",
        name + ": admission이 배정된 server ID를 담는다");

    string blankPlayer = tokens.IssueGameToken(new GameAdmission(
        "", "match-x", "audience-a", "token-x", t0.AddMinutes(1)));
    string blankMatch = tokens.IssueGameToken(new GameAdmission(
        "aud-a", "", "audience-a", "token-x", t0.AddMinutes(1)));
    string blankTokenId = tokens.IssueGameToken(new GameAdmission(
        "aud-a", "match-x", "audience-a", "", t0.AddMinutes(1)));
    Require(!tokens.TryValidateGameToken(blankPlayer, "audience-a", t0, out _),
        name + ": player claim이 빈 game token은 거부된다");
    Require(!tokens.TryValidateGameToken(blankMatch, "audience-a", t0, out _),
        name + ": match claim이 빈 game token은 거부된다");
    Require(!tokens.TryValidateGameToken(blankTokenId, "audience-a", t0, out _),
        name + ": token ID가 빈 game token은 거부된다");
}

// game token은 발급 45초 뒤 만료된다.
void TokenExpiry()
{
    const string name = "TokenExpiry";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("expiry-1", "kr", "10.2.0.1:41201", 2), t0),
        name + ": server 등록");
    string ticketId = plane.Enqueue(new QueueRequest("exp-a", 1500, "kr"), t0);
    plane.Enqueue(new QueueRequest("exp-b", 1500, "kr"), t0.AddSeconds(1));
    TicketView view = plane.GetTicket(ticketId, "exp-a", t0.AddSeconds(1))
        ?? throw new InvalidOperationException(name + ": ticket이 없다");
    Require(view.Status == TicketStatus.Assigned, name + ": ticket이 배정된다");

    Require(tokens.TryValidateGameToken(
            view.GameToken!, "expiry-1", t0.AddSeconds(44), out GameAdmission? admission),
        name + ": 만료 1초 전 game token은 유효하다");
    Require(admission!.ExpiresAt == t0.AddSeconds(1).AddSeconds(45),
        name + ": game token 수명은 배정 시각 기준 45초다");
    Require(!tokens.TryValidateGameToken(view.GameToken!, "expiry-1", t0.AddSeconds(46), out _),
        name + ": 만료 시각을 지난 game token은 거부된다");
    Require(!tokens.TryValidateGameToken(view.GameToken!, "expiry-1", t0.AddDays(1), out _),
        name + ": 하루 뒤 game token은 거부된다");
}

// player·server·region ID는 ASCII 영숫자와 `.`, `-`, `_` 만 허용하고 길이 상한을 가진다.
void IdentifierContract()
{
    const string name = "IdentifierContract";
    var plane = NewPlane();
    Require(!plane.RegisterServer(
        new ServerRegistration("server with spaces", "kr", "127.0.0.1:1", 1), t0),
        name + ": 공백이 든 server ID는 거부된다");
    Require(!plane.RegisterServer(
        new ServerRegistration("ident-1", "kr", "   ", 1), t0),
        name + ": 빈 endpoint는 거부된다");
    Require(!plane.RegisterServer(
        new ServerRegistration("ident-1", "kr/east", "127.0.0.1:1", 1), t0),
        name + ": 허용되지 않는 문자가 든 region은 거부된다");
    Require(!plane.RegisterServer(
        new ServerRegistration(new string('s', 65), "kr", "127.0.0.1:1", 1), t0),
        name + ": 64자를 넘는 server ID는 거부된다");
    Require(plane.RegisterServer(
        new ServerRegistration("ident-1", "kr", "127.0.0.1:1", 1), t0),
        name + ": 정상 등록은 통과한다");
    Require(plane.Metrics(t0).ReadyServers == 1,
        name + ": 거부된 등록은 fleet에 남지 않는다");

    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("invalid player", 1500, "kr"), t0),
        name + ": 공백이 든 player ID는 거부된다");
    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest(new string('p', 65), 1500, "kr"), t0),
        name + ": 64자를 넘는 player ID는 거부된다");
    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("ident-p", 1500, "kr/east"), t0),
        name + ": 허용되지 않는 문자가 든 region은 queue에서도 거부된다");
}

// ── step03: 매치메이킹과 fleet ──────────────────────────────────────────────

// capacity·rating·party size의 수치 범위 계약.
void RangeGuards()
{
    const string name = "RangeGuards";
    var plane = NewPlane();
    Require(!plane.RegisterServer(
        new ServerRegistration("range-1", "kr", "127.0.0.1:1", 0), t0),
        name + ": capacity 0 등록은 거부된다");
    Require(!plane.RegisterServer(
        new ServerRegistration("range-1", "kr", "127.0.0.1:1", -1), t0),
        name + ": 음수 capacity 등록은 거부된다");
    Require(plane.Metrics(t0).ReadyServers == 0,
        name + ": 거부된 등록은 fleet에 남지 않는다");
    Require(plane.RegisterServer(
        new ServerRegistration("range-1", "kr", "127.0.0.1:1", 1), t0),
        name + ": capacity 1 등록은 통과한다");

    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("range-p", -1, "kr"), t0),
        name + ": 음수 rating은 거부된다");
    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("range-p", 10_001, "kr"), t0),
        name + ": rating 상한 10000을 넘으면 거부된다");
    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("range-p", 1500, "kr", 2), t0),
        name + ": party size 2는 거부된다");
    Require(plane.Metrics(t0).WaitingTickets == 0,
        name + ": 거부된 queue 요청은 ticket을 만들지 않는다");
}

// capacity·endpoint 전달·ticket 중복 억제·슬롯 회수.
void MatchAllocation()
{
    const string name = "MatchAllocation";

    var plane = NewPlane();
    var alpha = new ServerRegistration("alloc-alpha", "kr", "10.3.0.11:41301", 1);
    var beta = new ServerRegistration("alloc-beta", "kr", "10.3.0.12:41302", 1);
    Require(alpha.Endpoint != beta.Endpoint, name + ": 두 server endpoint가 서로 다르다");
    Require(plane.RegisterServer(alpha, t0), name + ": alpha 등록");
    Require(plane.RegisterServer(beta, t0), name + ": beta 등록");
    string a1 = plane.Enqueue(new QueueRequest("alloc-1", 1500, "kr"), t0);
    string a2 = plane.Enqueue(new QueueRequest("alloc-2", 1500, "kr"), t0.AddSeconds(1));
    string a3 = plane.Enqueue(new QueueRequest("alloc-3", 1500, "kr"), t0.AddSeconds(2));
    string a4 = plane.Enqueue(new QueueRequest("alloc-4", 1500, "kr"), t0.AddSeconds(3));
    var when = t0.AddSeconds(3);
    TicketView v1 = plane.GetTicket(a1, "alloc-1", when)
        ?? throw new InvalidOperationException(name + ": ticket 1이 없다");
    TicketView v2 = plane.GetTicket(a2, "alloc-2", when)
        ?? throw new InvalidOperationException(name + ": ticket 2가 없다");
    TicketView v3 = plane.GetTicket(a3, "alloc-3", when)
        ?? throw new InvalidOperationException(name + ": ticket 3이 없다");
    TicketView v4 = plane.GetTicket(a4, "alloc-4", when)
        ?? throw new InvalidOperationException(name + ": ticket 4가 없다");
    Require(v1.Status == TicketStatus.Assigned && v2.Status == TicketStatus.Assigned &&
        v3.Status == TicketStatus.Assigned && v4.Status == TicketStatus.Assigned,
        name + ": capacity 1 server 두 대가 두 match를 소화한다");
    Require(v1.MatchId == v2.MatchId && v3.MatchId == v4.MatchId && v1.MatchId != v3.MatchId,
        name + ": 두 쌍이 서로 다른 match를 받는다");
    Require(v1.Endpoint == alpha.Endpoint,
        name + ": 첫 match가 alpha endpoint를 전달받는다");
    Require(v3.Endpoint == beta.Endpoint,
        name + ": 둘째 match가 beta endpoint를 전달받는다");
    Require(tokens.TryValidateGameToken(v1.GameToken!, alpha.ServerId, when, out _),
        name + ": 첫 match token의 audience는 alpha다");
    Require(tokens.TryValidateGameToken(v3.GameToken!, beta.ServerId, when, out _),
        name + ": 둘째 match token의 audience는 beta다");

    var capped = NewPlane();
    Require(capped.RegisterServer(
        new ServerRegistration("cap-1", "kr", "10.3.0.21:41311", 1), t0),
        name + ": capacity 1 server 등록");
    string c1 = capped.Enqueue(new QueueRequest("cap-a", 1500, "kr"), t0);
    string c2 = capped.Enqueue(new QueueRequest("cap-b", 1500, "kr"), t0.AddSeconds(1));
    string c3 = capped.Enqueue(new QueueRequest("cap-c", 1500, "kr"), t0.AddSeconds(2));
    string c4 = capped.Enqueue(new QueueRequest("cap-d", 1500, "kr"), t0.AddSeconds(3));
    var capWhen = t0.AddSeconds(3);
    Require(capped.GetTicket(c1, "cap-a", capWhen)?.Status == TicketStatus.Assigned &&
        capped.GetTicket(c2, "cap-b", capWhen)?.Status == TicketStatus.Assigned,
        name + ": 첫 두 명은 배정된다");
    Require(capped.GetTicket(c3, "cap-c", capWhen)?.Status == TicketStatus.Waiting &&
        capped.GetTicket(c4, "cap-d", capWhen)?.Status == TicketStatus.Waiting,
        name + ": capacity를 넘은 두 명은 대기한다");
    ControlPlaneMetrics capMetrics = capped.Metrics(capWhen);
    Require(capMetrics.ActiveMatches == 1,
        name + ": capacity 1 server는 match 하나만 소유한다");
    Require(capMetrics.WaitingTickets == 2,
        name + ": 남은 ticket 두 개가 대기 상태로 남는다");

    var dedup = NewPlane();
    Require(dedup.RegisterServer(
        new ServerRegistration("dedup-1", "kr", "10.3.0.31:41321", 2), t0),
        name + ": dedup server 등록");
    string first = dedup.Enqueue(new QueueRequest("dedup-p", 1500, "kr"), t0);
    string retry = dedup.Enqueue(new QueueRequest("dedup-p", 1500, "kr"), t0.AddSeconds(1));
    Require(first == retry, name + ": 대기 중 queue 재시도는 같은 ticket을 돌려준다");
    Require(dedup.Metrics(t0.AddSeconds(1)).WaitingTickets == 1,
        name + ": queue 재시도가 ticket을 늘리지 않는다");
    dedup.Enqueue(new QueueRequest("dedup-q", 1500, "kr"), t0.AddSeconds(2));
    Require(dedup.GetTicket(first, "dedup-p", t0.AddSeconds(2))?.Status ==
        TicketStatus.Assigned, name + ": dedup ticket이 배정된다");
    string assignedRetry = dedup.Enqueue(
        new QueueRequest("dedup-p", 1500, "kr"), t0.AddSeconds(3));
    Require(assignedRetry == first,
        name + ": 배정된 ticket도 queue 재시도에 재사용된다");

    var recycle = NewPlane();
    var recycleServer = new ServerRegistration("recycle-1", "kr", "10.3.0.41:41331", 1);
    Require(recycle.RegisterServer(recycleServer, t0), name + ": recycle server 등록");
    string r1 = recycle.Enqueue(new QueueRequest("recycle-a", 1500, "kr"), t0);
    recycle.Enqueue(new QueueRequest("recycle-b", 1500, "kr"), t0.AddSeconds(1));
    TicketView rv1 = recycle.GetTicket(r1, "recycle-a", t0.AddSeconds(1))
        ?? throw new InvalidOperationException(name + ": recycle ticket이 없다");
    Require(rv1.Status == TicketStatus.Assigned, name + ": recycle 첫 match 배정");
    Require(!recycle.RegisterServer(
        new ServerRegistration("recycle-1", "kr", "10.3.0.99:41399", 1), t0.AddSeconds(2)),
        name + ": 진행 중 match가 있으면 endpoint 변경 재등록을 거부한다");
    recycle.RecordResult(
        new MatchResultRequest("recycle-result", rv1.MatchId!,
            new Dictionary<string, int> { ["recycle-a"] = 1, ["recycle-b"] = 0 }),
        "recycle-1",
        t0.AddSeconds(10));
    string r3 = recycle.Enqueue(new QueueRequest("recycle-c", 1500, "kr"), t0.AddSeconds(10));
    recycle.Enqueue(new QueueRequest("recycle-d", 1500, "kr"), t0.AddSeconds(11));
    TicketView rv3 = recycle.GetTicket(r3, "recycle-c", t0.AddSeconds(11))
        ?? throw new InvalidOperationException(name + ": recycle 둘째 ticket이 없다");
    Require(rv3.Status == TicketStatus.Assigned,
        name + ": 결과 기록이 capacity 슬롯을 되돌려 준다");
    Require(rv3.MatchId != rv1.MatchId, name + ": 새 배정은 새 match ID를 쓴다");
    Require(recycle.Metrics(t0.AddSeconds(11)).ActiveMatches == 1,
        name + ": 완료된 match는 active에서 빠진다");
}

// heartbeat 신선도와 unhealthy 전이.
void ServerHealth()
{
    const string name = "ServerHealth";

    var alive = NewPlane();
    Require(alive.RegisterServer(
        new ServerRegistration("health-1", "kr", "10.4.0.1:41401", 2), t0),
        name + ": server 등록");
    Require(!alive.Heartbeat("health-unknown", t0.AddSeconds(10)),
        name + ": 모르는 server의 heartbeat는 실패한다");
    Require(alive.Heartbeat("health-1", t0.AddSeconds(10)),
        name + ": 등록된 server의 heartbeat는 성공한다");
    string h1 = alive.Enqueue(new QueueRequest("health-a", 1500, "kr"), t0.AddSeconds(20));
    alive.Enqueue(new QueueRequest("health-b", 1500, "kr"), t0.AddSeconds(20));
    Require(alive.GetTicket(h1, "health-a", t0.AddSeconds(20))?.Status ==
        TicketStatus.Assigned,
        name + ": heartbeat가 health window를 갱신한다");

    var stale = NewPlane();
    var staleServer = new ServerRegistration("stale-1", "kr", "10.4.0.2:41402", 2);
    Require(stale.RegisterServer(staleServer, t0), name + ": stale server 등록");
    string s1 = stale.Enqueue(new QueueRequest("stale-a", 1500, "kr"), t0.AddSeconds(16));
    stale.Enqueue(new QueueRequest("stale-b", 1500, "kr"), t0.AddSeconds(16));
    Require(stale.GetTicket(s1, "stale-a", t0.AddSeconds(16))?.Status ==
        TicketStatus.Waiting,
        name + ": 15초 넘게 heartbeat가 없으면 배정하지 않는다");
    Require(stale.Metrics(t0.AddSeconds(16)).ReadyServers == 0,
        name + ": timeout을 넘긴 server는 ready 집합에서 빠진다");
    Require(stale.Heartbeat("stale-1", t0.AddSeconds(16)),
        name + ": 늦은 heartbeat 자체는 성공한다");
    Require(stale.GetTicket(s1, "stale-a", t0.AddSeconds(17))?.Status ==
        TicketStatus.Waiting,
        name + ": 늦은 heartbeat 한 번으로 unhealthy가 복구되지 않는다");
    Require(stale.RegisterServer(staleServer, t0.AddSeconds(18)),
        name + ": 재등록은 성공한다");
    Require(stale.GetTicket(s1, "stale-a", t0.AddSeconds(18))?.Status ==
        TicketStatus.Assigned,
        name + ": 재등록이 ready 상태를 다시 선언한다");

    var mixed = NewPlane();
    var stalePick = new ServerRegistration("aaa-stale", "kr", "10.4.0.3:41403", 4);
    var freshPick = new ServerRegistration("zzz-fresh", "kr", "10.4.0.4:41404", 4);
    Require(mixed.RegisterServer(stalePick, t0), name + ": 먼저 stale 후보를 등록한다");
    string m1 = mixed.Enqueue(new QueueRequest("mixed-a", 1500, "kr"), t0);
    mixed.Enqueue(new QueueRequest("mixed-b", 1700, "kr"), t0.AddSeconds(1));
    Require(mixed.GetTicket(m1, "mixed-a", t0.AddSeconds(1))?.Status ==
        TicketStatus.Waiting,
        name + ": rating 차 200은 기본 window 밖이다");
    Require(mixed.RegisterServer(freshPick, t0.AddSeconds(20)),
        name + ": 20초 뒤 새 server를 등록한다");
    TicketView mixedView = mixed.GetTicket(m1, "mixed-a", t0.AddSeconds(20))
        ?? throw new InvalidOperationException(name + ": mixed ticket이 없다");
    Require(mixedView.Status == TicketStatus.Assigned,
        name + ": 넓어진 window에서 배정된다");
    Require(mixedView.Endpoint == freshPick.Endpoint,
        name + ": heartbeat가 끊긴 후보를 건너뛰고 신선한 server를 고른다");
}

// 대기 시간에 따른 rating window 확대와 상한·clamp.
void ProgressiveWidening()
{
    const string name = "ProgressiveWidening";

    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("widen-1", "kr", "10.5.0.1:41501", 1), t0),
        name + ": widen server 등록");
    string w1 = plane.Enqueue(new QueueRequest("widen-a", 1500, "kr"), t0);
    string w2 = plane.Enqueue(new QueueRequest("widen-b", 1950, "kr"), t0);
    Require(plane.GetTicket(w1, "widen-a", t0)?.Status == TicketStatus.Waiting,
        name + ": rating 차 450은 기본 window 밖이다");
    KeepAlive(plane, "widen-1", 30, name);
    // snippet-begin: widening-boundary
    Require(plane.GetTicket(w1, "widen-a", t0.AddSeconds(34))?.Status ==
        TicketStatus.Waiting,
        name + ": 34초에는 허용 폭이 440이라 아직 대기한다");
    Require(plane.GetTicket(w1, "widen-a", t0.AddSeconds(35))?.Status ==
        TicketStatus.Assigned,
        name + ": 35초에 허용 폭이 450이 되어 배정된다");
    // snippet-end: widening-boundary
    Require(plane.GetTicket(w2, "widen-b", t0.AddSeconds(35))?.Status ==
        TicketStatus.Assigned,
        name + ": 상대 ticket도 같은 시점에 배정된다");

    var capped = NewPlane();
    Require(capped.RegisterServer(
        new ServerRegistration("widen-2", "kr", "10.5.0.2:41502", 1), t0),
        name + ": 상한 확인용 server 등록");
    string x1 = capped.Enqueue(new QueueRequest("widen-c", 1400, "kr"), t0);
    capped.Enqueue(new QueueRequest("widen-d", 2000, "kr"), t0);
    KeepAlive(capped, "widen-2", 100, name);
    Require(capped.GetTicket(x1, "widen-c", t0.AddSeconds(100))?.Status ==
        TicketStatus.Waiting,
        name + ": 허용 폭은 500에서 멈추므로 rating 차 600은 배정되지 않는다");
    Require(capped.Metrics(t0.AddSeconds(100)).WaitingTickets == 2,
        name + ": 두 ticket 모두 대기로 남는다");

    var reverse = NewPlane();
    Require(reverse.RegisterServer(
        new ServerRegistration("clock-server", "kr", "10.5.0.3:41503", 1), t0),
        name + ": 역행 시계 server 등록");
    string clockFirst = reverse.Enqueue(new QueueRequest("clock-a", 1500, "kr"), t0);
    reverse.Enqueue(new QueueRequest("clock-b", 1580, "kr"), t0.AddSeconds(-5));
    Require(reverse.GetTicket(clockFirst, "clock-a", t0.AddSeconds(-5))?.Status ==
        TicketStatus.Assigned,
        name + ": 음수 경과 시간은 0으로 clamp되어 기본 폭 100을 유지한다");
}

// region이 다르면 서로도, server도 후보가 아니다.
void RegionAffinity()
{
    const string name = "RegionAffinity";

    var same = NewPlane();
    Require(same.RegisterServer(
        new ServerRegistration("region-kr", "kr", "10.6.0.1:41601", 2), t0),
        name + ": kr server 등록");
    string k1 = same.Enqueue(new QueueRequest("region-a", 1500, "kr"), t0);
    same.Enqueue(new QueueRequest("region-b", 1500, "kr"), t0.AddSeconds(1));
    Require(same.GetTicket(k1, "region-a", t0.AddSeconds(1))?.Status ==
        TicketStatus.Assigned,
        name + ": 같은 region 쌍은 배정된다");

    var foreign = NewPlane();
    Require(foreign.RegisterServer(
        new ServerRegistration("region-us", "us", "10.6.0.2:41602", 2), t0),
        name + ": us server 등록");
    string f1 = foreign.Enqueue(new QueueRequest("region-c", 1500, "kr"), t0);
    foreign.Enqueue(new QueueRequest("region-d", 1500, "kr"), t0.AddSeconds(1));
    Require(foreign.GetTicket(f1, "region-c", t0.AddSeconds(1))?.Status ==
        TicketStatus.Waiting,
        name + ": 다른 region server는 kr ticket을 받지 않는다");
    Require(foreign.Metrics(t0.AddSeconds(1)).WaitingTickets == 2,
        name + ": 두 ticket 모두 대기로 남는다");

    var split = NewPlane();
    Require(split.RegisterServer(
        new ServerRegistration("split-kr", "kr", "10.6.0.3:41603", 2), t0),
        name + ": split kr server 등록");
    Require(split.RegisterServer(
        new ServerRegistration("split-us", "us", "10.6.0.4:41604", 2), t0),
        name + ": split us server 등록");
    string s1 = split.Enqueue(new QueueRequest("split-a", 1500, "kr"), t0);
    string s2 = split.Enqueue(new QueueRequest("split-b", 1500, "us"), t0.AddSeconds(1));
    Require(split.GetTicket(s1, "split-a", t0.AddSeconds(1))?.Status ==
        TicketStatus.Waiting,
        name + ": region이 다른 두 ticket은 서로 짝이 되지 않는다");
    Require(split.GetTicket(s2, "split-b", t0.AddSeconds(1))?.Status ==
        TicketStatus.Waiting,
        name + ": 반대편 ticket도 대기로 남는다");
}

// drain은 신규 할당만 멈추고 진행 중 match는 보존한다.
void ServerDrain()
{
    const string name = "ServerDrain";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("drain-1", "kr", "10.8.0.1:41801", 4), t0),
        name + ": server 등록");
    string d1 = plane.Enqueue(new QueueRequest("drain-a", 1500, "kr"), t0);
    plane.Enqueue(new QueueRequest("drain-b", 1500, "kr"), t0.AddSeconds(1));
    Require(plane.GetTicket(d1, "drain-a", t0.AddSeconds(1))?.Status ==
        TicketStatus.Assigned,
        name + ": ready server는 match를 받는다");

    Require(!plane.DrainServer("drain-unknown"),
        name + ": 모르는 server의 drain은 실패한다");
    Require(plane.DrainServer("drain-1"), name + ": drain은 성공한다");
    string d3 = plane.Enqueue(new QueueRequest("drain-c", 1500, "kr"), t0.AddSeconds(2));
    string d4 = plane.Enqueue(new QueueRequest("drain-d", 1500, "kr"), t0.AddSeconds(3));
    var when = t0.AddSeconds(3);
    Require(plane.GetTicket(d3, "drain-c", when)?.Status == TicketStatus.Waiting,
        name + ": draining server는 새 match를 받지 않는다");
    Require(plane.GetTicket(d4, "drain-d", when)?.Status == TicketStatus.Waiting,
        name + ": 두 ticket 모두 대기로 남는다");
    ControlPlaneMetrics metrics = plane.Metrics(when);
    Require(metrics.DrainingServers == 1 && metrics.ReadyServers == 0,
        name + ": drain된 server는 ready 집합에서 빠진다");
    Require(metrics.ActiveMatches == 1,
        name + ": drain은 진행 중 match를 보존한다");
}

// ── step04: 멱등 result ─────────────────────────────────────────────────────

// result ID 재사용은 한 번만 기록되고 소유 server만 제출한다.
void IdempotentResult()
{
    const string name = "IdempotentResult";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("result-owner", "kr", "10.9.0.1:41901", 4), t0),
        name + ": server 등록");
    string p1 = plane.Enqueue(new QueueRequest("res-a", 1500, "kr"), t0);
    plane.Enqueue(new QueueRequest("res-b", 1500, "kr"), t0.AddSeconds(1));
    string p3 = plane.Enqueue(new QueueRequest("res-c", 1500, "kr"), t0.AddSeconds(2));
    plane.Enqueue(new QueueRequest("res-d", 1500, "kr"), t0.AddSeconds(3));
    var when = t0.AddSeconds(3);
    TicketView firstMatch = plane.GetTicket(p1, "res-a", when)
        ?? throw new InvalidOperationException(name + ": 첫 ticket이 없다");
    TicketView secondMatch = plane.GetTicket(p3, "res-c", when)
        ?? throw new InvalidOperationException(name + ": 둘째 ticket이 없다");
    Require(firstMatch.Status == TicketStatus.Assigned &&
        secondMatch.Status == TicketStatus.Assigned,
        name + ": 두 match가 배정된다");
    Require(firstMatch.MatchId != secondMatch.MatchId,
        name + ": 두 match ID가 다르다");

    Expect<ArgumentException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-null", firstMatch.MatchId!, null!),
            "result-owner", when),
        name + ": score map이 null이면 거부된다");
    Expect<ArgumentException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-empty", firstMatch.MatchId!,
                new Dictionary<string, int>()),
            "result-owner", when),
        name + ": 빈 score map은 거부된다");
    Expect<KeyNotFoundException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-ghost", "ghost-match",
                new Dictionary<string, int> { ["res-a"] = 1, ["res-b"] = 0 }),
            "result-owner", when),
        name + ": 존재하지 않는 match는 거부된다");
    Expect<InvalidOperationException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-players", firstMatch.MatchId!,
                new Dictionary<string, int> { ["res-a"] = 1, ["res-c"] = 0 }),
            "result-owner", when),
        name + ": allocation과 다른 player 집합은 거부된다");
    Expect<UnauthorizedAccessException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-foreign", secondMatch.MatchId!,
                new Dictionary<string, int> { ["res-c"] = 1, ["res-d"] = 0 }),
            "other-server", when),
        name + ": 남의 match에 새 result를 제출할 수 없다");

    // snippet-begin: idempotent-retry
    ResultReceipt receipt = plane.RecordResult(
        new MatchResultRequest("res-1", firstMatch.MatchId!,
            new Dictionary<string, int> { ["res-a"] = 1, ["res-b"] = 0 }),
        "result-owner", t0.AddMinutes(1));
    Require(!receipt.Duplicate, name + ": 첫 제출은 duplicate가 아니다");
    ResultReceipt again = plane.RecordResult(
        new MatchResultRequest("res-1", firstMatch.MatchId!,
            new Dictionary<string, int> { ["res-a"] = 1, ["res-b"] = 0 }),
        "result-owner", t0.AddMinutes(2));
    Require(again.Duplicate && again.ResultId == receipt.ResultId &&
        again.RecordedAt == receipt.RecordedAt,
        name + ": 재시도는 처음 기록한 receipt를 그대로 돌려준다");
    // snippet-end: idempotent-retry
    Expect<UnauthorizedAccessException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-1", firstMatch.MatchId!,
                new Dictionary<string, int> { ["res-a"] = 1, ["res-b"] = 0 }),
            "other-server", t0.AddMinutes(2)),
        name + ": 다른 server는 duplicate receipt를 읽지 못한다");
    Expect<InvalidOperationException>(
        () => plane.RecordResult(
            new MatchResultRequest("res-2", firstMatch.MatchId!,
                new Dictionary<string, int> { ["res-a"] = 1, ["res-b"] = 0 }),
            "result-owner", t0.AddMinutes(2)),
        name + ": 이미 완료된 match는 두 번째 result를 받지 않는다");
    Require(plane.Metrics(t0.AddMinutes(2)).RecordedResults == 1,
        name + ": 재시도가 기록 수를 늘리지 않는다");
    Require(plane.GetTicket(p1, "res-a", t0.AddMinutes(2))?.Status ==
        TicketStatus.Completed,
        name + ": 결과 기록이 ticket을 완료로 옮긴다");
}

// 같은 idempotency key에 다른 payload는 충돌이고, 순서만 다른 payload는 같다.
void PayloadConflict()
{
    const string name = "PayloadConflict";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("conflict-1", "kr", "10.10.0.1:42001", 4), t0),
        name + ": server 등록");
    string q1 = plane.Enqueue(new QueueRequest("conf-a", 1500, "kr"), t0);
    plane.Enqueue(new QueueRequest("conf-b", 1500, "kr"), t0.AddSeconds(1));
    string q3 = plane.Enqueue(new QueueRequest("conf-c", 1500, "kr"), t0.AddSeconds(2));
    plane.Enqueue(new QueueRequest("conf-d", 1500, "kr"), t0.AddSeconds(3));
    var when = t0.AddSeconds(3);
    TicketView firstMatch = plane.GetTicket(q1, "conf-a", when)
        ?? throw new InvalidOperationException(name + ": 첫 ticket이 없다");
    TicketView secondMatch = plane.GetTicket(q3, "conf-c", when)
        ?? throw new InvalidOperationException(name + ": 둘째 ticket이 없다");

    ResultReceipt receipt = plane.RecordResult(
        new MatchResultRequest("conf-result", firstMatch.MatchId!,
            new Dictionary<string, int> { ["conf-a"] = 1, ["conf-b"] = 0 }),
        "conflict-1", t0.AddSeconds(4));
    Require(!receipt.Duplicate, name + ": 첫 제출이 기록된다");

    // snippet-begin: payload-fingerprint
    ResultReceipt reordered;
    try
    {
        reordered = plane.RecordResult(
            new MatchResultRequest("conf-result", firstMatch.MatchId!,
                new Dictionary<string, int> { ["conf-b"] = 0, ["conf-a"] = 1 }),
            "conflict-1", t0.AddSeconds(5));
    }
    catch (InvalidOperationException error)
    {
        throw new InvalidOperationException(
            name + ": canonical fingerprint가 score map 삽입 순서를 무시해야 한다", error);
    }
    Require(reordered.Duplicate,
        name + ": 순서만 다른 같은 payload는 duplicate로 처리된다");

    Expect<InvalidOperationException>(
        () => plane.RecordResult(
            new MatchResultRequest("conf-result", firstMatch.MatchId!,
                new Dictionary<string, int> { ["conf-a"] = 0, ["conf-b"] = 1 }),
            "conflict-1", t0.AddSeconds(6)),
        name + ": 같은 result ID에 다른 score는 충돌이다");
    // snippet-end: payload-fingerprint
    Expect<InvalidOperationException>(
        () => plane.RecordResult(
            new MatchResultRequest("conf-result", secondMatch.MatchId!,
                new Dictionary<string, int> { ["conf-a"] = 1, ["conf-b"] = 0 }),
            "conflict-1", t0.AddSeconds(7)),
        name + ": 같은 result ID에 다른 match ID는 충돌이다");
    Require(plane.Metrics(t0.AddSeconds(7)).RecordedResults == 1,
        name + ": 충돌 제출은 아무것도 기록하지 않는다");
}

// 완료된 match 뒤에는 새 ticket으로 다시 queue에 들어갈 수 있다.
void TicketLifecycle()
{
    const string name = "TicketLifecycle";
    var plane = NewPlane();
    Require(plane.RegisterServer(
        new ServerRegistration("requeue-server", "kr", "10.11.0.1:42101", 2), t0),
        name + ": server 등록");
    string first = plane.Enqueue(new QueueRequest("requeue-a", 1500, "kr"), t0);
    plane.Enqueue(new QueueRequest("requeue-b", 1500, "kr"), t0.AddSeconds(1));
    TicketView view = plane.GetTicket(first, "requeue-a", t0.AddSeconds(1))
        ?? throw new InvalidOperationException(name + ": ticket이 없다");
    Require(view.Status == TicketStatus.Assigned, name + ": ticket이 배정된다");
    plane.RecordResult(
        new MatchResultRequest("requeue-result", view.MatchId!,
            new Dictionary<string, int> { ["requeue-a"] = 1, ["requeue-b"] = 0 }),
        "requeue-server",
        t0.AddSeconds(10));
    Require(plane.GetTicket(first, "requeue-a", t0.AddSeconds(10))?.Status ==
        TicketStatus.Completed,
        name + ": 완료된 match가 ticket을 해제한다");
    string second = plane.Enqueue(new QueueRequest("requeue-a", 1500, "kr"), t0.AddSeconds(11));
    Require(second != first, name + ": 완료 뒤에는 새 ticket이 발급된다");
    TicketView secondView = plane.GetTicket(second, "requeue-a", t0.AddSeconds(11))
        ?? throw new InvalidOperationException(name + ": 둘째 ticket이 없다");
    Require(secondView.Status == TicketStatus.Waiting &&
        secondView.MatchId is null && secondView.GameToken is null,
        name + ": 새 ticket은 완료된 match를 재사용하지 않는다");
}

// ── step05: HTTP adapter ────────────────────────────────────────────────────
// core를 직접 부르는 검사는 Program.cs의 status code 변환을 한 줄도 실행하지 않는다.
// 그래서 이 두 검사만 빌드된 API를 loopback에 띄우고 실제 응답 코드를 읽는다.

// 인증 경계 — token·server key가 없거나 틀리면 domain에 닿기 전에 막힌다.
void HttpAuthBoundary()
{
    const string name = "HttpAuthBoundary";
    WithApi((client, serverKey) =>
    {
        var registration = new
        {
            serverId = "http-auth-1",
            region = "kr",
            endpoint = "127.0.0.1:42401",
            capacity = 2,
        };
        // snippet-begin: http-auth-boundary
        Require(Call(client, HttpMethod.Post, "/login",
            new { playerId = "bad id" }).Status == 400,
            name + ": 허용되지 않는 문자가 든 player ID는 400이다");
        Require(Call(client, HttpMethod.Post, "/servers/register", registration).Status == 401,
            name + ": server key가 없는 등록은 401이다");
        Require(Call(client, HttpMethod.Post, "/servers/register", registration,
            ("X-Server-Key", "wrong-key")).Status == 401,
            name + ": 틀린 server key는 401이다");
        Require(Call(client, HttpMethod.Post, "/servers/register", registration,
            ("X-Server-Key", serverKey)).Status == 200,
            name + ": 맞는 server key는 200이다");
        // snippet-end: http-auth-boundary
        Require(Call(client, HttpMethod.Post, "/servers/http-auth-1/heartbeat", null)
            .Status == 401,
            name + ": server key가 없는 heartbeat는 401이다");
        Require(Call(client, HttpMethod.Post, "/servers/http-auth-1/drain", null)
            .Status == 401,
            name + ": server key가 없는 drain은 401이다");

        (int Status, string Body) login = Call(
            client, HttpMethod.Post, "/login", new { playerId = "http-a" });
        Require(login.Status == 200, name + ": 정상 player ID의 login은 200이다");
        string accessToken = TextField(login.Body, "accessToken");
        Require(accessToken.Length > 0, name + ": login이 access token을 돌려준다");

        var queue = new { rating = 1500, region = "kr" };
        Require(Call(client, HttpMethod.Post, "/queue", queue).Status == 401,
            name + ": bearer token이 없는 queue는 401이다");
        Require(Call(client, HttpMethod.Post, "/queue", queue,
            ("Authorization", "Bearer " + Tamper(accessToken))).Status == 401,
            name + ": 변조된 bearer token은 401이다");
        (int Status, string Body) ticketA = Call(client, HttpMethod.Post, "/queue", queue,
            ("Authorization", "Bearer " + accessToken));
        Require(ticketA.Status == 200, name + ": 정상 bearer token의 queue는 200이다");
        string ticketId = TextField(ticketA.Body, "ticketId");

        string otherToken = TextField(
            Call(client, HttpMethod.Post, "/login", new { playerId = "http-b" }).Body,
            "accessToken");
        Require(Call(client, HttpMethod.Get, "/assignments/" + ticketId, null,
            ("Authorization", "Bearer " + otherToken)).Status == 404,
            name + ": 남의 ticket 조회는 404다");
        Require(Call(client, HttpMethod.Get, "/assignments/" + ticketId, null,
            ("Authorization", "Bearer " + accessToken)).Status == 200,
            name + ": 소유자의 ticket 조회는 200이다");

        var orphan = new
        {
            resultId = "http-auth-result",
            matchId = "no-match",
            scores = new Dictionary<string, int> { ["http-a"] = 1 },
        };
        Require(Call(client, HttpMethod.Post, "/results", orphan).Status == 401,
            name + ": server key가 없는 result 제출은 401이다");
        Require(Call(client, HttpMethod.Post, "/results", orphan,
            ("X-Server-Key", serverKey)).Status == 401,
            name + ": X-Server-Id가 없는 result 제출은 401이다");
    });
}

// 오류 매핑 — 400·404·409·200이 서로 다른 원인에 붙는다.
void HttpErrorMapping()
{
    const string name = "HttpErrorMapping";
    WithApi((client, serverKey) =>
    {
        var serverHeader = ("X-Server-Key", serverKey);
        Require(Call(client, HttpMethod.Post, "/servers/register", new
        {
            serverId = "http-map-1",
            region = "kr",
            endpoint = "127.0.0.1:42501",
            capacity = 1,
        }, serverHeader).Status == 200, name + ": server 등록은 200이다");
        Require(Call(client, HttpMethod.Post, "/servers/http-map-none/heartbeat", null,
            serverHeader).Status == 404,
            name + ": 모르는 server의 heartbeat는 404다");
        Require(Call(client, HttpMethod.Post, "/servers/http-map-none/drain", null,
            serverHeader).Status == 404,
            name + ": 모르는 server의 drain은 404다");

        string tokenA = TextField(
            Call(client, HttpMethod.Post, "/login", new { playerId = "map-a" }).Body,
            "accessToken");
        string tokenB = TextField(
            Call(client, HttpMethod.Post, "/login", new { playerId = "map-b" }).Body,
            "accessToken");
        Require(Call(client, HttpMethod.Post, "/queue", new { rating = 1500, region = "kr/east" },
            ("Authorization", "Bearer " + tokenA)).Status == 400,
            name + ": 허용되지 않는 region은 400이다");
        string ticketA = TextField(
            Call(client, HttpMethod.Post, "/queue", new { rating = 1500, region = "kr" },
                ("Authorization", "Bearer " + tokenA)).Body,
            "ticketId");
        Call(client, HttpMethod.Post, "/queue", new { rating = 1500, region = "kr" },
            ("Authorization", "Bearer " + tokenB));
        string matchId = TextField(
            Call(client, HttpMethod.Get, "/assignments/" + ticketA, null,
                ("Authorization", "Bearer " + tokenA)).Body,
            "matchId");
        Require(matchId.Length > 0, name + ": 두 player가 같은 match에 배정된다");

        Require(Call(client, HttpMethod.Post, "/servers/register", new
        {
            serverId = "http-map-1",
            region = "kr",
            endpoint = "127.0.0.1:42599",
            capacity = 1,
        }, serverHeader).Status == 409,
            name + ": 진행 중 match가 있는 server의 endpoint 변경 재등록은 409다");

        var owner = ("X-Server-Id", "http-map-1");
        var scores = new Dictionary<string, int> { ["map-a"] = 1, ["map-b"] = 0 };
        // snippet-begin: http-status-mapping
        Require(Call(client, HttpMethod.Post, "/results",
            new { resultId = "map-r0", matchId, scores = new Dictionary<string, int>() },
            serverHeader, owner).Status == 400,
            name + ": 빈 score map은 400이다");
        Require(Call(client, HttpMethod.Post, "/results",
            new { resultId = "map-r0", matchId = "ghost-match", scores },
            serverHeader, owner).Status == 404,
            name + ": 없는 match는 404다");
        Require(Call(client, HttpMethod.Post, "/results",
            new { resultId = "map-r0", matchId, scores },
            serverHeader, ("X-Server-Id", "http-map-other")).Status == 401,
            name + ": 남의 match에 대한 result 제출은 401이다");

        (int Status, string Body) first = Call(client, HttpMethod.Post, "/results",
            new { resultId = "map-r1", matchId, scores }, serverHeader, owner);
        Require(first.Status == 200 && !BoolField(first.Body, "duplicate"),
            name + ": 첫 제출은 200이고 duplicate가 아니다");
        (int Status, string Body) retry = Call(client, HttpMethod.Post, "/results",
            new { resultId = "map-r1", matchId, scores }, serverHeader, owner);
        Require(retry.Status == 200 && BoolField(retry.Body, "duplicate"),
            name + ": 같은 요청의 재시도는 409가 아니라 200 duplicate다");
        Require(Call(client, HttpMethod.Post, "/results",
            new
            {
                resultId = "map-r1",
                matchId,
                scores = new Dictionary<string, int> { ["map-a"] = 0, ["map-b"] = 1 },
            }, serverHeader, owner).Status == 409,
            name + ": 같은 result ID에 다른 payload는 409다");
        Require(Call(client, HttpMethod.Post, "/results",
            new { resultId = "map-r2", matchId, scores },
            serverHeader, owner).Status == 409,
            name + ": 완료된 match의 두 번째 result는 409다");
        // snippet-end: http-status-mapping

        (int Status, string Body) metrics = Call(
            client, HttpMethod.Get, "/metrics/state", null);
        Require(metrics.Status == 200, name + ": 관측 endpoint는 200이다");
        Require(NumberField(metrics.Body, "recordedResults") == 1,
            name + ": 재시도와 충돌을 합쳐도 기록된 result는 하나다");
        Require(NumberField(metrics.Body, "activeMatches") == 0,
            name + ": 완료된 match는 active에서 빠진다");
    });
}

// ── 공용 도구 ───────────────────────────────────────────────────────────────

ControlPlane NewPlane() => new(tokens);

void KeepAlive(ControlPlane plane, string serverId, int untilSeconds, string label)
{
    for (int elapsed = 10; elapsed < untilSeconds; elapsed += 10)
    {
        Require(plane.Heartbeat(serverId, t0.AddSeconds(elapsed)),
            label + ": heartbeat가 server를 살려 둔다");
    }
    Require(plane.Heartbeat(serverId, t0.AddSeconds(untilSeconds)),
        label + ": heartbeat가 server를 살려 둔다");
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void Expect<TException>(Action action, string message)
    where TException : Exception
{
    try
    {
        action();
    }
    catch (Exception error) when (error is TException)
    {
        return;
    }
    throw new InvalidOperationException(message);
}

// 빌드된 API를 빈 loopback port에 띄우고, 검사가 끝나면 반드시 종료시킨다.
static void WithApi(Action<HttpClient, string> body)
{
    string executable = FindApiExecutable();
    string serverKey = Guid.NewGuid().ToString("N");
    int port = FreeTcpPort();
    string baseUrl = $"http://127.0.0.1:{port}";

    var start = new ProcessStartInfo(executable)
    {
        UseShellExecute = false,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
    };
    start.ArgumentList.Add("--urls");
    start.ArgumentList.Add(baseUrl);
    start.Environment["LIVE_SERVICE_TOKEN_SECRET"] =
        Convert.ToBase64String(RandomNumberGenerator.GetBytes(32));
    start.Environment["LIVE_SERVICE_SERVER_KEY"] = serverKey;

    Process api = Process.Start(start)
        ?? throw new InvalidOperationException("Live Service API를 실행하지 못했습니다.");
    // 파이프를 비워 두지 않으면 API가 로그 버퍼에서 멈춘다. 내용은 쓰지 않는다.
    api.OutputDataReceived += (_, _) => { };
    api.ErrorDataReceived += (_, _) => { };
    api.BeginOutputReadLine();
    api.BeginErrorReadLine();
    try
    {
        using var client = new HttpClient
        {
            BaseAddress = new Uri(baseUrl),
            Timeout = TimeSpan.FromSeconds(15),
        };
        WaitForReady(client, api);
        body(client, serverKey);
    }
    finally
    {
        if (!api.HasExited) api.Kill(entireProcessTree: true);
        api.WaitForExit(5000);
        api.Dispose();
    }
}

static void WaitForReady(HttpClient client, Process api)
{
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        if (api.HasExited)
        {
            throw new InvalidOperationException(
                $"Live Service API가 먼저 종료했습니다(exit {api.ExitCode}).");
        }
        try
        {
            using var probe = new HttpRequestMessage(HttpMethod.Get, "/metrics/state");
            using HttpResponseMessage response = client.Send(probe);
            if (response.StatusCode == HttpStatusCode.OK) return;
        }
        catch (HttpRequestException)
        {
        }
        Thread.Sleep(100);
    }
    throw new InvalidOperationException("Live Service API가 15초 안에 준비되지 않았습니다.");
}

// 테스트 출력 디렉터리에서 lab 루트를 거슬러 올라가 같은 구성으로 빌드된 API를 찾는다.
static string FindApiExecutable()
{
    var output = new DirectoryInfo(
        AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar));
    string framework = output.Name;
    string configuration = output.Parent?.Name
        ?? throw new DirectoryNotFoundException("테스트 출력 경로가 예상과 다릅니다.");

    DirectoryInfo? lab = output;
    while (lab is not null &&
        !File.Exists(Path.Combine(
            lab.FullName, "src", "LiveService.Api", "LiveService.Api.csproj")))
    {
        lab = lab.Parent;
    }
    if (lab is null)
    {
        throw new DirectoryNotFoundException("live_service_lab 루트를 찾지 못했습니다.");
    }

    string apiOutput = Path.Combine(
        lab.FullName, "src", "LiveService.Api", "bin", configuration, framework);
    string host = Path.Combine(apiOutput, OperatingSystem.IsWindows()
        ? "LiveService.Api.exe"
        : "LiveService.Api");
    if (File.Exists(host)) return host;
    throw new FileNotFoundException(
        $"API 빌드를 찾지 못했습니다: {host}. " +
        "LiveService.Api를 같은 구성으로 빌드하십시오.", host);
}

static int FreeTcpPort()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    try
    {
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
    finally
    {
        listener.Stop();
    }
}

static (int Status, string Body) Call(
    HttpClient client,
    HttpMethod method,
    string path,
    object? body,
    params (string Name, string Value)[] headers)
{
    using var request = new HttpRequestMessage(method, path);
    foreach ((string header, string value) in headers)
    {
        request.Headers.TryAddWithoutValidation(header, value);
    }
    if (body is not null)
    {
        request.Content = new StringContent(
            JsonSerializer.Serialize(body), Encoding.UTF8, "application/json");
    }
    using HttpResponseMessage response = client.Send(request);
    using var reader = new StreamReader(response.Content.ReadAsStream());
    return ((int)response.StatusCode, reader.ReadToEnd());
}

static string TextField(string json, string property)
{
    using JsonDocument document = JsonDocument.Parse(json);
    return document.RootElement.GetProperty(property).GetString() ?? string.Empty;
}

static bool BoolField(string json, string property)
{
    using JsonDocument document = JsonDocument.Parse(json);
    return document.RootElement.GetProperty(property).GetBoolean();
}

static int NumberField(string json, string property)
{
    using JsonDocument document = JsonDocument.Parse(json);
    return document.RootElement.GetProperty(property).GetInt32();
}

// 서명 끝자리는 base64url 패딩 비트라 바꿔도 같은 바이트로 디코드될 수 있다.
// 서명이 덮는 body 의 첫 글자를 바꿔야 위조가 반드시 탐지된다.
static string Tamper(string token) =>
    (token[0] == 'a' ? 'b' : 'a') + token[1..];
