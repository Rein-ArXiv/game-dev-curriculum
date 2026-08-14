using System.Text;
using LiveService.Core;

var t0 = DateTimeOffset.Parse("2026-01-01T00:00:00Z");
var tokens = new TokenService(Encoding.UTF8.GetBytes(
    "0123456789abcdef0123456789abcdef"));

AccessTokenContract();
TokenAudience();
TokenExpiry();
IdentifierContract();
MatchAllocation();
ServerHealth();
ProgressiveWidening();
RegionAffinity();
TicketOwnership();
ServerDrain();
IdempotentResult();
PayloadConflict();
TicketLifecycle();

Console.WriteLine("live_service_tests: all checks passed");

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

    Require(tokens.TryValidateGameToken(
            viewA.GameToken!, "audience-a", t0, out GameAdmission? admissionA) &&
        admissionA!.PlayerId == "aud-a",
        name + ": 소유 server가 game token을 연다");
    Require(!tokens.TryValidateGameToken(viewA.GameToken!, "audience-z", t0, out _),
        name + ": 다른 server ID로는 game token이 열리지 않는다");
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

// 식별자·용량·rating 범위 계약.
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
        new ServerRegistration("ident-1", "kr", "127.0.0.1:1", 0), t0),
        name + ": capacity 0 등록은 거부된다");
    Require(!plane.RegisterServer(
        new ServerRegistration("ident-1", "kr", "127.0.0.1:1", -1), t0),
        name + ": 음수 capacity 등록은 거부된다");
    Require(plane.RegisterServer(
        new ServerRegistration("ident-1", "kr", "127.0.0.1:1", 1), t0),
        name + ": 정상 등록은 통과한다");
    Require(plane.Metrics(t0).ReadyServers == 1,
        name + ": 거부된 등록은 fleet에 남지 않는다");

    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("invalid player", 1500, "kr"), t0),
        name + ": 공백이 든 player ID는 거부된다");
    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("ident-p", -1, "kr"), t0),
        name + ": 음수 rating은 거부된다");
    Expect<ArgumentException>(
        () => plane.Enqueue(new QueueRequest("ident-p", 1500, "kr", 2), t0),
        name + ": party size 2는 거부된다");
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
    Require(plane.GetTicket(w1, "widen-a", t0.AddSeconds(34))?.Status ==
        TicketStatus.Waiting,
        name + ": 34초에는 허용 폭이 440이라 아직 대기한다");
    Require(plane.GetTicket(w1, "widen-a", t0.AddSeconds(35))?.Status ==
        TicketStatus.Assigned,
        name + ": 35초에 허용 폭이 450이 되어 배정된다");
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
    Require(plane.GetTicket(mine, "owner-a", when) is not null,
        name + ": 소유자는 자기 ticket을 읽는다");
    Require(plane.GetTicket(mine, "owner-b", when) is null,
        name + ": 다른 player는 남의 ticket을 읽지 못한다");
    Require(plane.GetTicket(yours, "owner-a", when) is null,
        name + ": 반대 방향도 거부된다");
    Require(plane.GetTicket("00000000000000000000000000000000", "owner-a", when) is null,
        name + ": 없는 ticket ID는 null이다");
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
