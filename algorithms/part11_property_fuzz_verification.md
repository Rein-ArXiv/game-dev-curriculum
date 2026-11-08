## 들어가며

테스트는 입력과 기대 출력의 표를 늘리는 일이 아니다. 구현이 어떤 입력 공간에서 어떤 성질을 지켜야 하는지 명세하고, 그 성질을 깨는 반례를 효율적으로 찾으며, 발견한 실패를 작고 재현 가능한 형태로 보존하는 작업이다.

게임 코드는 정답표를 만들기 어려운 문제가 많다.

- 경로 탐색 결과는 여러 최단 경로 중 하나일 수 있다.
- 충돌 검출은 부동소수점 허용 오차를 가진다.
- 직렬화기는 수많은 schema와 손상 입력을 받는다.
- rollback 시뮬레이션은 긴 command sequence 뒤에만 어긋날 수 있다.
- concurrent queue는 특정 thread interleaving에서만 깨진다.

이때 예제 테스트만으로는 입력 공간을 덮을 수 없다. Property-based testing은 결과의 일반 성질을 검사하고, model-based testing은 단순한 기준 상태 기계와 실제 구현을 비교하며, fuzzing은 coverage와 sanitizer를 이용해 깊은 실패 경로를 찾는다. Metamorphic testing과 differential testing은 정답을 직접 계산하기 어려운 문제에 oracle을 제공하고, mutation testing은 테스트가 실제 의미 차이를 관찰하는지 검사한다.

이 글은 이 기법들을 별도 도구 목록이 아니라 *명세 → 입력 생성 → 실행 → oracle → 축소 → 회귀 보존* 의 검증 파이프라인으로 설명한다.

---

## 목차

1. [검증 대상과 oracle](#1-검증-대상과-oracle)
2. [예제와 property](#2-예제와-property)
3. [입력 생성과 shrinking](#3-입력-생성과-shrinking)
4. [상태 기계와 model-based testing](#4-상태-기계와-model-based-testing)
5. [Metamorphic·differential testing](#5-metamorphicdifferential-testing)
6. [Coverage-guided fuzzing](#6-coverage-guided-fuzzing)
7. [수치·확률·동시성 검증](#7-수치확률동시성-검증)
8. [Mutation testing과 테스트 강도](#8-mutation-testing과-테스트-강도)
9. [비신뢰 입력과 자원 상한](#9-비신뢰-입력과-자원-상한)
10. [지속 가능한 검증 절차](#10-지속-가능한-검증-절차)

---

## 1. 검증 대상과 oracle

검증을 시작할 때 함수 이름보다 계약을 먼저 쓴다.

```cpp
struct Contract {
    Predicate precondition;
    Relation postcondition;
    Invariant invariant;
    ResourceBound resourceBound;
    FailurePolicy failurePolicy;
};
```

- **사전조건**: 유효 입력의 범위
- **사후조건**: 반환 뒤 반드시 참인 관계
- **불변식**: 연산 sequence 중 계속 유지되는 성질
- **자원 상한**: 시간, 메모리, 출력 크기, 반복 횟수
- **실패 정책**: 잘못된 입력에서 허용되는 상태 변화

### Oracle의 종류

oracle은 실행 결과가 맞는지 판단하는 근거다.

| oracle | 장점 | 대표 한계 |
|---|---|---|
| 고정 기대값 | 명확하고 빠름 | 입력 범위가 작음 |
| 단순 기준 구현 | 독립적이고 이해하기 쉬움 | 큰 입력에 느림 |
| 수학적 property | 많은 입력을 포괄 | property가 약할 수 있음 |
| 상태 기계 model | sequence 검증에 강함 | model도 틀릴 수 있음 |
| 독립 구현 비교 | 복잡한 결과에 유용 | 둘이 같은 오류를 공유할 수 있음 |
| metamorphic relation | 정답 없이 관계 검사 | 변환 관계 설계가 필요 |
| 운영 불변식 | 실제 의미를 반영 | 실패 원인 위치가 멀 수 있음 |

oracle을 구현 코드의 복사본으로 만들면 같은 오류가 양쪽에 들어간다. 예를 들어 최적화된 spatial query의 기준 구현은 같은 BVH를 다른 문법으로 다시 쓰는 대신 모든 객체를 선형 순회해야 한다.

### 부분 oracle

복잡한 결과 전체를 알 수 없어도 일부 성질은 강하게 검사할 수 있다.

최단 경로 $P$에 대해:

1. 시작과 목표가 맞다.
2. 연속 노드가 실제 edge로 연결된다.
3. 금지 노드를 지나지 않는다.
4. 비용이 기준 알고리즘의 비용과 같다.

경로의 정확한 node sequence가 다르더라도 최적 비용과 유효성은 검증할 수 있다.

### 검증과 증명

유한 테스트는 일반적으로 모든 입력의 정확성을 증명하지 않는다. 그러나 구현 불변식과 수학적 증명을 코드의 assertion·property에 연결하면 두 방식이 보완된다.

```cpp
// lower_bound loop invariant:
// 미탐색 구간은 [lo, hi)이고, 정답 삽입 인덱스는 그 끝을 포함한 [lo, hi] 안에 있다.
// [0, lo)의 모든 원소는 target보다 작고, [hi, values.size())의 모든 원소는 target 이상이다.
CHECK(lo <= hi);
CHECK(hi <= values.size());
```

증명은 어떤 성질을 검사해야 하는지 알려 주고, property test는 구현이 그 증명의 전제를 실제로 지키는지 반례를 찾는다. 여기서 CHECK 두 줄은 인덱스 경계 안전성을 검사하며, target 보존 자체는 위 불변식과 갱신 규칙으로 따로 설명한다.

## 2. 예제와 property

예제 테스트는 알려진 중요 사례를 고정한다.

```cpp
std::vector<int> a{3, 1, 2};
std::vector<int> b;
std::vector<int> c{2, 2, 1};
const std::vector<int> expectedA{1, 2, 3};
const std::vector<int> expectedC{1, 2, 2};
Sort(a);
Sort(b);
Sort(c);
CHECK(a == expectedA);
CHECK(b.empty());
CHECK(c == expectedC);
```

property test는 생성된 모든 입력 $x$에 대해 관계를 검사한다.

```cpp
Property("sort preserves order and elements", [](std::vector<int> input) {
    const auto original = input;
    Sort(input);
    CHECK(std::is_sorted(input.begin(), input.end()));
    CHECK(Multiset(input) == Multiset(original));
});
```

“결과가 정렬되어 있다”만 검사하면 모든 원소를 버리고 빈 배열을 반환하는 구현도 통과한다. property는 의미를 충분히 제한하도록 조합해야 한다.

### 대표 property

**멱등성**

$$
f(f(x))=f(x)
$$

정렬, canonicalization, clamp에 유용하다.

**Round-trip**

$$
\operatorname{Decode}(\operatorname{Encode}(x))=x
$$

직렬화, 압축, text parser에 유용하다. encoder와 decoder가 같은 잘못된 field order를 공유해도 통과하므로 golden byte와 독립 decoder를 보완한다.

**역연산**

$$
f^{-1}(f(x))=x
$$

행렬 inverse, coordinate transform, encrypt/decrypt에 유용하다. inverse가 존재하는 domain만 생성해야 한다.

**대칭성과 반대칭성**

$$
\operatorname{Intersects}(A,B)
=\operatorname{Intersects}(B,A)
$$

$$
\operatorname{sign}(\operatorname{Compare}(a,b))
=-\operatorname{sign}(\operatorname{Compare}(b,a))
$$

이 식은 3-way compare 결과가 $-1,0,1$로 정규화됐을 때만 그대로 성립한다. `strcmp`류 API는 부호만 일정하면 되므로 $a<b$에서 $-2$, $a>b$에서 $1$을 돌려주어도 올바른 comparator다. 일반 API에서는 부호의 반대만 검사하고, `INT_MIN`에 단항 부정을 적용하면 signed overflow가 날 수 있다.

sequence number의 wrap-safe 비교처럼 domain 일부에서만 관계가 정의되면 사전조건을 property에 포함한다.

**보존 법칙**

충돌 전후 운동량, 거래 전후 재화 합, container 연산 전후 원소 multiset처럼 domain 의미를 검사한다.

### Property의 강도

property $P_1$이 $P_2$를 함의한다면 $P_2$만 검사할 이유가 약하다. 예를 들어 bit-exact round-trip은 tolerance round-trip보다 강하지만, lossy codec에는 적용할 수 없다. 계약이 허용하는 가장 강한 관계를 고른다.

## 3. 입력 생성과 shrinking

균등 난수는 중요한 경계를 거의 만들지 못한다. 32비트 정수를 균등 생성할 때 0, 1, 최대값, wrap 경계가 나올 확률은 매우 작다. 생성기는 domain 구조를 알아야 한다.

### 분포 설계

```text
일반 입력                         50%
빈 값·최솟값·최댓값              15%
중복·정렬·역정렬                 10%
퇴화·거의 평행·거의 singular     10%
크기 상한 바로 아래·위           10%
과거 실패 corpus                  5%
```

이 비율은 고정 규칙이 아니다. coverage와 실제 장애를 보고 조정한다.

### Constructive generator

사전조건을 만족하는 입력은 직접 구성하는 편이 효율적이다.

invertible matrix를 무작위 16개 scalar로 만든 뒤 determinant가 0이 아닌 것만 남기면 많은 표본을 버리고 ill-conditioned matrix가 섞인다. translation, rotation, bounded nonzero scale을 조합하면 의미 있는 affine transform을 직접 만들 수 있다.

```cpp
Mat4 GenerateInvertibleTransform(Random& rng) {
    Vec3 translation = GenerateFiniteVec3(rng);
    Quat rotation = GenerateUnitQuaternion(rng);
    Vec3 scale = GenerateVec3AwayFromZero(rng, 0.01f, 100.0f);
    return Compose(translation, rotation, scale);
}
```

invalid input 검증은 별도의 generator로 만든다. 유효·무효 분포를 섞어 놓고 어떤 계약을 검사하는지 흐리지 않는다.

### Dependent generator

그래프의 edge endpoint는 node count에 의존한다. packet payload length는 실제 buffer length와 관계가 있다. 독립 난수 field를 나열하지 말고 앞에서 만든 구조를 바탕으로 다음 field를 생성한다.

### Shrinking

실패한 큰 입력을 그대로 주면 원인을 찾기 어렵다. Shrinker는 실패를 유지하면서 입력을 단순화한다.

```text
배열 길이를 절반으로 줄임
원소를 0·1·-1로 이동
그래프 edge와 고립 node 제거
명령 sequence의 연속 구간 삭제
좌표를 원점으로 이동
부동소수점 exponent와 mantissa 단순화
```

Shrinking은 탐욕적으로 한 번 줄이는 것보다 후보 트리를 탐색한다. 어떤 축소가 실패를 없애면 그 branch는 버리고 다른 축소를 시도한다.

### 사전조건과 shrinking

축소 뒤 사전조건이 깨지면 실패가 다른 원인으로 바뀐다. convex polygon shrinker는 vertex를 지우면서 winding과 convexity를 유지해야 하고, transaction sequence shrinker는 begin/commit 구조를 보존해야 한다.

### Seed와 재현

실패 보고에는 다음을 저장한다.

```text
property name
master seed
generator version
shrunk input
build/compiler/platform
floating-point mode
schedule seed
```

generator가 변경되면 같은 seed가 다른 입력을 만들 수 있으므로 shrunk input 자체를 직렬화한다.

## 4. 상태 기계와 model-based testing

container, allocator, network protocol, save system은 단일 함수보다 command sequence에서 깨진다. Model-based testing은 느리지만 명확한 기준 model과 실제 구현에 같은 command를 적용한다.

```cpp
struct QueueModel {
    std::deque<int> values;
};

using Command = std::variant<Push, Pop, Clear>;
```

각 command는 다음을 정의한다.

- 실행 가능한 사전조건
- model에 적용하는 방법
- system under test에 적용하는 방법
- 관찰 결과 비교
- command shrink 방법

```cpp
void Apply(const Pop&, QueueModel& model, RealQueue& queue) {
    const bool modelOk = !model.values.empty();
    const auto actual = queue.Pop();
    CHECK(actual.has_value() == modelOk);
    if (!actual) {
        return;
    }
    if (modelOk) {
        CHECK(*actual == model.values.front());
        model.values.pop_front();
    }
}
```

CHECK가 실패해도 실행을 계속하는 프레임워크라면 실패한 optional을 역참조하면 안 된다. 존재 여부를 확인한 뒤 실패 시 반환하거나, 실패를 즉시 중단하는 fatal assertion을 사용한다.

### 상태 전이 coverage

코드 line coverage보다 model transition coverage가 더 의미 있을 수 있다.

```text
Empty -> NonEmpty
NonEmpty -> Full
Full -> NonFull
NonEmpty -> Empty
Reset during pending operation
Close with buffered data
```

각 transition과 transition pair가 실행되었는지 기록한다.

### Command sequence shrinking

실패 sequence가 만 개 command라면 구간 삭제 delta debugging을 적용한다.

1. sequence 절반을 제거해도 실패하는지 검사
2. 실패하면 제거를 유지
3. 실패하지 않으면 더 작은 구간을 시도
4. 개별 command parameter를 shrink

“10,241번째 pop에서 실패”를 “push, clear, pop 세 명령”으로 줄이면 상태 기계 오류가 드러난다.

### Reference model의 수준

lock-free queue의 model을 또 다른 lock-free queue로 만들지 않는다. 단일 thread `std::deque`와 선형화 가능한 관찰 결과를 비교한다. 성능 특성이 아니라 의미를 model이 소유한다.

### 지속성 model

WAL이나 save system은 crash point를 command로 포함한다.

```text
Begin
Put(k, v)
Append record
Flush
Crash
Recover
```

모든 write boundary에서 crash를 주입하되, 이미 durable 성공 응답을 받은 commit은 반드시 보존되고 미응답·미완료 transaction만 허용된 상태로 남는지 검사한다. 항상 빈 상태로 복구하는 구현도 prefix 조건만 보면 통과하므로, durable acknowledgment와 flush/barrier 지점을 model에 포함하고 acknowledged commit의 원자적 가시성까지 확인한다.

## 5. Metamorphic·differential testing

정답을 직접 계산하기 어렵지만 입력 변환 전후 관계를 알 수 있다면 metamorphic test를 사용한다.

### 기하 변환

두 shape에 같은 rigid transform $T$를 적용하면 교차 여부와 거리는 유지된다.

$$
\operatorname{Intersect}(A,B)
=\operatorname{Intersect}(T(A),T(B))
$$

normal과 witness는 transform에 맞게 이동·회전해야 한다.

### 그래프 비용 변환

모든 edge weight를 양수 $k$배하면 최단 경로 node sequence의 최적 집합은 유지되고 비용은 $k$배다.

$$
d_{kG}(s,t)=k\,d_G(s,t),\qquad k>0
$$

tie가 있으면 exact path sequence는 달라질 수 있어 비용과 유효성을 비교한다.

### 순열 불변성

set 의미의 입력은 순서를 바꿔도 결과가 같아야 한다. 단, floating-point reduction은 순서에 따라 반올림이 달라질 수 있다. 계약이 bit determinism인지 tolerance equivalence인지 명시한다.

### Differential testing

최적화 구현을 독립 기준과 비교한다.

| 대상 | 기준 |
|---|---|
| spatial query | brute force |
| A* | 작은 그래프의 Dijkstra |
| SIMD loop | scalar loop |
| custom allocator | 표준 allocator + ownership model |
| SAT | analytic primitive 또는 GJK |
| serializer | golden format parser |

둘이 다를 때 어느 쪽이 틀렸는지는 추가 oracle이 필요하다. 세 구현의 majority vote도 세 구현이 독립적이지 않으면 증거가 약하다.

### Cross-version testing

serializer와 network codec은 이전 released version의 binary를 corpus로 보존한다. 현재 encode-decode round-trip만 검사하면 과거 호환성 회귀를 찾지 못한다.

## 6. Coverage-guided fuzzing

Fuzzer는 입력을 생성·변형하고 새로운 code coverage를 여는 입력을 corpus에 남긴다. 단순 random보다 length field나 nested parser의 깊은 상태에 도달하기 쉽다. 다만 coverage feedback만으로 유효 checksum이 생기지는 않으므로, checksum repair·structured generation·value profiling/dictionary 같은 별도 보조가 필요하다.

### Fuzz target의 조건

- process-global 상태에 덜 의존
- 같은 입력에서 결정적
- 한 실행이 빠름
- memory·time 상한이 작음
- 실패가 sanitizer·assertion·명시적 oracle로 관찰됨

```cpp
extern "C" int LLVMFuzzerTestOneInput(
    const uint8_t* data, size_t size) {
    Packet packet;
    ParseResult result = ParsePacket({data, size}, packet);
    if (result.ok) {
        CHECK(packet.payload.size() <= kMaxPayload);
        const Bytes encoded = Encode(packet);
        CHECK(encoded.size() <= kMaxPacketBytes);
    }
    return 0;
}
```

parser가 오류를 반환하는 것은 실패가 아니다. out-of-bounds, use-after-free, integer overflow, 무한 loop, 상한 없는 allocation, 부분 외부 상태 commit이 실패다.

ParsePacket/Encode 뒤의 사후 크기 검사는 helper가 이미 거대한 할당이나 loop를 실행한 뒤라면 자원 제한이 아니다. parser 내부 work budget과 process RSS·time 상한을 harness에서 강제하고, 성공 parse는 encode 후 decode가 같은 의미를 내는지도 비교한다.

### Sanitizer 결합

- AddressSanitizer: 범위 밖 접근, use-after-free
- UndefinedBehaviorSanitizer: signed overflow, invalid shift 등
- MemorySanitizer: 초기화되지 않은 값 사용
- ThreadSanitizer: data race

모든 sanitizer를 한 binary에 동시에 넣을 수 없고 실행 비용도 다르다. corpus를 여러 build에 재생한다.

### Byte mutation과 structured generation

byte mutation은 잘린 header, 이상한 length, bit 조합을 잘 찾는다. structured fuzzer는 grammar와 checksum을 맞춰 더 깊은 semantic state에 도달한다.

두 층을 결합할 수 있다.

1. structured generator로 유효 message 생성
2. 일부 field와 byte를 mutation
3. encoder·parser 양쪽 경로 탐색

### Coverage의 한계

line coverage 100%는 assertion이 충분하다는 뜻이 아니다. 상태 공간, integer 경계, thread schedule은 같은 line에서도 다르다. protocol state와 semantic feature coverage를 별도로 기록한다.

```text
message type coverage
length class coverage
state transition coverage
error code coverage
allocation-size bucket coverage
```

### Corpus 관리

중복 coverage input을 최소화하고, 발견 버그를 의미 있는 regression fixture로 승격한다. 수백 MB corpus가 매 commit CI를 막으면 빠른 subset과 장시간 campaign을 분리한다.

## 7. 수치·확률·동시성 검증

### 부동소수점

절대 epsilon 하나는 scale이 달라지면 실패한다.

$$
|a-b|\le\epsilon_{\text{abs}}
+\epsilon_{\text{rel}}\max(|a|,|b|)
$$

비교 tolerance는 알고리즘의 허용 오차 계약에서 나와야 한다. 테스트를 통과시키기 위해 키우는 값이 아니다.

NaN은 모든 순서 비교가 false일 수 있다. finite precondition인지, NaN을 canonicalize할지, 오류로 반환할지 검사한다.

### 조건수와 오차 증폭

행렬 inverse나 constraint solve는 입력 조건수에 따라 오차가 커진다. 결과 오차만 검사하지 말고 residual을 본다.

$$
r=\|Ax-b\|
$$

raw residual $r$ 자체는 scale에 의존하므로 $A$나 $b$의 크기가 작으면 해가 크게 틀려도 작아 보일 수 있다. 정규화한 backward error

$$
\frac{\|Ax-b\|}{\|A\|\|x\|+\|b\|}
$$

와 조건수 기반 forward error, 또는 도메인별 허용 오차를 함께 기록한다. 해 $x$ 자체가 다른 reference와 다르더라도 residual과 domain invariant가 충분히 작을 수 있다.

### 확률 알고리즘

난수 결과 하나를 고정 기대값과 비교하지 않는다. seed를 고정한 deterministic regression과 distribution property를 분리한다.

주사위 $k$개 bucket의 관측값 $O_i$와 기대값 $E_i$에 대한 chi-square statistic:

$$
\chi^2=\sum_{i=1}^{k}\frac{(O_i-E_i)^2}{E_i}
$$

이 검정은 $E_i>0$이고 각 기대도수가 충분히 크며 표본이 독립일 때만 근사 임계값이 유효하다. 추정한 모수만큼 자유도를 보정하고, 기대도수가 작으면 exact/MonteCarlo 검정으로 대체한다. 통계 테스트는 정상 구현도 유의수준 확률로 실패한다. CI에서 단일 flaky threshold로 쓰기보다 sample 수, false-positive budget, 다중 검정 예산, 통과할 때까지 재시험하지 않는 판정 규칙을 설계한다.

### 동시성

동시성 버그의 입력은 값뿐 아니라 schedule이다. stress test를 오래 돌리는 것만으로 희귀 interleaving을 보장하지 않는다.

검증 방법:

- 작은 상태 공간의 deterministic scheduler
- yield·sleep·allocation failure injection
- ready worker 순서의 seed 기반 무작위화
- linearizability history 검사
- ThreadSanitizer

queue 연산의 호출·응답 history가 어떤 순차 순서로 설명 가능한지 검사한다. 이때 완료 응답이 다음 호출보다 먼저인 연산 사이의 실시간 선후 관계를 순서에 보존해야 linearizability다(순차 일관성과 구별되는 조건). 각 연산의 반환값과 미완료 연산의 completion 정책도 history oracle에 포함한다. 가능한 선형화 순서를 찾는 비용은 history 길이에 급격히 증가하므로 작은 concurrent history를 많이 생성한다.

## 8. Mutation testing과 테스트 강도

코드 coverage는 코드가 실행되었다고 말할 뿐 결과를 검사했다고 말하지 않는다. Mutation testing은 작은 의미 변화를 넣고 테스트가 실패하는지 확인한다.

대표 mutation:

```text
<  -> <=
+  -> -
조건문 제거
상수 0·1로 교체
반환값 반전
loop bound 증감
lock 제거
error check 제거
```

테스트가 실패하면 mutant를 죽였다. 살아남은 mutant는 세 종류다.

1. 테스트가 해당 의미를 관찰하지 못함
2. 실행 경로에 도달하지 않음
3. 실제 의미가 같은 equivalent mutant

### Mutation score

$$
\operatorname{score}
=\frac{\text{killed mutants}}
{\text{non-equivalent mutants}}
$$

분모의 equivalent 판정은 자동화하기 어렵다. 점수 자체를 목표로 삼으면 의미 없는 assertion이 늘 수 있다. parser length check, 경제 계산, 권한, sequence wrap, allocator boundary처럼 작은 변화의 위험이 큰 모듈에 집중한다.

### Property mutation

구현뿐 아니라 property를 약화해 본다.

- 원소 보존 property 제거
- tolerance 10배 증가
- error status를 모두 허용
- sequence 일부를 생성하지 않음

테스트 suite가 여전히 통과하는지만 보면 강도를 판단할 수 없다. 정상 구현이 강한 property $P$를 만족하면 더 약한 property $Q$도 만족하므로, 약화된 suite에서 정상 코드가 통과하는 것은 자연스럽다. 강도를 판단하려면 같은 알려진 결함 구현 집합을 강한 검사와 약한 검사에 모두 실행하고, 제거한 property 때문에 새로 통과한 결함 구현을 추적한다. 정렬 property를 예로 들면 다음과 같다.

| 구현 | 정렬성-only | 정렬성+보존 |
|---|---|---|
| 정상 정렬 | 통과 | 통과 |
| 정렬 후 원소 삭제 | 통과 | 실패 |
| 중복 치환 | 통과 | 실패 |

정상 구현이 양쪽을 통과한 것만으로 검사 강도를 판단하지 않는다.

## 9. 비신뢰 입력과 자원 상한

네트워크 packet, save file, shader source, mod content는 공격자 또는 손상 저장 장치가 만든 입력으로 취급한다.

### 길이와 산술

```cpp
bool CanReadArray(size_t remaining,
                  uint64_t count,
                  uint64_t elementSize) {
    return elementSize == 0 ||
           count <= remaining / elementSize;
}
```

`count * elementSize <= remaining`은 곱셈 overflow 뒤 통과할 수 있다. 나눗셈 형태로 검사한다.

### 복합 상한

각 field 길이만 제한해도 전체가 폭발할 수 있다.

```text
maximum packet bytes
maximum object count
maximum string bytes
maximum nesting depth
maximum total decoded bytes
maximum operation budget
```

압축 bomb은 작은 입력에서 큰 출력, recursive format은 작은 입력에서 깊은 stack을 만든다.

### 실패 원자성

parser가 절반의 객체를 live world에 등록한 뒤 오류를 반환하면 외부 상태가 부분 변경된다. 임시 구조에 parse·validate한 뒤 commit한다.

```cpp
ParseResult ParseAndCommit(Bytes input, Registry& registry) {
    TemporaryDocument temp;
    if (!Parse(input, temp)) return ParseResult::Invalid;
    if (!Validate(temp)) return ParseResult::Invalid;
    registry.Commit(std::move(temp));
    return ParseResult::Ok;
}
```

property:

```text
실패한 입력은 registry state hash를 바꾸지 않는다.
```

Commit 자체가 중간에 실패하거나 예외를 던지면 외부 상태가 일부만 바뀔 수 있다. Commit에 strong exception guarantee(실패 시 rollback)나 no-fail swap을 요구하고, 예외·할당 실패 주입을 commit 내부 지점에도 적용한다.

### 시간 복잡도 공격

timeout은 CPU를 이미 소비한 뒤 발생한다. parser·decompressor·path search가 작업할 때 budget을 차감한다.

```cpp
if (!budget.Consume(tokensProcessed))
    return Error::WorkLimitExceeded;
```

입력 길이 $n$에 대해 의도한 복잡도 상한을 sample scaling으로 검사한다. wall time은 noise가 크므로 비교 횟수, visited node, allocation count 같은 직접 counter를 함께 사용한다.

## 10. 지속 가능한 검증 절차

검증은 발견한 bug를 corpus에 쌓는 일회성 campaign이 아니라 코드 변경과 함께 유지되는 구조다.

### 계층

1. **compile-time contract**: type, static assertion, warning
2. **작은 deterministic unit test**: 모든 commit
3. **property·model test**: 여러 seed, 모든 commit 또는 pre-merge
4. **sanitizer corpus replay**: 정기 build
5. **짧은 fuzz smoke test**: CI
6. **장시간 fuzz campaign**: 별도 worker
7. **mutation test**: 핵심 모듈 주기 실행
8. **운영 invariant·telemetry**: 배포 환경

### 실패 triage

1. 원본 seed와 입력을 보존한다.
2. 동일 build에서 재현한다.
3. shrink하여 최소 반례를 얻는다.
4. contract 위반인지 test 오류인지 분리한다.
5. 수정 전에 최소 fixture를 추가한다.
6. 수정 후 관련 generator와 property를 강화한다.

flaky test를 단순 retry로 숨기면 race와 통계 오류를 잃는다. retry는 진단 자료를 더 모으는 용도로만 쓰고, 최종 상태는 flaky 원인을 추적한다.

### 검증 지표

- property별 실행 사례 수가 아니라 semantic bucket coverage
- shrink 전후 입력 크기
- unique crash·assertion 수
- corpus coverage 증가율
- state transition coverage
- sanitizer 종류별 failure
- mutation survivor 분류
- 평균·P95 실행 시간
- flaky seed 재현율

테스트 수와 line coverage는 보조 지표다. 핵심은 중요한 불변식을 어떤 독립 oracle로 검사하며, 과거 실패가 다시 발생하면 확실히 잡히는가다.

### 문서화할 계약

검증하기 어려운 함수는 대개 계약도 불명확하다. 다음을 API 옆에 적는다.

```text
valid input domain
output equivalence relation
determinism guarantee
numerical tolerance
maximum work and memory
failure atomicity
thread-safety and linearization point
```

이 정보가 generator, oracle, shrinker, fuzzer의 설계를 결정한다.

---

## 결론

강한 검증은 많은 예제를 나열하는 것이 아니라 구현과 독립적인 oracle을 세우는 일이다. Property는 결과의 일반 성질을 제한하고, model은 command sequence의 상태 전이를 설명하며, metamorphic·differential testing은 정답을 직접 알기 어려운 문제를 비교 가능하게 만든다. Fuzzing은 coverage와 sanitizer로 깊은 실패 경로를 찾고, shrinking은 그 실패를 이해할 수 있는 반례로 줄인다.

각 기법에는 사각지대가 있다. Round-trip은 같은 encoder·decoder 오류를 놓치고, differential test는 공통 오류를 공유할 수 있으며, coverage는 assertion의 강도를 말하지 않고, 통계 test는 정상 구현도 확률적으로 실패한다. 명세·property·기준 구현·상태 model·mutation·운영 불변식을 겹쳐 서로의 약점을 보완해야 한다.

## 더 읽을거리

- John Hughes, “QuickCheck Testing for Fun and Profit” — property-based testing과 shrinking
- Koen Claessen, John Hughes, “QuickCheck: A Lightweight Tool for Random Testing of Haskell Programs” — QuickCheck 원 논문
- George Candea, Patrice Godefroid, “Automated Software Test Generation: Some Challenges, Solutions, and Recent Advances” — 자동 입력 생성과 탐색
- Barton Miller et al., “An Empirical Study of the Reliability of UNIX Utilities” — fuzz testing의 고전
- Michał Zalewski, *American Fuzzy Lop* 문서 — coverage-guided fuzzing 설계
- Andreas Zeller, *Why Programs Fail* — delta debugging과 실패 원인 축소
- Glenford Myers et al., *The Art of Software Testing* — test 설계와 oracle
- Jepsen 문서와 Knossos 자료 — 상태 history와 linearizability 검증
- GoogleTest, libFuzzer, LLVM Sanitizers 공식 문서 — C++ 검증 도구의 실행 계약
