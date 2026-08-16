## 들어가며

게임 엔진이나 물리 시뮬레이션을 개발하다 보면 흥미로운 역설을 마주한다. 객체지향 프로그래밍(OOP)의 모범 사례를 따라 깔끔하게 설계한 코드가, 정작 실행 속도는 형편없이 느린 경우가 많다. 10,000개의 엔티티를 업데이트하는데 왜 이렇게 오래 걸릴까?

답은 하드웨어에 있다. 현대 CPU는 메모리 접근 패턴에 매우 민감하다. L1 hit와 DRAM까지 가는 cache miss는 서로 다른 latency·대역폭 경로를 가지지만 그 비율은 CPU, 병렬 miss 수와 prefetch 성공 여부에 따라 달라진다. 아름다운 추상화 뒤에 숨겨진 포인터 체이싱과 가상 함수 호출은 종속 cache miss와 불규칙한 간접 분기를 만들어 CPU가 지연을 겹치기 어렵게 할 수 있다.

Data-Oriented Design(DOD)은 이 문제에 대한 해답이다. 추상화와 개념 모델링 대신, 실제 데이터의 변환과 하드웨어 특성에 집중하는 프로그래밍 패러다임이다. 이 글은 DOD의 철학부터 구체적인 구현 기법, 실전 적용 사례까지 다룬다.


---

## 목차
1. [OOP vs DOD: 철학의 충돌](#1-oop-vs-dod-철학의-충돌)
2. [AoS vs SoA: 메모리 레이아웃의 과학](#2-aos-vs-soa-메모리-레이아웃의-과학)
3. [DOD 관점의 캐시 최적화](#3-dod-관점의-캐시-최적화)
4. [실전 적용: ECS 설계 철학](#4-실전-적용-ecs-설계-철학)
5. [실전 적용: 파티클 시스템](#5-실전-적용-파티클-시스템)
6. [실전 적용: 물리 시뮬레이션](#6-실전-적용-물리-시뮬레이션)
7. [DOD 설계 원칙](#7-dod-설계-원칙)
8. [DOD의 한계와 트레이드오프](#8-dod의-한계와-트레이드오프)
9. [측정과 검증](#9-측정과-검증)


---

## 1. OOP vs DOD: 철학의 충돌

### 객체지향의 아름다운 거짓말

2014년 CppCon에서 Insomniac Games의 Engine Director인 Mike Acton은 "Data-Oriented Design and C++"라는 강연을 통해 OOP의 세 가지 큰 거짓말을 지적했다.

**거짓말 1: "Software is the platform"**

많은 개발자가 소프트웨어 추상화가 하드웨어의 세부사항을 감춰줄 것이라 믿는다. 하지만 진실은 하드웨어가 플랫폼이라는 것이다. CPU의 캐시 계층구조, 메모리 대역폭, 파이프라인 특성을 무시하면 성능을 잃는다.

**거짓말 2: "Code should be designed around a model of the world"**

OOP는 현실 세계의 개념을 객체로 모델링하라고 가르친다. "자동차"는 Car 클래스가 되고, "엔진"은 Engine 클래스가 된다. 그러나 컴퓨터는 세계의 모델을 실행하지 않는다. 컴퓨터는 데이터를 변환한다. A 상태의 바이트 배열을 B 상태의 바이트 배열로 바꾸는 것이 전부다.

**거짓말 3: "Code is more important than data"**

코드는 예술이지만 데이터는 그저 다루어야 할 대상이라고? 그렇지 않다. 데이터가 모든 것이다. 코드는 데이터를 변환하는 수단일 뿐이다.

### 전통적 OOP의 성능 문제

구체적인 예시를 보자.

```cpp
// OOP 방식: 개념적으로는 아름답지만...
class Entity {
    Vector3 position;
    Vector3 velocity;
    Mesh* mesh;          // 포인터 체이싱
    Material* material;  // 또 다른 포인터 체이싱
    std::string name;    // 힙 할당

    virtual void update(float dt) {  // 가상 함수 오버헤드
        position += velocity * dt;
    }
};

std::vector<Entity*> entities = LoadActiveEntities();

// 메인 루프
for (auto* e : entities) {
    e->update(dt);  // pointee 배치와 타입 분포에 따라 locality가 달라짐
}
```

이 코드의 문제점을 메모리 관점에서 분석해보자.

1. **분산된 메모리**: 각 Entity 객체가 힙의 다른 위치에 있다
2. **캐시 라인 낭비**: Entity 크기가 크지만 update에서는 position과 velocity만 필요하다
3. **포인터 체이싱**: mesh와 material을 역참조하면 또 다른 캐시 미스
4. **가상 함수**: vtable 조회로 인한 간접 참조와 분기 예측 실패

이 구조의 비용은 엔티티 수만으로 정해지지 않는다. 객체 크기, 실제 pointee 배치, update가 읽는 필드, 가상 호출 대상의 분포와 작업량을 고정한 뒤 cache/TLB miss와 branch miss를 함께 측정해야 한다.

### DOD의 접근: 데이터가 먼저다

같은 문제를 DOD로 해결해보자.

```cpp
// DOD 방식: 데이터 변환에 집중
struct TransformSystem {
    std::vector<Vector3> positions;   // 연속된 메모리
    std::vector<Vector3> velocities;  // 연속된 메모리

    void update(float dt, size_t count) {
        // 캐시 친화적, SIMD 최적화 가능
        for (size_t i = 0; i < count; ++i) {
            positions[i] += velocities[i] * dt;
        }
    }
};
```

이 레이아웃은 해당 루프가 position과 velocity만 읽는다는 가정에서 두 스트림을 연속 배치한다. 가상 호출과 per-object 포인터 추적도 제거되어 자동 벡터화가 쉬워진다. 하지만 `Vector3`의 padding, 다른 시스템의 접근 패턴, 배열 크기와 메모리 대역폭에 따라 cache-line 활용률과 개선 폭은 달라진다.

### Mike Acton의 핵심 원칙

DOD를 실천하기 위한 구체적인 질문들이다.

```text
1. What is the actual data?
   실제 데이터가 무엇인가?

2. How much data is there?
   데이터가 얼마나 많은가?

3. What is the most common case?
   가장 일반적인 경우는?

4. What are we actually doing with the data?
   데이터로 실제로 무엇을 하는가?

5. How is the data changing over time?
   시간에 따라 데이터가 어떻게 변하는가?
```

이 질문에 답하면 자연스럽게 효율적인 데이터 레이아웃이 도출된다.

## 2. AoS vs SoA: 메모리 레이아웃의 과학

### 두 가지 배열 구성 방식

데이터를 배열에 저장하는 방법은 크게 두 가지다.

**Array of Structures (AoS)**: 구조체의 배열

```cpp
struct Particle {
    float x, y, z;     // position
    float vx, vy, vz;  // velocity
};

std::vector<Particle> particles(10000);
```

메모리 레이아웃:

```text
[x0,y0,z0,vx0,vy0,vz0][x1,y1,z1,vx1,vy1,vz1][x2,y2,z2,vx2,vy2,vz2]...
```

**Structure of Arrays (SoA)**: 배열의 구조체

```cpp
struct ParticleSystem {
    std::vector<float> x, y, z;      // positions
    std::vector<float> vx, vy, vz;   // velocities
};
```

메모리 레이아웃:

```text
X:  [x0,x1,x2,x3,x4,x5,x6,x7,...]
Y:  [y0,y1,y2,y3,y4,y5,y6,y7,...]
Z:  [z0,z1,z2,z3,z4,z5,z6,z7,...]
```

### 캐시 라인 활용도

CPU는 메모리를 캐시 라인 단위(64 bytes)로 읽는다. x 좌표만 필요한 바운딩 볼륨 검사를 생각해보자.

```cpp
// AoS: x만 필요한데 y, z, v도 같이 로드됨
for (const auto& p : particles) {
    if (p.x > boundary) {
        // ...
    }
}
```

캐시 라인 분석 (float = 4 bytes):

```text
Cache Line (64 bytes = float 16개):
[x0,y0,z0,vx0,vy0,vz0,x1,y1,z1,vx1,vy1,vz1,x2,y2,z2,vx2]
 ^                     ^                    ^
필요    낭비된 데이터    필요                 필요

활용률: 파티클당 24바이트 중 x 4바이트만 사용 = 1/6 ≈ 17%
```

캐시 라인 하나에서 float 16개를 로드하지만 실제 사용하는 x값은 2~3개뿐이다. 메모리 대역폭의 약 83%를 낭비한다.

반면 SoA는:

```cpp
for (size_t i = 0; i < particles.x.size(); ++i) {
    if (particles.x[i] > boundary) {
        // ...
    }
}
```

캐시 라인 분석:

```text
Cache Line (64 bytes):
[x0,x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,x11,x12,x13,x14,x15]
         모두 필요한 데이터

활용률: 16/16 = 100%
```

16개의 x값을 한 번에 로드하고 모두 사용한다. 낭비가 없다.

### 정량적 성능 비교

10,000개 파티클에서 x 좌표만 순회하는 경우:

| 항목 | AoS | SoA |
|------|------|------|
| 필요한 데이터 | 40,000 bytes | 40,000 bytes |
| 실제 로드 | 240,000 bytes (24B/파티클) | 40,000 bytes |
| 캐시 라인 로드 | 약 3,750개 | 약 625개 |
| 메모리 대역폭 효율 | 약 17% | 100% |

SoA가 6배 적은 캐시 라인을 로드한다.

### 레이아웃이 벡터 적재 방식을 정한다

캐시 라인 활용률과 같은 계산이 벡터 명령에서 한 번 더 반복된다. 벡터 레지스터도 **연속된 바이트를 통째로 가져오는 것이 가장 싸기** 때문이다.

```cpp
struct Particle { float x, y, z, vx, vy, vz; };   // 24바이트 간격

// 구조체 배열: x는 24바이트씩 떨어져 있다
__m256 xs = _mm256_set_ps(p[7].x, p[6].x, p[5].x, p[4].x,
                          p[3].x, p[2].x, p[1].x, p[0].x);

// 필드별 배열: x는 4바이트씩 붙어 있다
__m256 xs2 = _mm256_loadu_ps(&particles.x[i]);
```

두 줄의 겉모습은 비슷하지만 아래쪽만 명령 하나로 끝난다. 위쪽은 흩어진 주소에서 값을 하나씩 모아 레지스터에 끼워 넣는 작업으로 펼쳐진다. 여기서 확인할 것은 인트린직의 세부가 아니라 **같은 배치 결정이 두 계층에서 같은 방향으로 작용한다**는 사실이다. 캐시 라인을 낭비 없이 채우는 배치가 벡터 레지스터도 낭비 없이 채운다.

반대 방향의 사실도 같이 알아 두어야 한다. 루프가 구조체의 모든 필드를 읽는다면 어차피 모든 바이트를 가져와야 하므로 필드별 배열의 이점이 사라진다. 레이아웃은 데이터 자체의 속성이 아니라 **그 데이터를 읽는 루프의 속성**에 따라 정해진다.

### 하이브리드: AoSoA

때로는 중간 지점이 최적이다. AoSoA(Array of Structure of Arrays)는 작은 SoA 블록을 배열로 관리한다.

```cpp
struct ParticleBlock {
    float x[16], y[16], z[16];
    float vx[16], vy[16], vz[16];
};

std::vector<ParticleBlock> blocks;
```

이는 SoA의 SIMD 이점과 관련 데이터의 공간 지역성을 모두 확보한다. 16개 파티클이 하나의 블록에 모여 있으므로 블록 전체가 캐시에 올라가면 모든 속성에 빠르게 접근할 수 있다.

## 3. DOD 관점의 캐시 최적화

DOD는 캐시 친화적 메모리 레이아웃을 통해 성능을 끌어올린다. 캐시 라인은 64바이트 단위로 로드되므로, "한 캐시 라인 안에 같은 루프에서 쓰이는 데이터를 얼마나 많이 담을 수 있는가"가 핵심 지표다.

### Hot/Cold 데이터 분리

모든 데이터가 똑같이 자주 사용되지는 않다. 매 프레임 사용되는 hot data와 가끔 사용되는 cold data를 분리하면 캐시 라인 활용률이 극적으로 올라간다.

**잘못된 설계:**

```cpp
class Entity {
    // Hot data (매 프레임 사용)
    Vector3 position;      // 12 bytes
    Vector3 velocity;      // 12 bytes
    float health;          // 4 bytes

    // Cold data (가끔 사용)
    std::string name;              // 64+ bytes
    std::string description;       // 64+ bytes
    Texture* icon;                 // 8 bytes
    std::vector<Achievement> achievements;  // 24+ bytes
    EntityStats detailedStats;     // 128+ bytes

    void update(float dt) {
        position += velocity * dt;  // Hot data만 필요
    }
};
```

Entity 크기는 약 256 bytes다. 하지만 update에서는 28 bytes만 필요하다. 낭비율이 89%다. 매 프레임 10,000개 엔티티를 순회하면:

- 필요한 데이터: 280 KB
- 실제 로드: 2.5 MB
- 캐시를 오염시키는 쓸모없는 데이터: 2.22 MB

**최적화된 설계:**

```cpp
// Hot data만 모음 - 캐시 친화적
struct EntityTransform {
    Vector3 position;   // 12 bytes
    Vector3 velocity;   // 12 bytes
    float health;       // 4 bytes
    uint32_t id;        // 4 bytes
};  // Total: 32 bytes - 캐시 라인 절반에 딱 맞음

// Cold data는 별도 저장소
struct EntityMetadata {
    uint32_t entityId;  // 키
    std::string name;
    std::vector<Achievement> achievements;
};

class EntitySystem {
    std::vector<EntityTransform> transforms;                        // Hot
    std::unordered_map<uint32_t, EntityMetadata> metadata;          // Cold

    void update(float dt) {
        for (auto& t : transforms) {
            t.position += t.velocity * dt;
        }
    }
};
```

hot 배열에서 한 cache line에 들어가는 원소 수는 `sizeof(EntityTransform)`, alignment와 cache-line 경계에 따라 계산한다. cold metadata를 분리하면 update가 읽는 bytes/entity를 줄일 수 있지만, 실제 bandwidth 감소율과 실행 시간 개선율은 원래 layout의 padding, prefetch, store traffic과 update 연산량을 측정해야 알 수 있다.

### 접근 빈도별 분류

시스템별로 갱신 주기가 다르다는 점도 활용할 수 있다.

```cpp
// Hot: 물리 업데이트 (60 FPS)
struct PhysicsComponent {
    vec3 position, velocity, acceleration;
    float mass, drag;
};

// Warm: 렌더링 (60 FPS, 컬링으로 일부만)
struct RenderComponent {
    MeshHandle mesh;
    MaterialHandle material;
    mat4 transform;
};

// Cold: AI (10 FPS)
struct AIComponent {
    StateMachine state;
    PathfindingData path;
    BehaviorTree* tree;
};
```

각 컴포넌트를 별도 배열에 저장하면 각 시스템이 필요한 데이터만 순회한다.

### 프리페칭

랜덤 접근이 불가피한 경우, 명시적으로 프리페치할 수 있다.

```cpp
#include <xmmintrin.h>

void processEntities(Entity* entities, size_t count) {
    constexpr int PREFETCH_DISTANCE = 8;

    for (size_t i = 0; i < count; ++i) {
        if (i + PREFETCH_DISTANCE < count) {
            _mm_prefetch((const char*)&entities[i + PREFETCH_DISTANCE],
                        _MM_HINT_T0);
        }
        processEntity(entities[i]);
    }
}
```

순차 접근에서는 하드웨어 프리페처가 자동으로 동작하므로 명시적 프리페치는 필요 없다. 포인터 체이싱이 불가피하면 노드 포인터를 먼저 수집해 프리페치하면서 순회하는 패턴이 유효하다. 다만 근본적 해결책은 링크드 리스트를 배열로 바꾸는 것이다.

### 구조체 패딩 최소화

```cpp
struct ComponentBad {
    bool active;        // 1 byte
                        // 7 bytes 패딩
    double value;       // 8 bytes
    int id;             // 4 bytes
                        // 4 bytes 패딩
};  // Total: 24 bytes, padding 11 bytes (약 45.8%)

struct ComponentGood {
    double value;       // 8 bytes
    int id;             // 4 bytes
    bool active;        // 1 byte
                        // 3 bytes 패딩
};  // Total: 16 bytes (18.75% 낭비)
```

큰 멤버를 먼저 배치하여 패딩을 줄인다.

## 4. 실전 적용: ECS 설계 철학

게임 엔진의 전형적인 OOP 설계는 다음과 같은 모양이다.

```cpp
class GameObject {
public:
    virtual void update(float dt) = 0;
    virtual void render() = 0;
protected:
    Transform transform;
    bool active;
};

class Enemy : public GameObject {
    AI aiController;
    Health health;
    Weapon weapon;

    void update(float dt) override {
        if (!active) return;
        aiController.update(dt);
        weapon.update(dt);
    }
};

std::vector<GameObject*> gameObjects;
for (auto* obj : gameObjects) {
    obj->update(dt);  // 가상 함수, 캐시 미스
}
```

성능 문제는 한 원인으로 환원되지 않는다. 가상 호출 대상 변화, 분산된 객체 배치, 루프에서 쓰지 않는 필드, 데이터 의존성이 각각 cache miss·branch miss·벡터화 가능성에 영향을 준다. 객체 지향 코드도 배치를 바꾸거나 batch API를 제공하면 SIMD를 사용할 수 있으므로 상속 자체가 SIMD를 금지하는 것은 아니다.

### Entity Component System의 DOD 관점

ECS는 DOD의 대표적 적용 사례다. 데이터(Component)와 로직(System)을 분리하고, 동일 컴포넌트를 연속 메모리로 모은다.

**컴포넌트는 순수 데이터:**

```cpp
struct TransformComponent {
    vec3 position, rotation, scale;
};

struct VelocityComponent {
    vec3 linear, angular;
};

struct HealthComponent {
    float current, maximum;
};
```

컴포넌트에 메서드가 있는지보다 시스템이 필요한 필드를 조밀하게 순회할 수 있는지가 중요하다. 불변식을 지키는 작은 메서드는 데이터 지향 배치와 모순되지 않는다.

**시스템은 데이터 변환 로직:**

```cpp
class PhysicsSystem {
    std::vector<TransformComponent> transforms;
    std::vector<VelocityComponent> velocities;

public:
    void update(float dt) {
        for (size_t i = 0; i < transforms.size(); ++i) {
            transforms[i].position += velocities[i].linear * dt;
            transforms[i].rotation += velocities[i].angular * dt;
        }
    }
};
```

시스템은 컴포넌트 배열을 순회하며 데이터를 변환한다. 가상 함수도 분기도 없다.

**게임 루프:**

```cpp
physicsSystem.update(dt);
healthSystem.regenerate(dt);
aiSystem.update(dt);
renderSystem.render();
```

가능한 이득은 조밀한 순회, 불필요한 필드 제외, batch 단위 벡터화와 독립 시스템 scheduling에서 온다. 그러나 모든 ECS가 연속 배치를 보장하는 것은 아니며 sparse set, archetype, pointer-based storage의 비용이 다르다. cache hit, vectorization, job 병렬성은 구현과 질의별로 측정한다.

### 왜 이 형태로 수렴하는가

컴포넌트를 배열로 나눈 것만으로는 아직 한 가지 문제가 남는다. 시스템이 "`Transform`과 `Velocity`를 둘 다 가진 엔티티"를 순회하려 할 때, 두 배열의 인덱스가 같은 엔티티를 가리킨다는 보장이 없다는 것이다. 엔티티마다 컴포넌트 조합이 다르면 배열 길이도 달라진다.

가능한 답은 두 갈래뿐이다.

| 답 | 순회할 때 하는 일 | 대가 |
|---|---|---|
| 조합에 관계없이 배열을 유지하고 인덱스를 사상한다 | 컴포넌트마다 인덱스를 변환해 교차 | 순회마다 간접 참조가 추가된다 |
| 같은 조합끼리 미리 모아 둔다 | 같은 인덱스로 바로 접근 | 조합이 바뀌면 엔티티를 옮겨야 한다 |

두 번째가 archetype이다. 이름은 자료구조처럼 보이지만 실제로 하는 일은 **비용을 순회에서 구조 변경으로 옮기는 것**이다. 매 프레임 반복되는 것은 순회이고 컴포넌트 조합이 바뀌는 일은 드물다는 관찰이 이 교환을 정당화한다. 그 관찰이 성립하지 않는 워크로드 — 매 프레임 상태 플래그를 컴포넌트로 붙였다 떼는 코드 — 에서는 같은 구조가 손해로 뒤집힌다.

여기까지가 데이터 지향 관점에서 ECS에 대해 말할 수 있는 것이다. 어떤 배열을 어떤 단위로 묶고, 인덱스를 어떻게 사상하고, 조합이 바뀔 때 무엇을 복사하는지는 자료구조 설계의 문제이지 레이아웃 원칙의 문제가 아니다. 이 글이 소유하는 주장은 그 앞 단계다. **시스템이 실제로 읽는 필드만 연속으로 놓으면 캐시 라인 활용률이 계산 가능한 값으로 바뀌고, 그 계산이 자료구조 선택의 근거가 된다.**

## 5. 실전 적용: 파티클 시스템

파티클 시스템은 DOD의 이상적인 사용 사례다. 대량의 동질적 데이터, 매 프레임 업데이트, 단순한 데이터 변환.

### DOD 파티클 시스템

```cpp
class ParticleSystem {
    // SoA: 각 속성별로 연속 배열
    std::vector<vec3> positions;
    std::vector<vec3> velocities;
    std::vector<vec4> colors;
    std::vector<float> lifetimes;
    std::vector<float> sizes;

    size_t aliveCount = 0;

public:
    void update(float dt) {
        size_t i = 0;
        while (i < aliveCount) {
            lifetimes[i] -= dt;

            if (lifetimes[i] <= 0.0f) {
                // 죽은 파티클: 마지막 파티클과 swap
                swap(i, aliveCount - 1);
                --aliveCount;
            } else {
                velocities[i] += GRAVITY * dt;
                positions[i] += velocities[i] * dt;

                float normalizedLife = lifetimes[i] / maxLifetime;
                colors[i].a = normalizedLife;

                ++i;
            }
        }
    }

    void emit(const vec3& pos, const vec3& vel, float lifetime) {
        if (aliveCount < positions.size()) {
            positions[aliveCount] = pos;
            velocities[aliveCount] = vel;
            lifetimes[aliveCount] = lifetime;
            colors[aliveCount] = vec4(1.0f);
            sizes[aliveCount] = 1.0f;
            ++aliveCount;
        }
    }

private:
    void swap(size_t a, size_t b) {
        std::swap(positions[a], positions[b]);
        std::swap(velocities[a], velocities[b]);
        std::swap(colors[a], colors[b]);
        std::swap(lifetimes[a], lifetimes[b]);
        std::swap(sizes[a], sizes[b]);
    }
};
```

핵심 최적화는 세 가지다.

1. **연속 메모리**: 살아있는 파티클이 배열 앞쪽에 밀집
2. **분기 최소화**: swap 방식으로 죽은 파티클 제거 (delete 없음)
3. **캐시 친화적**: 순차 접근만 수행

### SIMD 최적화

SoA 덕분에 SIMD 최적화가 간단하다. 단, `std::vector<vec3>`는 성분이 12바이트 간격으로 인터리브되어 있어 `&velocities[i].y`에서 연속 8개를 로드할 수 없다. SIMD를 적용하려면 성분별로 분리된 float 배열(full SoA)이 필요하다.

```cpp
// SIMD를 위해 성분별 분리 배열로 전환
std::vector<float> posY, velY, lifetimes;  // x, z도 동일

void updateSIMD(float dt) {
    const __m256 gravity = _mm256_set1_ps(-9.8f);
    const __m256 dt_vec = _mm256_set1_ps(dt);

    size_t i = 0;
    for (; i + 8 <= aliveCount; i += 8) {
        __m256 life = _mm256_loadu_ps(&lifetimes[i]);
        life = _mm256_sub_ps(life, dt_vec);
        _mm256_storeu_ps(&lifetimes[i], life);

        __m256 vy = _mm256_loadu_ps(&velY[i]);
        vy = _mm256_fmadd_ps(gravity, dt_vec, vy);
        _mm256_storeu_ps(&velY[i], vy);

        __m256 py = _mm256_loadu_ps(&posY[i]);
        py = _mm256_fmadd_ps(vy, dt_vec, py);
        _mm256_storeu_ps(&posY[i], py);
    }

    for (; i < aliveCount; ++i) {
        // 스칼라 버전
    }
}
```

비교에서는 scalar 최적화 빌드, compiler auto-vectorized 빌드와 intrinsic 빌드를 나누고 같은 alive count·tail·메모리 배치를 사용한다. 이 루프는 여러 배열을 읽고 쓰므로 arithmetic throughput보다 메모리 대역폭이 먼저 포화될 수 있다. 처리 particles/s, bytes/s, vectorization report와 cache miss를 함께 기록한다.

## 6. 실전 적용: 물리 시뮬레이션

### Broadphase 충돌 감지

N개 객체의 모든 쌍을 검사하면 $N(N-1)/2$회다. 1,000개면 499,500쌍이며 점근적으로 $O(N^2)$이다.

**DOD 최적화: Sort and Sweep:**

```cpp
class BroadphaseSystem {
    std::vector<float> minX, maxX;
    std::vector<float> minY, maxY;
    std::vector<uint32_t> objectIds;
    std::vector<uint32_t> sortedIndices;

public:
    std::vector<CollisionPair> update() {
        assert(minX.size() == maxX.size() && minX.size() == minY.size() &&
               minX.size() == maxY.size() && minX.size() == objectIds.size());
        sortedIndices.resize(minX.size());
        std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
        std::sort(sortedIndices.begin(), sortedIndices.end(),
            [this](uint32_t a, uint32_t b) {
                return minX[a] < minX[b];
            });

        std::vector<CollisionPair> pairs;
        for (size_t i = 0; i < sortedIndices.size(); ++i) {
            uint32_t idxA = sortedIndices[i];

            for (size_t j = i + 1; j < sortedIndices.size(); ++j) {
                uint32_t idxB = sortedIndices[j];

                if (minX[idxB] > maxX[idxA]) break;

                if (maxY[idxA] < minY[idxB] ||
                    minY[idxA] > maxY[idxB]) continue;

                pairs.push_back({objectIds[idxA], objectIds[idxB]});
            }
        }

        return pairs;
    }
};
```

검사 수는 장면 분포와 축 겹침에 좌우된다. 1,000개 물체의 brute force 쌍 검사는 약 50만 쌍인 반면, sort-and-sweep은 정렬된 축에서 겹치지 않는 구간을 일찍 제외한다. 거의 정렬된 프레임에서는 정렬 갱신이 저렴할 수 있지만, 한 축에 투영된 구간이 대부분 겹치면 후보 쌍 수는 다시 이차 규모에 가까워질 수 있다.

### 강체 적분

```cpp
class RigidBodySystem {
    // Linear
    std::vector<vec3> positions;
    std::vector<vec3> velocities;
    std::vector<vec3> forces;
    std::vector<float> masses;

    // Angular
    std::vector<quat> orientations;
    std::vector<vec3> angularVelocities;
    std::vector<vec3> torques;
    std::vector<mat3> inertiaTensors;

public:
    void integrate(float dt) {
        const size_t count = positions.size();

        for (size_t i = 0; i < count; ++i) {
            vec3 acceleration = forces[i] / masses[i];
            velocities[i] += acceleration * dt;
            positions[i] += velocities[i] * dt;
            forces[i] = vec3(0);
        }

        for (size_t i = 0; i < count; ++i) {
            mat3 invInertia = inertiaTensors[i].inverse();
            vec3 angularAccel = invInertia * torques[i];
            angularVelocities[i] += angularAccel * dt;

            quat spin(0, angularVelocities[i] * 0.5f * dt);
            orientations[i] += spin * orientations[i];
            orientations[i].normalize();

            torques[i] = vec3(0);
        }
    }
};
```

SoA 덕분에 선형 운동과 각운동을 별도 루프로 분리할 수 있고, 각 루프 내에서 캐시 효율이 극대화되며, SIMD 벡터화도 용이하다.

## 7. DOD 설계 원칙

### 원칙 1: 데이터가 모든 것이다

코드보다 데이터를 먼저 생각한다. 데이터가 코드를 결정한다.

설계를 시작하기 전에 다음 질문에 답한다.

```text
- 데이터의 크기는? (10개? 10,000개? 10,000,000개?)
- 데이터의 형태는? (균일? 다양?)
- 접근 패턴은? (순차? 랜덤? 읽기 전용? 읽기/쓰기?)
- 생명주기는? (정적? 동적? 빈번한 생성/삭제?)
- 데이터 간 관계는? (독립? 계층? 그래프?)
```

추상 인터페이스를 먼저 정의하지 말고 데이터 레이아웃부터 시작한다.

```cpp
// 나쁜 예: 추상화부터 시작
class IUserManager {
    virtual void addUser(const User& user) = 0;
    virtual void removeUser(uint32_t id) = 0;
    // ... 20개 메서드
};

// 좋은 예: 데이터 분석부터
// 분석: 10만 명 사용자, 대부분 읽기, 가끔 업데이트
// 90%는 레벨/경험치만 확인, 10%만 상세 정보 필요

struct UserDatabase {
    std::vector<uint32_t> userIds;
    std::vector<uint8_t> levels;
    std::vector<uint32_t> exp;
    std::unordered_map<uint32_t, UserDetails> details;  // Cold
};
```

### 원칙 2: 하드웨어를 이해하라

CPU의 레지스터 파일, 여러 캐시 계층, TLB와 DRAM은 용량·지연·대역폭이 서로 다르지만 고정된 한 표는 존재하지 않는다. load-to-use latency는 CPU 세대, hit한 cache slice, NUMA 위치와 동시 outstanding miss 수에 따라 달라지고, 순차 스트림은 prefetch와 memory-level parallelism으로 일부 지연을 숨긴다. 대상 하드웨어의 vendor 문서와 pointer-chase/stream benchmark를 구분해 측정한다.

C/C++의 다차원 배열은 행 우선이다. 이에 맞춰 순회한다.

```cpp
// 좋은 예: 행 우선 (row-major)
for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
        process(image[y][x]);  // 순차 접근
    }
}
```

### 원칙 3: 존재하지 않는 문제를 풀지 마라

YAGNI(You Aren't Gonna Need It)의 DOD 버전이다. 실제로 필요한 것만 구현한다.

**안티패턴: 과도한 일반화**

```cpp
// 나쁜 예: "미래에 필요할지도..."
template<typename T, typename Allocator = std::allocator<T>,
         typename IndexType = size_t, size_t Alignment = 64,
         typename GrowthPolicy = ExponentialGrowth>
class FlexibleArray {
    // 500 줄의 복잡한 코드
};

// 실제로는 std::vector<float>만 사용됨

// 좋은 예: 실제 필요한 것만
std::vector<float> positions;
```

**점진적 복잡도:**

```cpp
// V1: 단순 구현
struct Particle {
    vec3 position, velocity;
};
std::vector<Particle> particles;
// 측정: 충분히 빠름? → 완료

// 아니라면...
// V2: SoA
std::vector<vec3> positions, velocities;
// 측정: 아직 느림? → 계속

// V3: SIMD
void updateSIMD(float dt);
```

프로파일링 없는 최적화는 시기상조다.

## 8. DOD의 한계와 트레이드오프

### DOD의 한계

**복잡한 관계 모델링 어려움**

그래프나 트리 구조는 포인터가 자연스럽다.

```cpp
// OOP: 직관적
struct Node {
    std::vector<Node*> neighbors;
};

// DOD: CSR(Compressed Sparse Row)로 모든 edge를 연속 저장
struct GraphDOD {
    std::vector<uint32_t> nodeIds;
    std::vector<uint32_t> offsets;   // node i의 범위: [offsets[i], offsets[i+1])
    std::vector<uint32_t> neighbors;
};
```

CSR은 관계 갱신이 드문 그래프의 순회 locality와 메모리 오버헤드를 개선한다. 반대로 edge 삽입·삭제가 잦으면 각 adjacency list, chunked edge pool 또는 다른 동적 표현이 더 적합할 수 있다. DOD가 항상 관계를 복잡하게 만드는 것이 아니라, workload에 맞는 저장 형태를 명시적으로 선택하게 한다.

**개발 속도와 가독성**

- 초기 설계 시간 증가
- 요구사항 변경 시 리팩토링 비용
- 데이터 분석과 측정 필요

```cpp
// OOP: 직관적
player.takeDamage(10);

// DOD: 덜 직관적
healthSystem.applyDamage(playerId, 10);
```

### 언제 DOD를 사용할 것인가

**DOD가 적합:**
- 대량의 동질적 데이터 (파티클, 엔티티, 강체)
- 성능이 중요한 루프 (게임 업데이트, 물리)
- SIMD 최적화 가능한 연산
- 명확한 접근 패턴

**OOP가 적합:**
- 소량의 복잡한 객체 (UI, 매니저)
- 복잡한 계층 구조와 다형성
- 플러그인 아키텍처
- 빠른 프로토타이핑

**하이브리드 접근:**

```cpp
// UI: OOP
class UIButton : public UIWidget {
    virtual void onClick() override;
};

// 게임 로직: DOD
class EntitySystem {
    std::vector<TransformComponent> transforms;
};

// 렌더링: 혼합
class RenderSystem {
    std::vector<DrawCommand> drawCommands;          // DOD
    std::unique_ptr<ShaderManager> shaderManager;   // OOP
};
```

모든 코드를 DOD로 바꿀 필요는 없다. 병목 지점에 집중한다.

## 9. 측정과 검증

### 벤치마크 작성

DOD의 효과를 검증하려면 측정이 필수다.

```cpp
class ScopedTimer {
    const char* name;
    high_resolution_clock::time_point start;

public:
    ScopedTimer(const char* n) : name(n),
        start(high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        printf("%s: %lld us\n", name, duration.count());
    }
};

void benchmarkOOP() {
    std::vector<GameObject*> objects(10000);
    initializeObjects(objects);

    ScopedTimer timer("OOP Update");
    for (int i = 0; i < 1000; ++i) {
        for (auto* obj : objects) {
            obj->update(0.016f);
        }
    }
}

void benchmarkDOD() {
    EntitySystem system(10000);
    initializeSystem(system);

    ScopedTimer timer("DOD Update");
    for (int i = 0; i < 1000; ++i) {
        system.update(0.016f);
    }
}
```

### 프로파일러 사용

전문 프로파일러로 캐시 미스를 측정한다.

```bash
# Linux perf
perf stat -e cache-misses,cache-references ./program

# Intel VTune
vtune -collect memory-access ./program

# Tracy Profiler (게임 엔진에 적합)
# https://github.com/wolfpld/tracy
```

캐시 미스율 하나만으로 DOD의 성공을 판정하지 않는다. miss의 절대 횟수와 수준(L1, LLC, TLB), 처리 원소 수, instruction 수, frame-time 분포를 함께 비교해야 한다. 데이터 변환과 유지보수 비용까지 포함해 동일한 결과를 더 안정적인 시간 예산 안에 계산하는지가 판단 기준이다.

### 실전 체크리스트

설계 단계에서 확인할 항목:

- 실제 데이터 크기와 개수를 파악했는가?
- 데이터 접근 패턴을 분석했는가?
- Hot/Cold 데이터를 구분했는가?
- SoA가 AoS보다 나은가?
- SIMD 벡터화 가능성을 고려했는가?
- 캐시 라인 크기(64 bytes)를 고려했는가?

최적화 단계:

- 프로파일링으로 병목을 확인했는가?
- 측정 가능한 성능 목표가 있는가?
- 메모리 레이아웃이 캐시 친화적인가?
- 분기 예측 실패를 최소화했는가?
- 포인터 체이싱을 피했는가?
- 최적화 후 성능을 다시 측정했는가?


---

## 결론

Data-Oriented Design은 현대 하드웨어의 특성을 최대한 활용하여 성능을 극대화하는 프로그래밍 패러다임이다. OOP의 추상화와 개념 모델링 대신, 실제 데이터의 변환과 하드웨어 특성에 집중한다.

핵심은 다섯 가지로 요약된다. 코드가 아닌 데이터가 설계를 주도한다. 캐시·메모리 대역폭·SIMD를 활용한다. 대부분의 경우 SoA가 유리하다. Hot/Cold 분리로 캐시 효율을 극대화한다. 존재하지 않는 문제를 풀지 말고, 측정하고 최적화한다.

DOD는 도구다. 프로파일링으로 병목을 찾고, 실제 데이터를 분석하고, 측정 가능한 목표를 세운다. 복잡도와 성능 사이의 균형을 찾는 것이 진정한 엔지니어링이다.

## 더 읽을거리

- Mike Acton, "Data-Oriented Design and C++" (CppCon 2014). https://www.youtube.com/watch?v=rX0ItVEVjHc
- Stoyan Nikolov, "OOP Is Dead, Long Live Data-oriented Design" (CppCon 2018). https://www.youtube.com/watch?v=yy8jQgmhbAU
- Richard Fabian, "Data-Oriented Design". https://www.dataorienteddesign.com/dodbook/
- Unity DOTS (Data-Oriented Technology Stack). https://unity.com/dots
- EnTT - Entity Component System library. https://github.com/skypjack/entt
- FLECS - Fast Lightweight ECS. https://github.com/SanderMertens/flecs
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007).
- M. Frigo, C. Leiserson, H. Prokop, S. Ramachandran, "Cache-Oblivious Algorithms" (1999).
