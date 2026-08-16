## 들어가며

DX12 의 측정 인프라 — *Timestamp Query (GPU 시간)* + *Counter (CPU 호출)* + *InfoQueue1 콜백 (디버그 메시지)*. graphics_gl_lab 의 `GL_TIME_ELAPSED` / `GL_KHR_debug` 패턴을 DX12 의 명시적 API 로 옮긴다.

여기까지의 코드 상태 — 골격 + 1 만 큐브 + CPU 프레임 타이머. 이 글에서 도달할 결과물 — CPU/GPU ms 동시 측정 + 카운터 + 디버그 로그 + GPU 없이 도는 계약 테스트 타깃.

## 이 step 이 추가·변경하는 파일

| 파일 | 상태 | 역할 |
|---|---|---|
| `src/measure/timestamp_query.h` · `.cpp` | 신규 | query heap + readback + ring/fence 상태 기계 |
| `src/measure/d3d12_debug.h` · `.cpp` | 신규 | `ID3D12InfoQueue1` 콜백 등록 + 메시지 링 |
| `src/scene/scene.h` | 변경 | `GlCounters` 추가 |
| `src/app/app.h` · `.cpp` | 변경 | `TimestampQuery` · `D3D12Debug` 멤버, 프레임 루프에 `Begin`/`End`/`SignalSubmitted`/`Poll` 삽입 |
| `src/ui/panel.h` · `.cpp` | 변경 | CPU/GPU ms plot + 디버그 로그 창 |
| `tests/d3d12_stubs.h` · `tests/graphics_dx_contract_tests.cpp` | 신규 | 디바이스 없는 대역 + 계약 테스트 |
| `CMakeLists.txt` | 변경 | `graphics_dx_contract_tests` 타깃과 `add_test` 추가 |

---

## 목차
1. [TimestampQuery 의 구조](#1-timestampquery-의-구조)
2. [Ring buffer와 fence 완료 판정](#2-ring-buffer와-fence-완료-판정)
3. [프레임 루프에 끼워 넣기](#3-프레임-루프에-끼워-넣기)
4. [GlCounters — 호출 카운터](#4-glcounters--호출-카운터)
5. [InfoQueue1 콜백](#5-infoqueue1-콜백)
6. [통합 패널](#6-통합-패널)
7. [상태 기계의 자동 검증](#7-상태-기계의-자동-검증)

---

## 1. TimestampQuery 의 구조

GPU 시간은 `ID3D12QueryHeap` 의 TIMESTAMP 쿼리 두 개(begin/end) 차이다. 결과는 GPU 가 쓰고 CPU 가 읽으므로 readback heap 이 필요하고, 틱을 ms 로 바꾸려면 큐의 주파수가 필요하다.

<!-- snippet: src/measure/timestamp_query.h -->
```cpp
namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

inline bool IsSlotReady(UINT64 completedValue, UINT64 slotFenceValue) {
    return slotFenceValue != 0 && completedValue >= slotFenceValue;
}

class TimestampQuery {
public:
    static constexpr int kRingSize    = 3;
    static constexpr int kHistorySize = 240;

    void Init(ID3D12Device* device, ID3D12CommandQueue* queue);
    void Shutdown();

    void InitHeadless(ID3D12QueryHeap* heap, ID3D12Resource* readback,
                      ID3D12CommandQueue* queue, ID3D12Fence* fence,
                      UINT64 frequency);

    void Begin(ID3D12GraphicsCommandList* cmd);
    void End(ID3D12GraphicsCommandList* cmd);

    void SignalSubmitted();

    void Poll();

    double LastMs() const { return lastMs_; }
    double EmaMs()  const { return emaMs_; }
    const std::array<float, kHistorySize>& History() const { return history_; }
    int HistoryHead() const { return historyHead_; }

    UINT64 SlotFence(int slot) const { return slotFences_[(size_t)slot]; }
    int WriteIndex()  const { return writeIdx_; }
    int ReadIndex()   const { return readIdx_; }
    int ActiveSlot()  const { return activeSlot_; }

private:
    ComPtr<ID3D12QueryHeap>     heap_;
    ComPtr<ID3D12Resource>      readback_;
    ComPtr<ID3D12CommandQueue>  queue_;
    ComPtr<ID3D12Fence>         fence_;
    UINT64                      frequency_ = 0;
    UINT64                      nextFenceValue_ = 0;
    std::array<UINT64, kRingSize> slotFences_{};
    int  writeIdx_ = 0;
    int  readIdx_ = 0;
    int  activeSlot_ = -1;
    int  submittedSlot_ = -1;

    double lastMs_ = 0.0;
    double emaMs_  = 0.0;
    static constexpr double kEmaAlpha = 0.1;
    std::array<float, kHistorySize> history_{};
    int historyHead_ = 0;
};

}  // namespace dxlab
```

`InitHeadless` 가 공개 API 에 있는 이유는 뒤의 §7 이다 — 같은 상태 기계를 디바이스 없이 돌리기 위해 자원 생성만 건너뛰는 초기화 경로를 둔다.

`Init` 은 세 자원을 만든다. query heap 은 `kRingSize * 2` 개 쿼리(슬롯당 begin/end 한 쌍), readback 버퍼는 같은 수의 `UINT64`, 그리고 slot 완료를 판정할 fence 다. `GetTimestampFrequency` 로 *틱 → ms* 비율을 얻는다 — NVIDIA / AMD / Intel 마다 다르다.

<!-- snippet: src/measure/timestamp_query.cpp#TimestampQuery::Init -->
```cpp
void TimestampQuery::Init(ID3D12Device* device, ID3D12CommandQueue* queue) {
    queue->GetTimestampFrequency(&frequency_);
    queue_ = queue;

    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = kRingSize * 2;
    if (FAILED(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap_)))) {
        throw std::runtime_error("CreateQueryHeap failed");
    }

    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width  = kRingSize * 2 * sizeof(UINT64);
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback_)))) {
        throw std::runtime_error("CreateCommittedResource(readback) failed");
    }
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    IID_PPV_ARGS(&fence_)))) {
        throw std::runtime_error("CreateFence(timestamp) failed");
    }
    slotFences_.fill(0);
    history_.fill(0.0f);
}
```

기록은 `EndQuery` 두 번이다 — 이름이 `EndQuery` 지만 begin 쪽에도 같은 함수를 쓴다. TIMESTAMP 쿼리는 구간이 아니라 *시점* 을 찍는 쿼리이기 때문이다. 사이의 GPU 시간이 우리가 원하는 값이다.

<!-- snippet: src/measure/timestamp_query.cpp#TimestampQuery::End -->
```cpp
void TimestampQuery::End(ID3D12GraphicsCommandList* cmd) {
    if (activeSlot_ < 0) return;

    cmd->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                  activeSlot_ * 2 + 1);
    cmd->ResolveQueryData(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          activeSlot_ * 2, 2,
                          readback_.Get(),
                          activeSlot_ * 2 * sizeof(UINT64));
    submittedSlot_ = activeSlot_;
    writeIdx_ = (activeSlot_ + 1) % kRingSize;
    activeSlot_ = -1;
}
```

`ResolveQueryData` 가 *readback heap* 에 *틱 값 두 개* 를 복사한다.

## 2. Ring buffer와 fence 완료 판정

`ResolveQueryData`도 GPU 명령이다. 명령을 기록했다는 사실이나 CPU 프레임이 세 번
지났다는 사실만으로 readback 쓰기 완료를 증명할 수 없다. 각 ring slot을 제출한
직후 queue에 fence 값을 signal하고, `GetCompletedValue()`가 그 값 이상인 slot만
`Map`해야 한다.

<!-- snippet: src/measure/timestamp_query.cpp#TimestampQuery::SignalSubmitted -->
```cpp
void TimestampQuery::SignalSubmitted() {
    if (submittedSlot_ < 0 || !queue_ || !fence_) return;
    const UINT64 value = ++nextFenceValue_;
    if (FAILED(queue_->Signal(fence_.Get(), value))) {
        submittedSlot_ = -1;
        return;
    }
    slotFences_[(size_t)submittedSlot_] = value;
    submittedSlot_ = -1;
}
```

<!-- snippet: src/measure/timestamp_query.cpp#TimestampQuery::Poll -->
```cpp
void TimestampQuery::Poll() {
    if (!fence_) return;
    const UINT64 ready = slotFences_[(size_t)readIdx_];
    if (ready == 0) return;
    if (!IsSlotReady(fence_->GetCompletedValue(), ready)) return;

    UINT64* mapped = nullptr;
    D3D12_RANGE rd{(size_t)(readIdx_ * 2 * sizeof(UINT64)),
                    (size_t)((readIdx_ * 2 + 2) * sizeof(UINT64))};
    if (FAILED(readback_->Map(0, &rd, reinterpret_cast<void**>(&mapped)))) return;
    UINT64 ts0 = mapped[readIdx_ * 2 + 0];
    UINT64 ts1 = mapped[readIdx_ * 2 + 1];
    D3D12_RANGE wr{0, 0};
    readback_->Unmap(0, &wr);
    slotFences_[(size_t)readIdx_] = 0;
    readIdx_ = (readIdx_ + 1) % kRingSize;

    if (frequency_ == 0 || ts1 < ts0) return;
    lastMs_ = (double)(ts1 - ts0) * 1000.0 / (double)frequency_;
    if (emaMs_ <= 0.0) emaMs_ = lastMs_;
    else emaMs_ = emaMs_ * (1.0 - kEmaAlpha) + lastMs_ * kEmaAlpha;

    history_[(size_t)historyHead_] = (float)lastMs_;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
}
```

미완료면 기다리지 않고 그냥 돌아온다. `Map` 에 넘기는 `D3D12_RANGE` 가 *읽을 구간만* 가리키고, `Unmap` 에 넘기는 범위는 `{0, 0}` 이다 — CPU 가 아무것도 쓰지 않았다는 뜻이다. 이 두 범위를 대충 `nullptr` 로 두면 드라이버가 버퍼 전체를 무효화한다.

ring을 다시 사용할 때도 같은 검사가 필요하다. GPU가 CPU보다 세 프레임 이상
뒤처졌다면 slot을 덮어쓰지 않고 그 프레임의 측정을 생략한다. 측정 누락은
프레임 정지보다 낫고, 완료되지 않은 readback을 읽는 것과 달리 결과의 의미를
보존한다.

<!-- snippet: src/measure/timestamp_query.cpp#TimestampQuery::Begin -->
```cpp
void TimestampQuery::Begin(ID3D12GraphicsCommandList* cmd) {
    activeSlot_ = -1;
    if (!fence_) return;

    const UINT64 previous = slotFences_[(size_t)writeIdx_];
    if (previous != 0 && !IsSlotReady(fence_->GetCompletedValue(), previous)) {
        return;
    }

    activeSlot_ = writeIdx_;
    cmd->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                  activeSlot_ * 2);
}
```

`activeSlot_ == -1` 이 “이번 프레임은 측정하지 않는다”는 표시다. `End` 가 그 값을 보고 아무 명령도 기록하지 않으므로, 쿼리 짝이 어긋나는 일이 생기지 않는다.

## 3. 프레임 루프에 끼워 넣기

호출 순서가 곧 계약이다. `Begin` / `End` 는 *명령 기록* 이므로 command list 가 열려 있는 동안 부르고, `SignalSubmitted` 는 *제출 이후* 에 부른다. 제출 전에 signal 하면 fence 값이 아직 기록되지 않은 명령을 보증하게 된다.

<!-- snippet: src/app/app.cpp@gpu-timer-record -->
```cpp
gpuTimer_.Begin(cmd);
// ... 장면 드로우 기록 ...
gpuTimer_.End(cmd);
```

<!-- snippet: src/app/app.cpp@frame-submit -->
```cpp
ctx_.EndFrame(vsync_);
gpuTimer_.SignalSubmitted();
gpuTimer_.Poll();
cpuTimer_.EndFrame();
```

`EndFrame` 이 `ExecuteCommandLists` + `Present` + 프레임 fence signal 을 하고, 그 직후에 timestamp 전용 fence 를 signal 한다. `Poll` 은 같은 프레임의 결과가 아니라 *가장 오래된 완료 slot* 을 가져오므로 보통 두세 프레임 전 값이다. GPU ms 가 CPU ms 보다 늦게 반응하는 것은 버그가 아니라 이 지연 때문이다.

## 4. GlCounters — 호출 카운터

시간만으로는 “무엇이 느린가”를 못 가른다. 그래서 프레임마다 명령 호출 수를 직접 센다.

<!-- snippet: src/scene/scene.h#GlCounters -->
```cpp
struct GlCounters {
    int drawCalls = 0;
    int instancedDraws = 0;
    int pipelineSets = 0;
    int rootSigSets  = 0;
    int rootConstantUpdates = 0;
    int barrierCalls = 0;
    // ...
    void Reset() {
        drawCalls = instancedDraws = pipelineSets = rootSigSets = 0;
        rootConstantUpdates = barrierCalls = descriptorHeapSets = 0;
        descriptorWrites = 0;
    }
};
```

Scene / App 의 코드가 *명시적으로 증가* 시킨다. 어떤 토글이 *어느 카운터를 줄이는지* 가 직관적으로 보인다. `Reset()` 이 프레임 시작마다 불리므로 패널의 값은 누적 합이 아니라 *이번 프레임* 의 수다.

## 5. InfoQueue1 콜백

D3D12 *Debug Layer* 가 활성이면 *드라이버 메시지* 가 발생한다. `ID3D12InfoQueue1::RegisterMessageCallback` 으로 후킹한다. 디버그 레이어가 꺼져 있으면 `QueryInterface` 자체가 실패하므로, 그 경우는 조용히 무시하고 진행한다.

<!-- snippet: src/measure/d3d12_debug.cpp#D3D12Debug::Install -->
```cpp
void D3D12Debug::Install(ID3D12Device* device) {
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue_)))) {
        return;
    }
    if (FAILED(infoQueue_->RegisterMessageCallback(
            &Callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &cookie_))) {
        cookie_ = 0;
        infoQueue_.Reset();
    }
}
```

콜백은 `__stdcall` 정적 함수이고, `this` 를 컨텍스트 포인터로 받아 인스턴스 링 버퍼에 넣는다.

<!-- snippet: src/measure/d3d12_debug.cpp#D3D12Debug::Callback -->
```cpp
void __stdcall D3D12Debug::Callback(D3D12_MESSAGE_CATEGORY /*category*/,
                                     D3D12_MESSAGE_SEVERITY severity,
                                     D3D12_MESSAGE_ID id,
                                     LPCSTR description,
                                     void* context) {
    auto* self = (D3D12Debug*)context;
    DebugMessage m;
    m.id = (UINT64)id;
    m.severity = ToSeverity(severity);
    m.text = description ? description : "";
    self->Push(m);
}
```

여기에 수명 함정이 하나 있다. 콜백이 `this` 를 붙잡고 있으므로 *객체가 파괴되기 전에 반드시 등록을 해제해야 한다*. 소멸자에만 맡기면 멤버 선언 순서에 따라 디바이스가 먼저 죽는 경우가 생기므로, 종료 경로에서 명시적으로 부른다.

<!-- snippet: src/measure/d3d12_debug.cpp#D3D12Debug::Uninstall -->
```cpp
void D3D12Debug::Uninstall() {
    if (infoQueue_ && cookie_ != 0) {
        infoQueue_->UnregisterMessageCallback(cookie_);
        cookie_ = 0;
    }
    infoQueue_.Reset();
}
```

D3D12 Debug Layer 의 *Corruption / Error / Warning / Info / Message* 5 단계 메시지를 자체 `Severity` 다섯 값으로 접는다. 처음 실행하면 *수십 개가 쏟아지는* 일이 흔하고, 그 대부분은 *실제 문제* 다.

## 6. 통합 패널

ImGui + ImPlot 의 패널이 *CPU ms + GPU ms 동시 plot* + *카운터 텍스트* + *디버그 로그 별도 창* 을 그린다.

두 히스토리는 링 버퍼이므로 그리기 직전에 head 기준으로 펴야 한다. graphics_gl_lab 과 같은 패턴이다 — Linearize 함수 / 시계열 plot / 색상 분류.

<!-- snippet: src/ui/panel.cpp@linearize -->
```cpp
template<int N>
std::array<float, N> Linearize(const std::array<float, N>& src, int head) {
    std::array<float, N> out{};
    for (int i = 0; i < N; ++i) out[(size_t)i] = src[(size_t)((head + i) % N)];
    return out;
}
```

<!-- snippet: src/ui/panel.cpp@ms-plot -->
```cpp
ImGui::SeparatorText("Frame");
ImGui::Text("CPU frame:  %.3f ms (ema %.3f)",
            cpuTimer.LastFrameMs(), cpuTimer.EmaFrameMs());
ImGui::Text("GPU pass:   %.3f ms (ema %.3f)",
            gpuTimer.LastMs(), gpuTimer.EmaMs());
ImGui::Text("fps:        %.1f", cpuTimer.Fps());

auto cpuHist = Linearize<FrameTimer::kHistorySize>(
    cpuTimer.History(), cpuTimer.HistoryHead());
auto gpuHist = Linearize<TimestampQuery::kHistorySize>(
    gpuTimer.History(), gpuTimer.HistoryHead());

if (ImPlot::BeginPlot("ms history", ImVec2(-1, 140),
                       ImPlotFlags_NoTitle)) {
    ImPlot::SetupAxes("frame", "ms",
                      ImPlotAxisFlags_AutoFit,
                      ImPlotAxisFlags_AutoFit);
    ImPlot::PlotLine("CPU", cpuHist.data(), FrameTimer::kHistorySize);
    ImPlot::PlotLine("GPU", gpuHist.data(), TimestampQuery::kHistorySize);
    ImPlot::EndPlot();
}
```

디버그 로그는 별도 창이다. 링에 쌓인 메시지를 오래된 것부터 순회하며 severity 색으로 출력한다.

<!-- snippet: src/ui/panel.cpp@debug-log-window -->
```cpp
ImGui::Begin("D3D12 debug log");
int n = debug.Count();
int head = debug.RingHead();
if (n == 0) {
    ImGui::TextDisabled("(no messages)");
} else {
    for (int i = 0; i < n; ++i) {
        int idx = ((head - n + i) + D3D12Debug::kRingSize) % D3D12Debug::kRingSize;
        const auto& m = debug.Get(idx);
        ImGui::PushStyleColor(ImGuiCol_Text, SevColor(m.severity));
        ImGui::Text("[%s] id=%llu %s", SevLabel(m.severity),
                    (unsigned long long)m.id, m.text.c_str());
        ImGui::PopStyleColor();
    }
}
ImGui::End();
```

## 7. 상태 기계의 자동 검증

Ring buffer + fence 패턴에서 진짜 위험한 실수는 두 가지다. *아직 GPU가 안 끝난
slot을 읽는 것*과 *아직 GPU가 안 끝난 slot에 새 쿼리를 덮어쓰는 것*. 둘 다
드물게 잘못된 숫자로만 드러나므로 눈으로는 못 잡는다.

그래서 판정을 순수 함수 하나로 뽑았다.

<!-- snippet: src/measure/timestamp_query.h#IsSlotReady -->
```cpp
inline bool IsSlotReady(UINT64 completedValue, UINT64 slotFenceValue) {
    return slotFenceValue != 0 && completedValue >= slotFenceValue;
}
```

`slotFenceValue == 0`은 “아직 제출된 적 없음”이라 완료값이 아무리 커도 준비된
것이 아니다. 회수(`Poll`)와 덮어쓰기 판정(`Begin`)이 같은 이 함수를 쓴다.

이것을 GPU 없이 돌리려면 타깃이 하나 더 필요하다. 계약 테스트는 헬퍼를 다시 구현하지 않고 *제품 TU 를 그대로 링크* 한다. 링크 대상은 디바이스·스왑체인·ImGui 에 의존하지 않는 TU 뿐이다.

<!-- snippet: CMakeLists.txt@contract-tests -->
```cmake
enable_testing()
add_executable(graphics_dx_contract_tests
    tests/graphics_dx_contract_tests.cpp
    src/barrier/barrier_helpers.cpp
    src/descriptor/heap_manager.cpp
    src/scene/scene_record.cpp
    src/measure/timestamp_query.cpp)
target_include_directories(graphics_dx_contract_tests PRIVATE
    src
    ${directxmath_SOURCE_DIR}/Inc)
target_compile_definitions(graphics_dx_contract_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    DXLAB_WINDOWS=1
    DXLAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(graphics_dx_contract_tests PRIVATE d3d12 dxguid)
add_test(NAME graphics_dx_contract_tests COMMAND graphics_dx_contract_tests)

if(MSVC)
    target_compile_options(graphics_dx_contract_tests PRIVATE
        /W4 /permissive- /utf-8)
endif()
```

`DXLAB_SOURCE_DIR` 은 링크할 수 없는 TU 의 계약을 위한 탈출구다. `src/app/app.cpp` 는 Win32 창·실제 디바이스·ImGui 백엔드에 묶여 헤드리스로 링크할 수 없는데, `App::Shutdown()` 의 *단계 순서* 만은 반드시 지켜져야 한다. 그래서 테스트가 그 파일을 소스 텍스트로 읽어 순서를 검사한다.

`tests/d3d12_stubs.h` 가 `ID3D12GraphicsCommandList` · `ID3D12Fence` · `ID3D12Resource` · `ID3D12QueryHeap` · `ID3D12CommandQueue` 의 모든 가상 함수를 채운 대역을 제공한다. 실제 인터페이스를 상속하므로 제품 코드는 자기가 대역과 이야기하는지 모른다. 타임스탬프 쪽은 `InitHeadless` 로 query heap · readback · queue · fence 를 대역으로 물리고 주파수를 1 MHz 로 주입한다 — 그래야 틱 차가 곧 마이크로초가 돼 기대값을 손으로 계산할 수 있다.

실행 단위는 그 실행 파일 하나가 아니다. `add_test` 는 검사 그룹의 묶음마다 하나씩 있고, 인자가 어느 묶음을 돌지 고른다. 측정 인프라 몫의 등록은 이 한 줄이다.

<!-- snippet: CMakeLists.txt@step02-target -->
```cmake
add_test(NAME graphics_dx_step02_measure      COMMAND graphics_dx_contract_tests step02)
```

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R graphics_dx_step02 --output-on-failure
build\Release\graphics_dx_contract_tests.exe step02
```

```text
1/1 Test #3: graphics_dx_step02_measure .......   Passed    0.03 sec

100% tests passed out of 1

  [step02] slot_ready_predicate                        5 checks
  [step02] timestamp_poll_waits_for_fence              14 checks
  [step02] timestamp_begin_waits_before_overwrite      11 checks
step02: 3 groups, 30 checks, 0 failures
DirectX contract tests passed
```

`-R` 를 빼면 등록된 전체 타깃 `graphics_dx_contract_tests` 가 모든 그룹을 한 번에 돈다. 없는 이름을 넘기면 실행 파일이 `no test group matches` 를 찍고 exit 2 로 죽는다 — 0 개를 돌고 조용히 통과하는 상태가 없다.

세 그룹의 검사 항목은 다음과 같다.

- 완료값이 slot fence 값보다 작으면 `Poll`이 `Map`을 *한 번도* 부르지 않고 slot도 소비하지 않는다.
- 완료 후에는 정확히 한 번 `Map`/`Unmap` 하고, 주입한 tick 차와 주파수로 계산한 ms가 나온다.
- 링을 완료 없이 가득 채운 뒤 `Begin`을 부르면 slot을 잡지 않고(`activeSlot == -1`) `EndQuery`도 기록하지 않는다. 완료를 알리면 그때 다시 같은 slot을 잡는다.
- `App::Shutdown()` 에서 `ctx_.Flush()` 가 ImGui 백엔드 종료·`gpuTimer_.Shutdown()`·`ctx_.Shutdown()` 보다 앞에 온다. `D3D12Context::Flush()` 가 `Signal` 뒤에 실제로 기다린다.

앞의 두 항목은 이렇게 판정한다. 주파수를 1 MHz 로 주입했으므로 tick 차 2000 은 정확히 2 ms 여야 한다.

<!-- snippet: tests/graphics_dx_contract_tests.cpp#TestTimestampPollWaitsForFence -->
```cpp
void TestTimestampPollWaitsForFence() {
    // ...
    tq.SignalSubmitted();
    CHECK(tq.SlotFence(0) == 1);
    CHECK(fence.lastSignaled == 1);

    // GPU 미완료 -> Map 하지 않고, slot 도 소비하지 않는다.
    fence.completed = 0;
    tq.Poll();
    CHECK(readback.mapCalls == 0);
    CHECK(tq.SlotFence(0) == 1);
    CHECK(tq.ReadIndex() == 0);

    // 완료 -> 정확히 1회 Map/Unmap 하고 ms 를 계산한다.
    fence.completed = 1;
    readback.storage[0] = 1000;
    readback.storage[1] = 3000;       // 2000 tick @1MHz = 2ms.
    tq.Poll();
    CHECK(readback.mapCalls == 1);
    CHECK(readback.unmapCalls == 1);
    CHECK(std::fabs(tq.LastMs() - 2.0) < 1e-9);
    CHECK(tq.SlotFence(0) == 0);
    CHECK(tq.ReadIndex() == 1);
}
```

`fence.completed` 를 GPU 대신 손으로 움직이는 것이 이 검사의 핵심이다. 실제 하드웨어에서는 "아직 안 끝난 slot 을 읽는" 순간을 재현할 수 없지만, 여기서는 그 순간이 첫 번째 `Poll()` 이다. 판정식 `IsSlotReady` 를 `return true;`로 바꾸면 그 `Poll()` 이 `Map` 을 불러 버리므로 검사들이 한꺼번에 깨진다.

---

## 결론

DX12 의 *Timestamp Query Ring Buffer + fence 완료 판정 + 카운터 +
InfoQueue1* 측정 인프라가 완성된다. GPU timestamp는 명령 기록 시각이 아니라
queue에서 실제 실행된 두 지점의 차이를 재며, CPU는 완료된 slot만 읽는다.

OpenGL 의 `GL_TIME_ELAPSED` 와 *근본 동일* 한 패턴. API 차이는 *명시적 ring buffer + heap 관리* 만.

이 step 의 한계 — 측정할 대상이 아직 하나뿐이다. 카운터 필드는 만들었지만 이를 실제로 움직이는 토글이 없어 `drawCalls` 외의 값은 프레임마다 같은 수만 찍힌다.

## 더 읽을거리

- Microsoft Learn, [Timing (Direct3D 12 Queries)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing) — 타임스탬프 쿼리·주파수 보정의 공식 문서.
- Microsoft Learn, [ID3D12InfoQueue1](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/nn-d3d12sdklayers-id3d12infoqueue1) — 디버그 레이어 콜백 명세.
- [PIX Timing Captures](https://devblogs.microsoft.com/pix/timing-captures/) — 프레임 전체의 queue 실행과 병목을 분석하는 표준 도구.
