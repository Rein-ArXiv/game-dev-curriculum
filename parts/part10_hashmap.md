## 들어가며

해시 맵은 평균 $O(1)$로 키-값을 조회할 수 있는 자료구조지만, 표준 라이브러리의 `std::unordered_map`이 곧바로 고성능 시스템의 요구를 만족시키는 경우는 드물다. 표준이 요구하는 참조 안정성 때문에 대부분 구현이 chaining을 채택하고, chaining은 노드 단위 힙 할당과 캐시 미스를 그대로 비용으로 안고 간다.

이 글은 해시 함수와 충돌 해결의 기초부터 시작해 `std::unordered_map`의 구조적 한계를 들여다보고, Linear/Quadratic/Double Hashing의 트레이드오프를 정리한 뒤, Robin Hood Hashing이 PSL을 분산 균등화로 어떻게 풀어내는지 본다. 이어서 Unreal `TMap`, EASTL `hash_map`, Abseil `flat_hash_map`(Swiss Table)을 각각의 설계 결정 관점에서 살펴보고, 게임 엔진에서 자주 쓰이는 string interning까지 다룬다. 마지막에는 Robin Hood Hash Map을 처음부터 구현하는 가이드를 둔다.

다루는 분량은 길지만 골격은 단순하다. 모든 해시 테이블은 "어떤 슬롯 배열 위에서 충돌과 빈 슬롯을 어떻게 관리하는가"의 변주이며, 이 글은 그 변주들을 정렬해서 비교 가능한 형태로 만들어두는 데 목적이 있다.


---

## 목차
1. [해시 테이블의 기초: 충돌은 피할 수 없다](#1-해시-테이블의-기초-충돌은-피할-수-없다)
2. [std::unordered_map의 한계](#2-stdunordered_map의-한계)
3. [Open Addressing의 진화](#3-open-addressing의-진화)
4. [Robin Hood Hashing](#4-robin-hood-hashing)
5. [게임 엔진의 해시 맵 구현](#5-게임-엔진의-해시-맵-구현)
6. [String Interning](#6-string-interning)
7. [구현 가이드: 실전 Robin Hood Hash Map](#7-구현-가이드-실전-robin-hood-hash-map)
8. [언제 무엇을 사용할 것인가](#8-언제-무엇을-사용할-것인가)


---

## 1. 해시 테이블의 기초: 충돌은 피할 수 없다

### 해시 함수의 역할과 특성

해시 함수는 임의 크기의 데이터를 고정 크기의 정수 값으로 매핑하는 함수이다. 좋은 해시 함수는 다음 특성을 만족해야 한다.

**결정성(Deterministic)**: 동일한 입력은 항상 동일한 해시 값을 생성해야 한다. 이는 너무나 당연해 보이지만, 해시 값이 메모리 주소나 시간에 의존한다면 문제가 된다.

**균등 분포(Uniform Distribution)**: 해시 값이 전체 범위에 고르게 분포해야 한다. 특정 버킷에만 값이 몰린다면, 해시 테이블의 성능 이점이 사라진다.

**빠른 계산(Fast Computation)**: 해시 계산 자체가 병목이 되어서는 안 된다. 암호학적으로 안전한 해시(SHA-256 등)는 이 용도에는 과도하다.

**눈사태 효과(Avalanche Effect)**: 입력의 작은 변화가 해시 값을 크게 변화시켜야 한다. 이는 비슷한 키들이 다른 버킷으로 분산되도록 한다.

실전에서 자주 사용되는 해시 함수를 살펴보자:

```cpp
// FNV-1a: 빠르고 간단한 해시 함수
uint32_t fnv1a_hash(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;  // FNV prime
    }
    return hash;
}

// MurmurHash3의 최종 믹싱 단계 (simplified)
uint32_t murmur3_finalize(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}
```

FNV-1a는 매우 간단하면서도 좋은 분포를 제공한다. 각 바이트를 XOR한 후 소수를 곱하는 과정을 반복하는데, 이 소수 값들은 수학적으로 좋은 분포를 만들도록 선택되었다. MurmurHash3는 더 복잡한 비트 믹싱을 통해 더 나은 균등 분포를 제공하지만, 계산 비용이 약간 더 든다.

해시 값을 배열 인덱스로 변환할 때도 주의가 필요하다:

```cpp
// 느린 방법: 모듈로 연산은 나눗셈이므로 비쌈
size_t index = hash % capacity;

// 빠른 방법: 비트마스크 (단, capacity가 2의 거듭제곱일 때만)
size_t index = hash & (capacity - 1);
```

런타임 정수 나눗셈은 비트 AND보다 일반적으로 처리량과 지연이 크다. capacity가 컴파일 타임 상수라면 컴파일러가 `%`를 곱셈·시프트로 바꿀 수도 있다. 2의 거듭제곱 capacity는 마스크 계산과 성장을 단순하게 하지만, 버킷 선택이 해시의 하위 비트 품질에 직접 의존하므로 충분한 mixing이 필요하다.

### 충돌 해결: 두 가지 철학

아무리 좋은 해시 함수를 사용해도, 비둘기집 원리(Pigeonhole Principle)에 의해 충돌은 불가피하다. 무한한 입력 공간을 유한한 버킷 공간으로 매핑하기 때문이다. 충돌을 처리하는 두 가지 주요 방식이 있다.

#### Chaining: 연결 리스트로 충돌 관리

Chaining은 각 버킷이 연결 리스트를 유지하는 방식이다. 같은 버킷에 해싱되는 모든 키들을 리스트로 연결한다.

```cpp
template<typename K, typename V>
class ChainedHashMap {
    struct Node {
        K key;
        V value;
        Node* next;
    };

    std::vector<Node*> buckets;
    size_t num_elements;

    size_t get_bucket(const K& key) const {
        return hash(key) & (buckets.size() - 1);
    }

public:
    V* find(const K& key) {
        size_t bucket = get_bucket(key);
        Node* node = buckets[bucket];

        // 연결 리스트 순회
        while (node) {
            if (node->key == key) {
                return &node->value;
            }
            node = node->next;  // 각 단계마다 캐시 미스 가능
        }
        return nullptr;
    }

    void insert(const K& key, const V& value) {
        size_t bucket = get_bucket(key);

        // 새 노드를 리스트 앞에 추가
        Node* new_node = new Node{key, value, buckets[bucket]};
        buckets[bucket] = new_node;
        num_elements++;
    }
};
```

Chaining의 장점은 구현이 단순하고, 삭제가 쉽다는 점이다. 로드 팩터(요소 수 / 버킷 수)가 1.0을 초과해도 작동한다. 하지만 치명적인 단점이 있다.

**캐시 비효율**: 개별 할당된 노드는 연속 slot 배열보다 locality가 낮을 수 있다. load-to-use latency는 CPU와 hit한 cache 계층, TLB·NUMA 상태에 따라 달라지므로 L1과 DRAM을 한 고정 cycle 수로 비교하지 않는다. 중요한 구조적 차이는 다음 노드 주소를 알아야 그다음 load를 시작할 수 있는 의존 사슬이라 memory-level parallelism이 제한된다는 점이다.

**포인터 추적 비용**: 각 노드 접근마다 포인터를 역참조해야 한다. 이는 추가적인 메모리 로드를 의미한다.

**메모리 오버헤드**: 각 노드에 next 포인터와 allocator metadata·정렬 padding이 추가된다. 포인터 크기는 데이터 모델에 따라 다르며 작은 키/값에서는 오버헤드 비율이 커질 수 있다.

```cpp
// 예: int → int 매핑
struct Node {
    int key;      // 4 bytes
    int value;    // 4 bytes
    Node* next;   // 8 bytes
    // 총 16 bytes, 실제 데이터는 8 bytes (50% 오버헤드!)
};
```

#### Open Addressing: 모든 것을 배열 안에

Open Addressing은 다른 접근법을 취한다. 모든 요소를 단일 배열 내에 저장하고, 충돌 시 다른 빈 슬롯을 찾는 탐색(probing)을 수행한다.

```cpp
template<typename K, typename V>
class OpenAddressHashMap {
    struct Slot {
        K key;
        V value;
        enum State { EMPTY, OCCUPIED, DELETED } state;
    };

    std::vector<Slot> slots;
    size_t num_elements;

public:
    V* find(const K& key) {
        size_t index = hash(key) & (slots.size() - 1);

        // 빈 슬롯을 만날 때까지 탐색
        while (slots[index].state != EMPTY) {
            if (slots[index].state == OCCUPIED &&
                slots[index].key == key) {
                return &slots[index].value;
            }
            index = (index + 1) & (slots.size() - 1);  // Linear probing
        }
        return nullptr;
    }

    void insert(const K& key, const V& value) {
        if (load_factor() > 0.7) {
            rehash();
        }

        size_t index = hash(key) & (slots.size() - 1);
        size_t first_deleted = slots.size();

        // 톰스톤을 발견해도 같은 probe chain 뒤의 기존 키를 먼저 찾는다.
        size_t probed = 0;
        while (probed < slots.size() && slots[index].state != EMPTY) {
            if (slots[index].state == OCCUPIED && slots[index].key == key) {
                slots[index].value = value;  // Update
                return;
            }
            if (slots[index].state == DELETED && first_deleted == slots.size()) {
                first_deleted = index;
            }
            index = (index + 1) & (slots.size() - 1);
            ++probed;
        }

        if (probed == slots.size() && first_deleted == slots.size()) {
            rehash();
            insert(key, value);
            return;
        }

        size_t target = first_deleted != slots.size() ? first_deleted : index;
        slots[target] = {key, value, OCCUPIED};
        num_elements++;
    }
};
```

Open Addressing의 장점은 명확하다. **캐시 친화적**이다. 모든 데이터가 연속된 배열에 있어 공간 지역성(spatial locality)이 우수하다. **메모리 효율적**이다. 포인터 오버헤드가 없고, 단일 배열 할당만 필요하다.

하지만 삭제가 복잡하다. 단순히 슬롯을 비우면 탐색 체인이 끊어진다. 따라서 "톰스톤(tombstone)" 상태가 필요하다. 그리고 로드 팩터가 높아지면 성능이 급격히 저하된다.

위 축약 코드는 `slots.size()`가 0이 아닌 2의 거듭제곱이고 `rehash()`가 더 큰 배열에 원소를 다시 삽입한다는 계약을 전제로 한다. 실제 구현은 tombstone 수도 별도로 세어, 살아 있는 원소의 load factor가 낮더라도 tombstone이 probe chain을 길게 만들면 재해시한다.

### 로드 팩터와 성능의 관계

로드 팩터 $\alpha = n / m$ (n은 요소 수, m은 슬롯 수)은 해시 테이블 성능의 핵심 지표이다.

Chaining에서 평균 탐색 길이는 대략 $1 + \alpha/2$ 이다. $\alpha = 1.0$ 이어도 평균 1.5번만 탐색하면 된다. 하지만 각 탐색이 캐시 미스를 동반한다면 실제 성능은 이론보다 훨씬 나쁘다.

단순 균등 해싱 가정 아래 linear probing의 성공 탐색 기대 probe 수는 대략
$\frac{1}{2}(1 + \frac{1}{1-\alpha})$, 실패 탐색은
$\frac{1}{2}(1 + \frac{1}{(1-\alpha)^2})$다. $\alpha$가 1에 가까워질수록 특히 실패 탐색과 삽입 비용이 급격히 커진다. 재해시 임계값은 구현의 probe 정책, SIMD group 크기와 삭제 방식에 따라 달라지므로 0.7이 보편 규칙은 아니다.

## 2. std::unordered_map의 한계

C++ 표준 라이브러리의 `std::unordered_map`은 대부분의 구현에서 Chaining을 사용한다. 왜 표준이 Chaining을 선택했을까? 그것은 표준이 **참조 안정성(reference stability)** 을 요구하기 때문이다.

```cpp
std::unordered_map<int, std::string> map;
map[1] = "hello";
std::string* ptr = &map[1];  // 요소에 대한 포인터 획득

map[2] = "world";  // 삽입 (리해싱 발생 가능)
// ptr은 여전히 유효해야 함!
```

이 요구사항은 요소들이 재배치되지 않아야 함을 의미한다. Chaining은 각 요소를 개별적으로 할당하므로 이를 만족한다. 하지만 이것이 성능 문제의 근원이다.

### 메모리 레이아웃의 비효율

Chaining 방식의 메모리 레이아웃을 시각화해보자:

```text
Bucket Array:  [ptr0][ptr1][ptr2][ptr3][ptr4][ptr5]...
                 |     |     |     |     |     |
                 v     v     v     v     v     v
Heap:          Node  Node  Node  Node  Node  Node
              (메모리 전체에 무작위로 흩어짐)
```

버킷 배열은 연속적이지만, 실제 노드들은 힙 전체에 흩어져 있다. 조회 과정을 보면:

1. 해시 값 계산
2. 버킷 배열 접근 (메모리 로드 1)
3. 노드 포인터 역참조 (메모리 로드 2, 캐시 미스 가능)
4. 키 비교
5. 일치하지 않으면 next 포인터 역참조 (메모리 로드 3, 캐시 미스 가능)
6. 반복...

여러 노드를 따라가는 동안 각 load가 먼 cache 계층에서 해결되고 주소 의존성 때문에 겹치지 못하면 지연이 누적된다. 정확한 비용은 probe 길이뿐 아니라 allocator가 우연히 같은 페이지·cache line에 노드를 배치했는지에도 좌우된다.

### 캐시 라인의 낭비

CPU는 메모리를 64바이트 캐시 라인 단위로 가져온다. Chaining에서 노드를 로드하면:

```text
Cache Line (64 bytes):
[Node (24 bytes)][Unused (40 bytes)]
```

64바이트 중 24바이트만 사용하고 나머지는 버려진다. 인접한 노드가 같은 캐시 라인에 있을 확률은 매우 낮다.

반면 Open Addressing은:

```text
Cache Line (64 bytes):
[Slot1][Slot2][Slot3][Slot4]...
```

하나의 캐시 라인에 여러 슬롯이 들어가며, Linear Probing 시 이미 로드된 데이터를 재사용할 수 있다.

### 실측 성능 비교

간단한 벤치마크로 차이를 확인해보자:

```cpp
#include <chrono>
#include <unordered_map>
#include <random>

void benchmark_unordered_map() {
    std::unordered_map<int, int> map;
    std::mt19937 rng(42);

    // 삽입
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        map[rng()] = i;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    // 조회
    rng.seed(42);
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; i++) {
        volatile auto it = map.find(rng());
    }
    end = std::chrono::high_resolution_clock::now();
    auto lookup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    std::cout << "Insert: " << insert_ms << "ms, Lookup: "
              << lookup_ms << "ms\n";
}
```

이 코드만으로 컨테이너의 일반적인 순위를 결론 내릴 수 없다. 난수에서 중복 키가 나오면 실제 원소 수가 달라지고, 컨테이너마다 reserve/load factor 정책이 다르며, 조회 키의 성공·실패 비율도 결과를 바꾼다. 키를 미리 생성해 cardinality를 검증하고, 모든 구현에 같은 최대 load factor와 메모리 예산을 적용하며, successful/unsuccessful lookup을 분리한다. 최적화 제거에는 `volatile` iterator 대신 벤치마크 도구의 `DoNotOptimize`를 쓴다.

## 3. Open Addressing의 진화

Open Addressing이 캐시 효율적이라는 것은 알았지만, 탐색 전략에 따라 성능이 크게 달라진다.

### Linear Probing과 1차 클러스터링

Linear Probing은 가장 간단한 방법이다. 충돌 시 다음 슬롯을 순차적으로 확인한다.

$$h(k, i) = (h(k) + i) \mod m, \quad i = 0, 1, 2, \ldots$$

```cpp
size_t find_slot_linear(const K& key) {
    size_t index = hash(key) & (slots.size() - 1);

    while (slots[index].state != EMPTY) {
        if (slots[index].state == OCCUPIED &&
            slots[index].key == key) {
            return index;
        }
        index = (index + 1) & (slots.size() - 1);
    }
    return NOT_FOUND;
}
```

Linear Probing의 장점은 **최고의 캐시 지역성**이다. 연속된 메모리를 순차적으로 접근하므로, CPU의 하드웨어 프리페처가 다음 슬롯을 미리 로드한다.

하지만 **Primary Clustering (1차 클러스터링)** 문제가 있다. 연속된 점유 슬롯들이 뭉쳐서 긴 체인을 형성하는 현상이다.

```text
초기 상태: [A][ ][ ][ ][ ][ ][ ][ ]
B 삽입 (hash(B)=0, 충돌):
           [A][B][ ][ ][ ][ ][ ][ ]
C 삽입 (hash(C)=1, 충돌):
           [A][B][C][ ][ ][ ][ ][ ]
D 삽입 (hash(D)=0):
           [A][B][C][D][ ][ ][ ][ ]  <- 클러스터 성장
E 삽입 (hash(E)=3, 하지만 D가 점유):
           [A][B][C][D][E][ ][ ][ ]  <- 더 성장!
```

클러스터가 크면 클수록, 그 주변으로 해싱되는 키들이 클러스터를 더 키운다. 이는 성능을 급격히 저하시킨다.

### Quadratic Probing과 2차 클러스터링

Quadratic Probing은 탐색 간격을 2차 함수로 늘린다:

$$h(k, i) = (h(k) + c_1 i + c_2 i^2) \mod m$$

2의 거듭제곱 크기 테이블에서는 삼각수 $i(i+1)/2$를 더하는 방식이 자주 쓰인다. 이는 $c_1=c_2=1/2$인 식을 정수로 정확히 계산한 것이다.

```cpp
size_t find_slot_quadratic(const K& key) {
    size_t base = hash(key) & (slots.size() - 1);

    for (size_t i = 0; i < slots.size(); i++) {
        size_t offset = i * (i + 1) / 2;  // 0, 1, 3, 6, 10, ...
        size_t index = (base + offset) & (slots.size() - 1);

        if (slots[index].state == EMPTY) return index;
        if (slots[index].state == OCCUPIED &&
            slots[index].key == key) return index;
    }
    return NOT_FOUND;
}
```

탐색 패턴이 `0, 1, 3, 6, 10, 15,...` 이므로 1차 클러스터링은 피한다. 하지만 **Secondary Clustering (2차 클러스터링)** 이 발생한다. 동일한 초기 해시 값을 가진 키들이 동일한 탐색 순서를 따르기 때문이다.

주의: 순수 제곱 오프셋($c_1=0, c_2=1$)은 2의 거듭제곱 테이블에서 모든 슬롯을 방문하지 못한다. $m=8$이면 서로 다른 오프셋이 0, 1, 4 세 개뿐이다. 반면 위 삼각수 오프셋은 2의 거듭제곱 테이블의 모든 슬롯을 정확히 한 번씩 방문한다. 테이블 크기를 소수로 잡아도 전수 방문은 얻지 못하고, 적재율이 1/2 미만일 때 빈 슬롯을 반드시 찾는다는 보장만 얻는다.

### Double Hashing: 클러스터링 최소화

Double Hashing은 두 개의 독립적인 해시 함수를 사용한다:

$$h(k, i) = (h_1(k) + i \cdot h_2(k)) \mod m$$

여기서 $h_2(k)$ 는 0이 아니어야 하며, 일반적으로 홀수로 만든다.

```cpp
size_t hash1(const K& key) const {
    return fnv1a_hash(&key, sizeof(K));
}

size_t hash2(const K& key) const {
    uint32_t h = murmur3_hash(&key, sizeof(K), 0xDEADBEEF);
    // 0 방지, 홀수로 만들기
    return (h & (slots.size() - 1)) | 1;
}

size_t find_slot_double(const K& key) {
    size_t h1 = hash1(key) & (slots.size() - 1);
    size_t h2 = hash2(key);

    for (size_t i = 0; i < slots.size(); i++) {
        size_t index = (h1 + i * h2) & (slots.size() - 1);

        if (slots[index].state == EMPTY) return index;
        if (slots[index].state == OCCUPIED &&
            slots[index].key == key) return index;
    }
    return NOT_FOUND;
}
```

각 키가 고유한 탐색 순서를 가지므로 2차 클러스터링도 회피한다. 이론적으로 가장 좋은 분포를 제공한다.

하지만 두 해시 함수 계산 비용과 비순차적 메모리 접근으로 인해, 실전에서는 Linear Probing보다 느릴 수 있다. 현대 CPU의 프리페처가 순차 접근을 매우 효율적으로 처리하기 때문이다.

## 4. Robin Hood Hashing

Robin Hood Hashing은 Pedro Celis의 1986년 박사 학위 논문(University of Waterloo)에서 소개된 기법으로, "부자에게서 빼앗아 가난한 자에게 주는" Robin Hood의 철학을 해시 테이블에 적용했다. 이는 Open Addressing의 장점을 유지하면서 클러스터링 문제를 극적으로 개선한다.

### 핵심 아이디어: Probe Sequence Length

Robin Hood의 핵심은 각 요소가 "이상적 위치"에서 얼마나 떨어져 있는지를 추적하는 것이다. 이를 **Probe Sequence Length (PSL)** 이라 부른다.

$$\text{PSL} = \text{현재 인덱스} - \text{이상적 인덱스}$$

예를 들어, 키 K의 해시 값이 5인데 인덱스 8에 저장되어 있다면, PSL은 3이다.

```cpp
template<typename K, typename V>
class RobinHoodMap {
    struct Slot {
        K key;
        V value;
        uint8_t psl;  // Probe Sequence Length
        bool occupied;
    };

    std::vector<Slot> slots;
    size_t num_elements;

    size_t ideal_index(const K& key) const {
        return hash(key) & (slots.size() - 1);
    }
};
```

PSL을 `uint8_t`로 선택한 이유는 실전에서 PSL이 255를 초과하는 경우가 거의 없기 때문이다. 메모리 효율을 위한 선택이다.

### 삽입 알고리즘: "부자"를 밀어내기

삽입 시, 새 요소의 PSL이 기존 요소의 PSL보다 크면, 기존 요소를 "빼앗아" 밀어낸다. 밀려난 요소는 계속 삽입을 시도한다.

```cpp
void insert(const K& key, const V& value) {
    if (load_factor() > 0.9) {  // Robin Hood는 높은 로드 팩터 허용!
        rehash();
    }

    size_t ideal = ideal_index(key);
    size_t index = ideal;

    K current_key = key;
    V current_value = value;
    uint8_t current_psl = 0;

    while (true) {
        // 빈 슬롯 발견 - 삽입 완료
        if (!slots[index].occupied) {
            slots[index] = {current_key, current_value, current_psl, true};
            num_elements++;
            return;
        }

        // 같은 키면 값만 갱신한다.
        if (slots[index].key == current_key) {
            slots[index].value = current_value;  // Update
            return;
        }

        // Robin Hood 교환: 현재 요소가 "더 가난함" (PSL이 더 큼)
        if (current_psl > slots[index].psl) {
            std::swap(current_key, slots[index].key);
            std::swap(current_value, slots[index].value);
            std::swap(current_psl, slots[index].psl);
        }

        // 다음 슬롯으로 이동
        index = (index + 1) & (slots.size() - 1);
        current_psl++;
    }
}
```

이 알고리즘의 아름다움은 단순함에 있다. 각 요소가 자신의 "불행 수준"(PSL)을 가지고, 더 불행한 자에게 자리를 양보한다.

시각적 예제로 이해해보자:

```text
초기 상태:
Index: 0   1   2   3   4   5
      [A] [B] [C] [ ] [ ] [ ]
Hash:  0   1   2
PSL:   0   0   0

D 삽입 (hash=0):
- index 0의 A와 만날 때 D의 PSL은 0이므로 계속 진행
- index 1에서 D의 PSL 1 > B의 PSL 0이므로 D와 B를 교환
- 밀려난 B는 index 2에서 PSL 1 > C의 PSL 0이므로 다시 교환
- 밀려난 C는 빈 index 3에 PSL 1로 삽입

결과:
Index: 0   1   2   3   4   5
      [A] [D] [B] [C] [ ] [ ]
PSL:   0   1   1   1
```

현재 삽입 중인 원소의 PSL이 더 클 때만 자리를 빼앗는다. 즉 이상적 위치에 가까운 기존 원소를 더 멀리 보내고, 이미 멀리 탐색한 원소를 그 자리에 둬 probe distance를 균등화한다. 반대로 새 원소의 PSL이 더 작으면 기존 원소를 밀어내지 않는다.

### PSL 분산의 극적 감소

Robin Hood의 진정한 힘은 PSL의 **분산**을 극적으로 줄인다는 점이다.

중요한 사실 하나를 먼저 짚는다. Linear probing에서 어떤 슬롯들이 점유되는지는 삽입 순서와 무관하게 결정되므로, **PSL의 총합(따라서 평균)은 Robin Hood를 써도 변하지 않는다**. Robin Hood가 하는 일은 그 총합을 요소들 사이에 고르게 재분배하는 것이다. 운 나쁜 소수가 매우 긴 PSL을 갖는 대신, 모두가 비슷한 PSL을 갖는다.

무작위 해싱과 고정된 load factor 같은 분석 가정 아래 Robin Hood hashing의 **최대 probe distance**는 매우 천천히, 전형적으로 $\Theta(\log\log n)$ 규모로 증가하는 것으로 분석된다. 이를 PSL의 분산 자체가 $O(\log\log n)$이라고 바꿔 말하면 서로 다른 통계량을 혼동하게 된다.

실전 예제 (같은 키 집합, 같은 점유 슬롯):

```text
Linear Probing (삽입 순서에 따라):
Index: 0   1   2   3   4   5
      [A] [B] [C] [D] [E] [ ]
PSL:   0   0   2   3   0
최대 PSL: 3 (C, D가 운 나쁘게 멀리 밀림)

Robin Hood (같은 키들):
Index: 0   1   2   3   4   5
      [A] [C] [D] [B] [E] [ ]
PSL:   0   1   2   2   0
최대 PSL: 2 (불행이 고르게 분배됨; 총합 5는 동일)
```

이는 조회 성능에 직접적인 영향을 준다. 최대 PSL이 작다는 것은, **조회 실패를 빠르게 판단**할 수 있다는 의미이다.

```cpp
V* find(const K& key) {
    size_t ideal = ideal_index(key);
    size_t index = ideal;
    uint8_t psl = 0;

    while (slots[index].occupied) {
        // 조기 종료 조건!
        if (psl > slots[index].psl) {
            return nullptr;  // 키가 존재하지 않음
        }

        if (slots[index].key == key) {
            return &slots[index].value;
        }

        index = (index + 1) & (slots.size() - 1);
        psl++;
    }
    return nullptr;
}
```

만약 현재 탐색 중인 PSL이 슬롯의 PSL보다 크다면, 키가 존재했다면 이미 그 위치에 있어야 했을 것이다. 따라서 즉시 탐색을 중단할 수 있다.

높은 load factor에서 조기 종료는 실패 조회의 긴 꼬리를 줄일 수 있다. 평균 probe 수는 hash 품질, 테이블 크기, 삽입·삭제 이력과 구현 세부에 따라 달라지므로 고정된 "50회 대 3~4회" 수치로 일반화하지 않고 동일한 키 trace로 측정한다.

### Backward Shift Deletion: 톰스톤의 종말

전통적인 Open Addressing의 큰 문제는 삭제이다. 단순히 슬롯을 비우면 탐색 체인이 끊어진다.

```cpp
// 잘못된 삭제
void bad_delete(const K& key) {
    size_t index = find_slot(key);
    slots[index].occupied = false;  // 체인이 끊어짐!
}

// 탐색이 실패함
// A → B → [삭제된 C] → D
// D를 찾으려 할 때, [삭제된 C]에서 탐색이 중단됨
```

전통적 해결책은 "톰스톤(tombstone)" 상태를 사용하는 것이다:

```cpp
enum State { EMPTY, OCCUPIED, DELETED };

void delete_with_tombstone(const K& key) {
    size_t index = find_slot(key);
    slots[index].state = DELETED;  // 톰스톤 마킹
}

// 조회 시 톰스톤은 건너뜀
while (slots[index].state != EMPTY) {
    if (slots[index].state == OCCUPIED &&
        slots[index].key == key) {
        return &slots[index].value;
    }
    index = (index + 1) & (slots.size() - 1);
}
```

하지만 톰스톤은 시간이 지나면서 축적되어 성능을 저하시킨다. 탐색 시 톰스톤도 방문해야 하기 때문이다.

Robin Hood는 **Backward Shift Deletion**이라는 우아한 해결책을 제공한다:

```cpp
void delete_robinhood(const K& key) {
    size_t index = find_slot(key);
    if (index == NOT_FOUND) return;

    slots[index].occupied = false;  // 슬롯 비우기

    // 뒤쪽 요소들을 앞으로 이동
    size_t curr = index;
    size_t next = (curr + 1) & (slots.size() - 1);

    while (slots[next].occupied && slots[next].psl > 0) {
        // 다음 요소를 현재 위치로 이동
        slots[curr] = slots[next];
        slots[curr].psl--;  // PSL 감소 (1칸 앞으로 왔으므로)

        slots[next].occupied = false;
        curr = next;
        next = (next + 1) & (slots.size() - 1);
    }

    num_elements--;
}
```

이 알고리즘은 삭제된 슬롯 뒤의 요소들을 앞으로 당긴다. 단, PSL이 0인 요소(이상적 위치에 있는 요소)를 만나면 중단한다. 그 요소는 이동할 필요가 없기 때문이다.

시각적 예제:

```text
삭제 전: B 삭제
Index: 0   1   2   3   4
      [A] [B] [C] [D] [ ]
PSL:   0   0   1   2

Step 1: B 제거
      [A] [ ] [C] [D] [ ]
PSL:   0       1   2

Step 2: C를 index 1로 이동 (PSL 1→0)
      [A] [C] [ ] [D] [ ]
PSL:   0   0       2

Step 3: D를 index 2로 이동 (PSL 2→1)
      [A] [C] [D] [ ] [ ]
PSL:   0   0   1

완료: 깔끔한 상태, 톰스톤 없음
```

Backward Shift의 장점:
- **톰스톤 불필요**: 테이블이 깨끗하게 유지됨
- **성능 저하 없음**: 삭제가 장기적 영향을 주지 않음
- **메모리 효율**: State 필드가 단순 bool로 충분

### 비교 벤치마크의 설계

Robin Hood hashing은 probe distance의 분산을 줄이고 stored distance를 이용한 unsuccessful lookup 조기 종료를 가능하게 한다. 대신 삽입 중 swap과 backward-shift 삭제가 더 많은 슬롯 쓰기를 만들 수 있으므로 linear probing보다 삽입·삭제가 항상 빠른 것은 아니다.

| 축 | 고정하거나 기록할 조건 |
|---|---|
| 키 | cardinality, 크기, hash 계산 비용, 분포와 adversarial collision |
| 테이블 | 실제 원소 수, capacity, load factor, 성장 횟수 |
| 질의 | 성공/실패 비율, hot/cold key, 질의 순서 |
| 삭제 | tombstone 정책, backward shift 거리, erase 후 steady state |
| 메모리 | bucket/slot, node allocation, allocator metadata, peak RSS |
| 하드웨어 | CPU·캐시, 컴파일러·옵션, 컨테이너 구현 버전 |

평균 시간만 보지 않고 probe length와 지연의 p50/p95/p99를 함께 기록한다. 게임의 리소스 조회처럼 실패 검색이 많다면 그 비율로 trace를 만들고, 삽입·삭제가 섞이는 런타임 맵이라면 steady-state churn을 재현한다.

## 5. 게임 엔진의 해시 맵 구현

이론을 넘어, 실제 게임 엔진들은 어떻게 해시 맵을 구현했을까? 각 엔진의 특수한 요구사항이 설계에 어떻게 반영되었는지 살펴보자.

### Unreal Engine TMap: 인덱스 기반 분리 연쇄

Unreal Engine의 `TMap`은 버킷이 원소 인덱스를 가리키고, 같은 버킷의 원소를 `HashNext` 인덱스로 잇는 **분리 연쇄(separate chaining)** 계열이다. 노드를 개별 할당하는 전통적인 체이닝과 달리 원소 저장소로 `TSparseArray`를 사용하지만, 충돌 해결 방식 자체가 open addressing과 혼합되는 것은 아니다.

```cpp
template<typename KeyType, typename ValueType>
class TMap {
    // 해시 테이블: 인덱스만 저장
    TArray<int32> Hash;

    // 실제 데이터: Sparse Array
    TSparseArray<TMapElement> Elements;

    struct TMapElement {
        KeyType Key;
        ValueType Value;
        int32 HashNext;  // 같은 버킷의 다음 요소 인덱스
    };
};
```

아키텍처를 시각화하면:

```text
Hash Array:     [2] [-1] [0] [-1] [1] ...
                 |        |        |
                 v        v        v
Elements:   [0: KeyA, ValA, next=4]
            [1: KeyB, ValB, next=-1]
            [2: KeyC, ValC, next=3]
            [3: KeyD, ValD, next=-1]
            [4: KeyE, ValE, next=-1]
```

Hash 배열은 각 버킷의 첫 번째 요소 인덱스를 저장한다. Elements 배열은 실제 키-값 쌍을 저장하며, HashNext는 같은 버킷의 다음 요소를 가리킨다.

`TSparseArray`는 Unreal의 특별한 컨테이너로, 중간에 "구멍"이 있을 수 있는 배열이다:

```cpp
template<typename ElementType>
class TSparseArray {
    TArray<ElementType> Data;
    TBitArray<> AllocationFlags;  // 각 슬롯의 점유 여부
    int32 FirstFreeIndex;         // 재사용을 위한 빈 슬롯 추적
};
```

삭제 시 구멍이 생기며 다음 삽입이 그 슬롯을 재사용할 수 있다. 이 구조는 빈 슬롯을 효율적으로 재사용하게 하지만, 컨테이너 변경 전반에 걸친 인덱스·주소 안정성을 보장하지는 않는다. 저장소 성장, compact 계열 연산, 제거와 재삽입은 기존 원소의 주소나 인덱스를 무효화할 수 있다.

**TMap의 장점**:

1. **Unreal 에코시스템**: allocator, reflection 주변 코드와 엔진의 직렬화·디버깅 도구에 맞는 컨테이너 계약을 제공한다.

2. **할당 제어**: 충돌 원소마다 일반 힙 노드를 하나씩 할당하는 구현보다 allocator 호출과 노드 메타데이터를 줄일 여지가 있다.

3. **삭제 슬롯 재사용**: sparse storage의 빈 슬롯을 후속 삽입에 재사용할 수 있다.

**단점**:

1. **간접 참조**: Hash → Elements로 여전히 2단계 접근이 필요하다.
2. **복잡성**: TSparseArray의 관리 오버헤드가 있다.
3. **캐시 효율**: probe 배열에 키와 값을 밀집시키는 open addressing보다 간접 접근과 sparse slot 비용이 커질 수 있다.

`Find`가 돌려준 포인터나 참조는 필요한 짧은 범위에서만 사용하고, `Add`, `Remove`, `Compact`, `Shrink` 같은 변경을 가로질러 보관하지 않는다. 장기 식별자가 필요하면 키나 별도의 generation handle을 저장한 뒤 다시 조회해야 한다. 이것이 컨테이너 구현의 우연한 현재 동작과 프로그램의 수명 계약을 분리하는 방법이다.

### EASTL hash_map: 게임을 위한 최적화

Electronic Arts의 EASTL (EA Standard Template Library)은 게임 개발을 위해 설계된 STL 대체이다. `hash_map`은 (std와 마찬가지로) **분리 연쇄(separate chaining)** 기반의 노드 테이블이며, Robin Hood/오픈 어드레싱이 아니라 **게임 친화적 메모리 제어·할당자 커스터마이즈·디버깅 기능**에 차별점을 둔다.

```cpp
template<typename Key, typename T, typename Hash = hash<Key>,
         typename Predicate = equal_to<Key>,
         typename Allocator = EASTLAllocatorType>
class hash_map {
    struct node_type {
        eastl::pair<const Key, T> mValue;
        node_type* mpNext;   // 같은 버킷의 다음 노드 (chaining)
    };

    node_type** mpBucketArray;  // 버킷별 노드 리스트 헤드
    size_type   mnBucketCount;
    size_type   mnElementCount;
    allocator_type mAllocator;  // 커스텀 할당자!
};
```

EASTL의 핵심 차별점은 **게임 특화 기능들**이다:

**1. 커스텀 할당자 지원**:

게임 엔진은 메모리를 매우 세밀하게 관리한다. 스택 기반 할당자, 풀 할당자, 프레임 할당자 등을 사용한다.

```cpp
// 게임별 메모리 풀 사용
hash_map<int, Texture*, hash<int>, equal_to<int>,
         GameMemoryAllocator> TextureCache;

// 프레임 단위 할당자 (매 프레임 리셋)
hash_map<EntityID, TempData*, hash<EntityID>, equal_to<EntityID>,
         FrameAllocator> FrameTempData;
```

**2. 고정 크기 최적화 (fixed_hash_map)**:

작은 해시맵을 위해 node와 bucket storage를 컨테이너 내부에 두는 fixed container를 제공한다. 핵심 계약은 다음과 같다.

```text
nodeCount = 16
bEnableOverflow = true
→ 처음 16개 node는 inline pool 사용
→ pool 고갈 뒤에는 지정한 overflow allocator 사용 가능

bEnableOverflow = false
→ inline pool을 넘는 삽입은 허용되지 않음
```

고정 buffer 안에 머무르는 동안 일반 heap allocation을 피할 수 있다. 정확한 template parameter 순서와 failure/assert 정책은 사용하는 EASTL version의 `fixed_hash_map.h`를 따른다. 큰 fixed container를 함수 stack에 두면 그 inline storage 전체가 stack budget을 소비하므로 “fixed”와 “stack에 두어도 안전함”은 같은 뜻이 아니다.

**3. 명시적 제어**:

```cpp
TextureCache.rehash(1024);  // 미리 예약, 리해싱 방지
TextureCache.max_load_factor(0.85f);  // 로드 팩터 조정
TextureCache.reserve(500);  // 최소 500개 수용 가능하도록
```

**4. 최적화된 해시 함수**:

EASTL은 타입별로 특화된 해시를 제공한다:

```cpp
// 벡터 타입에 대한 특화
template<> struct hash<Vector3> {
    size_t operator()(const Vector3& v) const {
        // 빠른 해시 결합
        size_t h = hash<float>{}(v.x);
        h ^= hash<float>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= hash<float>{}(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
```

이 해시 결합 기법은 각 컴포넌트의 해시를 효율적으로 섞는다. 매직 넘버 `0x9e3779b9`는 황금비의 역수를 32비트로 표현한 값으로, 좋은 분포를 만든다.

**게임 사용 사례**:

```cpp
// 엔티티 매니저
hash_map<EntityID, Entity*, hash<EntityID>, equal_to<EntityID>,
         EntityAllocator> EntityMap;

// 텍스처 캐시 (에셋 ID → 텍스처)
hash_map<uint32, Texture*> TextureCache;

// 문자열 인터닝 (다음 섹션에서 자세히)
hash_map<const char*, StringID, hash<const char*>,
         string_equal, StringPoolAllocator> InternTable;
```

### Abseil flat_hash_map: SIMD의 힘

Google의 Abseil 라이브러리는 Swiss Table이라는 혁신적 설계를 도입했다. 이는 SIMD (Single Instruction Multiple Data) 명령어를 활용한 해시맵이다.

**Swiss Table의 핵심: 제어 바이트 배열**

```cpp
template<typename K, typename V>
class flat_hash_map {
    int8_t* ctrl_;   // 제어 바이트 배열
    slot_type* slots_;  // 실제 슬롯 배열
    size_t size_;
    size_t capacity_;
};
```

각 슬롯마다 1바이트의 "제어 바이트"가 있다:

```cpp
enum ControlByte : int8_t {
    kEmpty = -128,    // 10000000: 빈 슬롯
    kDeleted = -2,    // 11111110: 삭제된 슬롯
    kSentinel = -1,   // 11111111: 제어 바이트 배열의 끝(반복자 종료 표식)
    // 0~127: H2(hash) - 해시의 하위 7비트 fingerprint
};
```

해시 값을 두 부분으로 나눈다:
- **H1**: H2를 제외한 상위 57비트, 시작 probe 위치 계산에 사용
- **H2**: 하위 7비트, 제어 바이트 fingerprint로 저장

**SIMD 기반 검색**:

제어 바이트를 16개씩 묶어서 "그룹"으로 처리하고, SSE2 명령어로 한 번에 검색한다:

```cpp
struct Group {
    __m128i ctrl;  // 16개의 제어 바이트 (128비트)

    // h2와 일치하는 슬롯 찾기
    BitMask Match(int8_t h2) const {
        __m128i match_value = _mm_set1_epi8(h2);  // h2를 16번 복제
        __m128i cmp = _mm_cmpeq_epi8(ctrl, match_value);  // 16개 동시 비교
        return BitMask(_mm_movemask_epi8(cmp));  // 결과를 16비트 마스크로
    }

    // 빈 슬롯 찾기
    BitMask MatchEmpty() const {
        __m128i empty_value = _mm_set1_epi8(kEmpty);
        __m128i cmp = _mm_cmpeq_epi8(ctrl, empty_value);
        return BitMask(_mm_movemask_epi8(cmp));
    }
};
```

`_mm_cmpeq_epi8`은 16개 바이트를 동시에 비교하는 SIMD 명령어이다. 일반 루프로 16번 비교하는 것보다 훨씬 빠르다.

**조회 프로세스**:

```cpp
V* find(const K& key) {
    size_t hash = Hash{}(key);
    size_t h1 = H1(hash);
    int8_t h2 = H2(hash);  // 하위 7비트

    size_t group_index = h1 & (num_groups_ - 1);

    while (true) {
        Group g = GetGroup(group_index);

        // SIMD: 16개 슬롯을 한 번에 검색
        BitMask matches = g.Match(h2);

        // 일치하는 슬롯들 확인
        while (matches.Any()) {
            size_t offset = matches.LowestBitSet();
            size_t slot = group_index * 16 + offset;

            // 실제 키 비교 (false positive 제거)
            if (slots_[slot].key == key) {
                return &slots_[slot].value;
            }

            matches.ClearLowestBit();
        }

        // 빈 슬롯 발견 = 키 없음
        if (g.MatchEmpty().Any()) {
            return nullptr;
        }

        // 다음 그룹으로
        group_index = (group_index + 1) & (num_groups_ - 1);
    }
}
```

프로세스 분석:
1. H2로 16개 슬롯을 동시에 필터링 (SIMD)
2. 일치하는 슬롯만 실제 키 비교 (false positive 제거)
3. 빈 슬롯을 만나면 즉시 종료

**메모리 레이아웃**:

```text
Control Bytes:  [h2][h2][E ][h2][D ][h2]...[h2][E ]
                  |   |       |       |       |
                  v   v       v       v       v
Slots:          [K,V][K,V][ ][K,V][ ][K,V]...[K,V][ ]

E = Empty, D = Deleted
h2 = 해시에서 분리한 7비트 fingerprint (Abseil 구현에서는 하위 7비트)
```

제어 바이트가 연속되어 있어, 하나의 캐시 라인(64바이트)에 제어 바이트 64개, 즉 16바이트 그룹 4개가 들어간다. 슬롯 본체를 건드리기 전에 캐시 라인 하나로 64개 후보를 걸러낼 수 있다는 뜻이다.

**성능 이점**:

1. **SIMD 병렬성**: 16개 슬롯을 동시에 검사
2. **캐시 효율**: 제어 바이트가 밀집되어 있음
3. **빠른 필터링**: H2로 대부분의 비교를 회피
4. **높은 로드 팩터**: 0.875 (7/8)까지 효율적

그룹 제어 바이트는 하나의 word 또는 SIMD 연산으로 여러 fingerprint 후보를 만든다. 실제 조회 시간은 키·값 크기, 성공/실패 비율, hash 품질과 load factor에 따라 달라지므로 컨테이너 이름만으로 고정 순위를 만들 수 없다. 트레이드오프는 다음과 같다.

- **그룹 연산 의존성**: 플랫폼별 SIMD 또는 portable group 연산 경로가 필요
- **복잡한 구현**: 디버깅과 유지보수가 어려움
- **삭제 오버헤드**: 여전히 톰스톤 사용 (Backward shift 아님)

## 6. String Interning

String Interning은 동일한 문자열을 메모리에 단 한 번만 저장하는 기법이다. 게임 엔진에서 매우 자주 사용되며, 메모리 절감뿐만 아니라 비교 성능을 극적으로 개선한다.

### 문제 상황: 문자열의 중복

게임 엔진을 생각해보자. 수천 개의 엔티티가 "Enemy", "Player", "Projectile" 같은 태그를 가진다:

```cpp
class Entity {
    std::string tag;
    // ...
};

std::vector<Entity> entities;
for (int i = 0; i < 10000; i++) {
    Entity enemy;
    enemy.tag = "Enemy";  // 태그마다 std::string 객체 하나
    entities.push_back(enemy);
}
```

"Enemy" 문자열이 10,000번 복제된다. 5자라 주요 구현에서는 SSO에 들어가 별도 힙 블록이 생기지는 않지만, std::string 객체의 오버헤드(보통 24-32바이트)가 10,000번 그대로 쌓인다. SSO 한도를 넘는 태그라면 여기에 힙 할당까지 더해진다.

더 심각한 문제는 비교이다:

```cpp
for (const auto& entity : entities) {
    if (entity.tag == "Enemy") {  // O(n) 문자열 비교!
        // ...
    }
}
```

`operator==`는 모든 문자를 비교한다. "Enemy"는 5글자이므로 최악의 경우 5번 비교이다. 짧아 보이지만, 이것이 10,000번 반복되면 50,000번의 문자 비교이다.

### String Pool: 중복 제거

String Interning의 핵심은 동일한 문자열을 단 한 번만 저장하고, 포인터로 공유하는 것이다:

```cpp
class StringPool {
    // 문자열 → 저장된 포인터
    absl::flat_hash_map<std::string_view, const char*> interned;

    // 실제 문자열 저장소 (소유권 관리)
    std::vector<std::unique_ptr<char[]>> storage;

public:
    const char* intern(std::string_view str) {
        // 이미 인터닝되었는지 확인
        auto it = interned.find(str);
        if (it != interned.end()) {
            return it->second;  // 기존 포인터 반환
        }

        // 새 문자열 저장
        auto buffer = std::make_unique<char[]>(str.size() + 1);
        std::memcpy(buffer.get(), str.data(), str.size());
        buffer[str.size()] = '\0';

        const char* ptr = buffer.get();
        storage.push_back(std::move(buffer));
        interned[std::string_view(ptr, str.size())] = ptr;

        return ptr;
    }
};
```

사용 예제:

```cpp
StringPool pool;

class Entity {
    const char* tag;  // 인터닝된 문자열 포인터
public:
    void set_tag(std::string_view tag_str) {
        tag = pool.intern(tag_str);
    }
};

// 모든 "Enemy" 엔티티가 같은 포인터를 공유
Entity e1, e2;
e1.set_tag("Enemy");
e2.set_tag("Enemy");

assert(e1.tag == e2.tag);  // 같은 포인터!
```

메모리 사용량은 `std::string` ABI, SSO 용량, allocator와 hash table의 load factor에 따라 달라진다. 다음은 무엇을 세어야 하는지 보여 주는 하한 계산이다.

```text
짧은 "Enemy":
- 주요 std::string 구현에서는 SSO에 들어가 별도 6바이트 heap allocation이 없다.
- 10,000개 문자열 객체: sizeof(std::string) × 10,000 (흔히 24B 또는 32B)

인터닝 사용:
- 엔티티의 포인터: sizeof(const char*) × 10,000
- 고유 문자열 1개 + hash slot/control byte + pool allocator 오버헤드
```

긴 문자열은 중복 heap buffer를 없애는 효과까지 더해진다:

```text
경로: "Assets/Textures/Characters/Enemy/Diffuse_2K.dds" (47 chars)

24B string object를 가정한 최소 payload 비교:
- 인터닝 없음: 10,000 × (24 + 최소 48) = 약 720,000 bytes
- 인터닝 사용: 10,000 × 8 + 고유 buffer 하나 + table/pool 오버헤드
```

실제 resident bytes에는 allocation rounding과 allocator metadata가 포함된다. 반대로 hash table의 빈 slot과 pool chunk도 포함해야 하므로 위 산술만으로 절감률을 고정하지 않고 heap profiler로 측정한다.

### 비교의 혁명: O(n) → O(1)

진정한 힘은 비교 성능에 있다:

```cpp
// 인터닝 없음: 문자열 비교는 O(n)
bool compare_slow(const std::string& a, const std::string& b) {
    return a == b;  // 각 문자 비교
}

// 같은 pool에서 canonicalize된 포인터끼리만 O(1) 비교 가능
bool compare_fast(const char* a, const char* b) {
    return a == b;  // 단일 정수 비교!
}
```

비교 비용의 차이는 다음과 같이 확인할 수 있다.

```cpp
std::string str1 = "LongAssetPathThatNeedsComparison";
std::string str2 = "LongAssetPathThatNeedsComparison";

// std::string 비교
auto start = std::chrono::high_resolution_clock::now();
for (int i = 0; i < 1000000; i++) {
    volatile bool result = (str1 == str2);
}
auto duration1 = std::chrono::high_resolution_clock::now() - start;

// 인터닝된 포인터 비교
const char* intern1 = pool.intern(str1);
const char* intern2 = pool.intern(str2);

start = std::chrono::high_resolution_clock::now();
for (int i = 0; i < 1000000; i++) {
    volatile bool result = (intern1 == intern2);
}
auto duration2 = std::chrono::high_resolution_clock::now() - start;
```

이미 intern된 두 문자열의 동일성 검사는 포인터 비교 하나지만, 전체 비용에는 문자열 hash·lookup·할당을 수행하는 intern 과정과 pool 수명 관리가 포함된다. 위 루프는 같은 두 값만 반복해 분기 예측과 캐시에 완전히 유리하므로 실제 리소스 trace를 대표하지 않는다. 길이·공통 접두부가 다른 문자열 분포, intern hit rate, 멀티스레드 경합을 포함해 end-to-end로 비교한다.

### 게임 엔진의 활용: FName과 StringID

**Unreal Engine의 FName**:

Unreal은 FName이라는 인터닝 시스템을 사용한다:

```cpp
class FName {
    int32 ComparisonIndex;  // 전역 인터닝 테이블의 인덱스
    int32 Number;           // "Actor_1", "Actor_2" 등의 번호

public:
    FName(const char* str);  // 생성 시 자동 인터닝

    bool operator==(const FName& Other) const {
        // O(1) 정수 비교!
        return ComparisonIndex == Other.ComparisonIndex &&
               Number == Other.Number;
    }

    bool operator<(const FName& Other) const {
        // 정렬도 O(1)
        return ComparisonIndex < Other.ComparisonIndex ||
               (ComparisonIndex == Other.ComparisonIndex &&
                Number < Other.Number);
    }
};

// 사용
FName PlayerTag("Player");
FName EnemyTag("Enemy");

if (entity.Tag == PlayerTag) {  // 두 개의 정수 비교만
    // ...
}
```

FName은 태그, 컴포넌트 이름, 프로퍼티 이름 등 Unreal 전체에서 사용된다. 문자열이지만 정수처럼 빠르다.

**StringID 패턴 - 컴파일 타임 해싱**:

더 나아가, 컴파일 타임에 해싱하는 방법도 있다:

```cpp
class StringID {
    uint32_t hash;

    // 컴파일 타임 해시 함수
    static constexpr uint32_t fnv1a(const char* str) {
        uint32_t hash = 2166136261u;
        while (*str) {
            hash ^= static_cast<uint8_t>(*str++);
            hash *= 16777619u;
        }
        return hash;
    }

public:
    constexpr StringID(const char* str) : hash(fnv1a(str)) {}

    constexpr bool operator==(StringID other) const {
        return hash == other.hash;
    }

    constexpr uint32_t value() const { return hash; }
};

// 컴파일 타임에 해싱됨!
constexpr StringID PlayerID("Player");
constexpr StringID EnemyID("Enemy");

// 런타임에는 정수 비교만
if (entity.ID == PlayerID) {
    // hash는 이미 컴파일 시 계산됨
}
```

`constexpr`로 인해 "Player"의 해시 값이 컴파일 시간에 계산되어, 바이너리에 상수로 들어간다. 런타임 해싱 비용이 전혀 없다!

### 스레드 안전한 구현

멀티스레딩 환경에서는 동기화가 필요하다:

```cpp
class ThreadSafeStringPool {
    mutable std::shared_mutex mutex;
    absl::flat_hash_map<std::string_view, const char*> interned;
    std::vector<std::unique_ptr<char[]>> storage;

public:
    const char* intern(std::string_view str) {
        // 먼저 읽기 락으로 확인 (다중 리더 허용)
        {
            std::shared_lock lock(mutex);
            auto it = interned.find(str);
            if (it != interned.end()) {
                return it->second;
            }
        }

        // 없으면 쓰기 락으로 삽입 (단독 접근)
        std::unique_lock lock(mutex);

        // Double-checked locking
        // (다른 스레드가 이미 삽입했을 수 있음)
        auto it = interned.find(str);
        if (it != interned.end()) {
            return it->second;
        }

        // 실제 삽입
        auto buffer = std::make_unique<char[]>(str.size() + 1);
        std::memcpy(buffer.get(), str.data(), str.size());
        buffer[str.size()] = '\0';

        const char* ptr = buffer.get();
        storage.push_back(std::move(buffer));
        interned[std::string_view(ptr, str.size())] = ptr;

        return ptr;
    }
};
```

`std::shared_mutex`는 읽기-쓰기 락이다. 다중 리더(shared_lock)를 허용하므로, 조회가 빈번한 워크로드에서 경합을 줄인다.

Double-checked locking은 쓰기 락을 획득한 후 다시 확인하는 패턴이다. 두 스레드가 거의 동시에 같은 문자열을 인터닝하려 할 때, 하나는 읽기 락 단계에서 통과하지만 쓰기 락 대기 중에 다른 스레드가 먼저 삽입할 수 있기 때문이다.

### 실전 활용: 리소스 매니저

String Interning은 반복되는 정규화 경로가 많고 interned 문자열의 수명과 메모리 상한을 관리할 수 있는 리소스 매니저에 유용하다.

```cpp
class ResourceManager {
    struct PointerHash {
        size_t operator()(const char* ptr) const {
            return std::hash<const void*>{}(ptr);
        }
    };

    struct PointerEqual {
        bool operator()(const char* a, const char* b) const {
            return a == b;  // 포인터 비교만
        }
    };

    ThreadSafeStringPool path_pool;

    // 경로 포인터 → 리소스 (포인터 비교!)
    absl::flat_hash_map<const char*, Texture*,
                        PointerHash, PointerEqual> textures;

public:
    Texture* LoadTexture(std::string_view path) {
        const char* interned_path = path_pool.intern(path);

        // 빠른 포인터 기반 조회
        auto it = textures.find(interned_path);
        if (it != textures.end()) {
            return it->second;  // 이미 로드됨
        }

        // 로드 및 캐싱
        Texture* texture = new Texture(interned_path);
        textures[interned_path] = texture;
        return texture;
    }
};
```

이제 경로 비교가 단순 포인터 비교이다. 경로가 아무리 길어도 O(1)이다.

## 7. 구현 가이드: 실전 Robin Hood Hash Map

이론을 실천으로 옮길 시간이다. 실제 사용 가능한 Robin Hood Hash Map을 구현해보자.

### 기본 구조

```cpp
template<typename K, typename V, typename Hash = std::hash<K>,
         typename Psl = uint8_t>
class RobinHoodMap {
public:
    struct Slot {
        K key;
        V value;
        Psl psl;  // Probe Sequence Length
        bool occupied;
    };

private:
    std::vector<Slot> slots_;
    size_t num_elements_ = 0;
    float max_load_factor_ = 0.9f;
    Hash hasher_;

    size_t ideal_index(const K& key) const {
        return hasher_(key) & (slots_.size() - 1);
    }

    void rehash() {
        size_t new_capacity = slots_.size() * 2;
        if (new_capacity == 0) new_capacity = 16;

        std::vector<Slot> old_slots = std::move(slots_);
        slots_.resize(new_capacity);
        num_elements_ = 0;

        for (const auto& slot : old_slots) {
            if (slot.occupied) {
                insert(slot.key, slot.value);
            }
        }
    }

public:
    RobinHoodMap() {
        slots_.resize(16);  // 초기 크기
    }

    void insert(const K& key, const V& value) {
        if (load_factor() > max_load_factor_) {
            rehash();
        }

        size_t index = ideal_index(key);
        K current_key = key;
        V current_value = value;
        Psl current_psl = 0;

        while (true) {
            if (!slots_[index].occupied) {
                slots_[index] = {current_key, current_value,
                                 current_psl, true};
                num_elements_++;
                return;
            }

            if (slots_[index].key == current_key) {
                slots_[index].value = current_value;  // Update
                return;
            }

            // Robin Hood 교환
            if (current_psl > slots_[index].psl) {
                std::swap(current_key, slots_[index].key);
                std::swap(current_value, slots_[index].value);
                std::swap(current_psl, slots_[index].psl);
            }

            index = (index + 1) & (slots_.size() - 1);
            current_psl++;
        }
    }

    V* find(const K& key) {
        size_t index = ideal_index(key);
        Psl psl = 0;

        while (slots_[index].occupied) {
            if (psl > slots_[index].psl) {
                return nullptr;  // 조기 종료
            }

            if (slots_[index].key == key) {
                return &slots_[index].value;
            }

            index = (index + 1) & (slots_.size() - 1);
            psl++;
        }

        return nullptr;
    }

    void erase(const K& key) {
        size_t index = ideal_index(key);
        Psl psl = 0;

        // 키 찾기
        while (slots_[index].occupied) {
            if (psl > slots_[index].psl) {
                return;  // 키 없음
            }

            if (slots_[index].key == key) {
                break;
            }

            index = (index + 1) & (slots_.size() - 1);
            psl++;
        }

        if (!slots_[index].occupied) return;

        // Backward shift deletion
        slots_[index].occupied = false;
        num_elements_--;

        size_t curr = index;
        size_t next = (curr + 1) & (slots_.size() - 1);

        while (slots_[next].occupied && slots_[next].psl > 0) {
            slots_[curr] = slots_[next];
            slots_[curr].psl--;
            slots_[next].occupied = false;

            curr = next;
            next = (next + 1) & (slots_.size() - 1);
        }
    }

    float load_factor() const {
        return static_cast<float>(num_elements_) / slots_.size();
    }

    size_t size() const { return num_elements_; }
    bool empty() const { return num_elements_ == 0; }
};
```

### 최적화 포인트

**1. 2의 거듭제곱 크기 강제**:

```cpp
size_t next_power_of_two(size_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;  // 64비트
    n++;
    return n;
}

void rehash_to(size_t min_capacity) {
    size_t new_capacity = next_power_of_two(min_capacity);
    // ...
}
```

이는 비트 조작으로 다음 2의 거듭제곱을 찾는다. `&` 연산으로 모듈로를 대체할 수 있게 한다.

**2. PSL 타입 선택**:

대부분의 경우 `uint8_t` (최대 PSL 255)로 충분하지만, 매우 높은 로드 팩터나 나쁜 해시 함수를 사용한다면 `uint16_t`를 고려할 수 있다.

```cpp
// 작은 맵: uint8_t
template<typename K, typename V>
using SmallRobinHoodMap = RobinHoodMap<K, V, std::hash<K>, uint8_t>;

// 큰 맵 또는 높은 로드 팩터: uint16_t
template<typename K, typename V>
using LargeRobinHoodMap = RobinHoodMap<K, V, std::hash<K>, uint16_t>;
```

**3. 메모리 정렬**:

캐시 라인(64바이트)을 고려한 정렬:

```cpp
struct alignas(64) Slot {  // 캐시 라인 정렬
    K key;
    V value;
    uint8_t psl;
    bool occupied;
    // 패딩이 자동으로 추가됨
};
```

단, 이는 슬롯 크기를 크게 만들 수 있으므로, 작은 key/value 타입에는 오히려 역효과일 수 있다. 프로파일링 후 결정하라.

### 벤치마킹과 프로파일링

성능을 측정하는 것은 최적화의 기본이다:

```cpp
class ProfiledRobinHoodMap : public RobinHoodMap<K, V> {
    mutable size_t total_probes_ = 0;
    mutable size_t num_lookups_ = 0;
    mutable size_t max_psl_ = 0;

public:
    V* find(const K& key) {
        size_t probes = 0;
        // ... (원래 find 로직, probes 카운팅)
        total_probes_ += probes;
        num_lookups_++;
        return result;
    }

    void print_stats() const {
        std::cout << "Average probe length: "
                  << static_cast<double>(total_probes_) / num_lookups_
                  << "\n";
        std::cout << "Max PSL observed: " << max_psl_ << "\n";
        std::cout << "Load factor: " << load_factor() << "\n";
    }
};
```

이를 통해 실제 워크로드에서의 성능을 파악할 수 있다.

## 8. 언제 무엇을 사용할 것인가

해시 맵은 다양한 선택지가 있다. 상황에 맞는 도구를 선택하는 것이 중요하다.

### 의사결정 트리

**std::unordered_map을 사용하라** if:
- 참조 안정성이 필수적이다
- 표준 라이브러리만 사용해야 한다
- 성능이 크리티컬하지 않다
- 이식성이 최우선이다

**Robin Hood Hashing을 사용하라** if:
- 높은 로드 팩터가 필요하다 (메모리 절약)
- 조회 실패가 빈번하다 (조기 종료 이점)
- 삭제 작업이 많다 (backward shift)
- 캐시 효율이 중요하다
- 작은~중간 크기의 key/value 타입

**Abseil flat_hash_map을 사용하라** if:
- 최고의 조회 성능이 필요하다
- SIMD를 활용할 수 있다 (SSE2/AVX2)
- 외부 라이브러리 사용이 가능하다
- 삭제가 드물다

**Unreal TMap을 사용하라** if:
- Unreal Engine 환경이다
- Unreal allocator와 컨테이너 API에 맞춰야 한다
- Unreal의 리플렉션/직렬화와 통합해야 한다

**EASTL hash_map을 사용하라** if:
- 게임 엔진을 개발 중이다
- 커스텀 메모리 관리가 필요하다
- 작은 맵에 대한 스택 할당 최적화가 유용하다

### 해시 함수 선택

**정수 키**:
```cpp
// Identity hash (잘 분포된 정수라면)
struct IntHash {
    size_t operator()(int key) const {
        return static_cast<size_t>(key);
    }
};

// Fibonacci hashing (더 나은 분포)
struct FibonacciHash {
    size_t operator()(uint32_t key) const {
        return key * 2654435769u;  // 2^32 / φ
    }
};
```

**문자열 키**:
```cpp
// FNV-1a (빠름, 괜찮은 분포)
struct FNV1aHash {
    size_t operator()(const std::string& str) const {
        size_t hash = 2166136261u;
        for (char c : str) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }
};

// MurmurHash3 (조금 느림, 더 나은 분포)
// 외부 라이브러리 또는 직접 구현
```

**포인터 키**:
```cpp
struct PointerHash {
    size_t operator()(void* ptr) const {
        // 하위 3비트 제거 (정렬으로 인해 항상 0)
        return reinterpret_cast<size_t>(ptr) >> 3;
    }
};
```


---

## 결론

해시 테이블은 슬롯 배열 위에 충돌과 빈 슬롯을 어떻게 관리하느냐의 변주다. 주요 `std::unordered_map` 구현은 참조 안정성, bucket interface와 노드 추출 같은 표준 요구를 만족시키기 쉬운 node-based chaining을 사용한다. 표준이 물리적인 chaining 구현 자체를 명령하는 것은 아니지만, 원소를 슬롯 안에서 자유롭게 이동하는 단순 flat table로 모든 요구를 만족시키기는 어렵다. Open addressing은 노드 추적 비용을 줄이는 대신 클러스터링과 삭제 상태라는 새 비용을 만든다. Robin Hood Hashing은 probe distance 분산을 줄이고 backward shift deletion으로 tombstone을 피할 수 있다.

Abseil의 Swiss Table은 제어 바이트 묶음을 word/SIMD 연산으로 비교해 후보 슬롯을 빠르게 좁힌다. 이것은 한 CPU 사이클을 보장한다는 뜻이 아니라, 비싼 키 비교 전에 여러 메타데이터를 병렬 필터링한다는 뜻이다. Unreal `TMap`은 `TSparseArray`라는 간접층으로 원소 저장과 hash bucket 연결을 분리한다. 어느 쪽이 옳고 그르냐의 문제가 아니라, 우선시하는 제약이 다를 뿐이다.

해시 테이블의 설계 결정은 항상 참조 안정성, 캐시 효율, 메모리 사용량, 구현 복잡도 사이의 트레이드오프다. 같은 슬롯 배열이라도 키·값 조회가 목표인 해시 테이블과 컴포넌트 순회가 목표인 SoA/ECS는 최적화할 데이터 흐름이 다르다. 저장 형태는 닮을 수 있지만 불변식과 성능 지표를 섞어서는 안 된다.


## 더 읽을거리

- [Emmanuel Goossaert — Robin Hood hashing](https://codecapsule.com/2013/11/11/robin-hood-hashing/)
- Sebastian Sylvan, "Robin Hood Hashing should be your default Hash Table implementation" (2013)
- [Abseil — Swiss Tables Design Notes](https://abseil.io/about/design/swisstables); Matt Kulukundis, "Designing a Fast, Efficient, Cache-friendly Hash Table, Step by Step" (CppCon 2017)
- [Tessil/robin-map](https://github.com/Tessil/robin-map): 프로덕션 품질의 Robin Hood 구현
- [Unreal Engine TMap 레퍼런스](https://docs.unrealengine.com/en-US/API/Runtime/Core/Containers/TMap/), [EASTL](https://github.com/electronicarts/EASTL)
- Donald Knuth, *The Art of Computer Programming, Vol. 3: Sorting and Searching*
