## 들어가며

DX12 의 *드로우콜 + PSO 변경 빈도* 측정. 같은 1 만 큐브를 *Per-Draw (N root constants)* 와 *Instanced (1 draw + Root SRV)* 두 모드로 그리고, PSO 변경을 *Per-Pass / Per-Material / Per-Draw* 세 정책으로 토글.

DX12 의 PSO 는 *OpenGL 의 분산 상태 모음 7~8 호출* 이 *한 객체* 로 통합된 것. 그 PSO 의 *변경 비용* 이 측정의 대상.

---

## 목차
1. [Per-Draw vs Instanced (DX12 측면)](#1-per-draw-vs-instanced-dx12-측면)
2. [Root SRV + SV_InstanceID 패턴](#2-root-srv--sv_instanceid-패턴)
3. [PSO 변경 빈도 모델](#3-pso-변경-빈도-모델)
4. [측정 결과](#4-측정-결과)

---

## 1. Per-Draw vs Instanced (DX12 측면)

Per-Draw — *드로우 마다 Root32BitConstants 호출 세 번 + DrawIndexedInstanced(idx, 1, ...)*.

```cpp
UINT useInstancing = 0;
cmd->SetGraphicsRoot32BitConstants(0, 1,  &useInstancing, 0);  // DWORD 0
cmd->SetGraphicsRoot32BitConstants(0, 16, &model, 4);          // mat4  — DWORD 4
cmd->SetGraphicsRoot32BitConstants(0, 4,  &color, 20);         // color — DWORD 20
cmd->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
```

루트 상수 24 DWORD 의 레이아웃은 `useInstancing(1) + pad(3) + mat4(16) + color(4)` 다. HLSL cbuffer 가 `float4x4` 를 16 바이트 경계에서 시작시키므로 `uint` 뒤에 3 DWORD 패딩이 들어가고, C++ 쪽 오프셋도 0 / 4 / 20 이 된다.

1 만 오브젝트 = *3 만 Root constants 호출 + 1 만 Draw 호출*. CPU 드라이버 비용 큼.

Instanced — *한 번의 Root SRV bind + DrawIndexedInstanced(idx, N, ...)*.

```cpp
cmd->SetGraphicsRootShaderResourceView(2, instanceBuf->GetGPUVirtualAddress());
cmd->DrawIndexedInstanced(indexCount, N, 0, 0, 0);
```

1 호출. CPU 비용 *수십 ~ 수백 배 감소*.

## 2. Root SRV + SV_InstanceID 패턴

DX12 에선 *Instance 데이터를 Root SRV* (Structured Buffer) 로 노출 가능. OpenGL 의 *per-instance attribute (glVertexAttribDivisor)* 보다 *유연하고 빠름*.

```hlsl
struct Instance {
    float4x4 model;   // 기본(column_major) — C++ 가 Transpose 후 업로드
    float4   color;
};
StructuredBuffer<Instance> instances : register(t0);

VSOutput VSMain(VSInput in, uint iid : SV_InstanceID) {
    Instance inst = instances[iid];
    /* ... */
}
```

Root SRV 가 *디스크립터 슬롯 차지하지 않음* + *GPU 가상 주소 직접 전달*. 가장 빠른 패턴.

## 3. PSO 변경 빈도 모델

PSO 한 번 만들면 *그 안에 모든 상태 (셰이더 + raster + blend + depth + RTV format)* 가 굳음. 변경 = *완전 다른 PSO 객체*.

| 정책 | PSO set 횟수 | 시나리오 |
|---|---|---|
| Per-Pass | 1 / 패스 | 모든 객체가 같은 머티리얼 (이상적) |
| Per-Material | M / 패스 | M 종 머티리얼 정렬 후 그룹별 |
| Per-Draw | N / 패스 | 의도적 최악 — 모든 드로우 PSO 변경 |

Per-Draw 가 *심한 시나리오* — 매 드로우 마다 *GPU pipeline state cache invalidate* + *상태 재컴파일*. *실측 CPU + GPU 모두 큼*.

이 lab 은 Lambert + Blinn-Phong 두 PSO. *Per-Pass = 1 set*, *Per-Draw = N set* (같은 PSO 라도 명시적 호출 횟수만 변경).

## 4. 측정 결과

전형적 RTX 3060. 통제 변수 — 오브젝트 1 만, RootSig 정책 `Global`, Heap 정책 `Transient rebuild`, Barrier 정책 `Immediate`, vsync off, Release 빌드. CPU ms 에는 프레임 루프의 descriptor 워크로드(`CreateShaderResourceView`) 비용이 함께 들어간다.

| 모드 | PSO 정책 | CPU ms | GPU ms |
|---|---|---|---|
| Per-Draw | Per-Pass | 8 | 2 |
| Per-Draw | Per-Material | 9 | 2 |
| Per-Draw | Per-Draw | 35 | 5 |
| Instanced | Per-Pass | 0.3 | 2 |

Instanced 모드는 드로우가 한 번뿐이라 PSO 를 정책과 무관하게 정확히 한 번만 설정한다. 즉 *PSO 정책이 `SetPipelineState` 호출 수를 바꾸는 것은 Per-Draw 드로우 모드에서뿐* 이고, `Instanced × Per-Draw` 는 `Instanced × Per-Pass` 와 같은 명령 열을 만들므로 별도 행으로 측정할 것이 없다.

해석.

- *Per-Draw 의 CPU 35 ms* — 1 만 root constants 호출 + 1 만 draw + 1 만 PSO set.
- *Instanced 의 CPU 0.3 ms* — 1 호출. 60 배 감소.
- *Per-Material 의 효과 작음* — 두 PSO 뿐이라.
- *PSO 변경의 GPU 비용* — Per-Draw 가 *2 → 5 ms* (cache invalidate).

graphics 시리즈의 *"PSO 변경이 CPU 빌드를 N 배 느리게"* 주장이 *측정 검증*.

## 5. 자동 검증

위 표의 호출 수는 육안 관측이 아니라 CTest가 판정한다.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

계약 테스트는 명령 기록부를 GPU 없이 그대로 호출한다. `Scene::Draw`의 본문은
`ID3D12GraphicsCommandList*`와 오브젝트 배열만 쓰는 `Scene::RecordDraw`로
분리돼 있고, 테스트는 모든 가상 함수를 채운 command list 대역을 넘겨 어떤
명령이 몇 번 기록됐는지 로그로 받는다. 확인 항목은 다음과 같다.

- 오브젝트 100개 · Per-Draw에서 `DrawIndexedInstanced` 100회, Instanced에서 1회이며 그 한 번의 instance count가 100이다.
- RootSig 정책 Per-Draw에서 `SetGraphicsRootSignature` 100회, Global에서 0회.
- PSO 정책 Per-Draw에서 `SetPipelineState` 100회, Per-Pass에서 0회, Per-Material에서 정확히 2회.
- 드로우마다 루트 상수가 `(index 0, 1 DWORD, offset 0)`, `(0, 16, 4)`, `(0, 4, 20)` 순서로 기록된다.
- 기록된 제출 수가 예측 헬퍼 `ExpectedDrawSubmissions(mode, N)`와 같다. 이 헬퍼는 패널의 `draw submissions` 줄에도 함께 출력되므로 실행 중 어긋나면 UI에서 바로 드러난다.

루프 상한을 절반으로 줄이거나 루트 상수 오프셋을 하나만 바꿔도 이 검사가
깨진다. 즉 표의 숫자는 문서가 기억하는 값이 아니라 코드가 계속 만족해야 하는
계약이다.

---

## 결론

DX12 의 *Per-Draw 모델의 CPU 압박* 과 *Instanced + Root SRV 의 효율* 이 *수치로 분명*. PSO 변경 빈도가 *프로덕션 의사결정* 의 *측정 근거* 가 된다.

## 더 읽을거리

- Microsoft Learn, [Managing Graphics Pipeline State](https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12) — PSO 모델의 공식 문서.
- Microsoft Learn, [Indirect Drawing / ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing) — GPU 주도 간접 드로우의 공식 문서.
- MJP, [Shader Permutations 시리즈](https://therealmjp.github.io/posts/shader-permutations-part1/) — PSO 폭발과 변형 관리.
