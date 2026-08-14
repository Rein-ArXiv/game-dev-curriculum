## 들어가며

DX12 의 측정 인프라 — *Timestamp Query (GPU 시간)* + *Counter (CPU 호출)* + *InfoQueue1 콜백 (디버그 메시지)*. graphics_gl_lab 의 `GL_TIME_ELAPSED` / `GL_KHR_debug` 패턴을 DX12 의 명시적 API 로 옮긴다.

여기까지의 코드 상태 — Step 1 의 골격 + 1 만 큐브. 이 글에서 도달할 결과물 — CPU/GPU ms 동시 측정 + 카운터 + 디버그 로그.

---

## 목차
1. [TimestampQuery 의 구조](#1-timestampquery-의-구조)
2. [Ring buffer와 fence 완료 판정](#2-ring-buffer와-fence-완료-판정)
3. [GlCounters — 호출 카운터](#3-glcounters--호출-카운터)
4. [InfoQueue1 콜백](#4-infoqueue1-콜백)
5. [통합 패널](#5-통합-패널)

---

## 1. TimestampQuery 의 구조

```cpp
ID3D12QueryHeap   heap;     // TIMESTAMP × 6 (3 frame × begin/end)
ID3D12Resource    readback; // 결과 회수용 readback heap
UINT64            frequency;
```

`GetTimestampFrequency(&frequency)` 로 *틱 → ms* 변환 비율 획득. NVIDIA / AMD / Intel 마다 다름.

```cpp
cmd->EndQuery(heap, TIMESTAMP, writeIdx * 2);     // begin
// ... draws ...
cmd->EndQuery(heap, TIMESTAMP, writeIdx * 2 + 1); // end
cmd->ResolveQueryData(heap, TIMESTAMP,
                       writeIdx * 2, 2,
                       readback, writeIdx * 2 * sizeof(UINT64));
```

`EndQuery` 두 번 호출 (begin / end). 사이의 GPU 시간이 *기록*.

`ResolveQueryData` 가 *readback heap* 에 *틱 값 두 개* 복사.

## 2. Ring buffer와 fence 완료 판정

`ResolveQueryData`도 GPU 명령이다. 명령을 기록했다는 사실이나 CPU 프레임이 세 번
지났다는 사실만으로 readback 쓰기 완료를 증명할 수 없다. 각 ring slot을 제출한
직후 queue에 fence 값을 signal하고, `GetCompletedValue()`가 그 값 이상인 slot만
`Map`해야 한다.

```cpp
void SignalSubmitted() {
    const UINT64 value = ++nextFenceValue;
    queue->Signal(fence, value);
    slotFences[submittedSlot] = value;
}

void Poll() {
    const UINT64 ready = slotFences[readIdx];
    if (ready == 0 || fence->GetCompletedValue() < ready)
        return; // 기다리지 않고 이번 Poll을 건너뛴다.

    // readIdx 범위만 Map해 timestamp 두 개를 읽는다.
}
```

ring을 다시 사용할 때도 같은 검사가 필요하다. GPU가 CPU보다 세 프레임 이상
뒤처졌다면 slot을 덮어쓰지 않고 그 프레임의 측정을 생략한다. 측정 누락은
프레임 정지보다 낫고, 완료되지 않은 readback을 읽는 것과 달리 결과의 의미를
보존한다.

## 3. GlCounters — 호출 카운터

```cpp
struct GlCounters {
    int drawCalls;
    int instancedDraws;
    int pipelineSets;
    int rootSigSets;
    int rootConstantUpdates;
    int barrierCalls;
};
```

Scene / App 의 코드가 *명시적으로 증가*. 어떤 토글이 *어느 카운터를 줄이는지* 가 직관적.

## 4. InfoQueue1 콜백

D3D12 *Debug Layer* 가 활성이면 *드라이버 메시지* 가 발생. *ID3D12InfoQueue1* 의 `RegisterMessageCallback` 으로 후킹.

```cpp
ComPtr<ID3D12InfoQueue1> q;
device->QueryInterface(IID_PPV_ARGS(&q));
q->RegisterMessageCallback(&MyCallback,
                            D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                            userPtr, &cookie);
```

```cpp
void __stdcall MyCallback(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY sev,
                          D3D12_MESSAGE_ID id, LPCSTR desc, void* ctx) {
    // ring buffer 에 누적.
}
```

D3D12 Debug Layer 의 *Corruption / Error / Warning / Info / Message* 5 단계 메시지. 처음 보면 *수십 개 쏟아짐* — 모두 *실제 문제* 인 경우가 흔하다.

## 5. 통합 패널

ImGui + ImPlot 의 패널이 *CPU ms + GPU ms 동시 plot* + *카운터 텍스트* + *디버그 로그 별도 창*.

graphics_gl_lab 의 패턴과 동일 — Linearize 함수 / 시계열 plot / 색상 분류.

## 6. 상태 기계의 자동 검증

Ring buffer + fence 패턴에서 진짜 위험한 실수는 두 가지다. *아직 GPU가 안 끝난
slot을 읽는 것*과 *아직 GPU가 안 끝난 slot에 새 쿼리를 덮어쓰는 것*. 둘 다
드물게 잘못된 숫자로만 드러나므로 눈으로는 못 잡는다.

그래서 판정을 순수 함수 하나로 뽑았다.

```cpp
inline bool IsSlotReady(UINT64 completedValue, UINT64 slotFenceValue) {
    return slotFenceValue != 0 && completedValue >= slotFenceValue;
}
```

`slotFenceValue == 0`은 “아직 제출된 적 없음”이라 완료값이 아무리 커도 준비된
것이 아니다. 회수(`Poll`)와 덮어쓰기 판정(`Begin`)이 같은 이 함수를 쓴다.

계약 테스트는 fence와 readback 버퍼를 대역으로 물려 GPU 없이 상태 기계를
돌린다.

- 완료값이 slot fence 값보다 작으면 `Poll`이 `Map`을 *한 번도* 부르지 않고 slot도 소비하지 않는다.
- 완료 후에는 정확히 한 번 `Map`/`Unmap` 하고, 주입한 tick 차와 주파수로 계산한 ms가 나온다.
- 링을 완료 없이 가득 채운 뒤 `Begin`을 부르면 slot을 잡지 않고(`activeSlot == -1`) `EndQuery`도 기록하지 않는다. 완료를 알리면 그때 다시 같은 slot을 잡는다.

판정식을 `return true;`로 바꾸면 이 검사들이 한꺼번에 깨진다.

---

## 결론

DX12 의 *Timestamp Query Ring Buffer + fence 완료 판정 + 카운터 +
InfoQueue1* 측정 인프라가 완성된다. GPU timestamp는 명령 기록 시각이 아니라
queue에서 실제 실행된 두 지점의 차이를 재며, CPU는 완료된 slot만 읽는다.

OpenGL 의 `GL_TIME_ELAPSED` 와 *근본 동일* 한 패턴. API 차이는 *명시적 ring buffer + heap 관리* 만.

## 더 읽을거리

- Microsoft Learn, [Timing (Direct3D 12 Queries)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing) — 타임스탬프 쿼리·주파수 보정의 공식 문서.
- Microsoft Learn, [ID3D12InfoQueue1](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/nn-d3d12sdklayers-id3d12infoqueue1) — 디버그 레이어 콜백 명세.
- [PIX Timing Captures](https://devblogs.microsoft.com/pix/timing-captures/) — 프레임 전체의 queue 실행과 병목을 분석하는 표준 도구.
