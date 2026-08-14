## 들어가며

core 상태 기계가 맞아도 HTTP adapter가 identity를 request body에서 읽거나, server header 하나를
빼먹거나, exception을 모두 500으로 바꾸면 외부 계약은 깨진다. 검증은 순수 core와 실제 HTTP
process를 나누고, 인증 실패·소유권·중복 요청의 status code까지 확인해야 한다.

이 API는 ASP.NET Core 최소 endpoint로 구성되며 외부 package 없이 실행된다. HTTP는 core에 현재
시각과 인증된 주체를 전달하고 domain 결과를 status code로 변환한다.

시작 상태는 HTTP 없이 함수 호출로만 쓰는 core이고, 종료 상태는 login·register·queue·assignment·
result·metrics endpoint를 제공하는 실행 가능한 API와 그 API를 실제 process로 띄우는 smoke다.

---

## 목차

1. HTTP 인증 경계
2. 오류 매핑
3. core 결정적 테스트
4. process smoke
5. 운영 범위
6. 실행 확인

---

## HTTP 인증 경계

플레이어 endpoint는 `Authorization: Bearer`에서 access token을 검증해 player ID를 얻는다. `/queue`
body에는 rating과 region만 있으며 player ID를 받지 않는다. assignment 조회도 token subject와 ticket
owner가 같아야 한다.

서버 endpoint는 `X-Server-Key`를 constant-time으로 비교한다. 결과 제출은 여기에 `X-Server-Id`도
요구해 allocation owner와 연결한다.

```csharp
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
```

header 값은 `StringValues`이므로 `ToString()`으로 core의 string 계약에 맞춘다. header가 있어도
값이 공백이면 401이다.

lab의 `/login`은 password를 확인하지 않는다. 허용된 ASCII player ID를 local identity로 보고 access
token을 발급한다. OAuth/OIDC, password hashing, MFA를 구현한 endpoint로 오해하지 않도록 실행 범위에
명시한다.

## 오류 매핑

domain exception을 모두 같은 status로 바꾸면 client가 재시도 가능한 실패와 요청 오류를 구분하지
못한다.

| 조건 | HTTP 의미 |
|---|---|
| token 또는 server key 실패 | 401 Unauthorized |
| 식별자·rating·payload 형식 오류 | 400 Bad Request |
| ticket·match·server 없음 | 404 Not Found |
| register 충돌, idempotency payload 충돌, 이중 완료 | 409 Conflict |
| 정상 최초·중복 결과 | 200 OK와 receipt |

중복 result는 409가 아니다. 같은 idempotency key와 같은 payload는 성공한 작업의 receipt를 다시
받는 정상 재시도다.

## core 결정적 테스트

core test는 고정된 `DateTimeOffset`과 32바이트 test secret을 사용한다. sleep이나 실제 port 없이
계약 단위로 나뉜 13개 함수가 각각 정상 경로 한 건과 실패 주입 한 건 이상을 검사한다.

| 함수 | 정상 경로 | 실패 주입 |
|---|---|---|
| `AccessTokenContract` | 발급 직후 검증 통과 | 만료 후 거부, 마지막 문자 변조 거부 |
| `TokenAudience` | 배정된 server ID로 game token 열림 | 다른 server ID 거부, player·match·token ID 공백 claim 거부 |
| `TokenExpiry` | 만료 1초 전 통과와 45초 만료 시각 일치 | 만료 시각 이후와 하루 뒤 거부 |
| `IdentifierContract` | 정상 등록 통과 | 공백 식별자, capacity 0·음수, 음수 rating, party size 2 거부 |
| `MatchAllocation` | 두 server가 endpoint를 각자 전달 | capacity 1 server에 4명 → 2명만 배정, queue 재시도 ticket 재사용 |
| `ServerHealth` | heartbeat가 health window 갱신 | 16초 공백 뒤 배정 거부, 늦은 heartbeat로 복구 안 됨, 신선하지 않은 후보 회피 |
| `ProgressiveWidening` | rating 차 450이 35초에 배정 | 34초에는 대기, 차 600은 100초에도 대기, 음수 경과 시간 clamp |
| `RegionAffinity` | 같은 region 쌍 배정 | 다른 region server 거부, region이 다른 두 ticket 비매칭 |
| `TicketOwnership` | 소유자가 자기 ticket 조회 | 다른 player와 없는 ticket ID는 null |
| `ServerDrain` | ready server가 match 수용 | drain 뒤 신규 배정 거부, 진행 중 match 보존 |
| `IdempotentResult` | 첫 receipt와 재시도 receipt 동일 | null·빈 score map, 없는 match, player 집합 불일치, 남의 match, 이중 완료 거부 |
| `PayloadConflict` | 삽입 순서만 다른 payload는 중복 | 같은 key에 다른 score·다른 match ID는 충돌 |
| `TicketLifecycle` | 완료 뒤 새 ticket 발급 | 새 ticket이 완료된 match를 재사용하지 않음 |

```csharp
ResultReceipt first = plane.RecordResult(request, "result-owner", now);
ResultReceipt retry = plane.RecordResult(sameContentDifferentInstance, "result-owner", later);
Require(!first.Duplicate && retry.Duplicate, "IdempotentResult: 재시도는 처음 기록한 receipt를 그대로 돌려준다");
```

각 `Require` 메시지는 함수 이름으로 시작하므로 실패한 줄만 보고 어느 계약이 깨졌는지 알 수 있다.
중복 receipt 검사는 같은 인스턴스를 두 번 넘기지 않고 내용이 같은 새 인스턴스를 만들어 넘긴다.
같은 객체를 재사용하면 fingerprint 비교가 항등식이 되어 정렬 규칙을 지워도 통과한다.

시간을 인자로 넣었기 때문에 token 만료와 heartbeat timeout을 테스트 시간 증가 없이 재현할 수 있다.

## process smoke

실제 HTTP smoke는 `scripts/check_live_mega.ps1`이 실행한다. API를 loopback 숨김 process로 띄운 뒤
전용 게임 서버 process까지 연결하는 교차 언어 smoke이며 다음을 수행한다.

1. server key로 UDP endpoint와 capacity 1을 등록한다.
2. 두 player의 access token을 발급한다.
3. 두 bearer token으로 queue에 들어간다.
4. 두 assignment의 match ID가 같은지 확인한다.
5. 두 game token을 admission 패킷에 담아 전용 서버에 보낸다.
6. 두 client가 모두 snapshot을 받는지 확인한다.

API ready를 고정 sleep 하나로 가정하지 않고 `/metrics/state`를 짧게 polling한다. test 종료의 `finally`
블록은 process를 정리해 실패한 smoke가 개발 포트를 계속 점유하지 않게 한다.

이 smoke는 행복 경로만 실행한다. `/results` 제출과 중복 receipt, 그리고 위 오류 매핑 표의 401·400·
404·409 응답은 자동으로 확인되지 않는 범위다. core 테스트는 `ControlPlane`을 직접 호출하므로 HTTP
status 변환 코드를 실행하지 않는다.

## 운영 범위

현재 상태는 process memory에 있고 server key 하나를 사용한다. 따라서 process restart, 다중 API
instance, key rotation, secret manager, durable queue를 해결하지 않는다. `/metrics/state`도 인증 없는
lab 관측 endpoint이므로 공개 환경 구성 그대로 사용할 수 없다.

Mega 전용 서버는 assignment game token을 실제 UDP handshake에서 오프라인 검증한다. 반면 Mega
process가 register, heartbeat, result HTTP를 직접 호출하지는 않는다. fleet agent가 이 API 호출을
소유한다는 배포 경계를 문서와 실행 명령에 그대로 남긴다.

## 실행 확인

API를 Release로 빌드하고 loopback에 띄운다.

```powershell
dotnet build `
  projects/mini/live_service_lab/src/LiveService.Api/LiveService.Api.csproj `
  -c Release

$env:LIVE_SERVICE_SERVER_KEY = 'local-server-key'
dotnet run `
  --project projects/mini/live_service_lab/src/LiveService.Api/LiveService.Api.csproj `
  --urls http://127.0.0.1:5088
```

다른 shell에서 관측 endpoint를 호출하면 기동 직후 계수가 모두 0이다.

```powershell
(Invoke-WebRequest http://127.0.0.1:5088/metrics/state).Content
```

```text
{"readyServers":0,"drainingServers":0,"waitingTickets":0,"activeMatches":0,"recordedResults":0}
```

허용되지 않는 문자가 들어간 player ID로 `/login`을 호출하면 400이 돌아온다. server를 등록하고 두
플레이어가 queue에 들어가면 같은 호출의 `readyServers`가 1, `activeMatches`가 1로 바뀐다.

---

## 결론

제어 평면 검증은 domain 함수 반환값에서 끝나지 않는다. HTTP adapter가 인증된 identity를 올바르게
전달하고, 충돌과 부재를 구분하며, 실제 process에서 registration부터 idempotent result까지 이어져야
한다. 구현하지 않은 credential provider, durable store, fleet agent를 명시하면 동작하는 lab과
production 서비스 사이의 경계도 검증 결과에 포함된다.
