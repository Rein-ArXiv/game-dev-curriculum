# graphics_dx_lab - DirectX 12 명시적 렌더링 비용 측정

## 컨셉

이 lab은 `graphics_gl_lab`의 같은 큐브 장면을 DirectX 12 모델로 옮겨, 드로우 제출과 명시적 상태 관리 비용을 측정한다. 핵심은 “DX12가 빠르다”가 아니라 “DX12는 앱이 PSO, Root Signature, Descriptor Heap, Resource Barrier, Fence를 직접 관리한다”는 점이다.

현재 구현은 다음을 실제 렌더 경로에 반영한다.

- Per-Draw root constants vs Instanced root SRV.
- PSO 설정 정책: per-pass / per-material / per-draw.
- Root Signature 설정 정책: global / per-draw.
- Descriptor 수명 정책: frame-slot transient rebuild vs persistent table.
- Resource Barrier 정책: immediate / batched / split-style begin/end.
- CPU frame timer, GPU timestamp query, D3D12 debug callback.

Bindless, PIX 자동 캡처, 대형 render graph barrier 최적화는 실행 코드 범위에 넣지 않는다.

## 토글 항목

| 항목 | 옵션 | 코드 반영 |
|---|---|---|
| Draw mode | Per-Draw / Instanced | 실제 draw 호출 수 변경 |
| PSO policy | Per-Pass / Per-Material / Per-Draw | `SetPipelineState` 호출 수 변경. Instanced 모드는 드로우가 1회라 정책과 무관하게 1회 |
| RootSig policy | Global / Per-Draw | `SetGraphicsRootSignature` 호출 수 변경. Instanced 모드에서는 1 → 2회 차이뿐 |
| Descriptor policy | Transient rebuild / Persistent table | frame-safe null SRV 작성 수와 CPU 프레임 비용 변경 |
| Barrier policy | Immediate / Batched / Split-style | 단일 back-buffer 전이의 기록 형태 확인. Immediate와 Batched의 성능 비교는 아님 |
| Object count | 1k / 10k / 100k | scene rebuild |

## 출력

- 메인 뷰포트 - 회전하는 큐브 격자.
- CPU/GPU ms 그래프 - `std::chrono` + `ID3D12QueryHeap` timestamp.
- 호출 카운터 - draw, instanced draw, PSO set, RootSig set, heap set, descriptor write, root constants, barrier.
- D3D12 debug log - `ID3D12InfoQueue1::RegisterMessageCallback` 메시지 표시.

## 기술 스택

- DirectX 12, DXGI flip-discard swapchain.
- Win32 window.
- Dear ImGui DX12 backend + ImPlot.
- DirectXMath.
- `D3DCompileFromFile` 기반 HLSL `vs_5_1` / `ps_5_1`.
- CMake 3.20+ Visual Studio generator.

## 디렉토리 구조

```text
graphics_dx_lab/
  CMakeLists.txt
  src/
    app/                  Win32 + frame loop + ImGui
    device/               D3D12 device, swapchain, upload helpers
    scene/                cube mesh + per-draw/instanced draw path
                          (기록부는 디바이스 비의존 TU 로 분리)
    pso/                  Lambert / Blinn-Phong PSO cache
    rootsig/              shared root signature
    descriptor/           shader-visible CBV/SRV/UAV heap cursor
    barrier/              immediate/batched/split-style barrier helper
    measure/              frame timer, timestamp query, debug callback
    ui/                   ImGui panel
  shaders/
    cube.hlsl
  articles/
    step01_skeleton.md ... step07_scale.md
```

## 빌드와 실행

```powershell
cmake -S . -B build
cmake --build build --config Release
build\Release\graphics_dx_lab.exe
```

GUI 없이 도는 자동 검증은 CTest 타깃 `graphics_dx_contract_tests` 하나다.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

기대 출력은 `1/1 Test #1: graphics_dx_contract_tests .... Passed`와 `DirectX contract tests passed`다.

이 타깃은 헬퍼만 컴파일하지 않고 **제품 TU 를 그대로 링크한다** — `src/scene/scene_record.cpp`, `src/barrier/barrier_helpers.cpp`, `src/descriptor/heap_manager.cpp`, `src/measure/timestamp_query.cpp`. GPU·디바이스·윈도우가 필요 없는 이유는 세 가지다. (1) 명령 기록부(`Scene::RecordDraw`)가 `ID3D12GraphicsCommandList*` 와 오브젝트 배열만 쓰도록 분리돼 있고, 테스트는 모든 가상 함수를 채운 command list 대역에 호출을 기록한다. (2) descriptor 작성 지점(`HeapManager::WriteNullSrvs`)이 virtual 이라 파생 클래스가 가로챈다. (3) fence/readback 은 스텁을 주입하는 헤드리스 초기화 경로로 물린다.

검사 대상:

| 대상 | 확인하는 것 |
|---|---|
| `Scene::RecordDraw` | 정책별 draw / SetPipelineState / SetGraphicsRootSignature 호출 수, 드로우당 루트 상수 3연속 `(0,1,0) (0,16,4) (0,4,20)`, 인스턴스 업로드 행렬과 per-draw 루트 상수 행렬의 일치 |
| `ExpectedDrawSubmissions` | 실제 기록된 제출 수와 예측값이 맞물리는지 |
| 루트 상수 슬롯 규약 | 슬롯이 서로 겹치지 않고 루트 시그니처 예산과 정확히 맞는지(컴파일 타임 단언 포함) |
| `PlanNullSrvWrites` | frame slot 구간끼리, 그리고 persistent 구간과 서로 겹치지 않는지 |
| `HeapManager::PrepareNullSrvWorkload` | 계획대로 descriptor 를 쓰고 persistent 표는 재사용하는지 |
| `BarrierBatch` 세 Flush | 반환한 발급 수 == 실제 기록된 `ResourceBarrier` 호출 수 (K / K?1:0 / 2K) |
| `RecordBackBufferBarriers` | 프레임 전이 사슬 PRESENT→RT→PRESENT 가 끊기지 않는지 |
| `IsSlotReady` / `TimestampQuery` | 미완료 slot 을 Map 하지 않고, 덮어쓰기 전 완료를 확인하는지 |
| `MakeFrameCB` | 원점이 NDC 중심, near/far 평면이 깊이 0/1, aspect 가 가로에만 적용되는지 |
| `ComputeInstanceModel` | 회전 후 이동 합성 결과가 해석적 기대 행렬과 일치하는지 |

Windows 전용이다. DX12 기능 레벨 12_0 이상 GPU가 필요하다. 의존성(ImGui, ImPlot, DirectXMath)은 `FetchContent`로 내려받는다.

## 진행 상태

- [x] **Step 1 골격** - D3D12 device, direct queue, swapchain, depth buffer, frame-in-flight, ImGui.
- [x] **Step 2 측정 인프라** - timestamp query, CPU timer, counters, D3D12 debug callback.
- [x] **Step 3 Draw + PSO** - Per-Draw vs Instanced, PSO set 정책.
- [x] **Step 4 RootSig** - global vs per-draw root signature set 정책.
- [x] **Step 5 Descriptor Heap** - transient rebuild와 persistent table의 실제 null SRV write 수.
- [x] **Step 6 Barrier** - immediate / batched / split-style 호출 의미. 현재 장면은 경계당 전이가 하나라 batching 이득을 측정하지 않음.
- [x] **Step 7 Scale** - 1k / 10k / 100k object rebuild.

## 학습 기준

이 lab을 따라가면 DX12의 핵심 비용이 OpenGL의 “드라이버 내부 상태”가 아니라 앱 코드의 명령 호출 패턴에 드러난다는 점을 확인한다. per-draw root constants와 instancing, PSO/RootSig 재설정, descriptor write 수명 정책은 실제 카운터와 시간으로 비교한다. Barrier 토글은 현재 장면에서 성능 순위를 만들지 않고 API 의미와 호출 수만 확인한다.
