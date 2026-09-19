## 들어가며

DX12 의 *RootSignature 변경 비용* 측정. PSO 와 유사하지만 *RootSig 가 더 무거움* — 변경 시 *모든 root parameter 의 GPU 측 캐시 invalidate*.

이 lab 의 단일 RootSig 시나리오에서 *Global (1 set)* vs *Per-Draw (N set)* 두 정책 비교.

시작 상태 — 루트 시그니처는 하나이고 프레임 시작에 한 번 바인딩된다. 종료 상태 — 패널의 `rootsig` 콤보가 `SetGraphicsRootSignature` 호출 수를 1 과 N+1 사이에서 바꾸고, 루트 상수 슬롯 규약이 컴파일 타임 단언으로 고정된다.

## 이 step 이 추가·변경하는 파일

| 파일 | 상태 | 역할 |
|---|---|---|
| `src/rootsig/root_signature.h` | 변경 | `RootSigPolicy` 열거 + 루트 상수 슬롯 상수 |
| `src/rootsig/root_signature.cpp` | 변경 | 파라미터 4 개 + static sampler 직렬화 |
| `src/scene/scene_record.cpp` | 변경 | `setRootSigForDraw` 람다가 정책을 읽는다 |
| `src/app/app.cpp` | 변경 | 프레임 시작의 RS 바인딩 + 루트 인자 순서 |
| `src/ui/panel.cpp` | 변경 | `rootsig` 콤보 |
| `tests/graphics_dx_contract_tests.cpp` | 변경 | 슬롯 규약 단언 + 정책별 호출 수 |

---

## 목차
1. [RootSignature 가 무엇을 묶는가](#1-rootsignature-가-무엇을-묶는가)
2. [Global vs Per-Draw 정책](#2-global-vs-per-draw-정책)
3. [측정 결과](#3-측정-결과)
4. [슬롯 규약의 자동 검증](#4-슬롯-규약의-자동-검증)

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

표를 코드로 옮기면 이렇다. 파라미터 배열을 채우고 버전 1.1 로 직렬화한 뒤 `CreateRootSignature` 에 넘긴다.

<!-- snippet: src/rootsig/root_signature.cpp#RootSignatureCache::Init -->
```cpp
void RootSignatureCache::Init(ID3D12Device* device) {
    D3D12_ROOT_PARAMETER1 params[4] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace  = 0;
    params[0].Constants.Num32BitValues = kRootConstTotalDwords;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace  = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;
    srvRange.BaseShaderRegister = 1;
    srvRange.RegisterSpace = 0;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    srvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &srvRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1.NumParameters = _countof(params);
    versioned.Desc_1_1.pParameters = params;
    versioned.Desc_1_1.NumStaticSamplers = 1;
    versioned.Desc_1_1.pStaticSamplers = &samp;
    versioned.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&versioned, &blob, &error);
    if (FAILED(hr)) {
        std::string msg = "SerializeRootSignature failed";
        if (error) msg += std::string(": ") + (const char*)error->GetBufferPointer();
        throw std::runtime_error(msg);
    }
    if (FAILED(device->CreateRootSignature(0,
            blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&root_)))) {
        throw std::runtime_error("CreateRootSignature failed");
    }
}
```

세 가지가 눈에 띈다.

- `Num32BitValues` 가 리터럴 24 가 아니라 `kRootConstTotalDwords` 다. 드로우가 쓰는 오프셋과 같은 헤더에서 온다.
- 직렬화 실패는 `error` 블롭에 사람이 읽을 수 있는 이유가 들어온다. 이걸 버리면 “왜 실패했는지 모르는 `E_INVALIDARG`”만 남는다.
- `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` 플래그가 없으면 정점 입력 레이아웃을 쓰는 PSO 생성이 거부된다.

## 2. Global vs Per-Draw 정책

Global — 패스 시작 시 *한 번* `SetGraphicsRootSignature(rs)`. 모든 드로우 같은 RS 공유.

Per-Draw — 각 드로우 직전에 *같은 RS 를 다시 set*. 실제로는 *no-op 캐시 hit* 되지만 CPU 호출 비용은 발생.

<!-- snippet: src/scene/scene_record.cpp@draw-lambdas -->
```cpp
auto setRootSigForDraw = [&] {
    if (rsPolicy == RootSigPolicy::PerDraw && rootSig) {
        cmd->SetGraphicsRootSignature(rootSig);
        counters.rootSigSets++;
    }
};
// ...
```

*드라이버는 같은 RS pointer 가 들어오면 보통 fast-path* 로 빠르게 처리하지만, *호출 비용은 0 이 아님*. 1 만 호출 = *~1 ms CPU*.

정책과 무관하게 프레임 시작에는 반드시 한 번 바인딩한다. 순서가 계약이다 — 루트 인자(b0 상수, b1 CBV, t0 SRV)는 *루트 시그니처가 바인딩된 뒤에만* 설정할 수 있고, `CommandList::Reset` 이 그 바인딩을 지운다.

<!-- snippet: src/app/app.cpp@rootsig-bind -->
```cpp
cmd->SetGraphicsRootSignature(rootSig_.Get());
counters_.rootSigSets++;

D3D12_GPU_VIRTUAL_ADDRESS cbAddr =
    frameCB_->GetGPUVirtualAddress() + f * sizeof(FrameCB);
cmd->SetGraphicsRootConstantBufferView(1, cbAddr);
```

FrameCB 주소가 프레임 슬롯마다 256 바이트의 배수만큼 밀려 있어야 하는 것에 주의한다. root CBV 주소는 `D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT`(256) 의 배수여야 하므로 슬롯 stride 는 구조체의 원래 크기(96 바이트)가 아니라 256 바이트 단위로 잡는다. 업로드 버퍼 하나를 프레임 수만큼 나눠 쓰므로, 프레임 슬롯마다 다른 구간을 가리켜야 in-flight 인 프레임의 상수를 덮어쓰지 않는다.

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

## 4. 슬롯 규약의 자동 검증

루트 상수 레이아웃은 *셰이더 · 루트 시그니처 · 드로우 기록* 세 곳이 동시에 만족해야 하는 규약이다. 셋 중 하나만 어긋나도 화면은 그럴듯하게 깨지고, 디버그 레이어는 아무 말도 하지 않는다. 그래서 예산에 정확히 맞는다는 것은 컴파일 타임에 못박고, 슬롯이 서로 겹치지 않는다는 것은 같은 테스트의 런타임 판정이 맡는다.

<!-- snippet: tests/graphics_dx_contract_tests.cpp#TestRootConstantBudget -->
```cpp
void TestRootConstantBudget() {
    static_assert(dxlab::kRootConstUseInstancingOffset +
                      dxlab::kRootConstUseInstancingCount <=
                  dxlab::kRootConstTotalDwords, "useInstancing 슬롯이 예산 밖");
    static_assert(dxlab::kRootConstModelOffset + dxlab::kRootConstModelCount <=
                  dxlab::kRootConstTotalDwords, "model 슬롯이 예산 밖");
    static_assert(dxlab::kRootConstColorOffset + dxlab::kRootConstColorCount ==
                  dxlab::kRootConstTotalDwords, "color 슬롯이 예산 끝과 불일치");
    // ...
}
```

`color` 만 `==` 인 것이 의도다. color 는 레이아웃의 마지막 슬롯이므로 *예산의 끝과 정확히 맞아야* 하고, 그렇지 않다면 24 라는 숫자와 실제 사용량이 갈라진 것이다.

정책별 호출 수도 같은 실행 파일이 판정한다. 루트 시그니처 몫만 도는 등록은 이 한 줄이다.

<!-- snippet: CMakeLists.txt@step04-target -->
```cmake
add_test(NAME graphics_dx_step04_rootsig      COMMAND graphics_dx_contract_tests step04)
```

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R graphics_dx_step04 --output-on-failure
build\Release\graphics_dx_contract_tests.exe step04
```

```text
1/1 Test #5: graphics_dx_step04_rootsig .......   Passed    0.04 sec

100% tests passed out of 1

  [step04] rootsig_policy_call_counts                  104 checks
  [step04] root_constant_budget                        5 checks
step04: 2 groups, 109 checks, 0 failures
DirectX contract tests passed
```

두 그룹이 각각 위 표와 앞 절의 단언을 맡는다. `rootsig_policy_call_counts` 의 104 판정이 확인하는 것은 둘이다.

- 오브젝트 100 개 · `RootSigPolicy::PerDraw` 에서 기록부의 `SetGraphicsRootSignature` 가 정확히 100 회이고, 모두 같은 포인터다.
- `RootSigPolicy::Global` 에서는 기록부가 루트 시그니처를 *한 번도* 건드리지 않는다(0 회). 표의 `Global = 1` 은 프레임 루프가 부르는 그 한 번이다.

<!-- snippet: tests/graphics_dx_contract_tests.cpp#TestRootSigPolicyCallCounts -->
```cpp
void TestRootSigPolicyCallCounts() {
    // Per-Draw -> 드로우마다 한 번, 전부 같은 루트 시그니처 포인터.
    RecordResult perDraw;
    RunRecord(perDraw, 100, dxlab::DrawMode::PerDraw,
              dxlab::RootSigPolicy::PerDraw, dxlab::PsoSwitchPolicy::PerPass);
    CHECK(perDraw.cmd.rootSigSets.size() == 100);
    for (auto* rs : perDraw.cmd.rootSigSets) CHECK(rs == FakeRootSig());
    CHECK(perDraw.counters.rootSigSets == 100);

    // Global -> 기록부는 루트 시그니처를 한 번도 건드리지 않는다.
    RecordResult global;
    RunRecord(global, 100, dxlab::DrawMode::PerDraw,
              dxlab::RootSigPolicy::Global, dxlab::PsoSwitchPolicy::PerPass);
    CHECK(global.cmd.rootSigSets.empty());
    CHECK(global.counters.rootSigSets == 0);
}
```

`RecordResult` 는 명령 기록을 남기는 command list 대역과 카운터를 함께 든 묶음이고, `RunRecord` 는 오브젝트 수와 세 정책을 받아 `Scene::RecordDraw` 를 한 번 돌린다. 두 번 돌리는 이유는 *정책 하나만 다르고 나머지가 같은* 두 실행을 나란히 놓아야 이 축의 효과가 분리되기 때문이다. 100 회 쪽의 포인터 비교가 있어야 “호출은 하는데 엉뚱한 시그니처를 물린다”가 통과하지 못한다.

---

## 결론

RootSignature 변경 비용은 *동일 RS 의 cache hit 시* 작음. *다른 RS* 로 전환 시 비용 큼. 실전 — *RS 종류를 최소화 + 정렬 후 패스별로만 변경*.

이 step 의 한계 — 시그니처가 하나뿐이라 *다른* RS 로 전환하는 비용은 이 lab 에서 측정되지 않는다. 표의 CPU 차이 1 ms 는 “같은 포인터 재설정”의 값이지 “전환”의 값이 아니다.

## 더 읽을거리

- Microsoft Learn, [Root Signatures](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures) — 64 DWORD 예산·루트 파라미터 종류별 비용.
- NVIDIA, [DX12 Do's and Don'ts](https://developer.nvidia.com/dx12-dos-and-donts) — 자주 바뀌는 파라미터를 앞 슬롯에 두는 규칙 등 벤더 지침.
- AMD GPUOpen, [Performance Guides](https://gpuopen.com/performance/) — RDNA 의 루트 시그니처·디스크립터 권고.
