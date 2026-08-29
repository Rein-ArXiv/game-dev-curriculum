# parts_perf_lab — parts 시리즈의 원리 측정·시각화

## 컨셉

이 lab은 **동일한 ECS 시뮬레이션을 여러 저레벨 구현으로 돌려놓고 차이를 측정**한다. 1만 개의 엔티티가 위치·속도를 가지고 매 프레임 업데이트되는 워크로드를 고정해두고, 그 위에서 할당자·메모리 레이아웃·컨테이너·SIMD 폭·branchless predication을 런타임에 토글한다. 토글이 바뀌는 순간 측정 그래프(frame/alloc/container 세 채널)가 갈라지는 모습을 그대로 본다.

## 짝 시리즈

- **parts** — 메모리 할당자 내부, 메모리 레이아웃, STL 컨테이너의 비용, SIMD, 분기 예측 같은 저레벨 성능 주제를 다루는 시리즈다. 이 lab이 측정으로 다루는 범위는 parts/03·04(할당자), parts/08·09(vector·intrusive list), parts/11·18(ECS·데이터 지향 레이아웃), parts/19(SIMD), parts/21(분기 예측) 여덟 편이다. 나머지 편은 이 lab의 측정 축에 대응하지 않는다.
- **algorithms** — 정렬·그래프·DP·압축·property 검증 같은 알고리즘 주제는 이 lab이 다루지 않는다. 그 축의 소유 lab은 `projects/mini/algorithms_lab`이다.

## 토글 항목

런타임 토글은 raygui 패널에서, 빌드 시 토글(예: AVX2 활성화)은 CMake 옵션으로 노출한다.

| 항목 | 옵션 | 비교 대상 | 현재 활성 |
|---|---|---|---|
| 엔티티 수 | 1k / 10k / 100k | 스케일링 곡선 비교 | ✓ |
| 메모리 레이아웃 | SoA / AoS / Archetype | parts/18 데이터 지향 설계, parts/11 ECS | ✓ |
| SIMD 폭 | 스칼라 / SSE / AVX2 | parts/19 SIMD | ✓ (SoA/Archetype만, AoS는 scalar 폴백) |
| branchless predication | on / off | parts/21 분기 예측 | ✓ (Scalar 경로에만 효과) |
| 할당자 | malloc / Arena / Pool / Slab | parts/03 메모리 할당자, parts/04 커스텀 할당자 | ✓ (EventBenchmark 워크로드에 적용) |
| 컨테이너 | vector / std::list / Intrusive list | parts/08 vector, parts/09 intrusive list | ✓ (ContainerBenchmark 워크로드에 적용) |

AVX2 경로는 빌드 시 `cmake -DPERF_LAB_ENABLE_AVX2=ON` 옵션이 필요하다. 옵션이 꺼진 상태에서 AVX2 토글을 선택하면 SSE 경로로 폴백한다. 비-x86 플랫폼(예: Apple Silicon)에서는 SSE/AVX2 토글 모두 scalar로 폴백한다.

여섯 축(엔티티 수·레이아웃·SIMD·branchless·할당자·컨테이너)이 모두 활성 상태다. allocator는 EventBenchmark(매 프레임 2000노드 alloc/free), container는 ContainerBenchmark(5000노드 순회)라는 별도 워크로드 채널에 연결되어 시뮬 측정과 분리된다.

## 출력

화면 상단은 시뮬 뷰포트, 하단은 측정 패널이다.

- 프레임타임 그래프 — frame(update+draw)·alloc·container 세 라인을 한 패널에 겹쳐 그림 (최근 600프레임 슬라이딩 윈도우, 60/30fps 기준선, 자동 스케일).
- 좌측 stats — entities, update/draw ms, cpu/actual fps, alloc ms·count·bytes, live/peak bytes, container ms·nodes·sum.
- 할당 통계 — `IAllocator` 내장 카운터(프레임당 호출 수·바이트, live/peak bytes).

미구현(외부 도구로 보완, articles/step08 참조): 캐시 미스·분기 미스 등 HW 카운터(perf/VTune), 메모리 단편화 시각화, GPU 시간 분리(RenderDoc 등).

## 측정 항목

- **프레임타임**: `std::chrono::steady_clock`으로 update/draw 구간 분리 샘플링.
- **alloc ms**: EventBenchmark가 매 프레임 노드 다발 alloc/free에 걸린 시간을 자체 측정.
- **container ms**: ContainerBenchmark가 N노드 순회에 걸린 시간을 자체 측정.
- **할당 횟수·바이트**: 커스텀 할당자(`IAllocator`)의 내장 카운터.

## 기술 스택

- C++20.
- raylib 5.0 (가벼운 2D 시각화).
- raygui 4.0 (토글 패널).
- CMake 3.20+.

## 디렉토리 구조

```text
parts_perf_lab/
  CMakeLists.txt
  src/
    main.cpp                   진입점, raylib 루프
    platform.h                 x86 SIMD 인트린식 사용 가능 여부 매크로 가드
    sim/                       isimulation.h 인터페이스 + 레이아웃 구현
      aos.{h,cpp}              AoS 업데이트 (계산 코어, raylib 비의존)
      soa.{h,cpp}              SoA 업데이트 (계산 코어, raylib 비의존)
      archetype.{h,cpp}        Archetype 업데이트 (계산 코어, raylib 비의존)
      {soa,aos,archetype}_draw.cpp   Draw() 전용 TU. 앱 타깃만 링크한다.
      factory.cpp
    alloc/                     iallocator.h + 4종 구현
      malloc_allocator.{h,cpp}
      arena_allocator.{h,cpp}
      pool_allocator.{h,cpp}
      slab_allocator.{h,cpp}
    measure/
      container_benchmark.{h,cpp}
      event_benchmark.{h,cpp}
      frame_timer.{h,cpp}
    ui/  hud.{h,cpp}  hud_layout.h  toggles.h  raygui_impl.cpp
  tests/
    core_tests.cpp             정렬 헬퍼·4종 할당자·컨테이너/이벤트 워크로드 계약·
                               레이아웃 x SIMD x branchless 결과 동등성·경계 반사 불변식
    headless_draw.cpp          Draw()의 빈 정의. 테스트 타깃이 raylib 없이 링크되게 한다.
  # raylib / raygui 는 FetchContent 로 build/_deps 아래 자동.
```

`tests/core_tests.cpp`는 `ISimulation::CopyState`로 각 구현의 상태를 스냅숏해 서로 비교한다. 이 순수 가상 함수는 렌더링 없이 시뮬 결과를 꺼내기 위한 검증용 인터페이스다.

`parts_core_tests` 타깃은 **raylib을 링크하지 않는다**. 각 레이아웃의 `Draw()`는 `src/sim/*_draw.cpp`로 분리되어 앱 타깃에만 들어가고, 테스트 타깃은 vtable을 채우기 위한 빈 정의(`tests/headless_draw.cpp`)만 링크한다. 그래서 GLFW·OpenGL 없이 계산 코어만 빌드해 검증할 수 있다.

검사 내용은 다음과 같다.

- 정렬 헬퍼와 4종 할당자의 정렬·크기 클래스·해제 계약, Pool 고갈 시 `nullptr` 반환과 반납 후 슬롯 재사용.
- 컨테이너 세 종류(vector / std::list / intrusive)의 **결과 동등성** — 같은 시드로 채운 노드 수가 정확히 같고 합산 결과가 순회 순서 차이만큼만 다르다.
- `EventBenchmark` 프레임 계약 — 4종 할당자 각각에서 프레임 종료 시 `LiveBytes() == 0`, `LastFrameAllocCount() == Target()`, Arena의 bump pointer가 0으로 되돌아온다.
- `{SoA, AoS, Archetype} x {Scalar, SSE} x {branchless off, on}` 12조합(AVX2 빌드에서는 18조합)이 모두 같은 결과를 낸다. 엔티티 수 513은 SIMD 잔여(513%4=1)와 Archetype 부분 chunk(2\*256+1)를 동시에 자극한다.
- 경계 반사 불변식 — 갱신 후 모든 엔티티가 뷰포트 안에 있고, `|velX|`·`|velY|`가 성분별로 보존되며, 좁은 뷰포트에서 엔티티마다 실제로 속도 부호가 뒤집힌다.

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release          # 기본 (SSE2까지)
cmake -S . -B build_avx2 -DPERF_LAB_ENABLE_AVX2=ON      # AVX2/FMA 경로 활성화
cmake --build build -j
./build/parts_perf_lab
```

자동 검증은 `parts_core_tests` 타깃이다.

```bash
cmake --build build --config Release --target parts_core_tests
ctest --test-dir build -C Release --output-on-failure
```

실행 파일은 하나지만 실행 단위는 주제별로 나뉘어 있다. `parts_core_tests` 는 `argv[1]` 을 그룹 이름으로 받아 그 그룹의 검사만 돌리고, CMake가 그룹마다 ctest 이름을 하나씩 등록한다. 전체를 도는 `parts_core_tests` 는 그대로 남는다.

| ctest 이름 | 그룹 인자 | 검사 대상 |
|---|---|---|
| `parts_skeleton_tests` | `skeleton` | `FrameTimer` 의 update/draw 분리와 링 버퍼 포화 |
| `parts_toggles_tests` | `toggles` | `RequiresRestart` 판정, 토글 패널 기하와 두 패스 그리기 |
| `parts_layout_tests` | `layout` | 세 레이아웃의 결과 동등성, 경계 반사 불변식 |
| `parts_simd_tests` | `simd` | SIMD 폭 사이 등식, AoS의 SIMD 무효성 |
| `parts_branch_tests` | `branch` | branchless 토글이 결과를 바꾸지 않는다는 등식 |
| `parts_allocator_tests` | `allocator` | 정렬 규약, 네 할당자의 프레임 계약, 풀 고갈 |
| `parts_container_tests` | `container` | 세 컨테이너의 합산 동등성과 노드 회수 |
| `parts_graph_tests` | `graph` | 네 측정 채널이 같은 head로 함께 밀리는가 |

```bash
ctest --test-dir build -C Release -R parts_allocator_tests --output-on-failure
./build/Release/parts_core_tests allocator   # ctest가 부르는 것과 같은 실행
```

```text
parts_core_tests: group `allocator`
  allocator/AlignmentHelpers ... 5 check(s) ok
  allocator/MallocAndArena ... 11 check(s) ok
  allocator/PoolAndSlab ... 15 check(s) ok
  allocator/PoolExhaustion ... 31 check(s) ok
  allocator/EventBenchmarkContract ... 86 check(s) ok
allocator: 5 test(s), 148 check(s), 0 failure(s)
```

그룹마다 단언 수가 다르므로, 출력 마지막 줄만 봐도 무엇을 돌렸는지와 검사가 줄지 않았는지를 함께 확인할 수 있다.

AVX2 구성에서도 같은 타깃을 돌린다. `PERF_LAB_AVX2` 정의와 `/arch:AVX2`(GCC/Clang은 `-mavx2 -mfma`)는 앱 타깃과 테스트 타깃에 동일하게 부여되므로, 이 구성에서만 `UpdateAVX2` 경로가 컴파일되고 검사된다.

```bash
cmake --build build_avx2 --config Release --target parts_core_tests
ctest --test-dir build_avx2 -C Release --output-on-failure
```

## 시리즈 글과의 관계

이 lab은 위에 적은 여덟 편의 핵심 측정을 재현 가능하게 만든다. 글에서 "Pool 할당자가 malloc 대비 N배 빠르다", "SoA가 AoS 대비 순회 시간을 M% 줄인다", "predication이 if 분기보다 K% 빠르다"고 주장한 수치를 독자가 본인 머신에서 그대로 측정할 수 있다. 다만 캐시 미스·분기 미스 같은 HW 카운터는 이 lab이 수집하지 않으므로, 그 계열 주장은 ms 차이로만 간접 관찰된다.

## 거대 프로젝트와의 관계

거대 프로젝트의 `memory`, `containers`, `ecs` 모듈은 현재 디펜스 제품 경로에 억지로 연결하지 않고 독립 테스트로 검증한다. 이 lab은 해당 모듈을 다른 제품 경로에 채택할 때 사용할 판단 근거를 만든다. 프레임 Arena·Pool·Slab의 수명 계약, SoA·AoS·Archetype의 순회 비용, 스칼라·SIMD 경로의 결과 동등성을 같은 워크로드에서 측정한다.
