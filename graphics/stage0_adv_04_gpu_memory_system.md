## 들어가며

GPU 메모리 문제는 단순히 "VRAM이 얼마나 큰가"로 끝나지 않는다. 렌더링 자원은 생성된 뒤에도 물리 메모리에 배치되고, CPU에서 채워지고, GPU 작업과 동기화되고, 예산이 부족하면 퇴거되며, 사용이 끝난 뒤에야 재사용될 수 있다. 이 수명 주기 중 한 경계라도 잘못 다루면 화면 깨짐, 간헐적 device loss, 프레임 지터, 대역폭 낭비가 발생한다.

이 글은 셰이더 내부의 캐시·coalescing보다 한 단계 바깥에 있는 **GPU 자원 메모리 시스템**을 다룬다. 이산 GPU와 UMA의 차이, host-visible·coherent·cached 속성, 전송 비용, 서브할당과 단편화, 상주 예산, 동적 업로드 링, 실행·메모리 동기화, 스트리밍과 검증을 하나의 수명 모델로 연결한다.

핵심 질문은 세 가지다.

1. 이 자원의 바이트는 지금 어느 물리 메모리에 있는가?
2. 어느 실행 주체가 그 바이트를 읽거나 쓸 수 있는가?
3. 그 저장 공간을 언제 안전하게 덮어쓰거나 회수할 수 있는가?


---

## 목차

1. [자원 메모리의 비용 모델](#1-자원-메모리의-비용-모델)
2. [메모리 속성과 CPU 가시성](#2-메모리-속성과-cpu-가시성)
3. [Host-Device 전송](#3-host-device-전송)
4. [할당, 단편화, 상주](#4-할당-단편화-상주)
5. [동적 업로드 링](#5-동적-업로드-링)
6. [실행과 메모리 동기화](#6-실행과-메모리-동기화)
7. [스트리밍과 가상화](#7-스트리밍과-가상화)
8. [측정과 검증](#8-측정과-검증)


---

## 1. 자원 메모리의 비용 모델

### 이산 GPU와 UMA

이산 GPU(discrete GPU)는 일반적으로 CPU의 시스템 메모리와 별도의 VRAM을 가진다. CPU가 만든 정점, 텍스처, 상수 데이터는 PCIe 같은 인터커넥트를 건너 GPU 쪽 저장 공간으로 이동한다. GPU는 VRAM에서 높은 대역폭을 얻지만 CPU와의 왕복 비용을 치른다.

UMA(Unified Memory Architecture)는 CPU와 GPU가 물리 DRAM을 공유한다. 모바일 SoC와 일부 콘솔·통합 GPU가 대표적이다. 물리 복사가 줄어들 수 있지만 다음 비용까지 사라지는 것은 아니다.

- CPU와 GPU의 캐시 계층은 여전히 다를 수 있다.
- 자원 레이아웃과 압축 상태는 GPU 전용일 수 있다.
- 양쪽 장치가 같은 메모리 채널을 두고 경쟁한다.
- 소유권 전환과 캐시 가시성에는 동기화가 필요하다.
- CPU 친화적 페이지 속성과 GPU 친화적 페이지 속성이 다를 수 있다.

따라서 "공유 메모리이므로 복사도 동기화도 없다"는 결론은 성립하지 않는다. UMA의 이점은 주소 공간 또는 물리 저장소의 공유이지, 모든 접근 비용의 동일화가 아니다.

### 용량, 예산, 상주는 다른 값이다

그래픽스 API와 운영체제가 보여 주는 메모리 값은 구분해서 읽어야 한다.

| 값 | 의미 |
|---|---|
| 물리 용량 | 장치나 시스템에 실제로 장착된 메모리 |
| 프로세스 예산 | 현재 프로세스가 압박 없이 사용할 수 있다고 OS가 추정한 범위 |
| 커밋 | 애플리케이션이 backing store를 약속받은 양 |
| 상주 집합 | 현재 GPU가 즉시 접근할 수 있도록 물리 메모리에 올라온 양 |
| 논리적 자원 크기 | 이미지·버퍼가 표현하는 전체 바이트 수 |

물리 VRAM이 12 GiB라고 해서 게임이 12 GiB를 독점할 수 있는 것은 아니다. 화면 합성기, 다른 프로세스, 드라이버 내부 자원도 같은 메모리를 사용한다. 예산은 실행 중 변할 수 있으므로 시작 시 한 번 읽고 고정 상수로 취급해서도 안 된다.

### Working set과 재사용 거리

프레임에 필요한 자원의 집합을 working set이라 하자. 자원 $i$의 크기를 $s_i$, 현재 프레임에서 필요하면 $a_i=1$이라 두면 단순한 활성 용량은

$$
W = \sum_i a_i s_i
$$

이다. 하지만 실제 상주 정책에는 재사용 거리와 교체 비용도 필요하다. 큰 텍스처라도 매 프레임 쓰면 높은 우선순위를 가져야 하고, 작은 자원이라도 다시 만드는 비용이 거의 없으면 먼저 퇴거할 수 있다.

한 자원의 유지 가치를 단순화하면

$$
V_i =
\frac{P_i \cdot C_i}{s_i}
$$

처럼 볼 수 있다. $P_i$는 가까운 시간 안에 재사용될 확률, $C_i$는 다시 읽고 압축 해제하고 업로드하는 비용이다. 이 식은 표준 정책이 아니라 우선순위 설계를 위한 모델이다. 실제 엔진은 가시성, 거리, mip, 게임플레이 중요도, 최근 사용 프레임을 함께 사용한다.

### 평균보다 상위 지연

메모리 시스템의 실패는 평균 프레임보다 꼬리 지연에서 드러난다. 자원 생성, 페이지 마이그레이션, 예산 초과 퇴거, 큰 업로드가 한 프레임에 몰리면 p99 프레임 시간이 튄다. 따라서 다음 값을 함께 기록해야 한다.

- 프레임별 업로드 바이트와 업로드 호출 수
- 할당·해제 횟수와 가장 느린 호출
- 커밋·상주·예산의 시간 변화
- 퇴거와 재상주 횟수
- 업로드 링 대기 시간
- copy queue와 graphics queue의 겹침 비율

## 2. 메모리 속성과 CPU 가시성

### 속성은 위치의 별명이 아니다

저수준 API는 메모리 타입을 여러 속성의 조합으로 노출한다.

| 속성 | 보장하거나 암시하는 것 |
|---|---|
| device-local | 장치 접근에 적합한 물리 메모리 |
| host-visible | CPU 가상 주소 공간에 매핑 가능 |
| host-coherent | CPU와 장치 사이의 일부 명시적 flush/invalidate를 생략 가능 |
| host-cached | CPU 읽기 캐시가 효율적으로 동작 |
| write-combined | CPU 순차 쓰기에 유리하지만 읽기에는 매우 불리할 수 있음 |

`host-visible`은 곧 빠른 CPU 메모리라는 뜻이 아니다. 이산 GPU의 작은 BAR 영역이나 write-combined 메모리일 수 있다. 반대로 UMA에서는 device-local과 host-visible이 같은 물리 힙을 가리킬 수 있다. 속성 비트와 실제 성능은 장치에서 조회하고 측정해야 한다.

### Coherent와 cached를 혼동하지 않는다

Coherent는 가시성 계약이고 cached는 CPU 접근 성능 속성이다. coherent이지만 CPU 읽기가 느린 메모리도 존재할 수 있다. readback은 CPU가 읽어야 하므로 host-cached 타입을 우선하고, 한 방향 업로드는 순차 쓰기 성능이 좋은 타입을 우선한다.

비-coherent 메모리에서는 CPU가 쓴 범위를 flush하고, GPU가 쓴 결과를 CPU가 읽기 전에 invalidate해야 한다. 이때 범위는 API가 요구하는 원자 크기 단위에 맞춰 확장한다.

```cpp
// offset, size: allocation 안의 flush/invalidate 대상 범위(바이트).
// allocationSize, mappedSize: 실제 할당 크기와 CPU에 매핑된 크기.
std::optional<Range> AlignNonCoherentRange(
    uint64_t offset,
    uint64_t size,
    uint64_t allocationSize,
    uint64_t mappedSize,
    uint64_t atomSize)
{
    if (atomSize == 0 || size == 0 || offset > allocationSize ||
        size > allocationSize - offset)          // offset+size overflow 방지 겸용
        return std::nullopt;

    const uint64_t begin = offset - offset % atomSize;
    uint64_t end = offset + size;                // <= allocationSize
    const uint64_t remainder = end % atomSize;
    if (remainder != 0) {
        const uint64_t delta = atomSize - remainder;
        if (end > std::numeric_limits<uint64_t>::max() - delta)
            return std::nullopt;
        end += delta;
    }
    // Vulkan 예외: 정렬된 끝이 allocation 끝에 닿으면 그 지점까지로 자른다.
    if (end > allocationSize)
        end = allocationSize;
    if (end <= begin)
        return std::nullopt;

    // 정렬된 범위가 매핑 밖이면 그대로 flush하지 않고 호출을 거절한다.
    if (begin >= mappedSize || end > mappedSize)
        return std::nullopt;

    return Range{begin, end - begin};
}
```

예를 들어 offset=900, size=100, allocationSize=1000, atomSize=256이면 정렬된 끝이 1024가 되지만, 할당 끝에 닿는 범위의 끝 정렬은 Vulkan이 면제하므로 1000으로 자른다. 정렬 결과가 매핑 범위를 벗어나면 그대로 flush하지 말고 매핑을 넓히거나 호출을 거절한다.

Flush는 GPU 작업 완료를 기다리는 동기화가 아니다. CPU 캐시의 변경을 장치가 볼 수 있는 메모리 영역으로 내보내는 작업일 뿐이다. GPU가 그 범위를 읽기 시작하는 시점은 별도의 실행 의존성으로 보장해야 한다.

### 영구 매핑

업로드 힙을 매 프레임 map/unmap할 필요는 보통 없다. 자원 수명 동안 한 번 매핑하고, 쓰기 오프셋만 이동시키는 영구 매핑(persistent mapping)이 호출 오버헤드와 드라이버 경로를 줄인다.

영구 매핑 포인터의 존재는 CPU가 언제든 안전하게 써도 된다는 뜻이 아니다. GPU가 아직 읽는 범위를 CPU가 덮어쓰면 데이터 레이스다. 안전성은 포인터가 아니라 fence로 퇴역한 바이트 범위가 결정한다.

### Resizable BAR

Resizable BAR가 활성화되면 CPU가 더 큰 device-local 영역을 주소화할 수 있다. 작은 BAR 창을 재매핑하던 제약을 줄일 수 있지만, CPU가 VRAM에 쓰는 대역폭과 지연이 시스템 RAM과 같아지는 것은 아니다. 큰 순차 업로드, GPU가 곧바로 소비하는 데이터, 장치별 측정이라는 조건 아래에서 스테이징 복사를 줄일 수 있는 선택지로 본다.

## 3. Host-Device 전송

### 지연과 대역폭을 분리한다

$N$바이트 전송 시간을 단순화하면

$$
T(N) = L_{\text{submit}} + L_{\text{queue}} + \frac{N}{B_{\text{effective}}}
$$

이다. $L_{\text{submit}}$은 호출·명령 기록·제출의 고정 비용, $L_{\text{queue}}$는 큐에서 실제 실행될 때까지의 대기, $B_{\text{effective}}$는 프로토콜과 접근 패턴을 포함한 유효 대역폭이다.

작은 복사 수천 번은 $L_{\text{submit}}$에 지배되고, 큰 복사 한 번은 대역폭에 지배된다. 그래서 업로드를 배치하되, 너무 큰 배치가 한 프레임의 copy queue를 독점하지 않도록 시간 예산도 둔다.

PCIe x16의 세대별 수치를 사용할 때는 원시 전송률, 인코딩 오버헤드, 방향, 칩셋 연결, 실제 DMA 효율을 구분한다. 명목 대역폭을 그대로 애플리케이션 복사 속도로 쓰면 안 된다.

### Pageable, pinned, mapped

운영체제가 이동하거나 스왑할 수 있는 일반 pageable 메모리는 장기 DMA의 안정적인 원본이 되기 어렵다. 런타임은 내부 pinned 버퍼를 거치거나 호출 동안 페이지를 고정할 수 있다. 애플리케이션이 pinned 메모리를 직접 관리하면 중간 복사를 줄일 수 있지만 과도한 고정은 OS의 메모리 관리 능력을 해친다.

권장 패턴은 제한된 크기의 영구 매핑 업로드 영역을 재사용하는 것이다.

1. CPU가 퇴역한 업로드 범위를 확보한다.
2. 데이터를 연속 범위에 복사한다.
3. 비-coherent 타입이면 정렬된 범위를 flush한다.
4. copy 명령을 기록하고 신호 값을 제출한다.
5. 소비 큐가 copy 완료 값에 의존하도록 한다.
6. 완료 값이 관측된 뒤 해당 바이트를 재사용한다.

### 전송과 계산 겹치기

복사 엔진과 그래픽스/컴퓨트 엔진이 독립적이면 업로드와 렌더링을 겹칠 수 있다. 단, 큐가 여러 개라는 사실만으로 병렬 실행이 보장되지는 않는다. 같은 메모리 대역폭을 경쟁하거나 하드웨어 큐가 내부적으로 직렬화될 수 있다.

겹침의 조건은 자원 의존성을 좁게 만드는 것이다. 프레임 전체가 하나의 업로드 완료를 기다리게 하지 말고, 실제로 새 데이터가 필요한 pass만 copy 완료 값에 의존시킨다.

```text
CPU decode ──> upload range ──> copy queue ──> texture mip 5
                                            └─signal 42

graphics queue: shadow pass ────────────────> terrain pass(wait 42)
```

이 구조에서는 shadow pass가 업로드와 겹칠 수 있다.

### Readback은 더 보수적으로 다룬다

GPU 결과를 CPU로 가져오는 readback은 파이프라인을 역방향으로 건넌다. CPU가 즉시 결과를 요구하면 GPU 완료까지 대기하면서 프레임을 직렬화한다. 가시성 질의, 타이밍, 스크린샷, 피드백은 가능한 한 몇 프레임 늦게 소비하고 여러 요청을 한 버퍼에 배치한다.

## 4. 할당, 단편화, 상주

### 자원과 메모리 할당을 분리한다

저수준 API의 이미지·버퍼 객체와 물리 메모리 할당은 별개다. 자원은 크기, 정렬, 호환 메모리 타입, dedicated allocation 필요 여부를 요구사항으로 낸다. 할당자는 이 요구사항을 만족하는 큰 블록의 일부를 자원에 바인딩한다.

자원마다 장치 할당을 하나씩 만들면 다음 문제가 생긴다.

- 드라이버·커널 호출 비용이 커진다.
- 장치별 최대 할당 개수에 접근한다.
- 작은 자원마다 페이지·메타데이터 낭비가 생긴다.
- 전체 단편화를 통제하기 어렵다.

큰 블록을 받고 그 안을 서브할당하는 것이 기본이다. 매우 큰 이미지, 외부 공유 메모리, 장치가 dedicated allocation을 선호하거나 요구하는 자원은 별도 할당이 적합하다.

### 할당자 선택

| 워크로드 | 적합한 구조 | 핵심 제약 |
|---|---|---|
| 프레임 임시 업로드 | 선형/링 할당자 | fence 순서로 일괄 회수 |
| 수명이 비슷한 렌더 타깃 | 선형 arena | 프레임 그래프 수명 분석 |
| 다양한 장기 자원 | TLSF, segregated free list | 외부 단편화 관리 |
| 2의 거듭제곱 블록 | buddy | 내부 단편화 |
| 고정 크기 디스크립터·타일 | bitmap/slab | 크기 클래스 고정 |

GPU 할당자는 바이트 크기만 받아서는 안 된다. 정렬, 메모리 타입, 자원 종류에 따른 granularity, dedicated 요구, alias 가능 여부가 키에 포함되어야 한다.

### 단편화

내부 단편화는 할당된 블록 안의 낭비이고, 외부 단편화는 총 여유 공간은 충분하지만 연속된 큰 구간이 없는 상태다.

$$
F_{\text{external}} =
1 - \frac{\text{largest free block}}{\text{total free bytes}}
$$

이 값은 외부 단편화의 한 가지 지표다. 0에 가까우면 여유 공간이 한 덩어리에 가깝고, 1에 가까우면 잘게 쪼개져 있다. 이것만으로 재배치 비용이나 메모리 타입별 분포까지 설명하지는 못하므로 크기 히스토그램과 함께 본다.

### Aliasing은 수명 분석 문제다

동시에 살아 있지 않은 두 transient 자원은 같은 메모리 구간을 공유할 수 있다. 핵심은 "같은 프레임에서 쓰이는가"가 아니라 실제 실행 구간이 겹치는가다.

자원 $A$와 $B$의 사용 구간을 각각 $[s_A,e_A]$, $[s_B,e_B]$라 하면

$$
e_A < s_B \quad \text{또는} \quad e_B < s_A
$$

일 때만 수명 관점에서 겹치지 않는다. 비동기 compute queue가 있으면 pass의 논리적 순서와 실제 실행 구간이 다를 수 있으므로 큐 간 의존성까지 포함해야 한다.

Aliasing에는 추가 조건이 있다.

- 두 자원의 크기와 정렬을 만족하는 오프셋이어야 한다.
- API가 요구하는 alias/placed-resource 생성 플래그를 사용한다.
- 이전 사용의 쓰기가 끝난 뒤 aliasing barrier 또는 이에 해당하는 의존성을 둔다.
- 압축 메타데이터나 이미지 레이아웃 전환 조건을 지킨다.
- 같은 물리 범위를 동시에 참조하지 않는다.

### 상주와 퇴거

논리적 자원이 존재해도 물리 메모리에 상주하지 않을 수 있다. 예산을 넘기면 OS와 드라이버가 페이지를 시스템 메모리로 옮기거나, API가 명시적 상주 관리를 요구할 수 있다. 페이지가 필요해지는 순간 재상주 비용이 프레임 지터로 나타난다.

상주 정책에는 hard limit보다 두 개의 문턱을 두는 편이 안정적이다.

```text
예산의 90% 초과: 퇴거 시작
예산의 80% 이하: 퇴거 중지
```

이 hysteresis가 없으면 경계에서 같은 자원을 반복해서 올리고 내리는 thrashing이 발생한다. 퇴거 우선순위는 최근 사용만 보지 말고 재생성 비용, 최소 품질 mip, 게임플레이 중요도를 포함한다.

## 5. 동적 업로드 링

### 절대 커서로 wrap을 표현한다

링 버퍼 구현에서 `head < tail` 같은 원형 오프셋 비교만 사용하면 wrap 이후의 사용 구간을 잘못 판정하기 쉽다. 논리적 커서를 계속 증가하는 64비트 절대 위치로 두고, 실제 버퍼 오프셋만 나머지 연산으로 구한다.

```cpp
// 요청 정렬을 checked 덧셈으로 계산한다. overflow면 nullopt.
static std::optional<uint64_t> AlignUpChecked(uint64_t x, uint64_t alignment) {
    if (alignment == 0) return std::nullopt;
    const uint64_t remainder = x % alignment;
    const uint64_t delta = remainder == 0 ? 0 : alignment - remainder;
    if (x > std::numeric_limits<uint64_t>::max() - delta)
        return std::nullopt;
    return x + delta;
}

class UploadRing {
public:
    struct Slice {
        uint64_t begin;   // 절대 위치
        uint64_t size;
        uint64_t offset;  // begin % capacity
        std::byte* cpu;
    };

    std::optional<Slice> Allocate(uint64_t size, uint64_t alignment) {
        // size<=capacity와 checked alignment를 먼저 거절한다.
        if (mapped_ == nullptr || capacity_ == 0 || size == 0 ||
            size > capacity_ || alignment == 0 || capacity_ % alignment != 0)
            return std::nullopt;

        // GPU 사용도, 아직 제출하지 않은 예약도 전부 없는 경우에만 기준을 재설정한다.
        const bool completelyEmpty = inFlight_.empty() && write_ == retired_;
        const uint64_t cursor = completelyEmpty ? 0 : write_;
        const uint64_t retired = completelyEmpty ? 0 : retired_;

        auto aligned = AlignUpChecked(cursor, alignment);
        if (!aligned) return std::nullopt;
        uint64_t begin = *aligned;
        uint64_t offset = begin % capacity_;

        // offset + size 대신 overflow 없는 뺄셈으로 물리 버퍼 끝을 검사한다.
        if (size > capacity_ - offset) {
            aligned = AlignUpChecked(begin, capacity_);
            if (!aligned) return std::nullopt;
            begin = *aligned;
            offset = 0;
        }

        if (begin < retired) return std::nullopt;
        const uint64_t usedWithPadding = begin - retired;
        if (usedWithPadding > capacity_ || size > capacity_ - usedWithPadding)
            return std::nullopt;

        // 성공 직전에만 상태를 갱신한다. 실패 경로는 head를 변경하지 않는다.
        retired_ = retired;
        write_ = begin + size;
        return Slice{begin, size, offset, mapped_ + offset};
    }

    void SubmitBatch(uint64_t fenceValue) {
        inFlight_.push_back({fenceValue, write_});
    }

    void Retire(uint64_t completedFence) {
        while (!inFlight_.empty() &&
               inFlight_.front().fenceValue <= completedFence) {
            retired_ = inFlight_.front().end;
            inFlight_.pop_front();
        }
    }

private:
    struct Batch {
        uint64_t fenceValue;
        uint64_t end;
    };

    uint64_t capacity_ = 0;
    uint64_t write_ = 0;
    uint64_t retired_ = 0;
    std::byte* mapped_ = nullptr;
    std::deque<Batch> inFlight_;
};
```

`capacity_`를 2의 거듭제곱으로 제한하면 modulo를 mask로 바꿀 수 있지만 정확성에는 필요하지 않다. 다만 `offset = begin % capacity_`가 요청한 정렬을 유지하려면 `capacity_`는 사용하는 모든 `alignment`의 배수여야 한다. `AlignUpChecked(begin, capacity_)`는 capacity가 일반 정수여도 동작하는 정수 나눗셈 기반 구현이어야 한다.

### 프레임 수가 아니라 완료 값이 수명을 결정한다

"triple buffering이므로 3프레임 뒤 재사용"은 GPU가 항상 3프레임 안에 끝난다는 가정이다. 프레임이 밀리거나 비동기 큐가 지연되면 깨진다. 실제 fence/timeline 값으로 완료를 확인해야 한다.

공간 부족 시 정책도 명시한다.

| 정책 | 장점 | 위험 |
|---|---|---|
| CPU 대기 | 구현 단순, 메모리 고정 | 프레임 지터 |
| 보조 블록 할당 | 대기 회피 | 메모리 최고점 증가 |
| 중요도 낮은 업로드 연기 | 프레임 안정 | 품질·가시성 지연 |
| 데이터 축소·병합 | 대역폭 절약 | CPU 작업 증가 |

실시간 렌더러는 대개 작은 보조 블록과 업로드 연기를 조합하고, 발생 횟수를 계측한다. 공간 부족을 조용히 fence wait으로 숨기면 원인을 찾기 어렵다.

### 정렬 규칙

업로드 슬라이스 정렬은 CPU SIMD 정렬만의 문제가 아니다. API의 uniform/constant buffer offset 정렬, 텍스처 row pitch, copy offset, 비-coherent atom 크기가 각각 다르다. 필요한 정렬들의 최소공배수 또는 용도별 링을 사용한다.

## 6. 실행과 메모리 동기화

### 네 가지 상태를 분리한다

GPU 동기화는 한 종류의 "barrier"로 뭉뚱그리면 이해하기 어렵다.

1. **실행 의존성**: 소비 작업이 생산 작업보다 먼저 실행되지 않게 한다.
2. **메모리 가용성**: 생산자의 쓰기가 메모리 계층 밖으로 공개되게 한다.
3. **메모리 가시성**: 소비자의 읽기가 공개된 값을 관측하게 한다.
4. **자원 상태·소유권**: 레이아웃, 압축 상태, 큐 소유권을 소비 용도에 맞춘다.

Fence는 보통 GPU 완료를 CPU가 관측하는 도구다. Semaphore/timeline 값은 큐 사이 실행 순서를 잇는다. Pipeline/resource barrier는 한 큐의 단계와 접근 범위, 자원 상태를 연결한다. API마다 이름은 달라도 이 네 질문으로 해석할 수 있다.

### Hazard에서 출발한다

| Hazard | 예 | 필요한 조치 |
|---|---|---|
| RAW | copy가 쓴 텍스처를 fragment가 읽음 | copy 완료 후 읽기 가시성·상태 전환 |
| WAR | fragment가 읽는 버퍼를 compute가 덮어씀 | 읽기 완료 뒤 쓰기 실행 |
| WAW | 두 pass가 같은 이미지에 기록 | 기록 순서와 최종 상태 명시 |
| RAR | 둘 다 읽기 | 대개 메모리 의존성 불필요 |

배리어는 "혹시 모르니 전체 단계에서 전체 단계로" 두는 것이 안전한 습관이 아니다. 정확성은 얻을 수 있어도 독립 작업까지 직렬화한다. 생산 단계, 소비 단계, 실제 접근 종류, 자원 범위를 좁혀야 한다.

### CPU 캐시 조작과 GPU 완료는 별개다

비-coherent 메모리의 flush/invalidate, CPU mutex, GPU fence는 서로 대체할 수 없다.

- flush: CPU 쓰기를 장치가 볼 수 있게 만든다.
- GPU barrier: GPU 작업 사이의 실행·메모리 의존성을 만든다.
- fence wait: GPU가 특정 제출을 끝냈음을 CPU가 확인한다.
- invalidate: GPU 쓰기를 CPU 캐시가 새로 읽게 만든다.

Readback의 안전한 순서는 GPU 쓰기 제출, 완료 값 대기, 필요 시 invalidate, CPU 읽기다. Upload는 CPU 쓰기, 필요 시 flush, copy/consumer 의존성 제출 순서다.

### Queue ownership

전용 copy queue에서 만든 자원을 graphics queue가 소비할 때 단순 semaphore만으로 충분한지는 API의 queue family·resource state 규칙에 달려 있다. 실행 순서와 함께 소유권 이전 또는 공용(concurrent) 소유 정책이 필요할 수 있다. 공용 정책은 전환을 줄이지만 장치가 더 보수적인 경로를 택할 수 있으므로 측정한다.

## 7. 스트리밍과 가상화

### 스트리밍은 다단계 큐다

자원 하나가 화면에 나타나기까지는 여러 서비스 시간이 합쳐진다.

```text
요청 생성
  -> 저장 장치 읽기
  -> 압축 해제·변환
  -> 업로드 링 확보
  -> copy queue 전송
  -> 상태 전환·페이지 테이블 갱신
  -> 렌더 pass 소비
```

전체 지연을

$$
T_{\text{ready}} =
T_{\text{queue}} +
T_{\text{io}} +
T_{\text{decode}} +
T_{\text{upload}} +
T_{\text{sync}}
$$

로 나누면 어느 단계가 병목인지 추적할 수 있다. "텍스처 로딩이 느리다"는 하나의 원인이 아니다.

각 단계는 bounded queue를 가져야 한다. 무제한 요청 큐는 카메라가 빠르게 회전할 때 이미 필요 없어진 자원까지 계속 처리해 지연과 메모리를 증폭시킨다. 요청에는 세대 번호와 취소 상태를 두고, 소비 직전에도 아직 유효한지 확인한다.

### 프레임별 예산

스트리밍은 남은 VRAM뿐 아니라 프레임 시간과 I/O 대역폭을 함께 예산화한다.

```text
프레임당 CPU decode: 1.0 ms
프레임당 upload: 24 MiB
동시 저장 장치 요청: 16
copy queue 목표 점유: 2.0 ms 이하
최소 상주 mip: 게임플레이 가독성을 보장하는 수준
```

수치는 하드웨어 등급과 콘텐츠에 따라 달라진다. 중요한 것은 모든 요청을 즉시 처리하는 대신 우선순위 큐가 제한된 예산을 나눠 갖게 하는 구조다.

### 가상 텍스처

가상 텍스처는 거대한 논리 이미지의 타일 일부만 물리 캐시에 둔다.

1. 셰이더가 논리 페이지 번호를 계산한다.
2. 페이지 테이블이 물리 캐시 위치와 상주 mip를 알려 준다.
3. 누락된 페이지는 더 낮은 해상도 조상 페이지로 대체한다.
4. 피드백을 모아 필요한 페이지를 요청한다.
5. 업로드 완료 뒤 페이지 테이블을 원자적으로 새 매핑으로 바꾼다.

페이지 테이블을 실제 타일보다 먼저 갱신하면 셰이더가 아직 채워지지 않은 캐시를 읽는다. 타일 copy와 필요한 전환이 완료된 뒤 매핑을 공개해야 한다. 반대로 캐시 슬롯을 재사용할 때는 그 슬롯을 가리키는 오래된 페이지 테이블 항목과 in-flight 프레임이 모두 퇴역했는지 확인한다.

### Thrashing 방지

카메라 경계에서 두 페이지 집합이 번갈아 필요해지면 LRU만으로는 매 프레임 교체가 반복될 수 있다. 완화 방법은 다음과 같다.

- 퇴거까지 최소 상주 시간을 둔다.
- 화면 경계 밖에 prefetch 여유 영역을 둔다.
- 고해상도 mip 승격과 강등에 서로 다른 문턱을 둔다.
- 동일 자원의 연속 요청을 합치고 오래된 세대 요청을 취소한다.
- 물리 캐시 일부를 핵심 UI·지형·게임플레이 자원에 예약한다.

### 압축과 랜덤 접근

GPU 블록 압축 텍스처는 물리 캐시에 압축 상태로 둘 수 있어 용량과 샘플 대역폭을 함께 줄인다. 저장 장치용 스트림 압축은 압축률만 높이면 되는 것이 아니다. 작은 타일을 하나 읽기 위해 거대한 앞 구간을 모두 해제해야 한다면 랜덤 접근 지연이 나빠진다. 청크 크기, 독립 복호화 가능성, checksum, dictionary 범위를 스트리밍 단위와 맞춘다.

## 8. 측정과 검증

### 계측 항목

GPU 메모리 시스템은 아래 값을 프레임 타임라인과 같은 시계에 기록해야 한다.

| 범주 | 값 |
|---|---|
| 용량 | heap별 budget, usage, committed, resident |
| 할당 | 블록 수, 서브할당 수, 최대 free block, 내부·외부 단편화 |
| 전송 | 바이트, 호출 수, 배치 크기 분포, copy queue 시간 |
| 링 | 사용률, wrap, 대기·보조 블록 발생, 가장 오래된 미완료 fence |
| 스트리밍 | 단계별 큐 길이, 취소율, cache hit, mip 승격·강등 |
| 동기화 | queue wait, barrier 수와 범위, CPU fence wait |

평균만 저장하지 말고 프레임별 원자료 또는 p50/p95/p99를 남긴다. 지터를 일으킨 프레임의 자원 ID와 요청 원인을 역추적할 수 있어야 한다.

### 오류 주입

정상 PC 한 대에서만 실행하면 수명 버그가 잘 드러나지 않는다. 아래 조건을 자동으로 주입한다.

- 사용 가능한 예산을 의도적으로 절반 이하로 제한한다.
- copy 완료를 무작위로 지연한다.
- 링 용량을 줄여 매분 wrap과 고갈을 발생시킨다.
- 비-coherent 메모리 경로를 강제로 사용한다.
- 스트리밍 요청을 재정렬·취소한다.
- 창 전환과 해상도 변경 중 자원을 반복 생성·파괴한다.
- 장치 제거 또는 제출 실패를 모의한다.

검증 레이어와 API 디버그 계층은 잘못된 상태 전환·수명·동기화를 찾는 데 유용하지만 모든 데이터 레이스를 증명하지는 않는다. 자원마다 마지막 writer, 접근 범위, 제출 값, 세대 번호를 디버그 빌드에 기록하면 재현하기 어려운 alias·use-after-free를 추적하기 쉽다.

### 성능 실험

전송 전략을 비교할 때는 동일한 총 바이트만 맞춰서는 안 된다.

1. 작은 복사 개수와 배치 크기를 통제한다.
2. CPU 메모리가 pageable인지 pinned인지 구분한다.
3. GPU 소비가 직렬화되는지 독립 큐와 겹치는지 구분한다.
4. warm-up과 첫 페이지 fault를 별도로 기록한다.
5. GPU timestamp와 CPU 제출 시간을 함께 측정한다.
6. 결과가 실제로 소비되게 하여 최적화로 제거되지 않게 한다.

메모리 할당자 비교도 처리량만 보지 않는다. 최대 상주 메모리, 단편화, p99 할당 시간, 예산 압박 뒤 회복 시간, 장시간 실행 후의 안정성을 함께 비교한다.


---

## 결론

GPU 자원 메모리는 **배치, 가시성, 수명**의 문제다. 자원의 논리적 존재와 물리 상주를 구분하고, host-visible·coherent·cached 속성을 각각의 계약으로 해석해야 한다. 전송은 고정 지연과 유효 대역폭으로 나누어 배치하며, CPU와 GPU의 병렬성을 유지하려면 필요한 소비 작업에만 좁은 의존성을 건다.

할당자는 자원 요구 정렬과 메모리 타입을 포함해 서브할당하고, transient 자원의 실제 실행 수명을 분석해 alias한다. 동적 업로드 공간은 프레임 수가 아니라 완료 값으로 회수한다. 스트리밍은 I/O·복호화·업로드·공개의 bounded pipeline이며, 상주 예산과 hysteresis가 없으면 thrashing이 발생한다.

정확성의 기준은 "배리어를 많이 넣었다"가 아니다. 어느 생산자가 어떤 범위를 썼고, 어느 소비자가 언제 읽으며, 저장 공간을 어느 완료 값 이후 재사용할 수 있는지 설명할 수 있어야 한다. 성능 판단 역시 명목 VRAM과 평균 대역폭이 아니라 예산 변화, p99 대기, 실제 queue overlap, cache hit와 퇴거 기록으로 내려야 한다.

## 더 읽을거리

- Khronos Group, [Vulkan Specification: Memory Allocation](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#memory)
- Khronos Group, [Vulkan Specification: Synchronization and Cache Control](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#synchronization)
- Microsoft, [D3D12 Memory Management](https://learn.microsoft.com/windows/win32/direct3d12/memory-management)
- Microsoft, [Residency](https://learn.microsoft.com/windows/win32/direct3d12/residency)
- GPUOpen, [Vulkan Memory Allocator](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/)
- NVIDIA, [CUDA C++ Best Practices Guide: Data Transfer](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)
- Adam Sawicki, [Memory Management in Vulkan and DX12](https://gpuopen.com/download/gdc_2018_tutorial_memory_management_vulkan_dx12.pptx) (GDC 2018)
