## 들어가며

실시간 멀티플레이는 서버가 판정할 때까지 기다리면 반응성이 무너지고, 클라이언트를 믿으면 공정성과 정합성이 무너진다. 권위 서버와 클라이언트 표현 사이에 예측, 보정, 보간, 과거 상태 판정이라는 시간 모델이 필요하다.

이 글은 서버 틱과 입력 명령, client prediction과 reconciliation, 다른 엔티티의 interpolation·extrapolation, lag compensation을 하나의 권위와 시간축 설계로 묶는다.

---

## 목차

1. 왜 예측이 필요한가
2. 입력 Command와 Sequence
3. 로컬 예측
4. 서버 Reconciliation
5. Replay 비용과 최적화
6. 오차 보정과 Smoothing
7. 무엇을 예측하고 무엇을 예측하지 않는가
8. 예측 실패를 줄이는 조건
9. 원격 엔티티가 끊기는 이유
10. 서버 시간 추정: 보간의 시계
11. Snapshot History Buffer
12. Interpolation Delay
13. 회전과 비위치 상태의 보간
14. Extrapolation의 한계
15. Smoothing과 Teleport Threshold
16. 애니메이션과 시각 보정
17. 지연 보상이 필요한 이유
18. 서버 History Buffer
19. Rewind Time의 계산
20. Hit Rewind 절차
21. Hitscan과 Projectile
22. 공정성의 트레이드오프
23. 악용 방지
24. 검증과 디버깅
25. Tick이 기준 시계인 이유
26. 틱레이트의 선택
27. Fixed Timestep Server
28. Input Tick과 Command Buffer
29. 클라이언트는 서버보다 앞서 달린다
30. Render Tick과 Snapshot Tick
31. Clock Sync와 Drift Correction
32. Tick Budget
33. 권위의 위치
34. 권위의 스펙트럼: 전부 아니면 전무가 아니다
35. 서버 중심 시뮬레이션 루프
36. 지연을 숨기는 계층
37. 한 발의 총알: 전체 흐름 추적
38. 보안 경계
39. 서버 코드의 구조적 요구
40. 확장성과 운영 구조
41. 선택 기준

---


## 1. 왜 예측이 필요한가

서버 권위 입력 흐름은 단순하다.

```text
client input -> network -> server simulation -> snapshot -> network -> client render
```

이 흐름을 그대로 따르면 local input의 권위 결과가 돌아오는 데 network 왕복, server tick 대기, snapshot 송신과 render queue가 더해진다. 60Hz의 한 tick은 약 16.67ms지만 체감 임계는 장르, 입력 장치, animation, frame pacing과 플레이어 기대에 따라 달라진다. 하나의 인간 지각 임계값으로 시스템을 설계하지 않고 end-to-end input-to-photon 분포를 측정한다.

Client prediction은 입력을 서버에 보내는 동시에 로컬에도 적용한다.

```text
client input -> local prediction -> immediate render
client input -> network -> server authority -> reconciliation
```

플레이어는 즉시 움직이는 것처럼 느낀다. 서버가 나중에 권위 상태를 보내면 예측이 맞았는지 확인한다.

역사적 맥락이 이 구조의 가치를 보여준다. QuakeWorld(1996)가 이 기법을 처음 대중화하기 전, 네트워크 게임의 이동은 전부 "서버 왕복 후 반영"이었고 모뎀 핑 200ms+에서는 사실상 플레이가 불가능했다. 예측의 도입으로 *내 캐릭터에 한해서는* 핑이 0인 것처럼 만들 수 있게 됐고, 이후 권위 서버 액션 게임의 표준 구조가 되었다. 중요한 한정 — "내 캐릭터에 한해서"다. 상대는 입력을 알 수 없어 보간된 과거로 보이고, 명중 판정에는 지연 보상이 필요하다. 예측은 지연을 없애는 것이 아니라 *지연의 체감 위치를 옮기는* 기술이다.

## 2. 입력 Command와 Sequence

입력은 프레임 단위 이벤트가 아니라 시뮬레이션 command로 표현해야 한다.

```cpp
struct InputCommand {
    uint32_t sequence;
    uint32_t clientTick;
    float dt;
    Vec2 move;
    bool jump;
    bool fire;
};
```

`sequence`는 입력의 순서를 나타낸다. `clientTick`은 클라이언트의 입력 시점이다. `dt`는 이동 시뮬레이션에 사용한 시간이다. 서버는 command를 순서대로 처리하고, snapshot에 마지막으로 처리한 sequence를 담아 보낸다.

"이벤트가 아니라 command"라는 구분이 reconciliation의 전제다. `OnKeyDown(W)` 같은 이벤트는 재적용할 수 없지만, "이 틱에 이 방향으로 dt만큼 이동 의도"라는 command는 몇 번이고 같은 결과로 재시뮬레이션된다. 입력 시스템 설계 단계에서 *상태 기반 폴링 + 틱 단위 캡처* 로 가야 예측 가능한 이동이 된다는 뜻이다 — 프레임 가변 dt를 그대로 command에 넣는 대신, 고정 시뮬레이션 틱마다 입력 상태를 샘플해 command를 만든다.

`dt` 필드에 대한 주의 — 서버는 클라이언트가 보낸 dt를 *신뢰하면 안 된다*. dt를 그대로 쓰면 dt를 부풀린 스피드핵이 성립한다. 고정 틱 모델에서는 dt가 상수이므로 필드 자체를 빼는 것이 가장 깔끔하고, 가변을 허용해야 한다면 서버가 상한을 클램프하고 누적 시간을 검증해야 한다(클라이언트 N초 동안 입력 dt 합이 N을 넘으면 거부).

입력 패킷에는 최근 command 여러 개를 함께 넣는 것이 일반적이다. 입력 패킷 하나가 손실되어도 다음 패킷에 같은 입력이 반복되어 서버가 받을 확률이 높아진다.

```cpp
struct InputPacket {
    uint32_t newestSequence;
    InputCommand recentInputs[8];
};
```

서버 쪽에는 작은 입력 버퍼(jitter buffer)를 둔다 — 입력이 도착하는 간격은 네트워크 지터로 출렁이는데, 서버 틱은 일정하므로, 1~2틱 분량을 버퍼링해 "틱마다 입력 하나"의 흐름을 만든다. 버퍼가 비면(입력 미도착) 서버는 마지막 입력을 반복하거나 무입력으로 처리하는데, 어느 쪽이든 클라이언트 예측과 어긋나므로 reconciliation이 흡수한다. 버퍼가 너무 쌓이면(클라이언트 시계가 빠름) 가장 낡은 것을 버리고 클라이언트에 시계 조정 신호를 보낸다.

## 3. 로컬 예측

클라이언트는 입력을 만들자마자 pending buffer에 넣고, 같은 입력을 로컬 상태에 적용한다.

```cpp
void Client::ApplyLocalInput(InputCommand cmd) {
    cmd.sequence = nextSequence_++;

    SendInput(cmd);
    pendingInputs_.push_back(cmd);

    SimulatePlayer(predictedState_, cmd);
}
```

예측 상태는 렌더링에 사용된다. 서버 응답을 기다리지 않는다. 이때 중요한 점은 클라이언트 이동 로직과 서버 이동 로직이 같은 규칙을 사용해야 한다는 것이다. 가속도, 마찰, 충돌 판정, 경사 처리, 점프 조건이 다르면 예측은 계속 틀린다.

"같은 규칙"의 실무적 형태는 *공유 이동 모듈* 이다 — 입력과 상태를 받아 다음 상태를 내는 순수 함수 `SimulatePlayer(state, cmd)`를 클라이언트와 서버가 같은 소스로 컴파일한다. 같은 코드라도 어긋나는 지점들이 남는다: 컴파일러·플랫폼별 부동소수점 차이(FMA 유무, x87 vs SSE), 충돌 월드의 차이(클라이언트가 모르는 동적 장애물), 시뮬레이션 순서(다른 플레이어와의 밀침 처리 순서). 부동소수점 미세 오차는 매 스냅샷의 권위 덮어쓰기가 흡수하므로 격투 게임식 비트 결정론까지는 필요 없다 — 오차가 *수렴 가능한 크기* 로 유지되기만 하면 된다.

`pendingInputs_`의 크기는 command 생성률에 권위 확인까지 걸린 시간을 곱한 값에 가깝다. 여기에 server queue, snapshot cadence와 loss recovery가 더해지므로 RTT만으로 정확히 정해지지 않는다. 버퍼가 비정상적으로 자라면 예측이 권위 시간보다 계속 앞서갈 수 있다. 상한은 초 단위 관습값이 아니라 허용 prediction lead와 복구 가능한 history로 정하고, 초과 시 reconnect·resync·입력 억제 가운데 제품 정책을 명시한다.

## 4. 서버 Reconciliation

서버 snapshot이 도착하면 클라이언트는 서버 권위 상태로 되돌아간 뒤, 서버가 아직 처리하지 않은 입력을 다시 적용한다.

```cpp
bool IsMoreRecent(uint32_t a, uint32_t b) {
    return (a > b && a - b <  0x80000000u) ||
           (b > a && b - a >  0x80000000u);
}

void Client::OnServerSnapshot(const Snapshot& snapshot) {
    predictedState_ = snapshot.myAuthoritativeState;

    while (!pendingInputs_.empty() &&
           !IsMoreRecent(pendingInputs_.front().sequence,
                         snapshot.lastProcessedInput)) {
        pendingInputs_.pop_front();
    }

    for (const InputCommand& cmd : pendingInputs_) {
        SimulatePlayer(predictedState_, cmd);
    }
}
```

이 구조의 의미는 명확하다.

- 서버가 처리한 입력은 확정되었으므로 버린다.
- 서버가 처리하지 않은 입력은 클라이언트에서만 앞서 적용된 입력이므로 다시 적용한다.
- 결과 상태는 서버 권위 과거 위에 미확정 입력을 쌓은 현재 예측이다.

`IsMoreRecent`는 packet sequence와 같은 wrap-safe 비교 함수다. 입력 sequence가 충분히 긴 시간 동안 재사용되지 않는다는 전제는 필요하지만, 단순한 `<=` 비교를 그대로 쓰면 번호가 wrap되는 장시간 세션에서 pending input 정리가 틀릴 수 있다.

이 방식은 예측이 틀려도 서버 상태로 다시 수렴한다. 수렴의 보장이 어디서 오는지 명확히 해 두자 — 매 스냅샷마다 *권위 상태에서 다시 시작* 하기 때문이다. 예측 오차는 누적되지 않고, 항상 "권위 + 미확정 입력 몇 개"의 범위 안에 갇힌다. 미확정 입력의 재적용이 또 틀릴 수 있지만(같은 원인으로), 그 오차의 수명도 다음 스냅샷까지다. 오차가 *지속* 된다면 그것은 예측 구조의 문제가 아니라 클라/서버 시뮬레이션 차이(8절)의 문제다.

최적화 한 가지 — 매 스냅샷마다 무조건 rollback+replay를 돌릴 필요는 없다. 서버 상태와 "그 시점의 내 예측 기록"을 먼저 비교해서, 오차가 양자화 허용치 이하면 replay를 건너뛴다. 이를 위해 클라이언트는 예측 상태의 히스토리(틱별)를 짧게 보관한다.

```cpp
void Client::OnServerSnapshot(const Snapshot& s) {
    const PredictedRecord* rec = predictionHistory_.Find(s.lastProcessedInput);
    if (rec && WithinTolerance(rec->state, s.myAuthoritativeState)) {
        DropConfirmedInputs(s.lastProcessedInput);   // 일치 — replay 생략
        return;
    }
    RollbackAndReplay(s);                            // 불일치 — 본 경로
}
```

## 5. Replay 비용과 최적화

Reconciliation의 숨은 비용은 replay하는 미확정 command 수다. 대략 `snapshot 수신 빈도 × correction이 필요한 snapshot 비율 × replay depth`로 호출량을 모델링할 수 있지만, 일치 snapshot은 replay를 생략할 수 있고 snapshot cadence와 RTT만으로 correction 비율은 알 수 없다. 캐릭터 컨트롤러가 무거운 충돌 질의를 품고 있으면 높은 prediction depth와 잦은 mismatch에서 CPU 비용이 커진다.

비용을 다스리는 수단들:

- **4절의 일치 검사로 replay 자체를 생략** — 오차가 없는 평시(대부분의 시간)에 비용이 0이 된다.
- **이동 시뮬레이션과 무거운 부가 효과의 분리** — replay 중에는 파티클·사운드·발자국 이벤트를 발화하지 않는다. `SimulatePlayer`에 "replay 모드" 플래그를 두거나, 이벤트를 결과 상태에서 파생시키는 구조로 만든다. 이 분리가 없으면 보정 한 번에 발소리가 열 번 겹쳐 나는 코미디가 생긴다.
- **충돌 질의의 캐싱** — replay 구간의 정적 월드는 동일하므로, 캐릭터 주변 지오메트리를 한 번 모아 재사용한다.

이 비용 구조는 롤백 넷코드(전 월드 재시뮬레이션)의 축소판이다 — 예측·재조정은 "내 캐릭터 하나만 롤백하는 롤백 넷코드"라고 봐도 된다. 그 일반화가 격투 게임의 GGPO식 전체 롤백이다.

## 6. 오차 보정과 Smoothing

서버 상태와 예측 상태의 차이가 작으면 즉시 snap하면 화면이 떨린다. 차이가 작을 때는 렌더링 위치만 부드럽게 보정하고, 시뮬레이션 상태는 권위 상태에 맞춘다.

```cpp
void Client::SmoothVisual(const Vec3& oldVisual,
                          const Vec3& correctedPhysics) {
    float error = Length(oldVisual - correctedPhysics);

    if (error > teleportThreshold_) {
        visualPosition_ = correctedPhysics;
    } else {
        visualPosition_ = Lerp(oldVisual, correctedPhysics, 0.15f);
    }
}
```

구조의 요점은 *시뮬레이션 상태와 표시 상태의 분리* 다. 시뮬레이션(predictedState_)은 reconciliation 즉시 권위로 교정된다 — 여기에 smoothing을 걸면 이후 입력 재적용이 잘못된 기반 위에서 돌므로 오차가 오차를 낳는다. 표시 위치만 교정된 시뮬레이션 위치를 향해 지수 감쇠로 따라간다. 떨림(jitter)은 표시 계층에서 죽고, 정확성은 시뮬레이션 계층에서 산다.

큰 오차는 순간 이동으로 처리해야 한다. 벽을 뚫고 예측했거나 서버가 이동을 거부한 상황을 천천히 보정하면 캐릭터가 오랫동안 잘못된 위치에 보인다. teleport 임계는 이동 속도, collision scale, camera mode와 허용 수렴 시간으로 정한다. 카메라까지 snap할지 별도 spring으로 흡수할지도 playtest와 멀미 기준으로 결정한다.

보정은 상태별로 달라질 수 있다. 접지 상태를 위반하는 수직 오차, 벽 침투, nav constraint 위반은 즉시 고치는 편이 안정적일 수 있고 자유 공간의 작은 위치 오차는 표시 계층에서 감쇠할 수 있다.

## 7. 무엇을 예측하고 무엇을 예측하지 않는가

클라이언트가 모든 것을 예측하면 안 된다. 예측 범위의 선택이 곧 이 시스템의 설계 품질이다.

| 대상 | 예측? | 이유 |
|---|---|---|
| 내 이동·점프·대시 | 한다 | 입력→결과가 결정적, 체감 지연에 가장 민감 |
| 내 무기 발사 이펙트·반동 | 한다 (연출만) | 총구 화염·사운드는 즉시, *명중 판정은 서버* |
| 내 스킬의 자원 소모 표시 | 조건부 | UI만 선반영, 실패 시 되돌림(거부 응답 처리 필수) |
| 상대 플레이어 | 안 한다 | 입력을 모르므로 수신 스냅샷 사이를 보간 |
| 피해·사망 판정 | 안 한다 | 권위 판정 + 지연 보상의 영역 |
| 랜덤 결과(드랍·크리티컬) | 안 한다 | 시드 동기화 없이는 어긋남이 보장됨 |
| 문·엘리베이터 등 공유 오브젝트 | 신중히 | 여러 명이 상호작용 — 거부 시 되돌림이 눈에 띔 |

둘째 줄의 구분 — *연출 예측과 결과 예측의 분리* — 이 사격 게임의 표준이다. 발사 입력에 총구 화염·사운드·탄피는 즉시 나간다(0ms 체감). 그러나 "맞았다" 표시(히트 마커·피해 숫자)는 서버 확인 후에 띄운다. 이 분리 덕에 반응은 즉각적이면서 판정은 권위적이다.

"조건부" 항목들의 공통 비용은 *거부 처리* 다 — 예측해 놓고 서버가 거부하면 되돌리는 코드가 필요하고, 그 되돌림이 플레이어 눈에 띈다. 되돌림 UX를 감당할 수 없는 상태(결제·인벤토리처럼 신뢰가 중요한 것)는 예측하지 않는 것이 옳다. 예측 범위는 "기술적으로 가능한가"가 아니라 "틀렸을 때의 비용을 감당할 수 있는가"로 정한다.

## 8. 예측 실패를 줄이는 조건

예측이 자주 틀리면 smoothing으로 해결되지 않는다. 원인을 줄여야 한다.

| 원인 | 대응 |
|---|---|
| 클라/서버 이동 코드 차이 | 공유 모듈 또는 동일 규칙 테스트 |
| 부동소수점 차이 | 서버 권위, 제한된 예측 범위 |
| 입력 손실 | 최근 입력 반복 전송 |
| dt 불일치 | fixed tick command 사용 |
| 클라가 모르는 충돌 | 서버 snapshot에서 즉시 보정 |
| 서버 전용 상태 | 클라이언트 예측 대상에서 제외 |

여기에 *측정* 을 더해야 한다 — "예측 오차율"을 1급 지표로 노출한다. 스냅샷당 오차 크기의 분포(P50/P99), replay 발동 비율, teleport 발동 빈도. 평시 P99 오차가 양자화 단위의 몇 배 이내면 건강하고, 특정 상황(경사로, 군중 밀집, 특정 스킬)에서만 튄다면 그 상황의 시뮬레이션 차이를 잡는 식으로 원인을 좁힌다. 첫 줄의 "동일 규칙 테스트"는 자동화할 수 있다 — 같은 입력 시퀀스를 클라/서버 이동 코드에 넣고 결과를 비트 단위가 아닌 허용 오차로 비교하는 단위 테스트를 CI에 둔다.

예측은 모든 지연을 없애지 않는다. 자기 입력의 체감 지연을 숨길 뿐이다. 상대 플레이어와 피해 판정은 별도의 보간, 지연 보상, 서버 검증이 필요하다.



## 9. 원격 엔티티가 끊기는 이유

서버가 20Hz로 snapshot을 보낸다면 상태 간격은 50ms다. 네트워크 jitter가 있으면 간격은 더 불규칙해진다.

```text
server send:  0ms  50ms 100ms 150ms 200ms
client recv: 42ms  96ms 171ms 190ms 280ms
```

도착 즉시 위치를 적용하면 42ms, 96ms, 171ms에만 위치가 바뀐다. 렌더링은 60Hz 또는 144Hz로 더 자주 일어나므로 나머지 프레임에서는 정지했다가 튄다. 화면 주사율이 높을수록 — 즉 좋은 모니터일수록 — 이 끊김은 더 잘 보인다.

원격 엔티티 처리는 simulation state와 visual state를 분리해야 한다. 네트워크에서 받은 상태는 history에 저장하고, 렌더링은 history를 샘플링한다. "수신 이벤트가 화면을 직접 건드리지 않는다"는 이 분리가 이 글 전체의 골격이다 — 수신은 불규칙하고 렌더링은 규칙적이므로, 둘 사이에 시간축을 가진 버퍼가 있어야 한다.

## 10. 서버 시간 추정: 보간의 시계

`renderTime = 서버시간 - delay`라는 식이 성립하려면 클라이언트가 *지금 서버 시간이 몇 시인지* 알아야 한다. 클라이언트와 서버의 시계는 다르고, 서로 드리프트하므로, 추정과 보정이 필요하다.

기본 재료는 snapshot에 실려 오는 `serverTick`이다. 수신 시각과 묶으면 표본이 된다 — "서버 시간 T인 패킷이 내 시간 t에 도착했다". 단방향 지연을 RTT/2로 근사하면 `서버시간 ≈ T + RTT/2 + (now - t)`로 현재 서버 시간을 추정할 수 있다.

```cpp
// 서버 시간 추정기 — 표본을 EWMA 로 평활하고, 시계는 점프 대신 미세 가감속한다.
class ServerClock {
public:
    void OnSnapshot(double serverTime, double rttHalf, double now) {
        double estimate = serverTime + rttHalf;            // 그 패킷이 떠난 시점의 서버 시간 + 비행
        double offset = estimate - now;
        offset_ = Lerp(offset_, offset, 0.05);             // 표본 노이즈 평활
    }
    double Now(double localNow) const { return localNow + offset_; }
private:
    double offset_ = 0;
};
```

요점 두 가지. 첫째, 추정 오차는 ±지터 수준이며 그래도 된다 — [보간 delay](#12-interpolation-delay)가 그 오차를 덮을 만큼 크기 때문이다. 정밀 NTP가 필요한 게 아니라 *안정적인 단조 시계* 가 필요한 것이다. 둘째, offset이 갱신될 때 renderTime이 *점프하면 안 된다* — 화면의 모든 원격 엔티티가 동시에 움찔한다. offset 변화는 위 코드처럼 평활하거나, renderTime의 진행 속도를 ±0.5~2%로 미세 조절해(시간을 약간 빠르게/느리게 흐르게 해) 따라잡는다. "시계를 돌리지 말고 시계의 속도를 돌린다"가 규칙이다.

### 시계 추정의 오차와 드리프트 보정

이 추정기는 두 근사 위에 서 있다 — 단방향 지연을 RTT/2로 본다는 것과, 표본을 EWMA로 평활한다는 것. 각 근사가 어떤 오차를 남기고, 왜 시계를 "돌리지 않고 속도만 돌리는지"를 정량으로 본다.

**RTT/2 가정의 편향.** 한 표본의 실제 전방 지연을 $d_f$, 후방 지연을 $d_b$라 하면 측정된 왕복은 $\text{RTT}=d_f+d_b$다(서버 처리 시간 무시). 추정기는 전방 지연을 $\text{RTT}/2$로 놓으므로 오프셋 추정 $\hat\theta$의 편향은

$$\hat\theta - \theta = \frac{\text{RTT}}{2} - d_f = \frac{d_b - d_f}{2}.$$

경로가 대칭이면($d_f\approx d_b$) 편향은 0에 수렴하고, 비대칭일수록 커진다. 한쪽 방향의 최소 지연 $d_{\min}$을 알면 $d_f\in[d_{\min},\,\text{RTT}-d_{\min}]$이므로 편향은

$$|\hat\theta - \theta| \le \frac{\text{RTT}}{2} - d_{\min}$$

로 갇힌다(Cristian 1989). 이것은 *줄일 수 없는* 오차다 — 표본을 아무리 모아도 단방향 분해 정보가 없으면 비대칭은 보이지 않는다. 모바일·케이블처럼 상·하행이 구조적으로 다른 회선에서는 수십 ms의 편향이 그대로 남고, 그 편향은 원격 엔티티가 렌더되는 시점을 같은 양만큼 밀어 17절의 지연 보상이 되감아야 할 시간에 합산된다. 비대칭은 평활로 사라지지 않으므로, 추정기는 *최솟값 필터*(창 안에서 최소 RTT를 가진 표본만 채택 — 큐잉으로 부풀려진 비대칭이 가장 적은 표본)로 방어하는 것이 NTP의 표준 처방이다.

**EWMA의 분산 감소와 추적 지연의 맞바꿈.** 코드의 $\text{offset}_k = (1-\alpha)\,\text{offset}_{k-1} + \alpha\,\theta_k$는 게인 $\alpha$의 1차 IIR이다. 표본 잡음이 분산 $\sigma^2$로 독립이면 정상상태 추정 분산은

$$\operatorname{Var}(\hat\theta) = \frac{\alpha}{2-\alpha}\,\sigma^2$$

이고, $\alpha=0.05$에서 비는 $0.05/1.95\approx0.0256$다 — 표준편차가 약 $1/\sqrt{0.0256}\approx6.2$배 줄고, 유효 기억 길이는 약 $1/\alpha=20$표본이다. 작은 $\alpha$는 더 매끈한 시계를 주지만 진짜 드리프트를 따라잡는 데도 그만큼 느리다. 이 맞바꿈이 다음 항을 강제한다.

**드리프트는 일회 동기화를 무력화한다.** 클라이언트와 서버의 크리스털 발진기는 공칭 주파수에서 어긋나고, 소비자 등급은 보통 $\pm 50\text{–}100$ ppm이다. 두 시계가 반대로 틀어지면 상대 드리프트는 약 200 ppm $=2\times10^{-4}$, 즉 오프셋이 초당 0.2 ms, 분당 12 ms씩 벌어진다. 한 번 맞춘 시계는 분 단위에서 이미 틀어지므로 추정은 *연속 보정* 이어야 한다.

**점프가 아니라 슬루(slew).** 오프셋이 $\Delta$만큼 어긋났을 때 렌더 시계에 즉시 $\Delta$를 더하면 화면의 모든 원격 엔티티가 동시에 $\Delta$만큼 순간이동한다. 대신 시계의 *진행 속도* 를 $\pm r$만큼 비틀어(예: $r=1\%$, 시간을 1% 빠르게/느리게 흐르게) 따라잡는다. 수렴 시간은

$$t_{\text{conv}} = \frac{\Delta}{r}$$

다 — $\Delta=30$ ms, $r=1\%$면 3초간 모든 것이 1% 빠르게 흐르고(체감 불가), 같은 30 ms를 한 번에 점프하면 즉각 보인다. 경계 조건은 *슬루율이 최대 드리프트율을 넘어야 한다* 는 것($r>\rho$)이다: $\rho\approx200$ ppm $=0.02\%$에 $r=1\%$면 여유가 충분하지만, 슬루율을 너무 작게 잡으면 시계가 드리프트를 영영 따라잡지 못해 오프셋이 무한정 벌어진다. 또 하나의 실패 사례는 경로 변경(route flap)으로 RTT가 튀는 이상치 표본이다 — EWMA가 평활하더라도 큰 이상치를 끌고 가므로, 위의 최솟값 필터나 이상치 클램프로 방어해야 한다.

요약하면 이 시계는 *정확* 할 필요가 없고 *안정적이고 단조* 하면 된다(비대칭 편향은 지연 보상이 어차피 흡수한다). 그래서 설계의 본체는 절대 정확도가 아니라, 잡음을 평활하면서($\alpha$) 드리프트를 슬루로 따라잡되($r>\rho$) 화면을 점프시키지 않는 세 손잡이의 균형이다.

## 11. Snapshot History Buffer

클라이언트는 엔티티별로 최근 snapshot을 시간순으로 저장한다.

```cpp
struct RemoteSample {
    double serverTime;
    Vec3 position;
    Quat rotation;
    Vec3 velocity;
};

struct RemoteHistory {
    std::deque<RemoteSample> samples;
};
```

렌더링할 때는 `renderTime`을 정하고, 그 시각을 감싸는 두 sample을 찾는다.

```cpp
Vec3 SamplePosition(const RemoteHistory& h, double renderTime) {
    const RemoteSample* a = nullptr;
    const RemoteSample* b = nullptr;

    FindSamplesAround(h.samples, renderTime, &a, &b);
    if (a && b) {
        float t = float((renderTime - a->serverTime) /
                        (b->serverTime - a->serverTime));
        return Lerp(a->position, b->position, t);
    }
    return h.samples.empty() ? Vec3{} : h.samples.back().position;
}
```

History buffer는 너무 길 필요가 없다. 보간 지연과 최대 jitter, 패킷 손실 복구 여유만큼이면 충분하다. 보통 수백 ms 범위다. renderTime보다 충분히 낡은 sample은 매 프레임 머리에서 잘라낸다 — 버퍼가 자라기만 하면 그 자체가 시계 추정 버그(renderTime이 안 흐르고 있다)의 신호다.

선형 보간은 두 sample 사이를 직선으로 잇는다. 곡선 운동(원을 그리는 차량 등)에서는 직선 현이 눈에 띌 수 있고, 그때는 속도 정보를 활용한 Hermite 보간(양 끝점 + 양 끝 속도)이 답이다 — sample에 velocity를 담아 두는 또 하나의 이유다. 다만 비용 대비 체감 차이는 snapshot rate에 달려 있다: 30Hz 이상이면 lerp로 충분한 경우가 대부분이고, 10Hz급의 듬성한 복제(원거리 엔티티 LOD)에서 Hermite가 빛난다.

## 12. Interpolation Delay

Interpolation delay는 클라이언트가 얼마나 과거를 렌더링할지 정하는 값이다.

```text
renderTime = estimatedServerTime - interpolationDelay
```

delay가 크면 보간에 사용할 양쪽 snapshot이 안정적으로 존재한다. 화면은 부드럽지만 더 과거를 본다. delay가 작으면 최신 상태에 가깝지만 snapshot이 부족해 extrapolation이 자주 발생한다.

| Delay | 장점 | 단점 |
|---|---|---|
| 1 snapshot 간격 | 최신성 높음 | jitter에 약함 |
| 2 snapshot 간격 | 부드러움과 최신성 균형 | 약간 과거를 렌더링 |
| 3 snapshot 이상 | 손실에 강함 | 반응이 둔해 보임 |

Delay는 snapshot rate와 network jitter에 따라 조정한다. 20Hz snapshot의 두 간격은 100ms이고 60Hz의 두 간격은 약 33ms다. Source 계열에서 알려진 `cl_interp` 값은 특정 게임·버전·server update rate의 역사적 설정 예이지 현대 게임의 보편 기본값이 아니다.

delay의 구성을 식으로 쓰면 `delay ≈ snapshot 간격 + 손실 여유 + 지터 여유` 다. 손실 여유는 "한 장 빠져도 다음 장으로 보간을 잇는" 1간격이고, 지터 여유는 도착 간격 분산의 상위 분위수다. 그래서 *적응형 delay* 가 자연스러운 발전형이다 — 클라이언트가 도착 간격의 P95 지터를 측정해 delay를 동적으로 잡고, 회선이 좋은 플레이어는 자동으로 더 최신을 본다. 단, delay 변경도 시계와 같은 규칙을 따른다: 점프 금지, 미세 가감속으로 이동.

server clock 추정이 정확하다면 내가 보는 상대의 snapshot time은 **현재 추정 server time보다 interpolation delay만큼 과거**다. `RTT/2`를 여기에 다시 더하면 client 표시 age를 중복 계산한다. 다만 사격 command가 server에 도착한 시점에서 보면, server가 되감아야 할 시간에는 사격 command의 outbound one-way delay 추정과 사격자가 사용한 interpolation delay가 함께 들어간다.

## 13. 회전과 비위치 상태의 보간

위치만 보간하면 절반만 한 것이다. 같은 history에서 다른 형의 상태들도 각자의 규칙으로 보간한다.

- **회전**: 쿼터니언 두 sample 사이를 slerp(또는 가까운 각도면 nlerp)한다. 오일러 각을 성분별 lerp하면 ±180° 경계에서 한 바퀴 도는 버그가 나온다 — 복제 형식 단계에서부터 쿼터니언(smallest-three)으로 다루는 이유다. 시선 방향(요·피치)만 따로 보낼 때는 각도 wrap을 처리한 최단호 보간을 쓴다.
- **속도·아날로그 값**: 체력 바, 차량 RPM 같은 연속 값은 lerp로 충분하다.
- **이산 상태**: 무기 종류, 앉기/서기 플래그는 보간이 무의미하다 — renderTime이 sample b를 지나는 순간 b의 값으로 *스위치* 한다. "보간 가능한 상태"와 "스위치 상태"를 복제 스키마에서 구분해 두면 이 분기가 기계적으로 처리된다.
- **이벤트성 동작**: 발사·피격은 보간 대상이 아니라 [애니메이션·시각 이벤트 보정](#16-애니메이션과-시각-보정)의 영역이다.

## 14. Extrapolation의 한계

보간할 미래 sample이 없을 때는 최신 sample에서 짧게 예측할 수 있다.

```cpp
Vec3 Extrapolate(const RemoteSample& s, double renderTime,
                 double maxExtrapolationSeconds) {
    double dt = renderTime - s.serverTime;
    dt = std::clamp(dt, 0.0, maxExtrapolationSeconds);
    return s.position + s.velocity * float(dt);
}
```

Extrapolation은 최대 시간을 제한해야 한다. 원격 플레이어가 방향을 바꾸거나 벽에 막히거나 점프하면 단순 속도 예측은 쉽게 틀린다. 등속 가정에서 실제 가속도 크기를 $a$로 bound할 수 있다면 위치 오차 항은 대략 $\frac12at^2$로 커진다. 허용 visual error를 $e_{\max}$라 할 때 설명용 상한은 $t_{\max}\le\sqrt{2e_{\max}/a_{\max}}$로 잡을 수 있고, collision·teleport 같은 불연속 사건 때문에 더 짧아질 수 있다. 100ms나 한 캐릭터 폭을 장르 공통 상한으로 사용하지 않는다.

외삽이 끝나고 진짜 sample이 도착하면 외삽 위치와 실제 위치가 어긋나 있다 — 이 복귀 오차는 [15절의 smoothing](#15-smoothing과-teleport-threshold)이 흡수한다. 외삽을 길게 허용할수록 복귀 오차가 커져서, "부드러움을 산 만큼 나중에 갚는" 구조라는 점을 기억해야 한다.

Extrapolation이 길어지는 것은 네트워크 상태가 나쁘다는 신호다. 화면을 조금 더 부드럽게 만드는 임시 수단이지, snapshot 손실을 해결하는 주 수단이 아니다. 한 가지 예외적 용법이 *dead reckoning* — 차량·발사체처럼 운동 모델이 좋은 엔티티를 의도적으로 낮은 빈도로 복제하고 외삽으로 메우는 설계 — 인데, 이는 "외삽이 신뢰할 만한 운동 모델"이 있을 때의 대역폭 최적화이지, 플레이어 캐릭터의 표준 경로가 아니다.

## 15. Smoothing과 Teleport Threshold

서버 snapshot이 갑자기 큰 위치 차이를 만들면 보간만으로 해결할 수 없다. 작은 오차는 smoothing하고, 큰 오차는 teleport로 처리한다.

```cpp
Vec3 SmoothCorrection(Vec3 visual, Vec3 target, float dt) {
    float error = Length(target - visual);
    if (error > 5.0f) {
        return target;
    }

    float alpha = 1.0f - std::exp(-12.0f * dt);
    return Lerp(visual, target, alpha);
}
```

`1 - exp(-k·dt)` 형태는 프레임레이트 독립 지수 감쇠다 — 60fps든 144fps든 같은 속도로 수렴한다. 고정 alpha(`Lerp(v, t, 0.15f)`)를 가변 dt에 쓰면 주사율에 따라 보정 속도가 달라지는 미세 버그가 된다.

Teleport threshold는 게임에 따라 다르다. 빠른 FPS에서는 큰 오차를 오래 끌면 조준과 피격 판정이 어색해진다. MMO나 캐주얼 게임에서는 더 부드러운 보정을 선택할 수 있다. 의도적 순간이동(텔레포트 스킬, 리스폰)은 오차 보정과 구분해야 한다 — 서버가 "이건 텔레포트"라는 플래그(또는 이벤트)를 함께 보내, 클라이언트가 smoothing을 건너뛰고 이펙트로 처리하게 한다. 플래그 없이 거리 임계만으로 구분하면, 빠른 이동 스킬이 임계 근처에서 어떤 날은 미끄러지고 어떤 날은 점프하는 비일관성이 생긴다.

## 16. 애니메이션과 시각 보정

원격 엔티티의 위치만 보간하면 발 미끄러짐이 생긴다. 애니메이션 상태, 바라보는 방향, 상체 aim, 무기 발사 효과도 네트워크 상태와 함께 보정해야 한다.

권장 구조는 다음과 같다.

- 물리/게임플레이 위치는 서버 snapshot에서 온다.
- 렌더링 위치는 보간된 visual transform이다.
- 애니메이션은 visual velocity와 서버 animation state를 함께 사용한다 — 이동 블렌드(idle/walk/run)는 보간된 위치의 *미분* 으로 로컬에서 구동하고, 이산 상태(리로드, 앉기)는 복제된 state id로 스위치한다. 서버가 애니메이션 프레임을 보내는 것이 아니라, 클라이언트가 보간된 운동에서 애니메이션을 *유도* 하는 것이다.
- 발사 효과나 피격 효과는 이벤트 channel로 보낸다. 이벤트에는 발생 시각(serverTick)을 실어, renderTime 시간축 위의 올바른 순간에 재생한다 — 도착 즉시 재생하면 보간된 몸과 이펙트가 어긋난다.

원격 엔티티는 정확한 시뮬레이션보다 그럴듯한 표시가 중요하다. 단, 게임플레이 판정은 서버 권위 상태를 기준으로 해야 한다. 클라이언트의 보간 위치는 *그 클라이언트 화면의 진실* 일 뿐이고, 그 화면 기준의 조준이 공정하게 판정되도록 서버가 시간을 되감는 것 — 그것이 지연 보상이다.



## 17. 지연 보상이 필요한 이유

원격 플레이어는 보통 과거 위치로 보인다.

```text
server now: 1000ms
client interpolation delay: 100ms
client sees target at: 900ms
shot command reaches server at: 1040ms
```

서버가 1040ms의 현재 위치로 판정하면 클라이언트가 본 900ms 위치와 다르다. 플레이어는 화면상으로 맞췄지만 서버는 빗나갔다고 판단한다.

지연 보상이 없던 시절의 해법은 플레이어에게 전가하는 것이었다 — "리드 샷": 핑만큼 대상의 진행 방향 앞을 쏘는 기술. QuakeWorld 세대는 그것을 실력으로 받아들였지만, 대중적 게임에서는 "맞췄는데 안 맞는" 경험이 곧 이탈이다. Half-Life의 lag compensation(Bernier, GDC 2001)이 그 부담을 서버로 옮겼다 — 클라이언트가 본 대로 판정해 주는 대신, 서버가 시간을 되감는 비용을 진다.

Lag compensation은 사격자가 본 시각을 기준으로 판정한다.

```text
rewindTime = serverReceiveTime - estimatedOneWayLatency - interpolationDelay
```

정확한 one-way latency를 알기는 어렵다. 보통 RTT의 절반과 시간 동기화 추정치를 사용한다. 핵심은 서버가 무조건 현재만 보지 않고, 클라이언트가 본 과거를 고려한다는 점이다.

## 18. 서버 History Buffer

서버는 플레이어와 주요 hitbox의 과거 상태를 일정 시간 보관한다.

```cpp
struct HitboxSample {
    double serverTime;
    PlayerId player;
    Vec3 position;
    Quat rotation;
    Capsule hitCapsule;
};

class LagHistory {
public:
    void Record(double time, Span<Player> players);
    HitboxSample Sample(PlayerId id, double time) const;

private:
    std::deque<HitboxSample> samples_;
};
```

History 길이는 최대 허용 rewind에 기록 간격, packet·tick jitter와 조회 경계 여유를 더해 정한다. 너무 길게 보관하면 메모리와 조회 비용이 늘고, 정책 실수로 과도한 과거 판정을 허용할 위험이 커진다.

메모리 비용은 `기록률 × history 초 × 대상 수 × sample 크기`로 계산한다. transform과 capsule만 저장하면 작을 수 있지만 bone pose, hitbox set과 dynamic obstacle까지 저장하면 빠르게 커진다. 위치·회전만 기록하면 되감은 hitbox가 그 tick의 animation 자세를 모른다. 정밀한 구현은 hitbox pose를 보관하거나 기록된 animation parameter로 자세를 재구성한다. coarse body 판정 뒤에만 정밀 pose를 조회하는 2단 판정은 한 가지 절충이다.

`Sample`은 요청 시각을 감싸는 두 기록 사이를 보간해야 한다 — 기록 주기(예: 64Hz)의 정수 배 시각만 반환하면 보상 자체에 최대 1틱의 계단 오차가 들어간다. 클라이언트의 보간과 같은 lerp가 서버에서 한 번 더 도는 셈이다.

## 19. Rewind Time의 계산

되감기 시각의 정밀도가 보상의 품질이다. 구성 요소를 분해하면:

```text
rewindTime = 명령 수신 시각
           - 단방향 지연 추정        (RTT/2 — 비대칭 경로에서는 오차원)
           - 사격자의 보간 지연       (클라이언트가 보고한 값을 서버가 검증)
```

두 항 모두 추정이고, 더 정확한 방법은 *클라이언트가 자신의 renderTime 기준 틱을 명령에 싣는 것* 이다 — "나는 tick 892와 893 사이(α=0.4)를 보며 쐈다". 서버는 그 주장을 그대로 믿지 않고 자신의 추정과 대조해 허용 창(예: ±2틱) 안이면 받아들인다. 신뢰는 [검증과 악용 방지](#23-악용-방지)를 전제로만 위임된다.

보간 지연 항의 함정 — 클라이언트마다 다르고(적응형 delay), 설정으로 바뀔 수 있다. 서버가 고정값을 가정하면 그 가정과 다른 클라이언트의 판정이 계통적으로 어긋난다. Source 엔진이 `cl_interp`를 서버에 보고하게 하는 이유이고, 보고값에 상한을 거는 이유이기도 하다(보간 지연을 부풀리면 더 깊은 과거를 합법적으로 쏠 수 있게 되므로).

### 되감기 오차 예산 — 시간 오차의 위치 증폭

되감기 시각의 추정 오차가 어떻게 판정 오차로 번지는지를 정량화하면, ±N틱 허용 창과 최대 되감기 한도가 왜 그 크기여야 하는지가 따라 나온다.

**시간 오차의 분해.** 서버가 재구성하는 되감기 시각은 $\hat t_r = t_{\text{recv}} - \widehat{\text{owl}} - \hat d_{\text{interp}}$ 이고, 사격자가 실제로 본 시각은 $t_r^\star = t_{\text{recv}} - \text{owl}^\star - d_{\text{interp}}^\star$ 다. 되감기 시간 오차는 두 추정 오차의 합이다.

$$\varepsilon_t = \hat t_r - t_r^\star = (\widehat{\text{owl}} - \text{owl}^\star) + (\hat d_{\text{interp}} - d_{\text{interp}}^\star)$$

주요 항은 다음과 같다.

- **경로 비대칭** — $\widehat{\text{owl}} = \text{RTT}/2$로 두지만 실제 단방향 지연은 $\text{owl}^\star = \text{RTT}/2 + \Delta$이며, $\Delta = (d_{\uparrow}-d_{\downarrow})/2$는 상·하향 경로 지연 차의 절반이다. 왕복 측정만으로는 $\Delta$를 분리할 수 없다 — 이는 NTP에서 오프셋이 경로 비대칭에 의해 최대 왕복 지연의 절반까지 편향되는 것과 같은 근본 한계다(RFC 5905 §8). 즉 $|\varepsilon_{\text{owl}}| \le \text{RTT}/2$가 원리적 상한이고, 대칭 경로에서만 작다. 모바일 상향처럼 비대칭이 큰 회선에서는 수십 ms까지 벌어진다.
- **보간 지연 불일치** — 클라이언트가 보고한 $\hat d_{\text{interp}}$가 실제 적응형 값과 다르면 그 차이가 그대로 오차다. 서버가 고정값을 가정하면 계통 편향이 된다(위 본문).
- **시계 동기 오차** — 시계 추정의 잔차. 보통 수 ms 규모로 31절의 보정 품질에 달려 있다.
- **서버 샘플 양자화** — history를 기록 주기 $T_h$의 격자에서만 뽑으면 nearest/previous sample 정책에 따라 최대 한 간격 규모의 시간 오차가 생긴다. 두 sample 사이 보간은 등속·등회전 가정 아래 이 오차를 줄이지만 tick 사이의 충돌·animation 불연속까지 복원하지는 못한다.

**위치로의 증폭.** 판정은 $t_r^\star$가 아니라 $\hat t_r$의 hitbox에 대해 이뤄지므로, 속도 $v$로 움직이는 대상에서 시간 오차는 위치 오차로 바뀐다.

$$\varepsilon_x \approx v\,\varepsilon_t$$

이 한 줄이 지연 보상이 어려운 이유의 핵심이다 — 같은 $\varepsilon_t$가 멈춘 대상에는 무해하고 질주하는 대상에는 치명적이다. 일반 FPS 달리기 $v\approx 6$ m/s, $\varepsilon_t = 15$ ms면 $\varepsilon_x \approx 9$ cm로, 머리 hitbox(반경 약 8–10 cm)는 빗나가도 몸통(폭 약 25–30 cm)은 맞는 경계다. 능력·차량으로 $v = 10$ m/s, $\varepsilon_t = 30$ ms면 $\varepsilon_x \approx 30$ cm — 몸통 하나 폭이라, "화면에선 분명히 맞춘" 사격이 서버에선 빗나간다. 위치 정밀도가 아니라 *시간* 정밀도가 판정 품질을 정한다는 뜻이고, 그래서 보상 설계의 모든 노력이 $\varepsilon_t$를 줄이는 데 모인다.

**허용 창의 크기.** ±N틱 창은 $\varepsilon_t$의 정당한 규모를 덮되 그 이상은 거부하도록 잡는다. 64Hz에서 2틱은 약 31 ms로, 위에서 본 정당한 오차 규모(수십 ms)와 같은 자릿수다. 창이 더 좁으면 정직한 비대칭 경로의 사격이 계통적으로 거부되고, 더 넓으면 치터가 임의의 과거를 주장해 깊은 되감기를 합법화한다. 그래서 더 정확한 해법은 창을 넓히는 것이 아니라 $\varepsilon_t$ 자체를 줄이는 것이다 — 클라이언트가 renderTick을 명령에 싣고 서버가 자기 추정과 대조하는 방식(위 본문).

**최대 되감기 한도의 의미.** 최대 되감기 $R_{\max}$(예: 200 ms)는 피격자의 최악 노출을 가둔다. 피격자는 엄폐에 도달한 뒤에도 최대 $R_{\max}$만큼 과거 판정으로 맞을 수 있고, 그 "엄폐 너머 거리"는 $v_{\text{victim}}\cdot R_{\max}$다 — $v = 6$ m/s, $R_{\max} = 200$ ms면 약 1.2 m다. 이것이 쏘는 사람 우대 정책(22절)이 피격자에게 청구하는 비용의 정량 표현이다. $R_{\max}$를 늘리면 고핑 사격자의 경험은 살지만 이 거리가 함께 늘어, 죽고 나서 한참 뒤의 사망 판정이 잦아진다.

**실패 경계.** $v$가 크고 동시에 지연 추정의 분산(지터)이 크면 $\varepsilon_x$는 무거운 꼬리를 가진다. $R_{\max}$ 클램프는 오차의 최댓값을 자를 뿐 매 사격의 $\varepsilon_x$를 줄이지 못한다. 빠른 대상이 많은 게임에서 "엄폐 뒤 피격" 민원이 속도에 비례해 늘어나는 이유이고, 근본 대책은 클램프가 아니라 $\varepsilon_t$ 축소(정확한 시계 동기, renderTick 보고, 비대칭 추정의 급변 클램프)에 있다.

## 20. Hit Rewind 절차

Hitscan 무기 판정 흐름은 다음과 같다.

```cpp
bool ServerValidateShot(const ShotCommand& cmd, ClientId shooter) {
    double receiveTime = ServerTime();
    double rewindTime = receiveTime
        - EstimateOneWayLatency(shooter)
        - InterpolationDelay(shooter);

    rewindTime = ClampRewind(rewindTime, receiveTime);

    Ray shotRay = ReconstructShotRay(cmd, shooter);
    for (PlayerId target : PossibleTargets(shooter)) {
        HitboxSample past = history_.Sample(target, rewindTime);
        if (Intersects(shotRay, past.hitCapsule)) {
            return ApplyHit(shooter, target, cmd.weapon);
        }
    }
    return false;
}
```

서버는 클라이언트가 보낸 hit 결과를 그대로 믿지 않는다. 클라이언트는 사격 방향, 입력 sequence, view angle 같은 command를 보내고, 서버가 과거 hitbox와 교차 검사를 수행한다.

되감기는 판정 순간에만 사용한다. 서버 월드 전체를 영구적으로 과거로 바꾸지 않는다. 일반적인 구현은 history에서 과거 hitbox를 샘플링해 임시 판정 구조를 만든다.

빠뜨리기 쉬운 디테일 둘. 첫째, *벽도 되감아야 하는가* — 움직이는 엄폐물(문, 차량)이 있다면 그렇다. 정적 지오메트리만 있는 맵은 현재 월드로 가려짐 검사를 해도 되지만, 동적 엄폐물을 현재 상태로 검사하면 "사격자 화면에선 열려 있던 문"이 판정을 막는 비대칭이 생긴다. 둘째, *사격자 자신의 위치* — 사격 광선의 원점은 사격자의 그 시점 예측 위치다. 서버가 보유한 사격자 권위 상태(해당 입력 처리 시점)와 명령의 원점 주장을 대조해, 어긋나면 서버 측 원점으로 강제한다.

## 21. Hitscan과 Projectile

Lag compensation은 hitscan 무기에서 가장 중요하다. 총알이 즉시 ray로 판정되기 때문이다. Projectile은 실제 탄이 시간을 두고 이동하므로 정책이 달라진다.

| 무기 유형 | 보상 방식 |
|---|---|
| hitscan rifle | 사격 시점의 target hitbox rewind |
| shotgun | pellet별 ray rewind (같은 rewindTime, 퍼짐 시드는 서버가 결정) |
| projectile rocket | 발사 위치/시각 검증 후 서버 projectile 시뮬레이션 |
| melee | 짧은 시간창의 근접 hitbox rewind |

Projectile을 클라이언트 시점으로 완전히 되감아 맞추면 피격자 입장에서 매우 불공정할 수 있다. 보통 발사 자체는 예측 표시하고, 실제 projectile은 서버에서 시뮬레이션한다. 절충형도 있다 — 발사 *시점만* 보상하고(사격자의 과거 시각에 탄을 생성) 이후 비행은 서버 현재 시간축에서 진행하는 방식. 탄속이 빠를수록 hitscan에 가까워지므로 보상의 가치가 커지고, 느린 투사체일수록 "리드해서 쏘는 게임 디자인"으로 받아들여진다 — 무기 디자인과 넷코드 정책이 여기서 직접 만난다.

샷건의 한 줄 디테일 — pellet 퍼짐 패턴의 난수는 서버가 시드를 정하고 클라이언트에 알려 주는 쪽이 옳다. 클라이언트가 시드를 정하면 "가장 잘 모이는 패턴"만 골라 쏘는 조작이 가능해진다.

## 22. 공정성의 트레이드오프

Lag compensation은 쏘는 사람과 맞는 사람 사이의 정책 선택이다.

쏘는 사람 우대:
- 화면에서 맞춘 사격이 맞는다.
- 고핑 플레이어도 공격 경험이 유지된다.
- 피격자는 엄폐 뒤에서 맞았다고 느낄 수 있다.

피격자 우대:
- 엄폐와 회피가 더 믿을 만하다.
- 고핑 사격이 불리하다.
- 쏘는 사람은 맞춘 것 같은데 빗나간다고 느낀다.

대부분의 FPS는 일정 범위 안에서 쏘는 사람을 우대한다. 이 선택에는 지각적 근거가 있다 — 사격자는 자기 크로스헤어와 대상을 *주시* 하고 있어 1프레임의 불일치도 알아채지만, 피격자는 자신이 "이미 엄폐했다"는 확신이 약하다(수십 ms 차이는 본인 행동 기억으로 검증 불가). 같은 불일치라도 사격자 쪽에 두는 편이 두 배로 눈에 띈다는 뜻이다. "맞힌 것은 맞는다(I shot you first)"가 업계 표준 정책이 된 이유다.

대신 최대 rewind policy를 둔다. 한도를 넘는 요청은 clamp, 부분 보상 또는 비보상 판정으로 처리할 수 있다. 값은 공격 종류, target speed, cover topology, 지역별 latency 분포와 “엄폐 뒤 피격” telemetry로 정한다. 지연 보상은 무한한 과거를 인정하는 장치가 아니며 매치메이킹과 server region 배치를 대체하지 않는다.

"피격자 우대"가 옳은 장르도 있다 — 회피가 핵심 기술인 게임(격투 게임의 무적기, 일부 배틀로얄의 위치 우위)에서는 피격자의 입력을 더 존중하는 판정이 게임성과 맞는다. 정책은 기술이 아니라 디자인 결정이고, 넷코드는 어느 쪽이든 구현할 수 있어야 한다.

## 23. 악용 방지

클라이언트가 보낸 timestamp를 그대로 믿으면 안 된다. 클라이언트가 일부러 과거 시각을 보내 유리한 판정을 만들 수 있다. 서버는 자체 수신 시간, RTT 추정, 입력 sequence, clock drift 범위로 rewind time을 제한한다.

필수 제한은 다음과 같다.

- 최대 rewind 시간 제한 — [공정성 정책](#22-공정성의-트레이드오프)이 정한 보상의 절대 상한.
- 클라이언트 주장 시각과 서버 추정의 허용 창 대조(±수 틱) — 창 밖이면 서버 추정으로 강제.
- 비정상 latency spike 필터링 — 핑을 인위적으로 출렁여 깊은 되감기를 만드는 조작(lag switch) 의 무력화. 지연 추정에 급변 클램프를 두고, 패턴이 반복되면 제재 신호로 넘긴다.
- 사격 rate와 ammo를 서버에서 검증 — 되감기 이전의 기본기.
- 시야각과 캐릭터 방향의 물리적 가능성 검증 — 등 뒤를 쏘는 명령은 회전 속도 상한과 대조.
- 벽과 엄폐물에 대한 서버 raycast — 클라이언트의 "보였다" 주장을 재현 검사.

Lag compensation은 치트 방어가 아니라 지연 보정이다. 서버 검증과 함께 쓰여야 한다. 구분이 중요하다 — 보상 시스템은 정직한 클라이언트의 *시점 차이* 를 메우는 장치이고, 그 장치의 입력(시각·지연 주장)이 새로운 공격 표면이 되므로 각 입력에 검증을 붙이는 것이다.

## 24. 검증과 디버깅

지연 보상 버그는 "가끔 안 맞는다"는 모호한 신고로 온다. 재현과 계측 수단을 시스템에 내장해야 한다.

- **판정 불일치 로그**: 클라이언트가 "맞췄다"고 표시(히트 예측)했는데 서버가 빗나감 판정한 비율을 상시 집계한다. 이 비율이 핑 구간별로 균일해야 정상이고, 특정 핑 구간에서 튀면 rewind 계산의 계통 오차다.
- **서버 측 시각화 리플레이**: 판정 순간의 (현재 월드, 되감은 hitbox, 사격 광선)을 덤프해 시각화한다. Source의 `sv_showlagcompensation`이 이 도구의 원형 — 되감은 hitbox를 게임 안에 빨간 윤곽으로 그려 준다.
- **합성 테스트**: 봇 두 대에 인위적 지연(50/100/200ms)을 주고, 한쪽이 등속 이동하는 다른 쪽을 자동 조준 사격하게 한다. 보상이 정확하면 명중률이 핑과 무관하게 일정해야 한다 — 이 그래프 한 장이 회귀 테스트가 된다.

이 계측은 [공정성 정책 튜닝](#22-공정성의-트레이드오프)의 데이터이기도 하다 — "엄폐 후 피격" 신고의 실제 분포(피격 시점과 엄폐 시점의 차)를 재 보면, 최대 rewind를 어디서 자를지가 감이 아니라 수치로 정해진다.



## 25. Tick이 기준 시계인 이유

서버가 frame delta time을 그대로 사용하면 매 실행마다 시뮬레이션 결과가 달라질 수 있다. 네트워크 게임에서는 입력이 어느 시간 단위에 적용되었는지 명확해야 한다. 그래서 서버는 고정 tick으로 상태를 갱신한다.

```text
60Hz server tick = 16.666ms
30Hz snapshot = 33.333ms
144Hz client render = 6.944ms
```

서버 tick은 권위 상태의 인덱스다. Snapshot, input command, replay, history buffer, lag compensation 모두 tick 또는 server time을 기준으로 연결된다. "tick 1847의 월드"는 모호함 없는 단 하나의 상태이고, 시스템의 모든 시간 참조가 이 정수 인덱스로 환원된다 — 부동소수점 초 단위로 시간을 다루기 시작하면 비교·직렬화·재현 모든 곳에서 오차 처리가 따라붙는다. *시간은 틱 정수로, 초는 표시할 때만* 이 규율이다.

## 26. 틱레이트의 선택

틱레이트는 반응성·공정성과 서버 비용의 직접 거래다.

| 틱레이트 | 틱 간격 | 의미 |
|---|---|---|
| 10~15Hz | 66~100ms | 낮은 갱신 비용, coarse simulation |
| 20~30Hz | 33~50ms | 비용과 입력·판정 해상도의 절충 후보 |
| 60~64Hz | ~16ms | 더 촘촘한 action simulation 후보 |
| 120~128Hz | ~8ms | 짧은 tick deadline과 높은 scheduler·network 부담 |

장르 이름만으로 tick rate를 정할 수는 없다. 한 tick 안에서 무엇을 처리하는지, sub-tick timestamp·continuous collision·lag compensation을 쓰는지, snapshot rate를 simulation rate와 분리했는지가 체감을 함께 결정한다.

입력을 tick 경계에서만 소비하고 도착 시점이 경계에 대해 균등하다고 가정하면 추가 대기 평균은 tick 간격의 절반이다. 그러나 timestamped command를 tick 내부 시각으로 재구성할 수 있고, 같은 tick의 사건도 명시적 ordering key로 정렬할 수 있다. 반대로 server queue와 missed deadline은 이 단순 양자화보다 큰 지연을 만든다.

tick rate를 두 배로 하면 per-tick simulation 호출 수는 두 배지만 전체 CPU·서버 비용이 정확히 두 배가 되지는 않는다. networking·match orchestration 같은 고정 비용은 그대로여서 증가율이 작을 수 있고, cache working set·lock contention·job scheduling·deadline miss가 악화되면 두 배보다 커질 수 있다. snapshot encoding과 전송도 독립 rate라면 같이 늘지 않는다. 같은 bot replay로 system별 CPU, p99 tick time, match density와 bandwidth를 측정해야 한다.

simulation tick과 판정 시각을 분리할 수도 있다. 명령의 검증된 sub-tick 시각, history 보간과 continuous collision을 사용하면 hit validation의 시간 해상도가 tick 간격보다 가늘 수 있다. 이는 simulation state가 그 시각에 실제로 존재했다는 뜻이 아니라 두 저장 state 사이를 정의된 규칙으로 재구성한다는 뜻이다.

## 27. Fixed Timestep Server

서버는 누적 시간을 기반으로 고정 dt 시뮬레이션을 수행한다.

```cpp
void ServerLoop() {
    const double tickDt = 1.0 / 60.0;
    double accumulator = 0.0;
    double last = Now();

    while (running_) {
        double now = Now();
        accumulator += now - last;
        last = now;

        while (accumulator >= tickDt) {
            ProcessInputsForTick(serverTick_);
            SimulateWorld(tickDt);
            BuildReplicationState(serverTick_);
            ++serverTick_;
            accumulator -= tickDt;
        }

        FlushNetwork();
        SleepUntilNextWork();
    }
}
```

서버가 밀리면 catch-up 루프가 여러 tick을 한 번에 실행할 수 있다. 하지만 무한 catch-up은 spiral of death를 만든다. 권위 simulation에서 tick을 조용히 버리면 timer, cooldown, physics와 client tick mapping 계약이 깨질 수 있으므로 일반적인 정답도 아니다. 최대 catch-up work를 제한하되, 초과 시 명시적 time dilation, match degradation·migration·termination, 부하 차단 가운데 게임 규칙에 맞는 정책을 선택하고 deadline miss를 운영 경보로 올린다.

`SleepUntilNextWork`의 정밀도도 실무 디테일이다. Windows의 timer resolution과 wait 동작은 OS 버전, process 설정, wait API와 전원 정책에 따라 달라져 “항상 15.6ms”가 아니다. absolute deadline 기반 waitable timer, 짧은 최종 spin과 실제 wakeup error 측정을 조합할 수 있다. 과도한 spin은 core와 전력을 소비하므로 wakeup-lateness histogram으로 범위를 정한다.

## 28. Input Tick과 Command Buffer

클라이언트 입력은 서버 tick에 맞춰 처리되어야 한다. 패킷은 늦게 도착할 수 있으므로 서버는 command buffer를 둔다.

```cpp
struct QueuedInput {
    ClientId client;
    uint32_t inputSequence;
    uint32_t targetTick;
    InputCommand command;
};

void ProcessInputsForTick(uint32_t tick) {
    for (ClientId c : clients_) {
        InputCommand cmd = inputBuffer_.GetOrRepeatLast(c, tick);
        ApplyInput(c, cmd);
    }
}
```

입력이 해당 tick까지 도착하지 않으면 마지막 입력을 반복하거나 neutral input을 사용한다. 액션 게임에서는 입력 지연을 줄이기 위해 오래 기다리지 않는다. Lockstep 게임처럼 모든 입력을 기다리는 방식은 지연을 크게 만든다.

버퍼 깊이가 지연·견고함의 다이얼이다 — 깊이 1틱이면 지터 1틱까지만 흡수하고 그 이상은 입력 누락(반복 처리)이 된다. 깊이를 늘리면 누락은 줄지만 입력이 그만큼 묵었다가 처리된다. 실전은 *적응형* 이다: 클라이언트별 입력 도착의 지터를 측정해 버퍼 목표 깊이를 정하고(예: P99 지터 + 1틱), 실제 깊이가 목표를 벗어나면 [클라이언트 시계를 가감속](#31-clock-sync와-drift-correction)시켜 조정한다. 오버워치가 발표한 구조가 정확히 이것이다 — 평시 버퍼를 최소로 유지하다, 누락이 감지되면 클라이언트가 일시적으로 더 앞서 보내게 만든다.

입력에는 sequence가 필요하다. 서버는 중복 입력을 버리고, 빠진 입력을 감지하고, snapshot에 `lastProcessedInput`을 담아 클라이언트 보정에 사용한다.

## 29. 클라이언트는 서버보다 앞서 달린다

[28절의 command buffer](#28-input-tick과-command-buffer)가 동작하려면 클라이언트의 입력이 *해당 틱이 서버에서 실행되기 전에* 도착해야 한다. 즉 클라이언트의 예측 시뮬레이션은 서버 시간보다 `상행 지연 + 버퍼 여유`만큼 *미래* 를 달린다.

```text
클라이언트 예측 틱 = 추정 서버 틱 + ceil(상행지연/틱간격) + 버퍼 여유
서버:     ... 1845 [1846 실행 중] ...
클라이언트: ... [1849 예측 중] → 입력(틱 1849)이 1849 실행 전에 도착하도록
```

이 선행량(run-ahead)이 곧 [command buffer](#28-input-tick과-command-buffer)의 공급선이다. 선행이 부족하면 입력이 늦어 누락 처리되고, 과하면 입력이 버퍼에서 묵는다. 서버는 snapshot에 "네 입력이 몇 틱 여유로 도착하는지"(버퍼 잔량)를 실어 보내고, 클라이언트는 그 피드백으로 자기 시계를 ±수 % 가감속해 선행량을 목표에 묶는다 — [drift correction](#31-clock-sync와-drift-correction)과 같은 메커니즘이다.

이 그림이 잡히면 시스템의 시간 지형이 한 장에 들어온다 — *클라이언트의 내 캐릭터는 서버보다 미래* (예측 + 선행)에 있고, *원격 엔티티는 서버보다 과거* (스냅샷 + 보간 지연)에 있다. 그 사이 어딘가의 "서버 현재"를 클라이언트는 직접 보지 못한다. 지연 보상이 메우는 것이 바로 이 두 시간대 사이의 간극이다.

## 30. Render Tick과 Snapshot Tick

클라이언트 렌더링은 서버 tick과 독립적이다. 144Hz 모니터에서는 렌더 프레임이 서버 tick보다 훨씬 많다. 그래서 클라이언트는 snapshot history와 예측 상태를 샘플링해 렌더링한다.

```text
server simulation: 60Hz
server snapshot:   20Hz
client render:    144Hz
```

이 세 주기를 분리하면 대역폭과 반응성을 각각 조정할 수 있다. Snapshot을 60Hz로 보내지 않아도 클라이언트는 144Hz로 부드럽게 렌더링할 수 있다. 대신 보간 delay와 예측 보정이 필요하다.

클라이언트 쪽도 같은 fixed-timestep 구조가 필요하다는 점을 빠뜨리기 쉽다 — 예측 시뮬레이션은 서버와 같은 고정 dt(60Hz)로 돌고, 렌더는 그 사이를 *알파 보간* 으로 메운다(`render = lerp(prevTickState, currTickState, accumulator/tickDt)`). 내 캐릭터조차 시뮬레이션 상태를 그대로 그리면 144Hz 화면에서 60Hz 계단이 보인다. "시뮬레이션은 틱으로, 표시는 보간으로"가 서버·클라이언트 양쪽에 대칭으로 적용되는 셈이다.

Snapshot tick은 모든 서버 tick을 보낼 필요가 없다. 예를 들어 서버는 매 tick 상태를 만들고, 3 tick마다 snapshot을 보낼 수 있다. 중요한 이벤트는 별도 reliable channel로 보낸다. 단, 송신 틱의 *위상* 은 고르게 — 3틱마다라면 정확히 3틱 간격으로 — 유지해야 수신 측 보간 buffer가 일정 깊이로 안정된다. 송신이 시뮬레이션 끝의 남는 시간에 "되는 대로" 일어나면 그 불규칙이 곧 인위적 지터다.

## 31. Clock Sync와 Drift Correction

클라이언트는 서버 시간을 추정해야 한다. 그래야 snapshot interpolation과 lag compensation command timestamp가 의미를 가진다.

```cpp
struct TimeSample {
    double clientSendTime;
    double serverReceiveTime;
    double serverSendTime;
    double clientReceiveTime;
};
```

RTT와 server time offset은 ping/pong 또는 패킷 timestamp로 추정한다. 위 4시각 구조(NTP와 동일)에서 `RTT = (수신-송신) - (서버처리시간)`, `offset = ((서버수신-클라송신) + (서버송신-클라수신)) / 2`로 계산되며, 경로가 비대칭이면 offset에 그 비대칭의 절반이 오차로 남는다 — 게임은 그 정도 오차를 보간 지연과 검증 창이 흡수하도록 설계한다. 단일 sample은 jitter에 취약하므로 moving average나 outlier rejection을 사용한다. 좋은 휴리스틱은 *RTT가 최소였던 표본을 우대* 하는 것 — 큐잉이 없던 왕복일수록 offset 추정이 깨끗하다.

클라이언트 시계는 서버보다 조금 빠르거나 느릴 수 있다. drift가 작으면 render time을 천천히 보정한다. 갑자기 시간을 점프시키면 보간 버퍼가 깨지고 화면이 튄다.

```cpp
estimatedServerTime_ += localDelta * clockScale_;
clockScale_ = Clamp(clockScale_, 0.98, 1.02);
```

±2%의 가감속은 1초에 최대 20ms를 따라잡는다 — 사람이 지각하지 못하는 속도 변화로 시계를 미끄러뜨리는 것이다. 같은 장치가 세 군데서 쓰인다는 점을 정리해 두자: 보간 renderTime의 추종, [입력 선행량](#29-클라이언트는-서버보다-앞서-달린다)의 조절, 그리고 일시정지·로딩 후의 재동기화. "시간 점프 금지, 속도 조절만"이 셋을 관통하는 단일 규칙이다.

## 32. Tick Budget

60Hz 서버 tick의 이론적 예산은 16.67ms다. 하지만 한 tick 안에 시뮬레이션만 있는 것이 아니다.

| 작업 | 예 |
|---|---|
| 입력 처리 | 패킷 decode, command queue |
| 게임 시뮬레이션 | 이동, 전투, AI 일부 |
| 물리 | 충돌, broadphase, solver |
| replication | AOI, delta encode |
| 송신 | packet build, socket send |
| 계측 | metrics, logging |

서버는 평균뿐 아니라 P95/P99와 deadline-miss rate, 최대 연속 miss를 봐야 한다. 긴 꼬리의 원인 후보에는 GC·allocator stall, 동기 로그/I/O, 다른 매치와의 core 경쟁, page fault, interrupt와 AOI 재계산 몰림이 있다. 원인은 profile과 trace로 확인해야 하며 동적 할당 하나로 미리 단정하지 않는다.

Tick budget은 서버 밀도와 직접 연결된다. 한 프로세스에 너무 많은 match를 넣으면 tail latency가 길어질 수 있다. P99가 예산 안이라는 사실은 나머지 1%가 deadline을 지킨다는 뜻이 아니므로 P99 하나로 밀도를 승인하지 않는다. workload별 distribution, deadline miss, CPU steal, run-queue delay와 연속 hitch를 함께 보고 headroom을 정한다.



## 33. 권위의 위치

권위 서버 모델의 원칙은 단순하다.

```text
client sends intent
server validates intent
server simulates world
server sends authoritative result
client displays result
```

클라이언트가 보내는 것은 "나는 이 위치에 있다"가 아니라 "나는 이 방향으로 이동하려 했다"에 가까워야 한다. 서버는 이동 가능성, 충돌, 쿨다운, 탄약, 피해 판정, 게임 규칙을 검증한다. 클라이언트가 결과를 주장하면 서버 권위가 약해진다.

권위의 위치에 따라 신뢰 모델이 바뀐다.

| 모델 | 진실의 위치 | 장점 | 단점 |
|---|---|---|---|
| P2P 결정론 | 모든 클라이언트 | 서버 비용 낮음, 반응성 좋음 | 치트와 동기화 관리 어려움 |
| Listen server | 호스트 클라이언트 | 구축 쉬움 | 호스트 이점, 이탈 문제 |
| Dedicated authoritative | 별도 서버 | 공정성, 보안, 확장성 | 서버 비용, 지연 보정 필요 |

경쟁성이 높고 치트 비용이 큰 게임일수록 서버 권위가 유리하다. 협동 게임이나 소규모 비경쟁 게임은 더 가벼운 모델을 선택할 수 있다.

역사의 방향도 참고가 된다 — 초기 PC 멀티플레이(Doom)는 P2P lockstep이었고, Quake가 클라이언트-서버를, QuakeWorld가 예측을, Half-Life가 지연 보상을 더하면서 "권위 서버 + 클라이언트 표시 기법"의 현대 표준이 완성됐다. 이후 30년 가까이 이 골격이 유지되는 이유는, *신뢰* 문제의 답이 구조적으로 이것뿐이기 때문이다 — 신뢰할 수 없는 단말들 사이의 합의는 비싸고(블록체인이 그 비용의 증명이다), 신뢰할 수 있는 중재자 하나가 싸다.

## 34. 권위의 스펙트럼: 전부 아니면 전무가 아니다

실무의 권위는 이분법이 아니라 *항목별 배분* 이다. 같은 게임 안에서도 상태마다 권위 수준이 다르다.

| 상태 | 권위 | 근거 |
|---|---|---|
| 피해·사망·경제 | 서버 완전 권위 | 조작 이득이 크고 되돌리기 어려움 |
| 이동 | 서버 권위 + 클라이언트 예측 | 반응성 필수, 검증 가능 |
| 시점(에임 방향) | 클라이언트 주도, 서버는 한계 검증 | 조작해도 이득 작고, 완전 검증 불가능 |
| 코스메틱·이펙트 | 클라이언트 자율 | 게임 결과에 무관 |
| 물리 소품(잔해) | 클라이언트별 로컬 | 동기화 비용 > 가치 |

마지막 줄들이 중요하다 — *동기화하지 않기로 한 것* 도 권위 설계의 일부다. 잔해·파티클·래그돌을 클라이언트마다 다르게 두는 결정은 대역폭을 아끼는 동시에 "이 상태는 게임플레이에 영향을 주지 않는다"는 디자인 계약을 만든다(잔해에 맞아 죽는 게임이라면 그 계약이 성립하지 않는다).

중간 형태로 *클라이언트 위임 권위* 도 있다 — 비경쟁 협동 게임에서 각 클라이언트가 자기 캐릭터 위치의 권위를 갖고 서버는 중계+상한 검증만 하는 모델(많은 모바일 협동 게임, 일부 콘솔 협동 게임). 반응성이 공짜이고 서버가 얇아지는 대신, 스피드핵 수준의 치트만 막을 수 있다. "치트의 피해자가 치터 자신뿐인가"가 이 모델의 허용 조건이다.

## 35. 서버 중심 시뮬레이션 루프

서버는 고정 tick으로 입력을 처리하고 월드를 갱신한다.

```cpp
void ServerTick(uint32_t tick) {
    ReceiveInputPackets();
    QueueClientCommands(tick);

    ValidateCommands(tick);
    SimulateWorld(tick);

    BuildSnapshots(tick);
    SendPackets();
}
```

이 루프에서 중요한 것은 순서다. 입력을 먼저 모으고, 검증한 뒤, 서버 상태를 갱신하고, 그 결과를 클라이언트에 보낸다. 서버 tick이 권위 상태의 시간 단위가 되므로, 입력과 snapshot도 tick 또는 sequence를 가져야 한다.

서버가 모든 일을 한 tick 안에 끝내지 못하면 클라이언트는 지연과 흔들림을 느낀다. 따라서 서버 권위 모델은 네트워크 설계이면서 동시에 서버 성능 설계다. Tick time, snapshot encode time, packet send time을 함께 봐야 한다.

루프 바깥의 구조도 정해야 한다 — 한 프로세스에 매치 하나인가 여럿인가. 매치당 프로세스는 격리(한 매치의 크래시·스파이크가 남에게 안 번짐)와 배포 단순성을 주고, 프로세스당 다중 매치는 메모리·기동 비용을 아낀다. 작은 매치(수 명)가 많은 게임은 다중 매치+스레드 풀로 밀도를 올리고, 큰 매치(수십 명)는 프로세스 격리가 보통이다. 어느 쪽이든 *매치는 상태 기계* 다 — 대기(워밍업) → 진행 → 종료 → 정산의 수명 주기를 갖고, 그 전이가 매치메이커·세션 서비스와의 계약이 된다.

## 36. 지연을 숨기는 계층

권위 서버의 대가는 지연이다. 입력이 서버에 갔다가 결과로 돌아오기까지 RTT가 걸린다. 이 지연을 그대로 보여주면 조작감이 무겁다.

보통 세 계층으로 지연을 숨긴다.

| 계층 | 대상 | 역할 |
|---|---|---|
| 예측 | 자기 캐릭터 | 입력을 즉시 로컬 표시 |
| 보정 | 자기 캐릭터 | 서버 권위 상태와 예측 상태를 재조정 |
| 보간 | 다른 플레이어 | snapshot 사이를 부드럽게 표시 |

사격 판정처럼 플레이어가 본 화면과 서버 현재 상태가 어긋나는 영역에는 지연 보상이 들어간다. 서버는 일정 시간의 과거 상태를 보관하고, 판정 시점에 필요한 범위만 되감아 검사한다.

이 기술들은 권위를 클라이언트에 넘기는 것이 아니다. 클라이언트는 화면을 먼저 보여줄 뿐이고, 최종 상태는 서버가 결정한다.

네 기법이 만드는 *시간 지형* 을 한 장으로 정리해 두자 — 이 그림이 아키텍처 전체의 요약이다.

```text
시간축:  과거 ◀──────────────────────────▶ 미래

[원격 플레이어]      [서버 현재]        [내 캐릭터]
 서버-Δ보간            tick T            서버+Δ선행
 (보간 지연만큼 과거)                  (예측+입력 선행만큼 미래)

지연 보상 = 사격자의 "과거 화면"(왼쪽)을 서버가 되감아 판정하는 다리
```

클라이언트 화면에는 서로 다른 권위 시점이 함께 그려진다. 내 캐릭터는 미확정 local input만큼 권위 server state보다 앞서 예측되고, 상대는 추정 server time보다 interpolation delay만큼 과거에 표시된다. server가 사격을 받을 때 재구성해야 하는 간극에는 command의 outbound delay와 interpolation delay가 들어간다. 이 시간 차이와 어느 화면을 존중할지 정한 정책이 “맞췄는데/피했는데” 논쟁의 원천이다.

## 37. 한 발의 총알: 전체 흐름 추적

부품들이 맞물리는 모습을 한 발의 사격으로 따라가 보자. RTT 80ms, 서버 64Hz, 스냅샷 32Hz, 보간 지연 100ms 가정.

```text
t=0ms    [클라] 발사 입력. 총구 화염·사운드 즉시 재생(연출 예측).
         크로스헤어 아래의 적은 추정 server time보다 약 100ms 과거의 모습.
         입력 command(틱·시점 정보 포함)를 패킷에 실어 송신.
t=40ms   [서버] 입력 도착, 입력 버퍼에 정렬.
t=47ms   [서버] 해당 틱 실행: 사격 검증(탄약·쿨다운·시야각)
         → 지연 보상: 현재 서버 시각 기준 약 140ms 과거로 hitbox 되감기
         → 명중 판정, 피해 적용. 권위 상태에 기록.
t=47ms+  [서버] 다음 스냅샷에 결과 포함: 대상 체력, 사망 여부,
         피격 이벤트(reliable), lastProcessedInput 갱신.
t=87ms   [클라(사격자)] 스냅샷 도착. 히트 마커·피해 숫자 표시(결과는 예측하지 않았음).
         pending input 정리, 예측 상태 재조정(이동 예측분).
t=87ms~  [클라(피격자)] 자기 체력 감소 수신. 피격 연출.
         체감상 "엄폐 직후 맞음"일 수 있음 — 보상 정책의 비용.
```

이 표에는 입력 command와 선행 예측, 서버 입력 버퍼와 틱, 보간된 과거, 되감기 판정, 스냅샷과 reliable 이벤트가 한 흐름으로 연결된다. 아키텍처 설계란 이 흐름의 각 단계에 *어느 부품과 어느 정책을 배치할지* 정하는 일이다.

## 38. 보안 경계

서버 권위 모델에서도 클라이언트를 완전히 믿지 않는다는 원칙을 유지해야 한다. 클라이언트는 수정될 수 있고, 입력은 조작될 수 있으며, 메모리는 읽힐 수 있다.

서버에서 검증해야 하는 대표 항목은 다음과 같다.

- 이동 속도와 가속도.
- 충돌과 벽 통과 여부.
- 발사 속도, 탄약, 재장전 시간.
- 스킬 쿨다운과 자원 소모.
- 시야와 거리 기반 상호작용.
- 아이템 획득과 거래 조건.

검증의 수위에는 실무적 판단이 들어간다 — *완전 재시뮬레이션* (서버가 이동을 처음부터 자기가 계산: 가장 강하지만 입력의 의미론이 단순해야 함)과 *상한 검증* (클라이언트 주장 결과를 받되 물리 한계로 클램프: 약하지만 유연함) 사이의 선택이 항목마다 다르다. 이동은 재시뮬레이션이 표준이고, 에임은 상한 검증(회전 속도 한계)밖에 없다 — 에임 자체의 정당성(에임봇 여부)은 넷코드가 아니라 행동 분석·안티치트의 영역으로 넘어간다. 권위 서버는 *규칙 위반* 을 막을 뿐, *비인간적으로 정확한 합법 입력* 은 막지 못한다는 한계를 명확히 인식해야 시스템 간 책임 분담이 선다.

중요한 정보는 보내지 않는 것이 가장 강한 방어다. 클라이언트가 알 필요 없는 적 위치, 숨겨진 오브젝트, 서버 내부 난수 상태를 보내면 표시만 숨겨도 치트가 읽을 수 있다.

## 39. 서버 코드의 구조적 요구

권위 모델은 코드 구조에도 요구를 건다. 자주 부딪히는 세 가지:

**클라이언트/서버 코드의 공유와 분리.** 이동·물리 같은 예측 대상 로직은 양쪽이 *공유* 해야 하고(예측의 전제), 판정·경제 같은 권위 로직은 서버 *전용* 이어야 하며(정보 노출 방지), 연출은 클라이언트 전용이다. 한 코드베이스에서 이 세 구역을 — 모듈 경계로든 빌드 플래그로든 — 구조적으로 가르지 않으면, "서버 전용이어야 할 드랍 테이블이 클라이언트 바이너리에 들어 있는" 부류의 사고가 난다.

**헤드리스 실행.** 서버 빌드는 렌더링·오디오·입력 없이 돌아야 한다. 게임 로직이 렌더 객체를 직접 쥐고 있으면 서버 빌드가 성립하지 않으므로, 권위 모델의 채택은 사실상 *로직과 표현의 분리* 를 강제한다 — 거꾸로 말해, 이 분리가 되어 있으면 리플레이·관전·봇 테스트가 거의 공짜로 따라온다.

**시뮬레이션의 자기 완결성.** 서버 틱은 입력 큐와 권위 상태만으로 진행되어야 한다 — 벽시계, 파일 I/O, 외부 서비스 호출이 틱 안에 끼면 재현성과 틱 예산이 함께 무너진다. 외부 세계(DB 저장, 매치 결과 보고)와의 접점은 틱 바깥의 비동기 경계로 밀어낸다.

## 40. 확장성과 운영 구조

권위 서버는 서버 비용을 만든다. 모든 match, zone, shard는 CPU, 메모리, 네트워크 egress를 사용한다. 운영 구조는 게임 규모에 맞게 선택해야 한다.

| 규모 | 흔한 구조 |
|---|---|
| 소규모 방 | match 단위 dedicated server |
| 협동 세션 | listen server 또는 lightweight dedicated |
| 대규모 월드 | zone/shard 분할 |
| 글로벌 서비스 | region 기반 배치와 matchmaking |

서버 확장은 단순히 인스턴스를 많이 띄우는 문제가 아니다. 플레이어를 어느 region에 배치할지, 같은 party를 어떻게 묶을지, match 서버가 죽을 때 어떻게 복구할지, 서버 밀도를 얼마나 높일지 결정해야 한다.

권위 서버는 상태를 가진다. 따라서 stateless HTTP 서버처럼 요청마다 아무 인스턴스로 보내면 안 된다. 한 match의 입력은 같은 시뮬레이션 인스턴스로 가야 한다. 이 *상태성* 이 운영의 모든 어려움의 뿌리다 — 무중단 배포가 안 되고(진행 중 매치는 끝까지 옛 버전으로), 장애 복구는 "다른 인스턴스로 넘기기"가 아니라 "매치 포기 또는 상태 스냅샷 복원"이며, 오토스케일링은 "새 매치를 새 인스턴스로"만 가능하고 진행 중인 것은 건드릴 수 없다. 매치 단위의 짧은 수명(수십 분)이 그나마 이 어려움을 견딜 만하게 만든다 — 영속 월드(MMO)는 같은 문제를 훨씬 무겁게 진다.

대규모 월드의 zone 분할에는 추가 난제가 있다 — *경계* 다. 인접 zone을 맡은 두 서버 사이에서 플레이어가 경계를 넘을 때의 상태 이관, 경계에 걸친 상호작용(경계 너머로 사격)의 판정 권위. seamless world를 표방하는 게임의 엔지니어링 비용 대부분이 이 경계 처리에 들어간다.

## 41. 선택 기준

권위 서버가 항상 정답은 아니다. 선택 기준은 장르와 위험이다.

권위 서버가 강한 경우:

- 경쟁성이 높다.
- 치트 방어가 중요하다.
- 플레이어 수가 많다.
- 서버가 경제, 인벤토리, 랭크를 검증해야 한다.
- 클라이언트 플랫폼을 완전히 신뢰할 수 없다.

다른 모델이 나을 수 있는 경우:

- 1대1 또는 소규모 입력 동기화 게임이다 — 격투의 P2P 롤백이 대표.
- 서버 비용을 극도로 낮춰야 한다.
- 완전한 결정론 구현이 가능하다 — RTS의 lockstep 전통.
- 치트 영향이 낮거나 친선 플레이 중심이다.

결국 선택은 지연, 비용, 보안, 구현 난이도의 균형이다. 서버 권위는 보안을 얻는 대신 지연 보정과 운영 비용을 지불한다. 그리고 선택은 *되돌리기 어렵다* — 권위 모델은 [서버 코드 구조](#39-서버-코드의-구조적-요구), [인프라와 운영](#40-확장성과-운영-구조), 게임 디자인의 판정 정책에 깊이 배어들므로 프로토타입 단계의 임시 선택이 그대로 굳는 경우가 많다. 첫 수직 슬라이스를 만들기 전에 내려야 할 결정 목록의 맨 위에 있어야 하는 이유다.


---

## 결론

권위 서버 구조의 핵심은 서버가 최종 판정을 소유하면서도 클라이언트가 지연을 직접 느끼지 않게 하는 것이다. 각 시간축의 기준과 보정 규칙을 명시하고, 지연과 손실이 커질 때 공정성과 시각 품질 중 무엇을 보존할지 결정해야 한다.

## 더 읽을거리

- Gabriel Gambetta, [Fast-Paced Multiplayer](https://www.gabrielgambetta.com/client-server-game-architecture.html) 및 [Entity Interpolation](https://www.gabrielgambetta.com/entity-interpolation.html) — 예측·재조정·보간의 단계별 입문.
- Yahn W. Bernier, [Latency Compensating Methods in Client/Server In-game Protocol Design and Optimization](https://developer.valvesoftware.com/wiki/Latency_Compensating_Methods_in_Client/Server_In-game_Protocol_Design_and_Optimization) (GDC 2001) — Half-Life의 예측·재조정·lag compensation 원전.
- Glenn Fiedler, [Snapshot Interpolation](https://gafferongames.com/post/snapshot_interpolation/), [Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/), [What Every Programmer Needs To Know About Game Networking](https://gafferongames.com/post/what_every_programmer_needs_to_know_about_game_networking/) — 보간, 고정 틱, 권위 모델의 배경.
- Jared Cone, *It IS Rocket Science! The Physics of Rocket League Detailed* (GDC 2018) — 물리 차량의 예측·재조정 사례.
- Timothy Ford, [Overwatch Gameplay Architecture and Netcode](https://www.youtube.com/watch?v=W3aieHjyNvw) (GDC 2017) — 예측 범위, 적응형 입력 버퍼, 클라이언트 선행과 판정 정책.
- Valve Developer Wiki, [Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking), [Interpolation](https://developer.valvesoftware.com/wiki/Interpolation), [Lag Compensation](https://developer.valvesoftware.com/wiki/Lag_Compensation) — Source 엔진의 틱·보간·hitbox rewind 계약.
- IEEE DIS(Distributed Interactive Simulation) 표준 — 운동 모델 기반 dead reckoning의 표준화된 형태.
- David L. Mills, [RFC 5905: Network Time Protocol Version 4](https://www.rfc-editor.org/rfc/rfc5905) — §8은 왕복 지연·오프셋 계산과 비대칭 한계, §10은 clock filter, §12는 clock discipline을 정의한다.
- Flaviu Cristian, *Probabilistic Clock Synchronization* (Distributed Computing 3, 1989) — 단방향 지연을 RTT/2로 추정할 때의 오차 경계.
- David Aldridge, *I Shot You First: Networking the Gameplay of Halo: Reach* (GDC 2011) — 권위·판정 정책·대역폭 운영 사례.
- Riot Games, [VALORANT's 128-Tick Servers](https://technology.riotgames.com/news/valorants-128-tick-servers) — 서버 tick budget 최적화 사례.
