## 들어가며

DX12 의 *오브젝트 스케일링* — 1k / 10k / 100k. graphics_gl_lab Step 7 과 *동일 측정 차원*, 단 *DX12 의 명시적 명령 모델 위에서*.

---

## 목차
1. [Scene Rebuild 의 GPU 핸들 정리](#1-scene-rebuild-의-gpu-핸들-정리)
2. [bottleneck 이동](#2-bottleneck-이동)
3. [DX12 vs OpenGL 의 비교](#3-dx12-vs-opengl-의-비교)

---

## 1. Scene Rebuild 의 GPU 핸들 정리

`Scene::Rebuild(newCount)` 가 *기존 GPU 자원 해제 + 재할당*. *주의 — GPU 가 그 자원을 아직 쓰는 중일 수 있음*. *Fence 완료 후* 안전.

이 lab 의 구현 — *프레임 시작 시 `Flush()` 로 이전 frame-in-flight 완료를 보장한 뒤 Rebuild*. 수 ms 의 frame hitch 를 감수하는 대신 in-flight 자원을 교체하는 문제를 없앤다.

```cpp
if (pendingCount_ != objectCount_) {
    objectCount_ = pendingCount_;
    // Instance upload buffer 를 교체할 수 있으므로 이전 frame-in-flight 사용이
    // 끝났음을 보장한 뒤 재빌드한다.
    ctx_.Flush();
    scene_.Rebuild(objectCount_, ctx_, ctx_.CmdList(), uploadKeepAlive_);
}
```

`ComPtr` 의 *RAII* + *DXGI 의 deferred destroy* 가 *직접 충돌 회피*. 실 프로덕션은 *delete queue + N-frame later destroy* 패턴 사용.

## 2. bottleneck 이동

| 오브젝트 | Draw Mode | CPU ms | GPU ms |
|---|---|---|---|
| 1k | Per-Draw | 1 | 0.3 |
| 1k | Instanced | 0.1 | 0.3 |
| 10k | Per-Draw | 8 | 2 |
| 10k | Instanced | 0.3 | 2 |
| 100k | Per-Draw | 80 | 10 |
| 100k | Instanced | 0.6 | 12 |

해석.

- 100k + Per-Draw — *CPU 80 ms* — 실용 불가. *Instanced 가 필수*.
- 100k + Instanced — *CPU 0.6 ms*, *GPU 12 ms*. *처음으로 GPU bound* 시나리오.

graphics_gl_lab 과 *근본 동일* 한 측정 결과. API 차이는 *명시적 명령 모델로 인한 코드 복잡도* 만.

## 3. DX12 vs OpenGL 의 비교

같은 1 만 큐브.

| 항목 | OpenGL | DX12 |
|---|---|---|
| 코드 LOC (`src` + `shaders` 실측) | 2,542 | 2,340 |
| CPU ms (Individual / Per-Draw) | 30 | 8 |
| CPU ms (Instanced) | 0.5 | 0.3 |
| GPU ms | 2 | 2 |
| 디버깅 도구 | RenderDoc | RenderDoc + PIX + VS Graphics Debugger |
| 모바일 지원 | OpenGL ES | (Xbox Series 만) |

DX12 가 *명시적 모델로 CPU 효율 ↑*. OpenGL 의 *드라이버 자동 처리* 가 *어느 정도 비용 추가*.

학습 곡선은 *DX12 가 압도적으로 큼* — RootSig / PSO / Barrier / Heap / Fence 모두 명시.

---

## 결론

`graphics_dx_lab` Step 1~7 은 *DX12 명시적 호출 패턴* 을 한 화면에서 바꿔 보는 형태로 마무리된다. PSO/RootSig/Barrier 는 실제 명령 호출 수에 반영되고, Descriptor Heap/Bindless 는 heap lifetime 모델까지 다룬다. 완전한 bindless 텍스처링과 render graph barrier 최적화는 더 큰 장면에서 다룰 주제다.

다음은 *Vulkan* — *DX12 와 유사한 명시적 모델* 이지만 *Pipeline Barrier 의 sync/access/layout 세 축*, *Timeline Semaphore*, *Mesh Shader* 같은 *추가 명시화*. graphics_vk_lab 이 그 측정 토픽을 다룬다.

## 더 읽을거리

- NVIDIA, [DX12 Do's and Don'ts](https://developer.nvidia.com/dx12-dos-and-donts) — 스케일 시나리오에서의 명시적 모델 활용 지침.
- [DirectX Graphics Samples](https://github.com/microsoft/DirectX-Graphics-Samples) — ExecuteIndirect·멀티스레드 기록 등 다음 단계 예제.
- Microsoft Learn, [Multi-engine synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization) — 큐 병렬화의 공식 문서.
