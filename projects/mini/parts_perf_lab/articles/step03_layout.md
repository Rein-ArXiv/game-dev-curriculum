## 들어가며

현재까지의 결과물은 토글 패널과 프레임 그래프가 동작하지만, *실제로 동작하는 토글은 엔티티 수* 한 가지였다. 1k에서 100k로 바꾸면 그래프가 갈라지지만, 그건 *수가 늘어서 느려진* 자명한 결과다. 측정 도구의 진짜 가치는 *같은 수의 엔티티를 다른 방식으로 처리할 때* 어떻게 갈라지는가에 있다. 이 단계가 그 첫 번째 *진짜* 비교 축을 연다.

이 단계의 목표는 *메모리 레이아웃 토글* 을 활성화하는 것이다. 같은 1만 점 시뮬을 SoA, AoS, Archetype 세 레이아웃으로 각각 구현해 두고, 콤보박스 한 번 클릭으로 시뮬 인스턴스 자체가 교체된다. 그 순간 update ms가 갈라지는 모습이 그래프에 그려진다. 결과 차이의 *원인* 은 메모리 접근 패턴 하나로 확정되지 않는다. 데이터가 SIMD 레지스터에 들어오는 방식, 연속성, 사용하는 필드의 비율, 컴파일러의 변환, 캐시·메모리 대역폭이 함께 작용한다.

> 이 실습은 게임 개발의 모든 CS 주제를 하나의 프로그램에 구현하는 과정이 아니다. 같은 상태를 여러 메모리 배치와 연산 방식으로 처리하고, 할당·순회 비용을 나누어 관찰하는 저수준 성능 실습이다. ECS 는 단일 컴포넌트 조합의 저장 배치까지만 다룬다. query 와 구조 변경, 문자열·인코딩·동시성 등은 별도 실습에서 다룬다.
>
> 실행 결과가 특정 순서로 나와야 성공하는 실습은 아니다. 먼저 같은 입력에 대해 같은 의미의 결과를 계산하는지 확인한다. 그다음 작업량·측정 구간·실행 환경을 맞추고 반복 측정한다. 기대와 다른 성능이 나오면 결과를 숨기거나 구현 오류로 단정하지 않고, 이 실험이 어떤 가정을 충족하지 못했는지 확인한다.
>
> 각 장의 코드는 세 종류로 구분한다. `현재 단계 완성 코드`는 해당 단계의 파일 집합과 함께 빌드할 수 있어야 한다. `발췌 코드`는 생략된 선언과 helper 의 실제 위치를 함께 적는다. `설명용 의사코드`는 의미와 제어 흐름을 보여주며 그대로 붙여 넣는 소스가 아니다. 최종 `src/`와 중간 단계의 함수 시그니처가 다르면 변환 표를 제공한다.

이 단계의 결과물은 다음 다섯 가지다.

1. `ISimulation` 추상 인터페이스
2. `SoASimulation` — 컴포넌트별 분리 배열
3. `AoSSimulation` — struct 배열
4. `ArchetypeSimulation` — 256-entity chunk 단위 SoA
5. `Layout` enum에서 인스턴스를 만드는 `CreateSimulation` 팩토리

이 단계에서 SIMD 토글은 여전히 grayed out이다. Update가 `simd` 인자를 받지만 모든 경로가 scalar로 폴백한다. 수동 SIMD 경로는 아직 비어 있고, SoA·Archetype에 SSE/AVX2 경로를 채울 여지를 남긴다.

---

## 목차
1. [왜 메모리 레이아웃이 성능을 가르는가](#1-왜-메모리-레이아웃이-성능을-가르는가)
2. [ISimulation 추상 인터페이스와 팩토리](#2-isimulation-추상-인터페이스와-팩토리)
3. [SoA: Structure of Arrays](#3-soa-structure-of-arrays)
4. [AoS: Array of Structures](#4-aos-array-of-structures)
5. [Archetype: Chunk 단위 SoA](#5-archetype-chunk-단위-soa)
6. [main의 다형성 통합과 Hud 활성화](#6-main의-다형성-통합과-hud-활성화)
7. [빌드와 실행 결과](#7-빌드와-실행-결과)

---

## 1. 왜 메모리 레이아웃이 성능을 가르는가

같은 1만 개 엔티티를 *어떻게 메모리에 펼치느냐* 가 처리 속도를 가른다. 핵심은 두 가지다.

### 캐시 라인 활용도

CPU는 메모리에서 한 번에 *캐시 라인 단위* (보통 64 byte)로 데이터를 가져온다. 64 byte는 16개 float 또는 8개 double에 해당한다. 핫 루프가 한 번에 만지는 데이터가 같은 캐시 라인 안에 *조밀하게* 모여 있을수록 메모리 대역폭이 효율적이다.

### 자동 벡터화 가능성

컴파일러는 핫 루프가 *연속된 메모리* 의 *동일 타입 원소* 를 *같은 연산* 으로 처리할 때 SSE/AVX 명령으로 자동 변환할 수 있다. 한 번의 명령이 4개(SSE) 또는 8개(AVX) float을 동시에 처리하면 산술 처리량은 4~8배 늘어날 수 있다. 자동 벡터화의 전제는 데이터 배치가 SIMD 친화적이라는 점이다.

### 세 가지 레이아웃의 차이

같은 *1만 개의 (posX, posY, velX, velY) 정점* 을 다음 셋으로 펼칠 수 있다.

```mermaid
flowchart LR
    A[동일 초기 fixture와 동일 업데이트 규칙] --> B[AoS: vector Entity]
    A --> C[SoA: posX / posY / velX / velY 배열]
    A --> D[Chunked SoA: vector unique_ptr Chunk]
    B --> E[매 step 네 필드 모두 사용]
    C --> E
    D --> E
    E --> F[독립 참조와 결과 비교]
    F --> G[동일 kernel 정책으로 반복 측정]
```

이 그림은 실제 구현용이다. 일반적인 ECS query나 position-only hot loop를 설명하는 그림은 별도 그림으로 분리한다. SoA와 Chunked SoA가 비슷한 성능을 내야 합격하는 것이 아니라, 같은 상태를 처리하고 측정 조건을 설명할 수 있어야 합격이다.

세 레이아웃의 핫 루프 메모리 접근 패턴은 다음 표로 요약된다.

| 레이아웃 | 핫 루프가 한 번에 만지는 위치 | SIMD 연속 로드 | 자동 벡터화 |
|---|---|---|---|
| SoA | `posX[0..15]` 한 캐시 라인 | 인접 4/8개 직접 로드 가능 | 매우 친화 |
| AoS | `entity[0]` 한 정점의 4 필드 (모두 사용) | 한 컴포넌트만 모으면 stride가 sizeof(Entity)라 `_mm_loadu_ps` 인접 로드가 안 맞음 | 컴파일러에 따라 SLP·shuffle로 부분 벡터화 가능 |
| Archetype | chunk 내부에서 SoA와 동일 | 인접 로드 가능 | chunk 내부는 SoA와 같은 형태 |

핵심은 *캐시 낭비* 가 아니다 — 이 시뮬은 한 정점의 4 필드(posX/posY/velX/velY)를 모두 매 프레임 사용하므로 AoS도 캐시 라인을 거의 다 쓴다. 차이는 *SIMD가 인접 4/8개의 같은 컴포넌트를 한 번에 로드할 수 있느냐* 다. AoS는 한 컴포넌트만 모으면 stride가 16 byte라 SSE의 `_mm_loadu_ps` 같은 인접 로드가 그대로 맞지 않는다. 다만 *AoS라서 자동 벡터화가 불가능한 것은 아니다*. 컴파일러는 SLP·shuffle/transpose 등으로 AoS 루프도 부분 벡터화할 수 있다. 이 lab의 AoS에는 수동 SIMD 경로를 넣지 않았다는 것이 구현 범위다. 이 단계가 그 차이를 *시각적 사건* 으로 만든다.

## 2. ISimulation 추상 인터페이스와 팩토리

세 레이아웃은 *데이터 배치만 다른 같은 워크로드* 다. 인터페이스를 추상 클래스로 통일하고, 토글에 따라 *인스턴스 자체* 를 교체한다.

```cpp
// src/sim/isimulation.h

#pragma once

#include "ui/toggles.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace ppl {

class ISimulation {
public:
    virtual ~ISimulation() = default;

    virtual void Init(std::size_t entityCount, int viewportW, int viewportH) = 0;
    virtual void Update(float dt, SimdWidth simd, bool branchless) = 0;
    virtual void Draw() const = 0;
    virtual void Shutdown() = 0;

    virtual std::size_t Count() const = 0;

    // GUI와 독립적인 정확성 검증용 스냅숏. 엔티티 순서대로
    // [posX, posY, velX, velY]를 기록한다.
    virtual void CopyState(std::vector<float>& out) const = 0;
};

std::unique_ptr<ISimulation> CreateSimulation(Layout layout);

}  // namespace ppl
```

`Update` 의 인자에 `SimdWidth simd` 와 `bool branchless` 가 미리 들어 있다. 이 단계의 스냅숏에서는 모든 구현이 이 둘을 받기만 하고 하나의 경로로 동작한다. 시그니처를 처음부터 넓게 잡아 두면 처리 경로가 늘어날 때 인터페이스를 다시 건드리지 않아도 된다.

### 검증용 상태 스냅숏: CopyState

`CopyState` 는 화면 출력과 무관하게 시뮬 상태를 꺼내는 통로다. 세 레이아웃은 내부 저장 방식이 전부 다르지만(분리 배열 / struct 배열 / chunk), `CopyState` 는 모두 *엔티티 순서대로 `[posX, posY, velX, velY]`* 를 이어 붙인 평탄한 `float` 배열을 만든다. 같은 시드·같은 `dt` 로 돌린 두 구현의 출력 벡터를 원소별로 비교하면 레이아웃 변경이 결과를 바꾸지 않았다는 것을 확인할 수 있다.

`tests/core_tests.cpp` 가 이 비교를 수행한다. 정렬 헬퍼(`AlignUp`), 네 할당자, 컨테이너·이벤트 워크로드의 계약, 세 레이아웃의 결과 동등성, scalar·SSE·AVX2 경로의 허용 오차, 그리고 경계 반사 불변식을 한 실행 파일에서 검사한다. CMake가 이 파일과 `sim/`·`alloc/`·`measure/` 소스를 묶어 별도 타깃으로 만들고 `add_test` 로 등록한다.

아래 CMake·테스트 블록과 이 장 뒤쪽의 `tests/core_tests.cpp` 발췌는 최종 `src/` 파일 집합을 기준으로 한다. 이 장 앞쪽의 단계별 코드 조각과 최종 파일의 시그니처가 다르면 함수 이름을 기준으로 대응시킨다. `Draw()` 처럼 화면 출력에만 쓰는 TU는 계산 코어와 분리되어 있다.

```cmake
enable_testing()
add_executable(parts_core_tests
    tests/core_tests.cpp
    tests/headless_draw.cpp
    src/sim/soa.cpp
    src/sim/aos.cpp
    src/sim/archetype.cpp
    src/sim/factory.cpp
    src/measure/container_benchmark.cpp
    src/measure/event_benchmark.cpp
    src/measure/frame_timer.cpp
    src/alloc/malloc_allocator.cpp
    src/alloc/arena_allocator.cpp
    src/alloc/pool_allocator.cpp
    src/alloc/slab_allocator.cpp
)
target_include_directories(parts_core_tests PRIVATE src)
add_test(NAME parts_core_tests COMMAND parts_core_tests)
```

한 실행 파일이지만 실행 단위는 하나가 아니다. `main` 이 `argv[1]` 을 그룹 이름으로 받아 그 그룹의 검사만 돌리고, CMake가 그룹마다 `add_test` 를 하나씩 더 등록한다. 전체를 한 번에 도는 `parts_core_tests` 는 그대로 남고, 그 옆에 주제별 이름이 나란히 선다.

<!-- snippet: CMakeLists.txt@group-tests -->
```cmake
function(parts_add_group_test name group)
    add_test(NAME ${name} COMMAND parts_core_tests ${group})
endfunction()

parts_add_group_test(parts_skeleton_tests  skeleton)
parts_add_group_test(parts_toggles_tests   toggles)
parts_add_group_test(parts_layout_tests    layout)
parts_add_group_test(parts_simd_tests      simd)
parts_add_group_test(parts_branch_tests    branch)
parts_add_group_test(parts_allocator_tests allocator)
parts_add_group_test(parts_container_tests container)
parts_add_group_test(parts_graph_tests     graph)
```

`ctest -R parts_layout_tests` 처럼 이름 하나를 골라 돌리면 그 주제의 검사만 돈다. 무엇을 만들다 깨졌는지가 ctest 출력의 이름에서 바로 보이고, 실패한 그룹만 반복해서 돌릴 수 있다.

빌드 후 `ctest --test-dir build -C Release --output-on-failure` 로 실행한다. 이 타깃은 raylib을 링크하지 않는다. 레이아웃 구현의 `Draw()` 는 `src/sim/*_draw.cpp` 라는 별도 번역 단위에 있고, 그 파일들은 앱 타깃에만 들어간다. `Draw()` 가 가상 함수라 vtable에 주소가 필요하므로 테스트 타깃은 대신 빈 정의만 담은 `tests/headless_draw.cpp` 를 링크한다. 그 결과 GLFW·OpenGL 없이 계산 코어만 빌드해 검증할 수 있고, 테스트는 창을 열지 않고 `Update` 와 `CopyState` 만 호출한다.

### 동등성만으로 부족한 지점

세 레이아웃의 출력이 서로 같은지만 보면 워크로드 규칙 자체를 지워도 모든 구현이 여전히 서로 같은 값을 낸다. 그래서 구현 사이 비교와 별개로 *절대 기준* 도 함께 검사한다. 갱신 후 모든 엔티티가 `[0, w-1] x [0, h-1]` 안에 있는가, 반사는 부호만 뒤집으므로 `|velX|`·`|velY|` 가 성분별로 보존되는가, 그리고 좁은 뷰포트에 큰 `dt` 를 주었을 때 실제로 속도 부호가 뒤집힌 엔티티가 나오는가. 마지막 항목은 엔티티별로 확인한다. 총합만 세면 SIMD 주 루프의 반사가 살아 있는 한 잔여(`count % 4`) 구간의 반사가 사라져도 드러나지 않기 때문이다.

팩토리는 단순한 switch다.

```cpp
// src/sim/factory.cpp

#include "sim/aos.h"
#include "sim/archetype.h"
#include "sim/isimulation.h"
#include "sim/soa.h"

namespace ppl {

std::unique_ptr<ISimulation> CreateSimulation(Layout layout) {
    switch (layout) {
        case Layout::SoA:       return std::make_unique<SoASimulation>();
        case Layout::AoS:       return std::make_unique<AoSSimulation>();
        case Layout::Archetype: return std::make_unique<ArchetypeSimulation>();
    }
    return std::make_unique<SoASimulation>();
}

}  // namespace ppl
```

vtable 호출 한 번이 dispatch 비용이지만, 1만 점 처리에 드는 마이크로초에 비하면 nanosecond 수준이라 무시할 수 있다. *프레임 단위 dispatch* 가 핫 루프 밖에 있는 게 핵심이다.

## 3. SoA: Structure of Arrays

SoA는 *컴포넌트별로 분리된 배열* 을 둔다. `posX`, `posY`, `velX`, `velY` 가 각각 별도의 `std::vector<float>` 다.

### 헤더

```cpp
// src/sim/soa.h

#pragma once

#include "sim/isimulation.h"

#include <vector>

namespace ppl {

class SoASimulation final : public ISimulation {
public:
    void Init(std::size_t entityCount, int viewportW, int viewportH) override;
    void Update(float dt, SimdWidth simd, bool branchless) override;
    void Draw() const override;
    void Shutdown() override;
    std::size_t Count() const override { return count_; }
    void CopyState(std::vector<float>& out) const override;

private:
    void UpdateScalar(float dt);

    std::vector<float> posX_, posY_, velX_, velY_;
    std::size_t count_ = 0;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
```

### 구현

```cpp
// 발췌: src/sim/soa.cpp(계산 코어) + src/sim/soa_draw.cpp(Draw).
// 두 파일은 별도 TU로 컴파일된다. 계산 코어 soa.cpp 에는 raylib.h include가 없고
// raylib을 링크하지 않는 테스트 타깃에도 그대로 들어간다. 아래 블록은 설명을 위해
// 두 파일을 이어 붙인 것이며, 각 include 옆에 어느 파일에 속하는지 표시한다.

#include "sim/soa.h"        // soa.cpp / soa_draw.cpp 공통

#include "raylib.h"         // soa_draw.cpp 전용

#include <algorithm>        // soa.cpp 전용
#include <random>

namespace ppl {

void SoASimulation::Init(std::size_t entityCount, int viewportW, int viewportH) {
    count_ = entityCount;
    viewportW_ = viewportW;
    viewportH_ = viewportH;

    posX_.resize(count_);
    posY_.resize(count_);
    velX_.resize(count_);
    velY_.resize(count_);

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> distX(0.0f,
                                                 static_cast<float>(viewportW_));
    std::uniform_real_distribution<float> distY(0.0f,
                                                 static_cast<float>(viewportH_));
    std::uniform_real_distribution<float> distV(-120.0f, 120.0f);

    for (std::size_t i = 0; i < count_; ++i) {
        posX_[i] = distX(rng);
        posY_[i] = distY(rng);
        velX_[i] = distV(rng);
        velY_[i] = distV(rng);
    }
}
```

Update와 Draw는 SoA 배열을 직접 순회한다.

```cpp
void SoASimulation::Update(float dt, SimdWidth /*simd*/, bool /*branchless*/) {
    // 현재 상태: simd/branchless 인자를 받기만 하고 scalar 경로 하나로 처리한다.
    UpdateScalar(dt);
}

void SoASimulation::UpdateScalar(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    for (std::size_t i = 0; i < count_; ++i) {
        posX_[i] += velX_[i] * dt;
        posY_[i] += velY_[i] * dt;

        if (posX_[i] < 0.0f || posX_[i] > maxX) {
            velX_[i] = -velX_[i];
            posX_[i] = std::clamp(posX_[i], 0.0f, maxX);
        }
        if (posY_[i] < 0.0f || posY_[i] > maxY) {
            velY_[i] = -velY_[i];
            posY_[i] = std::clamp(posY_[i], 0.0f, maxY);
        }
    }
}

void SoASimulation::Draw() const {
    for (std::size_t i = 0; i < count_; ++i) {
        DrawPixel(static_cast<int>(posX_[i]),
                  static_cast<int>(posY_[i]),
                  WHITE);
    }
}

void SoASimulation::CopyState(std::vector<float>& out) const {
    out.resize(count_ * 4);
    for (std::size_t i = 0; i < count_; ++i) {
        out[i * 4 + 0] = posX_[i];
        out[i * 4 + 1] = posY_[i];
        out[i * 4 + 2] = velX_[i];
        out[i * 4 + 3] = velY_[i];
    }
}

void SoASimulation::Shutdown() {
    posX_.clear(); posY_.clear();
    velX_.clear(); velY_.clear();
    count_ = 0;
}

}  // namespace ppl
```

`UpdateScalar` 의 핫 루프가 SoA의 핵심이다.

```cpp
posX_[i] += velX_[i] * dt;
posY_[i] += velY_[i] * dt;
```

이 두 줄에서 컴파일러는 다음을 본다.

- `posX_[i]` 를 만지는 동안 `posX_[i+1]`, `posX_[i+2]`, ... 도 같은 캐시 라인에 들어와 있다.
- `velX_[i]` 도 마찬가지로 인접 4~16개가 같이 적재된다.
- 인덱스 `i` 는 단조 증가하는 *induction variable* 이다.

이 셋이 모두 만족하면 컴파일러가 *자동 벡터화* 를 시도할 수 있다. 실제 적용 여부는 최적화 레벨, alias 분석, 분기 형태에 달린다. *수동 SIMD* 와 차이는, 자동 벡터화가 주로 *분기 없는 산술 부분* 에서 잘 먹힌다는 점이다. 경계 반사 분기는 자동 벡터화 대상에서 빠질 수 있다.

## 4. AoS: Array of Structures

AoS는 *한 엔티티의 모든 필드가 메모리에 인접* 한 모델이다. C++의 `struct` 배열 그대로다.

### 헤더

```cpp
// src/sim/aos.h

#pragma once

#include "sim/isimulation.h"

#include <vector>

namespace ppl {

class AoSSimulation final : public ISimulation {
public:
    struct Entity {
        float posX, posY;
        float velX, velY;
    };

    void Init(std::size_t entityCount, int viewportW, int viewportH) override;
    void Update(float dt, SimdWidth simd, bool branchless) override;
    void Draw() const override;
    void Shutdown() override;
    std::size_t Count() const override { return entities_.size(); }
    void CopyState(std::vector<float>& out) const override;

private:
    void UpdateScalar(float dt);

    std::vector<Entity> entities_;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
```

`Entity` 는 16 byte다(4 byte float × 4). 캐시 라인 64 byte에 4개 엔티티가 들어간다.

### 구현 핫 루프

```cpp
void AoSSimulation::UpdateScalar(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    for (auto& e : entities_) {
        e.posX += e.velX * dt;
        e.posY += e.velY * dt;

        if (e.posX < 0.0f || e.posX > maxX) {
            e.velX = -e.velX;
            e.posX = std::clamp(e.posX, 0.0f, maxX);
        }
        if (e.posY < 0.0f || e.posY > maxY) {
            e.velY = -e.velY;
            e.posY = std::clamp(e.posY, 0.0f, maxY);
        }
    }
}
```

핫 루프가 한 엔티티의 *4개 필드* 를 차례로 만진다. 같은 캐시 라인 안이라 임의 접근에는 좋다. 다만 한 컴포넌트의 값 4개(`posX` 네 개)를 SSE 한 명령에 모으려면 stride가 16 byte라 `_mm_loadu_ps` 같은 인접 로드가 그대로 맞지 않는다. gather/scatter, shuffle/transpose, 개체 내 필드 벡터화(SLP), 임시 SoA 복사 같은 우회 전략이 있고, 컴파일러가 그중 일부를 자동으로 수행하기도 한다. 이 lab의 AoS에 *수동 SIMD 경로를 넣지 않았다* 는 것이 구현 범위이고, 그 자체가 *AoS는 SIMD가 불가능하다* 는 결론은 아니다. AoS는 *임의 접근* (예: 특정 ID로 한 엔티티의 모든 필드 한 번에 읽기) 에 강하고, *대량 핫 루프* 에서는 컴파일러 변환에 따라 손익이 갈린다.

자동 벡터화는 컴파일러·플래그·루프 형태에 따라 부분적으로 일어날 수 있다. strided load를 인식해 묶는 경우도 있고, 개체 단위로 필드를 묶는 SLP가 도는 경우도 있다. 다만 SoA와 같은 폭의 이득을 항상 보장하지는 않으므로, 실제 변환 결과는 어셈블리나 컴파일러 벡터화 리포트로 확인한다.

게임 엔진의 일반 규칙은 *전체 엔티티를 매 프레임 같은 방식으로 처리하면 SoA, 일부 엔티티에 임의 접근하면 AoS* 다. Archetype은 둘을 청크 단위로 결합한다.

## 5. Archetype: Chunk 단위 SoA

Archetype은 *같은 컴포넌트 조합을 가진 엔티티들을 chunk로 묶어 SoA로 저장* 하는 모델이다. 이 lab은 *단일 컴포넌트 조합*(pos+vel 네 float)만 다룬다. Entity generation, component 추가·삭제, query matching, 다른 archetype으로의 migration은 구현하지 않았다. 같은 SoA라는 용어를 쓴다고 다른 엔진의 구체 저장 방식까지 이 구조와 같다고 단정하지 않는다. 예를 들어 Bevy의 Table은 컴포넌트별 type-erased column을 설명하는 자료이고(<https://docs.rs/bevy_ecs/latest/bevy_ecs/storage/struct.Table.html>), 이 lab의 고정 256개 `Chunk`를 직접 증명하지 않는다.

### 헤더

```cpp
// src/sim/archetype.h

#pragma once

#include "sim/isimulation.h"

#include <array>
#include <memory>
#include <vector>

namespace ppl {

class ArchetypeSimulation final : public ISimulation {
public:
    static constexpr std::size_t kChunkCapacity = 256;

    struct Chunk {
        std::array<float, kChunkCapacity> posX;
        std::array<float, kChunkCapacity> posY;
        std::array<float, kChunkCapacity> velX;
        std::array<float, kChunkCapacity> velY;
        std::size_t size = 0;
    };

    void Init(std::size_t entityCount, int viewportW, int viewportH) override;
    void Update(float dt, SimdWidth simd, bool branchless) override;
    void Draw() const override;
    void Shutdown() override;
    std::size_t Count() const override { return totalCount_; }
    void CopyState(std::vector<float>& out) const override;

private:
    void UpdateScalar(float dt);

    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::size_t totalCount_ = 0;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
```

`Chunk` 는 256 entity 단위. 한 chunk 안은 *4개 분리 배열* (`posX[256]`, `posY[256]`, `velX[256]`, `velY[256]`) 로 SoA다. chunk 외부는 `vector<unique_ptr<Chunk>>` 이고 각 `Chunk` 는 별도로 확보된다. 네 배열의 payload 합은 256 × 4 float × 4 byte = 4096 byte지만, `size_t size` 멤버와 정렬을 포함한 구조체 전체 크기는 다르다. 이번 x86-64 빌드에서 `sizeof(Chunk)` 는 4104, `alignof(Chunk)` 는 8이었다. 64 byte 정렬(`alignas`)을 추가하면 padding 비용까지 더 늘어난다. unaligned load를 쓰므로 이 8 byte 정렬이 곧바로 SIMD 미정렬 접근 오류가 되는 것은 아니다.

### 구현 핫 루프

```cpp
void ArchetypeSimulation::UpdateScalar(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);
    const float maxX = w - 1.0f;
    const float maxY = h - 1.0f;

    for (auto& chunkPtr : chunks_) {
        Chunk& c = *chunkPtr;
        const std::size_t n = c.size;

        for (std::size_t i = 0; i < n; ++i) {
            c.posX[i] += c.velX[i] * dt;
            c.posY[i] += c.velY[i] * dt;

            if (c.posX[i] < 0.0f || c.posX[i] > maxX) {
                c.velX[i] = -c.velX[i];
                c.posX[i] = std::clamp(c.posX[i], 0.0f, maxX);
            }
            if (c.posY[i] < 0.0f || c.posY[i] > maxY) {
                c.velY[i] = -c.velY[i];
                c.posY[i] = std::clamp(c.posY[i], 0.0f, maxY);
            }
        }
    }
}
```

내부 루프(`i = 0..n`)는 SoA와 *문자 그대로 같은 형태* 다. `posX`, `velX` 가 chunk 안에서 인접 메모리에 있다. 컴파일러가 자동 벡터화하기 좋은 형태다.

차이는 두 가지다.

1. **외부 루프 오버헤드**: chunk마다 메모리 할당과 포인터 추적이 한 번씩 더. 실제 비용은 chunk 수 × 한 번의 indirect access 정도.
2. **chunk 메타데이터**: 각 chunk가 `size_t size` 를 들고 있어 *부분 chunk* 처리가 자연스럽다.

이 시뮬은 *모든 엔티티가 같은 컴포넌트(pos+vel)* 한 조합이라 측정상 SoA와 큰 차이가 나지 않을 수 있다. Archetype 저장 모델의 이득은 *여러 컴포넌트 조합이 섞인 월드* 에서 *특정 조합 chunk만 골라 처리* 할 때 나타나지만, 이 도구는 단일 조합이라 그 이득을 다루지 않는다. 다만 'SoA ≈ Archetype 이어야 정상'이라는 결론을 미리 정하지 않는다 — 같은 계산을 하고 측정 조건이 맞으면 예상 순서가 나오지 않아도 실험은 유효하다.

## 6. main의 다형성 통합과 Hud 활성화

main에서 `Simulation` 직접 인스턴스 대신 `unique_ptr<ISimulation>` 을 받는다. Layout 토글이 바뀌면 *인스턴스 자체* 를 새로 만든다.

### main.cpp 변경

```cpp
#include "raylib.h"

#include "measure/frame_timer.h"
#include "sim/isimulation.h"
#include "ui/hud.h"
#include "ui/toggles.h"

#include <memory>

namespace {
constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;
constexpr int kHudH    = 140;
constexpr int kSimH    = kWindowH - kHudH;
}  // namespace

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(kWindowW, kWindowH, "parts_perf_lab");
    SetTargetFPS(0);

    ppl::Toggles toggles;
    ppl::Toggles prevToggles = toggles;

    auto sim = ppl::CreateSimulation(toggles.layout);
    sim->Init(toggles.EntityCount(), kWindowW, kSimH);

    ppl::FrameTimer timer;
    ppl::Hud hud;
    hud.Init(kWindowW, kWindowH, kHudH);
```

루프는 시뮬 update와 시뮬 draw를 측정한 뒤 샘플을 커밋하고 HUD를 그린다.

```cpp
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        timer.BeginUpdate();
        sim->Update(dt, toggles.simd, toggles.branchless);
        timer.EndUpdate();

        timer.BeginDraw();
        BeginDrawing();
        ClearBackground(Color{10, 10, 14, 255});
        sim->Draw();
        timer.EndDraw();
        timer.CommitFrame();
        hud.Draw(toggles, timer, sim->Count());
        EndDrawing();

        // 토글 변경 처리. 레이아웃이 바뀌면 시뮬 인스턴스 자체를 교체한다.
        if (ppl::RequiresRestart(prevToggles, toggles)) {
            const bool layoutChanged = (prevToggles.layout != toggles.layout);
            sim->Shutdown();
            if (layoutChanged) {
                sim = ppl::CreateSimulation(toggles.layout);
            }
            sim->Init(toggles.EntityCount(), kWindowW, kSimH);
        }
        prevToggles = toggles;
    }

    sim->Shutdown();
    CloseWindow();
    return 0;
}
```

핵심 변경은 셋이다.

1. `Simulation sim;` → `auto sim = CreateSimulation(layout);` (다형 포인터)
2. `sim.Update(...)` → `sim->Update(toggles.simd, toggles.branchless)` (인자 추가)
3. 재시작 시 *layout 변경* 이면 인스턴스 교체, 아니면 같은 인스턴스에 `Shutdown` + `Init`

### Hud 토글 활성화

기존 코드에서 layout 토글은 `GuiSetState(STATE_DISABLED)` 로 회색이었다. 그 두 줄을 빼고 라벨 색을 회색에서 밝은색으로 바꾼다.

```cpp
// src/ui/hud.cpp 의 layout 토글 부분

// 3행: 레이아웃 — 활성
DrawText("layout   :", x, rowY(row), 14, LIGHTGRAY);
int layoutIdx = (int)toggles.layout;
if (GuiDropdownBox({(float)(x + kLabelW), (float)rowY(row),
                    (float)kFieldW, (float)kRowH},
                   kLayoutOptions, &layoutIdx, layoutEdit_)) {
    layoutEdit_ = !layoutEdit_;
}
if (!layoutEdit_) {
    toggles.layout = (Layout)std::clamp(layoutIdx, 0, kLayoutCount - 1);
}
```

`STATE_DISABLED` ↔ `STATE_NORMAL` 두 호출이 빠지고 라벨 색이 `GRAY` → `LIGHTGRAY` 로 바뀐 게 전부다.

## 7. 빌드와 실행 결과

CMake 재구성, 빌드, 실행한다.

```bash
cmake -S . -B build
cmake --build build --config Release
```

실행하면 layout 콤보박스가 활성화되어 있다. 콤보를 클릭해 SoA → AoS → Archetype 순서로 바꿔 본다.

### 측정 시점에서 보이는 것

10k 엔티티 기준 일반적인 데스크톱 CPU에서 보일 수 있는 경향은 다음과 같다.

- **SoA**: update ms가 가장 작음. 자동 벡터화가 가장 잘 동작.
- **AoS**: SoA 대비 update ms가 *클 수 있다*. 다만 컴파일러가 AoS도 잘 풀거나 워크로드가 작으면 비슷하게 나올 수 있어, 큰 차이를 *반드시 본다고 단정하지 않는다*.
- **Archetype**: SoA와 비슷하거나 다를 수 있음. 단일 조합 시뮬이라 차이가 작을 수 있지만, *작아야 정답* 이라는 기준은 두지 않는다.

콤보박스 클릭 직후 그래프에 *세로 절벽* 이 그려진다. 직전 layout의 ms와 직후 layout의 ms가 한 줄로 이어지면서 차이가 시각적 사건이 된다.

100k 엔티티로 늘리면 절대 ms가 커지면서 *비율 차이* 가 더 잘 보인다. 1k에서는 차이가 측정 노이즈 안에 묻힐 수도 있다.

### 자동 검증: `parts_layout_tests`

ms를 비교하려면 세 레이아웃이 *같은 계산* 을 해야 한다. 그 전제를 `layout` 그룹이 두 방향에서 확인한다. 하나는 구현끼리의 동등성, 다른 하나는 구현과 무관한 절대 기준이다.

동등성 쪽은 SoA/Scalar/branched를 baseline으로 두고 나머지 조합을 같은 프레임 수만큼 돌려 비교한다. 초기 상태는 같은 시드·같은 좌표 상한에서 만들어지므로 모든 레이아웃이 수치적으로 같아야 한다. 검사는 허용 오차 0의 `NearStates` 로 하며, 이는 `+0`/`-0` 를 구분하지 않는 수치 동등이지 비트 동등이 아니다. 180프레임 뒤의 상태는 경로별 허용 오차 안에 들어야 한다.

<!-- snippet: tests/core_tests.cpp#TestLayoutEquivalence -->
```cpp
// tests/core_tests.cpp — layout 그룹

void TestLayoutEquivalence() {
    constexpr int   kFrames = 180;
    constexpr float kDt     = 1.0f / 120.0f;

    // baseline: SoA / Scalar / branched.
    std::vector<float> baselineInit, baseline;
    {
        auto sim = MakeAndInit(ppl::Layout::SoA);
        sim->CopyState(baselineInit);
        for (int i = 0; i < kFrames; ++i) {
            sim->Update(kDt, ppl::SimdWidth::Scalar, false);
        }
        sim->CopyState(baseline);
    }
    CHECK(baseline.size() == kEntities * 4);

    for (const Combo& c : AllCombos()) {
        const std::string label = ComboName(c);
        auto sim = MakeAndInit(c.layout);
        CHECK_AT(sim->Count() == kEntities, label);

        // 같은 시드·같은 좌표 상한이라 초기 상태는 수치적으로 같다(비트 동등이 아니다).
        std::vector<float> state;
        sim->CopyState(state);
        CHECK_AT(NearStates(state, baselineInit, 0.0f), label);

        for (int i = 0; i < kFrames; ++i) {
            sim->Update(kDt, c.simd, c.branchless);
        }
        sim->CopyState(state);
        CHECK_AT(NearStates(state, baseline, ToleranceFor(c.simd, kFrames)), label);
    }
}
```

절대 기준 쪽이 앞에서 말한 *동등성만으로 부족한 지점* 을 메운다. 실패는 조합 라벨(`AoS/SSE/branchless` 같은)과 함께 찍히므로 어느 경로가 깨졌는지 바로 나온다.

<!-- snippet: tests/core_tests.cpp#TestBoundaryInvariants -->
```cpp
// tests/core_tests.cpp — layout 그룹

void TestBoundaryInvariants() {
    // ...
    // (a) 갱신 후 모든 엔티티가 뷰포트 안에 있다.
    if (!(px >= 0.0f && px <= maxX && py >= 0.0f && py <= maxY)) {
        ++boundsViolations;
    }
    // (b) 반사는 부호만 뒤집으므로 |v|는 성분별로 보존된다.
    if (std::fabs(std::fabs(vx) - absV0[i * 2 + 0]) > 1e-4f ||
        std::fabs(std::fabs(vy) - absV0[i * 2 + 1]) > 1e-4f) {
        ++speedViolations;
    }
    // (c) 이번 프레임에 속도 부호가 뒤집혔는가.
    if (prev[i * 4 + 2] * vx < 0.0f) flippedX[i] = 1;
    if (prev[i * 4 + 3] * vy < 0.0f) flippedY[i] = 1;
    // ...
    CHECK_AT(boundsViolations == 0, label);
    CHECK_AT(speedViolations == 0, label);
    // ...
    // 좁은 뷰포트 + 큰 dt에서는 대다수가 실제로 반사해야 한다.
    CHECK_AT(nx >= kEntities / 2, label);
    CHECK_AT(ny >= kEntities / 2, label);
    // 잔여(count % 4) 처리 구간의 반사가 사라지면 여기서 걸린다.
    CHECK_AT(flippedX[kEntities - 1] == 1, label);
    CHECK_AT(flippedY[kEntities - 1] == 1, label);
}
```

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R parts_layout_tests --output-on-failure

./build/parts_core_tests layout              # Linux/macOS
.\build\Release\parts_core_tests.exe layout  # Windows
```

```text
parts_core_tests: group `layout`
  layout/LayoutEquivalence ... 55 check(s) ok
  layout/BoundaryInvariants ... 180 check(s) ok
  layout/InputContracts ... 13 check(s) ok
layout: 3 test(s), 248 check(s), 0 failure(s)
```

단언 수가 조합 수에 비례한다. 위 출력은 `-DPERF_LAB_ENABLE_AVX2=ON` 으로 구성한 18조합 빌드의 예로, `{SoA, AoS, Archetype} x {C++ Auto, SSE, AVX2} x {branched, branchless}` 다. 기본 빌드는 `{C++ Auto, SSE}` 12조합이라 `LayoutEquivalence` 37, `BoundaryInvariants` 120 이 나온다. `InputContracts` 는 조합 수와 무관하게 13개다. 합계는 기본 `170`(`37+120+13`), AVX2 빌드 `248`(`55+180+13`)이다. 수는 실행 통계일 뿐 검증 강도의 증거가 아니므로, 어떤 불변식을 검사했는지는 테스트 이름과 입력으로 확인한다.

### 검증 체크리스트

- layout 변경 시 화면이 깜박이지만 시뮬 패턴이 *같아 보임* → 결정론적 시드(`0xC0FFEE`)가 모든 layout에서 같으니 정상
- AoS가 SoA보다 *느리지 않음* → 컴파일러가 AoS도 잘 최적화했거나 데이터 크기가 아직 캐시 안에 머무는 케이스. `-O3` 또는 `/O2` 빌드에서 충분히 일어날 수 있다. 더 큰 차이를 보려면 entities를 100k로 늘려 캐시 한계를 넘긴다. `-O0` 빌드는 디버그 참고용이지 성능 판단 기준으로 쓰지 않는다
- Archetype이 SoA보다 *느림* → chunk 수가 너무 많아서 외부 루프 오버헤드가 보이는 경우. 100k에서는 약 391 chunk라 차이가 있을 수 있음
- 시뮬이 멈춘 것처럼 보임 → 재시작 직후 `GetFrameTime()` 이 비정상적으로 커서 첫 프레임에 모든 점이 경계로 몰린 경우. 갱신 루프가 매 프레임 `[0, w-1] x [0, h-1]` 로 clamp하므로 점이 화면 밖으로 나가지는 않는다

### 비교 측정의 정직성

이 도구가 보여 주는 차이는 *컴파일러 최적화* 와 *CPU 마이크로아키텍처* 의 결합 결과다. 같은 코드라도:

- GCC vs Clang vs MSVC에 따라 자동 벡터화 결과가 다름
- Skylake vs Zen 4의 캐시 계층이 다름
- L2/L3 크기에 따라 100k에서 다른 절벽이 보일 수 있음

도구는 *재현 환경* 만 제공한다. 절대값은 머신마다 다르고, 비율도 변한다. 결론을 일반화하지 않고 *본인 머신에서 측정* 한 결과로 다룬다.

---

## 결론

이 단계의 결과물은 layout 토글 한 번에 시뮬 인스턴스가 교체되고 그 직후 ms 차이가 그래프에 *세로 절벽* 으로 그려지는 도구다. 약 350줄의 새 코드(ISimulation + 3 layout + factory + main 변경)가 기존 코드 위에 얹혔다.

세 가지 결정이 이 단계의 토대를 만들었다.

1. **추상 인터페이스 + 팩토리** — vtable dispatch 한 번이 핫 루프 밖에 있어 dispatch 비용은 무시할 수 있다. *데이터 배치 차이* 만 측정에 남는다.
2. **인터페이스 시그니처를 넓게 잡아 둠** — 이 단계의 스냅숏에서는 `Update(dt, simd, branchless)` 가 simd/branchless를 받기만 하지만, 처리 경로가 늘어나도 시그니처를 바꾸지 않고 분기만 추가하면 된다.
3. **Archetype의 저장 범위를 *명시*** — 단일 조합 시뮬이라 SoA와 차이가 작을 수 있지만, 그 순서를 정답으로 고정하지 않는다. 차이가 나지 않는 *것* 도 측정 도구가 알려 주는 정보다.

여기까지의 코드는 자동 벡터화에만 의존한다. 컴파일러가 잘 처리하는 부분과 못 처리하는 부분이 섞여 있어 차이가 *예측 가능하지 않다*. 핫 루프를 SSE/AVX2 명령으로 직접 짠 경로가 없어, 지금 측정된 차이 중 어디까지가 레이아웃 때문이고 어디까지가 컴파일러의 벡터화 판단 때문인지 분리할 수 없다.

## 더 읽을거리

- Mike Acton, "Data-Oriented Design and C++" (CppCon 2014) — SoA/AoS 의 게임 엔진 적용
- Michele Caini (skypjack), "ECS back and forth" — sparse set 대 archetype 등 ECS 저장 모델 비교 시리즈
- [Unity Entities Components System](https://docs.unity3d.com/Packages/com.unity.entities@latest) — ECS 저장 배치의 개념 비교 자료
- [Bevy ECS — 공식 Getting Started 가이드](https://bevy.org/learn/quick-start/getting-started/ecs/) — archetype 기반 컴포넌트 저장의 개념 자료. 이 lab의 고정 256개 `Chunk`와 동일 구현이라는 근거는 아니다
- Stoyan Nikolov, "OOP is Dead, Long Live Data-Oriented Design" (CppCon 2018)
- [Andrei Alexandrescu, "Speed is found in the minds of people"](https://www.youtube.com/watch?v=FJJTYQYB1JQ) (CppCon 2019) — 자동 벡터화의 한계
- [Compiler Explorer (godbolt.org)](https://godbolt.org/) — 자동 벡터화 결과를 어셈블리로 직접 확인
