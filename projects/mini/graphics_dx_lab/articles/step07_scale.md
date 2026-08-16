## 들어가며

DX12 의 *오브젝트 스케일링* — 1k / 10k / 100k. graphics_gl_lab Step 7 과 *동일 측정 차원*, 단 *DX12 의 명시적 명령 모델 위에서*.

시작 상태 — 오브젝트 수가 `Scene::Init` 이 정한 10,000 으로 고정돼 있다. 종료 상태 — 패널의 `object count` 콤보가 세 규모를 오가며 장면을 다시 만들고, 그 사이 in-flight 자원을 안전하게 교체한다.

## 이 step 이 추가·변경하는 파일

| 파일 | 상태 | 역할 |
|---|---|---|
| `src/scene/scene.h` · `scene.cpp` | 변경 | `Rebuild` — 격자 재생성 + 인스턴스 버퍼 재확보 |
| `src/app/app.h` | 변경 | `objectCount_` / `pendingCount_` 두 값 |
| `src/app/app.cpp` | 변경 | 프레임 시작의 `Flush` + `Rebuild` |
| `src/ui/panel.cpp` | 변경 | `object count` 콤보 |

---

## 목차
1. [Scene Rebuild 의 GPU 핸들 정리](#1-scene-rebuild-의-gpu-핸들-정리)
2. [bottleneck 이동](#2-bottleneck-이동)
3. [DX12 vs OpenGL 의 비교](#3-dx12-vs-opengl-의-비교)
4. [규모 전환의 자동 검증](#4-규모-전환의-자동-검증)

---

## 1. Scene Rebuild 의 GPU 핸들 정리

`Scene::Rebuild(newCount)` 가 *기존 GPU 자원 해제 + 재할당*. *주의 — GPU 가 그 자원을 아직 쓰는 중일 수 있음*. *Fence 완료 후* 안전.

<!-- snippet: src/scene/scene.cpp#Scene::Rebuild -->
```cpp
void Scene::Rebuild(int objectCount, D3D12Context& ctx,
                     ID3D12GraphicsCommandList* /*uploadCmd*/,
                     std::vector<ComPtr<ID3D12Resource>>& /*keepAlive*/) {
    BuildObjects(objectCount);
    EnsureInstanceBuffer(ctx);
}
```

메시는 다시 올리지 않는다 — 큐브 정점·인덱스는 개수와 무관하다. 바뀌는 것은 오브젝트 배열과, 그 배열을 담을 인스턴스 업로드 버퍼뿐이다. `EnsureInstanceBuffer` 는 용량이 모자랄 때만 기존 버퍼를 `Unmap` 하고 새로 만든다.

바로 그 교체가 위험 지점이다. 이 lab 의 구현은 *프레임 시작 시 `Flush()` 로 이전 frame-in-flight 완료를 보장한 뒤 Rebuild* 한다. 수 ms 의 frame hitch 를 감수하는 대신 in-flight 자원을 교체하는 문제를 없앤다.

<!-- snippet: src/app/app.cpp@scene-rebuild -->
```cpp
if (pendingCount_ != objectCount_) {
    objectCount_ = pendingCount_;
    // Instance upload buffer 를 교체할 수 있으므로 이전 frame-in-flight 사용이
    // 끝났음을 보장한 뒤 재빌드한다.
    ctx_.Flush();
    scene_.Rebuild(objectCount_, ctx_, ctx_.CmdList(), uploadKeepAlive_);
}
```

값이 둘인 이유가 여기 있다. UI 는 `pendingCount_` 만 건드리고, 실제 교체는 프레임 경계에서 한 번 일어난다. 콤보를 조작하는 그 자리에서 바로 재빌드하면 이미 기록 중인 command list 가 죽은 버퍼를 가리키게 된다.

<!-- snippet: src/ui/panel.cpp@scale-combo -->
```cpp
ImGui::SeparatorText("Scale (Step 7)");
{
    const char* countNames[] = { "1,000", "10,000", "100,000" };
    const int   counts[]     = { 1000, 10000, 100000 };
    int idx = 1;
    for (int i = 0; i < 3; ++i) if (counts[i] == pendingObjectCount) idx = i;
    if (ImGui::Combo("object count", &idx, countNames, IM_ARRAYSIZE(countNames)))
        pendingObjectCount = counts[idx];
    ImGui::TextDisabled("(다음 프레임 시작 시 Flush 로 GPU 완료를 보장한 뒤 Scene 재빌드)");
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

측정 절차 — Release 빌드 · vsync off 로 실행하고, 콤보를 바꾼 *직후 한두 프레임은 버린다*. 그 프레임에는 `Flush()` 대기와 버퍼 재할당이 함께 들어 있어 정상 상태 값이 아니다.

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

## 4. 규모 전환의 자동 검증

`Scene::Rebuild` 는 두 가지를 한다 — `BuildObjects` 로 격자를 다시 만들고, `EnsureInstanceBuffer` 로 인스턴스 업로드 버퍼를 다시 확보한다. 뒤엣것은 `ID3D12Device` 를 요구하므로 GPU 없이 부를 수 없다. 앞엣것은 순수 CPU 단계라 제품 코드를 그대로 돌릴 수 있고, 버퍼 교체 쪽도 *용량 밖을 쓰지 않는다* 는 계약만은 저장소를 대역으로 물려 확인할 수 있다.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release -R graphics_dx_step07 --output-on-failure
build\Release\graphics_dx_contract_tests.exe step07
```

```text
1/1 Test #8: graphics_dx_step07_scale .........   Passed    0.04 sec

100% tests passed out of 1

  [step07] scene_rebuild_scales                        30 checks
  [step07] scene_rebuild_is_repeatable                 304 checks
  [step07] instance_storage_capacity_clamp             82 checks
step07: 3 groups, 416 checks, 0 failures
DirectX contract tests passed
```

등록은 이 한 줄이다. 실행 파일은 lab 이 원래 갖고 있던 계약 테스트 하나뿐이고, 인자가 이 규모 축의 그룹만 고른다.

<!-- snippet: CMakeLists.txt@step07-target -->
```cmake
add_test(NAME graphics_dx_step07_scale        COMMAND graphics_dx_contract_tests step07)
```

`scene_rebuild_scales` 는 콤보가 오가는 세 규모를 실제로 만든 뒤 되돌아온다. 마지막에 다시 1,000 으로 내려가는 것이 요점이다 — 재빌드가 이전 격자를 지우지 않으면 오브젝트 수가 누적돼 그 자리에서 드러난다.

<!-- snippet: tests/graphics_dx_contract_tests.cpp#TestSceneRebuildScales -->
```cpp
void TestSceneRebuildScales() {
    dxlab::Scene scene;
    const int scales[] = {1000, 10000, 100000, 1000};
    for (int count : scales) {
        scene.BuildObjects(count);
        CHECK(scene.ObjectCount() == count);
        CHECK(dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::PerDraw, count) ==
              count);
        CHECK(dxlab::ExpectedDrawSubmissions(dxlab::DrawMode::Instanced, count) == 1);

        FakeCmdList cmd;
        dxlab::GlCounters counters{};
        scene.RecordDraw(&cmd, dxlab::DrawMode::Instanced,
                         dxlab::RootSigPolicy::Global,
                         dxlab::PsoSwitchPolicy::PerPass, FakeRootSig(),
                         FakePsoA(), nullptr, counters);
        CHECK(cmd.draws.size() == 1);
        if (!cmd.draws.empty()) CHECK(cmd.draws[0].instanceCount == (UINT)count);
        CHECK(counters.instancedDraws == 1);
        CHECK(counters.drawCalls == 0);
    }
    // ... Per-Draw 로도 한 번 돌려 드로우 수가 규모와 같은지 본다.
}
```

위 표에서 `100k + Instanced` 만 CPU 0.6 ms 로 남는 근거가 이 루프의 세 번째 바퀴다. 규모가 100,000 으로 커져도 `DrawIndexedInstanced` 는 한 번이고, 늘어나는 것은 그 한 번의 instance count 뿐이다.

두 번째 그룹은 교체 순간의 위험을 본다. 규모를 키우면 격자는 즉시 커지지만 인스턴스 버퍼는 아직 이전 용량일 수 있다. 그 틈에 기록부가 용량 밖을 쓰면 남의 메모리를 밟는다.

<!-- snippet: tests/graphics_dx_contract_tests.cpp#TestInstanceStorageCapacityClamp -->
```cpp
void TestInstanceStorageCapacityClamp() {
    const size_t capacity = 16;
    const int objectCount = 64;
    const float sentinel = -12345.0f;
    std::vector<dxlab::Instance> storage((size_t)objectCount);
    for (auto& inst : storage) inst.model.m[3][3] = sentinel;

    dxlab::Scene scene;
    scene.BindInstanceStorage(storage.data(), capacity);
    scene.BuildObjects(objectCount);    // 알린 용량보다 큰 격자.
    // ...
    // 앞 capacity 칸은 채워졌고(alpha 1), 뒤 여유 칸은 손대지 않았다.
    for (size_t i = 0; i < capacity; ++i) {
        CHECK(NearF(storage[i].color.w, 1.0f));
        CHECK(NearF(storage[i].model.m[3][3], 1.0f));
    }
    for (size_t i = capacity; i < storage.size(); ++i)
        CHECK(NearF(storage[i].model.m[3][3], sentinel));
    // ...
}
```

저장소를 오브젝트 수만큼 잡되 *용량은 그보다 작게* 알리는 것이 설계다. 기록부의 클램프를 빼면 뒤 48 칸의 sentinel 이 덮여 판정이 48 건 깨진다 — 실제 버퍼 밖을 밟아 죽는 대신 어디까지 썼는지가 그대로 남는다.

세 번째 그룹 `scene_rebuild_is_repeatable` 은 1,000 → 10,000 → 1,000 으로 왕복한 뒤 앞뒤 격자의 model 행렬과 색을 대조한다. 고정 시드가 실제로 같은 장면을 내야 §2 의 표를 재측정할 수 있기 때문이다.

남는 것은 GPU 자원 교체 자체다. `Flush()` 가 실제로 in-flight 를 비웠는지, `EnsureInstanceBuffer` 가 옛 버퍼를 `Unmap` 했는지는 디바이스가 있어야 관측되므로 위 판정 밖에 있다. 그 부분은 실행 후 패널에서 `object count` 를 왕복시키며 Debug Layer 경고가 없는지로 확인한다.

---

## 결론

`graphics_dx_lab` 은 *DX12 명시적 호출 패턴* 을 한 화면에서 바꿔 보는 형태로 마무리된다. PSO/RootSig/Barrier 는 실제 명령 호출 수에 반영되고, Descriptor Heap 은 heap lifetime 모델까지 다룬다. 완전한 bindless 텍스처링과 render graph barrier 최적화는 더 큰 장면에서 다룰 주제다.

이 step 의 한계 — 재빌드가 `Flush()` 로 GPU 를 완전히 비우는 방식이라 콤보를 바꾼 프레임에 눈에 띄는 hitch 가 생긴다. 실제 스트리밍 렌더러가 쓰는 지연 파괴 큐는 이 lab 에 없다.

## 더 읽을거리

- NVIDIA, [DX12 Do's and Don'ts](https://developer.nvidia.com/dx12-dos-and-donts) — 스케일 시나리오에서의 명시적 모델 활용 지침.
- [DirectX Graphics Samples](https://github.com/microsoft/DirectX-Graphics-Samples) — ExecuteIndirect·멀티스레드 기록 등 다음 단계 예제.
- Microsoft Learn, [Multi-engine synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization) — 큐 병렬화의 공식 문서.
