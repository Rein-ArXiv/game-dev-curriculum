## 들어가며

이 글은 저레벨 성능 측정 도구를 처음부터 만들어 가는 작업의 첫 단계다. 같은 워크로드(1만 점이 화면 안을 떠다니는 시뮬레이션)를 고정해 두고, 그 위에서 *메모리 할당자, 데이터 레이아웃, SIMD 폭, branchless predication* 같은 저레벨 기법을 런타임으로 토글하면서 차이가 어떻게 갈라지는지를 측정·시각화한다. 본문에서 "Pool 할당자가 malloc 대비 N배 빠르다", "SoA가 AoS 대비 캐시 미스를 M% 줄인다" 같은 주장을 본인 머신에서 직접 재현 가능한 환경을 만드는 게 목표다.

> 이 실습은 게임 개발의 모든 CS 주제를 하나의 프로그램에 구현하는 과정이 아니다. 같은 상태를 여러 메모리 배치와 연산 방식으로 처리하고, 할당·순회 비용을 나누어 관찰하는 저수준 성능 실습이다. ECS 는 단일 컴포넌트 조합의 저장 배치까지만 다룬다. query 와 구조 변경, 문자열·인코딩·동시성 등은 별도 실습에서 다룬다.
>
> 실행 결과가 특정 순서로 나와야 성공하는 실습은 아니다. 먼저 같은 입력에 대해 같은 의미의 결과를 계산하는지 확인한다. 그다음 작업량·측정 구간·실행 환경을 맞추고 반복 측정한다. 기대와 다른 성능이 나오면 결과를 숨기거나 구현 오류로 단정하지 않고, 이 실험이 어떤 가정을 충족하지 못했는지 확인한다.
>
> 각 장의 코드는 세 종류로 구분한다. `현재 단계 완성 코드`는 해당 단계의 파일 집합과 함께 빌드할 수 있어야 한다. `발췌 코드`는 생략된 선언과 helper 의 실제 위치를 함께 적는다. `설명용 의사코드`는 의미와 제어 흐름을 보여주며 그대로 붙여 넣는 소스가 아니다. 최종 `src/`와 중간 단계의 함수 시그니처가 다르면 변환 표를 제공한다.

이 단계의 결과물은 다음 다섯 가지를 갖춘 *최소 골격* 이다.

1. raylib 윈도우와 메인 루프
2. SoA(Structure of Arrays) 레이아웃의 1만 점 시뮬레이션
3. update와 draw를 분리 측정하는 프레임 타이머
4. 텍스트 HUD에 ms와 fps 표시
5. CMake 한 번으로 빌드 가능한 프로젝트 구조

여기에는 raygui 토글, 프레임 그래프, 레이아웃 변종(AoS/Archetype), SIMD 경로, 할당자 비교, perf 카운터가 아직 없다. 이 단계는 *측정 인프라의 토대* 만 깔고, 그 위에 1만 점이 매끄럽게 떠다니는 동작 가능한 코드를 만든다.

---

## 목차
1. [도구의 성격: 게임이 아니라 실험실](#1-도구의-성격-게임이-아니라-실험실)
2. [기술 스택 선택: raylib + CMake](#2-기술-스택-선택-raylib--cmake)
3. [프로젝트 구조와 CMakeLists.txt](#3-프로젝트-구조와-cmakeliststxt)
4. [SoA 시뮬레이션 클래스](#4-soa-시뮬레이션-클래스)
5. [프레임 타이머](#5-프레임-타이머)
6. [main의 메인 루프](#6-main의-메인-루프)
7. [빌드와 실행 결과](#7-빌드와-실행-결과)

---

## 1. 도구의 성격: 게임이 아니라 실험실

이 도구는 게임이 아니다. *재미있는 결과물* 이 아니라 *증거* 다. 1만 점이 화면을 떠다니는 비주얼은 측정의 부산물이고, 본질은 다음 셋이다.

- **시리즈 글의 주장을 재현 가능하게 만든다**. 글이 *주장* 이라면 이 도구는 *재현 환경* 이다.
- **거대 프로젝트의 설계 결정에 데이터 근거를 제공한다**. 엔진 코어가 Pool 할당자를 채택할 때, "이 lab에서 측정한 차이"가 그 결정을 뒷받침한다.
- **동일 워크로드의 여러 구현을 동시 비교한다**. 시뮬은 *고정* 되고 *구현* 만 토글된다.

핵심은 워크로드의 *고정성* 이다. 같은 N개 엔티티, 같은 결정론적 시드, 같은 화면 경계 반사 규칙. 차이는 데이터 배치와 처리 경로에서만 나온다.

## 2. 기술 스택 선택: raylib + CMake

이 도구가 시각화에 요구하는 건 단순하다.

- 1만 점 그리기
- 텍스트 HUD
- 프레임 그래프 한 줄(현재 미구현)
- 토글 패널(현재 미구현)

풀 기능 게임 엔진이 필요 없다. 그래픽 API를 직접 다루기에는 부수 코드(컨텍스트, 셰이더, 정점 버퍼)가 측정 코드를 가리고, 큰 프레임워크는 의존성과 학습 부담이 크다. 그 사이의 균형점이 raylib이다.

| 항목 | raylib |
|---|---|
| API | C, 즉시 모드 (`DrawPixel`, `DrawText`) |
| 라이선스 | zlib/libpng |
| 코드 규모 | 약 30,000 LOC |
| 의존성 | 단일 라이브러리, 시스템 설치 불필요 |
| 크로스 플랫폼 | Windows / Linux / macOS / Web |
| CMake 통합 | `FetchContent_Declare` 한 번으로 끝 |

대안으로 SDL2 + 별도 그래픽 라이브러리, GLFW + ImGui only, Qt가 있지만 모두 초기 부담이 더 크다. 측정 코드가 그래픽 코드에 묻히지 않는 게 이 단계의 우선순위다.

빌드 시스템은 CMake로 한다. raylib을 `FetchContent`로 자동으로 가져와 시스템에 별도 설치 없이 빌드되도록 한다.

## 3. 프로젝트 구조와 CMakeLists.txt

이 단계의 디렉토리 구조는 다음과 같다.

```text
parts_perf_lab/
  CMakeLists.txt
  src/
    main.cpp
    sim/
      simulation.h
      simulation.cpp
    measure/
      frame_timer.h
      frame_timer.cpp
```

`sim/` 와 `measure/` 로 *데이터/시뮬* 와 *측정 인프라* 를 분리해 두었다. `alloc/`, `ui/` 같은 디렉토리도 같은 패턴으로 붙일 수 있는 구조다.

이 장의 파일 목록은 이 단계에서 함께 빌드하는 최소 집합이다. 최종 `src/` 트리에는 alloc/, ui/, 레이아웃 변종 등 이 장에 없는 파일이 더 있다. 본문 코드 블록 중 선언이 생략된 발췌는 주변 정의를 보충해야 컴파일되며, 생략 위치는 주석으로 밝힌다. 이 장의 테스트 출력은 실제 로그가 아니라 문서 명령으로 얻을 수 있는 예상 출력이다.

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(parts_perf_lab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# raylib을 FetchContent로 가져온다. 시스템 설치 불필요.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/sim/simulation.cpp
    src/measure/frame_timer.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
```

`SUPPORT_MODULE_RAUDIO OFF`는 raylib의 오디오 모듈을 빼려는 의도의 옵션이다. 다만 raylib 5.0은 이 옵션을 `CUSTOMIZE_BUILD=ON`일 때만 읽으므로 실제로는 `raudio.c`가 그대로 빌드된다 — 이 도구가 소리를 내지 않는다는 의도 표기로만 남아 있다.

## 4. SoA 시뮬레이션 클래스

워크로드는 단순하다. *N개의 점이 임의 초기 속도로 출발해 화면 경계에서 반사하며 떠다닌다*. 충돌 검사 같은 복잡한 단계는 없다. 단순함이 측정의 정직성을 담보한다.

이 단계는 *SoA(Structure of Arrays) 레이아웃* 한 가지만 구현한다. 컴포넌트(`posX`, `posY`, `velX`, `velY`)별로 별도의 `std::vector<float>` 를 둔다. 캐시 친화적이고 컴파일러가 자동 벡터화하기 가장 쉬운 형태다.

레이아웃이 SoA 하나뿐이라 지금은 레이아웃을 바꿨을 때의 차이를 측정할 수단이 없다.

### simulation.h

```cpp
#pragma once

#include <cstddef>
#include <vector>

namespace ppl {

class Simulation {
public:
    void Init(std::size_t entityCount, int viewportW, int viewportH);
    void Update(float dt);
    void Draw() const;
    void Shutdown();

    std::size_t Count() const { return count_; }

private:
    std::vector<float> posX_;
    std::vector<float> posY_;
    std::vector<float> velX_;
    std::vector<float> velY_;
    std::size_t count_ = 0;
    int viewportW_ = 0;
    int viewportH_ = 0;
};

}  // namespace ppl
```

`namespace ppl` 은 `parts_perf_lab` 의 약어로 통일한다. 이 도구의 모든 클래스가 같은 네임스페이스 안에 들어간다.

### simulation.cpp

초기화는 고정 시드를 쓴다. seed를 고정하면 동일 표준 라이브러리와 빌드 환경에서 같은 초기 상태를 만들기 쉽다. 다만 난수 엔진의 정수 수열과 실수 분포의 매핑은 별개의 요소이므로, "누가 어느 환경에서 돌려도 비트까지 같다"는 보장은 아니다. 서로 다른 표준 라이브러리에서도 동일 입력을 요구하는 실험에는 생성된 초기 상태를 파일로 저장하고 그 해시를 결과에 남긴다.

이 실습의 유효 좌표는 `0 ≤ x ≤ W-1`, `0 ≤ y ≤ H-1`로 정한다. W와 H는 2 이상이다. Init과 Update가 같은 상한을 써야 하며, 유효 상태에서 `dt=0`인 업데이트는 상태를 바꾸지 않아야 한다. 화면 픽셀 범위(정수 절삭 후)와 물리 좌표 계약은 구분한다.

```cpp
#include "sim/simulation.h"

#include "raylib.h"

#include <algorithm>
#include <random>

namespace ppl {

void Simulation::Init(std::size_t entityCount, int viewportW, int viewportH) {
    count_ = entityCount;
    viewportW_ = viewportW;
    viewportH_ = viewportH;

    posX_.resize(count_);
    posY_.resize(count_);
    velX_.resize(count_);
    velY_.resize(count_);

    // 좌표 계약: 0 <= x <= W-1, 0 <= y <= H-1. Init과 Update가 같은 상한을 쓴다.
    // 고정 시드. 동일 표준 라이브러리·빌드에서 같은 초기 상태를 만들기 쉽게 한다.
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> distX(0.0f,
                                                 static_cast<float>(viewportW_ - 1));
    std::uniform_real_distribution<float> distY(0.0f,
                                                 static_cast<float>(viewportH_ - 1));
    std::uniform_real_distribution<float> distV(-120.0f, 120.0f);

    for (std::size_t i = 0; i < count_; ++i) {
        posX_[i] = distX(rng);
        posY_[i] = distY(rng);
        velX_[i] = distV(rng);
        velY_[i] = distV(rng);
    }
}
```

`0xC0FFEEu` 시드는 임의의 32비트 상수다. 다른 값을 골라도 무방하지만 *고정* 이 핵심이다. 속도 범위 ±120 px/s는 1280×720 화면에서 한쪽 벽에서 다른 쪽까지 약 5~10초가 걸리는 보기 좋은 속도다. viewport는 W, H 각각 2 이상을 계약으로 두고, 입력이 그보다 작으면 2로 올린다. dt는 음이 아닌 유한값만 유효하며, NaN·무한·음수 dt는 상태를 바꾸지 않도록 0으로 취급한다.

업데이트 루프가 SoA의 핵심이다.

```cpp
void Simulation::Update(float dt) {
    const float w = static_cast<float>(viewportW_);
    const float h = static_cast<float>(viewportH_);

    // SoA 핫 루프. 컴파일러가 자동 벡터화하기 가장 쉬운 형태다.
    for (std::size_t i = 0; i < count_; ++i) {
        posX_[i] += velX_[i] * dt;
        posY_[i] += velY_[i] * dt;

        // 화면 경계 반사. 분기 두 개가 데이터에 의존하므로 분기 예측에 영향을 받는다.
        // clamp 상한은 w-1, h-1: DrawPixel(width, y)는 화면 밖이라 clip된다.
        if (posX_[i] < 0.0f || posX_[i] > w - 1.0f) {
            velX_[i] = -velX_[i];
            posX_[i] = std::clamp(posX_[i], 0.0f, w - 1.0f);
        }
        if (posY_[i] < 0.0f || posY_[i] > h - 1.0f) {
            velY_[i] = -velY_[i];
            posY_[i] = std::clamp(posY_[i], 0.0f, h - 1.0f);
        }
    }
}
```

`posX_[i] += velX_[i] * dt` 같은 순수한 SoA 연산은 컴파일러가 SSE/AVX2로 자동 벡터화하기 좋다. 반면 경계 반사의 두 분기는 *데이터 의존* 이라 자동 벡터화를 어렵게 만들 수 있다. 이 비대칭이 실제로 얼마나 비용을 만드는지는 이 장에서 측정하지 않으며, 관찰되는 시간 차이만으로 원인을 확정하지 않는다.

경계 처리는 한 step의 위치를 계산한 뒤 영역 밖이면 속도 부호를 한 번 바꾸고 위치를 경계에 고정하는 *시각적 성능 실험용 이산 경계 규칙* 이다. 큰 dt 동안 벽을 여러 번 통과하는 운동을 정확히 재현하는 물리 해법이 아니다. 이 과정은 서로 다른 메모리 배치가 같은 이산 규칙을 구현하는지 비교한다. 물리적으로 정확한 다중 반사가 필요하면 별도의 참조 모델을 추가한다.

```cpp
void Simulation::Draw() const {
    // 한 점당 한 픽셀. 1만 점이면 화면이 노이즈처럼 흩뿌려진 모양이 된다.
    for (std::size_t i = 0; i < count_; ++i) {
        DrawPixel(static_cast<int>(posX_[i]),
                  static_cast<int>(posY_[i]),
                  WHITE);
    }
}

void Simulation::Shutdown() {
    posX_.clear();
    posY_.clear();
    velX_.clear();
    velY_.clear();
    count_ = 0;
}

}  // namespace ppl
```

`DrawPixel`은 raylib의 즉시 모드 호출이다. 1만 호출이 매 프레임 발생하므로 draw 비용이 결코 무시할 수준이 아니다. 프레임 타이머가 *update와 draw를 분리 측정* 하는 이유가 여기에 있다.

## 5. 프레임 타이머

측정의 신뢰는 *어디서 시간이 가는가* 를 정확히 분리하는 데서 시작한다. update와 draw를 따로 재고, 그 둘을 합친 부분합과 프레임 전체(`frame wall`)도 각각 따로 잰다.

### frame_timer.h

```cpp
#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace ppl {

// 최근 N 프레임의 frame/sim/draw-submit/alloc/container 시간을 슬라이딩 윈도우로
// 보관한다. HUD가 이 데이터를 그래프로 표시한다.
//
// 구간 정의(서로 다른 지표를 같은 이름으로 부르지 않는다):
//   - frame wall      : BeginFrame()부터 EndFrame()까지. HUD와 EndDrawing(flush/swap)을
//                       포함하지만 GPU 실행 시간이나 전체 프로그램 CPU 시간은 아니다.
//   - sim             : 시뮬레이션 Update 호출 구간.
//   - draw-submit     : BeginDrawing/Clear/점 그리기 호출 구간. HUD와 EndDrawing 제외.
//                       일부 CPU 렌더 명령 발행 비용이며 GPU 완료 시간이 아니다.
//   - alloc/container : 각 벤치마크가 자체 측정해 CommitFrame으로 넘기는 외부 값.
class FrameTimer {
public:
    static constexpr std::size_t kCapacity = 600;

    // 프레임 전체 wall 구간. BeginFrame은 루프 선두, EndFrame은 EndDrawing 뒤에 둔다.
    void BeginFrame();
    void EndFrame();

    void BeginUpdate();
    void EndUpdate();

    void BeginDraw();
    void EndDraw();

    // 호출 직전에 EndFrame, BeginUpdate/EndUpdate, BeginDraw/EndDraw가 모두
    // 호출되었다고 가정한다. 한 프레임의 측정값과 별도 채널 ms를 링 버퍼에 push한다.
    void CommitFrame(float allocMs = 0.0f, float containerMs = 0.0f);

    // 최근 N 프레임 ms 배열. ring buffer라 head 위치도 함께 반환한다.
    const std::array<float, kCapacity>& FrameWallMs() const { return frameWallMs_; }
    const std::array<float, kCapacity>& UpdateMs()    const { return updateMs_; }
    const std::array<float, kCapacity>& DrawMs()      const { return drawMs_; }
    const std::array<float, kCapacity>& AllocMs()     const { return allocMs_; }
    const std::array<float, kCapacity>& ContainerMs() const { return containerMs_; }
    std::size_t Head()   const { return head_; }
    std::size_t Filled() const { return filled_; }

    float LastFrameWallMs()      const { return lastFrameWallMs_; }
    float LastUpdateMs()         const { return lastUpdateMs_; }
    float LastDrawSubmitMs()     const { return lastDrawMs_; }
    // sim + draw-submit. 전체 CPU 프레임이 아니므로 옛 이름(LastTotalMs)을 쓰지 않는다.
    float LastSimDrawSubmitMs()  const { return lastUpdateMs_ + lastDrawMs_; }
    float LastAllocMs()          const { return lastAllocMs_; }
    float LastContainerMs()      const { return lastContainerMs_; }

private:
    using Clock = std::chrono::steady_clock;
    using Time  = Clock::time_point;

    Time frameStart_{};
    Time updateStart_{};
    Time drawStart_{};

    std::array<float, kCapacity> frameWallMs_{};
    std::array<float, kCapacity> updateMs_{};
    std::array<float, kCapacity> drawMs_{};
    std::array<float, kCapacity> allocMs_{};
    std::array<float, kCapacity> containerMs_{};
    std::size_t head_ = 0;
    std::size_t filled_ = 0;

    float lastFrameWallMs_ = 0.0f;
    float lastUpdateMs_    = 0.0f;
    float lastDrawMs_      = 0.0f;
    float lastAllocMs_     = 0.0f;
    float lastContainerMs_ = 0.0f;
};

}  // namespace ppl
```

`std::chrono::steady_clock` 은 단조 증가 시계라 시계가 *뒤로 갈* 위험이 없다. 시스템 시각이 동기화로 점프해도 영향을 받지 않는다. 측정용으로 적합한 시계다.

`kCapacity = 600`은 60fps 기준 10초의 슬라이딩 윈도우다. 이 단계에서는 alloc/container 채널을 넘기지 않으므로 `CommitFrame()` 을 인자 없이 불러 두 채널은 0으로 남는다. 누적된 윈도우를 그림으로 보여 주는 경로는 아직 없다.

### frame_timer.cpp

```cpp
#include "measure/frame_timer.h"

namespace ppl {

void FrameTimer::BeginFrame() {
    frameStart_ = Clock::now();
}

void FrameTimer::EndFrame() {
    const auto end = Clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - frameStart_).count();
    lastFrameWallMs_ = static_cast<float>(dur) / 1.0e6f;
}

void FrameTimer::BeginUpdate() {
    updateStart_ = Clock::now();
}

void FrameTimer::EndUpdate() {
    const auto end = Clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - updateStart_).count();
    lastUpdateMs_ = static_cast<float>(dur) / 1.0e6f;
}

void FrameTimer::BeginDraw() {
    drawStart_ = Clock::now();
}

void FrameTimer::EndDraw() {
    const auto end = Clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - drawStart_).count();
    lastDrawMs_ = static_cast<float>(dur) / 1.0e6f;
}

void FrameTimer::CommitFrame(float allocMs, float containerMs) {
    lastAllocMs_     = allocMs;
    lastContainerMs_ = containerMs;

    frameWallMs_[head_] = lastFrameWallMs_;
    updateMs_[head_]    = lastUpdateMs_;
    drawMs_[head_]      = lastDrawMs_;
    allocMs_[head_]     = lastAllocMs_;
    containerMs_[head_] = lastContainerMs_;
    head_ = (head_ + 1) % kCapacity;
    if (filled_ < kCapacity) {
        ++filled_;
    }
}

}  // namespace ppl
```

`duration_cast<nanoseconds>` 후에 `1.0e6f` 로 나눠 ms로 환산한다. 직접 `duration<float, std::milli>` 를 써도 되지만 명시적인 정수 ns가 디버그할 때 더 직관적이다.

### 측정 구간의 정의

지표마다 포함하는 구간과 제외하는 구간을 이름에 드러낸다. 같은 시간 값을 서로 다른 이름으로 부르지 않는다.

| 이름 | 포함하는 구간 | 포함하지 않는 것 |
|---|---|---|
| sim_update_wall | 시뮬레이션 Update 호출 | allocator, container, Draw, HUD |
| alloc_workload_wall | 지정된 할당 trace | 시뮬레이션·다른 workload |
| container_walk_wall | 지정된 컨테이너 순회 | 초기 생성/해제 — 별도 지표가 없는 한 |
| draw_submit_wall | 현재 BeginDrawing/Clear/점 그리기 호출 구간 | HUD 와 EndDrawing, 별도 GPU 실행 시간 |
| end_drawing_wall | EndDrawing 의 CPU 측 경과 시간 | 순수한 GPU 시간이라고 해석할 수 없음 |
| frame_wall | 프레임 시작부터 모든 호출 완료까지 | 전체 프로그램 CPU 사용량/GPU 시간과 동일하지 않음 |

`LastSimDrawSubmitMs()` 는 sim과 draw-submit을 더한 부분합이다. 이를 전체 CPU 프레임 시간이나 CPU FPS 로 부르면 allocator·container·HUD·EndDrawing처럼 제외된 작업을 오해하기 쉽다. 그래서 `sim + draw-submit` 이라고 표시하고, 전체 프레임 경과 시간은 별도의 `frame wall` 로 잰다. raylib 5.0 의 EndDrawing 에는 렌더 배치 처리와 buffer swap 등이 있으므로, EndDrawing 앞에서 끝난 타이머가 그 비용까지 측정했다고 설명하지 않는다.

## 6. main의 메인 루프

raylib 윈도우와 메인 루프를 묶는다. 화면 하단 120px는 HUD 영역으로 비워 두고, 시뮬은 그 위쪽에서 그린다.

```cpp
// parts_perf_lab — 1만 점 ECS 시뮬레이션의 저레벨 성능 측정 도구.
#include "raylib.h"

#include "measure/frame_timer.h"
#include "sim/simulation.h"

#include <cstdio>

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;
constexpr int kHudH    = 120;
constexpr int kSimH    = kWindowH - kHudH;

void DrawHud(const ppl::FrameTimer& timer, std::size_t entityCount) {
    DrawRectangle(0, kSimH, kWindowW, kHudH, Color{20, 20, 28, 255});
    DrawLine(0, kSimH, kWindowW, kSimH, Color{80, 80, 100, 255});

    // sim + draw-submit은 부분합이다. allocator·container·HUD·EndDrawing은
    // 포함되지 않으므로 그 역수를 "cpu fps"라고 부르지 않는다.
    // 실제 화면 fps는 GetFPS로 따로 표시한다.
    const int actualFps = GetFPS();

    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "entities   : %zu\n"
                  "frame wall : %6.3f ms\n"
                  "sim        : %6.3f ms\n"
                  "draw-submit: %6.3f ms\n"
                  "sim+submit : %6.3f ms\n"
                  "actual fps : %5d",
                  entityCount,
                  timer.LastFrameWallMs(),
                  timer.LastUpdateMs(),
                  timer.LastDrawSubmitMs(),
                  timer.LastSimDrawSubmitMs(),
                  actualFps);
    DrawText(buf, 16, kSimH + 12, 18, RAYWHITE);
}

}  // namespace
```

main은 초기화 후 update/draw 측정 구간을 분리해서 돈다.

```cpp
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(kWindowW, kWindowH, "parts_perf_lab");
    SetTargetFPS(0);  // raylib 내부 FPS 제한을 끈다. VSync는 별도로 요청하지 않는다.

    ppl::Simulation sim;
    sim.Init(/*entityCount=*/10000, kWindowW, kSimH);

    ppl::FrameTimer timer;

    while (!WindowShouldClose()) {
        // frame wall은 루프 선두부터 EndDrawing(HUD·flush·swap 포함) 뒤까지다.
        timer.BeginFrame();

        const float dt = GetFrameTime();

        timer.BeginUpdate();
        sim.Update(dt);
        timer.EndUpdate();

        timer.BeginDraw();
        BeginDrawing();
        ClearBackground(Color{10, 10, 14, 255});
        sim.Draw();
        timer.EndDraw();
        // HUD는 EndDrawing 전에 그린다. frame wall 커밋은 EndDrawing 뒤에 하므로
        // HUD의 frame wall은 직전 프레임 값이다.
        DrawHud(timer, sim.Count());
        EndDrawing();

        timer.EndFrame();
        timer.CommitFrame();
    }

    sim.Shutdown();
    CloseWindow();
    return 0;
}
```

`SetTargetFPS(0)` 은 *raylib 내부의 FPS 제한 로직* 을 끈다. raylib이 매 프레임 끝에 `WaitTime`으로 sleep을 걸어 목표 fps에 맞추는 동작을 비활성화한다는 뜻이다. VSync는 GLFW 컨텍스트 옵션이라 *별도* 인데, 이 도구는 `FLAG_VSYNC_HINT` 를 요청하지 않는데, raylib은 창을 만들 때 `glfwSwapInterval(0)` 으로 VSync를 끄고 그 플래그가 있을 때만 1로 올리므로 결과는 disabled 다(드라이버 설정이 강제로 켜는 것은 별개다). 측정이 모니터의 16.67ms 갭에 묶이지 않도록 하는 게 두 설정의 공통 목적이다.

Update와 Draw 사이에 *Begin/End 측정*이 정확히 한 쌍씩 들어가는 게 핵심이다. 프레임 전체는 BeginFrame/EndFrame으로 따로 감싸고, CommitFrame은 EndDrawing 뒤에 둔다. 측정 호출이 어긋나면 그래프가 잘못된 값을 그린다.

## 7. 빌드와 실행 결과

CMake로 한 번 구성하고, 빌드, 실행한다.

```bash
cmake -S . -B build
cmake --build build --config Release
./build/parts_perf_lab        # Linux/macOS
.\build\Release\parts_perf_lab.exe   # Windows
```

첫 빌드는 raylib 다운로드 + 컴파일 때문에 오래 걸린다. 아래 시간은 예시이며 네트워크·머신에 따라 달라진다(예: 첫 빌드 2~5분, 이후 30초 안쪽).

실행하면 1280×720 윈도우가 뜬다. 화면 위쪽 600px 영역에 1만 개의 흰 점이 임의 방향으로 떠다니고, 화면 경계에 닿으면 반사된다. 화면 하단 120px의 HUD에는 다음과 같은 텍스트가 매 프레임 갱신된다.

```text
entities   : 10000
frame wall :  1.412 ms
sim        :  0.087 ms
draw-submit:  1.243 ms
sim+submit :  1.330 ms
actual fps :   747
```

위 수치는 한 환경에서 관측될 수 있는 예시이며 머신마다 다르다. sim이 draw-submit보다 작게 나오는 경향이 있지만, 이 순서는 어느 머신에서나 성립하는 합격 기준이 아니다. 관찰되는 차이의 원인을 시간 값만으로 확정하지 않는다. 다만 그리기 비용을 sim 비용과 섞어 재면 시뮬 최적화의 효과가 묻히므로 구간을 나눠 기록한다. 같은 설정을 반복 비교하려면 가변 dt 대신 고정 fixture·고정 dt로 재생하는 benchmark 모드가 필요하다(**보충 구현 목표**).

### 자동 검증: `parts_skeleton_tests`

화면을 눈으로 보는 것과 별개로, 창을 열지 않고 도는 검증 타깃이 함께 빌드된다. `parts_core_tests` 는 raylib을 링크하지 않는 계산 코어 전용 실행 파일이고, 인자로 그룹 이름 하나를 받아 그 부분만 돈다. 여기서 만든 `FrameTimer` 의 그룹 이름은 `skeleton` 이고, CMake가 그것을 `parts_skeleton_tests` 라는 ctest 이름으로 등록한다.

무엇을 확인하는가는 검사 코드가 직접 말한다. `FrameTimer` 가 하는 일은 *update와 draw를 나눠 재는 것* 과 *그 값을 600칸 링 버퍼에 쌓는 것* 둘뿐이라, 검사도 그 둘이다.

출력 가능한 창을 만들었다는 것과 계산이 맞다는 것은 다르다. 알려진 초기 상태로 위치 갱신·속도 반전·clamp를 손으로 계산해 참조값을 만들고 구현 결과와 비교해야 한다. 임의 입력의 구현 간 일치 검사는 그다음이다. 테스트의 assertion 실행 횟수는 참고 통계이며, 어떤 불변식을 검증했는지 대신해 주지 않는다. 독립 참조 fixture와 의도적 오변형(mutation) 실패 로그를 붙이는 작업은 **보충 구현 목표** 다.

<!-- snippet: tests/core_tests.cpp#TestFrameTimerWindow -->
```cpp
// tests/core_tests.cpp — skeleton 그룹

void TestFrameTimerWindow() {
    ppl::FrameTimer timer;
    CHECK(timer.Head() == 0);
    CHECK(timer.Filled() == 0);
    CHECK(timer.LastSimDrawSubmitMs() == 0.0f);
    // ...
    // LastSimDrawSubmitMs는 두 값을 더해 돌려주는 파생 값이라 이 등식은 항상 참이다.
    CHECK(timer.LastSimDrawSubmitMs() ==
          timer.LastUpdateMs() + timer.LastDrawSubmitMs());
    // frame wall은 HUD와 EndDrawing을 포함하므로 sim+draw-submit보다 크거나 같다.
    CHECK(timer.LastFrameWallMs() >= timer.LastSimDrawSubmitMs());
    // ...
    // 용량을 다 채우면 head가 0으로 감기고 filled는 kCapacity에서 멈춘다.
    constexpr std::size_t kCap = ppl::FrameTimer::kCapacity;
    for (std::size_t i = 1; i < kCap; ++i) timer.CommitFrame();
    CHECK(timer.Head() == 0);
    CHECK(timer.Filled() == kCap);
    timer.CommitFrame();
    CHECK(timer.Head() == 1);
    CHECK(timer.Filled() == kCap);
}
```

`LastSimDrawSubmitMs()` 등식은 합산 경로가 바뀌지 않았는지만 고정한다 — 파생 값이라 항상 참이고, 측정 구간이 어긋났는지는 잡지 못한다. `frame wall >= sim+draw-submit` 검사는 프레임 전체 구간이 부분합을 감싼다는 관계만 확인한다. 링 버퍼 포화 검사는 `filled_` 가 `kCapacity` 를 넘어 자라지 않는다는 것 — 그래프가 읽을 구간이 배열 밖으로 나가지 않는 근거다.

빌드하고 그 그룹만 돌린다.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R parts_skeleton_tests --output-on-failure

# ctest가 부르는 것과 같은 실행. 그룹 이름을 인자로 준다.
./build/parts_core_tests skeleton              # Linux/macOS
.\build\Release\parts_core_tests.exe skeleton  # Windows
```

```text
parts_core_tests: group `skeleton`
  skeleton/FrameTimerWindow ... 17 check(s) ok
skeleton: 1 test(s), 17 check(s), 0 failure(s)
```

위 출력은 문서 명령으로 얻을 수 있는 예상 출력이다. `17 check(s)` 는 이 실행에서 단언이 몇 번 평가됐는지를 보여 주는 실행 통계일 뿐, 검증 강도의 지표가 아니다. 조건을 느슨하게 바꾸거나 핵심 검사를 다른 반복 검사로 대체하면 수가 같아도 검증은 약해질 수 있다. 실패하면 `ok` 자리가 `FAILED` 로 바뀌면서 어긋난 단언의 파일·줄 번호가 stderr에 찍힌다.

### 검증 체크리스트

증상마다 확인할 단계를 먼저 가른다. 빌드/링크 오류와 이미 실행 중인 프로그램의 입력 문제는 진단 단계가 다르다.

- configure 단계 에러 → CMake 버전, 경로(ASCII 권장), 네트워크 필요 여부 확인
- build/link 단계 에러 → `FetchContent` 는 `git` 이 필요하다. 링크 오류면 번역 단위 포함 여부나 정의 누락을 먼저 의심한다.
- startup 에러 → 창 생성·셰이더 초기화 실패는 GPU·드라이버 쪽 로그를 본다.
- 점들이 화면 밖으로 나가서 안 보임(실행 중) → 시드/속도 범위가 너무 큼, 또는 viewport 크기 인자 잘못 전달
- sim ms가 거의 0(실행 중) → 표시 반올림, clock 해상도, workload 길이, 실제 생성된 기계어를 차례로 확인한다. Release 빌드가 아니라는 이유만으로 단정하지 않는다.

---

## 결론

이 단계의 결과물은 1만 점이 화면을 떠다니고 frame wall·sim·draw-submit 시간이 분리 측정되어 텍스트로 표시되는 *측정 도구의 골격* 이다. 약 250줄의 코드가 raylib 윈도우, SoA 시뮬레이션 클래스, 프레임 타이머, 메인 루프, 텍스트 HUD를 모두 묶어 동작 가능한 단일 실행 파일로 정리됐다.

세 가지 결정이 이 단계의 토대를 만들었다.

1. **raylib + CMake FetchContent** — 시스템 설치 없이 한 번에 빌드되는 최소 의존성.
2. **SoA 한 가지 레이아웃 + 고정 시드** — 동일 표준 라이브러리·빌드에서 같은 초기 상태를 재현한다. 교차 환경 비교에는 버전 붙은 fixture와 해시가 필요하다.
3. **구간 분리 측정(frame wall / sim / draw-submit)** — 부분합을 전체 프레임으로 부르지 않고, 어디에 시간이 가는지 *수치로 갈라서 보는* 것이 측정 도구의 출발점.

여기까지의 코드는 토글이 없다. 시뮬은 한 경로로만 돈다. 같은 워크로드 위에서 *여러 구현을 비교* 하는 일은 토글 시스템도 그래프 표시도 없는 현재 상태로는 불가능하다.

## 더 읽을거리

- [raylib 공식 문서](https://www.raylib.com/cheatsheet/cheatsheet.html)
- [raylib examples](https://github.com/raysan5/raylib/tree/master/examples) — 즉시 모드 그리기 패턴 다수
- [CMake FetchContent 모듈](https://cmake.org/cmake/help/latest/module/FetchContent.html)
- Jonathan Boccara, "Strong Types for Strong Interfaces" (Meeting C++ 2017) — 측정용 코드의 단위(ms, ns) 안전 처리 패턴
- Bryce Adelstein Lelbach, "Benchmarking C++ Code" (CppCon 2015) — 측정의 함정과 정직한 비교
- [Google Benchmark](https://github.com/google/benchmark) — 자동화 비교 측정. 시각화는 빈약하지만 자동화에 강함
- [Tracy Profiler](https://github.com/wolfpld/tracy) — 시각화 풀 기능 프로파일러. 프로덕션 게임에서 표준
