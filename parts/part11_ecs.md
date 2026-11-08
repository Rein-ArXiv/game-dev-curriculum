## 들어가며

게임 엔진과 시뮬레이션 시스템에서는 표준 컨테이너만으로 성능 요구를 맞추기 어려운 상황이 자주 등장한다. 60FPS를 유지하며 수만 엔티티를 갱신해야 하는 게임, 초당 수천 패킷을 처리하는 서버, 수백만 파티클을 렌더하는 이펙트 시스템 같은 워크로드는 동적 할당의 비결정성과 캐시 미스에 정면으로 부딪힌다.

이 글은 그런 워크로드에서 자주 등장하는 다섯 가지 자료구조 — Ring Buffer, Object Pool, Sparse Set, Slot Map, Archetype 기반 ECS — 를 각자의 설계 동기와 구현, 트레이드오프 관점에서 정리한다. 특히 절 3의 Sparse Set과 절 5의 Archetype은 현대 ECS의 두 축으로, 이 글의 무게 중심도 그쪽에 있다.

마지막에는 다섯 자료구조의 성능 특성을 한 표에 정리하고, 실제 게임 엔진이 이들을 어떻게 조합해서 쓰는지 본다.


---

## 목차
1. [Ring Buffer: 고정 크기 스트리밍의 정석](#1-ring-buffer-고정-크기-스트리밍의-정석)
2. [Object Pool: 할당 비용의 제거](#2-object-pool-할당-비용의-제거)
3. [Sparse Set: ECS의 핵심 자료구조](#3-sparse-set-ecs의-핵심-자료구조)
4. [Slot Map: 오래된 핸들 검출](#4-slot-map-오래된-핸들-검출)
5. [Archetype 기반 ECS: 극한의 성능](#5-archetype-기반-ecs-극한의-성능)
6. [종합 비교 및 선택 가이드](#6-종합-비교-및-선택-가이드)


---

## 1. Ring Buffer: 고정 크기 스트리밍의 정석

### 왜 Ring Buffer인가?

오디오 처리, 네트워크 패킷 버퍼링, 입력 이벤트 처리와 같은 스트리밍 작업에서는 FIFO(First-In-First-Out) 큐가 필요하다. `std::queue`를 사용할 수도 있지만, 실시간 시스템에서는 다음과 같은 문제가 발생한다:

1. **동적 할당의 예측 불가능성**: `std::queue`의 내부 컨테이너(`std::deque`)는 메모리를 동적으로 할당하므로, 할당 시간이 일정하지 않다.
2. **메모리 단편화**: 빈번한 할당과 해제로 인해 메모리 단편화가 발생할 수 있다.
3. **캐시 미스**: 데이터가 메모리 상에서 연속적이지 않을 수 있다.

Ring Buffer는 고정 크기의 배열을 원형으로 재사용하여 이러한 문제를 해결한다. Head와 Tail 포인터만 조작하면 되므로, 모든 연산이 O(1) 시간에 수행된다.

### 기본 구현

```cpp
template<typename T, size_t Size>
class RingBuffer {
    static_assert(Size > 0, "Size must be positive");
private:
    T buffer[Size];
    size_t head;  // 쓰기 위치
    size_t tail;  // 읽기 위치
    size_t count; // 현재 요소 개수

public:
    RingBuffer() : head(0), tail(0), count(0) {}

    bool push(const T& item) {
        if (count == Size) return false; // 버퍼 가득참

        buffer[head] = item;
        head = (head + 1) % Size;
        ++count;
        return true;
    }

    bool pop(T& item) {
        if (count == 0) return false; // 버퍼 비어있음

        item = buffer[tail];
        tail = (tail + 1) % Size;
        --count;
        return true;
    }

    bool isFull() const { return count == Size; }
    bool isEmpty() const { return count == 0; }
    size_t size() const { return count; }
};
```

이 구현은 간결하지만, count 변수로 인해 생산자와 소비자가 동일한 변수를 수정하게 되어 멀티스레드 환경에서는 동기화 오버헤드가 발생한다.

### Lock-Free 구현 (SPSC 전용)

다음 구현은 **단일 생산자 / 단일 소비자(SPSC)** 전용이다. 생산자가 한 명이라 head를 단독으로 갱신할 수 있고, 소비자도 마찬가지로 tail을 단독으로 갱신할 수 있다는 가정에서 락 없이 안전하다. 다중 생산자나 다중 소비자(MPMC) 환경에서는 head/tail 예약을 위한 별도의 CAS 루프가 필요하며, 그렇지 않으면 같은 슬롯에 두 producer가 동시에 쓰는 race가 발생한다. MPMC가 필요하면 moodycamel/concurrentqueue 같은 검증된 구현을 쓰거나 Vyukov MPMC 큐를 별도로 작성해야 한다. 또한 이 구현이 lock-free인 것은 `std::atomic<size_t>`가 lock-free이고 T의 복사·대입이 예외를 던지거나 블록되지 않는다는 전제에서다. `is_always_lock_free`로 확인하고 실시간 경로에는 nothrow·비차단 T를 쓴다.

```cpp
template<typename T, size_t Size>
class SPSCRingBuffer {
    static_assert(Size > 1, "SPSC ring keeps one slot empty");
private:
    T buffer[Size];
    std::atomic<size_t> head;  // producer만 store
    std::atomic<size_t> tail;  // consumer만 store

public:
    SPSCRingBuffer() : head(0), tail(0) {}

    bool push(const T& item) {  // 생산자 한 명만 호출 가능
        size_t current_head = head.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) % Size;

        if (next_head == tail.load(std::memory_order_acquire)) {
            return false; // 가득참
        }

        buffer[current_head] = item;
        head.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {  // 소비자 한 명만 호출 가능
        size_t current_tail = tail.load(std::memory_order_relaxed);

        if (current_tail == head.load(std::memory_order_acquire)) {
            return false; // 비어있음
        }

        item = buffer[current_tail];
        tail.store((current_tail + 1) % Size, std::memory_order_release);
        return true;
    }

    // 소비자가 호출: 지금 읽을 수 있는 최소 개수
    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_relaxed);
        return (h + Size - t) % Size;
    }
};
```

여기서 주목할 점은 메모리 순서(memory ordering) 지정이다:
- `memory_order_relaxed`: 같은 스레드 안의 단독 갱신이라 순서 보장이 필요 없는 자기 인덱스 읽기
- `memory_order_acquire`: 상대 인덱스를 읽어 슬롯의 가시성을 보장
- `memory_order_release`: 슬롯에 쓴 데이터를 상대 스레드가 acquire로 볼 수 있도록 게시

이 메모리 순서 제어가 SPSC에서 락 없이도 안전한 이유다. MPMC에서는 같은 head를 두 생산자가 동시에 갱신해 추가 동기화 없이는 안전하지 않다.

### 성능 최적화: 2의 거듭제곱 크기

버퍼 크기를 2의 거듭제곱으로 제한하면 wrap-around를 비트 마스크로 표현할 수 있다:

```cpp
template<typename T, size_t Size>
class OptimizedRingBuffer {
    static_assert(Size > 1 && (Size & (Size - 1)) == 0,
                  "Size must be a power of 2 greater than one");

private:
    T buffer[Size];
    size_t head = 0;
    size_t tail = 0;

    static constexpr size_t MASK = Size - 1;

public:
    bool push(const T& item) {
        size_t next_head = (head + 1) & MASK;  // % Size 대신 비트 마스크
        if (next_head == tail) return false;

        buffer[head] = item;
        head = next_head;
        return true;
    }

    bool pop(T& item) {
        if (tail == head) return false;

        item = buffer[tail];
        tail = (tail + 1) & MASK;
        return true;
    }
};
```

`OptimizedRingBuffer`는 원자 연산을 쓰지 않는 단일 스레드 버전이다. `Size`가 템플릿 상수이고 2의 거듭제곱임을 컴파일러가 알면 `% Size`도 같은 마스크로 최적화될 수 있다. 따라서 수동 치환의 고정 성능 이득을 기대하기보다 power-of-two라는 불변식을 명시하고 생성된 코드를 확인하는 데 의미가 있다.

### 실전 응용: 오디오 버퍼

실시간 오디오 처리는 Ring Buffer의 대표적인 응용 사례이다:

```cpp
constexpr size_t SAMPLE_RATE = 48000;
constexpr size_t BUFFER_SIZE = 4096;

class AudioBuffer {
    SPSCRingBuffer<float, BUFFER_SIZE> leftChannel;
    SPSCRingBuffer<float, BUFFER_SIZE> rightChannel;

public:
    // 오디오 콜백에서 호출 (생산자)
    void write(float left, float right) {
        leftChannel.push(left);
        rightChannel.push(right);
    }

    // 재생 스레드에서 호출 (소비자)
    bool read(float& left, float& right) {
        return leftChannel.pop(left) && rightChannel.pop(right);
    }

    // 언더런 체크
    bool hasEnoughData(size_t minSamples) const {
        return leftChannel.size() >= minSamples;
    }
};
```

위 예제는 좌우 채널을 각각의 큐로 보내므로 한쪽 push나 pop만 실패하면 두 채널의 시점이 어긋난다. 실제로는 `struct StereoFrame { float left, right; }` 하나를 단일 SPSC 큐로 전송하고, 가득 찼을 때 drop·block·overwrite 중 무엇을 할지와 비었을 때 silence·repeat 정책을 명시하는 편이 안전하다.

오디오 시스템에서는 버퍼 언더런이 발생하면 끊김이나 잡음이 생길 수 있다. 버퍼를 키우면 scheduler jitter에 강해지지만 입력부터 출력까지의 latency가 늘어난다. 필요한 frame 수와 callback block 수는 오디오 API, 장치, 장르의 반응성 요구와 최악 실행 시간을 측정해 정한다.

## 2. Object Pool: 할당 비용의 제거

### 메모리 할당의 숨겨진 비용

게임에서 파티클 시스템을 구현한다고 가정해보자. 폭발 이펙트 하나에 수천 개의 파티클이 생성되었다가 1-2초 내에 소멸한다. 순진한 구현은 다음과 같을 것이다:

```cpp
// 나쁜 예시
void emitParticle() {
    Particle* p = new Particle();  // 매번 힙 할당
    p->lifetime = 1.0f;
    activeParticles.push_back(p);
}

void updateParticles(float dt) {
    for (auto it = activeParticles.begin(); it != activeParticles.end();) {
        (*it)->age += dt;
        if ((*it)->age >= (*it)->lifetime) {
            delete *it;  // 매번 힙 해제
            it = activeParticles.erase(it);
        } else {
            ++it;
        }
    }
}
```

이 코드의 문제점:
1. **할당 비용**: `new`는 보통 사용자 공간 할당자를 거치며, 로컬 캐시 miss에서 공유 풀이나 OS 경로로 확장될 수 있다.
2. **해제 비용**: `delete`는 메모리를 반환하고 메타데이터를 업데이트해야 한다.
3. **메모리 단편화**: 작은 객체의 빈번한 할당/해제는 힙을 조각낸다.
4. **캐시 미스**: 파티클들이 메모리 상에서 흩어져 있어 순회 시 캐시 미스가 빈번하다.

Object Pool은 미리 객체를 할당해두고 재사용함으로써 이러한 문제를 해결한다.

### Object Pool 구현

```cpp
template<typename T, size_t PoolSize>
class ObjectPool {
private:
    // 실제 객체 저장소
    std::array<T, PoolSize> objects;

    // 사용 가능한 객체의 인덱스 스택
    std::array<size_t, PoolSize> freeList;
    size_t freeCount;

    // 사용 중인지 추적
    std::bitset<PoolSize> inUse;

public:
    ObjectPool() : freeCount(PoolSize) {
        // 모든 인덱스를 free list에 추가
        for (size_t i = 0; i < PoolSize; ++i) {
            freeList[i] = i;
        }
    }

    // 객체 할당
    T* acquire() {
        if (freeCount == 0) return nullptr;

        size_t index = freeList[--freeCount];
        inUse.set(index);

        // Placement new로 객체 재초기화
        return new (&objects[index]) T();
    }

    // 객체 반환
    void release(T* obj) {
        // 다른 배열의 포인터를 빼는 것 자체가 UB이므로 소속을 정수로 먼저 확인한다.
        const uintptr_t base = reinterpret_cast<uintptr_t>(objects.data());
        const uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
        // 범위와 슬롯 경계(정렬)를 함께 확인한다.
        if (addr < base || addr >= base + sizeof(T) * PoolSize ||
            (addr - base) % sizeof(T) != 0) {
            // 이 풀의 객체가 아니다
            return;
        }

        size_t index = (addr - base) / sizeof(T);
        if (!inUse[index]) {
            // 이중 해제
            return;
        }

        // 소멸자 명시적 호출
        obj->~T();

        inUse.reset(index);
        freeList[freeCount++] = index;
    }

    size_t available() const { return freeCount; }
    size_t capacity() const { return PoolSize; }
};
```

여기서 핵심은 **placement new**이다. 이미 할당된 메모리에 생성자만 호출하므로, 힙 할당 오버헤드가 발생하지 않는다.

한 가지 주의점이 있다. `std::array<T, N>`은 풀 생성 시점에 모든 요소를 이미 생성하므로, 첫 `acquire`에서 살아있는 객체 위에 placement new를 하는 셈이 된다. 이때 이전 객체의 자원은 소멸되지 않고, `release` 뒤 풀 소멸 시 같은 슬롯을 다시 소멸하는 이중 소멸이 될 수 있으며, `acquire`의 T 생성자가 예외를 던지면 `freeCount`와 `inUse`가 복구되지 않는다. 따라서 이 예제는 trivially destructible 타입에만 적용하고, 정석은 `alignas(T) std::byte storage[N * sizeof(T)]` 같은 원시 버퍼를 두고 생성/소멸을 풀이 전담하는 것이다.

### RAII 스타일 핸들

매번 수동으로 `release`를 호출하는 것은 실수하기 쉽다. RAII 패턴을 적용하여 자동으로 반환되도록 만들 수 있다:

```cpp
template<typename T, size_t PoolSize>
class PoolHandle {
    ObjectPool<T, PoolSize>* pool;
    T* object;

public:
    PoolHandle(ObjectPool<T, PoolSize>* p, T* obj) : pool(p), object(obj) {}

    ~PoolHandle() {
        if (object) pool->release(object);
    }

    // 이동 시맨틱
    PoolHandle(PoolHandle&& other) noexcept
        : pool(other.pool), object(other.object) {
        other.object = nullptr;
    }

    PoolHandle& operator=(PoolHandle&& other) noexcept {
        if (this != &other) {
            if (object) pool->release(object);
            pool = other.pool;
            object = other.object;
            other.object = nullptr;
        }
        return *this;
    }

    // 복사 금지
    PoolHandle(const PoolHandle&) = delete;
    PoolHandle& operator=(const PoolHandle&) = delete;

    T* operator->() { return object; }
    T& operator*() { return *object; }
    bool valid() const { return object != nullptr; }
};

// 사용 예시: Pool에는 acquireHandle()이 없으므로 획득한 포인터를 감싼다.
ObjectPool<Particle, 10000> pool;
PoolHandle<Particle, 10000> particleHandle(&pool, pool.acquire());
if (particleHandle.valid()) {
    particleHandle->position = {0, 0, 0};
    particleHandle->velocity = {1, 2, 3};
} // 스코프를 벗어나면 자동으로 풀에 반환
```

### 실전 파티클 시스템

```cpp
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 color;
    float lifetime;
    float age;
    float size;

    void reset() {
        position = {0, 0, 0};
        velocity = {0, 0, 0};
        color = {1, 1, 1, 1};
        lifetime = 1.0f;
        age = 0.0f;
        size = 1.0f;
    }
};

class ParticleSystem {
private:
    static constexpr size_t MAX_PARTICLES = 10000;
    ObjectPool<Particle, MAX_PARTICLES> pool;
    std::vector<Particle*> activeParticles;

public:
    void emit(const glm::vec3& position, const glm::vec3& velocity, float lifetime) {
        Particle* p = pool.acquire();
        if (!p) {
            // 풀이 가득참 - 활성 목록의 맨 앞 파티클을 재활용한다.
            // update()의 swap-pop이 순서를 흩뜨리므로 가장 오래된 파티클이라는 보장은 없다.
            if (!activeParticles.empty()) {
                p = activeParticles.front();
                activeParticles.erase(activeParticles.begin());
            } else {
                return; // 완전히 가득참
            }
        }

        p->reset();  // 재활용 슬롯을 포함해 모든 필드를 초기화한다.
        p->position = position;
        p->velocity = velocity;
        p->lifetime = lifetime;
        p->age = 0.0f;
        activeParticles.push_back(p);
    }

    void update(float deltaTime) {
        // 역순 순회 + swap-pop: 순서가 필요 없는 파티클 목록에 적합하다.
        for (size_t i = activeParticles.size(); i-- > 0;) {
            Particle* p = activeParticles[i];

            p->age += deltaTime;

            if (p->age >= p->lifetime) {
                // 파티클 수명 종료
                pool.release(p);
                activeParticles[i] = activeParticles.back();
                activeParticles.pop_back();
            } else {
                // 물리 업데이트
                p->velocity.y -= 9.8f * deltaTime; // 중력
                p->position += p->velocity * deltaTime;

                // 페이드 아웃
                float t = p->age / p->lifetime;
                p->color.a = 1.0f - t;
            }
        }
    }

    void render() {
        // 인스턴싱으로 한 번에 렌더링
        std::vector<glm::mat4> transforms;
        std::vector<glm::vec4> colors;

        transforms.reserve(activeParticles.size());
        colors.reserve(activeParticles.size());

        for (const Particle* p : activeParticles) {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), p->position);
            transform = glm::scale(transform, glm::vec3(p->size));
            transforms.push_back(transform);
            colors.push_back(p->color);
        }

        // GPU 인스턴스 렌더링
        renderInstanced(transforms, colors);
    }
};
```

이 구현은 pool backing storage를 준비한 뒤 개별 파티클의 일반 힙 할당을 피한다. 대신 고정 최대 개수, pool 초기화 비용, 빈 슬롯 관리와 초과 정책을 부담한다. 이득은 기존 할당자의 fast-path hit 비율과 파티클 수명 분포에 따라 측정해야 한다.

## 3. Sparse Set: ECS의 핵심 자료구조

### ECS란 무엇인가?

Entity Component System(ECS)은 전통적인 객체 지향 상속 구조의 대안으로 떠오른 아키텍처 패턴이다. 게임 오브젝트를 다음 세 가지 요소로 분리한다:

- **Entity**: 고유 ID만 가진 빈 컨테이너 (단순히 정수)
- **Component**: 데이터만 담은 구조체 (Position, Velocity, Health 등)
- **System**: 컴포넌트에 대한 로직 (MovementSystem, RenderSystem 등)

전통적인 객체 지향 방식과 비교해보자:

```cpp
// 전통적 OOP
class GameObject {
    glm::vec3 position;
    glm::vec3 velocity;
    int health;

    virtual void update(float dt) {
        position += velocity * dt;
    }
};

class Enemy : public GameObject {
    AI* aiController;

    void update(float dt) override {
        GameObject::update(dt);
        aiController->think();
    }
};
```

이 방식의 문제:
1. **다이아몬드 상속 문제**: 다중 상속 시 복잡도 증가
2. **캐시 미스**: 가상 함수 호출과 흩어진 데이터
3. **유연성 부족**: 런타임에 기능 추가/제거 어려움

ECS 방식:
```cpp
// Entity는 단순히 uint32_t
uint32_t entity = registry.create();

// 컴포넌트는 데이터만 담음
struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };
struct Health { int current, max; };

// 엔티티에 컴포넌트 부착
registry.add<Position>(entity, {0, 0, 0});
registry.add<Velocity>(entity, {1, 0, 0});
registry.add<Health>(entity, {100, 100});

// 시스템은 컴포넌트를 순회
void MovementSystem(Registry& reg, float dt) {
    reg.view<Position, Velocity>([dt](Position& pos, Velocity& vel) {
        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;
        pos.z += vel.dz * dt;
    });
}
```

### Sparse Set: 양쪽의 장점을 취하다

ECS에서 핵심 문제는 "어떤 엔티티가 어떤 컴포넌트를 가지고 있는가?"를 효율적으로 저장하고 조회하는 것이다. Sparse Set은 컴포넌트 종류별로 빠른 membership 검사와 조밀한 순회를 함께 얻는 대표적인 선택이다. 다만 sparse 배열 비용이 엔티티 ID 공간의 상한에 비례하고, 여러 컴포넌트 조합 질의는 가장 작은 집합을 순회하며 나머지를 검사해야 한다. 조합 단위 순회가 지배적이면 archetype chunk가 더 유리할 수 있다.

**핵심 아이디어**:
- **Sparse Array**: entity_id를 인덱스로 사용하여 dense 배열의 위치를 저장 (O(1) 조회)
- **Dense Array**: 실제로 컴포넌트를 가진 엔티티만 압축 저장 (O(n) 순회, n은 실제 엔티티 수)

```cpp
template<typename T>
class SparseSet {
private:
    // Sparse: entity_id → dense array의 인덱스
    std::vector<uint32_t> sparse;

    // Dense: 압축된 엔티티 ID 배열
    std::vector<uint32_t> dense;

    // Components: 실제 컴포넌트 데이터 (dense와 동일한 순서)
    std::vector<T> components;

    static constexpr uint32_t NULL_INDEX = UINT32_MAX;

public:
    // 엔티티가 컴포넌트를 가지는지 확인: O(1)
    bool has(uint32_t entity) const {
        if (entity >= sparse.size()) return false;

        uint32_t denseIdx = sparse[entity];
        return denseIdx < dense.size() && dense[denseIdx] == entity;
    }

    // 컴포넌트 가져오기: O(1)
    T* get(uint32_t entity) {
        if (!has(entity)) return nullptr;
        return &components[sparse[entity]];
    }

    // 컴포넌트 추가: O(1) 평균
    T& add(uint32_t entity, const T& component = T{}) {
        if (has(entity)) {
            // 이미 존재 - 덮어쓰기
            components[sparse[entity]] = component;
            return components[sparse[entity]];
        }

        // Sparse 배열 확장 (필요시)
        if (entity >= sparse.size()) {
            sparse.resize(entity + 1, NULL_INDEX);
        }

        // Dense 배열 끝에 추가
        uint32_t denseIdx = dense.size();
        sparse[entity] = denseIdx;
        dense.push_back(entity);
        components.push_back(component);

        return components.back();
    }

    // 컴포넌트 제거: O(1)
    void remove(uint32_t entity) {
        if (!has(entity)) return;

        uint32_t denseIdx = sparse[entity];
        uint32_t lastIdx = dense.size() - 1;

        if (denseIdx != lastIdx) {
            // Swap-and-pop: 마지막 요소와 교체
            uint32_t lastEntity = dense[lastIdx];

            dense[denseIdx] = lastEntity;
            components[denseIdx] = std::move(components[lastIdx]);
            sparse[lastEntity] = denseIdx;
        }

        dense.pop_back();
        components.pop_back();
        sparse[entity] = NULL_INDEX;
    }

    // 모든 컴포넌트 순회: 캐시 친화적
    void forEach(std::function<void(uint32_t entity, T& component)> callback) {
        for (size_t i = 0; i < dense.size(); ++i) {
            callback(dense[i], components[i]);
        }
    }

    size_t size() const { return dense.size(); }
};
```

이 구현은 `entity < NULL_INDEX`(즉 `UINT32_MAX` 미만)를 전제로 한다. `entity == UINT32_MAX`이면 `entity + 1`이 0으로 래핑해 sparse 배열을 잘못 확장한다. 또한 `components.push_back`이 예외를 던지면 dense와 components 길이가 어긋나므로 T는 nothrow 타입으로 제한하거나 예외 시 dense 갱신을 되돌려야 한다. `forEach` 안에서 add/remove 같은 구조 변경을 호출하면 안 된다.

### 왜 Sparse Set이 빠른가?

Sparse Set의 성능 비결은 **데이터 지역성**에 있다. 순회할 때는 dense 배열만 접근하므로:

1. **순차 메모리 접근**: CPU 프리페처가 다음 캐시 라인을 미리 가져온다.
2. **분기 예측**: iterator가 tombstone이나 bucket chain을 건너뛰는 분기를 줄일 수 있다.
3. **벡터화 가능**: 컴파일러가 SIMD 명령어로 최적화할 수 있다.

두 순회 형태의 핵심 차이:
```cpp
// std::unordered_map<uint32_t, Component> 사용 시
for (auto& [entity, comp] : hashMap) {
    comp.value += 1;  // 순회 자체는 재해시하지 않지만 node chain을 따라갈 수 있음
}

// SparseSet 사용 시
sparseSet.forEach([](uint32_t entity, Component& comp) {
    comp.value += 1;  // dense 배열을 순차 접근
});
```

`unordered_map`의 range-for가 각 원소마다 hash lookup을 수행하는 것은 아니다. 차이는 node-based 배치와 dense 배열의 locality, prefetch, vectorization 가능성에서 생긴다. 배율은 component 크기와 iteration body가 가벼울수록 크게 보이고, 본문 계산이 무거우면 작아진다.

### EnTT: 실전 ECS 라이브러리

EnTT는 sparse set 계열 storage와 view/group 질의를 제공하는 header-only C++ ECS 라이브러리다. 라이브러리 선택은 공개되지 않은 제품 채택 사례나 단일 순위보다 필요한 storage 안정성, 구조 변경 비용, scheduler 통합과 버전 API를 기준으로 판단한다.

```cpp
#include <entt/entt.hpp>

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };

int main() {
    entt::registry registry;

    // 엔티티 생성
    auto entity = registry.create();
    registry.emplace<Position>(entity, 0.0f, 0.0f, 0.0f);
    registry.emplace<Velocity>(entity, 1.0f, 0.0f, 0.0f);

    // 1000개 엔티티 생성
    for (int i = 0; i < 1000; ++i) {
        auto e = registry.create();
        registry.emplace<Position>(e, float(i), 0.0f, 0.0f);
        registry.emplace<Velocity>(e, 1.0f, 1.0f, 0.0f);
    }

    // 뷰를 사용한 순회
    auto view = registry.view<Position, Velocity>();
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& vel = view.get<Velocity>(entity);

        pos.x += vel.dx * 0.016f;
        pos.y += vel.dy * 0.016f;
    }

    // 또는 람다로 더 간결하게
    view.each([](auto& pos, auto& vel) {
        pos.x += vel.dx * 0.016f;
        pos.y += vel.dy * 0.016f;
    });

    return 0;
}
```

성능을 비교할 때는 entity 생성, component attach/detach, single-component 순회, multi-component join과 구조 변경을 분리한다. 같은 entity 수라도 component 조합과 view 구성 방식이 비용을 바꾸므로 "100만 개 몇 ms"를 라이브러리의 고정 속도로 사용하지 않는다.

## 4. Slot Map: 오래된 핸들 검출

### ID 재사용 문제

객체 풀이나 엔티티 시스템에서 숫자 ID를 재사용하면 오래된 참조가 새 객체를 가리킬 수 있다. 모양은 lock-free 알고리즘의 ABA와 비슷하지만 여기서 해결하려는 문제는 **애플리케이션 핸들의 세대 검증**이다. 원자적 CAS의 ABA와 안전한 메모리 회수는 별도의 동시성 문제다.

```cpp
EntityID enemy = spawnEnemy();      // ID = 5
Projectile* bullet = shootAt(enemy);  // 총알이 enemy(5)를 추적

destroyEnemy(enemy);                // ID 5 삭제
EntityID newEnemy = spawnEnemy();   // ID 5 재사용

// 문제: 총알이 새로운 적을 가리키게 됨!
bullet->update();  // 엉뚱한 적을 향해 날아감
```

이는 다음 상황에서 심각한 버그를 유발한다:
1. **지연된 참조**: 이벤트 큐에 저장된 오래된 ID
2. **비동기 처리**: 다른 스레드가 이미 삭제된 ID 참조
3. **네트워크 게임**: 클라이언트가 오래된 엔티티 ID 사용

### Generational Index: 세대로 구분하다

해결책은 각 슬롯에 "세대(generation)" 카운터를 추가하는 것이다:

```cpp
template<typename T>
class SlotMap {
private:
    struct Slot {
        uint32_t generation;  // 현재 세대
        uint32_t index;       // data 배열의 인덱스 (사용 중일 때)
        bool occupied;
    };

    std::vector<Slot> slots;
    std::vector<T> data;
    std::vector<uint32_t> indices;  // data → slot 역매핑
    std::vector<uint32_t> freeSlots;

public:
    struct Key {
        uint32_t index;
        uint32_t generation;

        bool operator==(const Key& other) const {
            return index == other.index && generation == other.generation;
        }
    };

    // 새 요소 추가
    Key insert(const T& value) {
        uint32_t slotIndex;

        if (!freeSlots.empty()) {
            // 빈 슬롯 재사용
            slotIndex = freeSlots.back();
            freeSlots.pop_back();
        } else {
            // 새 슬롯 생성
            slotIndex = slots.size();
            slots.push_back({0, 0, false});
        }

        Slot& slot = slots[slotIndex];
        slot.index = data.size();
        slot.occupied = true;

        data.push_back(value);
        indices.push_back(slotIndex);

        return {slotIndex, slot.generation};
    }

    // 요소 제거
    bool erase(Key key) {
        if (key.index >= slots.size()) return false;

        Slot& slot = slots[key.index];

        // 세대 확인 (오래된 핸들 거부)
        if (!slot.occupied || slot.generation != key.generation) {
            return false;
        }

        // Swap-and-pop
        uint32_t dataIndex = slot.index;
        uint32_t lastIndex = data.size() - 1;

        if (dataIndex != lastIndex) {
            data[dataIndex] = std::move(data[lastIndex]);
            indices[dataIndex] = indices[lastIndex];
            slots[indices[dataIndex]].index = dataIndex;
        }

        data.pop_back();
        indices.pop_back();

        // 슬롯 비우기 및 세대 증가
        slot.occupied = false;
        slot.generation++;  // 핵심: 세대 증가
        freeSlots.push_back(key.index);

        return true;
    }

    // 요소 접근
    T* get(Key key) {
        if (key.index >= slots.size()) return nullptr;

        const Slot& slot = slots[key.index];
        if (!slot.occupied || slot.generation != key.generation) {
            return nullptr;  // 오래된 키
        }

        return &data[slot.index];
    }

    // 유효성 검사
    bool isValid(Key key) const {
        return key.index < slots.size() &&
               slots[key.index].occupied &&
               slots[key.index].generation == key.generation;
    }

    // callback 안에서는 insert/erase처럼 dense 배열을 재배치하는 연산을 금지한다.
    template<class F>
    void forEach(F&& callback) {
        for (size_t dataIndex = 0; dataIndex < data.size(); ++dataIndex) {
            const uint32_t slotIndex = indices[dataIndex];
            const Slot& slot = slots[slotIndex];
            callback(Key{slotIndex, slot.generation}, data[dataIndex]);
        }
    }
};
```

`insert`는 `slot.occupied = true` 이후 data/indices push가 실패하면 슬롯과 배열이 어긋나므로, T를 nothrow 이동 가능 타입으로 제한하거나 예외 시 되돌리는 단계화가 필요하다. `Key`의 기본값(index=0, generation=0)은 슬롯 0의 최초 키와 충돌하므로 무효 키가 필요하면 별도 sentinel을 둔다. 모든 인덱스는 `uint32_t` 범위 안에 있어야 한다.

### 실전 활용 예시

```cpp
struct Enemy {
    float health;
    glm::vec3 position;
    SlotMap<Enemy>::Key target;  // 다른 적을 타겟팅할 수도 있음
};

class GameWorld {
    SlotMap<Enemy> enemies;

public:
    SlotMap<Enemy>::Key spawnEnemy(const glm::vec3& pos) {
        Enemy enemy;
        enemy.health = 100.0f;
        enemy.position = pos;
        return enemies.insert(enemy);
    }

    void damageEnemy(SlotMap<Enemy>::Key key, float damage) {
        Enemy* enemy = enemies.get(key);
        if (!enemy) {
            // 이미 죽었거나 유효하지 않은 키
            return;
        }

        enemy->health -= damage;
        if (enemy->health <= 0) {
            enemies.erase(key);
            // 이제 이 키는 무효화됨
        }
    }

    void update(float dt) {
        std::vector<std::pair<SlotMap<Enemy>::Key, float>> pendingDamage;

        enemies.forEach([this, dt, &pendingDamage](auto key, Enemy& enemy) {
            // AI 업데이트
            enemy.position += glm::vec3(1, 0, 0) * dt;

            // 다른 적을 공격
            if (shouldAttackOther(enemy)) {
                auto otherKey = findNearestEnemy(enemy.position);
                pendingDamage.emplace_back(otherKey, 10.0f);
            }
        });

        // 순회가 끝난 뒤 구조 변경을 적용한다. 앞선 공격으로 대상이
        // 삭제되었으면 generation 검사가 뒤의 명령을 안전하게 거부한다.
        for (auto [key, damage] : pendingDamage) {
            damageEnemy(key, damage);
        }
    }
};
```

이제 적이 죽고 같은 슬롯에 새 적이 생성되더라도 세대가 다른 오래된 키는 거부된다. 단, 세대 카운터가 래핑해 과거 값으로 돌아오면 같은 키가 다시 나타날 수 있다. 비트 폭은 슬롯의 예상 재사용 횟수와 프로그램 수명에 맞춰 정하고, 래핑을 실질적으로 불가능하게 만들거나 해당 슬롯을 영구 폐기하는 정책이 필요하다. 또한 이 컨테이너 자체가 자동으로 thread-safe가 되는 것은 아니다.

### 메모리 최적화: Compact Key

32비트 인덱스 + 32비트 세대 = 64비트는 포인터만큼 크다. 대부분의 경우 이보다 작은 키면 충분하다:

```cpp
struct CompactKey {
    uint32_t index : 20;      // 100만 엔티티
        uint32_t generation : 12; // 4096번 뒤 래핑하므로 수명 조건을 검토해야 함
};
```

또는:
```cpp
struct BalancedKey {
    uint16_t index;      // 65536 엔티티
    uint16_t generation; // 65536번 재사용
};
```

프로젝트의 요구사항에 따라 비트 수를 조절하여 메모리를 절약할 수 있다.

## 5. Archetype 기반 ECS: 극한의 성능

### Sparse Set의 한계

Sparse Set 기반 ECS는 충분히 빠르지만, 대규모 게임(엔티티 수십만 개)에서는 여전히 개선의 여지가 있다:

1. **간접 참조**: Sparse 배열을 거쳐 dense 배열에 접근
2. **분산된 메모리**: 각 컴포넌트 타입이 별도 배열에 저장
3. **SIMD 활용 부족**: 여러 컴포넌트를 함께 순회할 때 sparse 배열을 거친 교차 조회가 끼어들어 인덱스가 정렬되지 않는다

Unity DOTS, Flecs 등 현대 ECS 엔진은 **Archetype** 기반 아키텍처를 사용하여 이를 해결한다.

### Archetype이란?

**Archetype**: 동일한 컴포넌트 조합을 가진 엔티티의 집합

예를 들어:
- Archetype A: [Position, Velocity]
- Archetype B: [Position, Velocity, Health]
- Archetype C: [Position, Renderable]

각 Archetype은 컴포넌트를 **Structure of Arrays (SoA)** 레이아웃으로 저장한다.

### 청크가 결정하는 것

Archetype 청크는 컴포넌트마다 별도 배열을 두고 같은 인덱스로 한 엔티티를 표현한다. 컴포넌트별로 배열이 갈라져 있으므로 시스템은 자기가 읽는 컴포넌트의 배열만 순회하고, 만지지 않는 컴포넌트는 전송되지 않는다. 이 배치 자체는 Archetype의 발명이 아니다. Archetype이 정하는 것은 그 배열들을 **어떤 단위로 묶을 것인가**다. 그 단위가 청크이고, 청크 크기가 세 가지 결정을 한꺼번에 규정한다.

**첫째, 청크 용량은 컴포넌트 크기의 합에서 역산한다.** `Position`(12바이트) + `Velocity`(12바이트) + `Health`(8바이트) 조합이라면 엔티티당 32바이트다. 청크를 16KB로 잡으면 다음과 같다.

$$\text{capacity} = \left\lfloor \frac{16{,}384}{32} \right\rfloor = 512$$

512개 엔티티가 한 청크에 들어간다. 이 512는 엔티티 ID·청크 헤더·컴포넌트별 정렬 패딩을 제외한 payload 기준 상한이다. 청크 크기를 고정하고 용량을 역산하는 이유는, 컴포넌트 조합이 무엇이든 **청크 하나를 순회하는 동안 만지는 총 바이트가 일정하게 유지**되기 때문이다. 조합이 무거우면 엔티티 수가 줄고, 가벼우면 늘어난다. 순회 단위를 캐시 용량에 맞춰 고정하려는 설계다.

**둘째, 컴포넌트 추가·삭제는 archetype 이동이 된다.** 엔티티의 컴포넌트 조합이 바뀌면 그 엔티티는 다른 archetype에 속하게 되므로, 가지고 있던 모든 컴포넌트를 새 청크로 복사하고 원래 자리는 마지막 엔티티로 메워야 한다.

```cpp
// 구조적 변경 한 번의 실제 작업량
// 1. 새 archetype의 청크에서 슬롯 확보
// 2. 기존 컴포넌트 전부 복사 (여기서는 32바이트)
// 3. 새 컴포넌트 초기화
// 4. 원래 청크에서 swap-remove (마지막 엔티티를 빈 자리로)
// 5. 이동한 두 엔티티의 인덱스 갱신
```

Sparse set은 배열 하나만 건드리므로 이 비용이 없다. 대신 순회할 때 컴포넌트마다 다른 dense 배열을 따라가야 한다. **비용을 순회에서 낼 것인가 구조적 변경에서 낼 것인가**가 두 구조의 실제 갈림길이다.

| 기준 | Sparse Set | Archetype 청크 |
|---|---|---|
| 컴포넌트 하나 순회 | dense 배열 순차 접근 | 청크 안 배열 순차 접근 |
| 여러 컴포넌트 동시 순회 | 컴포넌트마다 sparse 조회로 교차 | 같은 인덱스로 바로 접근 |
| 컴포넌트 추가·삭제 | 해당 배열에 push/swap-remove | 조합 전체 복사 후 archetype 이동 |
| 조합이 많을 때 | 영향 없음 | archetype 수가 늘어남 |

**셋째, 조합이 많아지면 청크 점유율이 떨어진다.** 컴포넌트 타입이 $k$개면 가능한 조합은 최대 $2^k$개다. 실제로 등장하는 조합만 archetype이 되지만, 조합이 다양한 게임에서는 각 archetype에 엔티티가 몇 개씩만 들어간 청크가 대량으로 생긴다. 512개 용량의 청크에 3개만 들어 있으면 순회는 청크 헤더를 읽고 3개를 처리한 뒤 다음 청크로 점프한다. 연속 순회의 이점이 사라지고, 청크 경계 자체가 오버헤드가 된다.

그래서 archetype 방식의 성능 조건은 "SoA를 쓴다"가 아니라 **"엔티티가 소수의 무거운 archetype에 몰려 있고 구조적 변경이 드물다"** 이다. 이 조건이 깨지면 같은 레이아웃에서도 sparse set보다 느려진다. 판정 지표는 archetype 개수, 청크당 평균 점유율, 프레임당 구조적 변경 횟수 세 가지다.

### Archetype ECS 구현 (간소화 버전)

```cpp
class ArchetypeECS {
private:
    using ArchetypeID = size_t;

    struct ArchetypeStorage {
        struct IComponentArray {
            virtual ~IComponentArray() = default;
        };

        template<typename T>
        struct ComponentArray final : IComponentArray {
            std::vector<T> values;
        };

        std::vector<uint32_t> entities;
        std::unordered_map<std::type_index,
                           std::unique_ptr<IComponentArray>> componentArrays;

        template<typename T>
        std::vector<T>& getArray() {
            auto typeIdx = std::type_index(typeid(T));
            auto it = componentArrays.find(typeIdx);
            if (it == componentArrays.end()) {
                auto owned = std::make_unique<ComponentArray<T>>();
                it = componentArrays.emplace(typeIdx, std::move(owned)).first;
            }
            return static_cast<ComponentArray<T>&>(*it->second).values;
        }
    };

    struct TypeSetHash {
        size_t operator()(const std::vector<std::type_index>& types) const {
            size_t hash = 0;
            for (const auto& type : types) {
                hash ^= type.hash_code() + 0x9e3779b9 +
                        (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    std::unordered_map<ArchetypeID, ArchetypeStorage> archetypes;
    std::unordered_map<std::vector<std::type_index>, ArchetypeID, TypeSetHash>
        archetypeIDs;
    std::unordered_map<uint32_t, ArchetypeID> entityToArchetype;
    uint32_t nextEntity = 0;
    ArchetypeID nextArchetype = 0;

    // 컴포넌트 타입 조합으로 Archetype ID 계산
    template<typename... Components>
    ArchetypeID getArchetypeID() {
        std::vector<std::type_index> types = {std::type_index(typeid(Components))...};
        std::sort(types.begin(), types.end());

        auto [it, inserted] = archetypeIDs.emplace(types, nextArchetype);
        if (inserted) {
            ++nextArchetype;
        }
        return it->second;  // hash 충돌은 vector equality로 구분된다.
    }

public:
    // 엔티티 생성 (컴포넌트와 함께)
    template<typename... Components>
    uint32_t create(Components&&... components) {
        uint32_t entity = nextEntity++;
        ArchetypeID archetypeID =
            getArchetypeID<std::remove_cvref_t<Components>...>();

        auto& archetype = archetypes[archetypeID];

        archetype.entities.push_back(entity);
        (archetype.getArray<std::remove_cvref_t<Components>>()
             .push_back(std::forward<Components>(components)), ...);

        entityToArchetype[entity] = archetypeID;

        return entity;
    }

    // 쿼리: 특정 컴포넌트를 가진 모든 엔티티 순회
    template<typename... Components, typename Fn>
    void query(Fn&& callback) {
        for (auto& [archetypeID, storage] : archetypes) {
            if (!hasAllComponents<Components...>(storage)) continue;

            // 순회 (컴포넌트 배열은 루프 밖에서 한 번만 찾는다)
            auto run = [&](auto*... arrays) {
                size_t count = storage.entities.size();
                for (size_t i = 0; i < count; ++i) {
                    callback((*arrays)[i]...);
                }
            };
            run(&storage.getArray<Components>()...);
        }
    }

private:
    template<typename... Components>
    bool hasAllComponents(const ArchetypeStorage& storage) {
        return ((storage.componentArrays.find(std::type_index(typeid(Components))) !=
                 storage.componentArrays.end()) && ...);
    }
};
```

`create`는 같은 컴포넌트 타입이 두 번 들어오면 같은 column에 두 번 push되어 `entities` 길이와 어긋나므로, 컴포넌트 타입이 고유한지 compile-time으로 검사해야 한다. 컴포넌트 배열 생성·삽입 중 예외가 나면 일부 column만 길어지므로 모든 column 길이를 entity 수와 맞추는 transaction으로 처리한다. `query` 콜백 안에서 구조 변경은 즉시 하지 말고 command buffer로 미룬다. 이 구현은 컴포넌트별 `std::vector`로 archetype grouping만 보여 줄 뿐, 고정 16KB chunk allocator와 청크 헤더·오프셋은 구현하지 않는다.

### Unity DOTS의 실제 구현

Unity DOTS는 Archetype ECS의 대표적인 산업 구현이다:

```csharp
// 컴포넌트 정의
public struct Position : IComponentData {
    public float3 Value;
}

public struct Velocity : IComponentData {
    public float3 Value;
}

// 시스템 정의
[BurstCompile]  // LLVM 기반 최적화
public partial struct MovementSystem : IJobEntity {
    public float DeltaTime;

    // Position과 Velocity를 모두 가진 archetype을 자동으로 쿼리 (Health 등이 더 있어도 매치된다)
    void Execute(ref Position position, in Velocity velocity) {
        position.Value += velocity.Value * DeltaTime;
    }
}

// 시스템 실행
public partial class MovementSystemGroup : SystemBase {
    protected override void OnUpdate() {
        new MovementSystem {
            DeltaTime = Time.DeltaTime
        }.ScheduleParallel();  // 멀티스레드 병렬 실행!
    }
}
```

Unity DOTS의 핵심 기능:
1. **Burst Compiler**: C# 코드를 LLVM IR로 컴파일하여 네이티브 성능
2. **Job System**: CPU 코어를 활용한 자동 병렬화
3. **Chunk 기반 메모리**: 같은 archetype의 component를 chunk에 모아 반복 처리와 scheduling의 단위를 만든다. 구체적인 chunk 용량과 헤더 배치는 Entities 패키지 버전의 구현 세부사항이다.

DOTS가 GameObject 기반 코드보다 얼마나 빠른지는 component 구성, structural change 빈도, chunk occupancy, Burst가 생성한 코드와 job scheduling 비용에 따라 달라진다. 비교할 때는 같은 동작과 같은 결과를 구현하고 main-thread time, worker utilization, cache miss, frame-time 분포를 함께 측정해야 한다. 홍보 사례의 최대 배수는 일반적인 보장이 아니다.

### 청크 경계가 만드는 tail

청크 안의 컴포넌트 배열은 연속이므로 시스템 루프는 배열 하나를 처음부터 끝까지 훑는 형태가 된다. 여기서 archetype 저장이 추가로 만드는 조건이 하나 있다. **순회 길이가 전체 엔티티 수가 아니라 청크마다의 점유 수**라는 점이다.

```cpp
// 순회 단위는 청크다. 길이는 청크마다 다르다.
for (Chunk& chunk : archetype.chunks) {
    Position* p = chunk.array<Position>();
    Velocity* v = chunk.array<Velocity>();
    for (size_t i = 0; i < chunk.count; ++i) {   // count는 capacity 이하
        p[i].x += v[i].x * dt;
    }
}
```

전체 엔티티가 10,000개고 청크 용량이 512라면 청크는 20개다. 루프를 폭 $w$의 벡터 명령으로 처리한다면 tail은 전체에서 한 번이 아니라 **점유 수가 $w$의 배수가 아닌 청크마다 한 번씩** 생긴다. 구조적 변경이 반복되어 청크들이 부분적으로 찬 상태라면 청크당 최대 $w-1$개씩 남아, 20개 청크에 $w=8$이면 130개가 넘는 원소가 스칼라 경로로 밀려날 수 있다. 전체를 하나의 배열로 두었을 때 최대 7개인 것과 비교하면 스무 배 규모다.

여기서 나오는 설계 결정이 **청크 용량을 벡터 폭의 배수로 잡는 것**이다. 512는 8과 16 모두의 배수이므로 꽉 찬 청크에서는 tail이 없고, 부분적으로 찬 마지막 청크에서만 남는다. 용량을 500 같은 값으로 잡으면 꽉 찬 청크에서도 매번 tail이 생긴다. 컴포넌트 크기의 합에서 용량을 역산할 때 그 값을 내림하는 대신 벡터 폭의 배수로 내림하는 이유다.

같은 이유로 청크의 각 컴포넌트 배열은 청크 헤더 뒤에서 정렬 경계에 맞춰 시작하도록 오프셋을 계산한다. 배열 시작이 정렬되어 있지 않으면 청크마다 앞쪽에서도 정렬 맞추기 구간이 생겨 tail이 양쪽에 붙는다.

## 6. 종합 비교 및 선택 가이드

### 성능 특성 비교

| 자료구조 | 삽입 | 삭제 | 조회 | 순회 | 캐시 친화성 | 메모리 오버헤드 |
|---------|------|------|------|------|------------|----------------|
| Ring Buffer | O(1) | O(1) | O(1) | O(n) | 매우 높음 | 없음 |
| Object Pool | O(1) | O(1) | O(1) | O(n) | 높음 | 낮음 (비트셋) |
| Sparse Set | O(1) | O(1) | O(1) | O(n) | 높음 | 중간 (sparse 배열) |
| Slot Map | O(1) | O(1) | O(1) | O(n) | 높음 | 중간 (세대 카운터) |
| Archetype ECS | O(1)* | O(n)** | O(1) | O(n) | 매우 높음 | 높음 (청크 메타데이터) |

\* 동일 Archetype 내에서
\*\* Archetype 변경 시 이동하는 컴포넌트 바이트 수 n에 비례

표의 삽입·삭제 차수는 위 표기대로이며, 아래의 10만 같은 엔티티 수 경계는 보편값이 아니라 조합 수·질의/변경 비율·ID 공간에 따라 달라지는 출발점이다.

### 언제 무엇을 사용할 것인가?

#### Ring Buffer
- **최적**: 스트리밍 데이터, 오디오/비디오 버퍼, 네트워크 패킷, 입력 큐
- **피할 것**: 랜덤 액세스 필요, 동적 크기 필요

#### Object Pool
- **최적**: 수명이 짧은 객체 (파티클, 총알), 빈번한 생성/소멸
- **피할 것**: 객체 크기 차이가 큼, 생성 비용이 낮음

#### Sparse Set
- **최적**: 단순한 ECS, 프로토타이핑, 중소규모 엔티티 (<10만)
- **피할 것**: 극한의 성능 필요, 수백만 엔티티

#### Slot Map
- **최적**: 장시간 유지되는 참조, 네트워크 게임, 비동기 시스템
- **피할 것**: 메모리 극도로 제한적, 간접 참조 비용 민감

#### Archetype ECS
- **최적**: 대규모 엔티티 (10만+), SIMD 최적화, 멀티스레딩, AAA 게임
- **피할 것**: 컴포넌트 자주 변경, 단순한 게임, 복잡도 회피

### 실전 조합 전략

대부분의 프로젝트는 여러 자료구조를 조합하여 사용한다:

```cpp
class GameEngine {
    // 엔티티 관리
    ArchetypeECS entitySystem;  // 대규모 엔티티

    // 파티클 시스템
    ObjectPool<Particle, 100000> particlePool;

    // 네트워크
    RingBuffer<Packet, 1024> recvBuffer;
    RingBuffer<Packet, 1024> sendBuffer;

    // 안전한 핸들
    SlotMap<NetworkPlayer> players;
    SlotMap<GameObject> gameObjects;

public:
    void update(float dt) {
        // 네트워크 패킷 처리
        Packet packet;
        while (recvBuffer.pop(packet)) {
            handlePacket(packet);
        }

        // 엔티티 시스템 업데이트
        entitySystem.query<Position, Velocity>([dt](auto& pos, auto& vel) {
            pos += vel * dt;
        });

        // 파티클 업데이트
        updateParticles(dt);
    }
};
```

### 성능 프로파일링 필수

"조기 최적화는 만악의 근원"이라는 말을 기억하라. 항상 프로파일링을 통해 실제 병목을 확인한 후 최적화하라:

```cpp
#include <chrono>

void benchmark() {
    constexpr size_t ITERATIONS = 1000000;

    // Sparse Set
    {
        SparseSet<Position> sparseSet;

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            sparseSet.add(i, Position{0, 0, 0});
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Sparse Set insert: " << duration.count() << " us\n";
    }

    // Archetype ECS
    {
        ArchetypeECS ecs;

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            ecs.create<Position>(Position{0, 0, 0});
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Archetype ECS insert: " << duration.count() << " us\n";
    }
}
```


---

## 결론

다섯 자료구조의 공통점은 단순하다. 동적 할당의 비결정성을 걷어내고, 메모리 레이아웃을 데이터 흐름에 맞게 정렬해 캐시 라인의 활용을 끌어올린다. Ring Buffer는 고정 메모리 위에서 FIFO를 만들고, Object Pool은 placement new로 할당을 줄이며, Sparse Set은 sparse-dense 이중 배열로 $O(1)$과 캐시 친화적 순회를 동시에 챙긴다. Slot Map은 generational index로 오래된 애플리케이션 핸들을 검출하고, Archetype은 SoA 레이아웃 위에서 연속 순회와 벡터화에 유리한 조건을 만든다.

현대 게임 엔진의 흐름은 Archetype 기반 ECS 쪽이다. Unity DOTS, Unreal의 Mass Entity, Bevy 모두 같은 방향으로 가고 있다. 다만 모든 프로젝트가 그 정도의 복잡도를 요구하는 것은 아니다. 엔티티가 수만 단위라면 Sparse Set만으로 충분하고, 객체 생성이 빈번한 시스템이라면 Object Pool만으로도 큰 차이를 만든다. 프로젝트의 규모와 데이터 패턴을 먼저 측정한 뒤 그에 맞는 추상의 단계를 고르는 것이 순서다.

이 컨테이너들의 복잡도와 캐시 이점은 단일 스레드 모델만으로는 충분하지 않다. 실제 병렬 실행에서는 컴포넌트 접근 집합, 구조 변경 시점, 작업 간 의존성을 별도 불변식으로 두어야 하며, 컨테이너의 $O(1)$ 연산만으로 동시성 안전성과 확장성이 따라오지는 않는다.


## 더 읽을거리

- [EnTT](https://github.com/skypjack/entt) — Sparse Set 기반 헤더 온리 ECS, Minecraft Bedrock에서 사용
- [Unity DOTS](https://docs.unity3d.com/Packages/com.unity.entities@1.3/manual/index.html) — Archetype 기반, Burst/Job System
- [Flecs](https://github.com/SanderMertens/flecs) — Archetype 기반 관계형 ECS
- Robert Nystrom, [*Game Programming Patterns*](https://gameprogrammingpatterns.com/object-pool.html)
- Richard Fabian, [*Data-Oriented Design*](https://www.dataorienteddesign.com/dodbook/)
- Jason Gregory, *Game Engine Architecture*
- Mike Acton, "Data-Oriented Design and C++" (CppCon 2014) — <https://www.youtube.com/watch?v=rX0ItVEVjHc>
- "Overwatch Gameplay Architecture and Netcode" (GDC 2017)
- Ulrich Drepper, "What Every Programmer Should Know About Memory"
- ECS 벤치마크 모음 — <https://github.com/abeimler/ecs_benchmark>
