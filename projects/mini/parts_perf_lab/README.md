# parts_perf_lab — parts 시리즈의 원리 측정·시각화

## 컨셉

이 lab은 **동일한 ECS 시뮬레이션을 여러 저레벨 구현으로 돌려놓고 차이를 측정**한다. 1만 개의 엔티티가 위치·속도를 가지고 매 프레임 업데이트되는 워크로드를 고정해두고, 그 위에서 할당자·메모리 레이아웃·컨테이너·SIMD 폭·branchless predication을 런타임에 토글한다. 토글이 바뀌는 순간 측정 그래프(frame wall/alloc/container 세 채널)가 갈라지는 모습을 그대로 본다.

> 이 실습은 게임 개발의 모든 CS 주제를 하나의 프로그램에 구현하는 과정이 아니다. 같은 상태를 여러 메모리 배치와 연산 방식으로 처리하고, 할당·순회 비용을 나누어 관찰하는 저수준 성능 실습이다. ECS 는 단일 컴포넌트 조합의 저장 배치까지만 다룬다. query 와 구조 변경, 문자열·인코딩·동시성 등은 별도 실습에서 다룬다.
>
> 실행 결과가 특정 순서로 나와야 성공하는 실습은 아니다. 먼저 같은 입력에 대해 같은 의미의 결과를 계산하는지 확인한다. 그다음 작업량·측정 구간·실행 환경을 맞추고 반복 측정한다. 기대와 다른 성능이 나오면 결과를 숨기거나 구현 오류로 단정하지 않고, 이 실험이 어떤 가정을 충족하지 못했는지 확인한다.
>
> 각 장의 코드는 세 종류로 구분한다. `현재 단계 완성 코드`는 해당 단계의 파일 집합과 함께 빌드할 수 있어야 한다. `발췌 코드`는 생략된 선언과 helper 의 실제 위치를 함께 적는다. `설명용 의사코드`는 의미와 제어 흐름을 보여주며 그대로 붙여 넣는 소스가 아니다. 최종 `src/`와 중간 단계의 함수 시그니처가 다르면 변환 표를 제공한다.

## 짝 시리즈

- **parts** — 메모리 할당자 내부, 메모리 레이아웃, STL 컨테이너의 비용, SIMD, 분기 예측 같은 저레벨 성능 주제를 다루는 시리즈다. 이 lab이 측정으로 다루는 범위는 parts/03·04(할당자), parts/08·09(vector·intrusive list), parts/11·18(ECS·데이터 지향 레이아웃), parts/19(SIMD), parts/21(분기 예측) 여덟 편이다. 나머지 편은 이 lab의 측정 축에 대응하지 않는다.
- **algorithms** — 정렬·그래프·DP·압축·property 검증 같은 알고리즘 주제는 이 lab이 다루지 않는다. 그 축의 소유 lab은 `projects/mini/algorithms_lab`이다.

## 토글 항목

런타임 토글은 raygui 패널에서, 빌드 시 토글(예: AVX2 활성화)은 CMake 옵션으로 노출한다.

| 항목 | 옵션 | 비교 대상 | 현재 활성 |
|---|---|---|---|
| 엔티티 수 | 1k / 10k / 100k | 스케일링 곡선 비교 | ✓ |
| 메모리 레이아웃 | SoA / AoS / Archetype | parts/18 데이터 지향 설계, parts/11 ECS | ✓ |
| SIMD 폭 | C++ Auto / SSE / AVX2 | parts/19 SIMD | ✓ (SoA/Archetype만, AoS는 수동 SIMD 없음) |
| FMA | on / off (빌드 옵션) | parts/19 SIMD | ✓ (AVX2 경로에만 적용, `PERF_LAB_ENABLE_FMA`) |
| branchless predication | on / off | parts/21 분기 예측 | ✓ (C++ Auto 경로에만 효과) |
| 할당자 | malloc / Arena / Pool / Slab | parts/03 메모리 할당자, parts/04 커스텀 할당자 | ✓ (EventBenchmark 워크로드에 적용) |
| 컨테이너 | vector / std::list / Intrusive list | parts/08 vector, parts/09 intrusive list | ✓ (ContainerBenchmark 워크로드에 적용) |

AVX2 경로는 빌드 시 `cmake -DPERF_LAB_ENABLE_AVX2=ON` 옵션이 필요하다. 옵션이 꺼진 상태에서 AVX2 토글을 선택하면 SSE 경로로 폴백한다. 비-x86 플랫폼(예: Apple Silicon)에서는 SSE/AVX2 토글 모두 C++ Auto로 폴백한다. HUD는 요청한 커널과 실제 실행 커널, 폴백 이유를 함께 표시한다(AoS는 이 lab에 수동 SIMD 경로가 없어 항상 C++ Auto).

`PERF_LAB_ENABLE_AVX2=ON` 빌드에서 FMA 사용 여부는 별도 옵션 `PERF_LAB_ENABLE_FMA`로 나눈다(기본 ON). FMA는 곱셈·덧셈을 한 번의 반올림으로 합치므로 반올림 계약이 달라진다. 폭(AVX2)과 FMA를 독립 축으로 두어 위치 차이를 폭 때문으로 오해하지 않게 한다. `PERF_LAB_ENABLE_FMA=OFF`이면 AVX2 경로도 분리 곱셈·덧셈을 쓴다. AVX2/FMA 전용 바이너리는 그 기능을 지원하는 CPU/OS 대상 전용이며, 모든 CPU에서 안전한 범용 dispatch는 아직 구현하지 않았다(보충 구현 목표).

여섯 축(엔티티 수·레이아웃·SIMD·branchless·할당자·컨테이너)이 모두 활성 상태다. allocator는 EventBenchmark(매 프레임 2000노드 alloc/free), container는 ContainerBenchmark(5000노드 순회)라는 별도 워크로드 채널에 연결되어 시뮬 측정과 분리된다.

## 출력

화면 상단은 시뮬 뷰포트, 하단은 측정 패널이다.

- 프레임타임 그래프 — frame wall·alloc·container 세 라인을 한 패널에 겹쳐 그림 (최근 600프레임 슬라이딩 윈도우, 60/30fps 기준선, 자동 스케일). x축은 표본 번호라 실제 경과 시간 길이는 프레임률에 따라 달라진다.
- 좌측 stats — entities, frame wall/sim/draw-submit ms, requested→effective 커널, actual fps, alloc ms·ok/target·fail, live/peak bytes, slot/backing bytes, container ms·nodes·sum.
- 할당 통계 — `IAllocator` 내장 카운터(프레임당 호출 수·바이트, live/peak bytes, 실패 횟수, 예약 backing/점유 slot bytes).

미구현(보충 구현 목표, articles/step08 참조): 고정 fixture·고정 dt·반복 측정·JSONL export runner, 채널별 확대 모드, 캐시 미스·분기 미스 등 HW 카운터(perf/VTune), 메모리 단편화 시각화, GPU 시간 분리(RenderDoc 등).

## 측정 항목

- **frame wall / sim / draw-submit**: `std::chrono::steady_clock`으로 프레임 전체와 시뮬, 그리기 명령 발행 구간을 분리 샘플링. frame wall은 HUD와 `EndDrawing`(flush/swap)을 포함하지만 GPU 실행 시간이나 전체 프로그램 CPU 시간은 아니다. `sim+submit`은 전체 CPU 프레임이 아니므로 `cpu fps`로 부르지 않는다.
- **alloc ms**: EventBenchmark가 매 프레임 노드 다발 alloc/free 뒤 allocator `Reset`까지 걸린 시간을 자체 측정. 이는 특정 프로토콜(alloc+개별 반환+Reset)의 비용이지 allocator 일반 성능이 아니다.
- **container ms**: ContainerBenchmark가 N노드 순회에 걸린 시간을 자체 측정. 저장 형태·할당 방식·순회 순서가 함께 바뀌는 비교다.
- **할당 횟수·바이트·실패**: 커스텀 할당자(`IAllocator`)의 내장 카운터. 용량 부족으로 완료 수가 요청보다 적으면 HUD가 `invalid sample`로 표시하고 시간 비교에서 제외한다.

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
      kernel_dispatch.h        요청 SIMD와 실제 실행 커널 판정
      {soa,aos,archetype}_draw.cpp   Draw() 전용 TU. 앱 타깃만 링크한다.
      factory.cpp
    alloc/                     iallocator.h + 4종 구현
      malloc_allocator.{h,cpp}
      arena_allocator.{h,cpp}
      pool_allocator.{h,cpp}
      slab_allocator.{h,cpp}
    measure/
      bench_status.h           벤치 결과 상태(valid/skipped/invalid-capacity)
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

- 정렬 헬퍼와 4종 할당자의 정렬·크기 클래스·해제 계약, Pool 고갈 시 `nullptr` 반환과 반납 후 슬롯 재사용. Pool 생성자가 잘못된 size/count/alignment와 정렬 올림·용량 곱셈 overflow를 예외로 거부하고, `Allocate(0)`은 모든 backend에서 `nullptr`이며 카운터를 바꾸지 않는다.
- **소유권·활성 상태** — Pool의 `Owns`는 슬롯 시작이면서 현재 할당된 포인터만 인정한다. `ReservedBacking()`/`SlotBytesLive()`가 요청 byte와 다른 공간 회계를 드러낸다.
- 컨테이너 세 종류(vector / std::list / intrusive)의 **결과 동등성** — 같은 시드로 채운 노드 수가 정확히 같고 합산 결과가 순회 순서 차이만큼만 다르다. 용량이 모자라면 완료 수와 `InvalidCapacity` 상태가 남는다.
- `EventBenchmark` 프레임 계약 — 4종 할당자 각각에서 프레임 종료 시 `LiveBytes() == 0`, `LastFrameAllocCount() == Target()`, Arena의 bump pointer가 0으로 되돌아온다. `Init`은 이전 결과를 비우고, 고갈 표본은 `InvalidCapacity`로 표시한다.
- 요청/실행 커널 분리 — `ResolveKernel(Layout, requested, branchless)`이 실제 실행 커널과 폴백 이유를 정한다(AoS는 수동 SIMD가 없어 C++ Auto, AVX2 미빌드는 SSE 등).
- `{SoA, AoS, Archetype} x {C++ Auto, SSE, AVX2} x {branchless off, on}` 12조합(AVX2 빌드에서는 18조합)이 모두 같은 결과를 낸다. 엔티티 수 513은 SIMD 잔여(513%4=1)와 Archetype 부분 chunk(2\*256+1)를 동시에 자극한다.
- 입력 계약 — viewport < 2는 2로 올리고, NaN/음수 dt는 0으로 취급해 유효 상태가 변하지 않는다. 초기 좌표는 `0 <= x <= W-1` 안에 있다.
- 경계 반사 불변식 — 갱신 후 모든 엔티티가 뷰포트 안에 있고, `|velX|`·`|velY|`가 성분별로 보존되며, 좁은 뷰포트에서 대다수 엔티티의 속도 부호가 실제로 뒤집힌다. 이 반사는 큰 dt의 다중 반사를 보존하는 물리 모델이 아니라 한 step clamp + 부호 반전 규칙이다.
- 프레임 타이머와 패널 기하 — frame wall/sim/draw-submit 채널이 같은 head로 밀리고, 열린 드롭다운이 창 안에 들어오며, 0 크기 사각형은 겹침이 아니다.

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release          # 기본 (SSE2까지)
cmake -S . -B build_avx2 -DPERF_LAB_ENABLE_AVX2=ON      # AVX2 경로 활성화 (FMA는 별도 옵션)
cmake --build build -j
./build/parts_perf_lab
```

GUI 없이 계산 코어·테스트만 구성하려면 `-DPERF_LAB_BUILD_GUI=OFF`를 준다. 이 구성은 raylib/raygui FetchContent(네트워크·GLFW·OpenGL)를 건너뛰므로 의존성 없이 `parts_core_tests`를 빌드·실행할 수 있다.

```bash
cmake -S . -B build_headless -DPERF_LAB_BUILD_GUI=OFF
cmake --build build_headless --config Release --target parts_core_tests
ctest --test-dir build_headless -C Release --output-on-failure
```

자동 검증은 `parts_core_tests` 타깃이다.

```bash
cmake --build build --config Release --target parts_core_tests
ctest --test-dir build -C Release --output-on-failure
```

실행 파일은 하나지만 실행 단위는 주제별로 나뉘어 있다. `parts_core_tests` 는 `argv[1]` 을 그룹 이름으로 받아 그 그룹의 검사만 돌리고, CMake가 그룹마다 ctest 이름을 하나씩 등록한다. 전체를 도는 `parts_core_tests` 는 그대로 남는다.

| ctest 이름 | 그룹 인자 | 검사 대상 |
|---|---|---|
| `parts_skeleton_tests` | `skeleton` | `FrameTimer` 의 프레임/update/draw-submit 분리와 링 버퍼 포화 |
| `parts_toggles_tests` | `toggles` | `RequiresRestart` 판정, 토글 패널 기하(열린 팝업 창 안), 두 패스 그리기 |
| `parts_layout_tests` | `layout` | 세 레이아웃의 결과 동등성, 경계 반사 불변식, 입력 계약 |
| `parts_simd_tests` | `simd` | SIMD 폭 사이 등식, 요청/실행 커널 판정, AoS의 수동 SIMD 부재 |
| `parts_branch_tests` | `branch` | branchless 토글이 결과를 바꾸지 않는다는 등식 |
| `parts_allocator_tests` | `allocator` | 정렬 규약, 네 할당자의 프레임 계약, 생성자 입력 거부, 풀 고갈·소유권, 벤치 상태 |
| `parts_container_tests` | `container` | 세 컨테이너의 합산 동등성, 용량 부족 상태와 노드 회수 |
| `parts_graph_tests` | `graph` | 다섯 측정 채널이 같은 head로 함께 밀리는가 |

```bash
ctest --test-dir build -C Release -R parts_allocator_tests --output-on-failure
./build/Release/parts_core_tests allocator   # ctest가 부르는 것과 같은 실행
```

```text
parts_core_tests: group `allocator`
  allocator/AlignmentHelpers ... 5 check(s) ok
  allocator/MallocAndArena ... 21 check(s) ok
  allocator/PoolAndSlab ... 15 check(s) ok
  allocator/PoolExhaustion ... 31 check(s) ok
  allocator/AllocatorContractGuards ... 31 check(s) ok
  allocator/PoolOwnership ... 6 check(s) ok
  allocator/BenchmarkStatusAndReset ... 18 check(s) ok
  allocator/EventBenchmarkContract ... 90 check(s) ok
allocator: 8 test(s), 217 check(s), 0 failure(s)
```

그룹마다 단언 수가 다르므로, 출력 마지막 줄만 봐도 무엇을 돌렸는지와 검사가 줄지 않았는지를 함께 확인할 수 있다. 다만 단언 수 자체는 검증 강도의 지표가 아니다. 조건을 true로 바꾸거나 핵심 검사를 제거해도 수는 유지될 수 있으므로, 어떤 불변식을 검증했는지가 기준이다. 위 수치는 한 실행(MSVC x64, AVX2 ON)의 기록이며 환경에 따라 달라질 수 있다.

AVX2 구성에서도 같은 타깃을 돌린다. `PERF_LAB_AVX2` 정의와 `/arch:AVX2`(GCC/Clang은 `-mavx2` + 필요 시 `-mfma`)는 앱 타깃과 테스트 타깃에 동일하게 부여되므로, 이 구성에서만 `UpdateAVX2` 경로가 컴파일되고 검사된다. FMA는 `PERF_LAB_ENABLE_FMA` 옵션으로 분리한다. `PERF_LAB_ENABLE_AVX2=ON` 바이너리는 AVX2/FMA를 지원하는 CPU/OS 대상 전용이며, 미지원 CPU에서도 안전한 범용 실행 파일은 아직 아니다(보충 구현 목표).

```bash
cmake --build build_avx2 --config Release --target parts_core_tests
ctest --test-dir build_avx2 -C Release --output-on-failure
```

## 시리즈 글과의 관계

이 lab은 위에 적은 여덟 편의 핵심 측정을 재현 가능하게 만든다. 글에서 "Pool 할당자가 malloc 대비 N배 빠르다", "SoA가 AoS 대비 순회 시간을 M% 줄인다", "predication이 if 분기보다 K% 빠르다"고 주장한 수치를 독자가 본인 머신에서 그대로 측정할 수 있다. 다만 캐시 미스·분기 미스 같은 HW 카운터는 이 lab이 수집하지 않으므로, 그 계열 주장은 ms 차이로만 간접 관찰된다.

## 거대 프로젝트와의 관계

거대 프로젝트의 `memory`, `containers`, `ecs` 모듈은 현재 디펜스 제품 경로에서 모두 쓰이고 있고, 그 위에 독립 테스트가 따로 붙어 있다. 이 lab은 그 모듈을 다른 제품 경로에 채택하거나 대안을 고를 때 사용할 판단 근거를 만든다. 프레임 Arena·Pool·Slab의 수명 계약, SoA·AoS·Archetype의 순회 비용, C++ Auto·SIMD 경로의 결과 동등성을 같은 워크로드에서 측정한다.
