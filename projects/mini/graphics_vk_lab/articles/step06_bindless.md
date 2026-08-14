## 들어가며

Step 6 은 Vulkan 의 descriptor indexing 을 다룬다. DX12 의 bindless 가 shader-visible descriptor heap 직접 인덱싱이라면, Vulkan 의 bindless 는 descriptor set layout flag, partially bound array, runtime descriptor array, update-after-bind 규칙의 조합이다.

이 lab 의 실행 코드는 bindless texture path 를 완성하지 않는다. 대신 Vulkan 에서 bindless 를 넣으려면 어떤 조건이 필요한지 정리한다.

---

## 목차

1. [Descriptor Set의 기본 모델](#1-descriptor-set의-기본-모델)
2. [Descriptor Indexing 조건](#2-descriptor-indexing-조건)
3. [Bindless가 필요한 순간](#3-bindless가-필요한-순간)
4. [이 lab의 범위](#4-이-lab의-범위)
5. [빌드와 실행 결과](#5-빌드와-실행-결과)

---

## 1. Descriptor Set의 기본 모델

Vulkan 기본 모델은 descriptor set 을 pipeline layout 에 맞춰 bind 한다.

```cpp
vkCmdBindDescriptorSets(cmd,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    layout,
    0, 1, &set,
    0, nullptr);
```

작은 장면에서는 이 방식이 충분하다. 문제는 머티리얼과 텍스처 수가 커질 때다. draw 마다 descriptor set 을 바꾸면 CPU 비용과 descriptor lifetime 관리가 커진다.

## 2. Descriptor Indexing 조건

Vulkan bindless 를 하려면 feature 와 layout flag 가 필요하다.

```cpp
VkPhysicalDeviceVulkan12Features v12{};
v12.descriptorIndexing = VK_TRUE;
v12.runtimeDescriptorArray = VK_TRUE;
v12.descriptorBindingPartiallyBound = VK_TRUE;
v12.descriptorBindingVariableDescriptorCount = VK_TRUE;
```

layout 쪽에는 다음 flag 가 붙는다.

```cpp
VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
```

DX12 보다 조건이 많다. 대신 validation 이 lifetime 오류를 더 잘 잡는다.

## 3. Bindless가 필요한 순간

Bindless 는 “항상 빠른 옵션”이 아니다.

| 장면 | 권장 |
|---|---|
| 텍스처 몇 개 | 일반 descriptor set |
| 수백 머티리얼 | bindless 검토 |
| streaming texture 많음 | bindless + residency 관리 |
| GPU-driven draw | bindless 거의 필수 |

GPU-driven rendering 에서는 CPU 가 draw 마다 descriptor 를 바꿀 수 없다. shader 가 material index 로 texture array 를 직접 고르는 구조가 필요하다.

## 4. 이 lab의 범위

현재 Vulkan lab 은 큐브 장면과 synchronization 구조가 중심이다. 텍스처/머티리얼 시스템이 없으므로 bindless 를 구현해도 실제 측정 차이가 작다. 따라서 Step 6 은 다음을 목표로 한다.

- descriptor indexing 이 Vulkan 1.2+ feature 라는 점.
- layout flag 와 descriptor pool flag 가 함께 맞아야 한다는 점.
- bindless 는 descriptor update 비용보다 *GPU-driven material indirection* 에서 가치가 커진다는 점.

## 5. 빌드와 실행 결과

```powershell
cmake --build build --config Debug
build\Debug\graphics_vk_lab.exe
```

관측 항목.

- 이 step 도 설계 토픽이라 패널에 토글이 없다. 관측 대상은 실제로 만들어진 descriptor 구조다. 이 lab 이 만드는 것은 set 0 의 binding 두 개(`UNIFORM_BUFFER` 1개 + `STORAGE_BUFFER` 1개)이고 set 은 frame-in-flight 수만큼만 할당된다. descriptor array 도, update-after-bind flag 도 없다.
- 그래서 `descriptorIndexing` / `runtimeDescriptorArray` 를 device feature 로 켜지 않아도 실행이 성공한다. 이 두 feature 를 필수로 올리면 지원하지 않는 장치에서 device 생성부터 실패한다는 점이 이 step 의 판단 근거다.
- 즉 이 step 의 종료 상태는 “bindless 코드가 없고, 없어도 되는 이유와 필요해지는 조건이 문서로 고정된 상태”다.

---

## 결론

Vulkan 의 bindless 는 단일 API 호출이 아니라 feature chain, descriptor set layout, pool/update 규칙의 조합이다. 이 lab 에서는 구현을 주장하지 않고, 나중에 텍스처/머티리얼 시스템을 붙일 때 필요한 조건을 고정한다.

## 더 읽을거리

- [VK_EXT_descriptor_indexing 명세](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_descriptor_indexing.html) — update-after-bind·partially-bound 플래그의 원문.
- [vkguide.dev: GPU Driven Rendering](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/) — bindless 가 가치를 갖는 머티리얼 간접 참조 구조.
- Hans-Kristian Arntzen, ["A tour of Granite's Vulkan backend"](https://themaister.net/blog/2019/04/14/a-tour-of-granites-vulkan-backend-part-1/) — 디스크립터 관리 전략의 실전 기록.
