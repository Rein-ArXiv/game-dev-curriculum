## 들어가며

DX12 의 *RootSignature 변경 비용* 측정. PSO 와 유사하지만 *RootSig 가 더 무거움* — 변경 시 *모든 root parameter 의 GPU 측 캐시 invalidate*.

이 lab 의 단일 RootSig 시나리오에서 *Global (1 set)* vs *Per-Draw (N set)* 두 정책 비교.

---

## 목차
1. [RootSignature 가 무엇을 묶는가](#1-rootsignature-가-무엇을-묶는가)
2. [Global vs Per-Draw 정책](#2-global-vs-per-draw-정책)
3. [측정 결과](#3-측정-결과)

---

## 1. RootSignature 가 무엇을 묶는가

이 lab 의 RootSignature:

| 슬롯 | 종류 | 셰이더 레지스터 | 용도 |
|---|---|---|---|
| 0 | Root Constants (24 DWORD) | b0 | useInstancing(1) + pad(3) + mat4(16) + color(4) — cbuffer 패킹상 float4x4 가 16바이트 경계에서 시작해 패딩 3 DWORD 포함 |
| 1 | Root CBV | b1 | FrameCB |
| 2 | Root SRV | t0 | Instance buffer |
| 3 | Descriptor Table | t1~t4 | 예약 슬롯 — 이 lab 은 어디에서도 바인딩하지 않는다 |
| Static Sampler | linear-wrap | s0 | |

루트 시그니처 크기 = 24(상수) + 2(Root CBV) + 2(Root SRV) + 1(테이블) = *29 DWORD*. *64 DWORD 예산* 안. (Static sampler 는 예산에 안 들어감.)

## 2. Global vs Per-Draw 정책

Global — 패스 시작 시 *한 번* `SetGraphicsRootSignature(rs)`. 모든 드로우 같은 RS 공유.

Per-Draw — 각 드로우 직전에 *같은 RS 를 다시 set*. 실제로는 *no-op 캐시 hit* 되지만 CPU 호출 비용은 발생.

```cpp
for (object) {
    cmd->SetGraphicsRootSignature(rs);    // 매번
    // ... draws ...
}
```

*드라이버는 같은 RS pointer 가 들어오면 보통 fast-path* 로 빠르게 처리하지만, *호출 비용은 0 이 아님*. 1 만 호출 = *~1 ms CPU*.

## 3. 측정 결과

통제 변수 — 오브젝트 1 만, 드로우 모드 `Per-Draw`, PSO 정책 `Per-Pass`, Heap 정책 `Transient rebuild`, Barrier 정책 `Immediate`, vsync off, Release 빌드.

| 정책 | `SetGraphicsRootSignature` 호출 수 | CPU ms |
|---|---|---|
| Global | 1 | 8 |
| Per-Draw | 10,001 | 9 |

프레임 시작의 1 회는 정책과 무관하게 항상 발생한다 — 루트 인자(b0 상수, b1 CBV, t0 SRV)를 설정하려면 루트 시그니처가 먼저 바인딩돼 있어야 하기 때문이다. Per-Draw 는 그 위에 드로우마다 한 번씩 더 얹는다.

Instanced 모드에는 드로우가 한 번뿐이라 이 정책이 호출 수를 1 → 2 로만 바꾼다. 측정 가능한 CPU 차이가 나오는 것은 Per-Draw 드로우 모드뿐이다.

차이가 *작은 이유* — 같은 RS 라 *드라이버 fast-path*. 다른 RS 면 *훨씬 큰 차이*.

실제 게임은 *2~5 종 RootSig* (Graphics / Compute / Shadow / Post-process 등) 운영. 그 사이 전환이 *밀집* 하면 측정 가능한 영향.

graphics 시리즈의 *"RootSig 를 패스별로만 바꾸기"* 권장이 *이 측정의 일반화*.

---

## 결론

RootSignature 변경 비용은 *동일 RS 의 cache hit 시* 작음. *다른 RS* 로 전환 시 비용 큼. 실전 — *RS 종류를 최소화 + 정렬 후 패스별로만 변경*.

## 더 읽을거리

- Microsoft Learn, [Root Signatures](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures) — 64 DWORD 예산·루트 파라미터 종류별 비용.
- NVIDIA, [DX12 Do's and Don'ts](https://developer.nvidia.com/dx12-dos-and-donts) — 자주 바뀌는 파라미터를 앞 슬롯에 두는 규칙 등 벤더 지침.
- AMD GPUOpen, [Performance Guides](https://gpuopen.com/performance/) — RDNA 의 루트 시그니처·디스크립터 권고.
