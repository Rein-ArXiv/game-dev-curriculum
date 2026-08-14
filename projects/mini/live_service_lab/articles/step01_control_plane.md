## 들어가며

라이브 서비스는 게임 서버의 tick loop가 아니다. 플레이어를 인증된 세션으로 바꾸고, 대기열에서
매치를 만들고, 수용 가능한 서버를 선택하고, 한 번 끝난 결과를 중복 반영하지 않는 제어 평면이다.
실시간 패킷과 처리 시간·실패 모델이 다르므로 별도 프로세스와 상태 기계로 두어야 한다.

이 구현은 HTTP를 제거한 순수 `ControlPlane` core와 얇은 API 경계를 나눈다. 모든 시간 의존 함수는
현재 시각을 인자로 받아 만료와 heartbeat를 결정적으로 검증한다.

시작 상태는 제어 상태가 전혀 없는 빈 프로젝트이고, 종료 상태는 server·ticket·match·result를 독립
dictionary로 두고 시간을 인자로 받으며 단일 lock으로 전이를 원자화하는 `ControlPlane` core다.

---

## 목차

1. 제어 평면과 데이터 평면
2. 상태 모델
3. 명시적 시간
4. 동시성 경계
5. 불변식
6. 실행 확인

---

## 제어 평면과 데이터 평면

데이터 평면은 수십 Hz로 input과 snapshot을 처리한다. 제어 평면은 로그인, queue, allocation,
heartbeat, result처럼 초 단위 작업을 처리한다. 둘을 한 process에 넣으면 match queue의 느린 저장소
I/O가 tick을 막거나, 게임 서버 crash가 인증·매칭 상태까지 잃게 된다.

두 평면의 계약은 작다.

```text
allocation = endpoint + match_id + server_id + short_lived_game_token
```

클라이언트는 endpoint로 접속하고 game token을 제시한다. 게임 서버는 match 결과에 idempotency key를
붙여 제어 평면에 제출한다. 제어 평면은 플레이 중 위치나 projectile을 소유하지 않는다.

## 상태 모델

core는 server, ticket, match, result를 독립 상태로 두고, 플레이어별 active ticket을 찾는 역색인을
하나 더 가진다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs@state-maps -->
```csharp
private readonly Dictionary<string, ServerState> servers = new(StringComparer.Ordinal);
private readonly Dictionary<string, TicketState> tickets = new(StringComparer.Ordinal);
private readonly Dictionary<string, string> playerTickets = new(StringComparer.Ordinal);
private readonly Dictionary<string, MatchState> matches = new(StringComparer.Ordinal);
private readonly Dictionary<string, RecordedResult> results = new(StringComparer.Ordinal);
```

key 비교는 모두 `StringComparer.Ordinal`이다. 식별자는 표시용 문자열이 아니라 바이트 계약이므로
문화권별 대소문자·정규화 규칙이 개입해서는 안 된다.

server lifecycle은 `Ready`, `Draining`, `Unhealthy`다. draining 서버는 진행 중 매치를 끝낼 수 있지만
새 allocation을 받지 않는다. unhealthy는 heartbeat timeout을 넘긴 상태다. 복구는 단순 heartbeat가
아니라 register로 명시적으로 Ready 상태를 다시 선언한다.

ticket은 `Waiting`, `Assigned`, `Completed`, `Cancelled`다. 같은 플레이어의 active ticket은
`Waiting`이나 `Assigned` 상태 하나뿐이다. HTTP 재시도가 queue entry를 여러 개 만들지 않도록
`playerTickets` 역색인을 사용한다. match result가 기록되면 그 match의 ticket은 `Completed`로
전이해 active ticket이 아니게 되고, 같은 플레이어가 새 ticket으로 다시 queue에 들어갈 수 있다.
`Cancelled`는 선언된 상태이지만 이 구현에는 클라이언트 취소 경로가 없어 전이가 일어나지 않는다.

match는 서버 owner와 정확한 플레이어 집합을 보존한다. result 제출자는 owner server여야 하고,
score key 집합도 allocation의 플레이어 집합과 같아야 한다.

## 명시적 시간

`DateTimeOffset.UtcNow`를 core 내부에서 직접 읽으면 token expiry와 heartbeat timeout 테스트가
sleep에 의존한다. 대신 public 상태 전이는 `now`를 받는다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#GetTicket -->
```csharp
public TicketView? GetTicket(string ticketId, string playerId, DateTimeOffset now)
{
    lock (gate)
    {
        MarkUnhealthyServers(now);
        TryMatch(now);
        // ... 소유권 검사와 view 투영
    }
}
```

테스트는 같은 입력에 `now`, `now + 16초`, `now + 2분`을 넣어 서버 health와 token 만료를 즉시
검증할 수 있다. wall clock은 API adapter가 한 번 읽어 core에 전달한다.

## 동시성 경계

이 lab은 process memory 하나를 사용하므로 coarse lock 하나로 여러 dictionary의 전이를 원자화한다.
match 생성은 서버 capacity 증가, match 삽입, 두 ticket assignment를 같은 critical section에서
수행한다. 중간 상태를 다른 요청이 관측하지 않는다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#TryMatch -->
```csharp
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
```

이 코드는 `TryMatch`의 본문이고 `lock (gate)`는 공개 진입점인 `RegisterServer`, `Enqueue`,
`GetTicket`이 잡는다. capacity 검사는 별도의 early return이 아니라 후보 서버 필터의 술어
`candidate.ActiveMatches < candidate.Capacity`이므로 check와 increment가 같은 critical section
안에 있다.

이 선택은 분산 환경의 transaction을 해결하지 않는다. process가 죽으면 상태가 사라진다. 목적은
상태 전이 불변식을 먼저 고정하고, durable store가 필요로 하는 atomic boundary를 드러내는 것이다.

## 불변식

- 한 플레이어에게 waiting 또는 assigned ticket은 최대 하나다.
- Ready이고 heartbeat가 유효한 서버만 allocation을 받는다.
- `ActiveMatches < Capacity`일 때만 match를 추가한다.
- 두 ticket, match, server count는 한 lock 안에서 함께 바뀐다.
- ticket은 bearer token의 player ID와 일치할 때만 조회된다.
- 완료 match는 결과를 한 번만 가진다.

이 불변식은 API 응답 모양보다 중요하다. endpoint를 추가해도 owner와 capacity 전이가 원자적이지
않으면 같은 슬롯을 여러 match에 줄 수 있다.

## 실행 확인

core의 상태 전이는 고정된 `DateTimeOffset`을 넣는 contract test가 실행한다.

```powershell
dotnet run `
  --project projects/mini/live_service_lab/tests/LiveService.Tests/LiveService.Tests.csproj `
  -c Release
```

```text
live_service_tests: all checks passed
```

이 실행에는 실제 port도 sleep도 없다. 상태 전이가 깨지면 실패한 검사 이름이 예외 메시지로
출력되고 프로세스 종료 코드가 0이 아니다.

---

## 결론

라이브 서비스는 실시간 서버의 부속 endpoint가 아니라 별도 시간과 실패 모델을 가진 제어 평면이다.
server·ticket·match·result 상태를 분리하고, 시간을 주입하고, 여러 상태의 전이를 한 atomic boundary에
두면 매칭과 allocation의 정확성을 검증할 수 있다. process memory라는 한계도 명시해 상태 기계와
production durability를 혼동하지 않는다.
