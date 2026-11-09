## 들어가며

문자열은 프로그래밍에서 가장 기본적인 자료구조다. 하지만 그 "기본적임" 뒤에는 놀라울 정도로 복잡한 설계 결정이 숨어있다. C 언어가 왜 null-terminated 방식을 선택했는지, `std::string`은 어떻게 작은 문자열을 최적화하는지, Copy-on-Write가 왜 C++11에서 금지되었는지를 이해하는 것은 단순히 문자열을 넘어서 시스템 프로그래밍의 핵심 원칙을 이해하는 과정이다.

이 글에서는 문자열의 역사부터 현대 C++의 최적화 기법, 그리고 BSTR 같은 Windows 특화 length-prefix 구현까지, 깊이 있는 "왜"와 "어떻게"를 풍부한 코드 예제와 함께 탐구한다. 본문이 끝날 무렵에는 `strcat`이 왜 $O(n^2)$인지, SSO가 왜 효과적인지, 그리고 BSTR이 length-prefix를 선택한 실용적 이유를 모두 설명할 수 있게 된다.


---

## 목차
1. [C 문자열: 1970년대의 선택](#1-c-문자열-1970년대의-선택)
2. [Shlemiel the Painter's Algorithm: strcat의 비극](#2-shlemiel-the-painters-algorithm-strcat의-비극)
3. [std::string: 문자열을 클래스로](#3-stdstring-문자열을-클래스로)
4. [SSO: Small String Optimization](#4-sso-small-string-optimization)
5. [COW의 부상과 몰락](#5-cow의-부상과-몰락)
6. [std::string_view: 소유권 없는 문자열](#6-stdstring_view-소유권-없는-문자열)
7. [Length-prefix 실제 사례: BSTR](#7-length-prefix-실제-사례-bstr)


---

## 1. C 문자열: 1970년대의 선택

### Null-terminated vs Length-prefixed

C 언어의 문자열은 null 문자(`\0`)로 끝나는 문자 배열이다. 이것이 얼마나 자연스러워 보이는지와 별개로, 이는 여러 대안 중 하나의 설계 결정이었다.

```c
// C String: Null-terminated
char str[] = "Hello";
// 메모리: ['H', 'e', 'l', 'l', 'o', '\0']
// strlen()은 '\0'을 찾을 때까지 순회해야 한다
```

대조적으로, Pascal은 길이를 문자열 앞에 저장하는 length-prefixed 방식을 사용했다.

```pascal
// Pascal String: Length-prefixed
// 메모리: [5, 'H', 'e', 'l', 'l', 'o']
```

이 두 접근법의 차이는 단순한 구현 디테일이 아니다. 각각은 근본적으로 다른 트레이드오프를 내포한다.

**Null-terminated의 장점:**
- 길이 필드 대신 종료자 1코드 단위만 쓴다 (별도 길이 필드 불필요)
- 문자열 리터럴과 자연스럽게 매칭된다
- 서픽스(끝까지의 부분 문자열)를 포인터 이동만으로 만들 수 있다 (임의 구간은 끝 종료자가 별도로 필요)
- C 언어의 단순함과 부합한다 (구조체가 필요 없음)

**Null-terminated의 단점:**
- 길이 계산이 $O(n)$이다 (매번 순회 필요)
- Null 문자를 데이터로 포함할 수 없다
- 버퍼 오버런에 취약하다
- 문자열 연결 같은 연산이 $O(n^2)$으로 퇴화한다

### 역사적 맥락: 왜 C는 Null-terminated를 선택했는가

이 선택은 PDP-11의 메모리 부족만으로 설명하기보다 C의 선행 언어인 BCPL과 B에서 이어진 표현을 봐야 한다. Ritchie의 [설계 회고](https://www.nokia.com/bell-labs/about/dennis-m-ritchie/chist.pdf)에 따르면 BCPL은 첫 packed byte에 길이를 넣었고, B는 길이 대신 `*e`로 표기한 종료 문자를 사용했다. 그는 이 변화의 이유로 8·9비트 길이 필드의 문자열 길이 제한을 피하는 점과, 길이를 계속 관리하기보다 종료자를 쓰는 편이 편리했던 경험을 든다.

C의 `\0` 종료 표현도 이 계보 안에 있다. 별도 길이 필드 없이 문자 배열과 포인터로 다루기 쉽지만, 길이를 알아내려면 종료자까지 읽어야 한다는 비용을 함께 물려받았다. 특정 하드웨어가 유일하게 강제한 선택이나 모든 조건에서 이상적인 표현은 아니었다.

하지만 이 선택은 대가를 치렀다. 그리고 그 대가는 50년이 지난 지금도 지불되고 있다.

## 2. Shlemiel the Painter's Algorithm: strcat의 비극

2001년, Joel Spolsky는 그의 [유명한 블로그 글](https://www.joelonsoftware.com/2001/12/11/back-to-basics/)에서 `strcat`의 문제를 Shlemiel the Painter라는 비유로 설명했다.

> Shlemiel은 페인트통과 붓을 들고 도로에 선을 그린다. 매일 출발점에서 시작해 페인트를 칠하다가, 페인트가 떨어지면 다시 출발점까지 돌아가 페인트를 채운다. 매일 작업 거리가 멀어질수록 왕복 시간이 길어져 실제 칠하는 양은 갈수록 줄어든다.

이것이 정확히 `strcat`이 하는 일이다.

### strcat의 구현

```c
char* strcat(char* dest, const char* src) {
    char* ptr = dest;

    // 1단계: dest의 끝을 찾는다 - O(n)
    while (*ptr != '\0') {
        ptr++;
    }

    // 2단계: src를 복사한다 - O(m)
    while (*src != '\0') {
        *ptr++ = *src++;
    }
    *ptr = '\0';

    return dest;
}
```

문제는 1단계에 있다. `strcat`을 호출할 때마다, 함수는 이미 알고 있어야 할 정보(문자열의 끝 위치)를 다시 계산해야 한다.

### 왜 $O(n^2)$인가

이제 실제 사용 예를 보자.

```c
char buffer[1000] = "";
for (int i = 0; i < 100; i++) {
    strcat(buffer, "x");  // 매번 buffer 전체를 순회
}
```

각 호출에서 일어나는 일은 다음과 같다.

- 1번째 호출: 0개 문자 스캔
- 2번째 호출: 1개 문자 스캔
- 3번째 호출: 2개 문자 스캔
- n번째 호출: $(n-1)$개 문자 스캔

총 시간 복잡도는 다음과 같다.

$$
T(n) = 0 + 1 + 2 + \cdots + (n-1) = \frac{n(n-1)}{2} = O(n^2)
$$

이것은 이론이 아니다. 실제로 측정해보자.

```cpp
#include <chrono>
#include <iostream>
#include <cstring>

int main() {
    constexpr int N = 10000;
    char buffer[N * 2] = "";

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < N; i++) {
        strcat(buffer, "x");  // O(n²)
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Time: " << duration.count() << "ms\n";
}
```

이 루프가 수행하는 누적 스캔은 $N(N-1)/2$바이트다. 절대 시간은 컴파일러 최적화, C 라이브러리 구현, CPU와 `N`에 따라 크게 달라지므로 실행 환경과 최적화 옵션을 함께 기록해야 한다. 최종 `buffer`를 출력하거나 checksum에 넣어 측정 대상이 최적화로 제거되지 않게 한다. 작은 `N`에서 나온 한 번의 시간을 보편적인 수치로 제시하는 대신 여러 크기에서 $N^2$에 가까운 증가 추세를 확인한다.

### 해결책 1: 길이를 저장하라

가장 직관적인 해결책은 문자열의 길이를 명시적으로 저장하는 것이다.

```c
typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} String;

// src는 s->data 바깥의 문자열이어야 한다(자기 자신에 대한 별칭 금지).
void string_append(String* s, const char* src) {
    size_t src_len = strlen(src);

    if (src_len > SIZE_MAX - s->length - 1) return;  // 길이 overflow 방지
    const size_t required = s->length + src_len + 1;

    // 길이를 이미 알고 있으므로 끝 위치를 즉시 계산 - O(1)
    if (required > s->capacity) {
        // 재할당 필요. overflow 없이 요구량 이상으로 키운다.
        size_t new_capacity = s->capacity + src_len;
        if (new_capacity < s->capacity || new_capacity > SIZE_MAX / 2)
            new_capacity = required;
        else
            new_capacity *= 2;
        if (new_capacity < required) new_capacity = required;

        char* new_data = (char*)malloc(new_capacity);
        if (!new_data) return;  // 할당 실패 시 s를 변경하지 않는다
        memcpy(new_data, s->data, s->length);
        free(s->data);
        s->data = new_data;
        s->capacity = new_capacity;
    }

    memcpy(s->data + s->length, src, src_len);
    s->length += src_len;
    s->data[s->length] = '\0';
}
```

길이가 `src_len`인 한 번의 append는 복사 때문에 $O(src\_len)$이며, 재할당이 일어나면 기존 문자열까지 복사한다. 용량을 기하급수적으로 늘리면 기존 데이터 재복사의 누적 비용이 전체 입력 바이트 수에 대해 선형이 되므로, **문자 한 개 append**를 기준으로는 상각 $O(1)$이다.

### 해결책 2: 끝 포인터를 반환하라

POSIX 환경에는 이 인터페이스 문제를 해결하는 `stpcpy`가 있다. ISO C 표준 함수는 아니다.

```c
char* stpcpy(char* dest, const char* src) {
    while ((*dest = *src) != '\0') {
        dest++;
        src++;
    }
    return dest;  // '\0' 위치 반환 (strcpy는 dest를 반환)
}

// 사용 예
char buffer[1000];
char* end = buffer;
for (int i = 0; i < 100; i++) {
    end = stpcpy(end, "x");  // O(n)으로 개선
}
```

`stpcpy`는 복사가 끝난 위치의 포인터를 반환하므로, 다음 호출에서 다시 처음부터 스캔할 필요가 없다.

하지만 `stpcpy`는 POSIX 확장이고, 표준 `strcpy`/`strcat`보다 훨씬 덜 알려져 있다. 이것이 API 설계가 중요한 이유다. 잘못된 인터페이스는 사용자가 비효율적인 코드를 작성하도록 유도한다.

## 3. std::string: 문자열을 클래스로

C++는 문자열을 클래스로 캡슐화해서 C의 문제를 해결하려 했다. `std::string`의 핵심 설계를 이해하기 위해, 간단한 버전을 만들어보자.

### 기본 구조

```cpp
class String {
private:
    char* data_;       // 힙에 할당된 문자 배열 포인터
    size_t length_;    // 현재 문자열 길이 (null 제외)
    size_t capacity_;  // 할당된 버퍼 크기 (null 포함)

public:
    String() : data_(new char[1]), length_(0), capacity_(1) {
        data_[0] = '\0';
    }

    String(const char* str) {
        length_ = std::strlen(str);
        capacity_ = length_ + 1;
        data_ = new char[capacity_];
        std::memcpy(data_, str, capacity_);
    }

    // 소유 포인터를 가진 클래스는 복사·이동을 직접 정의해야 한다.
    // 정의하지 않으면 암시적 얕은 복사가 같은 버퍼를 두 번 해제한다.
    String(const String& other)
        : data_(new char[other.length_ + 1]),
          length_(other.length_),
          capacity_(other.length_ + 1) {
        if (other.data_) std::memcpy(data_, other.data_, length_ + 1);
        else data_[0] = '\0';  // moved-from 상태도 빈 문자열로 복사한다
    }

    String(String&& other) noexcept
        : data_(other.data_), length_(other.length_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.length_ = 0;
        other.capacity_ = 0;
    }

    // 소유 포인터를 가진 클래스이므로 대입은 명시적으로 막아 둔다(얕은 복사 방지).
    String& operator=(const String&) = delete;
    String& operator=(String&&) = delete;

    ~String() {
        delete[] data_;
    }

    size_t length() const { return length_; }
    const char* c_str() const { return data_ ? data_ : ""; }
};
```

이미 C 문자열보다 개선된 점이 보인다.

1. 길이가 $O(1)$에 조회 가능
2. RAII로 메모리 자동 관리
3. 캡슐화로 내부 구현 변경 가능

### 왜 length와 capacity를 분리하는가

`length_`와 `capacity_`를 따로 관리하는 것은 중요한 최적화다. 재할당은 비용이 크므로, 미리 여유 공간을 확보해두면 연속된 추가 연산에서 재할당 횟수를 줄일 수 있다.

```cpp
void reserve(size_t new_capacity) {
    if (new_capacity <= capacity_) return;

    char* new_data = new char[new_capacity];
    if (data_) std::memcpy(new_data, data_, length_ + 1);
    else new_data[0] = '\0';  // 빈(moved-from) 상태도 지원
    delete[] data_;
    data_ = new_data;
    capacity_ = new_capacity;
}

void append(const String& other) {
    if (other.length_ > SIZE_MAX - length_ - 1)
        throw std::length_error("String too long"); // <stdexcept>
    size_t new_length = length_ + other.length_;
    if (new_length >= capacity_) {
        // 재할당 필요
        reserve(new_length + 1);
    }
    // self-append(other == *this) 시 구간이 겹치므로 memmove를 쓴다.
    std::memmove(data_ + length_, other.c_str(), other.length_ + 1);
    length_ = new_length;
}
```

하지만 정확히 얼마나 여유 공간을 확보해야 할까?

### Growth Factor: 1.5배 vs 2배

버퍼가 부족할 때 얼마나 증가시킬지는 중요한 설계 결정이다.

#### 전략 1: 2배 증가

```cpp
void reserve(size_t new_capacity) {
    if (new_capacity <= capacity_) return;

    size_t new_cap = capacity_ < 1 ? 1 : capacity_;  // capacity_ == 0에서도 진행
    while (new_cap < new_capacity) {
        if (new_cap > SIZE_MAX / 2) { new_cap = new_capacity; break; }  // overflow 방지
        new_cap *= 2;  // 2배씩 증가
    }

    char* new_data = new char[new_cap];
    std::memcpy(new_data, c_str(), length_ + 1);
    delete[] data_;
    data_ = new_data;
    capacity_ = new_cap;
}
```

장점:
- 산술이 단순함
- 단일 문자 append를 반복하는 이상화된 모델에서 capacity가 $n$에 도달하기까지 재할당 횟수가 $\Theta(\log_2 n)$

단점:
- 메모리 낭비 가능성 (최대 50% 미사용)
- 큰 문자열에서 재할당 시 메모리 파편화

#### 전략 2: 1.5배 증가

```cpp
new_cap = capacity_ + std::max(capacity_ / 2, 1);  // 1.5배, capacity 1에서도 증가
```

장점:
- 메모리 재사용 가능성 향상
- 메모리 낭비 감소 (최대 33% 미사용)

단점:
- 재할당 횟수 소폭 증가

성장률이 황금비 미만이면 과거에 해제한 모든 buffer 크기의 합이 언젠가 새 buffer 크기에 도달할 수 있다는 이상화된 산술이 성립한다(황금비와 같으면 유한 단계에서는 미달하고 극한에서만 경계에 접근한다). 그러나 allocator가 그 block들을 하나의 연속 영역으로 합칠 수 있다는 보장은 없고 size class와 다른 allocation이 개입하므로, 1.5배가 실제 주소 재사용을 보장하는 것은 아니다.

특정 64비트 release ABI의 source에서 관찰되는 대표적인 경향은 다음과 같다. `basic_string`의 growth policy는 표준 보장이 아니며 library version, 요청 크기와 ABI에 따라 달라질 수 있다.

| 구현체 | Growth Factor | 이유 |
|--------|---------------|------|
| GCC libstdc++ | 대체로 기하 성장 | source version에서 확인 |
| LLVM libc++ | 대체로 기하 성장 | source version에서 확인 |
| MSVC STL | 대체로 1.5배 계열 | source version에서 확인 |

### operator+= vs operator+

문자열 연결 연산자를 구현할 때 미묘하지만 중요한 차이가 있다.

```cpp
class String {
public:
    // operator+=: 좌변을 수정 (in-place)
    String& operator+=(const String& other) {
        append(other);
        return *this;  // 자기 자신 반환
    }

    // operator+: 새 객체 생성 (임시 객체)
    String operator+(const String& other) const {
        if (other.length_ > SIZE_MAX - length_ - 1)
            throw std::length_error("String too long"); // <stdexcept>
        String result;
        result.reserve(length_ + other.length_ + 1);
        std::memcpy(result.data_, c_str(), length_);
        std::memcpy(result.data_ + length_, other.c_str(), other.length_ + 1);
        result.length_ = length_ + other.length_;
        return result;  // 값 반환 (NRVO 가능, 생략되지 않으면 이동 생성자)
    }
};
```

성능 차이는 명확하다.

```cpp
String s1 = "Hello";
String s2 = " World";

// Case 1: operator+=  (권장)
s1 += s2;  // 1번 할당 (필요시), 1번 복사

// Case 2: operator+  (비효율)
String s3 = s1 + s2;  // 2번 할당, 2번 복사 (임시 객체 생성)
```

더 심각한 문제는 연쇄 호출이다.

```cpp
String result = s1 + s2 + s3 + s4;
// 1. temp1 = s1 + s2     (버퍼 할당, s1+s2 크기)
// 2. temp2 = temp1 + s3  (버퍼 할당, s1+s2+s3 크기)
// 3. result = temp2 + s4 (버퍼 할당, s1+s2+s3+s4 크기)
// 호출마다 2번 할당(빈 result + reserve) → 총 6번 할당, 6번 복사 - O(n²) 퇴화
```

C++11의 move semantics는 이 문제를 크게 완화한다.

```cpp
String operator+(String&& lhs, const String& rhs) {
    lhs += rhs;  // 이미 임시 객체면 재사용
    return std::move(lhs);
}
```

이제 임시 객체의 기존 버퍼를 재사용할 수 있다. 다만 매번 정확한 요구 크기로만 재할당하면 왼쪽 문자열의 반복 복사는 여전히 누적될 수 있다. 긴 append 연쇄의 상각 비용까지 줄이려면 앞에서 본 기하 성장이나 충분한 사전 reserve가 함께 필요하다.

## 4. SSO: Small String Optimization

지금까지의 설계는 합리적이지만, 치명적인 문제가 하나 있다. 모든 문자열이 힙 할당을 한다는 것이다.

### 힙 할당의 비용

힙 할당은 생각보다 훨씬 비싸다.

- `malloc` 호출: allocator와 size class에 따라 수십~수백 CPU 사이클 (메모리 관리자 오버헤드)
- 캐시 미스: 메인 메모리 접근 (수십 나노초)
- 메모리 단편화 증가
- 멀티스레드 환경에서 락 경합

일반적인 게임/툴 워크로드에서 관찰되는 가정은 다음과 같다(데이터셋에 따라 달라진다).

- 대부분의 문자열은 매우 짧다 (< 20 char 코드 단위)
- 파일명, 태그, 토큰 등은 평균 10 char 코드 단위 미만
- 작은 문자열에 힙 할당은 과도한 오버헤드

이것이 SSO (Small String Optimization)가 탄생한 배경이다.

### 아이디어: 작은 문자열은 객체 내부에 저장

SSO의 핵심 아이디어는 간단하다. 문자열이 충분히 짧으면, 포인터를 저장하는 공간에 문자열 자체를 저장하자.

```cpp
class String {
private:
    static constexpr size_t SSO_SIZE = 15;  // 15자 + '\0'

    struct LongString {
        char* data;
        size_t length;
        size_t capacity;
    };

    struct ShortString {
        char buffer[SSO_SIZE + 1];  // String 객체 내부에 직접 저장
        unsigned char length;       // 1바이트 길이
    };

    union {
        LongString long_;
        ShortString short_;
    } storage_;
    bool short_mode_ = true;

    bool is_short() const {
        return short_mode_;
    }

public:
    const char* c_str() const {
        return is_short() ? storage_.short_.buffer : storage_.long_.data;
    }

    size_t length() const {
        return is_short() ? storage_.short_.length : storage_.long_.length;
    }
};
```

이 코드는 레이아웃 원리만 보이는 축약이다. `union`은 작은 표현과 큰 표현이 같은 저장 공간을 공유하게 하고, 별도 discriminator인 `short_mode_`가 현재 활성 멤버를 나타낸다. 실제 구현은 객체 크기를 줄이기 위해 길이·용량 필드의 일부 비트를 discriminator로 재사용하기도 하지만, **활성화되지 않은 union 멤버를 읽어 모드를 판별하는 코드는 일반적인 표준 C++ 구현이 아니다**. 완전한 클래스라면 생성·복사·이동·파괴 때 활성 멤버의 수명을 명시적으로 관리해야 한다.

### 메모리 레이아웃 분석

64비트 플랫폼에서 포인터는 8바이트다. 따라서 `LongString`은 다음과 같이 24바이트를 차지한다.

| 필드 | 크기 |
|------|------|
| `data*` | 8 bytes |
| `length` | 8 bytes |
| `capacity` | 8 bytes |
| **합계** | **24 bytes** |

위의 안전한 축약 구현은 별도 `bool`과 정렬 패딩 때문에 전체 `String`이 24바이트보다 커질 수 있다. 실제 24바이트 구현은 short/long 양쪽에서 유효하도록 discriminator를 인코딩하고, 남는 바이트를 inline 문자에 사용한다. 대표적인 24바이트 목표 레이아웃을 개념적으로 나누면 다음과 같다.

| 필드 | 크기 |
|------|------|
| `buffer[16]` | 16 bytes |
| `length` | 1 byte |
| 플래그/패딩 | 7 bytes |
| **합계** | **24 bytes** |

### 실제 구현체 비교

아래 표는 `char`를 쓰는 특정 64비트 release ABI의 대표적인 snapshot이다. 표준은 SSO 자체, 객체 크기와 inline 문자 수를 보장하지 않는다. debug iterator, hardening, alternate string layout, ABI version과 `CharT`가 바뀌면 숫자도 바뀐다.

| 구현체 | SSO 한계 | 객체 크기 | 비고 |
|--------|---------:|---------:|------|
| GCC libstdc++ | 15자 | 32바이트 | 패딩 포함 |
| Clang libc++ | 22자 | 24바이트 | 공격적 비트 인코딩 |
| MSVC | 15자 | 32바이트 | - |

libc++가 같은 24바이트로 더 긴 SSO를 달성한 비결은 비트 인코딩이다. short 모드에서는 첫 바이트에 길이를 1비트 시프트해 저장하고 최하위 비트를 short/long 플래그로 쓰며, long 모드에서는 capacity 필드의 최하위 비트가 플래그 역할을 한다. 두 모드가 같은 바이트의 같은 비트를 판별자로 공유하는 구조다.

### SSO의 실무 영향

```cpp
std::string filename = "config.txt";

// allocation 발생 여부는 실제 library/ABI에서 측정한다.
```

SSO 범위 안에서는 문자열별 heap allocation과 간접 접근을 피할 수 있다. 전체 성능 차이는 allocator, 복사·이동 패턴, 문자열 처리 자체와 cache pressure에 따라 달라지므로 고정 배수로 표현하지 않는다.

### SSO 확인 실험

실제로 SSO가 작동하는지 확인해보자.

```cpp
#include <iostream>
#include <string>

int main() {
    std::string s1 = "short";  // SSO 적용
    std::string s2 = "this is a very long string that exceeds SSO threshold";

    std::cout << "s1.data() = " << static_cast<const void*>(s1.data()) << "\n";
    std::cout << "&s1       = " << &s1 << "\n";

    std::cout << "s2.data() = " << static_cast<const void*>(s2.data()) << "\n";
    std::cout << "&s2       = " << &s2 << "\n";

    // s1.data()가 &s1 근처 주소 → SSO (객체 내부)
    // s2.data()가 먼 주소 → 힙 할당
}
```

MSVC 시스템에서의 출력 예시는 다음과 같다.

```text
s1.data() = 0000008F7AFFF958
&s1       = 0000008F7AFFF958

s2.data() = 000001E3B8D55420
&s2       = 0000008F7AFFF990
```

`s1.data`와 `&s1`이 정확히 같다. 즉, 문자열 데이터가 객체 자체 안에 저장되어 있다. 반면 `s2.data`는 완전히 다른 주소를 가리킨다.

## 5. COW의 부상과 몰락

2000년대 초반, 많은 `std::string` 구현체는 Copy-on-Write (COW)라는 최적화를 사용했다. GCC의 기본 구현도 COW였다. 하지만 C++11은 COW를 사실상 금지했다. 무슨 일이 있었던 걸까?

### COW의 동작 원리

COW의 아이디어는 우아하다. 복사가 일어날 때 실제로 데이터를 복사하지 말고, 같은 데이터를 가리키게 하자. 실제 쓰기가 일어날 때만 복사하면 된다.

```cpp
class String_COW {
private:
    struct StringData {
        int ref_count;  // 참조 카운트
        size_t length;
        size_t capacity;
        char data[1];   // struct hack (진짜 FAM은 char data[]; 이며 C++ 표준에는 없다)
    };

    StringData* ptr_;

public:
    // 복사 생성자: 데이터 공유
    String_COW(const String_COW& other) {
        ptr_ = other.ptr_;
        ++ptr_->ref_count;  // 카운트 증가만
    }

    // 수정 전 복사 발생
    void modify() {
        if (ptr_->ref_count > 1) {
            // 다른 객체가 공유 중이면 복사
            StringData* new_ptr = allocate_copy(ptr_);
            --ptr_->ref_count;
            ptr_ = new_ptr;
            ptr_->ref_count = 1;
        }
        // 이제 안전하게 수정 가능
    }

    char& operator[](size_t i) {
        modify();  // 쓰기 가능성이 있으므로 미리 복사
        return ptr_->data[i];
    }
};
```

C++03 시대의 장점은 다음과 같다.

- 복사 비용 감소 (포인터 복사 + `ref_count++`)
- 메모리 절약 (여러 객체가 동일 데이터 공유)
- 읽기 전용 작업에 최적

### C++11에서 COW가 금지된 이유

#### 이유 1: 이터레이터/참조 무효화 문제

```cpp
// C++03 COW string (GCC 구버전)
std::string s1 = "Hello";
char& ref = s1[0];    // 비const 참조 노출

// libstdc++ 구 COW는 이 시점에 s1을 unshareable로 표시하므로,
// 아래 복사는 단순 공유가 아니라 깊은 복사가 될 수 있다.
std::string s2 = s1;

s1[0] = 'J';          // 분리 여부는 구현에 따라 다르다

// N2668이 문제 삼은 것은 이런 참조·이터레이터가 다른 객체의 수정이나
// 병렬 element 접근으로 무효화·경합될 수 있다는 점, 그리고 const 연산에
// 숨은 ref_count 갱신이 끼어들어 데이터 경합을 만든다는 점이다.
```

C++11 표준 ([N2668 제안서](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2668.htm))은 명확히 요구한다.

> `std::string`의 `operator[]` 비-const 버전은 진짜 참조를 반환해야 하며, 다른 객체의 수정이 해당 참조를 무효화해서는 안 된다.

COW는 이 요구사항을 만족할 수 없다.

#### 이유 2: 멀티스레딩 경합 (Data Race)

```cpp
std::string shared = "data";

// Thread 1
std::string s1 = shared;  // ref_count++
s1[0] = 'D';             // ref_count--, 새 버퍼 할당

// Thread 2
std::string s2 = shared;  // ref_count++ (동시 접근)
s2[0] = 'X';             // ref_count--
```

`ref_count`가 여러 스레드에서 동시에 수정되면 데이터 경합이 발생한다. 이를 해결하려면 다음과 같이 한다.

```cpp
std::atomic<int> ref_count;  // 원자적 연산
```

원자적 참조 수는 일반 정수 연산과 다른 비용 구조를 가진다.

- 같은 코어의 uncontended 증감도 read-modify-write와 메모리 순서 제약을 가진다.
- 여러 코어가 같은 카운터를 갱신하면 cache-line 소유권이 이동한다.
- CAS 재시도형 구현은 경합에서 실패와 backoff 비용이 생긴다.
- 실제 배수는 명령어, memory order, cache 상태와 thread 배치에 따라 달라진다.

#### 이유 3: Move Semantics와의 관계

C++11에서 가장 중요한 기능 중 하나인 move semantics는 공유 버퍼를 가리키는 핸들도 그대로 옮길 수 있다. move는 원본을 무효 객체로 만드는 것이 아니라 유효하지만 미지정 상태로 남긴다.

```cpp
String(String&& other) noexcept {
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;  // 소유권 이전, other는 유효하지만 미지정 상태
}
```

따라서 move semantics 자체가 COW를 금지하지는 않는다. Qt의 암묵적 공유 타입도 이동과 함께 쓰인다. `std::string`에서 COW가 금지된 직접적인 이유는 이유 1·2의 참조·이터레이터 무효화와 데이터 경합 계약이며, 이 절은 move와의 양립 불가라는 오해를 정리한 것이다.

#### 이유 4: 실제 성능 저하

COW가 항상 느리다는 결론은 아니다. 복사 후 읽기만 하는 workload에서는 이득일 수 있지만, 다음 조건은 그 이득을 줄이거나 역전시킬 수 있다.

- 짧은 문자열에서는 복사 절약보다 참조 카운팅 비용이 커질 수 있다
- 멀티코어 환경에서 false sharing 발생
- SSO 범위의 문자열은 별도 힙 버퍼 공유 없이 복사할 수 있다
- 이동으로 소유권을 넘기는 경로에서는 COW의 복사 절약 이점이 작아진다

### N2668 제안서: COW의 종말

2008년, WG21 표준위원회는 N2668 "Concurrency Modifications to Basic String" 제안서를 채택했다.

핵심은 참조·이터레이터 무효화 규칙과 데이터 경합 금지 조항을 강화해, COW 구현이 표준을 만족할 수 없게 만든 것이다. 구현체는 즉시 복사(eager copy)로 전환해야 했고, GCC libstdc++도 GCC 5에서 새 ABI(`_GLIBCXX_USE_CXX11_ABI=1`)를 기본으로 하며 COW 문자열을 기본 경로에서 제외했다. 구 ABI 문자열 구현은 호환을 위해 남아 있다.

이것은 표준의 진화가 때로는 "실수를 인정하고 수정하는 과정"임을 보여준다. COW는 20년간 유효했던 최적화였지만, 새로운 요구사항(멀티스레딩과 참조·이터레이터 무효화 규칙)과 충돌했다.

### Qt QString은 왜 여전히 COW를 사용하는가

흥미롭게도, Qt의 `QString`은 여전히 COW를 사용한다. 왜일까?

Qt의 상황:
- C++98/03 시대에 설계된 API
- Qt는 자체 멀티스레딩 모델 사용 (`QMutex`, 암묵적 공유)
- 수백만 줄의 기존 코드가 COW를 가정

Qt의 해결책은 다음과 같다.

```cpp
class QString {
    QStringData* d;

public:
    void detach() {
        if (d->ref.loadRelaxed() != 1) {
            // 공유 중이면 복사
            reallocData(d->size, d->alloc);
        }
    }

    QChar& operator[](int i) {
        detach();  // 쓰기 가능성이 있으므로 미리 분리
        return d->data()[i];
    }
};
```

Qt는 명시적으로 `detach`를 호출해서 쓰기 전에 복사한다. 이것은 `std::string`과 다른 계약이고, [Qt 문서](https://doc.qt.io/qt-6/implicit-sharing.html)에서 명시적으로 설명된다.

하지만 Qt도 변화 중이다.
- Qt 6에서도 암묵적 공유 자체는 유지되지만, 컨테이너 내부 구현이 정리되었다 (QList/QVector 통합 등)
- `QStringView` 도입 (C++17의 `std::string_view`와 유사) — 공유/복사 없이 보기만 하는 경로 확대

## 6. std::string_view: 소유권 없는 문자열

C++17은 완전히 새로운 접근법을 도입했다. `std::string_view`다. 이것은 문자열을 소유하지 않고, 단지 "보기만" 한다.

### 핵심 아이디어

```cpp
class string_view {
private:
    const char* data_;  // 포인터만 저장
    size_t size_;       // 길이만 저장

public:
    constexpr string_view(const char* s, size_t count)
        : data_(s), size_(count) {}

    constexpr string_view(const char* s)
        : data_(s), size_(std::strlen(s)) {}

    // std::string에서 암묵적 변환
    string_view(const std::string& s)
        : data_(s.data()), size_(s.size()) {}

    constexpr size_t size() const { return size_; }
    constexpr const char* data() const { return data_; }
};
```

특징:
- 크기: 16바이트 (포인터 8 + size_t 8, 64비트 기준)
- 복사 비용: $O(1)$ (포인터 2개만 복사)
- 자체 힙 할당: 없음 (view 자체는 지역 변수·멤버·컨테이너 원소 등으로 둘 수 있음)
- 소유권: 없음 (원본 데이터의 생명주기에 의존)

### Zero-copy 패턴

`string_view`의 진가는 함수 매개변수에서 드러난다.

```cpp
// 나쁜 예: 복사 발생
void process(std::string s) {  // 매개변수 복사
    if (s.find("error") != std::string::npos) {
        // ...
    }
}

std::string log = "error: file not found";
process(log);  // log 전체 복사 (SSO가 아니라면 힙 할당)

// 좋은 예: Zero-copy
void process(std::string_view sv) {  // 포인터+길이만 복사
    if (sv.find("error") != std::string_view::npos) {
        // ...
    }
}

process(log);              // 복사 없음
process("literal");        // 복사 없음
process(std::string_view(log).substr(0, 5)); // 복사 없음
// 주의: log.substr(0, 5)는 std::string을 새로 만들어 복사가 발생한다
```

`std::string::substr`는 새 문자열을 생성하지만, `std::string_view::substr`는 단지 포인터와 길이만 조정한다.

```cpp
std::string s = "Hello World";
std::string_view sv = s;

std::string_view hello = sv.substr(0, 5);  // "Hello" (복사 없음)
// hello.data() == s.data()
// hello.size() == 5
```

### 위험: 댕글링 참조

`string_view`의 가장 큰 위험은 수명 관리다. `string_view`는 소유권이 없으므로, 가리키는 데이터가 소멸되면 즉시 댕글링 포인터가 된다.

#### 위험 1: 임시 객체

```cpp
std::string_view dangerous() {
    std::string temp = "temporary";
    return temp;  // UB: temp는 함수 종료 시 소멸
}

std::string_view sv = dangerous();
std::cout << sv;  // 미정의 동작
```

#### 위험 2: 재할당

```cpp
std::string s = "Hello";
std::string_view sv = s;  // s의 버퍼 참조

s += " World";  // append는 기존 포인터·참조를 무효화할 수 있다
// 재할당되면 sv는 dangling이다. 재할당이 없다는 관찰만으로 표준상 유효성을 보장하지 않는다.
// 변경 뒤 계속 사용할 view는 s에서 다시 만든다.
```

#### 위험 3: C 문자열 생명주기

```cpp
std::string_view sv;
{
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "format %d", 42);
    sv = buffer;  // buffer 참조
}  // buffer 소멸
// sv는 댕글링 포인터
```

### 안전한 사용 패턴

```cpp
// 패턴 1: 문자열 리터럴 (정적 저장소)
std::string_view sv1 = "literal";  // OK: 프로그램 전체 생명주기

// 패턴 2: 함수 매개변수 (호출자가 호출 동안 유효한 범위를 보장해야 함)
void log(std::string_view message) {
    std::cout << message << '\n';  // OK: 호출자가 message의 수명을 보장
}

// 패턴 3: 짧은 스코프
void parse(const std::string& input) {
    std::string_view input_view = input;
    std::string_view first_line = input_view.substr(0, input_view.find('\n'));
    process(first_line);  // OK: input은 함수 종료까지 유효
}

// 피해야 할 패턴: 멤버 변수 저장
class Logger {
    std::string_view prefix_;  // 위험: 생명주기 불명확
public:
    Logger(std::string_view prefix) : prefix_(prefix) {}
    // prefix가 가리키는 데이터가 언제 소멸될지 알 수 없음
};
```

### string_view vs const string& 선택 기준

| 상황 | 권장 타입 | 이유 |
|------|-----------|------|
| 함수 매개변수 (읽기 전용) | `string_view` | 복사 없음, C 문자열도 지원 |
| 멤버 변수 | `std::string` | 생명주기 명확 |
| 반환 타입 | `std::string` | 소유권 이전 보장 |
| 임시 객체 차단 | 삭제된 `const string&&` 오버로드 | `const string&`는 임시를 허용하므로 `= delete`로 막는다 |
| Null 종료 필요 | `const char*` | `string_view`는 Null 보장 안 함 |

null 종료를 요구하는 `%s`, `strlen` 같은 API에는 `view.data()`를 그대로 넘기지 않는다. view는 null 종료를 보장하지 않으므로, 소유 `std::string`을 거치거나 길이 기반 API를 쓴다.

`string_view`는 강력하지만 위험한 도구다. Rust의 차용 검사기 같은 컴파일 타임 보증이 없는 C++에서는, 프로그래머가 생명주기를 수동으로 관리해야 한다.

## 7. Length-prefix 실제 사례: BSTR

앞에서 length-prefix 표현이 이론적으로 무엇을 사고파는지 살펴봤다. 이 표현을 실제로 채택한 타입이 있다. Windows COM의 BSTR이다. 여기서 볼 것은 API 사용법이 아니라, **길이를 어디에 두는가라는 선택이 자료구조의 나머지를 어떻게 규정하는가**다.

### 길이를 버퍼 앞에 둔다

BSTR은 `wchar_t*`이고, 포인터는 데이터의 첫 문자를 가리킨다. 길이는 그 *앞* 에 있다.

| 오프셋 | 크기 | 내용 |
|:------:|:----:|------|
| -4 | 4바이트 | 바이트 길이 (종료 null 제외) |
| 0 | 가변 | UTF-16 데이터 |
| 끝 | 2바이트 | 종료 null |

이 배치는 세 가지를 동시에 만족시킨다.

```cpp
UINT bytes = SysStringByteLen(bstr);   // null BSTR이면 0을 반환하는 COM API
```

- **길이 조회가 $O(1)$이다.** 종료 null을 찾아 훑을 필요가 없다.
- **중간에 `U+0000`이 들어가도 된다.** 길이가 별도로 저장되므로 바이너리 데이터를 그대로 담을 수 있다.
- **종료 null도 함께 둔다.** 그래서 길이를 모르는 기존 `wchar_t*` 코드에 그대로 넘길 수 있다.

단, 중간 null을 포함한 이진 데이터는 종료 null 기반 C wide 문자열 API에 넘기면 그 지점에서 잘린다. `*((UINT*)bstr - 1)` 같은 직접 헤더 역참조는 이 ABI에 한정된 관찰이며 nullptr BSTR에서는 성립하지 않으므로, 길이는 `SysStringByteLen`/`SysStringLen`으로 조회한다.

세 번째가 이 설계의 실용적 핵심이다. 길이 접두만 두고 종료 null을 버렸다면 기존 C 문자열 API와 섞어 쓸 수 없었다. 두 표현의 비용을 모두 내고 두 표현의 호환성을 모두 사는 선택이다.

| 항목 | null 종료 문자열 | BSTR |
|------|----------|------|
| 길이 확인 | $O(n)$ 탐색 | $O(1)$ 필드 조회 |
| 중간 null | 불가능 | 가능 |
| 종료 null | 있음 | 있음 |
| 고정 오버헤드 | 2바이트 | 6바이트 |

여기서 6바이트는 논리 표현의 고정 오버헤드이며, allocator의 정렬·라운딩과 할당 metadata는 포함하지 않는다.

### 오버헤드는 짧은 문자열에서 지배적이다

길이 필드 4바이트와 종료 null 2바이트를 합쳐 문자열 하나당 6바이트가 고정으로 붙는다. $n$문자(UTF-16 코드 단위 기준) 문자열의 오버헤드 비율은 다음과 같다.

$$\text{overhead} = \frac{6}{2n + 6}$$

| 문자 수 | 데이터 | 총 크기 | 오버헤드 |
|---:|---:|---:|---:|
| 5 | 10바이트 | 16바이트 | 37.5% |
| 20 | 40바이트 | 46바이트 | 13.0% |
| 100 | 200바이트 | 206바이트 | 2.9% |

짧은 문자열이 많은 워크로드에서 이 비율은 무시할 수 없다. BSTR에서는 이 비용을 줄일 방법이 없다. COM의 할당/해제 계약(`SysAllocString`/`SysFreeString`)과 포인터 하나를 넘기는 ABI 때문에 길이 필드가 데이터와 같은 할당 블록 안에 있어야 하므로 **짧은 문자열이라고 해서 할당을 생략할 수 없다.** 길이 접두 표현 자체가 인라인 저장을 금지하는 것은 아니며, `{pointer, length}` 쌍이나 opaque handle 같은 대안도 있다.

### 왜 `std::string`은 이 배치를 쓰지 않는가

`std::string`도 길이를 저장한다. 다만 **버퍼 앞이 아니라 객체 안에** 둔다. 같은 정보를 다른 곳에 두는 이 차이가 두 타입의 성질을 갈라놓는다.

| | `std::string` | BSTR |
|---|---|---|
| 길이의 위치 | 객체 내부 필드 | 버퍼 앞 4바이트 |
| 전달되는 것 | 객체 전체(포인터·길이·용량) | 포인터 하나 |
| 짧은 값 최적화 | 가능 | 불가능 |
| 경계를 넘을 수 있는가 | 같은 ABI·같은 구현 안에서만 | 언어 중립 |

갈림길은 **무엇이 경계를 넘어가는가**다. `std::string`은 값이 C++ 코드 안에 머물기 때문에 객체 표현을 마음대로 정할 수 있다. 반면 BSTR이 속한 COM 자동화 ABI는 문자열을 **포인터 하나**로 주고받고 할당/해제를 전용 함수로 고정한다. 길이를 함께 실어 보낼 자리가 없어 길이를 데이터와 같은 블록에 붙였다. 언어 중립 인터페이스가 반드시 이 형태여야 하는 것은 아니며, `{pointer, length}` 쌍이나 opaque handle을 쓰는 ABI도 있다.

여기서 BSTR에 한정된 결론이 나온다. **BSTR의 length-prefix는 "포인터 하나로 길이까지 전달해야 하는 ABI"에서 나타나는 형태다.** 그 대가로 짧은 값 최적화를 잃고, 고정 오버헤드를 내며, 길이 필드가 사용자 코드에 타입으로 드러나지 않아 규약으로만 지켜진다. 같은 `wchar_t*`라도 길이 필드가 앞에 있는 포인터와 없는 포인터를 컴파일러는 구분하지 못한다. 그래서 이 표현을 채택한 환경은 전용 생성·해제 함수를 강제하고, 그 짝을 어기면 진단되지 않는 힙 손상이 된다. 표현의 이득을 규약의 부담으로 지불하는 셈이다.

---

## 결론

문자열은 단순해 보이지만 표현, 성장, 소유권과 ABI가 한데 묶인 자료구조다. 여기서 끌어낼 통찰은 다음과 같다.

null-terminated 표현은 길이 field 없이 C 배열과 pointer 연산에 맞지만, 길이 조회와 반복 append에서 선형 탐색 비용을 만든다. `strcat`을 반복하면 매번 끝을 다시 찾아 $O(n^2)$로 퇴화할 수 있으며, 현재 end와 capacity를 유지하는 builder가 이를 피한다. SSO는 짧은 값의 별도 allocation을 피하지만 inline capacity와 객체 크기의 교환이다. COW 문자열은 공유와 detach 비용을 선택하는 모델이며, 현대 `std::basic_string`의 iterator·reference와 동시성 요구에는 맞지 않는다.

소유권의 명확성은 안전성을 좌우한다. `std::string`은 소유권이 명시적이라 안전하지만 비용이 따른다. `std::string_view`는 빠르지만 댕글링 위험이 있다. BSTR은 길이를 버퍼 앞에 붙여 포인터 하나로 길이까지 전달하는 대신, 짧은 값 최적화와 타입 수준의 안전성을 포기했다. 세 표현 모두 같은 질문에 다르게 답한 결과다. 길이와 소유권 정보를 어디에 두면 그것을 쓰는 코드가 가장 적게 다치는가.


## 더 읽을거리

- [Joel Spolsky, "Back to Basics" (2001)](https://www.joelonsoftware.com/2001/12/11/back-to-basics/) - Shlemiel the Painter's Algorithm과 `strcat`의 $O(n^2)$ 문제
- [WG21 N2668 - Concurrency Modifications to Basic String (2008)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2668.htm) - C++11에서 COW를 금지한 공식 제안서
- [cppreference: std::basic_string](https://en.cppreference.com/w/cpp/string/basic_string)
- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)
- [Dennis Ritchie, "The Development of the C Language"](https://www.bell-labs.com/usr/dmr/www/chist.html) - C 언어 창시자가 직접 쓴 설계 결정 배경
- [GCC libstdc++ basic_string.h](https://github.com/gcc-mirror/gcc/blob/master/libstdc%2B%2B-v3/include/bits/basic_string.h) - GCC의 실제 구현 (15자 SSO)
- [LLVM libc++ string](https://github.com/llvm/llvm-project/blob/main/libcxx/include/string) - Clang의 구현 (22자 SSO)
- [Microsoft STL xstring](https://github.com/microsoft/STL/blob/main/stl/inc/xstring) - MSVC의 구현 (15자 SSO)
- [Facebook Folly: FBString Documentation](https://github.com/facebook/folly/blob/main/folly/docs/FBString.md) - 23자 in-situ·jemalloc 연동을 포함한 3단 저장 전략
- [Facebook Folly: FBVector Documentation](https://github.com/facebook/folly/blob/main/folly/docs/FBVector.md) - Growth factor 1.5배 vs 2배 성능 비교
- [Qt Documentation: Implicit Sharing](https://doc.qt.io/qt-6/implicit-sharing.html) - Qt가 여전히 COW를 사용하는 이유
- [WG21 P0254R2: Integrating `std::string_view` and `std::string`](https://wg21.link/p0254r2)
- [Microsoft Learn: BSTR](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/automat/bstr) - length-prefix 표현을 채택한 실제 타입의 명세
