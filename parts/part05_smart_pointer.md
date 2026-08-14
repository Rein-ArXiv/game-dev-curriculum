## 들어가며

C++11이 도입한 스마트 포인터는 메모리 관리의 패러다임을 바꿨다. 수동 `new`/`delete`가 만들어내는 누수, 이중 해제, dangling pointer 문제를 RAII로 묶어낸 것이 핵심이다. 하지만 게임처럼 매 프레임 수만 개의 객체를 다루는 코드에서 `std::shared_ptr`을 무분별하게 사용하면 atomic 카운트와 추가 캐시 라인 접근이 누적되어 성능 병목이 된다.

이 글은 메모리 모델과 RAII의 관계에서 출발해, `unique_ptr`/`shared_ptr`/`weak_ptr`의 내부 구조와 비용을 분석하고, intrusive 참조 카운팅과 object pool 같은 게임 친화적 변형을 다룬다. 후반부에서는 표준 이전에 자주 쓰이던 Linked Pointer와 union 기반 참조 카운팅 같은 변종도 살펴본다.


---

## 목차
1. [메모리 모델과 RAII](#1-메모리-모델과-raii)
2. [Move Semantics와 Perfect Forwarding](#2-move-semantics와-perfect-forwarding)
3. [unique_ptr: 제로 오버헤드 추상화](#3-unique_ptr-제로-오버헤드-추상화)
4. [shared_ptr: 숨겨진 비용](#4-shared_ptr-숨겨진-비용)
5. [캐시 미스 문제](#5-캐시-미스-문제)
6. [Intrusive Reference Counting](#6-intrusive-reference-counting)
7. [Object Pool과 스마트 포인터](#7-object-pool과-스마트-포인터)
8. [커스텀 Deleter 활용](#8-커스텀-deleter-활용)
9. [참조 카운팅의 변종](#9-참조-카운팅의-변종)
10. [게임에서의 가이드라인](#10-게임에서의-가이드라인)


---

## 1. 메모리 모델과 RAII

### 자원의 수명을 객체에 묶는다

RAII(Resource Acquisition Is Initialization)는 Bjarne Stroustrup이 C++ 초기에 도입한 패턴이다. 자원의 획득과 해제를 객체의 생성자/소멸자에 묶어, **스코프를 빠져나가는 모든 경로**에서 해제가 보장되도록 한다.

```cpp
void process() {
    std::ifstream file("data.bin");   // 생성자: open
    if (!file) return;                // 소멸자가 close 호출
    if (parse_failed()) throw 1;      // 예외 경로에서도 close
    // ...
}                                     // 정상 경로에서도 close
```

C에서는 모든 분기에 `close`/`free`를 흩뿌려야 하지만, RAII는 컴파일러가 생성하는 소멸자 호출 코드에 그 책임을 위임한다. 스마트 포인터는 이 패턴을 동적 메모리에 적용한 것이다.

### 메모리의 세 영역

객체가 어디에 사는지가 수명 관리 전략을 결정한다.

| 영역 | 수명 | 해제 책임 |
|------|------|-----------|
| 정적(static/global) | 프로그램 종료까지 | 런타임이 자동 정리 |
| 자동(stack) | 스코프 종료까지 | 컴파일러가 생성한 소멸자 호출 |
| 동적(heap) | `new`로 만들고 `delete`로 해제 | 프로그래머 (또는 RAII 래퍼) |

스마트 포인터는 **동적 영역의 객체를 자동 영역의 수명으로 끌고 들어오는 어댑터**다. 스택에 놓인 `unique_ptr<T>`는 스코프를 벗어날 때 힙의 `T`까지 함께 해제한다.

### 5가지 핵심 멤버 함수

C++ 객체의 수명에 관여하는 함수는 다섯 개다.

```cpp
class T {
    T();                       // 기본 생성자
    T(const T&);               // 복사 생성자
    T(T&&) noexcept;           // 이동 생성자
    T& operator=(const T&);    // 복사 대입
    T& operator=(T&&) noexcept;// 이동 대입
    ~T();                      // 소멸자
};
```

스마트 포인터는 이 다섯 개를 어떻게 정의하느냐로 종류가 갈린다. `unique_ptr`은 복사 생성/대입을 `= delete`로 막고 이동만 허용한다. `shared_ptr`은 다섯 개를 모두 정의하고, 복사 시에는 atomic 참조 카운트를 증가시킨다.

### 예외 안전성과 두 단계 초기화

RAII의 진짜 가치는 예외 안전성이다.

```cpp
void unsafe() {
    Foo* a = new Foo();
    Foo* b = new Foo();   // 여기서 throw하면 a는 누수
    process(a, b);
    delete b;
    delete a;
}

void safe() {
    auto a = std::make_unique<Foo>();
    auto b = std::make_unique<Foo>();  // throw해도 a는 자동 해제
    process(a.get(), b.get());
}                                       // b, a 자동 해제
```

`std::make_unique`/`std::make_shared`가 권장되는 또 다른 이유다. C++14까지는 함수 인자 평가가 인터리브될 수 있어 `f(std::shared_ptr<T>(new T), may_throw)` 같은 호출에서 누수가 가능했다. C++17부터는 인자 간 인터리브가 금지되어 이 위험은 사라졌지만, 단일 할당 최적화와 가독성 때문에 make 함수가 여전히 권장된다.


## 2. Move Semantics와 Perfect Forwarding

### 복사가 아닌 이전

C++11 이전에는 무거운 객체를 함수가 반환하려면 복사가 필요했다. 컴파일러의 NRVO(Named Return Value Optimization)에 운명을 맡기거나, 출력 인자(out parameter)를 쓰는 방식뿐이었다.

```cpp
// C++03: 큰 vector 반환은 복사 (NRVO 없으면 비싸다)
std::vector<int> make_data();
auto v = make_data();   // 잠재적으로 깊은 복사
```

C++11의 rvalue reference(`T&&`)와 이동 시멘틱은 이 비용을 제거했다. 이동은 자원의 소유권을 빼앗아 옮기는 것이다.

```cpp
class Buffer {
    char* data_;
    size_t size_;
public:
    // 이동 생성자: 소유권 이전
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // 원본은 비운다
        other.size_ = 0;
    }

    ~Buffer() {
        delete[] data_;          // nullptr이면 no-op
    }
};
```

이동 후 원본은 **유효하지만 미지정 상태**(valid but unspecified)다. 소멸자가 안전하게 호출될 수 있어야 하며, 그 외 동작은 보장되지 않는다.

### unique_ptr의 본질은 이동

`unique_ptr`이 복사 불가능한 이유가 여기에 있다. 두 개의 `unique_ptr`이 같은 포인터를 가리키면 이중 해제가 발생한다. 따라서 복사 대신 이동만 허용한다.

```cpp
auto a = std::make_unique<Widget>();
// auto b = a;             // 컴파일 에러: 복사 금지
auto b = std::move(a);     // OK: a는 이제 nullptr
```

`std::move`는 이름과 달리 아무것도 옮기지 않는다. 단지 `a`를 rvalue reference로 캐스팅해, 이동 생성자가 호출되도록 만드는 표시일 뿐이다.

### Universal Reference와 Perfect Forwarding

템플릿에서 `T&&`는 두 얼굴을 가진다. 이를 universal reference 또는 forwarding reference라고 부른다.

```cpp
template<typename T>
void wrapper(T&& arg) {
    // arg가 lvalue로 전달되면 T = U&, T&& = U&
    // arg가 rvalue로 전달되면 T = U,  T&& = U&&
    callee(std::forward<T>(arg));
}
```

`std::forward<T>`는 `arg`를 원래의 값 카테고리(lvalue/rvalue)대로 다음 함수에 전달한다. 이를 통해 `make_unique`/`make_shared`는 임의의 생성자 인자를 손실 없이 객체 생성자에 넘길 수 있다.

```cpp
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(
        new T(std::forward<Args>(args)...)
    );
}
```

`Args&&...`는 인자 개수만큼의 universal reference를 묶은 것이다. `std::forward`로 풀어내야 lvalue는 lvalue로, rvalue는 rvalue로 보존된다. 이 규칙을 어기면 임시 객체가 lvalue로 굳어 불필요한 복사가 일어난다.

### Move의 경계

이동이 모든 비용을 없애는 것은 아니다.

| 객체 종류 | 이동 비용 |
|-----------|-----------|
| 포인터 멤버만 가진 클래스 (`vector`, `string`) | 포인터 몇 개 복사 후 원본 비우기, $O(1)$ |
| 작은 POD (`std::array<int, 4>`) | 복사와 동일 (이동할 자원 없음) |
| 소멸자가 비싼 객체 | 원본의 소멸자가 여전히 호출됨 |

소멸자가 자원을 해제할 때 분기 한 번이라도 들어간다면, 이동 후의 원본 소멸도 무시할 비용이 아니다. `unique_ptr`이 nullptr 체크 후 `delete`하는 것이 그 예다.


## 3. unique_ptr: 제로 오버헤드 추상화

### unique_ptr의 내부 구조

`std::unique_ptr<T, std::default_delete<T>>`는 최적화된 일반 사용에서 raw owning pointer와 같은 크기·소멸 동작으로 내려가도록 설계된 소유권 추상화다. 상태가 있는 deleter, 함수 포인터 deleter와 ABI 경계에서는 추가 저장 공간이나 호출 비용이 생길 수 있다.

```cpp
// 실제 구현 (간략화)
template<typename T, typename Deleter = std::default_delete<T>>
class unique_ptr {
    T* ptr_;
    [[no_unique_address]] Deleter deleter_;  // C++20: 빈 deleter는 공간 차지 안 함

public:
    T* get() const noexcept { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    ~unique_ptr() {
        if (ptr_) deleter_(ptr_);
    }
};
```

**메모리 레이아웃:**

| Deleter 타입 | `unique_ptr<T>` 크기 | 설명 |
|-------------|:-------------------:|------|
| `std::default_delete<T>` | 보통 포인터 하나 | 빈 deleter 압축 가능 |
| `void(*)(T*)` | 보통 포인터 둘 | 함수 포인터 저장 필요 |
| Stateful deleter | 구현·정렬 의존 | deleter 상태 저장 필요 |

### 생성되는 어셈블리 비교

```cpp
void use_raw(Widget* w) {
    w->process();
    delete w;
}

void use_unique(std::unique_ptr<Widget> w) {
    w->process();
}  // 자동 해제
```

최적화된 어셈블리 (GCC -O2):

```asm
; use_raw:
use_raw(Widget*):
    push rbx
    mov rbx, rdi
    call Widget::process()
    mov rdi, rbx
    call operator delete(void*)
    pop rbx
    ret

; use_unique: (동일!)
use_unique(std::unique_ptr<Widget>):
    push rbx
    mov rbx, rdi
    call Widget::process()
    mov rdi, rbx
    call operator delete(void*)
    pop rbx
    ret
```

이 컴파일러·ABI·최적화 조건에서는 사실상 동일한 코드가 생성될 수 있다. 이것이 의도한 zero-overhead 사용 경로다.

한 가지 단서가 있다. Itanium C++ ABI에서 비자명(non-trivial) 소멸자를 가진 타입을 값으로 전달하면 레지스터가 아닌 메모리를 경유하고 소멸 책임이 호출자에 남아, 인라인되지 않는 경계에서는 raw 포인터 대비 미세한 차이가 생길 수 있다(Chandler Carruth, "There Are No Zero-cost Abstractions", CppCon 2019). 인라인이 일어나는 일반적인 사용에서는 차이가 사라진다.

### 소유권 이전과 이동

```cpp
class GameObject {
    std::unique_ptr<Component> physics_;
    std::unique_ptr<Component> render_;

public:
    // 소유권을 명시적으로 받음
    void set_physics(std::unique_ptr<Component> comp) {
        physics_ = std::move(comp);  // 이전 컴포넌트 자동 해제
    }

    // 소유권을 명시적으로 포기
    std::unique_ptr<Component> release_physics() {
        return std::move(physics_);
    }
};
```

`unique_ptr`의 이동은 단순한 포인터 복사다:

```cpp
// std::move(unique_ptr)의 실제 동작
new_ptr.ptr_ = old_ptr.ptr_;
old_ptr.ptr_ = nullptr;
```


## 4. shared_ptr: 숨겨진 비용

### shared_ptr의 내부 구조

`std::shared_ptr`은 생각보다 복잡하다:

```cpp
template<typename T>
class shared_ptr {
    T* ptr_;                    // 8 bytes: 실제 객체 포인터
    control_block* control_;    // 8 bytes: 제어 블록 포인터
};

struct control_block {
    std::atomic<long> strong_count;  // 4-8 bytes
    std::atomic<long> weak_count;    // 4-8 bytes
    // + deleter, allocator 저장 공간
    // + 가상 함수 테이블 포인터 (다형성 지원 시)
};
```

**메모리 레이아웃:**

| 구성 요소 | 크기 | 위치 |
|----------|:----:|------|
| `shared_ptr` 객체 | 16 bytes | 스택/멤버 |
| Control block | 16-32+ bytes | 힙 (별도 할당) |
| 실제 객체 | sizeof(T) | 힙 |

**총 2번의 힙 할당이 발생한다** (make_shared 미사용 시).

### make_shared의 최적화

```cpp
// 나쁜 예: 2번 할당
auto p1 = std::shared_ptr<Widget>(new Widget());

// 좋은 예: 1번 할당
auto p2 = std::make_shared<Widget>();
```

`make_shared`는 객체와 제어 블록을 하나의 메모리 블록에 할당한다:

| 방식 | 할당 횟수 | 메모리 레이아웃 |
|------|:--------:|----------------|
| `shared_ptr(new T)` | 2회 | `[control block]` + `[object]` (분리) |
| `make_shared<T>` | 1회 | `[control block \| object]` (연속) |

```cpp
// make_shared 내부 구현 개념
template<typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args) {
    // 하나의 할당으로 control block + object
    void* mem = operator new(sizeof(control_block_with_object<T>));

    auto* block = new(mem) control_block_with_object<T>();
    new(&block->object) T(std::forward<Args>(args)...);

    return shared_ptr<T>(block);
}
```

### 참조 카운팅의 진짜 비용

참조 카운트 증감은 **atomic 연산**이다:

```cpp
shared_ptr<Widget> copy = original;  // 내부적으로:
// atomic_fetch_add(&control_->strong_count, 1);
```

atomic RMW의 비용을 고정 사이클로 둘 수는 없다. 같은 코어가 Modified 상태의 cache line을 계속 소유하는 경우와 여러 코어가 같은 count를 갱신해 소유권이 왕복하는 경우는 전혀 다른 경로다.

| 조건 | 주된 비용 | 측정 항목 |
|------|-----------|----------|
| 한 스레드가 복사·파괴 | locked/atomic RMW와 dependency | cycles/op, instructions |
| 여러 코어가 같은 control block 갱신 | cache line ownership transfer | cache-to-cache transfer, scaling |
| 서로 다른 객체를 독립 갱신 | control block 배치와 false sharing | line alignment, branch/cache miss |
| 마지막 strong/weak 해제 | destructor·deleter·allocator 경로 | p95/p99 latency, allocation trace |

멀티스레드 환경에서 동일한 `shared_ptr`을 여러 스레드가 복사하면 **캐시 라인 바운싱**이 발생한다:

```text
Thread 1 (Core 0)              Thread 2 (Core 1)
     |                              |
     v                              v
[Copy shared_ptr]             [Copy shared_ptr]
     |                              |
     v                              v
atomic_add(count)             atomic_add(count)
     |                              |
     +---> Cache invalidation <-----+
           (MESI protocol)
```

### shared_ptr 복사 vs 참조 전달

```cpp
// 나쁜 예: 매번 참조 카운트 증감
void process_bad(std::shared_ptr<Widget> w) {  // 복사!
    w->do_something();
}

// 좋은 예: 참조 전달
void process_good(const std::shared_ptr<Widget>& w) {  // 참조
    w->do_something();
}

// 소유권이 필요 없는 API: 객체 자체를 빌린다.
void process_borrowed(Widget& w) {
    w.do_something();
}
```

값으로 받은 `shared_ptr`은 함수가 공유 소유권을 보관할 수 있다는 계약이며 보통 제어 블록의 강한 참조 수를 증감한다. `const shared_ptr&`는 그 증감을 피하지만 API를 특정 소유권 표현에 결합한다. 단순 관찰자라면 `Widget&` 또는 nullable 의미가 필요할 때 `Widget*`가 계약을 더 정확히 표현한다. 호출 비용 배수는 인라이닝, 제어 블록의 캐시 위치와 스레드 경합에 따라 달라지므로, 호출만 재는 마이크로벤치마크보다 실제 call site의 참조 수 변경과 cache miss를 프로파일링한다.

### weak_ptr: 순환 참조와 캐시 패턴

`std::weak_ptr`은 소유권 없이 `shared_ptr`이 관리하는 객체를 참조한다.

**순환 참조 문제:**

```cpp
// 메모리 누수 발생!
class Node {
    std::shared_ptr<Node> next;
    std::shared_ptr<Node> prev;  // 순환 참조
};

auto a = std::make_shared<Node>();
auto b = std::make_shared<Node>();
a->next = b;
b->prev = a;  // a ↔ b 순환, 둘 다 해제 안 됨
```

**해결: weak_ptr 사용**

```cpp
class Node {
    std::shared_ptr<Node> next;
    std::weak_ptr<Node> prev;  // 약한 참조 (소유권 없음)
};

auto a = std::make_shared<Node>();
auto b = std::make_shared<Node>();
a->next = b;
b->prev = a;  // prev는 weak_ptr이므로 순환 아님
// a가 스코프 벗어나면 → a 해제 → b의 유일한 소유자 사라짐 → b 해제
```

**weak_ptr 사용법:**

```cpp
std::shared_ptr<Widget> sp = std::make_shared<Widget>();
std::weak_ptr<Widget> wp = sp;  // 약한 참조 생성

// 사용 시 반드시 lock()으로 shared_ptr 획득
if (auto locked = wp.lock()) {
    locked->do_something();  // 안전하게 사용
} else {
    // 객체가 이미 소멸됨
}

// 또는 expired()로 확인
if (!wp.expired()) {
    // 여전히 존재 (하지만 race condition 가능)
}
```

**게임에서의 활용 - 캐시 패턴:**

```cpp
class TextureCache {
    std::unordered_map<std::string, std::weak_ptr<Texture>> cache_;

public:
    std::shared_ptr<Texture> get(const std::string& path) {
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            if (auto tex = it->second.lock()) {
                return tex;  // 캐시 히트
            }
            // 이미 해제됨, 엔트리 제거
            cache_.erase(it);
        }

        // 새로 로드
        auto tex = std::make_shared<Texture>(load_from_disk(path));
        cache_[path] = tex;
        return tex;
    }
};
```

이 패턴에서:
- 텍스처를 사용하는 동안은 `shared_ptr`이 유지
- 아무도 사용하지 않으면 자동 해제 (메모리 절약)
- 다시 필요하면 재로드

**weak_ptr의 비용:**

| 항목 | 비용 |
|------|------|
| 크기 | 16 bytes (shared_ptr과 동일) |
| `lock` | atomic load + 조건부 atomic increment |
| 제어 블록 | weak_count로 인해 객체 해제 후에도 유지 |

### enable_shared_from_this

객체 내부에서 자기 자신의 `shared_ptr`을 얻어야 할 때 사용한다.

**문제 상황:**

```cpp
class Widget {
public:
    void register_callback() {
        // 위험! this로 새 shared_ptr 생성하면 이중 해제
        callback_manager.add(std::shared_ptr<Widget>(this));
    }
};
```

**해결: enable_shared_from_this**

```cpp
class Widget : public std::enable_shared_from_this<Widget> {
public:
    void register_callback() {
        // 안전! 기존 제어 블록 공유
        callback_manager.add(shared_from_this());
    }

    std::shared_ptr<Widget> get_shared() {
        return shared_from_this();
    }

    std::weak_ptr<Widget> get_weak() {
        return weak_from_this();  // C++17
    }
};

// 사용
auto w = std::make_shared<Widget>();
w->register_callback();  // 안전
```

**주의사항:**

```cpp
// 반드시 shared_ptr로 관리되어야 함
Widget w;  // 스택 객체
w.shared_from_this();  // 예외 발생! (bad_weak_ptr)

// 생성자에서 호출 금지
class Bad : public std::enable_shared_from_this<Bad> {
    Bad() {
        shared_from_this();  // 예외! 아직 shared_ptr 없음
    }
};
```

**내부 구현 원리:**

```cpp
template<typename T>
class enable_shared_from_this {
    mutable std::weak_ptr<T> weak_this_;

    template<typename U> friend class shared_ptr;

public:
    std::shared_ptr<T> shared_from_this() {
        return std::shared_ptr<T>(weak_this_);
    }

protected:
    // shared_ptr 생성 시 자동 호출됨
    void _internal_accept_owner(const std::shared_ptr<T>& owner) {
        weak_this_ = owner;
    }
};
```

**게임에서의 활용:**

```cpp
class Entity : public std::enable_shared_from_this<Entity> {
public:
    void spawn_child() {
        auto child = std::make_shared<Entity>();
        child->parent_ = weak_from_this();  // 부모를 약한 참조
        children_.push_back(child);
    }

    void schedule_destruction() {
        // 자기 자신을 destruction queue에 등록
        destruction_queue.push(shared_from_this());
    }

private:
    std::weak_ptr<Entity> parent_;
    std::vector<std::shared_ptr<Entity>> children_;
};
```


## 5. 캐시 미스 문제

### shared_ptr의 메모리 접근 패턴

`shared_ptr`의 `operator->`는 보관 중인 객체 포인터를 반환하고, 그 포인터로 객체에 접근한다:

```cpp
shared_ptr<Widget> sp;
sp->process();  // 실제 접근:
// 1. sp가 보관한 객체 포인터를 얻음
// 2. 그 포인터로 Widget에 접근
```

`make_shared` 사용 여부와 관계없이 `operator->`는 제어 블록을 읽을 필요가 없다. 제어 블록은 `shared_ptr`을 복사·파괴하거나 `weak_ptr::lock()`을 수행할 때 접근한다. `make_shared`의 주된 이점은 객체와 제어 블록을 한 번에 할당하는 것이며, 두 영역이 가까워져 소유권 연산의 locality가 좋아질 수 있다는 점이다.

### 배열에서의 캐시 효율

```cpp
// 나쁜 예: shared_ptr 배열
std::vector<std::shared_ptr<Entity>> entities;
for (auto& e : entities) {
    e->update();  // 매번 2+ 포인터 역참조
}
```

**메모리 접근 패턴:**

```text
entities vector:
[sp0|sp1|sp2|sp3|...]
  |   |   |   |
  v   v   v   v
[E0][E1][E2][E3]...  <- 힙 곳곳에 흩어짐 (캐시 미스!)
```

```cpp
// 좋은 예: 객체 직접 저장
std::vector<Entity> entities;  // 연속 메모리
for (auto& e : entities) {
    e.update();  // 캐시 친화적
}
```

**메모리 접근 패턴:**

```text
entities vector (연속 메모리):
[E0|E1|E2|E3|E4|E5|...]
 ↑
 하나의 캐시 라인에 여러 Entity
```

### 벤치마크: 순회 성능

```cpp
constexpr size_t COUNT = 1'000'000;

// shared_ptr 배열
std::vector<std::shared_ptr<Data>> ptr_vec;
for (size_t i = 0; i < COUNT; ++i)
    ptr_vec.push_back(std::make_shared<Data>());

// 직접 저장 배열
std::vector<Data> direct_vec(COUNT);

// 순회 테스트
```

이 실험은 직접 저장과 간접 저장의 비용을 비교하지만 보편적인 배율을 만들지는 않는다. `Data` 크기, 포인터가 가리키는 객체의 배치, prefetch 가능성, `shared_ptr`의 제어 블록 접근 여부가 결과를 바꾼다. 순회 본문이 객체 필드만 읽는다면 `unique_ptr`와 `shared_ptr`은 주로 한 단계의 간접 참조와 객체 분산 비용을 공유하며, 복사·파괴가 없다면 참조 카운트 원자 연산은 발생하지 않는다. 측정에서는 실행 시간과 cache/TLB miss를 함께 기록한다.


## 6. Intrusive Reference Counting

### 문제: 외부 제어 블록

`std::shared_ptr`의 제어 블록은 객체 외부에 있다:

| 문제 | 설명 |
|------|------|
| 추가 메모리 | 제어 블록 16-32 bytes |
| 캐시 미스 | 객체와 제어 블록이 다른 캐시 라인 |
| 할당 오버헤드 | 별도 할당 필요 (make_shared 미사용 시) |

### 해결: 객체 내부에 카운트 두기

참조 카운트를 객체 내부에 포함시킨다:

```cpp
class RefCounted {
    mutable std::atomic<uint32_t> ref_count_{0};

public:
    void add_ref() const noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() const noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    uint32_t use_count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }
};
```

**Intrusive 포인터 구현:**

```cpp
template<typename T>
class IntrusivePtr {
    T* ptr_;

public:
    IntrusivePtr() noexcept : ptr_(nullptr) {}

    explicit IntrusivePtr(T* p) noexcept : ptr_(p) {
        if (ptr_) ptr_->add_ref();
    }

    IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->add_ref();
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ~IntrusivePtr() {
        if (ptr_) ptr_->release();
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->add_ref();
        }
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
};
```

### 메모리 레이아웃 비교

| 항목 | `shared_ptr<T>` | `IntrusivePtr<T>` |
|------|:--------------:|:-----------------:|
| 포인터 크기 | 16 bytes | 8 bytes |
| 제어 블록 | 별도 할당 | 없음 (객체 내부) |
| 총 오버헤드 | 32+ bytes | 4 bytes (카운트만) |
| 캐시 라인 | 2-3개 | 1개 |

### Unreal Engine의 TSharedPtr

Unreal Engine은 `std::shared_ptr`을 그대로 쓰지 않고 자체 스마트 포인터 라이브러리(`TSharedPtr`, `TSharedRef`, `TWeakPtr`, `TUniquePtr`)를 제공한다. 이는 일반 C++ 객체용이며, 게임 오브젝트(UObject)는 이 시리즈로 관리하지 않는다는 점이 핵심이다.

```cpp
// 일반 C++ 객체에 사용. UObject 계열에는 사용하지 않는다.
class FRenderResource {
    // ...
};

TSharedPtr<FRenderResource> Resource = MakeShared<FRenderResource>();
TWeakPtr<FRenderResource> WeakRef = Resource;
```

**UObject는 별도의 GC가 관리한다:**

UObject 파생 클래스(AActor, UActorComponent 등)는 `TSharedPtr`로 감싸지 않는다. UObject는 엔진의 mark-and-sweep GC가 관리하며, 참조는 `UPROPERTY`로 표시한 raw 포인터로 표현한다. GC는 UPROPERTY 그래프를 마킹해 도달 불가능한 UObject를 수거한다.

```cpp
UCLASS()
class AMyActor : public AActor {
    UPROPERTY()
    AActor* Target;  // GC가 추적하는 참조. TSharedPtr 사용 금지.

    // 일반 C++ 객체는 TSharedPtr 가능
    TSharedPtr<FRenderResource> RenderRes;
};
```

**TSharedPtr의 모드:**

`TSharedPtr`은 `ESPMode::NotThreadSafe`와 `ESPMode::ThreadSafe` 정책을 제공한다. 전자는 reference count 갱신에 thread-safe synchronization을 제공하지 않고, 후자는 여러 thread에서 control block을 공유할 수 있는 counter 경로를 사용한다. 생략했을 때의 default mode와 사용 가능한 최적화는 Unreal 버전·플랫폼 header에서 확인해야 한다. `MakeShared`는 지원되는 타입과 mode에서 객체와 controller를 한 allocation에 배치해 별도 할당을 줄일 수 있다.

```cpp
TSharedPtr<FResource, ESPMode::ThreadSafe> Resource = MakeShared<FResource, ESPMode::ThreadSafe>();
```

`TSharedFromThis<T>`는 `std::enable_shared_from_this`의 대응물로, 자기 자신에 대한 `TSharedPtr`을 만들 수 있게 한다. UObject가 아닌 일반 C++ 클래스에만 적용한다.


## 7. Object Pool과 스마트 포인터

### 문제: new/delete 오버헤드

스마트 포인터는 `delete`를 자동 호출하지만, 힙 할당/해제 자체가 비용이다:

```cpp
// 매 프레임 총알 생성/소멸
void spawn_bullet() {
    auto bullet = std::make_unique<Bullet>();  // 힙 할당
    bullets_.push_back(std::move(bullet));
}

void remove_bullet(size_t idx) {
    bullets_.erase(bullets_.begin() + idx);  // 힙 해제
}
```

### Object Pool 기반 스마트 포인터

```cpp
template<typename T, size_t PoolSize = 1024>
class PooledPtr {
public:
    class Pool {
        struct Slot {
            alignas(T) std::byte bytes[sizeof(T)];
        };

        std::array<Slot, PoolSize> storage_;
        std::array<size_t, PoolSize> free_indices_;
        size_t free_count_ = PoolSize;
        std::mutex mutex_;

    public:
        Pool() {
            for (size_t i = 0; i < PoolSize; ++i) {
                free_indices_[i] = PoolSize - 1 - i;
            }
        }

        T* allocate() {
            std::lock_guard lock(mutex_);
            if (free_count_ == 0) return nullptr;
            const size_t idx = free_indices_[--free_count_];
            return reinterpret_cast<T*>(storage_[idx].bytes);
        }

        void deallocate(T* ptr) {
            std::lock_guard lock(mutex_);
            auto* base = reinterpret_cast<std::byte*>(storage_.data());
            auto* raw = reinterpret_cast<std::byte*>(ptr);
            const ptrdiff_t offset = raw - base;
            assert(offset >= 0 && offset % sizeof(Slot) == 0);
            const size_t idx = static_cast<size_t>(offset) / sizeof(Slot);
            assert(idx < PoolSize && free_count_ < PoolSize);
            free_indices_[free_count_++] = idx;
        }
    };

private:
    T* ptr_;
    Pool* pool_;

public:
    explicit PooledPtr(Pool& pool) : pool_(&pool) {
        ptr_ = pool_->allocate();
        if (ptr_) {
            try { new(ptr_) T(); }
            catch (...) { pool_->deallocate(ptr_); ptr_ = nullptr; throw; }
        }
    }

    template<typename... Args>
    explicit PooledPtr(Pool& pool, Args&&... args) : pool_(&pool) {
        ptr_ = pool_->allocate();
        if (ptr_) {
            try { new(ptr_) T(std::forward<Args>(args)...); }
            catch (...) { pool_->deallocate(ptr_); ptr_ = nullptr; throw; }
        }
    }

    ~PooledPtr() {
        if (ptr_) {
            ptr_->~T();
            pool_->deallocate(ptr_);
        }
    }

    // Move only
    PooledPtr(PooledPtr&& other) noexcept
        : ptr_(other.ptr_), pool_(other.pool_) {
        other.ptr_ = nullptr;
    }

    PooledPtr& operator=(PooledPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->~T();
                pool_->deallocate(ptr_);
            }
            ptr_ = other.ptr_;
            pool_ = other.pool_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    PooledPtr(const PooledPtr&) = delete;
    PooledPtr& operator=(const PooledPtr&) = delete;

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
};
```

### 사용 예시

```cpp
// 총알 풀
PooledPtr<Bullet>::Pool bullet_pool;

void spawn_bullet(const Vec3& pos, const Vec3& dir) {
    auto bullet = PooledPtr<Bullet>(bullet_pool, pos, dir);
    if (bullet) {
        active_bullets_.push_back(std::move(bullet));
    }
}
```

풀은 일반 할당자의 탐색·동기화와 페이지 반환을 줄일 수 있지만, 생성자·소멸자 비용은 그대로이고 고정 용량의 미사용 슬롯이라는 내부 단편화를 만든다. 비교할 때는 같은 객체 생성자, 같은 peak live count, 같은 초기화 정책을 사용하고 풀 준비 비용과 메모리 상한도 포함한다. “풀이라 단편화가 없음”이 아니라 외부 단편화를 예측 가능한 예약 낭비와 맞바꾸는 설계다.


## 8. 커스텀 Deleter 활용

### 기본 사용법

```cpp
// 파일 핸들 자동 정리
auto file = std::unique_ptr<FILE, decltype(&fclose)>(
    fopen("data.bin", "rb"),
    fclose
);

// Windows HANDLE: CreateFile 실패값은 nullptr가 아니라 INVALID_HANDLE_VALUE다.
struct HandleCloser {
    using pointer = HANDLE;
    void operator()(HANDLE h) const noexcept {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

HANDLE raw = CreateFileW(L"file.txt", GENERIC_READ, FILE_SHARE_READ,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
if (raw == INVALID_HANDLE_VALUE) {
    throw std::system_error(GetLastError(), std::system_category());
}
std::unique_ptr<void, HandleCloser> handle(raw);
```

### 상태 없는 Deleter (Zero Overhead)

```cpp
// 함수 객체 사용 (빈 클래스)
struct FileCloser {
    void operator()(FILE* f) const noexcept {
        if (f) fclose(f);
    }
};

auto file = std::unique_ptr<FILE, FileCloser>(fopen("data.bin", "rb"));
// sizeof(unique_ptr<FILE, FileCloser>) == 8 bytes (EBO 적용)
```

### Object Pool 반환 Deleter

```cpp
template<typename T>
class PoolDeleter {
    ObjectPool<T>* pool_;

public:
    explicit PoolDeleter(ObjectPool<T>* pool) noexcept : pool_(pool) {}

    void operator()(T* ptr) const noexcept {
        if (ptr) {
            ptr->~T();
            pool_->deallocate(ptr);
        }
    }
};

template<typename T>
using PoolUniquePtr = std::unique_ptr<T, PoolDeleter<T>>;

// 사용
ObjectPool<Particle> particle_pool(10000);

PoolUniquePtr<Particle> create_particle() {
    void* mem = particle_pool.allocate();
    auto* p = new(mem) Particle();
    return PoolUniquePtr<Particle>(p, PoolDeleter<Particle>(&particle_pool));
}
```

### 풀 반환 deleter를 가진 shared_ptr

```cpp
// 풀에서 할당하고 풀로 반환하는 shared_ptr (커스텀 deleter 활용)
template<typename T, typename... Args>
std::shared_ptr<T> allocate_shared_from_pool(ObjectPool<T>& pool, Args&&... args) {
    T* ptr = pool.allocate();
    new(ptr) T(std::forward<Args>(args)...);

    return std::shared_ptr<T>(ptr, [&pool](T* p) {
        p->~T();
        pool.deallocate(p);
    });
}
```

람다가 풀을 참조로 캡처하므로 풀의 수명이 모든 shared_ptr보다 길어야 한다. 제어 블록까지 커스텀 메모리에서 할당하려면 `std::allocate_shared`에 allocator를 넘기는 방법도 있다.


## 9. 참조 카운팅의 변종

표준 `std::shared_ptr`이 정착되기 전, 그리고 그 이후에도 메모리 예산이 빠듯한 환경에서는 다양한 참조 카운팅 구현이 사용되었다. 두 가지 대표적인 접근이 있다. 하나는 카운트를 별도의 control block에 두는 표준 방식이고, 다른 하나는 소유자들을 이중 연결 리스트로 묶어 마지막 소유자가 사라질 때 객체를 해제하는 **Linked Pointer** 방식이다.

### Control Block 방식

표준 `shared_ptr`은 별도의 제어 블록에 참조 카운트를 저장한다.

| 구성 요소 | 저장 위치 | 내용 |
|-----------|-----------|------|
| `shared_ptr` 변수들 | 스택/멤버 | Object 포인터 + Control Block 포인터 |
| Control Block | 별도 힙 | strong_count, weak_count, deleter |
| Object | 별도 힙 (또는 control block과 한 덩어리, make_shared) | 실제 데이터 |

| 항목 | 비용 |
|------|------|
| 참조 카운트 확인 | $O(1)$ |
| 복사 | atomic increment |
| 추가 메모리 | Control Block 16~32 bytes |

### Linked Pointer 방식

소유자들이 원형 이중 연결 리스트로 연결되고, 자기 자신만 가리키는 노드가 마지막 소유자다.

```cpp
template<typename T>
class LinkedPtr {
    T* ptr_;
    mutable LinkedPtr* prev_;
    mutable LinkedPtr* next_;

public:
    LinkedPtr() : ptr_(nullptr), prev_(this), next_(this) {}

    explicit LinkedPtr(T* p) : ptr_(p), prev_(this), next_(this) {}

    LinkedPtr(const LinkedPtr& other) : ptr_(other.ptr_) {
        if (ptr_) Insert(other);
        else      prev_ = next_ = this;
    }

    ~LinkedPtr() {
        if (ptr_) Remove();
    }

private:
    void Insert(const LinkedPtr& other) {
        // 링크 필드는 mutable이며 pointee의 논리적 값은 바꾸지 않는다.
        auto* anchor = const_cast<LinkedPtr*>(&other);
        prev_ = anchor;
        next_ = other.next_;
        other.next_->prev_ = this;
        other.next_ = this;
    }

    void Remove() {
        if (prev_ == this) {
            delete ptr_;            // 마지막 노드
        } else {
            prev_->next_ = next_;
            next_->prev_ = prev_;
        }
        prev_ = next_ = this;
    }
};
```

복사할 때마다 양쪽 리스트에 자신을 끼워 넣고, 소멸할 때 빠져나간다. `prev_ == this`로 자기 자신만 가리킬 때가 유일한 소유자라는 신호다.

### 두 방식 비교

| 측면 | Control Block | Linked Pointer |
|------|---------------|----------------|
| 메모리 오버헤드 | 별도 블록 16~32 bytes | 포인터 2개 (16 bytes/소유자) |
| 복사 비용 | atomic increment | 리스트 삽입(포인터 4개 조작) |
| 해제 비용 | atomic decrement + 조건부 delete | 리스트 제거(포인터 2~4개 조작) |
| 스레드 안전성 | atomic으로 해결 | 리스트 전체 락 필요 |
| 참조 수 확인 | $O(1)$ | $O(n)$ 순회 |
| 캐시 지역성 | Control Block 별도 캐시 라인 | 포인터들이 스마트 포인터와 함께 |

Linked Pointer가 2000년대 초반에 선택된 이유는 다음과 같다.

| 이유 | 설명 |
|------|------|
| Control Block 할당 회피 | 당시 `new`가 비쌌고 작은 객체 할당이 단편화를 유발 |
| 싱글스레드 클라이언트 | 락 오버헤드 걱정 없음 |
| Boost 미도입 | `boost::shared_ptr` 의존성 추가를 꺼림 |
| 32-bit 메모리 예산 | Control Block 16바이트 절약이 유의미 |

### Union 기반 카운트와 free list 공유

객체가 **사용 중**일 때와 **풀에 반환된 free 상태**일 때는 상호 배타적이다. 같은 메모리 공간을 두 용도로 재사용할 수 있다.

```cpp
class RefCountedPoolObject {
public:
    union {
        RefCountedPoolObject* next_free_;       // free 상태
        std::atomic<uint32_t> ref_count_;       // 사용 중
    };

    void InitializeFromPool() {
        new (&ref_count_) std::atomic<uint32_t>(1);
    }

    void AddRef() {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    bool Release(ObjectPool* pool) {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ReturnToPool(pool);
            return true;
        }
        return false;
    }

private:
    void ReturnToPool(ObjectPool* pool) {
        next_free_ = pool->free_head_;
        pool->free_head_ = this;
    }
};
```

상태 전이는 다음과 같다.

| 상태 | ref_count_ | next_free_ | 의미 |
|------|:----------:|:----------:|------|
| 사용 중 | $\geq 1$ | (무의미) | 누군가 참조 중 |
| Free | (무의미) | 다음 free 블록 | 풀에서 대기 |

| 단계 | 동작 | union 멤버 |
|------|------|------------|
| 1. 풀에서 분리 | `obj = free_head; free_head = obj->next_free` | next_free_ |
| 2. 초기화 | `ref_count_ = 1` | ref_count_로 전환 |
| 3. AddRef/Release | atomic 증감 | ref_count_ |
| 4. count == 0 | `next_free_ = free_head; free_head = obj` | next_free_로 전환 |

32-bit 환경의 클라이언트에서 객체 수가 수백만 개일 때 필드 하나(4 bytes)를 줄이는 것이 수 MB의 절약이었다. 이것이 union을 type punning에 가깝게 사용한 이유다.

### Union 트릭의 위험

C++ 표준상 union의 활성 멤버가 아닌 멤버에 접근하는 것은 정의되지 않은 동작에 가깝다. 실무에서 동작하는 조건은 다음과 같다.

| 조건 | 설명 |
|------|------|
| 같은 크기 | 두 멤버가 동일한 바이트 수 (32-bit 시스템에서 `void*`와 `uint32_t`) |
| 명확한 상태 전이 | 동시 접근이 없고, 누가 어느 멤버를 활성화했는지 추적 가능 |
| 컴파일러 허용 | strict aliasing 규칙이 적용되지 않는 영역 |

디버깅 측면에서도 까다롭다. free된 객체의 `ref_count_`를 디버거가 출력하면 그것은 사실 다음 free 블록의 포인터 값이다. `ref_count_ = 0x7FFE1234`라는 출력이 사실은 `next_free_`라는 사실을 모르면 혼란스럽다.

스레드 안전성도 까다롭다. 같은 메모리 위치에 두 개의 atomic 타입이 union으로 들어가 있으면, 활성 멤버 전환 사이에 데이터 경합이 가능하다. 대부분의 레거시 구현은 싱글스레드 가정 하에 작성되었다.

### 현대적 대안

| 패턴 | 장점 | 단점 |
|------|------|------|
| `std::shared_ptr` + `make_shared` | 표준, 안전, 1회 할당 | Control Block 16+ bytes |
| Intrusive 참조 카운트 | 8 bytes 포인터, 캐시 지역성 | 침습적 설계 필요 |
| Pool + Handle | dangling 감지, 풀 친화적 | 간접 참조 비용 |

새 코드에는 `std::make_shared` 또는 intrusive 참조 카운트가 권장된다. 레거시 코드를 분석할 때 union 기반 카운트나 Linked Pointer를 만나면, 그것은 당시 메모리 예산과 STL 한계가 강요한 합리적 선택이었다는 점을 먼저 이해할 필요가 있다.


## 10. 게임에서의 가이드라인

### 언제 무엇을 사용할 것인가

| 상황 | 권장 | 이유 |
|------|------|------|
| 단일 소유권, 수명 명확 | `unique_ptr` | 제로 오버헤드 |
| 소유권 공유 필요 | `shared_ptr` (신중히) | atomic 비용 |
| 대량 객체 (파티클 등) | Object Pool + raw ptr | 캐시 효율 |
| 참조만 필요 | raw pointer / reference | 소유권 불필요 |
| 순환 참조 가능성 | `weak_ptr` | 순환 방지 |

### 피해야 할 패턴

```cpp
// 1. 불필요한 shared_ptr
class Player {
    std::shared_ptr<Weapon> weapon_;  // 정말 공유 필요?
    // -> 대부분 unique_ptr로 충분
};

// 2. shared_ptr 값 전달
void process(std::shared_ptr<Entity> e);  // 복사!
// -> const 참조 또는 원시 포인터 사용

// 3. 컨테이너에 shared_ptr
std::vector<std::shared_ptr<Component>> components_;
// -> 직접 저장 또는 unique_ptr 고려

// 4. 핫 루프에서 shared_ptr 생성
for (int i = 0; i < 10000; ++i) {
    auto p = std::make_shared<Temp>();  // 매번 힙 할당!
}
```

### 권장 패턴

```cpp
// 1. 명확한 소유권 계층
class Scene {
    std::vector<std::unique_ptr<Entity>> entities_;  // Scene이 소유

public:
    Entity* spawn_entity() {
        entities_.push_back(std::make_unique<Entity>());
        return entities_.back().get();  // 원시 포인터로 참조 제공
    }
};

// 2. Pool + Handle 패턴
class EntityManager {
    ObjectPool<Entity> pool_;

public:
    EntityHandle create() {
        return EntityHandle(pool_.allocate());
    }

    Entity* get(EntityHandle h) {
        return pool_.get(h);
    }
};

// 3. Intrusive counting (공유 필요 시)
class Asset : public RefCounted {
    // ...
};

IntrusivePtr<Asset> texture = load_asset("texture.png");
```


### 비용을 비교하는 기준

| 표현 | 직접 비용 | 구조적 비용 |
|---|---|---|
| `unique_ptr<T>` | 이동은 포인터 소유권 이전 | pointee가 별도 할당이면 순회 locality가 낮아질 수 있음 |
| `shared_ptr<T>` | 복사·소멸 시 공유 제어 블록 갱신 | 제어 블록, 약한 참조 수명, 경합과 cache-line 이동 |
| intrusive ref count | 객체 안 카운터 갱신 | 타입 침습, 잘못된 스택 객체 참조 위험 |
| pool + handle | handle 검증과 간접 인덱스 | 용량 예약, generation wrap, pool 수명 관리 |

객체 생성 벤치마크에는 할당 방식과 생성자 비용을 분리하고, 순회 벤치마크에는 pointee 배치와 접근 필드를 고정한다. 메모리는 `sizeof` 추정이 아니라 peak live bytes, allocator resident bytes, control block과 pool의 예약분을 함께 측정한다.


---

## 결론

스마트 포인터는 소유권 계약을 타입에 담는다. `unique_ptr`은 기본 deleter와 일반적인 ABI에서 한 포인터로 최적화되는 경우가 많지만 사용자 정의 deleter의 상태와 pointee 별도 할당 비용은 남는다. `shared_ptr`은 공유 제어 블록과 thread-safe 참조 수 갱신 비용을 가지며, 실제 비용은 경합과 배치에 좌우된다. 대량 객체에 pool과 handle이 유리한지도 최대 동시 수, 순회 방식, 수명 패턴을 측정해 결정한다.

핵심은 **소유권의 명확한 설계**다. 누가 객체를 만들고, 누가 빌려 쓰고, 누가 지우는지를 타입으로 표현하면 대부분의 경우 `unique_ptr`과 원시 포인터만으로 충분하다. `shared_ptr`은 정말로 공유가 필요할 때만 꺼낸다.

이 글은 메모리 모델 위에서 RAII가 자원 수명을 어떻게 묶는지, 이동 시멘틱이 복사 비용을 어떻게 제거하는지에서 출발해 표준 스마트 포인터의 비용과 변종까지 살펴봤다. 컨테이너에 스마트 포인터를 넣는 순간 포인터 안정성뿐 아니라 캐시 레이아웃과 할당자 선택도 비용의 일부가 된다.


## 더 읽을거리

### 공식 문서
- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)

### 기술 논문 및 서적
- Herb Sutter, "GotW #91: Smart Pointer Parameters"
- Scott Meyers, "Effective Modern C++", Items 18-22
- Andrei Alexandrescu, "Modern C++ Design" - Policy-based smart pointers

### 소스 코드 분석
- [LLVM libcxx shared_ptr 구현](https://github.com/llvm/llvm-project/blob/main/libcxx/include/__memory/shared_ptr.h)
- [Boost intrusive_ptr](https://www.boost.org/doc/libs/release/libs/smart_ptr/doc/html/smart_ptr.html#intrusive_ptr)
- [Unreal Engine TSharedPtr](https://docs.unrealengine.com/en-US/API/Runtime/Core/Templates/TSharedPtr/)

### 도구
- Valgrind - 메모리 누수 탐지
- AddressSanitizer (ASan) - 메모리 오류 탐지
- perf - 캐시 미스 프로파일링
