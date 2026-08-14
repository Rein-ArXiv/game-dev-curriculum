## 들어가며

DX12 의 *드로우콜 + PSO 변경 빈도* 측정. 같은 1 만 큐브를 *Per-Draw (N root constants)* 와 *Instanced (1 draw + Root SRV)* 두 모드로 그리고, PSO 변경을 *Per-Pass / Per-Material / Per-Draw* 세 정책으로 토글.

DX12 의 PSO 는 *OpenGL 의 분산 상태 모음 7~8 호출* 이 *한 객체* 로 통합된 것. 그 PSO 의 *변경 비용* 이 측정의 대상.

시작 상태 — 고정된 한 가지 드로우 경로 + CPU/GPU ms 측정. 종료 상태 — 패널의 두 콤보(`draw`, `pso`)가 실제 기록되는 명령 열을 바꾸고, 카운터가 그 수를 보여 준다.

## 이 step 이 추가·변경하는 파일

| 파일 | 상태 | 역할 |
|---|---|---|
| `src/scene/scene.h` | 변경 | `DrawMode`, `ExpectedDrawSubmissions`, `Instance`, `ComputeInstanceModel`, `RecordDraw` 선언 |
| `src/scene/scene_record.cpp` | 변경 | 세 기록 경로(instanced / per-material / per-draw) |
| `src/scene/scene.cpp` | 변경 | `EnsureInstanceBuffer` — Instance 업로드 버퍼 |
| `src/pso/pso_cache.h` · `.cpp` | 변경 | `PsoSwitchPolicy` 열거와 이름 |
| `src/app/app.cpp` | 변경 | per-pass PSO 설정, `Scene::Draw` 호출 인자 |
| `src/ui/panel.cpp` | 변경 | `draw` / `pso` 콤보 + 카운터 출력 |
| `tests/graphics_dx_contract_tests.cpp` | 변경 | 기록 수 계약 |
| `CMakeLists.txt` | 변경 | 계약 테스트 소스에 `src/scene/scene_record.cpp` 추가 |

---

## 목차
1. [Per-Draw vs Instanced (DX12 측면)](#1-per-draw-vs-instanced-dx12-측면)
2. [Root SRV + SV_InstanceID 패턴](#2-root-srv--sv_instanceid-패턴)
3. [PSO 변경 빈도 모델](#3-pso-변경-빈도-모델)
4. [측정 결과](#4-측정-결과)
5. [자동 검증](#5-자동-검증)

---

## 1. Per-Draw vs Instanced (DX12 측면)

기록부는 `Scene::RecordDraw` 하나다. `ID3D12GraphicsCommandList*` 와 오브젝트 배열만 쓰므로 디바이스가 없어도 호출할 수 있고, 그래서 §5 의 계약 테스트가 이 함수를 그대로 부른다.

<!-- snippet: src/scene/scene.h#Scene -->
```cpp
    void RecordDraw(ID3D12GraphicsCommandList* cmd,
                    DrawMode mode,
                    RootSigPolicy rsPolicy,
                    PsoSwitchPolicy psoPolicy,
                    ID3D12RootSignature* rootSig,
                    ID3D12PipelineState* primaryPso,
                    ID3D12PipelineState* secondaryPso,
                    GlCounters& counters);
```

Per-Draw — *드로우 마다 Root32BitConstants 호출 세 번 + DrawIndexedInstanced(idx, 1, ...)*.

<!-- snippet: src/scene/scene_record.cpp@per-draw-loop -->
```cpp
for (size_t i = 0; i < objects_.size(); ++i) {
    setRootSigForDraw();
    setPsoForDraw(i);
    UINT useInstancing = 0;
    cmd->SetGraphicsRoot32BitConstants(0, kRootConstUseInstancingCount,
                                       &useInstancing,
                                       kRootConstUseInstancingOffset);
    counters.rootConstantUpdates++;
    const auto& o = objects_[i];
    XMMATRIX m = ComputeInstanceModel(o.axis, o.angle, o.position);
    XMFLOAT4X4 mt;
    XMStoreFloat4x4(&mt, XMMatrixTranspose(m));
    cmd->SetGraphicsRoot32BitConstants(0, kRootConstModelCount, &mt,
                                       kRootConstModelOffset);
    float c[4] = { o.color.x, o.color.y, o.color.z, 1.0f };
    cmd->SetGraphicsRoot32BitConstants(0, kRootConstColorCount, c,
                                       kRootConstColorOffset);
    counters.rootConstantUpdates += 2;
    cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    counters.drawCalls++;
}
```

세 호출의 목적지 오프셋은 리터럴이 아니라 이름 붙은 상수다. 루트 상수 24 DWORD 의 레이아웃이 `useInstancing(1) + pad(3) + mat4(16) + color(4)` 이고, HLSL cbuffer 가 `float4x4` 를 16 바이트 경계에서 시작시키므로 `uint` 뒤에 3 DWORD 패딩이 들어간다. 그래서 오프셋이 0 / 4 / 20 이 된다.

<!-- snippet: src/rootsig/root_signature.h@root-const-slots -->
```cpp
constexpr UINT kRootConstUseInstancingOffset = 0;
constexpr UINT kRootConstUseInstancingCount  = 1;
constexpr UINT kRootConstModelOffset         = 4;
constexpr UINT kRootConstModelCount          = 16;
constexpr UINT kRootConstColorOffset         = 20;
constexpr UINT kRootConstColorCount          = 4;
constexpr UINT kRootConstTotalDwords         = 24;
```

같은 헤더가 루트 시그니처의 b0 크기를 선언할 때도 `kRootConstTotalDwords` 를 쓴다. 숫자를 두 곳에 적지 않는 것이 이 규약의 요점이다 — 하나만 바꾸면 셰이더가 엉뚱한 DWORD 를 읽는다.

1 만 오브젝트 = *3 만 Root constants 호출 + 1 만 Draw 호출*. CPU 드라이버 비용 큼.

Instanced — *한 번의 Root SRV bind + DrawIndexedInstanced(idx, N, ...)*.

<!-- snippet: src/scene/scene_record.cpp@instanced-path -->
```cpp
if (mode == DrawMode::Instanced) {
    setRootSigForDraw();
    if (psoPolicy == PsoSwitchPolicy::PerMaterial && primaryPso) {
        cmd->SetPipelineState(primaryPso);
        counters.pipelineSets++;
    } else {
        setPsoForDraw(0);
    }
    if (instanceBuf_) {
        cmd->SetGraphicsRootShaderResourceView(
            2, instanceBuf_->GetGPUVirtualAddress());
    }
    UINT useInstancing = 1;
    cmd->SetGraphicsRoot32BitConstants(0, kRootConstUseInstancingCount,
                                       &useInstancing,
                                       kRootConstUseInstancingOffset);
    counters.rootConstantUpdates++;
    cmd->DrawIndexedInstanced(indexCount_, (UINT)objects_.size(), 0, 0, 0);
    counters.instancedDraws++;
```

드로우 1 회 + 루트 상수 1 회 + Root SRV 1 회. CPU 비용이 *수십 ~ 수백 배 감소* 한다.

## 2. Root SRV + SV_InstanceID 패턴

DX12 에선 *Instance 데이터를 Root SRV* (Structured Buffer) 로 노출할 수 있다. OpenGL 의 *per-instance attribute (glVertexAttribDivisor)* 보다 유연하다 — 정점 입력 레이아웃을 건드리지 않는다.

<!-- snippet: shaders/cube.hlsl#VSMain -->
```hlsl
VSOutput VSMain(VSInput input, uint iid : SV_InstanceID) {
    VSOutput o;
    float4x4 m;
    float4   c;
    if (useInstancing == 1) {
        m = instances[iid].model;
        c = instances[iid].color;
    } else {
        m = model;
        c = color;
    }
    float4 worldPos = mul(float4(input.pos, 1.0), m);
    o.pos      = mul(worldPos, viewProj);
    o.worldPos = worldPos.xyz;
    o.normalWS = mul(float4(input.normal, 0.0), m).xyz;
    o.color    = c.rgb;
    return o;
}
```

두 경로가 *같은 셰이더* 를 쓴다. 분기는 루트 상수 첫 DWORD 하나다. 덕분에 PSO 도, 정점 레이아웃도 모드에 따라 달라지지 않아 *비교의 통제 변수* 가 유지된다.

Root SRV 는 디스크립터 슬롯을 차지하지 않고 *GPU 가상 주소를 직접* 받는다. 그 주소가 가리키는 버퍼는 Upload heap 에 영구 map 해 두고 매 프레임 덮어쓴다.

<!-- snippet: src/scene/scene.cpp#Scene::EnsureInstanceBuffer -->
```cpp
void Scene::EnsureInstanceBuffer(D3D12Context& ctx) {
    if (instanceCapacity_ >= objects_.size()) return;
    if (instanceBuf_) {
        instanceBuf_->Unmap(0, nullptr);
        instanceBuf_.Reset();
    }
    const size_t capacity = objects_.size();
    void* mapped = nullptr;
    instanceBuf_ = CreateBufferUpload(ctx.Device(),
                                       sizeof(Instance) * capacity,
                                       &mapped);
    BindInstanceStorage((Instance*)mapped, capacity);

    instanceVbv_.BufferLocation = instanceBuf_->GetGPUVirtualAddress();
    instanceVbv_.SizeInBytes    = (UINT)(sizeof(Instance) * instanceCapacity_);
    instanceVbv_.StrideInBytes  = sizeof(Instance);
}
```

업로드 루프는 기록부 맨 앞에 있다. 저장소가 연결되지 않은 헤드리스 경로에서는 통째로 건너뛰므로, 같은 함수가 GPU 유무와 무관하게 돈다.

<!-- snippet: src/scene/scene_record.cpp@instance-upload -->
```cpp
if (instanceMapped_ != nullptr) {
    const size_t writable = (std::min)(objects_.size(), instanceCapacity_);
    for (size_t i = 0; i < writable; ++i) {
        const auto& o = objects_[i];
        XMMATRIX m = ComputeInstanceModel(o.axis, o.angle, o.position);
        XMStoreFloat4x4(&instanceMapped_[i].model, XMMatrixTranspose(m));
        instanceMapped_[i].color = { o.color.x, o.color.y, o.color.z, 1.0f };
    }
}
```

두 경로가 같은 행렬을 만들어야 화면이 같다. 그래서 합성식을 함수 하나로 뽑아 두 경로가 공유한다.

<!-- snippet: src/scene/scene.h#ComputeInstanceModel -->
```cpp
inline DirectX::XMMATRIX ComputeInstanceModel(const DirectX::XMFLOAT3& axis,
                                               float angle,
                                               const DirectX::XMFLOAT3& position) {
    return DirectX::XMMatrixRotationAxis(DirectX::XMLoadFloat3(&axis), angle)
         * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
}
```

## 3. PSO 변경 빈도 모델

PSO 를 한 번 만들면 *그 안에 모든 상태 (셰이더 + raster + blend + depth + RTV format)* 가 굳는다. 변경 = *완전히 다른 PSO 객체*.

이 lab 은 픽셀 셰이더만 다른 두 PSO 를 만든다. 나머지 상태가 전부 같으므로 “PSO 를 바꾼다”가 곧 “셰이딩 모델을 바꾼다”가 된다.

<!-- snippet: src/pso/pso_cache.cpp#PsoCache::Init -->
```cpp
void PsoCache::Init(ID3D12Device* device,
                     ID3D12RootSignature* rootSig,
                     DXGI_FORMAT rtvFormat,
                     DXGI_FORMAT depthFormat) {
    ComPtr<ID3DBlob> vs, psL, psB;
    CompileShader(L"shaders/cube.hlsl", "VSMain", "vs_5_1", vs);
    CompileShader(L"shaders/cube.hlsl", "PSLambert",     "ps_5_1", psL);
    CompileShader(L"shaders/cube.hlsl", "PSBlinnPhong",  "ps_5_1", psB);

    D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSig;
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.InputLayout = { il, _countof(il) };
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.FrontCounterClockwise = TRUE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = rtvFormat;
    desc.DSVFormat     = depthFormat;
    desc.SampleMask = UINT_MAX;
    desc.SampleDesc.Count = 1;

    desc.PS = { psL->GetBufferPointer(), psL->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&psoLambert_)))) {
        throw std::runtime_error("CreateGraphicsPipelineState(Lambert) failed");
    }

    desc.PS = { psB->GetBufferPointer(), psB->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&psoBlinn_)))) {
        throw std::runtime_error("CreateGraphicsPipelineState(Blinn) failed");
    }
}
```

`desc` 를 한 번 채우고 `desc.PS` 만 바꿔 두 번 만든다 — PSO 가 상태 *전부* 를 굳힌다는 성질이 그대로 드러나는 코드다.

| 정책 | PSO set 횟수 | 시나리오 |
|---|---|---|
| Per-Pass | 1 / 패스 | 모든 객체가 같은 머티리얼 (이상적) |
| Per-Material | M / 패스 | M 종 머티리얼 정렬 후 그룹별 |
| Per-Draw | N / 패스 | 의도적 최악 — 모든 드로우 PSO 변경 |

정책은 두 곳에서 갈린다. `Per-Pass` 는 프레임 루프가 패스 시작에 한 번 설정한다.

<!-- snippet: src/app/app.cpp@pso-per-pass -->
```cpp
auto* primaryPso =
    useBlinnPhong_ ? psoCache_.BlinnPhong() : psoCache_.Lambert();
auto* secondaryPso =
    useBlinnPhong_ ? psoCache_.Lambert() : psoCache_.BlinnPhong();
if (psoPolicy_ == PsoSwitchPolicy::PerPass) {
    cmd->SetPipelineState(primaryPso);
    counters_.pipelineSets++;
}
```

`Per-Draw` 는 기록부의 람다가 드로우마다 설정한다. 짝수·홀수 드로우에 서로 다른 PSO 를 물려 *진짜로 파이프라인이 바뀌게* 한다 — 같은 포인터를 반복 설정하면 드라이버가 걸러낼 여지가 있기 때문이다.

<!-- snippet: src/scene/scene_record.cpp@draw-lambdas -->
```cpp
auto setRootSigForDraw = [&] {
    if (rsPolicy == RootSigPolicy::PerDraw && rootSig) {
        cmd->SetGraphicsRootSignature(rootSig);
        counters.rootSigSets++;
    }
};

auto setPsoForDraw = [&](size_t drawIndex) {
    if (psoPolicy == PsoSwitchPolicy::PerDraw && primaryPso) {
        ID3D12PipelineState* pso =
            (secondaryPso && (drawIndex & 1)) ? secondaryPso : primaryPso;
        cmd->SetPipelineState(pso);
        counters.pipelineSets++;
    }
};
```

Per-Draw 가 *심한 시나리오* 다 — 매 드로우마다 GPU 파이프라인 상태가 갈아 끼워진다. *실측 CPU + GPU 모두 큼*.

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
- 기록된 제출 수가 예측 헬퍼 `ExpectedDrawSubmissions(mode, N)`와 같다.

예측 헬퍼는 표에 적힌 숫자를 코드로 옮긴 것이다.

<!-- snippet: src/scene/scene.h#ExpectedDrawSubmissions -->
```cpp
inline int ExpectedDrawSubmissions(DrawMode mode, int objectCount) {
    if (objectCount <= 0) return 0;
    return mode == DrawMode::PerDraw ? objectCount : 1;
}
```

같은 헬퍼가 패널의 `draw submissions` 줄에도 함께 출력되므로 실행 중 어긋나면 UI 에서 바로 드러난다.

<!-- snippet: src/ui/panel.cpp@counter-readout -->
```cpp
ImGui::SeparatorText("Counters (this frame)");
{
    const int actualSubmissions = counters.drawCalls + counters.instancedDraws;
    const int expectedSubmissions =
        ExpectedDrawSubmissions(drawMode, scene.ObjectCount());
    ImGui::Text("draw submissions:%d (expected %d)%s",
                actualSubmissions, expectedSubmissions,
                actualSubmissions == expectedSubmissions ? "" : "  <-- MISMATCH");
}
ImGui::Text("draw calls:      %d", counters.drawCalls);
ImGui::Text("instanced draws: %d", counters.instancedDraws);
ImGui::Text("PSO sets:        %d", counters.pipelineSets);
ImGui::Text("RootSig sets:    %d", counters.rootSigSets);
ImGui::Text("Root constants:  %d", counters.rootConstantUpdates);
ImGui::Text("Heap sets:       %d", counters.descriptorHeapSets);
ImGui::Text("Descriptor writes:%d", counters.descriptorWrites);
ImGui::Text("Barriers:        %d", counters.barrierCalls);
```

루프 상한을 절반으로 줄이거나 루트 상수 오프셋을 하나만 바꿔도 이 검사가
깨진다. 즉 표의 숫자는 문서가 기억하는 값이 아니라 코드가 계속 만족해야 하는
계약이다.

---

## 결론

DX12 의 *Per-Draw 모델의 CPU 압박* 과 *Instanced + Root SRV 의 효율* 이 *수치로 분명*. PSO 변경 빈도가 *프로덕션 의사결정* 의 *측정 근거* 가 된다.

이 step 의 한계 — 머티리얼이 둘뿐이라 `Per-Material` 이 사실상 `Per-Pass` 두 번이다. 실제 씬의 머티리얼 정렬 비용도, PSO 개수가 수백 개로 늘었을 때의 캐시 압박도 이 장면에는 없다.

## 더 읽을거리

- Microsoft Learn, [Managing Graphics Pipeline State](https://learn.microsoft.com/en-us/windows/win32/direct3d12/managing-graphics-pipeline-state-in-direct3d-12) — PSO 모델의 공식 문서.
- Microsoft Learn, [Indirect Drawing / ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing) — GPU 주도 간접 드로우의 공식 문서.
- MJP, [Shader Permutations 시리즈](https://therealmjp.github.io/posts/shader-permutations-part1/) — PSO 폭발과 변형 관리.
