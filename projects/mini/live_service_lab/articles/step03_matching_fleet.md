## 들어가며

매치메이킹은 점수 차가 작은 두 사람을 고르는 정렬 문제가 아니다. 대기 시간이 길어질수록 품질
조건을 완화해야 하고, 같은 region의 건강한 서버에 capacity를 넘지 않게 배치해야 하며, HTTP
재시도가 ticket과 match를 중복 생성하지 않아야 한다. 검색 품질과 자원 할당이 하나의 상태 전이에서
만난다.

이 구현은 2인 매치, 단일 region 선호, 점진적 MMR 범위, 최소 부하 서버 선택을 결정적인 순서로
구성한다.

시작 상태는 ticket과 server를 저장만 하고 배정하지 않는 core이고, 종료 상태는 중복 억제·범위 확대·
health·drain·region·capacity 필터를 모두 통과한 서버에 두 ticket을 배정하는 `TryMatch` 경로다.

---

## 목차

1. ticket 중복 억제
2. 검색 범위 확대
3. 서버 health와 drain
4. capacity allocation
5. 공정성과 한계
6. 실행 확인

---

## ticket 중복 억제

모바일 네트워크 timeout 뒤 클라이언트가 `/queue`를 재시도할 수 있다. 첫 요청이 성공했지만 응답만
잃었다면 새 ticket을 만들면 한 플레이어가 두 match에 배정될 수 있다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#Enqueue -->
```csharp
if (playerTickets.TryGetValue(request.PlayerId, out string? existingId) &&
    tickets.TryGetValue(existingId, out TicketState? existing) &&
    existing.Status is TicketStatus.Waiting or TicketStatus.Assigned)
{
    return existing.Id;
}
```

deduplication key는 인증된 player ID다. request body의 임의 player ID를 믿지 않고 bearer token의
subject로 `QueueRequest`를 만든다.

## 검색 범위 확대

대기열은 enqueue 시각 순서로 스캔한다. 두 플레이어의 rating gap 허용치는 100에서 시작하고 대기
1초당 10씩 증가해 최대 500에서 멈춘다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#TryMatch -->
```csharp
double waitedSeconds = Math.Max(0, Math.Min(
    (now - first.EnqueuedAt).TotalSeconds,
    (now - second.EnqueuedAt).TotalSeconds));
int allowedGap = 100 + Math.Min(400, (int)(waitedSeconds * 10));
```

두 사람 중 짧게 기다린 시간을 사용하는 이유는 오래 기다린 한 사람이 방금 들어온 상대의 품질
조건을 즉시 최대 폭으로 끌어올리지 않게 하기 위해서다. 이 정책은 유일한 정답이 아니라 명시적으로
측정 가능한 선택이다.
wall clock 보정으로 `now`가 enqueue 시각보다 작아질 수 있으므로 elapsed time은 0 아래로 내려가지
않게 clamp한다. 그렇지 않으면 기본 허용 폭 100도 음수로 줄어 정상 후보를 거부한다.

region이 다르면 후보가 아니다. 실제 서비스에서는 latency probe와 cross-region fallback이 필요하지만,
클라이언트가 보낸 region 문자열만으로 대륙 간 매치를 만드는 것보다 단일 region 계약을 먼저
검증한다.

## 서버 health와 drain

서버 register는 endpoint, region, capacity, heartbeat 시각을 기록하고 Ready 상태로 만든다. Ready
서버가 15초 동안 heartbeat를 보내지 않으면 Unhealthy로 바뀐다. heartbeat가 늦게 한 번 도착했다고
자동 Ready로 되돌리지 않는다. 운영 주체가 register로 준비 상태를 다시 선언해야 한다.

drain은 신규 allocation을 멈추지만 active match count를 보존한다. 배포 교체 시 진행 중 매치가
끝날 시간을 주기 위한 상태다.

서버 후보 조건은 모두 참이어야 한다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#TryMatch -->
```csharp
.Where(candidate =>
    candidate.Lifecycle == ServerLifecycle.Ready &&
    candidate.Region == first.Region &&
    now - candidate.LastHeartbeat <= ServerHealthTimeout &&
    candidate.ActiveMatches < candidate.Capacity)
```

`first`는 짝을 이루는 두 ticket 중 대기열에서 먼저 스캔된 쪽이다. 두 ticket의 region이 이미 같아야
짝이 되므로 한쪽 region만 서버 조건에 넣으면 충분하다.

## capacity allocation

capacity는 player slot 수가 아니라 동시에 소유할 match 수다. 2인 전용 process를 하나씩 실행하는
구성에서는 1로 등록한다. multi-match process라면 실제 격리 단위 수를 넣는다.

후보는 active match가 적은 순서, 동률이면 server ID 순서로 선택한다. 두 번째 정렬 기준은 테스트와
장애 분석에서 같은 입력이 같은 결정을 만들게 한다.

서버 count 증가, match 생성, 두 ticket assignment는 같은 lock 안에서 수행한다. check와 increment를
분리하면 동시 요청 둘이 마지막 한 슬롯을 함께 볼 수 있다.

<!-- snippet: src/LiveService.Core/ControlPlane.cs#TryMatch -->
```csharp
server.ActiveMatches++;
matches[matchId] = new MatchState(
    matchId, server.Id, [first.PlayerId, second.PlayerId]);
Assign(first, matchId, server, now);
Assign(second, matchId, server, now);
```

각 플레이어는 같은 endpoint와 match ID를 받지만 game token은 서로 다른 player ID와 token ID를
가진다.

## 공정성과 한계

현재 O(n²) 대기열 스캔은 작은 lab에서 정책을 드러내기 위한 구현이다. 대규모 queue에서는 rating
bucket, region partition, party constraint, ticket lease가 필요하다. 단순 자료구조 교체만으로 해결되지
않으며 검색 품질 지표와 starvation 정책을 함께 정의해야 한다.

서버 상태도 process memory에 있으므로 API restart 뒤 allocation을 잃는다. durable DB를 붙일 때는
server lease, ticket claim, match insert를 transaction 또는 compare-and-swap으로 다시 표현해야 한다.

## 실행 확인

배정 정책은 시각을 인자로 넣는 contract test가 실행한다. 검사 이름을 인자로 주면 이 문서가
추가한 여섯 개만 돈다.

```powershell
dotnet run `
  --project projects/mini/live_service_lab/tests/LiveService.Tests/LiveService.Tests.csproj `
  -c Release -- step03
```

```text
live_service_tests: filter=step03 -> 6 check(s)
  step03 RangeGuards         ok
  step03 MatchAllocation     ok
  step03 ServerHealth        ok
  step03 ProgressiveWidening ok
  step03 RegionAffinity      ok
  step03 ServerDrain         ok
live_service_tests: step03 6/6 checks passed
```

`RangeGuards`는 capacity 0·음수 등록과 음수·상한 초과 rating, party size 2를 거부하는지 본다.
`MatchAllocation`은 capacity 1인 서버 두 대에 네 명을 넣어 각 쌍이 서로 다른 match와 서로 다른
endpoint를 받는 것, queue 재시도가 ticket을 늘리지 않는 것을 확인한다. `ServerHealth`는 16초
공백 뒤 배정 거부와 늦은 heartbeat 한 번으로 복구되지 않는 것을, `ServerDrain`은 drain 뒤 신규
배정 거부와 진행 중 match 보존을, `RegionAffinity`는 region이 어긋난 ticket·server 조합을 잡는다.

범위 확대는 경계 두 개를 붙여 재현한다. 34초와 35초는 허용 폭 440과 450으로 갈리므로 rating
차 450인 두 ticket의 상태가 이 1초 사이에 바뀐다.

<!-- snippet: tests/LiveService.Tests/Program.cs@widening-boundary -->
```csharp
Require(plane.GetTicket(w1, "widen-a", t0.AddSeconds(34))?.Status ==
    TicketStatus.Waiting,
    name + ": 34초에는 허용 폭이 440이라 아직 대기한다");
Require(plane.GetTicket(w1, "widen-a", t0.AddSeconds(35))?.Status ==
    TicketStatus.Assigned,
    name + ": 35초에 허용 폭이 450이 되어 배정된다");
```

한쪽만 검사하면 확대가 아예 없어도, 처음부터 무한대여도 통과한다. 두 줄을 붙여야 시작값
100과 기울기 10이 함께 측정된다. 상한 500과 음수 경과 시간 clamp는 같은 검사의 뒷부분에서
rating 차 600인 쌍과 `now`가 enqueue 시각보다 이른 쌍으로 확인한다.

---

## 결론

매치메이킹은 품질 함수와 fleet capacity의 결합 문제다. player ticket을 중복 억제하고, 기다린 시간에
따라 rating 범위를 제한적으로 넓히고, health·drain·region·capacity를 모두 통과한 서버에 원자적으로
배치해야 한다. 정책 숫자는 바뀔 수 있지만 중복 배정과 capacity 초과를 막는 불변식은 유지되어야 한다.
