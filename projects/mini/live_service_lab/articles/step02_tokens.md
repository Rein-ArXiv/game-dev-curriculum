## 들어가며

access token과 game token은 모두 문자열이지만 권한과 audience가 다르다. access token은 API에서
플레이어 identity를 증명하고, game token은 특정 플레이어가 특정 매치의 특정 서버에 짧은 시간 안에
입장할 권한을 증명한다. 하나의 범용 token으로 합치면 게임 서버가 계정 API 권한까지 받아 버린다.

이 구현은 JSON payload를 base64url로 인코딩하고 HMAC-SHA256으로 서명한다. 암호화가 아니라 무결성과
발급자 인증을 제공하는 대칭키 token이다.

시작 상태는 자격 증명 없이 상태 전이만 하는 `ControlPlane` core이고, 종료 상태는 access token과
game token을 발급·검증하는 `TokenService`가 core에 들어간 상태다.

---

## 목차

1. token 형식
2. access와 game claim
3. 서명 검증
4. 만료와 audience
5. 식별자 계약과 키 관리
6. 실행 확인

---

## token 형식

형식은 두 구간이다.

```text
base64url(utf8_json) + "." + base64url(hmac_sha256(secret, body))
```

서명 입력은 decoded JSON이 아니라 점 앞의 ASCII body다. 검증자가 JSON을 다시 serialize하면 property
순서, 공백, escape 차이로 서명이 달라질 수 있다. 받은 body bytes 그대로 HMAC에 넣은 뒤에만 JSON을
해석한다.

<!-- snippet: src/LiveService.Core/TokenService.cs@sign-body -->
```csharp
byte[] json = JsonSerializer.SerializeToUtf8Bytes(payload);
string body = Base64UrlEncode(json);
byte[] signature = HMACSHA256.HashData(secret, Encoding.ASCII.GetBytes(body));
return body + "." + Base64UrlEncode(signature);
```

## access와 game claim

access payload는 kind, player ID, expiry만 가진다. API의 queue와 assignment 조회는 bearer token에서
꺼낸 player ID를 resource owner와 비교한다.

game payload에는 권한을 더 좁히는 claim이 들어간다.

<!-- snippet: src/LiveService.Core/TokenService.cs#IssueGameToken -->
```csharp
var payload = new GamePayload(
    "game",
    admission.PlayerId,
    admission.MatchId,
    admission.ServerId,
    admission.TokenId,
    admission.ExpiresAt.ToUnixTimeSeconds());
```

`ServerId`는 audience다. 다른 서버가 같은 fleet secret을 알아도 자기 ID가 아닌 token을 받아서는
안 된다. `TokenId`는 한 자격 증명이 두 UDP endpoint에 재사용되는 것을 게임 서버가 탐지할 수 있게
한다. `MatchId`는 한 process에 서로 다른 match의 플레이어가 섞이는 것을 막는다.

## 서명 검증

검증은 token에 점이 정확히 하나인지 확인하고 두 구간을 base64url decode한다. supplied signature와
expected signature는 일반 문자열 비교 대신 constant-time API로 비교한다.

<!-- snippet: src/LiveService.Core/TokenService.cs@verify-signature -->
```csharp
byte[] expected = HMACSHA256.HashData(secret, Encoding.ASCII.GetBytes(body));
if (supplied.Length != expected.Length ||
    !CryptographicOperations.FixedTimeEquals(supplied, expected)) return false;
```

길이 검사를 먼저 하는 것은 API 요구사항이다. 같은 길이의 비교에서는 첫 불일치 위치에 따라 loop가
일찍 끝나지 않는다. HMAC을 검증하기 전에 payload의 claim을 신뢰하지 않는다.

## 만료와 audience

game token 수명은 45초다. assignment를 탈취한 값이 오래 재사용되는 시간을 제한하면서 queue 응답
뒤 실제 서버 접속 시간을 허용하는 실습 값이다. 서버 시계가 심하게 어긋나면 정상 token도 실패하므로
운영에서는 clock synchronization과 작은 허용 오차 정책이 필요하다.

<!-- snippet: src/LiveService.Core/TokenService.cs#TryValidateGameToken -->
```csharp
if (!TryVerify(token, out GamePayload? payload) || payload is null ||
    payload.Kind != "game" || payload.ServerId != expectedServerId ||
    payload.ExpiresAt <= now.ToUnixTimeSeconds() ||
    string.IsNullOrWhiteSpace(payload.PlayerId) ||
    string.IsNullOrWhiteSpace(payload.MatchId) ||
    string.IsNullOrWhiteSpace(payload.TokenId))
{
    return false;
}
```

만료 검사는 `<=`다. expiry 초와 현재 초가 같으면 이미 유효 구간이 끝났다. 서명이 맞아도 player,
match, token ID claim이 비어 있으면 거부한다. HMAC은 발급자만 증명할 뿐 claim이 쓸 수 있는 값인지는
보장하지 않는다.

## 식별자 계약과 키 관리

Mega의 작은 C++ token parser와 JSON escape 규칙이 달라지지 않도록 player, server, region ID는 ASCII
영숫자와 `.`, `-`, `_`만 허용한다. 최대 길이도 player/server 64, region 32로 제한한다. display
name은 별도 데이터이며 인증 subject와 섞지 않는다.

secret은 최소 32바이트다. 환경 변수에는 raw 문자열이 아니라 Base64를 넣는다. 로컬 고정 secret은
두 lab의 smoke를 연결하기 위한 값일 뿐 공개 배포 credential이 아니다. 실제 운영에는 무작위 키,
secret store, rotation, key ID가 필요하다.

HMAC은 payload를 숨기지 않는다. token을 본 사람은 JSON을 읽을 수 있다. 민감 정보를 claim에 넣지
않고 TLS로 전송 구간을 보호해야 한다.

## 실행 확인

token 계약은 고정된 32바이트 test secret을 쓰는 contract test가 실행한다. 검사 이름을 인자로
주면 이 문서가 추가한 네 개만 돈다.

```powershell
dotnet run `
  --project projects/mini/live_service_lab/tests/LiveService.Tests/LiveService.Tests.csproj `
  -c Release -- step02
```

```text
live_service_tests: filter=step02 -> 4 check(s)
  step02 AccessTokenContract ok
  step02 TokenAudience       ok
  step02 TokenExpiry         ok
  step02 IdentifierContract  ok
live_service_tests: step02 4/4 checks passed
```

`AccessTokenContract`는 발급 직후 검증 통과, 만료 뒤 거부, 마지막 문자를 바꾼 token의 거부를
차례로 확인한다. `TokenExpiry`는 만료 시각이 배정 시각 + 45초라는 값 자체를 비교하고 만료
1초 전·1초 후·하루 뒤를 함께 넣는다. `IdentifierContract`는 공백·64자 초과·허용되지 않는
문자가 든 식별자를 player와 server 양쪽에서 거부하는지 본다.

audience 검사는 같은 token 문자열에 `expectedServerId`만 바꿔 넣는 두 줄이다.

<!-- snippet: tests/LiveService.Tests/Program.cs@game-token-audience -->
```csharp
Require(tokens.TryValidateGameToken(
        viewA.GameToken!, "audience-a", t0, out GameAdmission? admissionA) &&
    admissionA!.PlayerId == "aud-a",
    name + ": 소유 server가 game token을 연다");
Require(!tokens.TryValidateGameToken(viewA.GameToken!, "audience-z", t0, out _),
    name + ": 다른 server ID로는 game token이 열리지 않는다");
```

첫 줄만 있으면 audience 검사를 통째로 지워도 통과한다. 둘째 줄이 있어야 "같은 fleet secret을
아는 다른 서버가 남의 token을 열 수 없다"가 실제로 측정된다. 나머지 검사도 같은 방식으로
정상 경로 한 건과 실패 주입 한 건 이상을 짝지어 둔다.

---

## 결론

token 보안은 HMAC 호출 한 번보다 claim의 권한 범위를 줄이는 데 달려 있다. access와 game token을
분리하고, 원본 body를 서명하고, signature를 상수 시간으로 비교하고, expiry·audience·token ID를
검사해야 한다. 식별자와 키 형식까지 소비자 사이의 명시적 계약으로 만들면 C# 제어 평면과 C++ 게임
서버가 같은 자격 증명을 해석할 수 있다.
