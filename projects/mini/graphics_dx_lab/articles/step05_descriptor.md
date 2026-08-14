## 들어가며

이 단계는 shader-visible descriptor heap의 **갱신 수명**을 측정한다. 비교 대상은 두 가지다.

- `Transient rebuild`: frame slot 전용 구간에 같은 수의 descriptor를 매 프레임 다시 쓴다.
- `Persistent table`: descriptor를 한 번 쓰고 내용이 바뀌지 않는 동안 재사용한다.

현재 장면은 텍스처를 읽지 않는다. 따라서 null SRV 표를 사용해 `CreateShaderResourceView`의 CPU 비용만 분리한다. 이 실험은 bindless 렌더링의 GPU 성능을 측정하지 않으며, descriptor를 생성하는 정책과 descriptor를 셰이더에서 찾는 정책을 구분한다.

---

## 목차

1. [측정 계약](#1-측정-계약)
2. [Shader-visible heap의 수명](#2-shader-visible-heap의-수명)
3. [유효한 null descriptor workload](#3-유효한-null-descriptor-workload)
4. [Frame slot 분할](#4-frame-slot-분할)
5. [두 갱신 정책](#5-두-갱신-정책)
6. [프레임 루프 통합](#6-프레임-루프-통합)
7. [측정과 해석](#7-측정과-해석)
8. [Bindless와의 경계](#8-bindless와의-경계)
9. [검증](#9-검증)

---

## 1. 측정 계약

두 모드는 장면, 오브젝트 수, draw mode, PSO, Root Signature, barrier 정책을 동일하게 유지한다.

| 구분 | 값 |
|---|---|
| 조작 변수 | descriptor 갱신 정책 |
| 통제 변수 | 동일한 큐브 수와 렌더 경로 |
| 관측값 | `descriptorWrites`, CPU frame time |
| 기대 결과 | transient는 매 프레임 N회, persistent는 최초 또는 크기 변경 시에만 N회 |
| 실패 신호 | persistent 모드에서 매 프레임 write가 발생하거나 frame 구간이 겹침 |

GPU 시간은 이 실험의 핵심 관측값이 아니다. 셰이더가 workload descriptor를 읽지 않으므로 두 정책의 GPU 명령과 픽셀 결과는 같아야 한다.

## 2. Shader-visible heap의 수명

D3D12의 CBV/SRV/UAV descriptor heap은 CPU handle과 GPU handle을 함께 제공한다.

```cpp
D3D12_DESCRIPTOR_HEAP_DESC desc{};
desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
desc.NumDescriptors = capacity;
desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap));
```

CPU는 `CreateShaderResourceView` 같은 함수로 descriptor 내용을 쓴다. GPU는 command list에 설정된 heap과 root descriptor table을 통해 그 내용을 읽는다.

```cpp
ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
cmd->SetDescriptorHeaps(1, heaps);
```

중요한 제약은 **GPU가 참조할 수 있는 descriptor를 CPU가 덮어쓰면 안 된다**는 점이다. command list 기록이 끝났다는 사실은 GPU 사용이 끝났다는 뜻이 아니다. transient 영역은 frame fence와 같은 완료 추적 단위에 맞춰 분리해야 한다.

## 3. 유효한 null descriptor workload

리소스가 없는 null SRV도 view description을 완전히 지정하면 유효한 descriptor다. 이 성질을 이용하면 텍스처 메모리와 업로드 비용을 섞지 않고 descriptor write 호출만 반복할 수 있다.

```cpp
D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
srv.Texture2D.MipLevels = 1;

device->CreateShaderResourceView(nullptr, &srv, cpuHandle);
```

`nullptr`만 넘기고 `pDesc`까지 생략하는 것은 이 workload의 의도가 아니다. null resource에 대응하는 형식과 차원을 명시해야 셰이더가 접근했을 때 정의된 0 읽기 동작을 갖는 null descriptor가 된다.

현재 셰이더는 이 descriptor를 실제로 접근하지 않는다. 그러므로 이 코드는 다음 비용을 측정한다.

- API 호출과 런타임 검증
- CPU descriptor 인코딩
- heap 메모리에 descriptor를 쓰는 비용

다음 비용은 측정하지 않는다.

- texture sampling과 cache hit
- descriptor table 탐색의 GPU 비용
- material별 texture locality
- non-uniform indexing과 wave divergence

## 4. Frame slot 분할

heap을 나눌 때 흔히 빠지는 함정이 하나 있다. transient 구간만 생각해 용량을 frame 수로 나누면 persistent 표가 놓일 자리가 남지 않는다. 그러면 persistent 표는 결국 어떤 frame slot 위에 겹쳐 앉게 되고, 그 frame이 in-flight인 동안 CPU가 같은 descriptor를 덮어쓴다. 두 정책을 한 heap에서 토글하는 이 lab에서는 persistent 구간도 처음부터 예약 대상이다.

heap 용량을 `C`, 동시에 진행 가능한 frame 수를 `F`라 하면 구획은 `F + 1`개다 — transient `F`개와 persistent 1개.

$$
S = \left\lfloor \frac{C}{F + 1} \right\rfloor
$$

frame index가 `i`일 때 transient 구간은 `[iS, (i+1)S)`이고, persistent 구간은 그 뒤 전부, 즉 `[FS, C)`다.

```cpp
const UINT sliceSize = capacity / (framesInFlight + 1);
const UINT slot = frameIndex % framesInFlight;
const UINT first = slot * sliceSize;                 // transient
const UINT persistentFirst = framesInFlight * sliceSize;
const UINT persistentRoom  = capacity - persistentFirst;
```

`D3D12Context::BeginFrame`은 해당 frame slot의 fence 완료를 기다린 뒤 allocator와 command list를 재사용한다. descriptor 구간도 같은 slot에 묶으면 CPU가 그 구간을 다시 쓸 때 이전 GPU 작업은 완료된 상태다.

이 구현은 descriptor 수가 구간 용량을 넘으면 조용히 wrap하지 않고 `sliceSize`로 제한한다. 살아 있는 descriptor를 순환 덮어쓰는 구현은 fence 정보가 없는 단순 modulo allocator로 만들 수 없다.

## 5. 두 갱신 정책

### Transient rebuild

매 프레임 현재 slot의 구간을 다시 쓴다.

```cpp
if (policy == HeapPolicy::RebuildEachFrame) {
    const UINT slot = frameIndex % framesInFlight;
    WriteNullSrvs(device, slot * sliceSize, count);
    return count;
}
```

오브젝트 수가 10,000이면 `descriptorWrites`도 매 프레임 10,000이다. 이 정책은 descriptor 내용이 프레임마다 실제로 달라지는 workload의 상한에 가깝다.

### Persistent table

descriptor 수 또는 정책이 바뀔 때만 표를 만든다. 쓰는 위치는 transient 슬라이스 뒤의 예약 구간이다.

```cpp
if (lastPolicy_ != policy || persistentCount_ != count) {
    WriteNullSrvs(device, persistentFirst, count);
    persistentCount_ = count;
    lastPolicy_ = policy;
    return count;
}
return 0;
```

정적 texture와 sampler가 안정된 ID를 갖는다면 매 프레임 descriptor를 다시 쓸 이유가 없다. persistent 정책에서 오브젝트 수가 유지되는 동안 `descriptorWrites == 0`이 정상이다.

요청 수는 transient일 때 `S`로, persistent일 때 예약 구간 크기 `C - FS`로 잘린다. 이 lab의 실제 값(`C = 100000`, `F = 3`)에서는 `S = 25000`이므로 상한 32,768을 요청해도 25,000에서 멈춘다. 잘리는 것이 정상이다 — 살아 있는 남의 구간을 넘어가는 것보다 낫다.

## 6. 프레임 루프 통합

실험 descriptor 수는 장면 오브젝트 수와 연결하되 한 frame에 32,768개로 제한한다.

```cpp
const UINT descriptorWork =
    static_cast<UINT>(std::min(objectCount_, 32768));

counters_.descriptorWrites += static_cast<int>(
    heap_.PrepareNullSrvWorkload(
        ctx_.Device(),
        heapPolicy_,
        frameIndex,
        kFramesInFlight,
        descriptorWork));
```

카운터는 프레임 시작에 초기화된다. 따라서 패널에 보이는 값은 누적 합이 아니라 현재 프레임에서 발생한 실제 write 수다.

```cpp
ImGui::Text("Descriptor writes: %d", counters.descriptorWrites);
```

heap 자체의 바인딩 횟수는 두 정책에서 같다. `SetDescriptorHeaps` 횟수와 descriptor 내용 갱신 횟수는 서로 다른 변수다.

## 7. 측정과 해석

정확한 비교 절차는 다음과 같다.

1. Release 빌드와 VSync off를 사용한다.
2. draw mode, PSO, Root Signature, barrier 정책을 고정한다.
3. 오브젝트 수를 1k, 10k, 100k 순서로 바꾼다.
4. 각 구간에서 transient와 persistent를 번갈아 선택한다.
5. 첫 정책 전환 프레임은 초기화 비용이 있으므로 steady-state와 분리한다.

예상되는 정성적 결과는 다음과 같다.

| 정책 | 첫 프레임 write | 안정 상태 write | CPU 비용 경향 |
|---|---:|---:|---|
| Transient rebuild | N | N/frame | N에 따라 증가 |
| Persistent table | N | 0/frame | 초기화 후 거의 고정 |

절대 시간은 CPU, 드라이버, Debug Layer, 오브젝트 수에 따라 달라진다. 특정 하드웨어에서 얻은 수치를 다른 환경의 보장값처럼 쓰면 안 된다.

`Persistent table`이 항상 옳다는 뜻도 아니다. streaming, texture residency, 동적 view, transient render target은 descriptor 변경이 필요하다. 핵심은 변경 빈도에 맞는 영역을 분리하는 것이다.

## 8. Bindless와의 경계

Persistent descriptor table과 bindless는 같은 개념이 아니다.

- **Persistent table**: descriptor를 오래 유지하는 CPU 수명 정책이다.
- **Bindless**: 셰이더가 큰 descriptor 공간을 index로 선택하는 바인딩 모델이다.

SM 6.6의 직접 heap indexing은 root signature flag와 DXC shader model 계약이 필요하다.

```hlsl
Texture2D<float4> tex = ResourceDescriptorHeap[material.textureIndex];
float4 color = tex.Sample(linearSampler, uv);
```

```cpp
flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
```

이 실행 코드에는 SM 6.6 직접 인덱싱이 없다. 따라서 UI에서 `Bindless`라고 표시하거나 bindless의 GPU 성능을 측정했다고 결론 내리지 않는다.

## 9. 검증

빌드와 실행:

```powershell
cmake -S . -B build
cmake --build build --config Release
build\Release\graphics_dx_lab.exe
```

자동 검증 — 위 계획 수식(`PlanNullSrvWrites`)은 CTest가 검사한다.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

기대 출력은 `1/1 Test #1: graphics_dx_contract_tests .... Passed`와 `DirectX contract tests passed`다. 계약 테스트가 다음을 확인한다.

- Transient 모드의 frame slot 시작 오프셋과 `sliceSize` 상한.
- Persistent 모드의 최초 write 수가 N이고, 위치가 예약 구간 시작이다.
- 정책과 count가 그대로면 persistent write 수가 0이다.
- **구간 불변식** — `F`개 transient 구간이 서로 겹치지 않고, persistent 구간이 그 어느 것과도 겹치지 않으며, 모두 heap 용량 안에 있다. 위 §3 표의 실패 신호 “frame 구간이 겹침”을 육안이 아니라 CTest가 판정한다는 뜻이다. 앱이 실제로 쓰는 크기(`C = 100000`, `F = 3`, 요청 32,768)에 대해서도 같은 검사를 돌린다.
- **계획과 실행의 일치** — `PrepareNullSrvWorkload`가 계획한 위치·개수 그대로 descriptor 작성 지점을 호출하는지. 작성 지점이 virtual이라 테스트가 파생 클래스로 가로채 호출 로그를 본다. 이 검사가 있으면 “계획 수식은 맞는데 실행이 그걸 안 쓰는” 상태가 통과하지 못한다.

육안 체크리스트 — 실행 후 패널에서 확인한다.

- Transient 모드에서 `descriptorWrites`가 매 프레임 N이다.
- Persistent 모드의 첫 프레임은 N, 안정 상태는 0이다.
- 오브젝트 수를 바꾸면 persistent 표가 정확히 한 번 다시 작성된다.
- 두 정책의 화면 결과와 GPU draw counter가 같다.
- Debug Layer에 descriptor overwrite 또는 invalid view 경고가 없다.
- 100k 오브젝트에서도 write 수는 요청 상한 32,768과 구간 크기 25,000 중 작은 값, 즉 25,000을 넘지 않는다.

---

## 결론

Descriptor heap 최적화의 첫 질문은 “bindless인가”가 아니라 “어떤 descriptor가 얼마나 자주 바뀌는가”다. frame fence와 수명을 맞춘 transient 구간은 동적 view를 안전하게 다시 쓸 수 있고, persistent table은 정적 자원의 반복 write를 제거한다. 이 단계는 두 정책을 실제 API 호출 수로 분리하면서도 bindless 렌더링을 구현했다고 과장하지 않는다.

## 더 읽을거리

- Microsoft Learn, [Descriptor Heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps)
- Microsoft Learn, [Creating Descriptors](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptors)
- Microsoft DirectX Specs, [HLSL SM 6.6 Dynamic Resources](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html)
