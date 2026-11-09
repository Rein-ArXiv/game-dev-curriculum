## 들어가며

권위 서버의 상태를 모든 클라이언트에 그대로 복사할 수는 없다. 엔티티 수, 틱레이트, 패킷 예산이 제한되어 있으므로 무엇을 누구에게 얼마나 자주 어떤 정밀도로 보낼지 결정해야 한다.

이 글은 snapshot과 baseline·delta, interest management, 우선순위, quantization과 bit packing을 하나의 상태 복제 예산 문제로 다룬다.

---

## 목차

1. Snapshot의 역할
2. Full Snapshot과 Delta Snapshot
3. Baseline과 Ack
4. Quantization: 값을 줄이는 기술
5. Entity State 설계
6. 생성과 소멸의 복제
7. Priority와 Budget
8. 손실 복구
9. 검증: 스냅샷 시스템의 계측
10. 왜 AOI가 필요한가
11. Relevancy의 기준
12. 공간 분할 기반 AOI
13. AOI 전이의 수명 주기
14. Priority와 Update Rate
15. Hysteresis와 Thrashing 방지
16. 구조의 확장: Replication Graph
17. 보안과 정보 은닉
18. 예산을 숫자로 잡기
19. Packet Rate와 Packet Size
20. Bit Packing
21. 사례 연구: 엔티티 한 개의 다이어트
22. 예산 내 트랜잭션 조립
23. 상행 트래픽과 비용
24. 계측 심화: 필드 귀속과 합성 부하

---


## 1. Snapshot의 역할

Snapshot은 특정 서버 tick에서 클라이언트가 알아야 하는 상태 묶음이다.

```cpp
struct EntityState {
    uint32_t id;
    uint16_t type;
    QuantizedVec3 position;
    QuantizedQuat rotation;
    uint16_t flags;
};

struct Snapshot {
    uint32_t serverTick;
    uint32_t baselineTick;
    uint32_t lastProcessedInput;
    std::vector<EntityState> entities;
};
```

`serverTick`은 이 상태가 서버의 어느 시점인지 나타낸다. `baselineTick`은 delta가 어떤 상태를 기준으로 만들어졌는지 나타낸다. `lastProcessedInput`은 해당 클라이언트의 입력 중 서버가 어디까지 처리했는지 알려준다 — 클라이언트 예측의 재조정이 이 한 필드에 걸려 있으므로, 스냅샷 구조의 일부로 못 박아 두는 것이 옳다.

Snapshot rate는 simulation tick rate와 같을 필요가 없다. 예를 들어 simulation 60Hz와 snapshot 20Hz를 독립적으로 둘 수 있다. 높은 snapshot rate는 sample 간격과 새 정보가 송신되기까지의 대기 상한을 줄이는 대신 bandwidth, PPS와 serialization CPU를 늘린다. interpolation delay는 snapshot rate만으로 정해지지 않고 network jitter·loss와 playout policy가 결정하지만, sample 간격보다 너무 얕은 buffer는 underrun에 취약하다. 장르 이름으로 20Hz·60Hz를 고정하지 않고 motion error, correction, bandwidth와 server CPU의 분포로 rate를 정한다.

중요한 불변식 하나 — snapshot은 *어느 한 tick의 일관된 단면* 이어야 한다. 엔티티 절반은 tick 100, 절반은 tick 101의 상태로 섞이면, 클라이언트 화면에서 서로 닿아 있는 두 캐릭터가 다른 시간에 살게 된다. 시뮬레이션이 끝난 직후의 상태를 한 번에 캡처하거나, 직렬화 중 시뮬레이션이 끼어들지 못하게 순서를 잡아야 한다.

## 2. Full Snapshot과 Delta Snapshot

Full snapshot은 필요한 모든 상태를 담는다. 구현은 쉽지만 비싸다. Delta snapshot은 baseline과 달라진 필드만 담는다.

```cpp
void WriteDelta(BitWriter& w,
                const EntityState& current,
                const EntityState& baseline) {
    w.WriteBits(current.id, 20);

    bool posChanged = current.position != baseline.position;
    bool rotChanged = current.rotation != baseline.rotation;
    bool flagsChanged = current.flags != baseline.flags;

    w.WriteBit(posChanged);
    w.WriteBit(rotChanged);
    w.WriteBit(flagsChanged);

    if (posChanged) w.WriteVec3(current.position);
    if (rotChanged) w.WriteQuat(current.rotation);
    if (flagsChanged) w.WriteBits(current.flags, 16);
}
```

Delta는 변화가 적을수록 강하다. 위치가 거의 매번 바뀌는 캐릭터도 회전, 애니메이션, 체력, 장비 상태는 매번 바뀌지 않는다. 필드별 change mask만으로도 큰 절감이 생긴다.

절감의 크기를 감 잡아 두자. 엔티티 하나의 full 인코딩이 200비트라 할 때, 변경 엔티티만 나열하는 포맷에서는 "안 바뀐 엔티티"가 delta 본문에 아예 실리지 않는다. 다만 패킷의 엔티티 개수·종료 표식 같은 컨테이너 오버헤드는 남는다. "위치만 바뀐 엔티티"는 id 20비트 + 마스크 3비트 + 위치 ~50비트 ≈ 73비트로 1/3이 된다. 전형적 장면에서 절감 폭은 변경률과 포맷에 따라 크게 달라지므로, full 대비 배율은 실제 장면을 캡처해 측정해야 한다. Quake 3가 널리 알린 클라이언트 ack 기반 delta 구조는 이후 많은 스냅샷 엔진의 기본 골격이 되었다.

change flag를 field마다 byte로 쓰면 overhead가 커지므로 flag들을 bit mask word로 묶거나 BitWriter로 연속 pack한다. byte-oriented serializer도 mask 자체를 `uint8_t/uint16_t`로 정의하면 flag 하나당 1바이트를 쓸 필요는 없다. field 수가 많으면 group mask, sparse field ID 또는 자주·드물게 바뀌는 group을 corpus에서 비교한다.

## 3. Baseline과 Ack

Delta를 적용하려면 서버와 클라이언트가 같은 baseline을 공유해야 한다. 서버는 클라이언트가 어떤 snapshot을 받았는지 ack로 확인하고, 그 snapshot을 기준으로 delta를 만든다.

```text
server snapshot 100 전송
client snapshot 100 수신
client packet에 ackSnapshot=100 포함
server는 client가 100을 가진다고 판단
server snapshot 105를 baseline 100 대비 delta로 전송
```

패킷 손실이 있으면 baseline이 어긋날 수 있다. 클라이언트가 baseline을 갖고 있지 않은 delta를 받으면 적용할 수 없다. 이때는 해당 delta를 버리고 다음 full snapshot 또는 다른 baseline delta를 기다린다.

서버는 클라이언트마다 마지막으로 ack된 baseline을 추적한다. 클라이언트 A는 snapshot 100을 ack했고, 클라이언트 B는 98까지만 ack했을 수 있다.

메모리 구조는 스냅샷 생성 방식에 따라 달라진다. 모든 클라이언트가 같은 월드 상태를 같은 양자화 규칙으로 받는 단순 모델이라면 과거 월드 스냅샷은 공용 history 한 벌로 두고, 클라이언트별로 ack tick만 보관할 수 있다. 그러나 AOI, 권한별 정보 은닉, 클라이언트별 우선순위 때문에 전송 상태가 달라지면 baseline의 가시 엔티티 집합과 인코딩 결과도 클라이언트별이다. 이 경우 공용 월드 history에 더해 클라이언트별 전송 history 또는 재구성 가능한 baseline 상태가 필요하다. history 깊이는 대략 `허용할 RTT·손실 구간 + 여유`로 정하고 실제 메모리 비용을 측정해야 한다.

baseline 선택 정책도 명시한다. 가장 최근 ack된 snapshot은 변화 구간이 짧아 흔한 후보지만 encoded delta가 항상 최소라는 보장은 없다. AOI transition, sparse update와 retained history에 따라 다른 acked baseline이 더 작을 수 있다. 후보 수를 제한해 예상 byte를 비교하거나 recent baseline 하나를 쓰되, history 만료·delta 비대화·schema reset에서는 full 또는 entity-level refresh로 전환한다. 선택한 baseline ID와 client possession은 인증된 ack로 검증한다.

## 4. Quantization: 값을 줄이는 기술

Delta가 "보낼 필드를 줄인다"면 quantization은 "필드당 비트를 줄인다". 원리는 하나다 — *게임에 필요한 범위와 정밀도만 남긴다*.

**위치**: 맵 크기와 허용 오차로 비트 수가 결정된다. 4km 맵, 1cm 정밀도면 축당 `log2(400000) ≈ 19비트`, 3축 57비트 — float3 96비트의 60%다. 실내 게임(100m, 1cm)이면 축당 14비트로 42비트까지 내려간다.

```cpp
// 범위 기반 양자화: [min, max] 를 2^bits 단계로. 실패하면 nullopt.
// float 입력이 finite여도 (max-min)이나 (v-min)이 float 범위를 넘을 수 있으므로
// 차분을 처음부터 double로 올린다.
std::optional<std::uint32_t> QuantizeFloatChecked(
    float v, float min, float max, int bits) {
    if (bits < 1 || bits > 32 ||
        !std::isfinite(v) || !std::isfinite(min) ||
        !std::isfinite(max) || !(min < max)) {
        return std::nullopt;
    }
    const double dMin = min;
    const double dMax = max;
    const double x = std::clamp(static_cast<double>(v), dMin, dMax);
    const double t = std::clamp((x - dMin) / (dMax - dMin), 0.0, 1.0);
    const double maxCode = std::ldexp(1.0, bits) - 1.0;
    const double q = std::clamp(std::round(t * maxCode), 0.0, maxCode);
    return static_cast<std::uint32_t>(q);
}
```

`1u << bits`는 `bits == 32`에서 정의되지 않으므로 단계 수 계산은 `std::ldexp`나 64비트로 올린다. 위 예제는 유한한 범위 밖 `v`를 clamp하고, NaN·무한대나 `bits` 범위 오류처럼 정의되지 않은 입력은 오류로 돌려준다. float 자체의 정밀도 때문에 32비트 코드 수가 불가능한 것이 아니라 중간 차분의 범위가 문제다. wire 호환이 필요하면 반올림 규칙, 허용 오차, 역양자화 식과 범위 밖 입력의 실패 규약을 함께 고정하고 복호화 오차까지 테스트한다.

범위 폭을 $W$, round-to-nearest 양자화의 최대 허용 오차를 $\varepsilon$라 하면 step $\Delta=W/(2^b-1)$가 $2\varepsilon$ 이하여야 한다.

$$
b\ge\left\lceil\log_2\left(\frac{W}{2\varepsilon}+1\right)\right\rceil.
$$

“허용 오차”를 code 간격 전체로 정의하면 식의 2가 빠지므로 format 문서에서 resolution과 max error를 구분한다. bit를 하나 늘리면 큰 b에서 step이 대략 절반이 된다.

**회전**: 단위 quaternion은 *smallest-three*로 줄일 수 있다. 절댓값이 가장 큰 성분의 index를 보내고 부호를 양수로 정규화한 뒤 나머지 세 성분을 양자화한다. 빠진 성분은 `sqrt(max(0, 1-x²-y²-z²))`로 복원한다. 남는 성분 범위는 $[-1/\sqrt2,1/\sqrt2]$다. 성분당 bit 수는 “시각적으로 충분한 9~10bit”라는 상수가 아니라 angular-error max·p99를 encoder test로 측정해 정한다.

**속도·기타**: velocity에 design상 normal range가 있어도 teleport, impulse와 numerical failure가 범위를 넘을 수 있다. clamp가 허용되는 presentation field인지, escape code로 full precision을 보낼지, packet을 거부할지를 명시한다. health·ammo·animation time도 실제 range, sentinel과 prediction error budget에서 bit 수를 계산한다.

quantization의 함정은 *서버와 클라이언트의 비교 기준* 이다. delta의 change 판정은 양자화 *후* 값으로 해야 한다 — 원본 float가 미세하게 흔들려도 양자화 값이 같으면 "안 바뀜"이다. 반대로 서버 내부 시뮬레이션 값과 클라이언트 표시 값이 양자화만큼 어긋나는 것은 받아들인다(그 허용 오차가 비트 수를 정한 근거였다). 예측·재조정에 쓰는 비교 임계도 양자화 단위보다 커야 한다 — 아니면 양자화 잡음이 매 스냅샷 보정을 일으킨다.

## 5. Entity State 설계

Snapshot에 엔티티의 내부 객체 전체를 넣으면 안 된다. 네트워크 상태는 별도 표현이어야 한다.

| 내부 상태 | 네트워크 상태 |
|---|---|
| float position[3] | 범위 기반 quantized position |
| animation graph 전체 | animation state id + normalized time |
| AI blackboard | 필요한 공개 상태만 |
| inventory object graph | 장착/표시가 필요한 id만 |
| physics body 전체 | 위치, 회전, 속도 일부 |

네트워크 상태는 세 가지 질문으로 정한다.

- 클라이언트가 표시하거나 예측하는 데 필요한가.
- 매 snapshot마다 보내야 하는가.
- 범위와 정밀도를 줄여도 플레이어가 느끼지 못하는가.

둘째 질문은 channel 분류로 이어진다. transient command/event는 reliable ordered channel 후보이고, 현재 장비처럼 오래 유지되며 late join·resync가 복원해야 하는 값은 discrete여도 replicated state에 속한다. reliable “장비 교체” event만 저장하면 접속 중간의 client가 현재 장비를 알 수 없으므로 initial/full state 또는 주기적 state refresh가 필요하다. 위치 같은 연속 상태는 최신 sample이 이전 것을 대체할 수 있지만 spawn·inventory transaction처럼 한 번의 전이가 의미를 가지는 데이터는 deduplication과 ordering을 갖춘 event가 필요하다.

정밀도는 게임 디자인과 연결된다. 1cm 정확도가 필요 없는 원거리 캐릭터를 32-bit float로 보내면 대역폭을 낭비한다. 더 나아가면 *거리별 정밀도 LOD* 가 된다 — 가까운 엔티티는 1cm·30Hz, 먼 엔티티는 10cm·10Hz. 7절의 priority가 "보낼지 말지"를 고른다면, 정밀도 LOD는 "보낼 때 얼마나 거칠게"를 고른다.

## 6. 생성과 소멸의 복제

스냅샷의 암묵적 난제는 "달라진 값"이 아니라 *"생기고 사라지는 엔티티"* 다. delta는 양쪽이 같은 엔티티 집합을 안다고 가정하는데, 그 집합 자체가 변한다.

한 구조는 snapshot에 *존재 정보*를 함께 싣는 것이다. baseline에 있었는데 현재에 없으면 remove record를, baseline에 없었는데 현재에 있으면 create record를 쓴다. create는 type/archetype과 client가 entity를 구성하는 데 필요한 initial replicated state를 담으며 server 내부 object 전체라는 뜻의 “full”이 아니다. wire section과 dependency 순서를 명시해 component update가 아직 생성되지 않은 entity를 참조하지 않게 한다.

```text
delta(baseline=100, current=105) 의 구성:
  removed:  [id 17, id 42]                  ← 사라진 엔티티
  updated:  [id 3 (pos), id 8 (pos+rot)]    ← change mask 기반
  created:  [id 99: type, full state]       ← 신규는 항상 full
```

함정 둘. 첫째, **id 재사용**. 엔티티 17이 죽고 새 엔티티가 id 17을 재사용하면, 낡은 baseline을 가진 클라이언트에게 "17 갱신"은 *다른 객체에 대한 delta* 가 된다. 해법은 id에 세대(generation)를 박거나(슬롯 맵과 같은 발상), id 재사용을 충분히 늦추는 것이다. 둘째, **소멸의 신뢰성**. 생성은 다음 스냅샷에도 (여전히 존재하므로) 다시 실리지만, 삭제 레코드가 유실된 채 그 엔티티가 영영 안 실리면 클라이언트에 유령이 남는다. 삭제는 "baseline에 있는 한 계속 실린다"는 규칙으로 자연 복구되도록 delta 생성을 짜야 한다 — baseline 대비 비교라는 정의를 정확히 지키면 공짜로 얻어지고, "한 번 보냈으니 끝"으로 최적화하는 순간 유령 버그가 들어온다.

## 7. Priority와 Budget

서버는 한 클라이언트에게 무한히 많은 상태를 보낼 수 없다. 클라이언트별 송신 budget을 정하고, 그 안에서 엔티티와 필드 우선순위를 선택한다.

```cpp
float ComputePriority(const Entity& e, const ClientView& view) {
    float distanceScore = 1.0f / (1.0f + Distance(e.pos, view.pos));
    float visibilityScore = IsVisible(e, view) ? 2.0f : 0.5f;
    float importanceScore = e.IsPlayer() ? 3.0f : 1.0f;
    float starvationScore = 0.1f * e.ticksSinceLastSent;
    return distanceScore * visibilityScore * importanceScore + starvationScore;
}
```

Priority는 가까움, 가시성, 게임플레이 중요도, 마지막 전송 이후 시간으로 구성한다. `starvationScore`가 없으면 낮은 우선순위 엔티티가 영원히 전송되지 않을 수 있다.

priority accumulator는 현재 가치와 오래 미전송된 시간을 한 score로 합치는 실용적인 휴리스틱이다. 다만 `priority=3`이 `priority=1`보다 정확히 세 배 자주 전송된다는 보장은 없다. record 크기, 한 tick당 최대 1회라는 포화, 동적 relevancy, tie-break와 packing 순서가 실제 빈도를 바꾼다.

```cpp
// 우선순위 누적기 — 패킷 조립 시 호출
void FillSnapshot(PacketWriter& w, ClientView& view) {
    for (auto& e : relevantEntities) e.accum += ComputePriority(e, view);
    SortByAccumDescending(relevantEntities);
    for (auto& e : relevantEntities) {
        size_t bytes = MeasureEntityDelta(e, view.baseline);
        if (bytes > w.Remaining()) {
            DeferEntity(e);
            continue;
        }
        auto mark = w.Mark();
        if (!w.TryWriteEntityDelta(e, view.baseline)) {
            w.Rollback(mark);
            MarkEncodingError(e);
            continue;
        }
        e.accum = 0.0f;
        e.ticksSinceLastSent = 0;
    }
}
```

맞지 않는 첫 record에서 `break`하면 큰 entity 하나가 뒤의 작은 record까지 막을 수 있으므로 `continue`와 명시적 defer를 사용한다. `TryWrite`가 부분 기록 후 실패할 수 있다면 mark/rollback 또는 사전 측정이 필요하다. size estimate와 실제 byte 차이도 계측한다.

Budget은 entity 수가 아니라 실제 wire byte와 pacer가 허용한 packet budget으로 계산한다. 예산이 줄면 accumulator가 낮은 score의 entity를 더 오래 미루는 방향으로 degrade하지만 모든 update frequency가 정확히 같은 비율로 감소하지는 않는다.

### 목표 빈도의 유체 모델과 구현 가능한 scheduler

entity $i$의 update 한 번이 $s_i$ byte, 목표 빈도가 tick당 $f_i$라면 어떤 scheduler도 다음 필요조건을 넘을 수 없다.

$$
0\le f_i\le1,\qquad \sum_i f_i s_i\le B.
$$

가중치 $p_i$에 비례하는 빈도를 **목표로** 하고 어떤 entity도 포화하지 않는 fluid model에서는

$$
f_i=\frac{p_iB}{\sum_j p_js_j}
$$

로 둘 수 있다. 이 식은 capacity plan의 출발점이지 reset-to-zero accumulator의 정리나 실제 packet packing 결과가 아니다. $f_i>1$인 entity는 1로 clamp하고 남은 budget을 다시 배분해야 하며, $s_i>B$인 record는 chunk·schema 변경 없이는 보낼 수 없다.

byte cost를 반영한 장기 service share가 필요하면 deficit round-robin 계열을 명시적으로 구현할 수 있다. 아래는 매 packet마다 첫 flow에서 시작하지 않고 지속 커서로 순회를 회전시키며, quantum은 논리 라운드가 새로 시작될 때만 보충하는 축약형이다.

```cpp
void ScheduleRound(ClientRepState& c, PacketWriter& w) {
    const size_t n = c.flows.size();
    for (size_t visited = 0; visited < n && w.Remaining() > 0; ++visited) {
        if (c.cursor >= n) {                 // 새 논리 라운드 진입 → quantum 보충
            c.cursor = 0;
            for (RepFlow& f : c.flows)
                if (f.dirty) f.deficit = std::min(f.deficit + f.quantum, f.maxDeficit);
        }
        RepFlow& f = c.flows[c.cursor++];
        if (!f.dirty) { f.deficit = 0.0f; continue; }   // 비활성 전이 시 남은 deficit 폐기

        size_t bytes = MeasureNextUpdate(f);
        if (bytes > f.deficit) continue;     // 이번 라운드 deficit 부족 → 다음 flow
        if (bytes > w.Remaining()) {         // packet 예산 부족 → 다음 packet에서 이어감
            --c.cursor;
            break;
        }
        WriteNextUpdate(w, f);
        f.deficit -= bytes;                  // reset하지 않고 실제 service cost만 차감
    }
}
```

DRR의 fairness는 persistent 활성 목록·cursor, bounded record size와 논리 라운드가 실제 packet 작성 호출과 무조건 같지는 않다는 가정 아래 quantum에 비례한 **byte service**로 성립한다. 위 구현은 학습용 축약이므로 active-set 전이, record가 deficit이나 packet 예산을 넘을 때의 이어가기/건너뛰기, 비활성 전환 시 남은 deficit 처리와 한 record가 quantum보다 클 때의 상한을 실제 정책으로 고정해야 한다. entity update frequency 자체를 보장하려면 target interval·deadline scheduler를 별도로 둔다. 예를 들어 각 entity에 `nextDueTick`, 최대 stale age와 value-per-byte를 기록하고, overdue entity를 먼저 처리하되 hard deadline을 놓친 횟수를 계측한다.

동적 combat relevance에는 단순 accumulator가 더 자연스러울 수 있고, periodic objective에는 deadline이 낫고, large reliable transfer에는 deficit queue가 낫다. 하나의 score가 freshness·importance·byte fairness를 모두 수학적으로 보장한다고 가정하지 않는다.

## 8. 손실 복구

Snapshot은 보통 unreliable이다. 손실된 snapshot을 재전송하면 이미 낡은 상태가 도착한다. 대신 다음 snapshot을 보낸다. 하지만 delta가 baseline에 의존하므로 복구 전략은 필요하다.

흔한 방법은 세 가지다.

- 주기적으로 full snapshot을 섞는다.
- ack된 baseline 중 여러 후보를 유지한다.
- 중요한 엔티티는 delta 실패 시 full entity state를 다시 보낸다.

```cpp
if (!client.HasAckedRecentBaseline()) {
    WriteFullSnapshot(packet, current);
} else {
    SnapshotId base = client.MostRecentAckedSnapshot();
    WriteDeltaSnapshot(packet, current, history[base]);
}
```

Full snapshot은 비싸지만 연결 상태를 다시 맞추는 안전장치다. Delta snapshot만으로 운영하면 한 번 baseline이 어긋났을 때 긴 시간 동안 상태를 적용하지 못할 수 있다.

acknowledged baseline 방식은 server가 client possession을 추측하지 않게 해 snapshot loss 뒤에도 적용 가능한 delta를 만들 수 있다. 그러나 ack loss·지연만으로도 server가 오래된 baseline을 계속 사용하고, history 만료나 AOI/schema 변경이 full refresh를 유발할 수 있다. 양방향이 동시에 나빠야만 full이 필요한 것은 아니다. ack age, selected-baseline age, delta/full byte ratio와 history miss를 각각 계측해 어떤 경로가 refresh를 만들었는지 구분한다.

접속 직후에는 client가 알아야 할 initial replicated state와 baseline이 필요하다. 이것은 server 내부 world 전체와 동치가 아니다. payload가 datagram budget을 넘으면 bounded chunk transfer, independently decodable region 또는 priority 기반 progressive spawn으로 나눈다. join 완료 조건과 gameplay 시작 조건을 분리해 아직 받지 못한 entity가 collision·visibility에 미치는 의미를 정의한다.

## 9. 검증: 스냅샷 시스템의 계측

스냅샷 시스템은 계측 없이는 튜닝할 수 없다. 다음 지표를 처음부터 노출한다.

| 지표 | 정상 거동 | 이상 신호 |
|---|---|---|
| 스냅샷 크기 분포 (P50/P99) | 손실률에 따라 완만히 증가 | full 비율 급증 — ack 경로 점검 |
| full snapshot 비율 | workload baseline 안에서 안정 | 급증 — ack/history/AOI 원인 분해 |
| 엔티티별 전송 간격 | priority와 대략 반비례 | 특정 엔티티 기아 — starvation 항 점검 |
| change mask 적중률 | 필드별로 안정 | 모든 필드 매번 변화 — 양자화 전 비교 버그(4절) |
| budget 사용률 | 품질 SLO와 함께 안정 | 포화 지속 + stale-age 증가 — budget 과소 |

특히 "change mask가 매번 전부 켜진다"는 4절의 고전 버그(양자화 전 float 비교)의 시그니처라 자동 경보를 걸 가치가 있다. 대역폭 그래프가 이론치(엔티티 수 × 평균 delta 크기 × 송신율)와 크게 어긋나면, 어딘가에서 delta가 무력화되고 있는 것이다.



## 10. 왜 AOI가 필요한가

대역폭 비용은 클라이언트 수와 엔티티 수의 곱으로 증가한다.

```text
clients = 100
entities = 5,000
entity delta = 20 bytes
snapshot rate = 20 Hz

naive = 100 * 5,000 * 20 * 20 = 200 MB/s
```

이 수치는 게임플레이 서버 하나가 감당하기 어렵다. 실제로는 헤더, reliable 이벤트, voice, retransmission, 암호화 오버헤드도 있다.

AOI는 클라이언트마다 필요한 엔티티만 고른다. 플레이어 주변 200개만 보내면 같은 계산은 8MB/s로 줄어든다. 중요도 기반 update rate까지 적용하면 더 줄어든다.

CPU 측도 같은 곱셈에 묶여 있다는 점을 함께 봐야 한다 — 클라이언트별 delta 직렬화는 `클라이언트 수 × 후보 엔티티 수`에 비례한다. AOI는 그 후보 수를 줄여 직렬화 CPU를 함께 깎는다. 배틀로얄(100명, 수만 엔티티)이 AOI 없이는 대역폭 이전에 서버 틱 예산에서 먼저 무너지는 이유다.

## 11. Relevancy의 기준

관련성은 거리 하나로만 결정되지 않는다. 장르와 시스템에 따라 여러 기준을 결합한다.

| 기준 | 설명 |
|---|---|
| 거리 | 가까운 엔티티 우선 |
| 가시성 | 벽, 포털, PVS, 시야각 |
| 팀/소유권 | 파티원, 소환물, 자신의 투사체 |
| 게임플레이 중요도 | 보스, objective, 폭발물 |
| UI 필요성 | 맵 아이콘, 점령 상태 |
| 사운드/이펙트 | 화면 밖이지만 들리는 이벤트 |

Relevancy는 boolean일 수도 있고 score일 수도 있다. 작은 게임은 포함/제외만으로 충분하다. 큰 게임은 score를 계산해 budget 안에서 높은 것부터 보낸다.

```cpp
float RelevancyScore(const ClientView& view, const Entity& e) {
    float score = 0.0f;
    score += DistanceScore(view.position, e.position);
    score += IsVisible(view, e) ? 2.0f : 0.0f;
    score += e.IsObjective() ? 3.0f : 0.0f;
    score += e.owner == view.player ? 5.0f : 0.0f;
    return score;
}
```

기준들의 비용 차이를 알고 결합해야 한다. 거리는 뺄셈 한 번이고, 팀/소유권은 비교 한 번이지만, *가시성은 레이캐스트 또는 PVS 조회* 다. 레이캐스트를 모든 (클라이언트, 후보) 쌍에 돌리면 그것이 새 병목이 된다. 실전 배치는 싼 기준으로 후보를 좁힌 뒤 비싼 기준을 통과한 것에만 적용하는 깔때기다 — 거리/팀(전수) → 시야각 원뿔(통과분) → 레이캐스트/PVS(최종 소수). 미리 계산된 PVS(섹터별 가시 섹터 집합)가 있는 게임은 레이캐스트 없이 조회로 끝나므로, 실내 중심 게임에서 가시성 기준의 비용 구조가 완전히 달라진다.

장르가 기준의 가중을 정한다 — 배틀로얄은 거리 중심(넓은 야외, 시야 은닉의 게임적 가치가 낮음), 전술 FPS는 가시성 중심(월핵 방어가 1급 요구), MMO는 거리 + 사회적 관계(파티·길드원은 멀어도 관련) 중심이다.

## 12. 공간 분할 기반 AOI

가장 흔한 구현은 spatial grid나 quadtree를 이용해 후보를 줄이는 것이다. 모든 엔티티를 매번 검사하지 않고, 플레이어 주변 셀만 본다.

```cpp
std::vector<EntityId> QueryAOI(const ClientView& view,
                               const SpatialGrid& grid) {
    std::vector<EntityId> result;

    for (CellId cell : grid.CellsOverlapping(view.aoiBounds)) {
        for (EntityId id : grid.EntitiesInCell(cell)) {
            if (IsRelevant(view, id)) {
                result.push_back(id);
            }
        }
    }
    return result;
}
```

균일한 월드는 grid가 단순하고 빠르다. 밀도가 크게 다른 월드는 quadtree, loose octree, portal/PVS, zone graph 같은 구조가 필요할 수 있다. 중요한 점은 AOI가 snapshot 생성 경로에 있으므로, 복잡한 쿼리가 서버 tick을 밀어내면 안 된다는 것이다.

grid cell size $c$와 query 반경 $r$이면 축당 검사 cell 수는 경계 정렬에 따라 대략 $\lceil2r/c\rceil+1$ 규모이고, 후보 수는 cell density에 좌우된다. 작은 c는 cell traversal·hash overhead를, 큰 c는 false candidate 검사를 늘린다. entity 이동 update rate도 `속도/c`와 경계 분포에 비례하므로 representative density·speed trace로 query CPU와 update CPU의 합을 최소화한다.

질의 방향을 뒤집을 수도 있다. client마다 주변 entity를 찾는 pull 방식과 entity 이동 때 영향받는 subscription을 갱신하는 push 방식의 비용은 각각 view 수, mover 수, overlap fan-out과 update 빈도의 함수다. 정적 entity가 많고 mover가 적으면 push가 유리할 수 있지만 한 mover가 많은 client 영역을 가로지르면 fan-out이 커진다. 두 경로를 섞어 spatial membership은 push로 유지하고 snapshot 때 client node를 pull하는 구조도 가능하다.

## 13. AOI 전이의 수명 주기

AOI는 집합의 *순간값* 이 아니라 *전이* 가 일을 만든다. 클라이언트 관점에서 엔티티는 세 상태를 오간다 — 모름 → 알고 있음(in AOI) → 잊음.

**진입(모름 → 앎)**: delta의 기준이 될 baseline이 없으므로 *full state 생성 레코드* 가 나가야 한다. 이것이 AOI의 숨은 비용 — 진입이 잦으면 full 전송이 잦다. 빠르게 이동하는 플레이어(차량 탑승)는 초당 수십 개의 엔티티를 새로 만나므로, 진입 full 비용이 평시 delta 비용을 넘어설 수 있다. 진입 순간 한 번에 모두 보내지 않고 우선순위 누적기에 태워 수 틱에 걸쳐 흘리는 것(가까운 것부터)이 표준 완충이다.

**이탈(앎 → 잊음)**: 두 가지 의미론이 있다. *despawn* — 클라이언트가 엔티티를 완전히 파괴(메모리 회수, 다시 만나면 full 재전송). *dormancy(휴면)* — 클라이언트가 마지막 상태를 보존한 채 갱신만 중단(다시 만나면 그 상태 기준 delta 재개). 자주 재진입하는 엔티티(경계 근처 배회)는 dormancy가 진입 비용을 없애 주고, 영영 안 볼 엔티티는 despawn이 클라이언트 메모리를 지킨다. 거리 기반 단계화 — 가까운 이탈은 dormancy, 먼 이탈은 despawn — 가 실용적이다.

휴면에서 delta를 재개하려면 양쪽이 같은 확인된 baseline을 보존하고, 휴면 중 변화도 그 baseline과 비교해 포함해야 한다. baseline이 만료되었거나 세대·스키마가 바뀌었다면 full refresh가 필요하다.

서버 측 자료구조도 이 수명에 묶인다 — 클라이언트별 "알고 있는 엔티티 집합 + 엔티티별 마지막 ack 상태"가 delta 복제의 전제이고, AOI 전이는 그 집합의 삽입/삭제다. 클라이언트별 메모리 예산은 active·dormant·retained baseline을 모두 포함해야 한다. 활성 AOI 상한(최대 N개)은 매 순간 활성인 엔티티 수만 제한하고, dormancy로 보존한 entry는 서로 겹치지 않는 지역 k곳을 방문하면 kN까지 쌓일 수 있다. 따라서 dormant entry의 개수·수명과 eviction/full-refresh 규칙을 별도로 정해야 메모리 상한이 된다.

## 14. Priority와 Update Rate

AOI에 포함된 엔티티도 모두 같은 빈도로 보낼 필요가 없다.

| 엔티티 | 송신 빈도 예 |
|---|---|
| 내 캐릭터 권위 상태 | snapshot마다 |
| 가까운 적 플레이어 | 높음 |
| 먼 플레이어 | 낮음 |
| 정적 오브젝트 | 변화 시 |
| 맵 objective | 낮지만 reliable event 병행 |

Priority accumulator 방식은 starvation 위험을 줄인다.

```cpp
void AccumulatePriority(EntityRepState& r, float basePriority) {
    r.accumulatedPriority += basePriority;
}

bool ShouldSend(EntityRepState& r, int estimatedBytes, int& budget) {
    if (estimatedBytes > budget) return false;
    if (r.accumulatedPriority < r.sendThreshold) return false;

    budget -= estimatedBytes;
    r.accumulatedPriority = 0.0f;
    return true;
}
```

양의 age 항은 오래 미전송된 entity의 score를 높여 starvation 위험을 줄인다. 그러나 record가 budget보다 크거나 candidate가 계속 교체되는 경우의 hard guarantee는 아니다. 최대 stale tick deadline, oversize counter와 missed-deadline metric을 별도로 둔다.

갱신 빈도의 차등은 수신 측 보간과 맞물려야 한다 — 10Hz로 갱신되는 먼 엔티티는 보간 buffer도 그 간격에 맞춰 깊어야 하고(아니면 상시 외삽), 갱신 간격이 바뀌는 순간(가까워져서 10Hz→30Hz) 보간 시계가 매끄럽게 따라와야 한다. 송신 측 LOD 결정이 수신 측에 암묵적으로 전파되는 셈이라, 엔티티별 "현재 갱신 등급"을 복제 상태에 포함시키면 수신 측이 명시적으로 대응할 수 있다.

## 15. Hysteresis와 Thrashing 방지

엔티티가 AOI 경계 근처에 있으면 포함과 제외가 매 tick 반복될 수 있다. 이를 thrashing이라고 한다. 해결책은 hysteresis다.

```text
enter radius = 90m
exit radius = 110m
```

90m 안으로 들어오면 AOI에 추가하고, 110m 밖으로 나가야 제거한다. 경계 사이에서는 기존 상태를 유지한다. 이렇게 하면 작은 위치 흔들림으로 subscribe/unsubscribe가 반복되지 않는다.

AOI 변경 이벤트도 비용이 있다. 새 entity는 initial state가 필요하고 제거에는 despawn 또는 dormancy 의미가 필요하다. spatial hysteresis에 time hysteresis를 더할 수 있지만 시간은 고정 1~2초가 아니라 경계 속도, 정보 노출 budget과 재진입 비용으로 정한다. 너무 길면 client가 이미 멀어진 entity의 정보를 오래 보유하고 server memory도 늘어난다. dormant entry도 무한정 보관하지 않고 수명·개수 상한과 evict 기준을 정하며, evict된 entity는 재진입 시 full state로 다시 보낸다.

[13절의 dormancy](#13-aoi-전이의-수명-주기)와 합치면 경계 배회의 비용 구조가 완성된다 — hysteresis가 전이 *횟수* 를 줄이고, dormancy가 전이 *단가* 를 줄인다.

## 16. 구조의 확장: Replication Graph

naive relevancy가 client 수 C와 entity 수 E에 대해 $O(CE)$ 후보 검사를 수행하므로 어느 규모에서든 tick budget을 넘는지 측정해야 한다. 임계가 “수백 client”로 고정되는 것은 아니며 E, score cost와 hardware에 따라 달라진다. spatial·team·ownership처럼 결과가 같은 부분을 node 단위로 공유하면 client별 후보 집합을 재사용할 수 있다.

UE Replication Graph가 이 발상의 한 구현이다. entity를 spatial, always-relevant, team, owner-only 같은 node에 담고 connection이 참조할 node 집합을 구성한다. node의 candidate list는 여러 connection이 재사용할 수 있지만 최종 per-connection priority·privacy filter와 serialization은 여전히 남는다. shared candidate generation과 client-specific decision의 경계를 계측한다.

처음부터 graph framework가 필요한 것은 아니다. 단순 score path의 p99가 budget 안이면 유지하되, 기준을 spatial·global·relationship·owner-only로 분리해 candidate generation을 나중에 공유할 수 있게 한다.

## 17. 보안과 정보 은닉

AOI는 월핵 방어의 첫 번째 선이다. 서버가 적 위치를 보내면 클라이언트는 그 정보를 숨기기만 할 뿐이다. 클라이언트 메모리를 읽는 공격자는 숨겨진 정보를 볼 수 있다. 서버가 보내지 않으면 볼 수 없다.

단, 정보 은닉은 게임플레이와 충돌한다. 벽 뒤 적을 완전히 보내지 않으면 발소리, 그림자, 예측 이동, 문 열림 같은 간접 정보가 늦게 도착할 수 있다. FPS에서는 시야 기반 relevancy와 사운드 relevancy를 분리해야 한다.

적이 보이는 순간에야 relevant가 되면 initial state 전송과 playout buffer 때문에 화면 등장이 늦을 수 있다. 반대로 너무 일찍 보내면 wallhack이 활용할 미래 정보를 늘린다. visibility set을 이동 속도·network/presentation delay의 상한만큼 보수적으로 확장하거나 low-precision prefetch state와 visible 전환 state를 분리할 수 있다. 확장 horizon은 고정 수백 ms가 아니라 reveal-latency SLO와 정보 노출 공격 실험으로 정한다.

좋은 정책은 정보 등급을 나눈다.

- 정확한 위치: 보이는 적 또는 매우 가까운 적.
- 흐린 위치: 미니맵, 사운드 단서, 팀 정보 — 양자화를 일부러 거칠게 한 위치는 그 자체가 은닉이다.
- 비공개: 클라이언트가 알 필요 없는 적.



## 18. 예산을 숫자로 잡기

먼저 client당 application send budget $R$ bytes/s, snapshot interval rate $f$, path datagram budget $D$를 분리한다. 한 snapshot interval에 쓸 수 있는 평균 byte는 $R/f$지만 한 datagram payload는 $D$를 넘을 수 없다.

```text
target downstream per client = 30 KB/s
snapshot rate = 20 Hz
average interval budget = 30 * 1024 / 20 = 1536 bytes
measured safe application payload per datagram = D
```

이 가정에서 1536B는 interval 평균이지 단일 packet 크기 허용값이 아니다. $D<1536$이면 낮은 priority record를 미루거나 둘 이상의 datagram으로 pace해야 한다. $D$는 PLPMTUD와 IP version·tunnel·UDP·protocol header·AEAD tag를 반영해 connection별로 정한다. “snapshot당 1.1KB” 같은 상수로 rate budget과 path MTU를 한 숫자에 섞지 않는다.

예산은 congestion controller가 관측한 현재 delivery capacity, target population의 telemetry, gameplay quality SLO와 server egress cost에서 나온다. 최저 회선 하나에 전체 품질을 고정하기보다 quality tier와 graceful degradation을 정의한다. provider price는 region·commitment·traffic class에 따라 달라지므로 `bytes/client/s × concurrent clients × play seconds × replication/relay factor`에 실제 단가를 적용한다.

서버 전체 송신량도 계산해야 한다.

```text
clients = 100
downstream per client = 30 KB/s
server egress = 3 MB/s
```

이는 payload 평균값이다. 실제 egress에는 protocol overhead와 packet rate가 더해지고, 전투 개시처럼 여러 client의 snapshot이 동시에 커지는 correlated burst가 pacer queue를 채울 수 있다. 초당 token bucket, per-interval budget, queue delay와 P99 packet size를 함께 제한한다.

## 19. Packet Rate와 Packet Size

Packet rate를 높이면 지연은 줄지만 헤더 오버헤드와 PPS가 늘어난다. Packet size를 키우면 효율은 좋아지지만 MTU 초과와 손실 피해가 커진다.

| 선택 | 장점 | 단점 |
|---|---|---|
| 작은 패킷 고빈도 | 낮은 지연 | 헤더/PPS 증가 |
| 큰 패킷 저빈도 | 효율적 | 지연과 손실 피해 증가 |
| MTU 근처 패킷 | 대역폭 효율 | fragmentation 위험 |

IPv4+UDP는 option이 없을 때 28B, IPv6+UDP는 extension header가 없을 때 48B다. 여기에 connection/sequence header, AEAD tag, tunnel과 link overhead를 더한다. 총 fixed overhead를 H, payload를 P라 하면 wire 전송량 중 overhead 비율은 $H/(H+P)$다. 예를 들어 H=50B라는 가정에서는 P=100B일 때 33%, P=1000B일 때 약 4.8%다. PPS는 별도 비용 축이며 `connections × packets/s`로 NIC queue, batching과 syscall path를 부하 시험한다.

규칙적인 timestamp cadence는 playout 예측을 단순하게 하지만 network send 시각은 pacer와 congestion budget에 따라 달라질 수 있다. payload가 interval budget을 넘으면 낮은 priority update를 미루되 max stale age를 지킨다. rate와 size 중 하나만 변동시킨다는 보편 규칙보다 receiver가 server tick으로 재생 시각을 복원하고 sender가 queue delay를 제한하는 것이 핵심이다.

## 20. Bit Packing

필드는 바이트 단위로 딱 맞지 않는다. 체력 0~100은 7비트면 충분하고, 팀 번호 0~3은 2비트면 충분하다. Bit packing은 이런 필드를 비트 단위로 채운다.

```cpp
struct BitWriter {
    void WriteBits(uint32_t value, int bits);
    void WriteBool(bool value) { WriteBits(value ? 1u : 0u, 1); }
};

void WriteState(BitWriter& w, const PlayerNetState& s) {
    w.WriteBits(s.entityId, 20);
    w.WriteBits(s.health, 7);
    w.WriteBits(s.team, 2);
    w.WriteBool(s.isCrouching);
    w.WriteBool(s.isReloading);
}
```

구현 디테일 둘. 첫째, *읽기와 쓰기의 대칭을 한 함수로* — `Serialize(Stream&, State&)` 하나를 읽기/쓰기 스트림 양쪽에 템플릿으로 돌리면, 인코더와 디코더가 어긋나는 부류의 버그(필드 순서·비트 수 불일치)가 구조적으로 사라진다. 둘째, *경계 검사* — 수신 비트스트림은 적대적 입력이다. 읽기 초과·범위 밖 값에서 안전하게 실패해야 하고(인덱스로 배열 접근 전 검증), 디버그 빌드에서는 필드 사이에 매직 비트를 끼워 어긋남을 즉시 잡는 기법도 유용하다.

Bit packing은 CPU 비용이 있다. 하지만 snapshot replication에서는 대역폭이 병목인 경우가 많다. CPU 비용이 문제가 되면 hot path를 측정하고, 자주 쓰는 encoding을 테이블화하거나 SIMD/배치 처리를 고려한다.

엔트로피 코딩(허프만·범위 부호화)까지 가는 경우도 있다 — 필드 값의 분포가 치우쳐 있으면(대부분의 delta가 0 근처) 고정 비트보다 더 줄일 수 있다. 다만 구현·디버깅 비용이 한 단계 뛰므로, 고정 비트 + delta + 우선순위로 예산이 안 맞을 때의 마지막 카드로 남겨 둔다.

## 21. 사례 연구: 엔티티 한 개의 다이어트

기법들이 누적되는 모습을 한 엔티티로 따라가 보자. 캐릭터 하나의 상태 — 위치, 회전, 속도, 체력, 팀, 자세 플래그 2개.

| 단계 | 인코딩 | payload 크기 |
|---|---|---:|
| 논리 필드 합 | float3 + float4 + float3 + int32×2 + bool×2 | 50B, C++ struct는 padding으로 더 클 수 있음 |
| packed full record | id 20b + 위치 57b + 회전 32b + 속도 36b + 체력 7b + 팀 2b + flag 2b | 156b → 20B |
| delta, field 변화 없음 | id 20b + change mask 6b | 26b → 4B |
| delta, 위치만 변화 | id 20b + mask 6b + 위치 57b | 83b → 11B |

byte 수는 `ceil(bits/8)`로 올림한다. 26bit를 3B, 83bit를 10B로 내리면 buffer를 작게 할당하는 오류가 된다. 예시에서 위치 delta는 byte-aligned 논리 필드 50B보다 약 4.5배 작지만 record count, create/remove, packet header와 entropy는 별도다. AOI와 update-rate 절감도 독립이라고 가정해 배율을 단순 곱하지 않고 같은 match trace에서 end-to-end bytes를 측정한다.

## 22. 예산 내 트랜잭션 조립

priority scheduler가 선택 순서를 만들었어도 wire writer가 budget 경계에서 record를 원자적으로 commit하지 못하면 packet이 손상된다. 이 절의 책임은 후보 선정이 아니라 **크기 측정, 쓰기 실패 rollback, 실제 byte 차감**이다.

```cpp
void BuildSnapshot(Client& c, int byteBudget) {
    auto candidates = GatherRelevantEntities(c);
    SortByPriority(candidates);

    for (EntityId id : candidates) {
        int estimate = EstimateDeltaBytes(c, id);
        if (estimate > byteBudget) {
            DeferEntity(c, id);
            continue;
        }

        auto mark = c.writer.Mark();
        if (!TryWriteEntityDelta(c, id, byteBudget)) {
            c.writer.Rollback(mark);
            DeferEntity(c, id);
            continue;
        }
        byteBudget -= c.writer.BytesWrittenSince(mark);
    }
}
```

`EstimateDeltaBytes`가 과소하면 transactional rollback이 필요하고 과대하면 budget을 남긴다. 이전 크기 cache는 후보 estimate일 뿐 create·field mask 변화에서 틀릴 수 있으므로 estimate error histogram을 남기고 exact bit-count pass 또는 scratch writer와 비교한다. `byteBudget`은 고정 상수가 아니라 congestion/pacing policy가 이번 interval에 허락한 양이다.

## 23. 상행 트래픽과 비용

action game의 상행은 input command와 ack가 주를 이루는 경우가 많지만 voice, user content와 telemetry를 같은 경로에 섞으면 별도 budget이 필요하다. input channel도 command 크기·rate·redundancy에서 직접 계산한다.

client가 simulation tick마다 packet 하나를 보내면 receive PPS는 `clients × command packet rate`다. 여러 **새** tick을 기다려 한 packet에 묶으면 PPS 대신 oldest command latency가 늘고, 최신 command를 즉시 보내면서 과거 command를 중복 포함하면 PPS는 줄지 않지만 loss recovery가 좋아진다. 두 기법을 구분한다. 상행 capacity와 queueing은 access network·radio·동시 upload에 따라 측정하고 command packet은 pacer에서 voice·asset upload보다 높은 latency priority를 줄 수 있다.

provider는 흔히 Internet egress에 요금을 부과하지만 region 간 전송, relay와 commitment 조건도 비용에 들어간다. smallest-three가 rotation field를 16B에서 4B로 줄여도 전체 traffic 절감률은 rotation 전송 빈도와 packet padding에 달려 있다. field-level byte telemetry를 실제 egress 단가에 연결해 최적화 우선순위를 정한다.

## 24. 계측 심화: 필드 귀속과 합성 부하

snapshot health 지표에 더해 전체 network budget을 channel·entity type·field에 귀속해야 최적화 대상을 고를 수 있다.

| 지표 | 의미 |
|---|---|
| bytes/sec | 송수신 평균 대역폭 |
| packets/sec | PPS와 헤더 오버헤드 |
| snapshot bytes | snapshot별 payload |
| entities sent/skipped | 예산으로 전송/연기된 엔티티 |
| reliable queue size | reliable backlog |
| packet loss | 손실률 |
| jitter | 도착 간격 변동 |
| encode time | snapshot 생성 CPU |

평균만 보면 위험하다. P95/P99 snapshot size와 reliable queue size가 중요하다. 특정 전투 상황에서만 대역폭이 폭증하면 평균은 괜찮아 보여도 실제 플레이는 끊긴다.

한 단계 더 — *필드별 대역폭 분해* 를 갖추면 튜닝이 추측에서 측정으로 바뀐다. "전체 트래픽의 40%가 회전, 그중 절반이 정지 중인 NPC의 미세 회전 잡음"이라는 분해가 나오면 처방(회전 dead zone, NPC 갱신 빈도 인하)이 자동으로 따라온다. 직렬화 코드가 필드별 바이트를 태깅하며 쓰도록 만들어 두면(디버그 빌드 한정이라도), 이 분해는 공짜다.

합성 부하 테스트도 계측의 일부다 — 봇 N명을 최악 시나리오(전원 한곳 밀집, 전 스킬 난사)로 돌리며 P99 스냅샷 크기와 egress를 재는 것. 이 수치가 출시 전 용량 계획의 근거이고, 콘텐츠 패치(새 스킬·새 엔티티)마다 회귀로 돌릴 기준선이다.


---

## 결론

상태 복제는 직렬화 문제가 아니라 제한된 대역폭으로 플레이어에게 중요한 세계를 재구성하는 문제다. 관련성, 우선순위, 정밀도, 갱신 주기를 함께 조절하고 누락과 지연 아래의 품질 저하를 측정해야 한다.

## 더 읽을거리

- Glenn Fiedler, [Snapshot Interpolation](https://gafferongames.com/post/snapshot_interpolation/), [Snapshot Compression](https://gafferongames.com/post/snapshot_compression/), [Reading and Writing Packets](https://gafferongames.com/post/reading_and_writing_packets/), [Serialization Strategies](https://gafferongames.com/post/serialization_strategies/) — 보간, delta·quantization, bit packing 구현.
- Fabien Sanglard, [Quake 3 Network Model](https://fabiensanglard.net/quake3/network.php) — baseline + delta snapshot 구조.
- Mark Frohnmayer, Tim Gift, ["The TRIBES Engine Networking Model"](https://www.gamedevs.org/uploads/tribes-networking-model.pdf) — ghost, relevancy와 우선순위 기반 복제.
- Valve Developer Wiki, [Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) — delta snapshot 운영 문서.
- David Aldridge, *I Shot You First: Networking the Gameplay of Halo: Reach* (GDC 2011) — 우선순위·budget 운영 사례.
- Jean-Sébastien Boulanger, Jörg Kienzle, Clark Verbrugge, *Comparing Interest Management Algorithms for Massively Multiplayer Games* (NetGames 2006) — 거리·셀·시야 기반 AOI 비교.
- Epic Games, [Unreal Engine: Replication Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/replication-graph-in-unreal-engine) — 대규모 relevancy 시스템 구조.
- Riot Games, ["Demolishing Wallhacks with VALORANT's Fog of War"](https://technology.riotgames.com/news/demolishing-wallhacks-valorants-fog-war) — 시야 기반 정보 은닉과 등장 지연의 절충.
- M. Shreedhar, George Varghese, *Efficient Fair Queueing Using Deficit Round Robin* (IEEE/ACM Transactions on Networking, 1996) — byte service 공정성의 원전. update 빈도 보장은 별도 deadline·target interval 정책이 필요하다.
