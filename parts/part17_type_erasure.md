## 들어가며

`std::function<void()>`에 람다를 저장하면, 컴파일러는 어떻게 전혀 다른 타입들을 하나의 변수에 담는 것인가? 함수 포인터도 아니고, 가상 함수도 아닌 방법으로?

답은 **Type Erasure**에 있다. C++ 템플릿 메타프로그래밍의 정수이자, `std::function`, `std::any`, `std::shared_ptr`의 커스텀 deleter 등이 사용하는 핵심 기법이다. Adobe의 Sean Parent는 GoingNative 2013의 ["Inheritance Is The Base Class of Evil"](https://www.youtube.com/watch?v=bIhUE5uUFOA)이라는 도발적인 제목의 강연에서, C++의 다형성은 상속이 아니라 값 시맨틱을 갖춘 regular type 위에 세워야 한다고 주장했다.

상속 없이 다형성을 구현하는 방법. 컴파일 타임 타입을 런타임에 지우고, 필요할 때 복원하는 기술. 이 글에서는 Type Erasure의 본질을 정리하고, `std::function`/`std::any`/`std::variant`의 내부 구조와 커스텀 구현, 그리고 가상 함수와의 성능 비교를 다룬다.

가상 함수의 동적 디스패치는 객체 내부의 vptr이 타입별 vtable을 가리키고, 호출 시 함수 포인터를 간접 참조하는 구조다. 이 글에서는 Type Erasure가 그 메커니즘을 명시적인 함수 포인터 테이블 형태로 어떻게 재구성하는지에 초점을 맞춘다.


---

## 목차
1. [std::function의 내부](#1-stdfunction의-내부)
2. [std::any: 타입을 완전히 지우기](#2-stdany-타입을-완전히-지우기)
3. [std::variant: 타입을 제한적으로 지우기](#3-stdvariant-타입을-제한적으로-지우기)
4. [커스텀 Type Erasure 구현](#4-커스텀-type-erasure-구현)
5. [Sean Parent의 Inheritance-Free Polymorphism](#5-sean-parent의-inheritance-free-polymorphism)
6. [성능 분석: Virtual vs Type Erasure vs Variant](#6-성능-분석-virtual-vs-type-erasure-vs-variant)
7. [실전 적용: 게임 엔진의 선택](#7-실전-적용-게임-엔진의-선택)
8. [고급 테크닉: SBO 최적화](#8-고급-테크닉-sbo-최적화)
9. [Rust의 Trait Objects vs C++ Type Erasure](#9-rust의-trait-objects-vs-c-type-erasure)


---

## 1. std::function의 내부

### 문제: 서로 다른 타입의 callable 저장

```cpp
void free_func() { std::cout << "function\n"; }

struct Functor {
    void operator()() { std::cout << "functor\n"; }
};

auto lambda = []() { std::cout << "lambda\n"; };

// std::function은 모두 저장할 수 있다
std::function<void()> f1 = free_func;
std::function<void()> f2 = Functor{};
std::function<void()> f3 = lambda;
```

세 callable의 타입은 다음과 같다.

- `free_func` 자체의 타입: `void()` 함수 타입. 저장 시에는 `void(*)()` 함수 포인터로 decay한다.
- `Functor{}`의 타입: `Functor`.
- `lambda`의 타입: `__lambda_1_impl`(컴파일러 생성).

세 타입은 호환되지 않는다. 그런데 하나의 `std::function<void()>`에 모두 저장된다.

### 순진한 접근: 가상 함수

```cpp
// 시도 1: 가상 함수로 해결
class CallableBase {
public:
    virtual void operator()() = 0;
    virtual ~CallableBase() = default;
};

template<typename F>
class CallableImpl : public CallableBase {
    F func_;
public:
    CallableImpl(F f) : func_(std::move(f)) {}
    void operator()() override { func_(); }
};

class function {
    std::unique_ptr<CallableBase> impl_;
public:
    template<typename F>
    function(F f) : impl_(std::make_unique<CallableImpl<F>>(std::move(f))) {}

    void operator()() { (*impl_)(); }
};
```

이 방식은 작동한다. 실제로 많은 Type Erasure 구현이 이 패턴을 사용한다. 하지만 두 가지 문제가 있다.

- 이 단순 구현에서는 작은 람다도 항상 힙 할당한다.
- 호출마다 vptr → vtable → 간접 호출의 오버헤드가 발생한다.

### 해결: Small Buffer Optimization (SBO)

`std::function` 구현은 작은 callable을 내부에 저장할 수 있다. 다음은 그 원리를 보이는 축약 모형이며 실제 표준 라이브러리 소스는 아니다.

```cpp
template<typename Signature>
class function;

template<typename R, typename... Args>
class function<R(Args...)> {
    static constexpr size_t BufferSize = 16;
    alignas(std::max_align_t) char buffer_[BufferSize];

    void* heap_ptr_ = nullptr;

    // vtable 역할: 함수 포인터 테이블
    struct VTable {
        R (*invoke)(void*, Args...);
        void (*destroy)(void*);
        // 버퍼 경로면 dst에 placement new 하고 nullptr을, 힙 경로면 새로 할당한 포인터를 반환한다.
        void* (*copy)(void* dst, const void* src);
        // 버퍼 경로 전용. 힙 경로는 포인터만 옮기므로 호출되지 않는다.
        void (*move)(void* dst, void* src);
    };

    const VTable* vtable_ = nullptr;

    void* obj() noexcept { return heap_ptr_ ? heap_ptr_ : buffer_; }
    const void* obj() const noexcept { return heap_ptr_ ? heap_ptr_ : buffer_; }

    void reset() noexcept {
        if (vtable_) {
            vtable_->destroy(obj());
            heap_ptr_ = nullptr;
            vtable_ = nullptr;
        }
    }

    void take_from(function& src) noexcept {
        if (!src.vtable_) return;
        if (src.heap_ptr_) {
            heap_ptr_ = src.heap_ptr_;          // 힙 객체는 포인터만 훔친다
            src.heap_ptr_ = nullptr;
        } else {
            src.vtable_->move(buffer_, src.buffer_);
        }
        vtable_ = src.vtable_;
        src.vtable_ = nullptr;
    }

public:
    function() = default;

    template<typename F>
    function(F f) {
        using Functor = std::decay_t<F>;

        if constexpr (sizeof(Functor) <= BufferSize &&
                      alignof(Functor) <= alignof(std::max_align_t) &&
                      std::is_nothrow_move_constructible_v<Functor>) {
            new(buffer_) Functor(std::move(f));
            vtable_ = &get_vtable_for<Functor, true>();
        } else {
            heap_ptr_ = new Functor(std::move(f));
            vtable_ = &get_vtable_for<Functor, false>();
        }
    }

    function(const function& other) {
        if (other.vtable_) {
            heap_ptr_ = other.vtable_->copy(buffer_, other.obj());
            vtable_ = other.vtable_;
        }
    }

    function(function&& other) noexcept { take_from(other); }

    function& operator=(const function& other) {
        if (this != &other) {
            function tmp(other);   // 복사가 던져도 *this는 그대로 남는다
            reset();
            take_from(tmp);
        }
        return *this;
    }

    function& operator=(function&& other) noexcept {
        if (this != &other) {
            reset();
            take_from(other);
        }
        return *this;
    }

    ~function() { reset(); }

    R operator()(Args... args) {
        if (!vtable_) {
            throw std::bad_function_call();  // std::function과 같은 빈 상태 계약
        }
        return vtable_->invoke(obj(), std::forward<Args>(args)...);
    }

private:
    template<typename Functor, bool InBuffer>
    static const VTable& get_vtable_for() {
        static const VTable vtable = {
            [](void* obj, Args... args) -> R {
                auto* functor = static_cast<Functor*>(obj);
                if constexpr (std::is_void_v<R>) {
                    (*functor)(std::forward<Args>(args)...);
                } else {
                    return (*functor)(std::forward<Args>(args)...);
                }
            },
            [](void* obj) {
                auto* functor = static_cast<Functor*>(obj);
                if constexpr (InBuffer) {
                    // 버퍼에 placement new로 만든 객체는 소멸자만 호출한다.
                    functor->~Functor();
                } else {
                    // 힙 객체는 delete가 소멸자 호출 + 메모리 해제를 같이 처리한다.
                    delete functor;
                }
            },
            [](void* dst, const void* src) -> void* {
                const auto* s = static_cast<const Functor*>(src);
                if constexpr (InBuffer) {
                    new(dst) Functor(*s);
                    return nullptr;
                } else {
                    return new Functor(*s);
                }
            },
            [](void* dst, void* src) {
                if constexpr (InBuffer) {
                    auto* s = static_cast<Functor*>(src);
                    new(dst) Functor(std::move(*s));
                    s->~Functor();
                }
            }
        };
        return vtable;
    }
};
```

### 핵심 테크닉

**1) 타입 지우기**

```cpp
template<typename F>
function(F f) {
    // F의 타입 정보는 생성자에서만 알려짐
    new(buffer_) F(std::move(f));

    // vtable에는 F를 위한 특수화된 함수 포인터 저장
    vtable_ = &get_vtable_for<F>();
}

// 이후 operator()에서는 F가 무엇인지 모름
// void* 포인터와 vtable을 통해 간접 호출
```

**2) SBO로 힙 할당 회피**

```cpp
auto small = [x = 0]() { return x; };       // sizeof(small) == 4 → buffer_에 저장
auto big = [data = std::array<int, 100>()]() { };  // sizeof(big) == 400 → 힙 할당
```

**3) 명시적 vtable**

가상 함수의 vtable이 컴파일러에 의해 자동 생성되는 데 비해, Type Erasure는 함수 포인터 테이블을 프로그래머가 명시적으로 정의한다. 이로 인해 가상 함수보다 더 자유로운 의미론(임의 타입 저장, 값 시맨틱, 커스텀 SBO 크기 등)을 가질 수 있다. 호출 비용 자체는 두 방식 모두 한 번의 간접 호출이라는 점에서 비슷하다.

| 항목 | 가상 함수 | Type Erasure |
|------|-----------|--------------|
| vtable 위치 | 컴파일러가 자동 생성 | 프로그래머가 명시적 정의 |
| vptr 저장 | 객체 시작 부분 | 별도 멤버 변수 |
| 타입 안전성 | 컴파일 타임 검사 | 템플릿으로 보장 |
| 오버헤드 | vptr 8 bytes | vtable 포인터 8 bytes |
| 유연성 | 상속 계층 고정 | 임의 타입 저장 가능 |

## 2. std::any: 타입을 완전히 지우기

### 임의의 타입 저장

`std::function`은 시그니처가 고정되어 있다. `std::any`는 복사 생성 가능한 여러 타입의 값을 저장한다. move-only 타입까지 아무 제약 없이 담는 컨테이너는 아니다.

```cpp
std::any a = 42;                    // int
a = std::string("hello");           // string
a = std::vector<double>{1.0, 2.0};  // vector<double>

int* pi = std::any_cast<int>(&a);                       // nullptr (현재 vector<double>)
auto* pv = std::any_cast<std::vector<double>>(&a);      // 성공
```

### std::any의 구조

```cpp
class any {
    static constexpr size_t BufferSize = 32;
    alignas(std::max_align_t) char buffer_[BufferSize];
    void* heap_ptr_ = nullptr;

    const std::type_info* type_ = nullptr;

    struct VTable {
        void (*destroy)(void*);
        // 버퍼 경로면 dst에 placement new 하고 nullptr을, 힙 경로면 새로 할당한 포인터를 반환한다.
        void* (*copy)(void* dst, const void* src);
        // 버퍼 경로 전용. 힙 경로는 포인터만 옮기므로 호출되지 않는다.
        void (*move)(void* dst, void* src);
    };
    const VTable* vtable_ = nullptr;

    void take_from(any& src) noexcept {
        if (!src.vtable_) return;
        if (src.heap_ptr_) {
            heap_ptr_ = src.heap_ptr_;          // 힙 객체는 포인터만 훔친다
            src.heap_ptr_ = nullptr;
        } else {
            src.vtable_->move(buffer_, src.buffer_);
        }
        type_ = src.type_;
        vtable_ = src.vtable_;
        src.type_ = nullptr;
        src.vtable_ = nullptr;
    }

public:
    any() = default;

    template<typename T>
    any(T value) {
        using ValueType = std::decay_t<T>;

        if constexpr (sizeof(ValueType) <= BufferSize &&
                      alignof(ValueType) <= alignof(std::max_align_t) &&
                      std::is_nothrow_move_constructible_v<ValueType>) {
            new(buffer_) ValueType(std::move(value));
        } else {
            heap_ptr_ = new ValueType(std::move(value));
        }

        type_ = &typeid(ValueType);
        vtable_ = &get_vtable<ValueType>();
    }

    any(const any& other) {
        if (other.vtable_) {
            heap_ptr_ = other.vtable_->copy(buffer_, other.data());
            type_ = other.type_;
            vtable_ = other.vtable_;
        }
    }

    any(any&& other) noexcept { take_from(other); }

    any& operator=(const any& other) {
        if (this != &other) {
            any tmp(other);        // 복사가 던져도 *this는 그대로 남는다
            reset();
            take_from(tmp);
        }
        return *this;
    }

    any& operator=(any&& other) noexcept {
        if (this != &other) {
            reset();
            take_from(other);
        }
        return *this;
    }

    ~any() { reset(); }

    void reset() noexcept {
        if (vtable_) {
            vtable_->destroy(data());
            heap_ptr_ = nullptr;
            type_ = nullptr;
            vtable_ = nullptr;
        }
    }

    bool has_value() const noexcept { return type_ != nullptr; }
    const std::type_info& type() const noexcept {
        return type_ ? *type_ : typeid(void);
    }

    void* data() noexcept {
        return heap_ptr_ ? heap_ptr_ : static_cast<void*>(buffer_);
    }

    const void* data() const noexcept {
        return heap_ptr_ ? heap_ptr_ : static_cast<const void*>(buffer_);
    }

    template<typename T>
    T* cast() {
        if (type_ && *type_ == typeid(T)) {
            return static_cast<T*>(heap_ptr_ ? heap_ptr_ : buffer_);
        }
        return nullptr;
    }

private:
    template<typename T>
    static const VTable& get_vtable() {
        constexpr bool in_buffer = sizeof(T) <= BufferSize &&
                                   alignof(T) <= alignof(std::max_align_t) &&
                                   std::is_nothrow_move_constructible_v<T>;
        static const VTable vt = {
            [](void* obj) {
                auto* p = static_cast<T*>(obj);
                if constexpr (in_buffer) { p->~T(); } else { delete p; }
            },
            [](void* dst, const void* src) -> void* {
                const auto* s = static_cast<const T*>(src);
                if constexpr (in_buffer) { new(dst) T(*s); return nullptr; }
                else { return new T(*s); }
            },
            [](void* dst, void* src) {
                if constexpr (in_buffer) {
                    auto* s = static_cast<T*>(src);
                    new(dst) T(std::move(*s));
                    s->~T();
                }
            }
        };
        return vt;
    }
};
```

### any_cast의 내부

```cpp
template<typename T>
T* any_cast(any* operand) {
    if (operand && operand->type() == typeid(T)) {
        return static_cast<T*>(operand->data());
    }
    return nullptr;
}

template<typename T>
const T* any_cast(const any* operand) {
    if (operand && operand->type() == typeid(T)) {
        return static_cast<const T*>(operand->data());
    }
    return nullptr;
}

template<typename T>
T any_cast(const any& operand) {
    using U = std::remove_cvref_t<T>;
    auto* p = any_cast<U>(&operand);
    if (!p) {
        throw std::bad_any_cast();
    }
    return static_cast<T>(*p);
}
```

### std::any의 비용

`sizeof(std::any)`, SBO 버퍼 크기와 SBO 허용 조건은 표준이 정하지 않는다. 어떤 구현은 nothrow-move 가능한 작은 타입만 내부에 저장하고 나머지는 힙을 쓴다. `any_cast` 비용도 타입 메타데이터 구현, 인라이닝, 성공·실패 경로에 따라 달라진다.

| 경로 | 확인할 비용 |
|---|---|
| 작은 객체 생성·이동 | SBO 적용 여부와 manager 함수 호출 |
| 큰 객체 생성·복사 | 할당 횟수, 복사 생성자, 예외 경로 |
| 성공 cast | 타입 검사와 값 복사/참조 반환 |
| 실패 cast | 포인터 overload의 null과 값 overload의 예외 비용 |

`std::any`는 편리하지만, 타입을 미리 알 수 있다면 `std::variant`가 더 효율적이다.

## 3. std::variant: 타입을 제한적으로 지우기

### any의 오버헤드 회피

위 `any` 모형은 RTTI로 타입을 비교하지만 모든 구현의 `any_cast`가 반드시 RTTI를 쓰는 것은 아니다. 예를 들어 libstdc++는 타입별 manager 함수 주소도 비교한다. 포인터 형태 `any_cast`의 실패는 nullptr이고 값·참조 형태의 실패는 예외다. `std::variant`는 타입 집합을 컴파일 타임에 고정하고 discriminator로 구분하므로 별도 런타임 타입 탐색이 필요 없다.

```cpp
std::variant<int, float, std::string> v;

v = 42;
v = 3.14f;
v = "hello";

std::visit([](auto&& arg) {
    std::cout << arg << "\n";
}, v);

if (auto* p = std::get_if<int>(&v)) {
    std::cout << *p;
}
```

### std::variant의 구조

```cpp
template<typename... Types>
class variant {
    static constexpr size_t BufferSize = std::max({sizeof(Types)...});
    static constexpr size_t Alignment = std::max({alignof(Types)...});

    alignas(Alignment) char buffer_[BufferSize];
    size_t index_ = 0;

    using DestroyFunc = void(*)(void*);
    static constexpr DestroyFunc destroy_table_[] = {
        [](void* obj) { static_cast<Types*>(obj)->~Types(); }...
    };

public:
    template<typename T>
    variant(T&& value) {
        using U = std::decay_t<T>;
        constexpr size_t index = index_of<U, Types...>();

        new(buffer_) U(std::forward<T>(value));
        index_ = index;
    }

    // 이 골격은 타입별 복사/이동 테이블을 생략했으므로 암시적 바이트 복사를 막는다
    variant(const variant&) = delete;
    variant& operator=(const variant&) = delete;
    variant(variant&&) = delete;
    variant& operator=(variant&&) = delete;

    ~variant() {
        destroy_table_[index_](buffer_);
    }

    size_t index() const noexcept { return index_; }

    template<typename T>
    T* get_if() {
        constexpr size_t index = index_of<T, Types...>();
        if (index_ == index) {
            return static_cast<T*>(static_cast<void*>(buffer_));
        }
        return nullptr;
    }

private:
    template<typename T, typename First, typename... Rest>
    static constexpr size_t index_of() {
        if constexpr (std::is_same_v<T, First>) {
            return 0;
        } else {
            return 1 + index_of<T, Rest...>();
        }
    }
};
```

### std::visit의 마법

`std::visit`은 표준이 구현 전략을 규정하지는 않지만, 보통 컴파일 타임에 모든 타입 조합을 분기하는 테이블을 만들어 dispatch한다.

```cpp
// 실제 구현처럼 보이지만 방문자 타입·반환형·valueless 처리를 생략한 구조 설명용 의사코드다.
template<typename Visitor, typename... Types>
auto visit(Visitor&& vis, variant<Types...>& v) {
    using ReturnType = /* 모든 호출의 공통 반환 타입 */;

    static constexpr auto jump_table = make_jump_table<Visitor, Types...>();

    return jump_table[v.index()](std::forward<Visitor>(vis), &v.buffer_);
}

template<typename Visitor, typename... Types>
constexpr auto make_jump_table() {
    return std::array{
        [](Visitor&& vis, void* buf) {
            return std::forward<Visitor>(vis)(*static_cast<Types*>(buf));
        }...
    };
}
```

### 성능 비교

```cpp
struct Data { virtual ~Data() = default; };
struct IntData : Data { int value; };
struct FloatData : Data { float value; };
struct StringData : Data { std::string value; };

// 1) 가상 함수
std::vector<std::unique_ptr<Data>> vec_virtual;

// 2) std::any
std::vector<std::any> vec_any;

// 3) std::variant
using DataVariant = std::variant<IntData, FloatData, StringData>;
std::vector<DataVariant> vec_variant;
```

이 세 표현은 저장 레이아웃까지 같게 만들지 않으면 호출 방식만 비교할 수 없다. `unique_ptr<Data>`는 pointee를 흩어 놓고, `any`는 구현과 타입 크기에 따라 SBO/heap이 갈리며, `variant`는 가장 큰 대안의 크기와 정렬을 모든 원소가 부담한다.

`variant`의 가능한 이점은 다음과 같다.

1. **닫힌 타입 집합**: 정수 discriminator로 분기하고 잘못된 타입은 컴파일러가 더 잘 진단한다.
2. **인라인 저장**: 대안 객체가 variant 내부에 있어 별도 pointee 할당을 피한다.
3. **최적화 가능성**: 방문자가 보이면 컴파일러가 분기와 대안 코드를 인라인할 수 있다.
4. **연속 배치**: `vector<variant<...>>` 자체는 연속이지만, 가장 큰 대안 때문에 stride가 커질 수 있다.

비교 시에는 같은 의미의 payload, 같은 타입 분포와 방문 순서, 같은 할당 전략을 사용하고 총 bytes/element, branch miss, cache miss, p95 시간을 함께 기록한다. 수동 tagged union도 수명·예외 안전성을 정확히 구현해야 하며 자동으로 더 빠르다고 가정할 수 없다.

## 4. 커스텀 Type Erasure 구현

### std::function의 한계

`std::function`은 범용적이지만, 게임에서는 제약이 있다.

- 복사 가능해야 함(move-only 람다 불가).
- SBO 크기가 고정됨(큰 캡처는 힙 할당).
- 예외를 사용함(일부 게임 엔진은 예외 비활성화).

게임 엔진은 종종 자체 Type Erasure를 구현한다.

### Move-Only Function

```cpp
template<typename Signature>
class unique_function;

template<typename R, typename... Args>
class unique_function<R(Args...)> {
    static constexpr size_t BufferSize = 32;
    alignas(std::max_align_t) char buffer_[BufferSize];
    void* heap_ptr_ = nullptr;

    struct VTable {
        R (*invoke)(void*, Args...);
        void (*destroy)(void*);
        void (*move)(void* dst, void* src);
    };
    const VTable* vtable_ = nullptr;

public:
    unique_function() = default;

    template<typename F>
    unique_function(F&& f) {
        using Functor = std::decay_t<F>;

        constexpr bool use_sbo =
            sizeof(Functor) <= BufferSize &&
            alignof(Functor) <= alignof(std::max_align_t) &&
            std::is_nothrow_move_constructible_v<Functor>;

        if constexpr (use_sbo) {
            new(buffer_) Functor(std::forward<F>(f));
        } else {
            heap_ptr_ = new Functor(std::forward<F>(f));
        }

        vtable_ = &get_vtable<Functor>();
    }

    unique_function(const unique_function&) = delete;
    unique_function& operator=(const unique_function&) = delete;

    unique_function(unique_function&& other) noexcept {
        if (other.vtable_) {
            vtable_ = other.vtable_;
            if (other.heap_ptr_) {
                // 힙 객체는 포인터만 훔치면 된다 (객체 이동 불필요)
                heap_ptr_ = other.heap_ptr_;
                other.heap_ptr_ = nullptr;
            } else {
                // 버퍼 객체는 이동 생성으로 옮긴다
                vtable_->move(buffer_, other.buffer_);
            }
            other.vtable_ = nullptr;
        }
    }

    unique_function& operator=(unique_function&& other) noexcept {
        if (this != &other) {
            if (vtable_) {
                vtable_->destroy(heap_ptr_ ? heap_ptr_ : buffer_);
                vtable_ = nullptr;
                heap_ptr_ = nullptr;
            }
            if (other.vtable_) {
                vtable_ = other.vtable_;
                if (other.heap_ptr_) {
                    heap_ptr_ = other.heap_ptr_;
                    other.heap_ptr_ = nullptr;
                } else {
                    vtable_->move(buffer_, other.buffer_);
                }
                other.vtable_ = nullptr;
            }
        }
        return *this;
    }

    R operator()(Args... args) {
        void* obj_ptr = heap_ptr_ ? heap_ptr_ : buffer_;
        return vtable_->invoke(obj_ptr, std::forward<Args>(args)...);
    }

    ~unique_function() {
        if (vtable_) {
            void* obj_ptr = heap_ptr_ ? heap_ptr_ : buffer_;
            vtable_->destroy(obj_ptr);
        }
    }

private:
    template<typename Functor>
    static const VTable& get_vtable() {
        constexpr bool in_buffer =
            sizeof(Functor) <= BufferSize &&
            alignof(Functor) <= alignof(std::max_align_t) &&
            std::is_nothrow_move_constructible_v<Functor>;

        static const VTable vt = {
            [](void* obj, Args... args) -> R {
                if constexpr (std::is_void_v<R>) {
                    (*static_cast<Functor*>(obj))(std::forward<Args>(args)...);
                } else {
                    return (*static_cast<Functor*>(obj))(std::forward<Args>(args)...);
                }
            },
            [](void* obj) {
                auto* f = static_cast<Functor*>(obj);
                if constexpr (in_buffer) {
                    f->~Functor();   // 버퍼 객체: 소멸자만
                } else {
                    delete f;        // 힙 객체: delete가 소멸자까지 처리
                }
            },
            [](void* dst, void* src) {
                // 버퍼 객체 전용 (힙 객체는 포인터를 훔치므로 호출되지 않음)
                auto* old = static_cast<Functor*>(src);
                new(dst) Functor(std::move(*old));
                old->~Functor();
            }
        };
        return vt;
    }
};
```

사용 예시:

```cpp
// std::function으로는 불가능
auto ptr = std::make_unique<int>(42);
unique_function<int()> f = [p = std::move(ptr)]() { return *p; };

unique_function<int()> f2 = std::move(f);  // OK
int result = f2();  // 42
```

이 골격은 비어 있거나 이동된 `unique_function`을 호출하지 않는 계약이다. `unique_function<void()>`에는 값을 반환하는 callable도 넣을 수 있으며 반환값은 버린다. 표준 C++23에는 같은 용도의 `std::move_only_function`도 있다.

### Delegate: 멤버 함수 포인터 지원

```cpp
template<typename Signature>
class delegate;

template<typename R, typename... Args>
class delegate<R(Args...)> {
    void* object_ = nullptr;
    R (*stub_)(void*, Args...) = nullptr;

public:
    delegate() = default;

    // 자유 함수
    template<R(*Func)(Args...)>
    static delegate from_function() {
        delegate d;
        d.stub_ = [](void*, Args... args) -> R {
            return Func(std::forward<Args>(args)...);
        };
        return d;
    }

    // 멤버 함수
    template<typename C, R(C::*Method)(Args...)>
    static delegate from_method(C* obj) {
        delegate d;
        d.object_ = obj;
        d.stub_ = [](void* obj, Args... args) -> R {
            return (static_cast<C*>(obj)->*Method)(std::forward<Args>(args)...);
        };
        return d;
    }

    // 람다 (저장소 필요)
    // 주의: static 저장소는 F 타입당 하나뿐이다. 같은 타입의 람다로
    // 두 번째 delegate를 만들어도 첫 번째 객체가 유지된다. 데모용 코드이며,
    // 실전에서는 별도 버퍼나 unique_function을 사용해야 한다.
    template<typename F>
    delegate(F&& f) requires (!std::is_same_v<std::decay_t<F>, delegate>) {
        // F가 참조 타입이면 static_cast<F*>가 참조의 포인터가 되어 컴파일되지 않으므로 decay한다
        using Functor = std::decay_t<F>;
        static Functor stored = std::forward<F>(f);
        object_ = &stored;
        stub_ = [](void* obj, Args... args) -> R {
            return (*static_cast<Functor*>(obj))(std::forward<Args>(args)...);
        };
    }

    R operator()(Args... args) const {
        return stub_(object_, std::forward<Args>(args)...);
    }

    explicit operator bool() const { return stub_ != nullptr; }
};
```

사용 예시:

```cpp
struct Player {
    void take_damage(int amount) { health -= amount; }
    int health = 100;
};

void log_damage(int amount) {
    std::cout << "Damage: " << amount << "\n";
}

Player player;
delegate<void(int)> on_damage;

on_damage = delegate<void(int)>::from_function<&log_damage>();
on_damage(25);  // "Damage: 25"

on_damage = delegate<void(int)>::from_method<Player, &Player::take_damage>(&player);
on_damage(25);  // player.health = 75
```

크기 비교:

다음은 64비트 구현의 크기 예시다. 라이브러리 버전과 ABI에 따라 달라지므로 사용하는 빌드에서 `sizeof`로 확인한다. 멤버 함수 delegate는 객체를 소유하지 않으므로 대상은 호출이 끝날 때까지 살아 있어야 하며, 빈 delegate는 호출하지 않는다.

| 타입 | 크기 | 힙 할당 |
|------|:----:|:-------:|
| `std::function<void(int)>` | 32 bytes (libstdc++ 기준; libc++ 48, MSVC x64 64) | 때로 |
| `delegate<void(int)>` | 16 bytes | 없음 |
| 함수 포인터 | 8 bytes | 없음 |

## 5. Sean Parent의 Inheritance-Free Polymorphism

### 상속 계층의 경직성

전통적인 OOP:

```cpp
class Drawable {
public:
    virtual void draw() const = 0;
    virtual ~Drawable() = default;
};

class Circle : public Drawable {
public:
    void draw() const override { /* ... */ }
};

class Rectangle : public Drawable {
public:
    void draw() const override { /* ... */ }
};

// 새 타입 추가 시 Drawable 상속 필수
// 기존 타입을 Drawable로 만들 수 없음 (예: 서드파티 라이브러리)
```

### External Polymorphism

Sean Parent의 아이디어:

```cpp
// 어떤 타입이든 draw() 메서드만 있으면 저장 가능
class object {
    struct concept_t {
        virtual ~concept_t() = default;
        virtual void draw() const = 0;
        virtual concept_t* clone() const = 0;
    };

    template<typename T>
    struct model_t : concept_t {
        T data_;

        model_t(T x) : data_(std::move(x)) {}

        void draw() const override {
            draw_impl(data_);  // ADL로 찾음
        }

        concept_t* clone() const override {
            return new model_t(*this);
        }
    };

    std::unique_ptr<concept_t> self_;

public:
    template<typename T>
    object(T x) : self_(std::make_unique<model_t<T>>(std::move(x))) {}

    object(const object& x) : self_(x.self_->clone()) {}
    object(object&&) noexcept = default;

    object& operator=(object x) noexcept {
        self_.swap(x.self_);
        return *this;
    }

    void draw() const {
        self_->draw();
    }
};

// 자유 함수로 draw 구현
struct Circle { /* ... */ };
void draw_impl(const Circle& c) { /* ... */ }

struct Rectangle { /* ... */ };
void draw_impl(const Rectangle& r) { /* ... */ }

// 사용
std::vector<object> shapes;
shapes.emplace_back(Circle{});
shapes.emplace_back(Rectangle{});

for (const auto& shape : shapes) {
    shape.draw();
}
```

이 골격에서 `object`의 이동 생성자는 기본 구현이므로 이동한 뒤 `self_`는 비어 있다. `draw`와 복사 생성자는 `self_`를 역참조하므로, 비어 있는 `object`를 복사하거나 `draw`를 호출하지 않도록 비어 있지 않음이 보장된 상태에서만 사용한다.

### 장점

1. **Non-intrusive**: 상속 불필요, 기존 타입 그대로 사용.
2. **Value semantics**: 복사 가능, 스택 저장 가능.
3. **Type-safe**: 컴파일 타임 체크.
4. **확장 가능**: 새 타입 추가 시 기존 코드 수정 불필요(새 연산은 `concept_t`와 `model_t` 수정이 필요하다).

### 실전 예제: Document Storage

```cpp
class document {
    struct concept_t {
        virtual ~concept_t() = default;
        virtual void draw() const = 0;
        virtual void serialize(std::ostream&) const = 0;
        virtual concept_t* clone() const = 0;
    };

    template<typename T>
    struct model_t : concept_t {
        T data_;

        model_t(T x) : data_(std::move(x)) {}

        void draw() const override { draw_impl(data_); }
        void serialize(std::ostream& os) const override { serialize_impl(data_, os); }
        concept_t* clone() const override { return new model_t(*this); }
    };

    std::unique_ptr<concept_t> self_;

public:
    template<typename T>
    document(T x) : self_(std::make_unique<model_t<T>>(std::move(x))) {}

    document(const document& x) : self_(x.self_->clone()) {}
    document(document&&) noexcept = default;
    document& operator=(document x) { self_ = std::move(x.self_); return *this; }

    void draw() const { self_->draw(); }
    void serialize(std::ostream& os) const { self_->serialize(os); }
};

struct MyShape {
    int x, y, radius;
};

void draw_impl(const MyShape& s) {
    std::cout << "Circle at (" << s.x << ", " << s.y << ")\n";
}

void serialize_impl(const MyShape& s, std::ostream& os) {
    os << s.x << " " << s.y << " " << s.radius;
}

std::vector<document> docs;
docs.emplace_back(MyShape{10, 20, 5});
docs[0].draw();
```

## 6. 성능 분석: Virtual vs Type Erasure vs Variant

### 테스트 시나리오

```cpp
// 1) 가상 함수
struct ShapeBase {
    virtual double area() const = 0;
    virtual ~ShapeBase() = default;
};

struct CircleData {
    double radius;
    double area() const { return 3.14159 * radius * radius; }
};

struct RectangleData {
    double width, height;
    double area() const { return width * height; }
};

struct Circle : ShapeBase {
    CircleData value;
    double area() const override { return value.area(); }
};

struct Rectangle : ShapeBase {
    RectangleData value;
    double area() const override { return value.area(); }
};

std::vector<std::unique_ptr<ShapeBase>> shapes_virtual;

// 2) Type Erasure (std::function 스타일)
class shape_erased {
    std::unique_ptr<void, void(*)(void*)> data_;
    double (*area_func_)(const void*);

public:
    template<typename T>
    shape_erased(T shape)
        : data_(new T(std::move(shape)), [](void* p) { delete static_cast<T*>(p); })
        , area_func_([](const void* p) { return static_cast<const T*>(p)->area(); })
    {}

    double area() const { return area_func_(data_.get()); }
};

std::vector<shape_erased> shapes_erased;

// 3) std::variant
using Shape = std::variant<CircleData, RectangleData>;

std::vector<Shape> shapes_variant;

// 4) Tagged Union (수동)
enum class ShapeType { Circle, Rectangle };

struct shape_tagged {
    ShapeType type;
    union {
        CircleData circle;
        RectangleData rect;
    };

    explicit shape_tagged(CircleData value)
        : type(ShapeType::Circle), circle(value) {}
    explicit shape_tagged(RectangleData value)
        : type(ShapeType::Rectangle), rect(value) {}

    double area() const {
        switch (type) {
            case ShapeType::Circle: return circle.area();
            case ShapeType::Rectangle: return rect.area();
        }
        return 0;
    }
};

std::vector<shape_tagged> shapes_tagged;
```

### 벤치마크 설계

고정된 시간·메모리 수치는 컴파일러, 타입 크기, 타입 순서와 할당 전략을 숨기므로 보편적 결과로 제시할 수 없다. 같은 payload와 연산을 사용해 다음 축을 기록한다.

| 방식 | 주된 구조적 비용 | 반드시 측정할 항목 |
|------|------------------|--------------------|
| 가상 함수 | pointee 배치, vptr, 간접 분기 | allocation 수, branch miss, cache/TLB miss |
| Type Erasure | 저장 정책, manager/vtable 간접 호출 | SBO hit율, 객체 크기, allocation 수 |
| `std::variant` | 가장 큰 대안 크기 + discriminator | `sizeof`, 타입 분포, 방문 분기 |
| Tagged Union | 수동 수명 관리 + discriminator | 객체 stride, branch miss, 예외 안전성 |
| 타입별 직접 배열 | 타입별 loop와 데이터 분리 | 총 순회 시간, 추가 분류·이동 비용 |

`sizeof(variant)`는 가장 큰 대안과 discriminator, 정렬·패딩을 포함하므로 대안 정의가 다르면 비교 자체가 달라진다. warm-up 뒤 여러 타입 분포와 정렬·무작위 순서를 각각 측정하고 컴파일러의 devirtualization 여부를 어셈블리로 확인한다.

### 분석

가상 함수와 Type Erasure 모두 간접 호출을 사용할 수 있지만 저장 정책, 인라이닝 가능성과 데이터 배치가 다르므로 전체 성능이 같다고 단정할 수는 없다.

`variant`가 이점을 얻을 수 있는 조건은 다음과 같다.

- 타입이 컴파일 타임에 알려져 있으므로 점프 테이블이 인라인 가능.
- 데이터가 연속 메모리에 위치(캐시 친화적).
- 컴파일러가 `std::visit` 내부를 인라인 가능.

Tagged Union의 극한 최적화:

- 컴파일러가 switch를 완전히 최적화 가능.
- 분기 예측이 잘 작동(패턴이 일정하면).

타입별 직접 배열로 순회하는 경우:

```cpp
std::vector<CircleData> circles;
std::vector<RectangleData> rectangles;

for (auto& c : circles) total += c.area();
for (auto& r : rectangles) total += r.area();
```

타입별로 일괄 처리할 수 있다면 유리한 후보지만, 타입 사이의 처리 순서를 보존해야 하거나 분류·이동 비용이 크면 그 비용까지 비교해야 한다.

## 7. 실전 적용: 게임 엔진의 선택

아래 코드는 실제 엔진 소스를 인용한 것이 아니라 각 엔진의 설계 경향을 요약한 유사 설계 의사코드다. Unreal의 `Tick`/`BeginPlay`는 `UObject`가 아니라 `AActor`·`UActorComponent`에 있고, `FProperty`는 값 저장과 리플렉션 메타데이터를 함께 다룬다.

### Unreal Engine: 가상 함수 + Type Erasure 혼용

```cpp
// UObject 계층(예: AActor): 가상 함수 사용
class AActor {
public:
    virtual void Tick(float DeltaTime) {}
    virtual void BeginPlay() {}
};

// FTimerDelegate: Type Erasure
struct FTimerDelegate {
    TFunction<void()> Callback;
};

// FProperty: Type Erasure로 임의 타입 저장
class FProperty {
    void* ValuePtr;
    const FPropertyVTable* VTable;
};
```

선택 기준:

- UObject 계층: 가상 함수(개발자 편의성).
- 델리게이트, 프로퍼티: Type Erasure(유연성).

### Unity (Entities): ECS + Type Erasure

```cpp
// 컴포넌트는 타입별 배열 (DoD)
struct TransformComponent { /* ... */ };
struct RigidbodyComponent { /* ... */ };

// 시스템은 Type Erasure로 저장
class ISystem {
    virtual void Update(float dt) = 0;
};

std::vector<std::unique_ptr<ISystem>> systems;

// 하지만 실제 데이터는 타입별 분리
std::vector<TransformComponent> transforms;
std::vector<RigidbodyComponent> rigidbodies;
```

### ID Tech (Doom): Tagged Union + 함수 포인터

```cpp
enum entity_type {
    ET_PLAYER,
    ET_MONSTER,
    ET_ITEM
};

struct entity {
    entity_type type;

    union {
        player_state_t player;
        monster_state_t monster;
        item_state_t item;
    };

    void (*think)(entity* self, float dt);
};
```

이 방식의 이유:

- C 코드와 호환.
- 메모리 레이아웃 완전 제어.
- 캐시 최적화 용이.

### 선택 가이드

| 상황 | 권장 방식 | 이유 |
|------|-----------|------|
| 플러그인 시스템, 스크립팅 | Type Erasure | 타입 독립성 |
| 이벤트, 콜백 | `std::function` 또는 커스텀 | 유연성 |
| 엔티티 시스템 (대량) | DoD (타입별 배열) | 성능 |
| 고정된 타입 집합 | `std::variant` | 타입 안전성 + 성능 |
| UI 위젯 | 가상 함수 | 개발 편의성 |
| 네트워크 패킷 | Tagged Union | 직렬화 용이 |

## 8. 고급 테크닉: SBO 최적화

### 정렬 고려 SBO

```cpp
template<typename Signature, size_t BufferSize = 32, size_t Alignment = alignof(std::max_align_t)>
class optimized_function;

template<typename R, typename... Args, size_t BufferSize, size_t Alignment>
class optimized_function<R(Args...), BufferSize, Alignment> {
    alignas(Alignment) char buffer_[BufferSize];
    void* heap_ptr_ = nullptr;

    // VTable...

public:
    template<typename F>
    optimized_function(F&& f) {
        using Functor = std::decay_t<F>;

        constexpr bool fits = sizeof(Functor) <= BufferSize &&
                              alignof(Functor) <= Alignment &&
                              std::is_nothrow_move_constructible_v<Functor>;

        if constexpr (fits) {
            void* aligned_ptr = buffer_;
            size_t space = BufferSize;
            if (!std::align(alignof(Functor), sizeof(Functor), aligned_ptr, space)) {
                throw std::bad_alloc();
            }
            new(aligned_ptr) Functor(std::forward<F>(f));
        } else {
            heap_ptr_ = ::operator new(
                sizeof(Functor), std::align_val_t{alignof(Functor)});
            try {
                new(heap_ptr_) Functor(std::forward<F>(f));
            } catch (...) {
                // 생성자가 던지면 이 객체의 소멸자는 실행되지 않으므로 여기서 해제한다
                ::operator delete(heap_ptr_, std::align_val_t{alignof(Functor)});
                heap_ptr_ = nullptr;
                throw;
            }
        }
    }
};
```

heap 경로의 vtable `destroy`는 `Functor` 소멸자를 호출한 뒤 생성 때 사용한 것과 같은 `std::align_val_t{alignof(Functor)}`를 전달해 aligned `operator delete`를 호출해야 한다. 생성과 해제의 allocator·alignment 쌍도 type-erased metadata의 일부다.

### Reference Counting SBO

```cpp
template<typename T, size_t BufferSize = 32>
class sbo_shared {
    struct control_block {
        std::atomic<int> refcount{1};
        alignas(T) char buffer[sizeof(T)];
    };

    control_block* control_ = nullptr;

    static constexpr bool use_sbo = sizeof(control_block) <= BufferSize;

    alignas(control_block) char inline_storage_[BufferSize];

public:
    // 인자가 sbo_shared 자신 하나뿐이면 제외해, 복사 생성자를 가로채지 않게 한다
    template<typename... Args>
        requires (!(sizeof...(Args) == 1 && (std::is_same_v<std::decay_t<Args>, sbo_shared> && ...)))
    sbo_shared(Args&&... args) {
        if constexpr (use_sbo) {
            control_ = new(inline_storage_) control_block();
        } else {
            control_ = new control_block();
        }
        try {
            new(control_->buffer) T(std::forward<Args>(args)...);
        } catch (...) {
            if constexpr (!use_sbo) delete control_;
            throw;
        }
    }

    sbo_shared(const sbo_shared& other) : control_(other.control_) {
        if (control_) {
            control_->refcount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 암시적 대입은 참조 카운트를 갱신하지 않고 포인터만 덮어쓰므로 이중 해제를 막기 위해 삭제한다
    sbo_shared& operator=(const sbo_shared&) = delete;

    ~sbo_shared() {
        if (control_ && control_->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            get()->~T();
            if constexpr (!use_sbo) {
                delete control_;
            }
        }
    }

    T* get() const { return std::launder(reinterpret_cast<T*>(control_->buffer)); }
};
```

이 코드에는 본질적인 한계가 있다. inline 저장(use_sbo) 시 복사본들이 원본 객체의 `inline_storage_`를 가리키므로, 원본이 사본보다 먼저 소멸하면 댕글링이 된다. 일반 `shared_ptr`이 요구하는 안정된 대상 주소와 독립적인 소유자 수명은 **대상 주소가 고정된 단순 owner-inline SBO와 양립하기 어려우며**, 이것이 `std::shared_ptr`이 제어 블록과 객체를 별도로 두는 이유다. 위 패턴은 원본의 수명이 모든 사본보다 길다고 보장되는 제한된 상황에서만 성립한다.

### 과도한 Cache Line Alignment를 피하라

```cpp
template<typename Signature>
class cache_aligned_function {
    static constexpr size_t CacheLineSize = 64;  // 대상 플랫폼에서 측정한 예시값

    alignas(CacheLineSize) char buffer_[CacheLineSize];

    alignas(CacheLineSize) const void* vtable_;
};
```

이 코드는 함수 래퍼 하나를 최소 두 cache line 규모로 팽창시키는 반례다. 서로 다른 스레드가 갱신하는 atomic 상태의 false sharing을 막는 경우에는 cache-line 정렬이 유효할 수 있지만, 불변 vtable pointer와 SBO buffer를 각각 정렬해 분리할 이유는 보통 없다. cache line도 모든 CPU에서 64바이트라는 표준 계약이 없으므로, `std::hardware_destructive_interference_size`의 지원 여부와 실제 접근 패턴을 확인해 필요한 공유 가변 상태에만 적용한다.

## 9. Rust의 Trait Objects vs C++ Type Erasure

### Rust의 접근

```rust
trait Drawable {
    fn draw(&self);
}

struct Circle { radius: f64 }
impl Drawable for Circle {
    fn draw(&self) { println!("Circle"); }
}

// Type erasure (dyn Trait)
let shapes: Vec<Box<dyn Drawable>> = vec![
    Box::new(Circle { radius: 5.0 }),
];

for shape in shapes {
    shape.draw();  // 동적 디스패치
}
```

### C++ 등가 코드

```cpp
template<typename T>
concept Drawable = requires(T t) {
    { t.draw() } -> std::same_as<void>;
};

template<Drawable T>
void process(T& obj) {
    obj.draw();  // 정적 디스패치 (템플릿)
}

// 동적 디스패치가 필요하면 Type Erasure
class erased_drawable {
    std::unique_ptr<void, void(*)(void*)> obj_;
    void (*draw_)(void*);

public:
    template<Drawable T>
    erased_drawable(T obj)
        : obj_(new T(std::move(obj)), [](void* p) { delete static_cast<T*>(p); })
        , draw_([](void* p) { static_cast<T*>(p)->draw(); })
    {}

    void draw() { draw_(obj_.get()); }
};
```

### 비교

| 항목 | Rust `dyn Trait` | C++ Type Erasure |
|------|------------------|------------------|
| 문법 | 언어 내장 | 수동 구현 |
| 안전성 | 컴파일 타임 보장 | 프로그래머 책임 |
| 성능 | Fat pointer (16 bytes) | 구현에 따라 다름 |
| 저장 방식 | `&dyn Trait`는 borrow라 별도 heap 불필요, `Box<dyn Trait>`는 heap 소유 | 구현에 따라 inline/SBO 또는 heap |


---

## 결론

Type Erasure는 C++ 템플릿의 정수다. 컴파일 타임 타입 정보를 런타임 다형성으로 변환하는 기법이다.

핵심 원리:

1. **타입 지우기**: 생성자에서 타입 정보를 vtable에 담기.
2. **SBO**: 작은 객체는 힙 할당 회피.
3. **명시적 vtable**: 가상 함수 대신 함수 포인터 테이블.
4. **Value semantics**: 복사/이동 가능한 값 타입.

언제 사용할까:

- `std::function`: 콜백, 델리게이트.
- `std::any`: 복사 가능한 임의 타입 저장(타입 검사 방식은 구현에 따름).
- `std::variant`: 고정된 타입 집합(RTTI 불필요).
- 커스텀 구현: 특수 요구사항(move-only, 큰 SBO 등).

성능 특성:

- 가상 함수와 거의 동일(둘 다 간접 호출).
- SBO로 힙 할당 회피 가능.
- `std::variant`는 닫힌 타입 집합의 인라인 저장과 분기 최적화를 활용할 수 있음.
- 타입별 배열은 일괄 처리에 유리하지만 순서·분류·이동 비용도 비교해야 함.

Sean Parent의 표현을 빌리자면, 좋은 C++ 코드는 복잡한 상속 계층이 아니라 일반 타입과 알고리즘에 관한 것이다. Type Erasure는 상속 없이 다형성을 달성하는 강력한 도구다. 다만 만능은 아니다. 성능이 중요하면 `std::variant`나 타입별 배열을, 유연성이 중요하면 Type Erasure를, 개발 편의성이 중요하면 가상 함수를 고른다. 트레이드오프를 이해하고 상황에 맞게 선택하는 것이 핵심이다.

## 더 읽을거리

- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function)
- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any)
- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)
- [P0088R3: Variant: a type-safe union for C++17](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0088r3.html)
- Klaus Iglberger, ["Breaking Dependencies: Type Erasure - A Design Analysis"](https://www.youtube.com/watch?v=4eeESJQk-mw), CppCon 2021
- Arthur O'Dwyer, ["Back to Basics: Type Erasure"](https://www.youtube.com/watch?v=tbUCHifyT24), CppCon 2019
- Kevlin Henney, "Valued Conversions", *C++ Report* 12(7), 2000
- Andrei Alexandrescu, "Modern C++ Design", Addison-Wesley, 2001 - Chapter 5: Generalized Functors
- Sean Parent, ["Inheritance Is The Base Class of Evil"](https://www.youtube.com/watch?v=bIhUE5uUFOA), GoingNative 2013
- Sean Parent, ["Value Semantics and Concepts-based Polymorphism"](https://www.youtube.com/watch?v=_BpMYeUFXv8), C++Now 2012
- Sean Parent, ["Better Code: Runtime Polymorphism"](https://www.youtube.com/watch?v=QGcVXgEVMJg), NDC 2017
- Chandler Carruth, ["There Are No Zero-cost Abstractions"](https://www.youtube.com/watch?v=rHIkrotSwcc), CppCon 2019
- [LLVM libc++ std::function](https://github.com/llvm/llvm-project/blob/main/libcxx/include/__functional/function.h)
- [LLVM libc++ std::any](https://github.com/llvm/llvm-project/blob/main/libcxx/include/any)
- [LLVM libc++ std::variant](https://github.com/llvm/llvm-project/blob/main/libcxx/include/variant)
- [Folly Function](https://github.com/facebook/folly/blob/main/folly/Function.h) - Facebook의 move-only function 구현
- [Function2](https://github.com/Naios/function2) - 고성능 함수 래퍼 라이브러리
- [Dyno](https://github.com/ldionne/dyno) - Louis Dionne의 런타임 다형성 라이브러리
- [Microsoft Proxy](https://github.com/microsoft/proxy) - Microsoft의 Type Erasure 라이브러리
- Boost.TypeErasure - [공식 문서](https://www.boost.org/doc/libs/release/doc/html/boost_typeerasure.html)
- Arthur O'Dwyer, ["What is Type Erasure?"](https://quuxplusone.github.io/blog/2019/03/18/what-is-type-erasure/)
- [Compiler Explorer (godbolt.org)](https://godbolt.org/) - Type Erasure의 어셈블리 확인
- [C++ Insights](https://cppinsights.io/) - 템플릿 확장 결과 확인
- [Quick Bench](https://quick-bench.com/) - Type Erasure vs Virtual vs Variant 성능 비교
