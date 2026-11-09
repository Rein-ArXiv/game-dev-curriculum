## 들어가며

`std::vector`는 C++에서 가장 기본적인 컨테이너지만, 내부 구조를 정확히 짚고 있는 코드는 의외로 드물다. 이 글은 vector의 내부 표현, 성장 인자의 수학적 트레이드오프, 이터레이터 무효화의 미세한 규칙, Small Vector Optimization, 그리고 게임 엔진들의 도메인 특화 구현을 다룬다.

vector는 단순한 동적 배열을 넘어 메모리 연속성, 캐시 지역성, SIMD 자동 벡터화 가능성을 모두 갖춘 자료구조다. 이 특성 덕분에 게임 엔진, 데이터베이스, 컴파일러 같은 성능 민감 영역에서도 vector의 골격을 그대로 가져다 쓰되, 도메인별 트레이드오프에 맞춰 커스텀 구현을 둔다. Unreal Engine의 `TArray`, LLVM의 `SmallVector`, Facebook의 `FBVector`가 대표적이다.

이 글의 범위는 표준 vector의 내부 표현, 성장 정책의 수학적 분석, 무효화 규칙, SVO와 trivially relocatable, 그리고 핵심 멤버 함수의 구현까지다. 동일한 메모리 모델을 공유하는 다른 컨테이너의 트레이드오프 분석에 곧바로 응용할 수 있는 토대가 된다.


---

## 목차
1. [std::vector의 내부 구조: 세 개의 포인터](#1-stdvector의-내부-구조-세-개의-포인터)
2. [성장 인자(Growth Factor)의 수학](#2-성장-인자growth-factor의-수학)
3. [Iterator Invalidation: 미묘하지만 치명적인 함정](#3-iterator-invalidation-미묘하지만-치명적인-함정)
4. [Small Vector Optimization](#4-small-vector-optimization)
5. [게임 엔진의 Vector 구현](#5-게임-엔진의-vector-구현)
6. [핵심 함수 구현 깊이 파기](#6-핵심-함수-구현-깊이-파기)
7. [실전 최적화 기법](#7-실전-최적화-기법)


---

## 1. std::vector의 내부 구조: 세 개의 포인터

### 기본 설계

대부분의 표준 라이브러리 구현에서 `std::vector<T>`(기본 allocator, `bool` 제외)는 놀랍게도 단 세 개의 포인터로 구성된다. stateful allocator나 fancy pointer, debug ABI를 쓰면 allocator 객체가 공간을 차지해 레이아웃이 달라질 수 있고, `std::vector<bool>`은 비트를 압축하는 특수화라 이 구조가 아니다:

```cpp
template<typename T, typename Allocator = std::allocator<T>>
class vector {
private:
    T* m_begin;           // 첫 번째 요소를 가리킴
    T* m_end;             // 마지막 요소의 다음을 가리킴
    T* m_end_of_storage;  // 할당된 메모리의 끝을 가리킴
    Allocator m_alloc;
};
```

이 세 개의 포인터는 각각 명확한 역할을 가지고 있다:

- `m_begin`: 벡터의 첫 번째 요소 위치
- `m_end`: 현재 사용 중인 마지막 요소의 **다음** 위치 (past-the-end)
- `m_end_of_storage`: 할당된 메모리 블록의 끝

왜 마지막 요소를 직접 가리키지 않고 "다음" 위치를 가리킬까? 이는 C++의 반개방 구간(half-open range) 설계 철학에 따른 것이다. `[begin, end)` 형태는 빈 컨테이너를 `begin == end`로 간결하게 표현할 수 있고, 이터레이터 순회 로직도 단순해진다.

### 포인터 기반 연산의 효율성

이 세 포인터만으로 모든 기본 연산을 상수 시간에 구현할 수 있다:

```cpp
size_type size() const noexcept {
    return m_begin ? static_cast<size_type>(m_end - m_begin) : 0;
}

size_type capacity() const noexcept {
    return m_begin ? static_cast<size_type>(m_end_of_storage - m_begin) : 0;
}

bool empty() const noexcept {
    return m_begin == m_end;  // O(1)
}

iterator begin() noexcept {
    return m_begin;  // O(1)
}

iterator end() noexcept {
    return m_end;  // O(1)
}
```

이 연산들은 요소 수와 무관한 상수 시간이다. 빈 상태를 세 null pointer로 표현하는 이 단순 모델에서는 null pointer끼리 뺄셈하지 않도록 먼저 검사한다. `noexcept`는 이 함수가 예외를 던지지 않는다는 계약이지, 일반적으로 런타임 예외 처리 비용을 자동 제거하는 성능 스위치는 아니다.

### size vs capacity의 의미

Vector를 이해하는 핵심은 size와 capacity의 차이를 정확히 아는 것이다:

**Size (크기):**
- 현재 벡터에 저장된 **실제 요소의 개수**
- 사용자가 `push_back`으로 추가한 요소 수
- `m_end - m_begin`으로 계산

**Capacity (용량):**
- 현재 할당된 메모리에 저장할 수 있는 **최대 요소 개수**
- 재할당 없이 추가할 수 있는 여유 공간
- `m_end_of_storage - m_begin`으로 계산

예시를 통해 이해해보자:

```cpp
std::vector<int> v;
// size: 0, capacity: 0
// m_begin == m_end == m_end_of_storage == nullptr

v.push_back(1);
// size: 1, capacity: 1 (구현에 따라 다를 수 있음)
// m_begin ----------> [1]
// m_end -------------> ^
// m_end_of_storage --> ^

v.push_back(2);
// 재할당 발생! (capacity 부족)
// size: 2, capacity: 2
// m_begin ----------> [1][2]
// m_end -----------------> ^
// m_end_of_storage ------> ^

v.push_back(3);
// 재할당 발생!
// size: 3, capacity: 4 (2.0x 성장의 경우)
// m_begin ----------> [1][2][3][?]
// m_end --------------------> ^
// m_end_of_storage ------------> ^

v.reserve(100);
// size는 변경 없음, capacity만 증가
// size: 3, capacity: 100
// m_begin ----------> [1][2][3][?][?]...[?]
// m_end --------------------> ^
// m_end_of_storage ----------------------------> ^
```

여유 공간 = `capacity - size`는 재할당 없이 추가할 수 있는 요소 개수를 의미한다.

### 연속 메모리의 보장과 그 가치

C++ 표준은 `std::vector`가 요소들을 **연속된(contiguous) 메모리**에 저장한다고 명시적으로 보장한다. 이는 단순한 구현 세부사항이 아니라, vector의 핵심 가치이다.

**메모리 레이아웃:**

*Heap Memory:*
| elem0 | elem1 | elem2 | elem3 | unused | (버퍼 끝) |
|:-----:|:-----:|:-----:|:-----:|:------:|:---------:|
| ↑ `m_begin` | | | | ↑ `m_end` | ↑ `m_end_of_storage` |

*Stack (vector object) - 3개의 포인터 (24 bytes on 64-bit):*
| 필드 | 역할 |
|------|------|
| `m_begin` | 첫 요소 포인터 |
| `m_end` | 마지막+1 포인터 |
| `m_end_of_storage` | 용량 끝 포인터 |

연속 메모리가 제공하는 이점들:

**1. 캐시 지역성 (Cache Locality)**

현대 CPU는 메모리 접근 시 단일 바이트가 아닌 캐시 라인(보통 64바이트) 단위로 데이터를 가져온다. Vector의 요소들이 연속되어 있으면 하나의 캐시 라인에 여러 요소가 담기므로 순차 접근 시 캐시 히트율이 극적으로 높아진다.

```cpp
std::vector<int> v(1000000);

// 캐시 친화적: 순차 접근
for (size_t i = 0; i < v.size(); ++i) {
    process(v[i]);  // 높은 캐시 히트율
}

// 비교: std::list는 노드가 힙에 분산됨
std::list<int> l(1000000);
for (auto& val : l) {
    process(val);  // 잦은 캐시 미스
}
```

순차 순회에서는 `vector`가 노드 기반 `list`보다 빠른 경우가 많지만 배수는 원소 크기, 처리 함수의 비용, allocator 배치와 캐시 상태에 따라 달라진다. 비교할 때는 순회 자체가 최적화로 제거되지 않게 하고, 같은 원소와 같은 작업을 사용해 cache miss와 실행 시간을 함께 측정한다.

**2. C 스타일 API와의 호환**

연속 메모리 보장 덕분에 vector를 C 라이브러리와 안전하게 연동할 수 있다:

```cpp
std::vector<float> vertices = {...};

// OpenGL 함수에 직접 전달
glBufferData(GL_ARRAY_BUFFER,
             vertices.size() * sizeof(float),
             vertices.data(),  // T*를 반환
             GL_STATIC_DRAW);

// 파일 I/O
std::vector<uint8_t> buffer(1024);
fread(buffer.data(), 1, buffer.size(), file);
```

`data` 멤버 함수는 C++11부터 공식적으로 제공되며, 첫 번째 요소의 주소를 반환한다.

**3. SIMD 최적화**

연속 메모리는 컴파일러가 SIMD(Single Instruction Multiple Data) 명령어를 적용하기 쉽게 만든다:

```cpp
void multiply(std::vector<float>& v, float scalar) {
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] *= scalar;
    }
}

// 대상 CPU·옵션과 의존성 조건이 맞으면 다음과 같은 AVX 벡터화가 가능:
// 한 번에 8개의 float를 처리 (256-bit register)
// vmulps ymm0, ymm1, ymm2  (8x 병렬 곱셈)
```

GCC나 Clang은 `-O3 -march=native` 옵션으로 이러한 자동 벡터화를 적극적으로 수행한다.

**4. 포인터 연산**

연속성이 보장되므로 포인터 연산이 안전하다:

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};

int* ptr = &v[0];    // 또는 v.data()
ptr[3] = 100;         // v[3] = 100과 동일
*(ptr + 2) = 200;     // v[2] = 200과 동일

// 범위 기반 포인터 연산
int* begin = v.data();
int* end = v.data() + v.size();
std::sort(begin, end);  // 완벽히 동작
```

## 2. 성장 인자(Growth Factor)의 수학

### 왜 성장 인자가 중요한가

Vector에 요소를 계속 추가하다 보면 용량이 부족해진다. 이때 벡터는 더 큰 메모리를 할당하고 기존 요소들을 복사/이동한 후 이전 메모리를 해제하는 **재할당(reallocation)** 과정을 거친다.

재할당은 비용이 크다:
- allocator에서 새 storage 획득. 일반 heap allocator라도 매번 OS system call을 한다는 뜻은 아님
- 모든 요소 복사/이동: O(n) 시간
- 이전 storage를 allocator에 반환

따라서 재할당을 너무 자주 하면 성능이 떨어지고, 너무 드물게 하면 메모리가 낭비된다. 이 균형을 맞추는 핵심이 바로 **성장 인자(growth factor)** 이다.

### 주요 구현체의 선택

표준은 capacity growth factor를 지정하지 않는다. 아래 식은 특정 library source와 단일 원소 append에서 관찰되는 대표적인 정책을 단순화한 것이며, version·현재 size·한 번에 추가하는 원소 수와 최대 크기 근처에서 달라질 수 있다.

**libstdc++ / libc++의 대표적인 단일 append 경로: 약 2배 계열**
```cpp
size_type new_capacity = capacity() == 0 ? 1 : capacity() * 2;
```

**MSVC STL의 대표적인 단일 append 경로: 약 1.5배 계열**
```cpp
size_type new_capacity = capacity() + capacity() / 2;
```

왜 이렇게 다를까? 각각 다른 트레이드오프를 선택했기 때문이다.

### 성장 인자의 수학적 분석

성장 인자 $k$를 가진 벡터가 $n$번 재할당을 거쳐 용량 $c_n$에 도달했다고 하자:

$$c_0, c_1 = kc_0, c_2 = k^2c_0,..., c_n = k^nc_0$$

재할당이 일어나는 순간 새 buffer와 기존 buffer가 동시에 존재한다. 과거에 해제한 모든 buffer가 완전히 병합 가능하다고 가정하면, 새 buffer $c_n$을 그 총공간에서 얻기 위한 **필요조건**은 현재 사용 중인 $c_{n-1}$을 제외한 이전 allocation의 합이 $c_n$ 이상인 것이다.

$$c_0 + c_1 +... + c_{n-2} \geq c_n$$

등비급수로 정리하면:

$$c_0 \cdot \frac{k^{n-1} - 1}{k - 1} \geq c_0 \cdot k^n$$

양변을 $c_0 \cdot k^n$으로 나누면:

$$\frac{k^{-1} - k^{-n}}{k - 1} \geq 1$$

$n \to \infty$일 때 $k^{-n} \to 0$이므로:

$$\frac{1}{k(k - 1)} \geq 1 \quad \Longleftrightarrow \quad \frac{1}{k - 1} \geq k$$

양변에 $(k - 1)$을 곱하면 (k > 1이므로 부호 보존):

$$1 \geq k(k - 1) = k^2 - k$$

$$k^2 - k - 1 \leq 0$$

이차방정식의 양의 근을 구하면:

$$k \leq \frac{1 + \sqrt{5}}{2} = \phi \approx 1.618$$

여기서 $\phi$는 **황금비(golden ratio)** 이다.

**이상화된 aggregate-space 결론:**
- $1 < k < \phi$이면 유한 단계에서 과거 해제량의 합이 새 buffer 크기에 도달할 수 있다.
- $k = \phi$이면 유한 단계에서는 미달하고 극한에서만 경계에 접근한다.
- $k > \phi$이면 이 합조차 새 buffer보다 작다.

이 조건은 충분조건이 아니다. 실제 allocator에서는 block이 비연속이고 size class, header, alignment와 다른 allocation이 끼어들기 때문에 $k < \phi$여도 같은 주소 영역을 재사용하지 못할 수 있다.

| 대표 정책 | 황금비와의 관계 | aggregate-space 필요조건 |
|---|---|---|
| 1.5배 | $1.5 < \phi$ | 장기적으로 충족 가능 |
| 2.0배 | $2.0 > \phi$ | 충족하지 못함 |

그렇다면 MSVC가 무조건 우월할까? 그렇지 않다.

### 성장 인자의 트레이드오프

| 성장 인자 | capacity 여유 | 재할당 빈도 | 기존 원소 이동 상한 계수 | 과거 해제량 합 조건 |
|-----------|------------|-------------|------------|--------------|
| 1.5배 | 작음 | 높음 | 큼 | 충족 가능 (< φ) |
| 2.0배 | 큼 | 낮음 | 작음 | 불충족 (> φ) |

**재할당 횟수 비교:**

$n$개 요소를 삽입할 때:
- 2.0배 성장: $\log_2 n$ 번 재할당
- 1.5배 성장: $\log_{1.5} n \approx 1.71 \log_2 n$ 번 재할당

정확한 $n$이 growth boundary에 놓인 이상화된 경우 1.5배 계열의 로그 재할당 횟수가 2배 계열보다 약 71% 많다. 시작 capacity, 정수 반올림과 bulk insertion이 실제 횟수를 바꾼다.

**분할 상환 복사 비용:**

최종 capacity가 $n$인 경계에 도달했다고 단순화하고, 매번 기존 원소를 이동한다고 하자. 최초 삽입 $n$번을 제외한 **기존 원소 이동 횟수**의 기하급수 상한은 다음과 같다.

2.0배 성장:
$$\frac{n}{2} + \frac{n}{4} + \frac{n}{8} + \cdots < n$$

1.5배 성장:
$$\frac{2n}{3} + \frac{4n}{9} + \frac{8n}{27} + \cdots < 2n$$

따라서 이 모델에서는 1.5배 growth의 기존 원소 이동 상한 계수가 2배 growth의 약 두 배다. 최초 $n$개 원소 construction까지 포함한 총 element write 상한은 각각 약 $3n$과 $2n$이지만, 이것을 “복사 3n 대 2n”으로 부르면 서로 다른 비용을 섞게 된다.

실전에서는 도메인 이름으로 growth factor를 고르지 않는다. peak live bytes, allocation count, 이동 생성자 비용, iterator invalidation과 allocator size class를 workload trace로 측정한다. 최종 크기를 예측할 수 있다면 implementation growth policy를 추측하는 것보다 `reserve`로 필요한 capacity를 명시하는 편이 직접적이다.

### Facebook FBVector: jemalloc과의 협력

Facebook의 Folly 라이브러리는 더 똑똑한 접근을 택했다. 할당자의 실제 동작을 고려하는 것이다.

**문제점:**

대부분의 메모리 할당자는 내부적으로 **size class**를 사용한다. 예를 들어 jemalloc의 size classes:

```text
Small sizes:  8, 16, 32, 48, 64, 80, 96, 112, 128,
              160, 192, 224, 256, 320, 384, 448, 512, ...
Larger sizes: 페이지 크기와 설정에 맞춘 클래스 ...
```

대표적인 기본 jemalloc 크기 클래스에서 400바이트 요청은 448바이트 클래스로 반올림되어 48바이트가 내부 단편화된다. 정확한 클래스는 jemalloc 버전·설정과 플랫폼에 따라 확인해야 한다.

**FBVector의 해결책:**

```cpp
// folly/FBVector.h의 핵심 아이디어
template <class T>
class fbvector {
private:
    size_t goodMallocSize(size_t minSize) const {
        // jemalloc이 실제로 할당할 크기를 미리 계산
        return folly::goodMallocSize(minSize * sizeof(T)) / sizeof(T);
    }

    void grow_to(size_type new_size) {
        size_type new_cap = std::max<size_type>(capacity(), 1);

        // 지수 성장하되, jemalloc의 size class에 맞춤
        while (new_cap < new_size) {
            new_cap = goodMallocSize(new_cap + std::max<size_type>(new_cap / 2, 1));
        }

        reserve(new_cap);
    }
};
```

**실제 효과:**

```cpp
std::vector<int> v;
v.reserve(100);
// 요청: 400 bytes
// 실제 할당: 448 bytes (대표적인 jemalloc 기본 클래스)
// 사용 가능: 400 bytes
// allocator 관점의 여유: 48 bytes

folly::fbvector<int> fv;
fv.reserve(100);
// goodMallocSize(400) = 448 확인
// 실제 capacity: 448/4 = 112 요소로 조정
// allocator가 이미 확보한 48 bytes를 vector capacity로 활용
```

FBVector는 할당자의 실제 동작을 고려하여 메모리 낭비를 최소화하고, 재할당 빈도도 줄인다. 이는 Facebook의 대규모 서버 환경에서 검증된 최적화이다.

## 3. Iterator Invalidation: 미묘하지만 치명적인 함정

### 무효화(Invalidation)란 무엇인가

Iterator invalidation은 C++ vector 사용 시 가장 흔한 버그 원인 중 하나이다. 간단히 말해, 특정 연산 후 이터레이터나 참조가 더 이상 유효하지 않게 되는 것을 의미한다.

유효하지 않은 이터레이터를 역참조하면 **undefined behavior (UB)** 가 발생한다. 크래시가 날 수도 있고, 조용히 잘못된 값을 읽을 수도 있다. 디버그하기 매우 어려운 버그가 된다.

### 무효화 규칙 완전 정리

**읽기 전용 연산 (무효화 없음):**
무효화가 없다는 뜻과 접근 전제는 별개다. 아래 `v[0]`·`front()`에는 벡터가 비어 있지 않다는 전제가 필요하다.

```cpp
const std::vector<int>& v = /* ... */;
v.size();      // 안전
v[0];          // 안전
v.begin();     // 안전
v.front();     // 안전
```

**재할당 발생 가능 연산:**

| 연산 | 재할당 시 | 재할당 없을 시 |
|------|-----------|---------------|
| `reserve(n)` | 모든 이터레이터/참조 무효화 | 무효화 없음 |
| `push_back(x)` | 모든 이터레이터/참조 무효화 | `end` 무효화, 나머지 유효 |
| `emplace_back(...)` | 모든 이터레이터/참조 무효화 | `end` 무효화, 나머지 유효 |
| `insert(pos, x)` | 모든 이터레이터/참조 무효화 | `pos`를 포함한 그 이후 무효화 |
| `resize(n)` | 모든 이터레이터/참조 무효화 | 증가하면 기존 `end`, 감소하면 제거 원소와 그 이후(기존 `end` 포함), 같은 크기면 무효화 없음 |

**삭제 연산:**

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
auto it = v.begin() + 2;  // points to 3

v.erase(it);
// it 무효화
// it 이후의 모든 이터레이터 무효화
// end() 무효화
// it 이전 이터레이터는 유효

v.pop_back();
// 마지막 요소 이터레이터 무효화
// end() 무효화
// 나머지는 유효

v.clear();
// 모든 이터레이터 무효화
```

### 재할당의 메커니즘

재할당이 발생하면 왜 모든 이터레이터가 무효화될까? 내부 동작을 보면 명확하다:

```cpp
void push_back(const T& value) {
    if (m_end == m_end_of_storage) {
        // 1. 새 메모리 할당
        size_type new_cap = capacity() == 0 ? 1 : capacity() * 2;
        T* new_begin = allocate(new_cap);

        // 2. 요소들을 새 위치로 이동
        T* new_end = new_begin;
        for (T* p = m_begin; p != m_end; ++p, ++new_end) {
            new (new_end) T(std::move(*p));
        }

        // 3. 이전 요소 소멸 후 메모리 해제
        for (T* p = m_begin; p != m_end; ++p) {
            p->~T();
        }
        deallocate(m_begin);  // ← 이터레이터들이 가리키던 메모리!

        // 4. 포인터 갱신
        m_begin = new_begin;
        m_end = new_end;
        m_end_of_storage = new_begin + new_cap;
    }

    new (m_end) T(value);
    ++m_end;
}
```

이전 메모리가 해제되었으므로, 그 주소를 가리키던 모든 이터레이터는 dangling pointer가 된다. 이 축약 구현은 예외 처리와 `v.push_back(v[i])` 같은 자기 별칭을 다루지 않는다. 표준 구현은 새 원소를 먼저 구성한 뒤 옛 저장소를 정리해 두 경우를 모두 처리한다.

### end 이터레이터의 특수성

`push_back`/`emplace_back`로 크기가 늘면 이전 `end()`는 재할당 여부와 관계없이 무효화된다. 이는 모든 vector 연산이 `end()`를 무효화한다는 뜻은 아니며, 연산별 invalidation 규칙을 확인해야 한다.

```cpp
std::vector<int> v = {1, 2, 3};
v.reserve(100);  // 재할당 방지

auto it_end = v.end();

v.push_back(4);
// 재할당 발생 안 했는데도 it_end는 무효화!
```

이유는 간단한다. `end`는 `m_end` 포인터 값을 반환하는데, 요소 추가 시 `m_end`가 변경되기 때문이다:

```cpp
iterator end() noexcept {
    return m_end;  // m_end의 현재 값을 복사
}

void push_back(const T& value) {
    // ...
    ++m_end;  // ← m_end 변경! 이전 end() 값은 이제 틀림
}
```

### 흔한 실수와 올바른 패턴

**실수 1: 루프 중 삽입**

```cpp
// 위험한 코드
auto end = v.end();
for (auto it = v.begin(); it != end; ++it) {
    if (should_duplicate(*it)) {
        v.push_back(*it);  // end 무효화. 재할당 가능
    }
}
```

이 코드는 다음과 같은 문제가 있다:
- `end`가 무효화되어 루프 조건이 정의되지 않은 동작
- 재할당 시 `it`도 무효화될 수 있음

**해결책 1: 인덱스 사용**

```cpp
// 안전한 코드
size_t original_size = v.size();
for (size_t i = 0; i < original_size; ++i) {
    if (should_duplicate(v[i])) {
        v.push_back(v[i]);  // 안전
    }
}
```

**실수 2: 루프 중 삭제**

```cpp
// 위험한 코드
for (auto it = v.begin(); it != v.end(); ++it) {
    if (should_remove(*it)) {
        v.erase(it);  // it 무효화. ++it는 UB
    }
}
```

**해결책 2: erase의 반환값 사용**

```cpp
// 안전한 코드
for (auto it = v.begin(); it != v.end(); ) {
    if (should_remove(*it)) {
        it = v.erase(it);  // erase는 다음 유효 이터레이터 반환
    } else {
        ++it;
    }
}
```

**해결책 3: erase-remove idiom (가장 효율적)**

```cpp
v.erase(std::remove_if(v.begin(), v.end(), should_remove), v.end());
```

이는 단 한 번의 순회로 모든 삭제를 처리하며, 이터레이터 무효화 문제도 없다.

### 디버그 빌드의 도움

많은 표준 라이브러리 구현은 디버그 모드에서 이터레이터 무효화를 감지한다:

```cpp
// MSVC의 디버그 STL
std::vector<int> v = {1, 2, 3};
auto it = v.begin();

v.push_back(4);  // 재할당 발생

*it;  // 런타임 에러: "vector iterator not dereferenceable"
```

libstdc++를 쓰는 GCC/Clang 구성에서는 `-D_GLIBCXX_DEBUG`로 유사한 검사를 활성화할 수 있다. libc++ 등 다른 표준 라이브러리는 해당 버전의 hardening/debug 설정을 확인한다.

## 4. Small Vector Optimization

### 힙 할당의 숨겨진 비용

비어 있지 않은 `std::vector`는 원소 저장 공간을 객체 외부에서 얻는다.

```cpp
std::vector<int> v = {1, 2, 3};
// allocator를 통해 외부 저장 공간 확보
```

작은 벡터(예: 3~8개 요소)를 빈번하게 생성/삭제하는 코드에서는 이 오버헤드가 무시할 수 없다:

- **할당자 비용**: size class 조회, free-list와 메타데이터 갱신
- **공유 상태**: 로컬 캐시 miss나 cross-thread free에서 동기화 가능
- **캐시 지역성 저하**: 벡터 객체와 데이터가 분리됨
- **메모리 단편화**: 작은 할당이 힙을 조각냄

Small Vector Optimization (SVO)은 이 문제를 우아하게 해결한다.

### 기본 아이디어

작은 크기의 요소들은 벡터 객체 내부에 직접 저장하고, 크기가 커지면 힙으로 전환하는 것이다:

```cpp
SmallVector<int, 8> sv;  // 8개까지 스택에 저장

sv.push_back(1);  // 스택에 저장 (힙 할당 없음!)
sv.push_back(2);  // 스택에 저장
// ...
sv.push_back(8);  // 스택에 저장

sv.push_back(9);  // 이제 힙 할당 발생
```

### LLVM SmallVector 구현

LLVM은 컴파일러 인프라이므로 작은 임시 배열을 엄청나게 많이 사용한다. 토큰 리스트, AST 노드의 자식 배열, 레지스터 목록 등이 대부분 작다. SmallVector는 이를 위해 설계되었다.

**기본 구조:**

```cpp
// llvm/ADT/SmallVector.h의 레이아웃을 단순화한 교육용 의사코드다.
// 실제 LLVM은 정렬·소유권·예외 처리를 다른 방식으로 다룬다.
template <typename T>
class SmallVectorImpl {
protected:
    T* BeginX;
    unsigned Size = 0;
    unsigned Capacity;

public:
    explicit SmallVectorImpl(unsigned N) : Capacity(N) {}

    void push_back(const T& Elt) {
        if (Size >= Capacity) {
            grow();
        }
        ::new ((void*)(BeginX + Size)) T(Elt);
        ++Size;
    }

    void grow() {
        size_t NewCapacity = Capacity ? Capacity * 2 : 1;  // N == 0도 1 이상으로
        // malloc은 기본 정렬만 보장하므로 alignof(T)가 크면 aligned allocation이 필요하다.
        T* NewElts = static_cast<T*>(malloc(NewCapacity * sizeof(T)));
        if (!NewElts) throw std::bad_alloc(); // grow 실패 후 push_back이 계속 쓰지 않게 중단

        // 이동한 뒤 옛 원소는 소멸시킨다.
        for (unsigned i = 0; i < Size; ++i) {
            ::new (static_cast<void*>(NewElts + i)) T(std::move(BeginX[i]));
            BeginX[i].~T();
        }

        // 이전 메모리 해제 (인라인 버퍼가 아닌 경우만)
        if (!isSmall()) {
            free(BeginX);
        }

        BeginX = NewElts;
        Capacity = NewCapacity;
    }

    bool isSmall() const {
        // BeginX가 인라인 버퍼를 가리키는지 확인
        return BeginX == getFirstEl();
    }

    const void* getFirstEl() const {
        // this 바로 뒤의 메모리 (인라인 스토리지 시작 위치).
        // 이 축약은 기반 객체 바로 뒤에 인라인 저장소가 오는 레이아웃을 가정한다.
        // alignof(T)가 기반보다 크면 패딩이 끼어 실제 주소와 어긋날 수 있다.
        return reinterpret_cast<const void*>(
            reinterpret_cast<const char*>(this) + sizeof(*this)
        );
    }
};

template <typename T, unsigned N>
class SmallVector : public SmallVectorImpl<T> {
    // 인라인 스토리지: N개의 T를 저장할 공간
    alignas(T) char InlineElts[(N == 0 ? 1 : N) * sizeof(T)]; // 표준 C++의 0길이 배열 회피

public:
    SmallVector() : SmallVectorImpl<T>(N) {
        this->BeginX = reinterpret_cast<T*>(InlineElts);
    }
};
```

**메모리 레이아웃 상세 - `SmallVector<int, 4>` (스택에 할당, Total: 32 bytes):**

| 영역 | 필드 | 크기 | 설명 |
|:----:|------|:----:|------|
| Header | `BeginX` | 8 bytes | → InlineElts[0]를 가리킴 |
| | `Size` | 4 bytes | |
| | `Capacity` | 4 bytes | |
| Inline | `InlineElts[0]` | 4 bytes | ← BeginX가 여기를 가리킴 |
| | `InlineElts[1]` | 4 bytes | |
| | `InlineElts[2]` | 4 bytes | |
| | `InlineElts[3]` | 4 bytes | |

**사용 예:**
```cpp
SmallVector<int, 4> sv;

sv.push_back(1);
// InlineElts[0] = 1
// Size = 1, BeginX = &InlineElts[0]

sv.push_back(2);
sv.push_back(3);
sv.push_back(4);
// InlineElts = [1, 2, 3, 4]
// Size = 4, Capacity = 4
// 여전히 스택에만 존재

sv.push_back(5);
// grow() 호출!
// malloc(8 * sizeof(int)) → 힙 할당
// BeginX가 이제 힙을 가리킴
// InlineElts는 더 이상 사용되지 않음
```

### 핵심 기법: isSmall 체크

SmallVector의 핵심은 현재 상태가 "small"인지 판단하는 것이다:

```cpp
bool isSmall() const {
    return BeginX == getFirstEl();
}
```

`BeginX`가 인라인 버퍼를 가리키면 small, 다른 곳을 가리키면 힙 할당된 상태이다. 이를 통해 `grow` 시 조건부로 메모리를 해제한다:

```cpp
void grow() {
    // 새 메모리 할당
    T* NewElts = static_cast<T*>(malloc(NewCapacity * sizeof(T)));
    if (!NewElts) throw std::bad_alloc();

    // 요소 이동
    std::uninitialized_move(BeginX, BeginX + Size, NewElts);
    std::destroy_n(BeginX, Size); // 옛 객체 수명 종료는 inline/heap 양쪽 모두 필요

    // 이전 메모리 해제 (힙이었다면)
    if (!isSmall()) {
        free(BeginX);  // InlineElts는 free하면 안 됨!
    }

    BeginX = NewElts;
    Capacity = NewCapacity;
}
```

이 두 `grow` 모형은 크기 산술이 유효하고 `T`의 이동이 예외를 던지지 않는 경우의 수명 흐름을 보여 준다. `push_back`의 인자가 기존 원소를 가리키는 자기 별칭과 over-aligned 타입의 저장소는 별도 처리가 필요하다. 실제 LLVM `SmallVector`도 표준 `vector`와 같은 예외 안전성을 목표로 하지 않으므로 두 컨테이너의 계약을 혼동하지 않는다.

### N 선택 가이드라인

템플릿 파라미터 `N`을 얼마로 설정해야 할까?

**프로파일링 기반 결정:**

```cpp
// 실제 사용 데이터 수집
std::map<size_t, size_t> size_distribution;

void track_usage(size_t size) {
    size_distribution[size]++;
}

// 분석 후 90th percentile을 N으로 설정
// 예: 90%의 경우 크기가 8 이하라면 N=8
```

**객체 크기 고려:**

```cpp
sizeof(SmallVector<int, 4>)  = 32 bytes    // 합리적
sizeof(SmallVector<int, 16>) = 80 bytes    // 여전히 OK
sizeof(SmallVector<int, 64>) = 272 bytes   // 스택 부담
```

너무 큰 `N`은 스택 오버플로 위험이 있다. 함수 지역 변수로 사용 시 특히 주의해야 한다.

인라인 용량 `N`은 관습적인 숫자가 아니라 실제 크기 분포와 객체 예산으로 정한다. `N` 이하 비율이 높을수록 heap allocation을 피하지만, 모든 `SmallVector` 객체가 `N * sizeof(T)`에 가까운 저장 공간을 품으므로 드문 큰 용량을 기준으로 잡으면 스택·구조체·캐시 사용량이 늘어난다. 비교할 때는 allocation count, 객체 전체 크기, move 비용과 p95 크기를 함께 기록한다.

## 5. 게임 엔진의 Vector 구현

게임 엔진은 표준 라이브러리를 그대로 사용하지 않는다. 프레임당 수천 개의 벡터가 생성/삭제되고, 메모리 할당은 성능의 병목이 되기 쉽다. 각 엔진은 자신의 도메인에 최적화된 커스텀 구현을 제공한다.

### Unreal Engine TArray: 재배치 가능 타입 계약

Unreal Engine의 `TArray`는 엔진이 정의한 재배치 가능성 trait와 allocator 계약을 이용해 원소 저장소를 관리한다. 아래 코드는 실제 엔진 소스를 그대로 옮긴 것이 아니라, bytewise relocation이 가능한 타입에 적용할 수 있는 경로를 단순화한 의사 코드다.

**핵심 가정: Trivially Relocatable**

```cpp
// UnrealEngine/Engine/Source/Runtime/Core/Public/Containers/Array.h
template<typename T, typename Allocator = FDefaultAllocator>
class TArray {
private:
    T* Data;
    int32 ArrayNum;     // size
    int32 ArrayMax;     // capacity

public:
    void Reallocate(int32 NewMax) {
        T* NewData = Allocator::Malloc(NewMax * sizeof(T));

        // T가 엔진의 bytewise-relocatable 계약을 만족하는 경로만 단순화
        FMemory::Memcpy(NewData, Data, ArrayNum * sizeof(T));

        // 소멸자 호출 없음
        Allocator::Free(Data);

        Data = NewData;
        ArrayMax = NewMax;
    }
};
```

임의의 `T`에 이 의사 코드를 적용하면 객체 수명과 자기 참조·내부 포인터 불변식을 깨뜨릴 수 있다. 엔진 컨테이너가 안전하려면 타입별 trait, 생성·소멸 경로와 allocator의 relocation 지원이 서로 맞아야 한다. 따라서 "`TArray`는 모든 타입을 무조건 `memcpy`한다"거나 "표준 규칙을 그냥 무시한다"로 이해하면 안 된다.

**Trivially Relocatable이란?**

메모리를 `memcpy`로 복사해도 안전한 타입이다:

```cpp
// Trivially relocatable
struct Vec3 {
    float x, y, z;
};
// POD 타입: 비트 단위 복사 가능

struct Transform {
    Vec3 position;
    Vec3 rotation;
    Vec3 scale;
};
// 모든 멤버가 trivially relocatable

// NOT trivially relocatable
class SmallBuffer {
    char inline_[16];
    char* ptr_;                // 짧은 값이면 자기 자신의 inline_을 가리킨다

    SmallBuffer(SmallBuffer&& other) noexcept {
        std::memcpy(inline_, other.inline_, sizeof(inline_));
        ptr_ = inline_;        // ← 새 주소를 다시 가리켜야 한다
    }
};
// memcpy로 재배치하면:
// 1. ptr_이 여전히 옛 객체의 inline_을 가리킴
// 2. 옛 저장소를 해제하는 순간 dangling → 자기 참조 불변식이 깨진다
```

**Unreal의 타입 특성 시스템:**

```cpp
template<typename T>
struct TIsTriviallyCopyConstructible {
    enum { Value = __is_trivially_copy_constructible(T) };
};

template<typename T>
struct TIsTriviallyRelocatable {
    // 개념 예시: 실제 엔진 trait와 버전별 기본값은 엔진 헤더를 따른다.
    enum { Value = TIsTriviallyCopyConstructible<T>::Value };
};

// 명시적 특수화로 안전하지 않은 타입 표시 (자기 참조 저장소를 가진 가상의 타입)
template<>
struct TIsTriviallyRelocatable<SmallBuffer> {
    enum { Value = false };
};
```

trivially relocatable 계약은 타입을 새 주소로 바이트 이동한 뒤 이전 저장소를 별도 소멸하지 않아도 된다는 뜻이다. 단순히 "게임 타입은 대부분 POD"라고 가정해서는 안 되며, 포인터가 자기 주소를 참조하거나 이동 시 외부 등록을 갱신하는 타입은 이 계약을 만족하지 않는다.

**성능 이점:**

```cpp
// std::vector (안전하지만 느림)
void grow() {
    for (size_t i = 0; i < size; ++i) {
        new (new_data + i) T(std::move(old_data[i]));  // N번 이동 생성
        old_data[i].~T();                               // N번 소멸
    }
}
// 비-trivial 타입은 원소별 이동 생성과 이전 원소 소멸이 필요할 수 있다.

// byte-relocatable로 증명된 타입
void grow() {
    memcpy(new_data, old_data, size * sizeof(T));  // 단일 메모리 복사!
}
// 연속 바이트 복사로 재배치할 수 있다.
```

이득은 타입의 이동 생성자·소멸자 비용과 메모리 대역폭에 따라 달라진다. `Vec3`처럼 trivially copyable한 타입은 표준 라이브러리 구현과 컴파일러도 바이트 복사로 최적화할 수 있으므로, 위 두 경로가 곧 `std::vector` 대 `TArray`의 고정 배율을 뜻하지 않는다.

**위험성과 대응:**

물론 이는 위험하다. 잘못된 타입에 TArray를 사용하면 크래시나 메모리 손상이 발생한다. Unreal은 이를 다음과 같이 완화한다:

1. **엄격한 타입 정책**: 엔진 컨테이너가 요구하는 relocatable 가정을 trait와 문서로 규정
2. **컴파일 타임 체크**: 특정 타입에 대해 `static_assert` 사용
3. **문서화**: 커스텀 타입 작성 시 주의사항 명시
4. **대안 제공**: relocatable하지 않은 타입을 위한 `TIndirectArray` 등

### EASTL vector: 인스턴스 할당자의 힘

Electronic Arts의 EASTL은 게임 엔진의 메모리 관리 요구사항을 깊이 이해하고 설계되었다.

**표준 할당자의 한계:**

```cpp
// std::vector도 할당자 인스턴스를 멤버로 갖지만, 기본 생성하면 상태가 없다
std::vector<int, MyAllocator<int>> v1;
std::vector<int, MyAllocator<int>> v2;

// 다른 풀을 쓰려면 생성 시점에 할당자 인스턴스를 넘겨야 하고,
// 할당자 타입이 vector 타입의 일부라 나중에 교체할 수 없다
std::vector<int, MyAllocator<int>> v3{MyAllocator<int>(&poolA)};
```

**EASTL의 접근: 타입에 매이지 않는 기본 할당자**

```cpp
// EASTL/vector.h의 구조를 단순화한 의사 코드.
// 실제 VectorBase는 capacity 포인터와 할당자를 compressed_pair로 묶는다.
template <typename T, typename Allocator = EASTLAllocatorType>
class vector {
protected:
    T* mpBegin;
    T* mpEnd;
    T* mpCapacity;
    Allocator mAllocator;  // ← 인스턴스로 저장!

public:
    // 할당자를 생성자에서 받음
    vector(const Allocator& allocator = Allocator())
        : mpBegin(nullptr), mpEnd(nullptr), mpCapacity(nullptr),
          mAllocator(allocator) {}

    void push_back(const T& value) {
        if (mpEnd == mpCapacity) {
            grow(mAllocator);  // 인스턴스 할당자 사용
        }
        ::new((void*)mpEnd) T(value);
        ++mpEnd;
    }

private:
    void grow(Allocator& alloc) {
        const size_type newCap = (mpCapacity == mpBegin)
            ? 1
            : 2 * (mpCapacity - mpBegin);

        T* newData = (T*)alloc.allocate(newCap * sizeof(T));
        // ...
    }
};
```

**게임에서의 활용:**

게임은 레벨별, 시스템별로 메모리를 분리 관리한다:

```cpp
class GameLevel {
    MemoryArena m_persistentArena;  // 레벨이 살아있는 동안
    MemoryArena m_frameArena;       // 매 프레임 리셋

public:
    void load() {
        ArenaAllocator persistentAlloc(m_persistentArena);
        ArenaAllocator frameAlloc(m_frameArena);

        // 레벨 데이터: 지속적 메모리 풀
        eastl::vector<Enemy*, ArenaAllocator> enemies(persistentAlloc);
        eastl::vector<StaticMesh*, ArenaAllocator> meshes(persistentAlloc);

        // 임시 데이터: 프레임 메모리 풀
        eastl::vector<RenderCommand, ArenaAllocator> commands(frameAlloc);

        // 모든 할당이 각각의 arena에서 발생
    }

    void unload() {
        m_persistentArena.reset();  // 모든 메모리 한 번에 해제!
        // 개별 delete 불필요
    }

    void tick() {
        m_frameArena.reset();  // 프레임 시작 시 리셋
        // 이전 프레임의 모든 임시 데이터 즉시 정리
    }
};
```

여기서는 컨테이너를 `load()` 지역 변수로 두었지만, 레벨 수명을 따르게 하려면 `GameLevel`의 멤버로 두고 raw `Enemy*`/`Mesh*` pointee의 소유자도 명시해야 한다. arena `reset()`은 비-trivial 객체의 소멸자를 호출하지 않으므로 소멸이 필요한 객체는 먼저 destroy한 뒤 reset하고, GPU가 비동기로 참조하는 자원은 frame-in-flight가 끝난 뒤에만 reset한다.

**이점:**

1. **빠른 할당/해제**: Arena allocator는 단순한 포인터 증가
2. **캐시 친화적**: 관련 데이터가 메모리에 인접
3. **외부 단편화 없음**: 레벨 전체를 한 번에 해제 (예약분의 내부 낭비는 남음)
4. **디버깅 용이**: 메모리 릭을 arena 단위로 추적

**EASTL의 추가 최적화:**

```cpp
// 1. Fixed vector (SmallVector와 유사)
eastl::fixed_vector<int, 16> fv;
// 16개까지 스택 저장, 오버플로 시 힙

// 2. 이동 최적화 (할당자 비교)
vector& operator=(vector&& x) {
    if (mAllocator == x.mAllocator) {
        // 할당자가 같으면 포인터만 swap (O(1))
        swap(mpBegin, x.mpBegin);
        swap(mpEnd, x.mpEnd);
        swap(mpCapacity, x.mpCapacity);
    } else {
        // 다르면 요소별 이동 (O(n))
        // 메모리는 원래 할당자에서 해제되어야 함
    }
}

// 3. has_trivial_relocate 지원
template<typename T>
struct has_trivial_relocate {
    static const bool value = is_pod<T>::value;
};
// TArray처럼 memcpy 최적화 가능
```

### Godot Vector: COW (Copy-on-Write)의 양날의 검

Godot Engine은 스크립팅 언어(GDScript)와의 통합을 고려한 독특한 접근을 택했다.

**COW의 기본 아이디어:**

복사 시 실제 데이터를 복사하지 않고 참조만 증가시키고, 쓰기가 발생하면 그때 복사한다.

```cpp
// COW 동작을 설명하기 위한 개념 코드. 실제 Godot 내부 레이아웃과 동일하지 않다.
template <typename T>
class Vector {
    struct VectorData {
        std::atomic<int> refcount;
        int size;
        int capacity;
        T* data;
    };

    VectorData* _data;

public:
    Vector() : _data(nullptr) {}

    // 복사는 포인터 공유만
    Vector(const Vector& other) : _data(other._data) {
        if (_data) {
            _data->refcount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 쓰기 시 복사 (Copy-on-Write)
    void set(int index, const T& value) {
        _cow();  // 필요시 복사
        _data->data[index] = value;
    }

    T get(int index) const {
        return _data->data[index];  // 읽기는 복사 불필요
    }

private:
    void _cow() {
        if (!_data || _data->refcount.load() == 1) {
            return;  // 유일한 소유자, 복사 불필요
        }

        // 공유 중이면 복사
        VectorData* new_data = _allocate(_data->capacity);
        new_data->size = _data->size;
        new_data->capacity = _data->capacity;
        new_data->refcount.store(1);

        for (int i = 0; i < _data->size; ++i) {
            new_data->data[i] = _data->data[i];
        }

        _unref();  // 기존 데이터 참조 감소
        _data = new_data;
    }

    void _unref() {
        if (_data && _data->refcount.fetch_sub(1) == 1) {
            _deallocate(_data);  // 마지막 참조였으면 해제
        }
    }
};
```

**장점:**

```cpp
// 복사가 저렴함 (O(1))
Vector<int> v1 = {1, 2, 3, 4, 5};
Vector<int> v2 = v1;  // 포인터 복사, refcount++
Vector<int> v3 = v1;  // 또 포인터 복사
// 메모리: 단일 VectorData만 할당됨

// 읽기도 저렴
int x = v1.get(0);  // backing 배열 복사 없음
int y = v2.get(0);  // backing 배열 복사 없음
```

**단점:**

```cpp
// 첫 쓰기가 비쌈 (O(n) 복사)
v2.set(0, 100);
// _cow() 호출 → 전체 배열 복사

// v1이 이미 1000000개 원소를 가진다는 전제에서 반복 쓰기 비용 관찰
for (int i = 0; i < 1000000; ++i) {
    v1.set(i, i);
    // 매번 refcount 체크 (atomic load)
    // 첫 번째만 복사하지만 체크는 항상 발생
}

// 캐시 지역성 저하
Vector<int> arr[100];
for (int i = 0; i < 100; ++i) {
    arr[i] = create_vector();
    // 각각 다른 힙 위치
    // 순차 접근해도 캐시 미스
}
```

**Godot의 선택 이유:**

GDScript의 `Array`는 참조 시맨틱스를 따른다. 배열의 구성 목록을 따로 보관하려면 `duplicate()`로 복사한다:

```gdscript
var enemies = [enemy1, enemy2, enemy3]

func backup_state():
    var backup = enemies.duplicate()  # 배열 자체의 얕은 복사본
    # duplicate() 없이 대입하면 같은 Array를 공유한다
    return backup

func modify_enemies():
    enemies.append(enemy4)  # enemies만 변경되고 backup은 그대로
```

`duplicate()`의 기본값은 얕은 복사이므로 배열 원소인 enemy 객체의 내부 상태까지 저장하는 스냅샷은 아니다. 중첩 Array/Dictionary는 `duplicate(true)`로 복제할 수 있지만 Object 파생 원소는 별도의 복제·상태 저장 계약이 필요하다. C++ 내부 `Vector`의 COW와 스크립트 `Array`의 참조 시맨틱스는 구분해서 봐야 한다.

**성능 트레이드오프 정리:**

| 패턴 | std::vector | TArray | EASTL | Godot COW |
|------|------------|--------|-------|-----------|
| 복사 | O(n) | O(n) | O(n) | O(1) |
| 읽기 | O(1) | O(1) | O(1) | O(1) |
| 첫 쓰기 (공유 시) | O(1) | O(1) | O(1) | O(n) |
| 멀티스레드 (읽기 전용) | 안전 | 안전 | 안전 | 공유 시 atomic refcount |
| 멀티스레드 (구조 변경) | 별도 동기화 | 별도 동기화 | 별도 동기화 | 별도 동기화 |

atomic 참조 카운트는 pointee의 동시 수정이나 컨테이너 구조 변경을 보호하지 않는다. 동시 접근 계약은 컨테이너별로 다르며, 같은 컨테이너의 구조 변경과 동일 원소 수정에는 별도 동기화가 필요하다.

## 6. 핵심 함수 구현 깊이 파기

이론을 넘어 실제로 vector를 구현해보자. 예외 안전성, 이동 시맨틱스, placement new 등 C++의 고급 기법들이 총동원된다.

### push_back: 단순하지만 복잡한

**기본 구현:**

```cpp
template<typename T>
class vector {
public:
    void push_back(const T& value) {
        if (m_end == m_end_of_storage) {
            // 재할당 필요
            reserve(capacity() == 0 ? 1 : capacity() * 2);
        }

        // placement new로 복사 생성
        ::new (static_cast<void*>(m_end)) T(value);
        ++m_end;
    }

    void push_back(T&& value) {
        if (m_end == m_end_of_storage) {
            reserve(capacity() == 0 ? 1 : capacity() * 2);
        }

        // placement new로 이동 생성
        ::new (static_cast<void*>(m_end)) T(std::move(value));
        ++m_end;
    }
};
```

이 축약에서 `value`가 이 벡터의 원소(예: `v.push_back(v[i])`)를 가리키면 `reserve`의 재할당 뒤 `value`가 dangling이 된다. 표준 구현은 새 자리를 먼저 확보한 뒤 원소를 생성해 이 경우도 처리한다.

왜 `new T(value)`가 아니라 `::new (m_end) T(value)` 형태를 쓸까.

일반 `new`는 두 가지를 수행한다:
1. 메모리 할당
2. 객체 생성

하지만 vector는 이미 메모리를 할당했다(`reserve`). 따라서 **placement new**를 사용하여 이미 할당된 메모리에 객체만 생성한다.

**예외 안전성 강화:**

```cpp
void push_back(const T& value) {
    if (m_end == m_end_of_storage) {
        // 강력한 예외 보장 (strong exception guarantee)
        size_type new_cap = capacity() == 0 ? 1 : capacity() * 2;

        T* new_begin = m_alloc.allocate(new_cap);
        T* new_slot = new_begin + size();   // 새 요소가 들어갈 맨 뒤 자리
        T* new_end = new_begin;
        bool slot_built = false;

        try {
            // 1. 새 요소를 맨 뒤 자리에 먼저 생성
            ::new (static_cast<void*>(new_slot)) T(value);
            slot_built = true;

            // 2. 기존 요소들을 앞에서부터 옮긴다.
            for (T* p = m_begin; p != m_end; ++p, ++new_end) {
                // 이동이 noexcept가 아니고 복사 가능하면 복사해 원본을 보존한다.
                if constexpr (std::is_nothrow_move_constructible_v<T> ||
                              !std::is_copy_constructible_v<T>) {
                    ::new (static_cast<void*>(new_end)) T(std::move(*p));
                } else {
                    ::new (static_cast<void*>(new_end)) T(*p);
                }
            }
            ++new_end;  // 맨 뒤의 새 요소까지 포함
        } catch (...) {
            // 예외 발생 시 정리
            while (new_end != new_begin) {
                --new_end;
                new_end->~T();  // 소멸자 명시적 호출
            }
            if (slot_built) new_slot->~T();
            m_alloc.deallocate(new_begin, new_cap);
            throw;  // 예외 재전파
        }

        // 3. 성공 시에만 기존 요소 소멸 및 교체
        for (T* p = m_begin; p != m_end; ++p) {
            p->~T();
        }

        if (m_begin) {
            m_alloc.deallocate(m_begin, capacity());
        }

        m_begin = new_begin;
        m_end = new_end;
        m_end_of_storage = new_begin + new_cap;
    } else {
        // 재할당 불필요한 경우
        ::new (static_cast<void*>(m_end)) T(value);
        ++m_end;
    }
}
```

이 구현은 복사가 가능하거나 이동이 `noexcept`인 조건에서 **강력한 예외 보장(strong exception guarantee)** 을 제공한다. 즉, `push_back`이 예외를 던지면 벡터는 호출 전 상태를 유지한다. 별도의 rvalue 삽입이나 `reserve`에서 복사가 불가능한 move-only 타입의 이동 생성자가 예외를 던지면 원본이 이미 moved-from일 수 있다. 표준은 이 경우 효과를 미지정으로 두므로 “언제나 기본 보장은 남는다”고 가정해서는 안 된다.

### reserve: 용량 확장의 핵심

```cpp
void reserve(size_type new_cap) {
    if (new_cap <= capacity()) {
        return;  // 이미 충분한 용량
    }

    T* new_begin = m_alloc.allocate(new_cap);
    T* new_end = new_begin;
    const size_type old_size = size();  // 기존 storage 해제 전에 크기를 저장한다

    try {
        // 기존 요소 이동
        for (T* p = m_begin; p != m_end; ++p, ++new_end) {
            // 복사할 수 없거나 이동이 noexcept면 이동, 그 밖에는 복사
            if constexpr (std::is_nothrow_move_constructible_v<T> ||
                          !std::is_copy_constructible_v<T>) {
                ::new (static_cast<void*>(new_end)) T(std::move(*p));
            } else {
                ::new (static_cast<void*>(new_end)) T(*p);
            }
        }
    } catch (...) {
        // 예외 발생 시 생성된 요소 정리
        while (new_end != new_begin) {
            --new_end;
            new_end->~T();
        }
        m_alloc.deallocate(new_begin, new_cap);
        throw;
    }

    // 기존 요소 소멸
    for (T* p = m_begin; p != m_end; ++p) {
        p->~T();
    }

    // 기존 메모리 해제
    if (m_begin) {
        m_alloc.deallocate(m_begin, capacity());
    }

    // 포인터 갱신
    m_begin = new_begin;
    m_end = new_begin + old_size;
    m_end_of_storage = new_begin + new_cap;
}
```

**핵심 결정: 이동 vs 복사**

```cpp
if constexpr (std::is_nothrow_move_constructible_v<T>) {
    // 이동 생성자가 예외를 안 던진다고 보장됨
    ::new (new_end) T(std::move(*p));
} else {
    // 이동 생성자가 예외를 던질 수 있음
    // 강력한 예외 보장을 위해 복사 사용
    ::new (new_end) T(*p);
}
```

왜 이런 분기가 필요할까? 이동 중 예외가 발생하면 원본 객체가 이미 변경되었을 수 있어 복구가 불가능하다. 복사는 원본을 보존하므로 예외 발생 시 원래 상태로 돌아갈 수 있다.

이것이 "이동 생성자는 `noexcept`로 선언하라"는 권고의 이유이다.

### emplace_back: 완벽한 전달

`push_back`은 객체를 생성한 후 복사/이동한다. `emplace_back`은 객체를 벡터 내부에서 직접 생성한다.

```cpp
template<typename... Args>
reference emplace_back(Args&&... args) {
    if (m_end == m_end_of_storage) {
        // 재할당 필요
        size_type new_cap = capacity() == 0 ? 1 : capacity() * 2;
        T* new_begin = m_alloc.allocate(new_cap);
        T* new_end = new_begin + size();

        try {
            // 직접 생성 (복사/이동 없음!)
            ::new (static_cast<void*>(new_end)) T(std::forward<Args>(args)...);

            // 기존 요소 이동
            T* dest = new_begin;
            for (T* p = m_begin; p != m_end; ++p, ++dest) {
                ::new (static_cast<void*>(dest)) T(std::move(*p));
            }
        } catch (...) {
            // ... 예외 처리
        }

        // ... 정리 및 갱신
        m_begin = new_begin;
        m_end = new_end + 1;
        m_end_of_storage = new_begin + new_cap;

        return *new_end;
    } else {
        // 공간이 충분한 경우
        ::new (static_cast<void*>(m_end)) T(std::forward<Args>(args)...);
        return *m_end++;
    }
}
```

이 `emplace_back`은 예외 정리와 포인터 갱신의 세부를 `...`으로 생략한 의사코드다. 실제 구현은 완성된 `push_back`처럼 새 원소를 먼저 만들고, 실패하면 새 storage만 정리한 뒤 예외를 재전파한다.

**성능 차이 실험:**

```cpp
struct Widget {
    int id;
    std::string name;

    Widget(int i, std::string n) : id(i), name(std::move(n)) {
        std::cout << "Widget constructed\n";
    }

    Widget(const Widget& other) : id(other.id), name(other.name) {
        std::cout << "Widget copied\n";
    }

    Widget(Widget&& other) noexcept
        : id(other.id), name(std::move(other.name)) {
        std::cout << "Widget moved\n";
    }
};

vector<Widget> v;

// push_back: 임시 객체 생성 → 이동
Widget w(1, "temp");
v.push_back(std::move(w));
// 출력:
// Widget constructed
// Widget moved

// emplace_back: 직접 생성!
v.emplace_back(2, "direct");
// 출력:
// Widget constructed
// Widget moved      (용량이 1→2로 늘며 기존 원소가 이동한다)
```

`emplace_back(args...)`는 **새 원소**를 목적 위치에 직접 생성하므로 호출자가 별도 임시 객체를 만든 경우의 이동을 피할 수 있다. 그러나 용량 증가가 발생하면 기존 원소의 이동·복사 또는 구현이 허용한 bytewise relocation 비용은 여전히 생긴다. `push_back(T(...))`도 copy elision과 이동 최적화의 영향을 받으므로, 두 API 사이에 보편적인 큰 성능 차이가 있다고 가정하지 않는다.

### 이동 생성자와 이동 대입

일반적인 `std::vector`의 이동 생성은 allocator를 함께 이동하고 저장소 소유권을 넘겨 상수 시간에 끝날 수 있다. 이동 대입은 allocator 전파 규칙과 두 allocator의 동등성에 따라 저장소를 넘기지 못하고 원소별 이동을 수행할 수 있으므로, 모든 이동 연산을 단순한 포인터 교환으로 일반화할 수 없다:

```cpp
// 이동 생성자
vector(vector&& other)
    noexcept(std::is_nothrow_move_constructible_v<Allocator>)
    : m_begin(other.m_begin),
      m_end(other.m_end),
      m_end_of_storage(other.m_end_of_storage),
      m_alloc(std::move(other.m_alloc))
{
    // other를 빈 상태로 만듦
    other.m_begin = nullptr;
    other.m_end = nullptr;
    other.m_end_of_storage = nullptr;
}

// 이동 대입 연산자
vector& operator=(vector&& other)
    noexcept(AllocatorTraits::propagate_on_container_move_assignment::value ||
             AllocatorTraits::is_always_equal::value) {
    if (this != &other) {
        if constexpr (AllocatorTraits::propagate_on_container_move_assignment::value) {
            release_storage();
            m_alloc = std::move(other.m_alloc);
            steal_storage(other);  // O(1)
        } else if (m_alloc == other.m_alloc) {
            release_storage();
            steal_storage(other);  // O(1)
        } else {
            assign(std::make_move_iterator(other.begin()),
                   std::make_move_iterator(other.end()));  // O(n), 예외 가능
            other.clear();
        }
    }
    return *this;
}
```

**성능:**

```cpp
// 복사: O(n) 시간, n개 요소 복사
vector<int> v1(1000000);
vector<int> v2 = v1;  // 1백만 번 복사

// 기본 allocator를 사용하는 이동 생성: O(1), 저장소 소유권 이전
vector<int> v3 = std::move(v1);  // 포인터 3개만
```

allocator 인수를 받지 않는 `std::vector` 이동 생성은 저장소를 넘겨 상수 시간이다. 이동 대입과 allocator 인수를 받는 이동 생성은 allocator 전파·동등성 조건이 맞지 않으면 원소별 이동으로 $O(n)$이 될 수 있다.

## 7. 실전 최적화 기법

### reserve로 재할당 회피

가장 간단하지만 강력한 최적화:

```cpp
// 나쁜 코드: 여러 번 재할당
vector<int> v;
for (int i = 0; i < 1000; ++i) {
    v.push_back(i);  // log(1000) ≈ 10번 재할당
}

// 좋은 코드: 단 한 번 할당
vector<int> v;
v.reserve(1000);
for (int i = 0; i < 1000; ++i) {
    v.push_back(i);  // 재할당 없음
}
```

`reserve`의 이득은 성장 중 발생했을 재할당 횟수와 원소 이동 비용에 비례한다. 최종 크기를 정확히 아는 경우 allocation count를 한 번으로 제한하고 iterator/reference invalidation도 피하지만, 처리 시간이 항상 일정 배율로 줄어드는 것은 아니다.

### shrink_to_fit으로 메모리 절약

과도한 용량을 제거:

```cpp
vector<int> v;
v.reserve(10000);
// ... 사용
v.resize(100);  // 실제로는 100개만 사용

// capacity는 여전히 10000
// 9900개 분량의 메모리 낭비!

v.shrink_to_fit();  // 축소 요청. 실제 capacity 감소는 구현에 달림
```

주의: `shrink_to_fit`은 **비구속적 요청**이다. 구현이 수행하면 재할당과 원소 이동이 발생할 수 있고, 거부하면 capacity가 그대로일 수 있으므로 빈번히 호출하거나 즉시 반환을 전제로 해서는 안 된다.

### swap trick (C++11 이전)

```cpp
vector<int> v;
// ... 사용 후

vector<int>().swap(v);  // v를 빈 벡터와 swap
// 이전 메모리 즉시 해제
```

C++11 이후에는 `v.clear(); v.shrink_to_fit();`으로 축소를 요청하거나 `v = std::vector<int>{};`처럼 빈 벡터를 대입할 수 있다. 어느 경우에도 allocator의 메모리가 즉시 OS로 반환된다고 보장되지는 않는다.


---

## 결론

vector의 본질은 세 개의 포인터로 표현할 수 있는 연속 메모리 추상이다. 이 단순한 표현 위에 분할 상환 $O(1)$의 `push_back`, 성장 인자의 시간-공간 트레이드오프, 무효화 규칙, 그리고 SVO와 trivially relocatable 같은 구현 기법이 얹힌다. `reserve`로 재할당을 줄이고 새 원소를 목적 위치에 직접 만들 필요가 있을 때 `emplace_back`을 사용하며, 이동 생성자가 실제로 예외를 던지지 않는 타입이라면 `noexcept`로 그 계약을 표현한다. 최종 효과는 워크로드에서 측정한다.

게임 엔진이나 컴파일러 인프라처럼 도메인 가정이 분명한 곳에서는 `TArray`의 `memcpy` 재배치, `SmallVector`의 인라인 버퍼, `FBVector`의 `goodMallocSize` 같은 개별 최적화가 의미를 가진다. 다만 이런 선택은 모두 안전성과 메모리 사용량의 일부를 내준 결과다. 기본 도구는 `std::vector`로 두고, 프로파일링으로 병목이 드러난 자리에만 도메인 가정을 박은 변종을 도입하는 순서가 합리적이다.

여기까지의 내용은 연속 배열 기반 자료구조 전반의 분석에 그대로 옮겨 쓸 수 있다. 충돌과 빈 슬롯 관리가 본질인 해시 테이블, 노드 단위 할당이 비용의 중심이 되는 연결 리스트, 엔티티 단위 SoA 레이아웃을 다루는 ECS 모두 동일한 메모리 모델 위에서 트레이드오프를 다시 그어내는 작업이다.


## 더 읽을거리

- cppreference의 [std::vector](https://en.cppreference.com/w/cpp/container/vector)와 [vector] 표준 초안: <https://eel.is/c++draft/vector>
- 표준 라이브러리 구현: [GCC libstdc++ stl_vector.h](https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/bits/stl_vector.h), [LLVM libc++ vector](https://github.com/llvm/llvm-project/blob/main/libcxx/include/vector), [MSVC STL vector](https://github.com/microsoft/STL/blob/main/stl/inc/vector)
- 커스텀 구현: [Folly FBVector](https://github.com/facebook/folly/blob/main/folly/docs/FBVector.md), [LLVM SmallVector.h](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/SmallVector.h), [EASTL vector.h](https://github.com/electronicarts/EASTL/blob/master/include/EASTL/vector.h)
- 예외 안전성: Herb Sutter, *Exceptional C++* (Addison-Wesley, 1999); Dave Abrahams, "Exception-Safety in Generic Components" (1998)
- 최신 제안: [P1144 std::is_trivially_relocatable](https://wg21.link/p1144), [P0843 inplace_vector](https://wg21.link/p0843)
