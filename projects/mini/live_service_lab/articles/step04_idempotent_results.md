## 들어가며

게임 서버가 매치 결과를 보낸 뒤 응답을 받기 전에 연결이 끊길 수 있다. 재시도를 금지하면 결과를
잃고, 재시도를 그대로 처리하면 보상과 랭크가 두 번 반영된다. 분산 경계에서 "정확히 한 번 전송"을
기대하는 대신 여러 번 도착해도 효과는 한 번인 API를 만들어야 한다.

이 구현은 client-generated result ID를 idempotency key로 사용하고, canonical payload fingerprint와
match owner를 함께 검증한다.

시작 상태는 매치를 배정하지만 결과를 받지 못하는 core이고, 종료 상태는 result ID·fingerprint·owner
server를 함께 검사해 재시도를 한 번만 반영하고 match와 ticket을 완료로 전이시키는 core다.

---

## 목차

1. 재시도 모델
2. idempotency key와 fingerprint
3. 서버 소유권
4. match 완료 전이
5. 영속 저장소 경계
6. 실행 확인

---

## 재시도 모델

첫 요청의 가능한 결과는 세 가지다.

- 서버가 요청을 받지 못했다.
- 결과를 기록했지만 응답을 보내지 못했다.
- 기록과 응답이 모두 성공했다.

클라이언트는 timeout만으로 앞의 둘을 구분할 수 없다. 같은 result ID와 같은 payload로 재시도해야
한다. API는 처음 기록이면 `Duplicate=false`, 같은 요청의 재전송이면 `Duplicate=true` receipt를
반환한다.

## idempotency key와 fingerprint

result ID만 dictionary에 보관하면 같은 key를 다른 payload에 재사용하는 오류를 숨긴다. score map을
player ID 순서로 정렬해 canonical fingerprint를 만든다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#ResultFingerprint -->
```csharp
private static string ResultFingerprint(MatchResultRequest request)
{
    string scores = string.Join(",", request.Scores
        .OrderBy(pair => pair.Key, StringComparer.Ordinal)
        .Select(pair => $"{pair.Key}:{pair.Value}"));
    return request.MatchId + "|" + scores;
}
```

같은 result ID가 이미 있으면 fingerprint도 같아야 한다. 다르면 HTTP 409 conflict다. dictionary의
iteration order나 JSON property order를 fingerprint에 사용하지 않는 이유는 의미가 같은 payload가
serialization 순서 때문에 달라지는 것을 막기 위해서다.

기존 receipt를 반환하기 전에도 저장된 owner server ID와 호출자의 `X-Server-Id`가 같아야 한다.
중복 경로가 authorization을 건너뛰면 다른 서버가 result ID를 아는 것만으로 성공 receipt를 조회할
수 있다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#RecordResult -->
```csharp
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
```

## 서버 소유권

server key가 fleet 전체에 공유되더라도 아무 서버가 아무 match 결과를 제출해서는 안 된다. HTTP
header의 `X-Server-Id`와 allocation에 저장된 owner server ID가 같아야 한다.

score map의 key 집합도 match player 집합과 정확히 같아야 한다. 누락된 플레이어, 추가된 플레이어,
다른 match의 결과를 거부한다. 이 검사는 점수 값의 게임 규칙 검증과 별개다. 실제 서비스는 승패,
점수 상한, 서명된 match summary도 확인할 수 있다.

## match 완료 전이

새 결과의 원자적 전이는 다음 상태를 함께 바꾼다.

1. result receipt와 fingerprint를 owner server ID와 함께 저장한다.
2. match를 completed로 표시한다.
3. 그 match에 배정된 ticket을 `Completed`로 전이시킨다.
4. owner server의 active match count를 1 감소시킨다.

세 번째 전이가 없으면 ticket이 `Assigned`에 머물러 중복 억제 검사에 계속 걸리고, 매치가 끝난
플레이어가 새 ticket을 받지 못한다.

두 번째로 다른 result ID를 보내면 idempotency 재시도가 아니라 한 match의 이중 완료이므로 conflict다.
같은 result ID 재시도는 match completed 검사보다 먼저 기존 receipt를 반환해야 한다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#RecordResult -->
```csharp
results[request.ResultId] = new RecordedResult(
    receipt, fingerprint, serverId);
match.Completed = true;
CompleteTickets(request.MatchId);
if (servers.TryGetValue(match.ServerId, out ServerState? server) &&
    server.ActiveMatches > 0)
{
    server.ActiveMatches--;
}
```

## 영속 저장소 경계

process memory에서는 lock 하나가 atomicity를 제공한다. DB에서는 result insert의 unique key,
match의 completed compare-and-set, server capacity release가 한 transaction에 있어야 한다. 외부 보상
서비스로 event를 보낸다면 transaction 안에서 outbox row도 기록하고 별도 publisher가 전달해야 한다.

API transaction과 message broker publish를 순서대로 호출하면 둘 사이 crash에서 결과와 보상이
갈라진다. idempotent consumer와 outbox는 이 실패 경계를 줄이는 수단이지 네트워크를 exactly-once로
만드는 마법이 아니다.

## 실행 확인

result 계약은 contract test가 실행한다. 검사 이름을 인자로 주면 이 문서가 추가한 세 개만 돈다.

```powershell
dotnet run `
  --project projects/mini/live_service_lab/tests/LiveService.Tests/LiveService.Tests.csproj `
  -c Release -- step04
```

```text
live_service_tests: filter=step04 -> 3 check(s)
  step04 IdempotentResult    ok
  step04 PayloadConflict     ok
  step04 TicketLifecycle     ok
live_service_tests: step04 3/3 checks passed
```

`IdempotentResult`가 재시도 경로를 잡는다. 같은 요청을 두 번 보내되 두 번째는 시각을 1분 뒤로
옮긴다. receipt의 `RecordedAt`이 처음 기록한 값 그대로여야 재시도가 새로 기록한 것이 아니라는
뜻이다.

<!-- snippet: tests/LiveService.Tests/Program.cs@idempotent-retry -->
```csharp
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
```

두 번째 호출은 같은 인스턴스를 다시 넘기지 않고 내용이 같은 새 인스턴스를 만들어 넘긴다.
같은 객체를 재사용하면 fingerprint 비교가 항등식이 되어 정렬 규칙을 지워도 통과한다. 같은
검사가 뒤이어 null·빈 score map, 없는 match, allocation과 다른 player 집합, 남의 match,
이중 완료를 예외 종류까지 지정해 거부하는지 확인한다.

`PayloadConflict`는 fingerprint의 두 방향을 함께 본다. 삽입 순서만 다른 payload는 중복으로
통과해야 하고, 값이 다른 payload는 충돌이어야 한다.

<!-- snippet: tests/LiveService.Tests/Program.cs@payload-fingerprint -->
```csharp
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
```

정렬을 지우면 앞의 `try`가 잡는 예외로, fingerprint 비교 자체를 지우면 뒤의 `Expect`로
드러난다. `TicketLifecycle`은 완료 전이가 ticket까지 내려가는지 본다. 결과 기록 뒤 같은
플레이어가 새 ticket을 받고, 그 새 ticket이 완료된 match를 재사용하지 않는다.

---

## 결론

결과 API는 timeout을 정상 조건으로 가정해야 한다. 안정적인 result ID, canonical fingerprint,
allocation owner, 정확한 player 집합, 한 match의 단일 완료 전이를 검사하면 같은 요청의 재시도는
안전하고 다른 요청의 충돌은 드러난다. durable DB에서는 이 경계를 unique constraint와 transaction,
필요한 경우 outbox로 보존해야 한다.
