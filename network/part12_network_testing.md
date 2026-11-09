## 들어가며

로컬 네트워크에서 잘 동작하는 넷코드는 실제 인터넷에서 쉽게 깨진다. 패킷은 손실되고, 순서가 바뀌고, 지연이 흔들리고, 순간적으로 몰려 도착한다. 서버 tick은 가끔 밀리고, 클라이언트 clock은 drift를 가진다. 네트워크 테스트는 이런 조건을 의도적으로 만들고 재현하는 과정이다.

좋은 테스트는 "핑 50ms에서 플레이해 봤다"가 아니다. latency, jitter, loss, reorder, duplication, bandwidth limit, burst loss를 조합하고, 그 상황에서 prediction, reconciliation, interpolation, reliable queue, snapshot budget이 어떻게 반응하는지 측정해야 한다.

Deterministic repro는 특히 중요하다. 네트워크 버그는 재현이 어렵다. 입력 로그, 패킷 로그, random seed, server tick history를 남기면 같은 상황을 다시 실행해 원인을 추적할 수 있다.

이 글은 주입해야 할 조건의 목록에서 시작해, 도구의 계층(내장 시뮬레이터 ↔ OS 도구), 로그와 결정론 기반 재현, 봇 부하와 soak, 그리고 운영 계측까지 — 넷코드 품질을 *증명* 하는 인프라 전체를 다룬다.


---

## 목차
1. [테스트해야 하는 네트워크 조건](#1-테스트해야-하는-네트워크-조건)
2. [Latency, Jitter, Loss 주입](#2-latency-jitter-loss-주입)
3. [OS 레벨 도구의 사용법](#3-os-레벨-도구의-사용법)
4. [패킷 로그와 재현](#4-패킷-로그와-재현)
5. [Deterministic Simulation Check](#5-deterministic-simulation-check)
6. [봇과 부하 테스트](#6-봇과-부하-테스트)
7. [Soak Test와 Tail Latency](#7-soak-test와-tail-latency)
8. [운영 계측](#8-운영-계측)


---

## 1. 테스트해야 하는 네트워크 조건

실전 네트워크는 단일 숫자로 표현되지 않는다. 다음 값은 제품 권장값이 아니라 각 장애 축을 설명하기 위한 예다.

| 조건 | 예 |
|---|---|
| latency | 20ms, 80ms, 150ms |
| jitter | 패킷마다 ±20ms 변동 |
| loss | 1%, 3%, 10% |
| burst loss | 200ms 동안 연속 손실 |
| reorder | 늦게 보낸 패킷이 먼저 도착 |
| duplication | 같은 패킷이 두 번 도착 |
| bandwidth limit | 30KB/s 제한 |
| clock drift | 클라이언트 시계 오차 |

각 조건은 다른 버그를 드러낸다. Loss는 reliable queue와 snapshot 복구를 검증한다. Jitter는 interpolation buffer를 검증한다. Reorder는 sequence 처리와 stale packet discard를 검증한다. Bandwidth limit은 priority와 budget 정책을 검증한다.

조건들은 *독립이 아니라 상관* 으로 온다는 점이 중요하다. bottleneck queue가 차면 지연 상승, burst loss, 뒤이은 몰림 도착이 하나의 사건으로 나타날 수 있다. 균등 무작위 손실만 테스트하면 보간 버퍼 고갈과 baseline 연쇄 실패를 놓친다. 운영 packet trace를 지연 분포·loss-run·reorder·outage 구간으로 요약해 대표 profile을 만들고 자동 테스트를 profile matrix로 돌린다. “Wi-Fi 30ms”, “모바일 2초 blackout” 같은 이름과 수치는 측정 없이 현실 profile로 간주하지 않는다.

개발 빌드에는 clean·typical·adverse를 즉시 바꾸는 preset과 현재 조건을 눈에 띄게 표시하는 UI를 둔다. adverse profile을 일상적으로 실행하면 오류를 빨리 드러낼 수 있지만, 이를 유일한 기본값으로 두면 local baseline regression과 threshold 경계만의 버그를 놓칠 수 있다. CI는 clean baseline과 여러 adverse profile을 모두 통과해야 한다.

## 2. Latency, Jitter, Loss 주입

테스트 환경에는 network simulator가 필요하다. OS 도구를 사용할 수도 있고, 게임 내부에 packet shim을 넣을 수도 있다.

```cpp
struct NetCondition {
    double baseLatencyMs = 80.0;
    double jitterMs = 20.0;
    double lossPercent = 2.0;
    double duplicatePercent = 0.1;
    double reorderPercent = 0.5;
};
```

내부 simulator는 플랫폼과 무관하게 테스트를 자동화하기 쉽다. 송신 직전 패킷을 queue에 넣고, 도착 시간을 조작하고, 일부를 버린다.

```cpp
void SimulatedLink::Send(Packet p) {
    if (RandomPercent() < condition_.lossPercent) return;

    double jitter = RandomRange(-condition_.jitterMs, condition_.jitterMs);
    p.deliveryTime = now_ + condition_.baseLatencyMs + jitter;
    p.deliveryTime = std::max(p.deliveryTime, now_);

    inFlight_.push_back(std::move(p));
}
```

이 단순 모델의 한계도 알아 두자 — 균등 jitter와 독립 손실은 상관된 장애 패턴을 만들지 못한다. 개선 두 가지면 충분히 현실적이 된다: 손실을 2상태 마르코프 체인(Gilbert-Elliott 모델 — "좋음" 상태와 "나쁨" 상태를 오가며, 나쁨에서 손실률이 높음)으로 바꾸면 burst가 생기고, 대역폭 제한을 *토큰 버킷 + 유한 큐* 로 구현하면 "큐잉 지연 상승 → 큐 초과 손실"의 인과가 재현된다. 혼잡 제어 테스트에는 이 모델이 필수다.

이 burst 모델의 수치 감각을 잡아 두면 균등 모델과의 차이를 정량화할 수 있다. 좋음·나쁨 두 상태의 전이 확률을 좋음→나쁨 $p$, 나쁨→좋음 $r$이라 하고, 좋음에서는 손실이 없고 나쁨에서는 패킷을 버린다고 두자(고전 Gilbert 모델). 정상 분포는 균형식 $\pi_G\,p = \pi_B\,r$과 $\pi_G + \pi_B = 1$에서 $\pi_B = \frac{p}{p+r}$이므로, *평균* 손실률은 $\frac{p}{p+r}$이다. 한편 나쁨 상태를 매 스텝 확률 $r$로 벗어나므로 연속 손실의 길이는 모수 $r$의 기하분포를 따라 평균 $\frac{1}{r}$이다. 예를 들어 $p=0.001$, $r=0.1$이면 평균 손실률은 $\frac{0.001}{0.101}\approx 0.99\%$로 균등 1% 손실과 구별되지 않지만, 손실이 올 때는 평균 $\frac{1}{0.1}=10$개가 연달아 사라진다. *평균이 같아도* 보간 버퍼 고갈과 reliable 재전송 폭주는 이 10연속 구간에서만 터진다 — 평균 손실률이 아니라 burst 길이를 별도 축으로 둬야 하는 정량적 이유다.

내장 시뮬레이터의 장점은 결정론에 필요한 입력을 직접 통제하기 쉽다는 점이다. 난수 시드뿐 아니라 가상 시계, 패킷 삽입 순서, 동일 시각 이벤트의 타이브레이크까지 고정해야 같은 손실 패턴과 전달 순서를 재현할 수 있다. 그러면 CI에서 "시드 12345, 프로파일 모바일, 봇 4대, 10분"을 회귀 테스트로 돌릴 수 있다. `netem`도 버전과 옵션에 따라 난수 시드를 줄 수 있지만, 실제 소켓 버퍼·스케줄러·NIC·다른 트래픽의 타이밍까지 완전히 고정하지는 못한다.

## 3. OS 레벨 도구의 사용법

실제 OS 레벨 도구도 필요하다. 내부 simulator는 게임 패킷 경로만 테스트하지만, OS buffer, NIC, routing, firewall 조건은 반영하지 못한다.

```bash
# 실험 1: 순수 netem — 독립 실행(별도 interface 또는 정리 후 시작)
tc qdisc del dev eth0 root 2>/dev/null || true
# Linux tc-netem: eth0 송신에 지연 80ms(지터 20ms, 상관 25%), 손실 1%, 재정렬 0.5% 주입
tc qdisc add dev eth0 root netem delay 80ms 20ms 25% loss 1% reorder 0.5%
tc qdisc del dev eth0 root

# 실험 2: 대역폭 병목 재현 — 실험 1과 별개로 실행한다.
# TBF 아래에 netem을 붙이면 inner qdisc가 교체되어 TBF의 limit/latency가
# 더 이상 최종 queue 한도가 아니므로 netem 자체 limit을 명시하고
# 실제 queueing 시간을 측정한다.
tc qdisc del dev eth0 root 2>/dev/null || true
tc qdisc add dev eth0 root handle 1: tbf rate 1mbit burst 32768 latency 400ms
tc qdisc add dev eth0 parent 1:1 handle 10: netem limit 1000 delay 50ms 10ms loss 0.5%

# 정리
tc qdisc del dev eth0 root
```

TBF의 `burst`는 바이트 크기이므로 위의 `32768`은 32KiB 버킷이다. `32kbit`라고 쓰면 32킬로비트, 즉 약 4KiB로 해석되어 의도와 달라질 수 있다. `parent 1:1`은 TBF가 자식 qdisc를 붙이도록 제공하는 내부 클래스에 netem을 연결한다. 다만 TBF 아래에 netem을 붙이면 inner qdisc가 교체되어 TBF의 `limit`/`latency`가 최종 큐 한도가 아니므로, netem의 `limit`을 별도로 정하고 실제 큐잉 시간을 측정한다. 버킷·큐 크기와 `latency`는 테스트하려는 링크의 대역폭-지연 곱과 허용 큐잉 시간을 기준으로 정하며, 예시 숫자를 제품 프로파일로 그대로 사용하지 않는다.

Windows에서는 [clumsy](https://jagt.github.io/clumsy/)(WinDivert 기반 GUI)가 같은 역할을 한다 — lag/drop/throttle/duplicate/out-of-order를 실행 중에 토글할 수 있어 수동 탐색에 좋다. 콘솔 플랫폼은 각 플랫폼 SDK의 네트워크 에뮬레이션 기능을 쓴다.

배치에 대한 조언 — netem은 *지정한 방향(송신)* 에만 걸린다는 점을 잊기 쉽다. 왕복 조건을 만들려면 양 끝 또는 중간 라우터(VM 한 대를 브리지로) 양방향에 걸어야 한다. 그리고 OS 도구는 머신 전체에 걸리므로 SSH 세션까지 느려진다 — 전용 테스트 머신이나 네임스페이스(`ip netns`) 격리가 편하다.

내장 시뮬레이터와 OS 도구의 역할 분담: *통제된 자동화·재현은 내장으로, 커널과 실제 I/O 경로를 포함한 검증은 OS로*. 둘 다 통과해야 "테스트했다"고 말할 수 있다.

## 4. 패킷 로그와 재현

네트워크 버그를 잡으려면 "어떤 패킷이 언제 왔는지"가 남아야 한다.

```cpp
struct PacketLogEntry {
    uint64_t localFrame;
    uint64_t monotonicNs;
    uint32_t connectionId;
    uint32_t sequence;
    uint32_t ack;
    uint32_t ackBits;
    uint16_t bytes;
    uint8_t direction;
    uint8_t channel;
};
```

`monotonicNs`는 wall clock이 아니라 process 시작점 기준 단조 시각이어야 clock correction에 흔들리지 않는다. 서로 다른 host의 로그를 합칠 때는 clock offset·uncertainty를 별도로 기록한다. payload 전체는 기본적으로 저장하지 않는다. message type, 길이, canonical hash와 decode 결과로 충분한 경우가 많고, full capture는 명시적 opt-in·접근 통제·짧은 보존 기간 아래에서만 사용한다.

재현에는 입력 로그도 필요하다. 클라이언트가 어떤 input command를 만들었고, 서버가 어느 tick에 처리했는지 알 수 있어야 reconciliation 오류를 찾을 수 있다.

재현의 완성형은 *재생 가능한 캡처* 다 — 도착 시각뿐 아니라 해당 경로에서 실제로 소비한 메시지 내용 또는 이를 재구성할 충분한 데이터, 입력 로그, 초기 상태, 시드와 외부 사건이 필요하다. 앞의 메타데이터와 해시만으로는 원래 payload를 복원할 수 없다. 수신 루프가 "소켓에서 읽기"와 "로그에서 읽기"를 같은 인터페이스로 추상화되어 있으면 재생 모드는 거의 공짜로 얻어진다. 한 번 갖춰지면 버그 신고의 단위가 "말로 된 설명"에서 "캡처 파일"로 바뀌고, 그 차이가 네트워크 버그 수정 속도를 가른다.

상시 release capture는 크기가 제한된 *ring buffer*로 운영할 수 있다. 보존 시간은 packet rate와 privacy budget으로 산출하고, desync·큰 보정·연결 끊김 같은 trigger가 발생하면 전후 구간을 동결한다. 자동 upload에는 사용자 동의, redaction, sampling과 rate limit이 필요하다.

## 5. Deterministic Simulation Check

결정론이 필요한 구조에서는 같은 입력과 같은 초기 상태가 같은 결과를 만들어야 한다. 서버 권위 구조에서도 replay와 bug repro에는 결정성이 중요하다.

```cpp
uint64_t HashWorldState(const World& world) {
    Hash64 h;
    for (const Entity& e : world.EntitiesSortedById()) {
        h.Add(e.id);
        h.Add(CanonicalBits(e.position));
        h.Add(CanonicalBits(e.velocity));
        h.Add(e.health);
    }
    return h.Value();
}
```

Tick마다 state hash를 남기면 어느 tick부터 결과가 갈라졌는지 찾을 수 있다. hash 대상은 안정적인 순서와 명시된 byte order로 직렬화해야 한다. 같은 tick 번호라도 한쪽이 예측 입력을, 다른 쪽이 확정 입력을 사용했다면 비교가 성립하지 않으므로 양쪽이 확인한 confirmed input frontier 위에서 비교하고, object memory dump와 canonical serialization을 혼동하지 않도록 padding·container 순서·RNG 상태까지 schema로 고정한다. `unordered_map` 순회를 그대로 사용하면 구현·rehash 상태에 따라 순서가 달라진다. 엄격한 결정론 검사에서 `Quantize`를 사용하면 작은 divergence를 숨기므로 canonical bit representation이나 simulation이 정의한 exact fixed-point 값을 hash한다. 허용 오차 기반 비교가 필요하면 exact hash와 별도의 diagnostic metric으로 둔다.

갈라진 tick을 찾은 다음은 *무엇이* 갈라졌는지다 — 전체 해시 하나로는 모자라므로, 시스템별 부분 해시(이동/전투/물리/AI 각각)를 계층으로 남기면 어느 tick의 어느 시스템이 갈라졌는지로 범위를 좁힌다. 단 tick 해시의 일치 여부는 단조 술어가 아니라 `같음→다름→같음`이 가능하므로, 최초 불일치는 이분 탐색이 아니라 처음부터의 선형 비교(또는 prefix digest)로 찾는다. 더 정밀하게는 갈라진 tick의 두 실행에서 엔티티별 상태를 직접 diff하는 도구까지 — 결정론 디버깅은 도구 투자에 정직하게 비례한다.

Floating point, random, 멀티스레드 작업 순서, 시간 API 사용은 재현성을 깨는 흔한 원인이다. 네트워크 테스트는 이 원인을 빨리 드러내야 한다. 흔한 범인 목록: 시뮬레이션 안의 `Now` 호출(시간은 틱에서 유도해야 한다), 전역 난수기를 시뮬레이션 밖(이펙트·UI)과 공유(스트림 분리 필요), 병렬 for의 합산 순서(부동소수점 덧셈은 비결합), 그리고 플랫폼별 수학 라이브러리 차이. CI에 "같은 입력 2회 실행 해시 일치" 테스트를 두면 결정론 회귀가 커밋 단위로 잡힌다.

비결합성이 추상적으로 들리면 IEEE 754 배정밀도의 구체적 반례 하나로 충분하다. $(10^{20} + (-10^{20})) + 1 = 1$이지만 $10^{20} + ((-10^{20}) + 1) = 0$이다. $10^{20}$ 규모에서 ULP는 약 $2^{14}$이라, 가운데 합 $-10^{20}+1$을 계산할 때 $1$이 반올림으로 통째로 흡수되어 $-10^{20}$이 되고 거기에 $10^{20}$을 더하면 $0$이 된다. 반대로 같은 크기 두 수가 먼저 상쇄되면 정확히 $0$이 남고 거기에 $1$을 더해 $1$이 된다. 병렬 reduction이 부분합의 결합 순서를 실행마다 바꾸면 정확히 이 종류의 차이가 누적되어 state hash가 갈라진다.

결정론이 필요한 합산은 순서를 고정하고, 컴파일러의 contraction·fast-math·중간 정밀도 같은 실행 계약도 통제한다. 정수·고정소수점은 플랫폼별 부동소수점 차이를 줄이지만 자동으로 결정론을 보장하지 않는다. C++의 signed overflow는 정의되지 않은 동작이고, 포화 연산·클램프·고정소수점 곱셈의 반올림은 연산 순서에 따라 결과가 달라질 수 있다. 비트 폭, overflow 정책, 나눗셈과 반올림 규칙, 작업 순서를 프로토콜의 일부처럼 명시해야 한다.

## 6. 봇과 부하 테스트

수동 플레이는 재현도 확장도 안 된다. 네트워크 테스트의 주력은 *봇 클라이언트* 다 — 실제 클라이언트와 같은 프로토콜 스택을 쓰되 렌더링 없이 도는 헤드리스 빌드.

봇의 행동 수준은 목적에 따라 다르다.

| 수준 | 행동 | 용도 |
|---|---|---|
| 리플레이 봇 | 기록된 입력 재생 | 회귀 — 같은 시나리오 반복 |
| 랜덤 워커 | 무작위 이동·점프·사격 | soak — 넓은 상태 공간 배회 |
| 시나리오 봇 | 최악 패턴 강제(전원 밀집, 스킬 난사) | 용량 — P99 측정 |
| 적대 봇 | 프로토콜 위반·경계값 전송 | 보안 — 서버 검증 강화 |

마지막 줄이 흔히 빠진다 — 잘못된 sequence, 범위 밖 입력, 거대 패킷, 중복 폭주를 *일부러* 보내는 fuzz 봇은 서버 검증 코드의 단위 테스트다. 수신 비트스트림은 적대적 입력이라는 원칙은 이 봇이 상시 두들겨야 지켜진다.

부하 테스트의 산수 — 서버 한 대의 목표 용량(매치 수 × 인원)을 봇으로 채우고, 1절의 프로파일을 섞어(전원 깨끗한 회선은 비현실), 핵심 그래프(틱 P99, 스냅샷 크기 P99, egress)를 콘텐츠 패치마다 기준선과 비교한다. "새 스킬 하나가 스냅샷을 20% 키웠다"는 출시 후가 아니라 머지 전에 알아야 하는 사실이다.

## 7. Soak Test와 Tail Latency

짧은 수동 테스트로는 긴 꼬리 문제를 찾기 어렵다. Soak test는 서버와 봇 클라이언트를 오래 실행해 memory leak, reliable queue 증가, tick spike, reconnect 문제를 찾는다.

측정은 평균보다 tail을 봐야 한다.

| 지표 | 봐야 할 값 |
|---|---|
| server tick time | P50, P95, P99, max |
| snapshot encode time | P95, P99 |
| reliable queue size | 평균, 최대 |
| packet loss | 구간별 |
| correction distance | 평균, P95 |
| interpolation underrun | 횟수 |
| disconnect reason | 분포 |

P99 tick이 튀면 snapshot이 늦고, 클라이언트는 보간 버퍼가 비거나 보정을 크게 받는다. 평균 ping이 낮아도 tail latency가 크면 게임은 끊긴다.

soak의 또 다른 표적은 *단조 증가하는 모든 것*이다. 메모리, reliable queue, 연결 table과 log file을 시간에 따라 본다. 16비트 sequence라면 wraparound 주기는 초당 $f$ packet에서 $2^{16}/f$이고, $f=60$이면 약 18분이다. soak 길이는 임의의 12~48시간이 아니라 sequence wrap, token/key rotation, match recycle, reconnect와 daily job 등 검사할 lifecycle을 여러 번 포함하도록 정한다. 감싸는 sequence의 선후는 RFC 직렬번호 산술을 사용하며, 두 번호의 거리가 공간의 절반일 때는 순서가 정의되지 않는다. 수신 window를 절반보다 훨씬 작게 제한하고 wrap 경계·오래된 duplicate·재접속을 별도 테스트한다.

## 8. 운영 계측

테스트 환경에서만 보이는 지표와 실제 운영에서만 보이는 지표가 다르다. 운영 빌드에도 가벼운 계측을 넣어야 한다.

운영에서 중요한 지표는 다음과 같다.

- 지역별 RTT와 packet loss.
- 서버별 tick time P95/P99.
- 클라이언트별 snapshot bytes/sec.
- reliable resend count.
- disconnect와 timeout 사유.
- reconciliation correction 크기.
- lag compensation rewind 분포.

이 지표는 밸런싱과 운영 정책에도 쓰인다. 예를 들어 특정 지역에서 RTT가 높으면 matchmaking region 구성이 잘못되었을 수 있다. 특정 무기에서 rewind가 과도하게 발생하면 lag compensation 정책을 조정해야 할 수 있다.

운영 계측에는 client 체감 지표도 포함한다. server가 정상이어도 interpolation underrun, 표시 보정, render frame drop과 audio callback miss가 함께 발생할 수 있다. match 종료 요약은 유용하지만 crash·강제 종료 때 사라지므로 bounded periodic aggregate와 event-triggered sample을 병행한다. 경보는 SLO 절대 한도와 release baseline 대비 변화율을 함께 본다. 변화율 임계도 표본 수와 분산을 무시한 고정 퍼센트가 아니라 confidence interval과 사용자 영향으로 판단한다.


---

## 결론

네트워크 테스트는 지연을 한 번 넣어 보는 일이 아니다. Latency, jitter, loss, reorder, bandwidth limit, clock drift를 — 그것도 현실적 상관(burst, 큐잉)을 가진 프로파일로 — 조합해 넷코드의 각 계층을 검증하는 과정이다. 내장 시뮬레이터(통제·자동화)와 OS 도구(실제 I/O 경로)를 분담시키고, 개발 빌드에서 clean·typical·adverse preset을 즉시 전환하며, 봇 4종(리플레이·랜덤·시나리오·적대)을 CI와 용량 테스트에 포함한다.

재현 가능한 패킷 로그, 입력 로그, state hash(시스템별 계층), 링 버퍼 상시 캡처, soak test, 운영 계측이 있어야 네트워크 버그를 추적할 수 있다. 좋은 넷코드는 좋은 프로토콜만으로 완성되지 않는다. 나쁜 네트워크 조건을 반복해서 맞고도 원인을 설명할 수 있어야 한다 — 그리고 그 설명 능력은 전부 사전에 깔아 둔 인프라에서 나온다.

## 더 읽을거리

- [tc-netem(8) 매뉴얼](https://man7.org/linux/man-pages/man8/tc-netem.8.html) — Linux 표준 네트워크 장애 주입(지연·지터·손실·재정렬·대역폭).
- [clumsy](https://jagt.github.io/clumsy/) — Windows 용 실시간 패킷 장애 주입 도구(WinDivert 기반).
- [Wireshark User's Guide](https://www.wireshark.org/docs/wsug_html_chunked/) — 패킷 로그 분석의 표준 도구.
- E. N. Gilbert, "Capacity of a Burst-Noise Channel" (1960) — burst 손실 모델(Gilbert-Elliott)의 원전.
- [RFC 1982 — Serial Number Arithmetic](https://www.rfc-editor.org/rfc/rfc1982) — 감싸는 시퀀스 번호의 대소 비교 규약(직렬 번호 산술)의 표준.
- David Goldberg, [What Every Computer Scientist Should Know About Floating-Point Arithmetic](https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html) (1991) — 반올림과 비결합성의 표준 해설.
- Glenn Fiedler, [Deterministic Lockstep](https://gafferongames.com/post/deterministic_lockstep/) 및 [Floating Point Determinism](https://gafferongames.com/post/floating_point_determinism/) — 결정론 검증의 조건.
