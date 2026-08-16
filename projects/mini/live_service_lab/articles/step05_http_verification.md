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

<!-- snippet: src/LiveService.Api/Program.cs@results-endpoint -->
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
계약 단위로 나뉜 함수가 각각 정상 경로 한 건과 실패 주입 한 건 이상을 검사한다.

검사는 이름표를 가진 표 하나로 모여 있다. 실행 단위를 step 문서 하나씩으로 가르는 장치가 이
표다. 인자를 주지 않으면 전체가, `step03`처럼 이름을 주면 그 이름의 검사만 돈다.

<!-- snippet: tests/LiveService.Tests/Program.cs@check-table -->
```csharp
(string Step, string Name, Action Run)[] checks =
[
    ("step01", "TicketOwnership", TicketOwnership),
    ("step01", "AtomicTransitions", AtomicTransitions),
    ("step01", "InjectedClock", InjectedClock),
    // ...
    ("step05", "HttpAuthBoundary", HttpAuthBoundary),
    ("step05", "HttpErrorMapping", HttpErrorMapping),
];
```

| 소속 | 검사 | 정상 경로 | 실패 주입 |
|---|---|---|---|
| step01 | `TicketOwnership` | 소유자가 자기 ticket 조회 | 다른 player와 없는 ticket ID는 null |
| step01 | `AtomicTransitions` | 동시 40건에서 match 3개와 대기 34개 | capacity 비교를 느슨하게 하면 등식이 깨짐 |
| step01 | `InjectedClock` | 세 epoch에서 같은 결과 tuple | wall clock을 읽으면 세 결과가 갈라짐 |
| step02 | `AccessTokenContract` | 발급 직후 검증 통과 | 만료 후 거부, 마지막 문자 변조 거부 |
| step02 | `TokenAudience` | 배정된 server ID로 game token 열림 | 다른 server ID 거부, player·match·token ID 공백 claim 거부 |
| step02 | `TokenExpiry` | 만료 1초 전 통과와 45초 만료 시각 일치 | 만료 시각 이후와 하루 뒤 거부 |
| step02 | `IdentifierContract` | 정상 등록 통과 | 공백·64자 초과·허용되지 않는 문자가 든 식별자 거부 |
| step03 | `RangeGuards` | capacity 1 등록 통과 | capacity 0·음수, 음수·상한 초과 rating, party size 2 거부 |
| step03 | `MatchAllocation` | 두 server가 endpoint를 각자 전달 | capacity 1 server에 4명 중 2명만 배정, queue 재시도 ticket 재사용 |
| step03 | `ServerHealth` | heartbeat가 health window 갱신 | 16초 공백 뒤 배정 거부, 늦은 heartbeat로 복구 안 됨, 신선하지 않은 후보 회피 |
| step03 | `ProgressiveWidening` | rating 차 450이 35초에 배정 | 34초에는 대기, 차 600은 100초에도 대기, 음수 경과 시간 clamp |
| step03 | `RegionAffinity` | 같은 region 쌍 배정 | 다른 region server 거부, region이 다른 두 ticket 비매칭 |
| step03 | `ServerDrain` | ready server가 match 수용 | drain 뒤 신규 배정 거부, 진행 중 match 보존 |
| step04 | `IdempotentResult` | 첫 receipt와 재시도 receipt 동일 | null·빈 score map, 없는 match, player 집합 불일치, 남의 match, 이중 완료 거부 |
| step04 | `PayloadConflict` | 삽입 순서만 다른 payload는 중복 | 같은 key에 다른 score·다른 match ID는 충돌 |
| step04 | `TicketLifecycle` | 완료 뒤 새 ticket 발급 | 새 ticket이 완료된 match를 재사용하지 않음 |
| step05 | `HttpAuthBoundary` | 맞는 server key와 bearer token은 200 | key 없음·틀린 key·변조 token·남의 ticket·`X-Server-Id` 없음 거부 |
| step05 | `HttpErrorMapping` | 첫 제출 200, 재시도 200 duplicate | 빈 score map 400, 없는 match 404, 남의 match 401, 다른 payload와 이중 완료 409 |

`name`은 각 함수 첫 줄의 상수이므로 모든 `Require` 메시지가 함수 이름으로 시작한다. 실패한 줄만
보고 어느 계약이 깨졌는지 알 수 있다. 시간을 인자로 넣었기 때문에 token 만료와 heartbeat
timeout도 테스트 시간 증가 없이 재현한다.

이 표에서 `HttpAuthBoundary`와 `HttpErrorMapping`만 socket을 연다. 나머지는 `ControlPlane`을
직접 호출하므로 앞에 실은 status code 변환 코드를 한 줄도 실행하지 않는다. 두 검사는 빌드된
API 실행 파일을 빈 loopback port에 띄우고 실제 응답 코드를 읽는다.

<!-- snippet: tests/LiveService.Tests/Program.cs@http-status-mapping -->
```csharp
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
// ...
Require(retry.Status == 200 && BoolField(retry.Body, "duplicate"),
    name + ": 같은 요청의 재시도는 409가 아니라 200 duplicate다");
```

네 요청은 같은 `/results` 경로에 거의 같은 payload를 보내고 한 군데씩만 다르다. score map을
비우면 400, match ID를 없는 값으로 바꾸면 404, `X-Server-Id`만 남의 것으로 바꾸면 401,
그대로 다시 보내면 200이다. exception 하나를 다른 status로 바꾸면 그 줄만 실패한다.

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

이 smoke는 행복 경로만 실행한다. `/results` 제출과 중복 receipt, 오류 매핑 표의 401·400·404·409
응답은 여기서 밟히지 않고 `HttpAuthBoundary`와 `HttpErrorMapping`이 맡는다. 두 검사는 게임 서버
없이 HTTP 응답 코드만 읽고, 반대로 UDP admission handshake와 교차 언어 token 검증은 이 smoke에만
있다. 남는 공백은 여러 API instance와 실제 fleet agent가 관여하는 경로다.

## 운영 범위

현재 상태는 process memory에 있고 server key 하나를 사용한다. 따라서 process restart, 다중 API
instance, key rotation, secret manager, durable queue를 해결하지 않는다. `/metrics/state`도 인증 없는
lab 관측 endpoint이므로 공개 환경 구성 그대로 사용할 수 없다.

Mega 전용 서버는 assignment game token을 실제 UDP handshake에서 오프라인 검증한다. 반면 Mega
process가 register, heartbeat, result HTTP를 직접 호출하지는 않는다. fleet agent가 이 API 호출을
소유한다는 배포 경계를 문서와 실행 명령에 그대로 남긴다.

## 실행 확인

HTTP 계약 두 검사는 API 실행 파일을 스스로 띄운다. test project가 API project를 참조하므로
같은 구성으로 함께 빌드되고, 준비 명령은 따로 없다.

```powershell
dotnet run `
  --project projects/mini/live_service_lab/tests/LiveService.Tests/LiveService.Tests.csproj `
  -c Release -- step05
```

```text
live_service_tests: filter=step05 -> 2 check(s)
  step05 HttpAuthBoundary    ok
  step05 HttpErrorMapping    ok
live_service_tests: step05 2/2 checks passed
```

각 검사는 빈 loopback port를 잡고, 임시 secret과 임시 server key를 환경 변수로 넘기고,
`/metrics/state`가 200을 낼 때까지 짧게 polling한 뒤 요청을 보낸다. `finally`에서 프로세스를
반드시 죽이므로 실패한 검사가 포트를 붙들지 않는다. 인자를 빼면 18개 검사가 모두 돌고 마지막
줄이 `live_service_tests: all checks passed`가 된다.

손으로 띄워 볼 때는 API를 Release로 빌드하고 loopback에 붙인다.

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
