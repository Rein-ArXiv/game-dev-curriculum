## 들어가며

> 컴퓨트의 전제는 스레드·워크그룹·디스패치 계층, 공유 메모리와 배리어, 맵·리덕션·스캔 패턴, 발산과 점유율의 의미다. 이 글은 그 전제를 Vulkan API로 구현한다. 핵심은 컴퓨트 파이프라인 구성, push constant 한도, std140/std430 레이아웃, buffer device address, 컴퓨트→그래픽 파이프라인 배리어(sync/access 분리), `vkCmdDrawIndirect`, 아토믹 경합과 서브그룹 인트린식이다.

대량 파티클을 CPU에서 갱신한 뒤 매 프레임 GPU로 업로드하면 CPU 반복 비용과 `particleCount × stateStride × updateRate`의 전송량이 함께 증가한다. GPU 컴퓨트 셰이더에 시뮬레이션 상태를 유지하면 갱신 결과를 그래픽 패스가 같은 장치 메모리에서 소비할 수 있어 이 왕복을 제거할 수 있다. 실제 손익분기점은 파티클 수, 커널 산술량, GPU 점유율, 동기화, CPU에서 결과를 읽어야 하는 빈도에 따라 달라지므로 CPU SoA 기준 구현과 GPU 타임스탬프를 함께 측정해야 한다.

컴퓨트 셰이더는 파티클에만 쓰이지 않는다. 후처리의 블러와 톤매핑, 광원 컬링(light culling), 입자 정렬, 물리 시뮬레이션, GPU 기반 컬링과 LOD 선택까지 그래픽 파이프라인 바깥의 범용 병렬 연산이 모두 컴퓨트로 옮겨 간다. 그래픽 파이프라인이 정점에서 픽셀로 흐르는 고정된 데이터 경로를 가진 반면, 컴퓨트는 입력도 출력도 셰이더가 직접 정한 버퍼와 이미지뿐인 자유로운 모델이다. 이 자유로움이 강력함과 동시에 함정의 원천이다. Vulkan에서는 그래픽·컴퓨트 어느 쪽이든 패스 사이의 자원 의존 관계를 명시해야 한다. 특히 컴퓨트는 임의 주소의 읽기·쓰기를 하므로 알고리즘 내부의 협업 범위까지 직접 정한다.

여기까지의 코드는 모델 로딩, 물리 기반 렌더링(PBR), 이미지 기반 조명(IBL), 캐스케이드 섀도 맵(CSM)까지 그래픽 표현 능력을 갖췄다. 이 글에서 도달할 결과물은 100만 파티클이 GPU 측에서 시뮬레이션되고 렌더링되며, CPU는 디스패치 명령만 발급하는 시스템이다. 컴퓨트 파이프라인 구성, 워크그룹 크기 선택, 스토리지 버퍼 패킹, 컴퓨트와 그래픽 사이의 동기화, GPU 측 데이터 구조, 간접 드로우(indirect draw), 아토믹 연산, 서브그룹 인트린식의 여덟 단계로 쌓는다.


---

## 목차
1. [Compute Pipeline 구성](#1-compute-pipeline-구성)
2. [Workgroup과 Local Size](#2-workgroup과-local-size)
3. [Storage Buffer와 메모리 레이아웃](#3-storage-buffer와-메모리-레이아웃)
4. [Compute에서 Graphics로의 동기화](#4-compute에서-graphics로의-동기화)
5. [GPU 파티클 데이터 구조](#5-gpu-파티클-데이터-구조)
6. [Indirect Draw와 동적 카운터](#6-indirect-draw와-동적-카운터)
7. [Atomic 연산과 경합](#7-atomic-연산과-경합)
8. [Subgroup 인트린식](#8-subgroup-인트린식)


---

## 1. Compute Pipeline 구성

### 그래픽 파이프라인과의 차이

그래픽 파이프라인은 정점 입력 상태, 래스터화 상태, 블렌딩, 렌더 패스, 프레임버퍼 등 수십 개의 고정 함수 단계를 한 `VkGraphicsPipelineCreateInfo`에 묶는다. 컴퓨트 파이프라인은 그 대부분이 사라진다. 묶이는 것은 셰이더 모듈 하나와 파이프라인 레이아웃뿐이다. 렌더 패스도, 프레임버퍼도, 정점 버퍼 바인딩도 없다. 컴퓨트는 화면이라는 출력 대상을 전제하지 않기 때문이다.

`VkComputePipelineCreateInfo`의 구성은 다음과 같다.

```cpp
VkPipelineShaderStageCreateInfo stage{};
stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
stage.module = computeShader;
stage.pName  = "main";

VkComputePipelineCreateInfo info{};
info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
info.stage  = stage;
info.layout = computeLayout;

VkPipeline computePipeline = VK_NULL_HANDLE;
vkCreateComputePipelines(device, pipelineCache, 1, &info, nullptr,
                         &computePipeline);
```

두 번째 인자 `pipelineCache`는 구현이 파이프라인 생성에 재사용할 수 있는 불투명 캐시다. 내용이 셰이더 ISA인지, 생성의 어느 단계를 생략하는지는 명세가 정하지 않으며 캐시 적중도 생성 시간 단축을 보장하지 않는다. `vkGetPipelineCacheData`로 저장한 데이터는 헤더의 vendor/device/driver 호환성을 확인해 같은 구현 계열에서만 재사용하고, 무효하거나 거부되면 빈 캐시로 다시 생성한다. 생성 지연은 pipeline creation feedback과 자체 계측으로 분포를 확인한다.

### Pipeline Layout: 디스크립터와 Push Constant

컴퓨트 셰이더가 읽고 쓸 자원의 형태는 파이프라인 레이아웃이 기술한다. 파티클 시뮬레이션에서는 스토리지 버퍼 하나 또는 둘과, 중력·시간 간격·파티클 수 같은 작은 시뮬레이션 파라미터를 푸시 상수(push constant)로 전달한다.

```cpp
VkPushConstantRange pc{};
pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
pc.offset     = 0;
pc.size       = sizeof(SimParams);   // limits.maxPushConstantsSize 이하

VkPipelineLayoutCreateInfo plInfo{};
plInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
plInfo.setLayoutCount         = 1;
plInfo.pSetLayouts            = &particleSetLayout;
plInfo.pushConstantRangeCount = 1;
plInfo.pPushConstantRanges    = &pc;

vkCreatePipelineLayout(device, &plInfo, nullptr, &computeLayout);
```

푸시 상수는 디스크립터 없이 커맨드 버퍼에 직접 값을 박아 넣는 작은 데이터를 전달하는 경로다. 실제 성능은 UBO와 측정해 비교한다. 다만 보장된 크기가 작다. Vulkan 명세는 `maxPushConstantsSize`의 하한을 128바이트로만 보장하므로, 많은 하드웨어가 정확히 128바이트만 제공한다. 시뮬레이션 파라미터가 그보다 크면 유니폼 버퍼로 옮긴다. `SimParams`에는 `float dt`, `vec4 gravity`, `uint particleCount`, `float spawnLife` 정도가 들어가 128바이트 안에 충분히 맞는다.

### Compute Dispatch

파이프라인을 바인딩하고, 디스크립터 세트를 묶고, 푸시 상수를 채운 뒤 디스패치를 발급한다. 바인딩 포인트가 `VK_PIPELINE_BIND_POINT_COMPUTE`라는 점이 그래픽과 다르다. 그래픽 바인딩과 컴퓨트 바인딩은 같은 커맨드 버퍼 안에서 독립적으로 유지되므로, 그래픽 파이프라인을 바인딩한 상태에서도 컴퓨트 바인딩은 따로 살아 있다.

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                        computeLayout, 0, 1, &particleSet, 0, nullptr);
vkCmdPushConstants(cmd, computeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                   0, sizeof(SimParams), &simParams);

uint32_t groupCount = (numParticles + 63) / 64;   // 64씩 올림 나눗셈
vkCmdDispatch(cmd, groupCount, 1, 1);
```

`(numParticles + 63) / 64`는 올림 나눗셈이다. 워크그룹은 64개 단위로만 발급되므로, 파티클 수가 64의 배수가 아니면 마지막 워크그룹의 일부 스레드는 유효한 파티클을 넘어선 인덱스를 받는다. 이 경계 처리를 셰이더가 직접 해야 한다는 점이 컴퓨트의 첫 함정이다. 셰이더 첫 줄에서 범위를 검사하지 않으면 버퍼 바깥을 쓰게 된다.

```glsl
void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= particleCount) return;   // 마지막 워크그룹의 잉여 스레드 차단
    // ... 시뮬레이션 ...
}
```

한 축의 디스패치 그룹 수에는 상한이 있다. `maxComputeWorkGroupCount[0]`은 하드웨어 대부분에서 65,535 이상이지만, 명세가 보장하는 하한은 그 값이다. 64 스레드 워크그룹으로 65,535개를 디스패치하면 한 축에서 약 419만 스레드까지 다룬다. 100만 파티클은 한 축으로 충분하지만, 그보다 큰 작업은 y/z 축으로 분할하거나 한 스레드가 여러 원소를 처리하는 그리드-스트라이드 루프를 쓴다.

## 2. Workgroup과 Local Size

### 디스패치 모델

`vkCmdDispatch(x, y, z)`는 $x \cdot y \cdot z$ 개의 워크그룹을 실행한다. 각 워크그룹 내부의 스레드 수는 셰이더의 `local_size_x/y/z` 레이아웃 한정자가 결정한다. 즉 전체 스레드 수는 디스패치 그룹 수와 로컬 크기의 곱이다. 디스패치 인자는 커맨드 버퍼 기록 시점에 정하고, 로컬 크기는 셰이더 선언으로 정하거나 specialization constant를 사용해 pipeline 생성 시 확정할 수 있다.

```glsl
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

void main() {
    uint gid = gl_GlobalInvocationID.x;     // 전체에서의 고유 ID
    uint lid = gl_LocalInvocationID.x;      // 워크그룹 내 0~63
    uint wid = gl_WorkGroupID.x;            // 워크그룹의 인덱스
    // gid = wid * gl_WorkGroupSize.x + lid
}
```

`gl_LocalInvocationIndex`는 다차원 로컬 ID를 1차원으로 평탄화한 값으로, 공유 메모리 배열의 인덱스로 자주 쓴다. `gl_NumWorkGroups`는 디스패치된 그룹 수를 셰이더 안에서 읽게 해 준다.

### 벤더별 자연스러운 크기

GPU는 스레드를 한 개씩 스케줄링하지 않는다. 고정된 폭의 묶음 단위로 같은 명령을 동시에 실행한다. 이 묶음을 NVIDIA는 워프(warp), AMD는 웨이브(wave), Vulkan 명세는 서브그룹(subgroup)이라 부른다. 로컬 크기가 이 묶음 크기의 배수가 아니면 마지막 묶음의 일부 레인이 놀게 된다. 로컬 크기를 잘못 잡으면 점유율이 떨어진다.

| 벤더 | Warp / Wave 크기 | 자연스러운 local_size |
|---|---|---|
| NVIDIA | Warp 32 | 32, 64, 128, 256 |
| AMD GCN | Wave 64 | 64, 128 |
| AMD RDNA | Wave 32 (또는 64) | 32, 64 |
| Intel Xe | SIMD 8/16/32 (동적) | 32, 64, 128 |
| Apple / 모바일 | 다양 (16/32/64) | 32, 64 |

64는 여러 장치에서 시작점으로 시험할 수 있지만 보편적 최적값은 아니다. subgroup 크기는 `VkPhysicalDeviceSubgroupProperties`로 확인하고, workgroup 크기는 `maxComputeWorkGroupInvocations`와 축별 `maxComputeWorkGroupSize`를 모두 만족해야 한다. 레지스터·공유 메모리 사용량과 총 dispatch 크기 때문에 64와 256의 상대 성능은 셰이더마다 달라진다. specialization constant로 후보 크기를 만들고 대표 장치에서 측정한다.

### Occupancy

점유율(Occupancy)은 GPU의 한 연산 유닛(NVIDIA의 SM, AMD의 CU)에 동시에 상주하는 워크그룹의 수, 또는 이론적 최대 대비 실제 상주 비율이다. GPU는 한 워프가 메모리 접근으로 멈추면 즉시 다른 상주 워프로 전환해 메모리 지연을 숨긴다. 이 지연 숨기기(latency hiding)가 GPU 성능의 핵심 메커니즘이고, 점유율이 높을수록 전환할 대상이 많아 효과가 커진다.

점유율을 제한하는 요인은 두 가지다. 스레드당 레지스터 수와 워크그룹당 공유 메모리 사용량이며, 최대 상주 invocation·subgroup·workgroup 수도 한계를 만든다.

```glsl
shared float tile[1024];   // 4KB 공유 메모리
```

사용 가능한 shared memory 상한은 `maxComputeSharedMemorySize`로 질의한다. 워크그룹당 4KiB를 쓴다고 해서 그 상한을 4KiB로 나눈 수만큼 상주한다고 결론낼 수 없다. 연산 유닛별 물리 용량, 최대 상주 subgroup·workgroup, 레지스터 할당 단위가 추가 제약이다. Vulkan은 셰이더의 물리 레지스터 수나 spill 임계값을 표준 속성으로 노출하지 않는다. 컴파일러 통계와 벤더 프로파일러에서 VGPR/SGPR·register·spill을 확인해야 한다.

벤더 도구로 실제 점유율을 측정하는 것이 추측보다 낫다. NVIDIA에서는 Nsight Graphics의 GPU trace 기능이, AMD에서는 Radeon GPU Profiler와 Radeon GPU Analyzer가 셰이더별 레지스터 사용량과 점유율 한계 요인, 웨이브 점유율과 VGPR/SGPR 사용량을 보여 준다. Nsight Compute는 CUDA·OptiX 중심 도구이므로 Vulkan compute shader의 실행 경로를 그대로 분석한다고 보장할 수 없다. Vulkan GPU shader 분석에는 RenderDoc이나 지원되는 Nsight Graphics 기능, 벤더 GPU profiler를 쓰고, 사용하는 도구 버전이 대상 GPU와 해당 API를 지원하는지 먼저 확인한다. 디스패치 그리드 크기를 바꿔 가며 측정하면, 점유율이 100%여도 더 빠르지 않거나 오히려 50% 점유율에서 더 빠른 경우를 종종 본다. 점유율은 목표가 아니라 지표다.

### 3D 디스패치: 이미지 처리

파티클은 1차원 배열이라 1D 디스패치가 자연스럽지만, 이미지 후처리는 2D 그리드가 맞다. HDR 이미지에 블룸이나 톤매핑을 적용하는 컴퓨트 셰이더는 픽셀 좌표를 `gl_GlobalInvocationID.xy`로 받는다.

```glsl
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0, rgba16f) uniform image2D hdrImage;

void main() {
    ivec2 px   = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(hdrImage);
    if (px.x >= size.x || px.y >= size.y) return;   // 경계 검사

    vec4 color = imageLoad(hdrImage, px);
    color.rgb  = color.rgb / (color.rgb + vec3(1.0));   // Reinhard 톤매핑
    imageStore(hdrImage, px, color);
}
```

16×16은 2D 타일의 한 후보다. 256 invocation을 지원하는지 장치 한계와 shared memory 사용량을 확인하고 8×8, 16×8, 16×16 등을 측정한다. 이미지를 storage image로 읽고 쓸 때는 해당 포맷 기능과 descriptor type을 확인하고 쓰기를 허용하는 레이아웃을 사용한다. `VK_IMAGE_LAYOUT_GENERAL`은 일반적인 선택이며 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`은 쓰기 레이아웃이 아니다.

```cpp
vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
```

## 3. Storage Buffer와 메모리 레이아웃

### Storage Buffer 디스크립터

파티클 데이터처럼 셰이더가 읽고 쓰는 대용량 버퍼는 스토리지 버퍼(Storage Buffer, SSBO)로 다룬다. 유니폼 버퍼가 읽기 전용에 최소 보장 범위가 16KiB이고 실제 `maxUniformBufferRange`까지 읽기 전용 상수를 담는다면, 스토리지 버퍼는 읽기·쓰기가 모두 되고 크기 상한이 훨씬 크다. DirectX 12의 UAV(Unordered Access View)에 대응한다.

```cpp
VkDescriptorSetLayoutBinding b{};
b.binding         = 0;
b.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
b.descriptorCount = 1;
b.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
```

버퍼 자체를 만들 때 `usage`에 `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`를 줘야 한다. 파티클 버퍼를 컴퓨트가 쓰고 그래픽이 정점 입력으로 읽는다면 `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`도 함께 줘야 한다. 스토리지 버퍼는 보통 `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` 메모리에 둔다. CPU가 매 프레임 읽을 필요가 없는 GPU 전용 데이터이기 때문이다.

### std430과 std140

셰이더 안의 구조체 레이아웃은 호스트 측 C++ 구조체와 바이트 단위로 일치해야 한다. 일치하지 않으면 컴파일은 통과하지만 GPU가 엉뚱한 오프셋에서 데이터를 읽어, 파티클이 뒤틀린 위치에 나타나거나 색이 어긋나는 식으로 나타난다. 디버깅이 까다로운 부류의 버그다. 레이아웃 규칙은 유니폼 버퍼에 std140, 스토리지 버퍼에 std430을 쓴다.

```glsl
layout(std140, binding = 0) uniform UBO {
    vec3  pos;       // offset 0, base alignment 16, 데이터 크기 12
    float scale;     // offset 12, vec3 뒤 남는 성분을 채울 수 있다
};

layout(std430, binding = 0) buffer SSBO {
    vec3  pos;       // offset 0,  base alignment 16, 점유 12
    float scale;     // offset 12  — vec3 뒤 남는 4바이트를 채운다
};
```

두 규칙의 차이를 표로 정리한다.

| 규칙 | std140 | std430 |
|---|---|---|
| `vec3` base alignment | 16바이트 | 16바이트 |
| `vec3` 뒤 `float` 오프셋 | 12 (남는 자리 사용) | 12 (남는 자리 사용) |
| `float[]` 배열 stride | 16바이트 | 4바이트 |
| `vec2[]` 배열 stride | 16바이트 | 8바이트 |
| `vec3[]` 배열 stride | 16바이트 | 16바이트 |
| 적용 대상 | UBO | SSBO |

std430이 더 조밀해 메모리 효율이 높다. 가장 큰 차이는 스칼라·`vec2` 배열과 구조체의 정렬·stride다. std140에서 `float[1024]`는 각 원소가 16바이트 stride를 가져 16KB를 쓰지만, std430에서는 4KB로 끝난다. `vec3`의 base alignment는 두 규칙 모두 16바이트지만 데이터 크기는 12바이트이므로, 바로 뒤의 `float`은 두 레이아웃 모두 offset 12를 채울 수 있다. 반면 `vec3` 배열 원소의 stride는 16바이트다. CPU 구조체와의 매칭을 단순하게 만들기 위해 `vec4`로 묶는 패턴은 여전히 유용하지만, 실제 offset은 SPIR-V reflection과 `offsetof`로 확인한다.

호스트 측 C++ 구조체에는 `alignas`로 정렬을 강제하거나, 셰이더와 같은 패킹을 손으로 맞춘 뒤 `static_assert(sizeof(Particle) == 64)`로 크기를 못 박아 두는 것이 안전하다. 실수가 컴파일 타임에 잡힌다.

### Buffer Device Address

Vulkan 1.2부터 `VK_KHR_buffer_device_address`로 버퍼의 GPU 가상 주소를 직접 얻어 셰이더에 포인터처럼 넘길 수 있다. 디스크립터 슬롯을 거치지 않고 적절한 usage·할당 플래그로 만든 버퍼의 유효한 주소 범위에 접근한다. 모든 GPU 메모리를 임의로 읽는 권한은 아니다.

```cpp
VkBufferDeviceAddressInfo info{};
info.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
info.buffer = particleBuffer;
VkDeviceAddress addr = vkGetBufferDeviceAddress(device, &info);
```

이 주소를 얻으려면 버퍼 생성 시 `usage`에 `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`를 주고, 메모리 할당 시 `VkMemoryAllocateFlagsInfo`에 `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`를 켜고, 논리 디바이스 생성 시 `bufferDeviceAddress` 기능을 활성화해야 한다. 셰이더 쪽은 `GL_EXT_buffer_reference` 확장으로 버퍼를 포인터 타입으로 선언한다.

```glsl
#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) buffer ParticleBuffer {
    Particle data[];
};

layout(push_constant) uniform Push {
    ParticleBuffer particles;   // 64비트 주소가 들어온다
} pc;

void main() {
    Particle p = pc.particles.data[gl_GlobalInvocationID.x];
    // ...
}
```

이 방식은 바인드리스(bindless) 컴퓨트의 토대다. 대규모 씬에서 수천 개의 버퍼를 디스크립터 슬롯으로 관리하는 부담이 주소 하나로 줄어든다. 디스크립터 세트 재바인딩 비용이 사라지고, 셰이더가 자료 구조를 따라 다른 버퍼로 점프하는 포인터 추적도 가능해진다. 다만 주소·범위·정렬·수명을 어기면 정의되지 않은 동작이며 디바이스 손실(device lost)도 생길 수 있고, 디버거가 슬롯 정보를 보여 주지 못해 추적이 어렵다는 대가가 따른다.

## 4. Compute에서 Graphics로의 동기화

컴퓨트가 파티클 버퍼에 쓴 결과를 그래픽 패스가 정점 입력으로 읽는다. 같은 큐에 디스패치와 드로우를 순서대로 기록해도, 그 제출 순서만으로 이 read-after-write 위험에 필요한 실행·메모리 의존성이 모두 만들어지지는 않는다. 관련 파이프라인 단계는 겹쳐 실행될 수 있으므로, 컴퓨트의 쓰기가 정점 입력 읽기 전에 완료되고 보이도록 파이프라인 배리어(pipeline barrier)를 명시해야 한다.

### 두 가지 보장: 실행 의존성과 메모리 의존성

배리어는 두 가지를 함께 정의한다. 실행 의존성(execution dependency)은 지정한 src 범위의 작업이 dst 범위보다 happens-before가 되게 한다. 메모리 의존성(memory dependency)은 src access 범위의 쓰기를 *available* 하게 만들고, dst access 범위의 읽기에 *visible* 하게 만든다. 실제 하드웨어가 이를 캐시 flush/invalidate로 구현할지는 구현 세부다. 실행 순서만 만들고 올바른 access 범위를 빠뜨리면, 순서는 맞아도 그래픽이 낡은 값을 읽을 수 있다.

### Buffer Memory Barrier

파티클 버퍼를 컴퓨트 셰이더 쓰기에서 정점 입력 읽기로 넘기는 배리어는 다음과 같다.

```cpp
VkBufferMemoryBarrier barrier{};
barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
barrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
barrier.dstAccessMask       = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
barrier.buffer              = particleBuffer;
barrier.offset              = 0;
barrier.size                = VK_WHOLE_SIZE;

vkCmdPipelineBarrier(cmd,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,   // srcStage
    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,     // dstStage
    0,
    0, nullptr,
    1, &barrier,
    0, nullptr);
```

src는 컴퓨트 셰이더 단계의 쓰기로 고정이지만, dst는 그래픽 패스가 그 데이터를 어떤 방식으로 읽느냐에 따라 달라진다. 정점 속성으로 읽으면 정점 입력 단계, 정점 셰이더 안에서 스토리지 버퍼로 읽으면 정점 셰이더 단계, 간접 드로우 인자로 읽으면 드로우 인다이렉트 단계다.

| 그래픽 측 사용처 | dstStage | dstAccess |
|---|---|---|
| 정점 버퍼 입력 | `VERTEX_INPUT_BIT` | `VERTEX_ATTRIBUTE_READ_BIT` |
| 인덱스 버퍼 | `VERTEX_INPUT_BIT` | `INDEX_READ_BIT` |
| Indirect 인자 버퍼 | `DRAW_INDIRECT_BIT` | `INDIRECT_COMMAND_READ_BIT` |
| 정점 셰이더 SSBO | `VERTEX_SHADER_BIT` | `SHADER_READ_BIT` |
| 프래그먼트 셰이더 SSBO | `FRAGMENT_SHADER_BIT` | `SHADER_READ_BIT` |
| 유니폼 버퍼 | 해당 셰이더 stage | `UNIFORM_READ_BIT` |

파티클을 인스턴스 정점 데이터로 읽으면서 동시에 간접 드로우 인자도 같은 디스패치로 갱신했다면, dst 스테이지에 `VERTEX_INPUT_BIT`와 `DRAW_INDIRECT_BIT`를 OR로 묶고 dstAccess도 두 마스크를 모두 켜야 한다.

### 너무 넓은 배리어는 손해다

`ALL_COMMANDS`와 넓은 access mask는 필요한 것보다 큰 실행·메모리 의존성을 만들 수 있다. 그렇다고 모든 구현에서 “후속 명령 전체가 정지한다”는 물리적 실행을 단정할 수는 없다. 생산자의 실제 write stage/access와 소비자의 read stage/access로 범위를 좁히면 독립 작업을 겹칠 여지를 보존한다. 파티클 버퍼를 vertex attribute로 읽는다면 동기화 2에서는 목적 stage를 `VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT`, access를 `VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT`로 지정한다.

Vulkan 검증 레이어의 synchronization validation은 기록된 API 사용에서 여러 hazard를 탐지할 수 있다. 모든 셰이더 주소 계산과 논리적 알고리즘 오류를 증명하는 도구는 아니므로, 켰을 때 경고가 없다는 사실을 완전한 동기화 증명으로 보지 않는다. 검증 레이어, 캡처 도구, 결과 해시·sentinel 테스트를 함께 사용한다.

### Queue 분리와 소유권 이전

컴퓨트 전용 큐와 그래픽 큐가 서로 다른 큐 패밀리(queue family)에 속하고 EXCLUSIVE 버퍼를 넘긴다면, 버퍼 배리어의 `srcQueueFamilyIndex`와 `dstQueueFamilyIndex`로 큐 패밀리 소유권 이전(queue family ownership transfer)을 명시해야 한다. 이는 배리어 하나를 추가하는 것으로 끝나지 않는 *양쪽 계약*이다. 소유를 넘기는 쪽이 ownership release 배리어를, 받는 쪽이 ownership acquire 배리어를 각각 기록하고, 두 큐 사이의 실행·메모리 연결을 semaphore wait/signal로 맞춰야 한다. 같은 큐 패밀리 안의 서로 다른 큐라도 명령 순서가 자동으로 공유되지 않으므로 semaphore나 fence 의존성이 필요하다. 같은 큐 패밀리에서만 쓴다면 두 인덱스 모두 `VK_QUEUE_FAMILY_IGNORED`로 둔다. 비동기 컴퓨트(async compute)로 시뮬레이션을 그래픽과 병렬로 돌릴 때 이 처리가 필요하다. CONCURRENT 버퍼는 ownership transfer를 생략하지만 queue 간 읽기·쓰기 의존은 여전히 연결한다.

## 5. GPU 파티클 데이터 구조

### 캐시 친화적인 크기

파티클 하나에 필요한 최소 상태는 위치, 속도, 수명이다. 거기에 크기, 색, 회전을 더하면 한 파티클이 차지하는 바이트가 정해진다. 이 크기가 GPU 메모리 대역폭을 직접 좌우한다. 100만 파티클을 매 프레임 한 번 읽고 한 번 쓰는 더블 버퍼 시뮬레이션은, 파티클당 64바이트면 매 프레임 약 128MB의 트래픽을 만든다. 60fps에서는 초당 약 7.7GB다. 파티클을 128바이트로 키우면 그 두 배가 된다. 데이터 구조 크기는 곧 대역폭이다.

```glsl
struct Particle {
    vec4 posSize;   // xyz = 위치, w = 크기            16
    vec4 velLife;   // xyz = 속도, w = 남은 수명        16
    vec4 color;     // RGBA                            16
    vec4 misc;      // xyz = 각속도, w = 누적 나이      16
    // 합계 64바이트
};
```

네 `vec4`는 std430과 std140에서 모두 64바이트 구조체가 되어 host와 shader 레이아웃을 맞추기 쉽다. 그러나 64바이트라는 크기 자체가 coalescing의 최적값은 아니다. 한 subgroup이 AoS 배열의 `posSize`만 읽으면 lane 사이 주소 간격이 64바이트라 불필요한 필드가 캐시·메모리 구간에 섞일 수 있다. 모든 필드를 매 단계 함께 쓰면 AoS가 단순하고, 위치·수명처럼 일부 필드만 읽는 패스가 많으면 SoA 또는 hot/cold 분리가 전송 바이트를 줄일 수 있다. 구조체 크기별 요청 transaction과 useful-byte 비율을 프로파일한다.

### Ping-pong 버퍼

in-place 갱신의 안전성은 접근 불변식에 달려 있다. invocation $i$가 원소 $i$만 읽고 같은 원소 $i$만 쓰며 다른 invocation이 그 원소를 읽지 않는 독립 적분이라면 같은 storage buffer에서도 데이터 경쟁이 없다. 반대로 이웃 파티클, 공간 격자, 이전 상태 전체를 읽는 알고리즘은 다른 invocation의 새 값과 옛 값이 섞일 수 있다. 이 경우 읽기 스냅숏과 쓰기 대상을 분리하는 ping-pong 구성이 필요하다.

```cpp
VkBuffer particleBuffer[2];   // A와 B
// simulationStep이 완료된 순서로 read/write 역할을 교환한다.
```

한 simulation step에서 A를 읽어 B에 쓴 뒤, 다음 step은 B를 읽기 전에 앞 dispatch의 shader write가 보이도록 배리어나 semaphore 의존성을 가져야 한다. frames-in-flight의 host frame 번호만 `& 1`로 적용하면 GPU 완료 전 버퍼 역할을 재사용할 수 있으므로 simulation step과 timeline 값을 기준으로 수명을 관리한다.

```glsl
layout(std430, binding = 0) readonly  buffer InParticles  { Particle data[]; } inP;
layout(std430, binding = 1) writeonly buffer OutParticles { Particle data[]; } outP;

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= particleCount) return;

    Particle p = inP.data[id];
    p.velLife.xyz += gravity.xyz * dt;       // 중력 적분
    p.posSize.xyz += p.velLife.xyz * dt;     // 위치 적분
    p.velLife.w   -= dt;                      // 수명 감소
    p.color.a      = clamp(p.velLife.w / spawnLife, 0.0, 1.0);
    outP.data[id]  = p;
}
```

`readonly`와 `writeonly` 한정자는 단순한 문서화가 아니라 컴파일러에 메모리 접근 패턴을 알려 최적화를 돕고, 잘못된 접근을 컴파일 타임에 막는다.

### AoS와 SoA

위 구조는 한 파티클의 모든 속성을 한 묶음에 둔 배열의 구조체(Array of Structures, AoS)다. 대안은 위치 배열, 속도 배열, 색 배열을 따로 두는 구조의 배열(Structure of Arrays, SoA)이다.

```glsl
layout(std430, binding = 0) buffer Positions  { vec4 data[]; } positions;
layout(std430, binding = 1) buffer Velocities { vec4 data[]; } velocities;
layout(std430, binding = 2) buffer Colors     { vec4 data[]; } colors;
```

선택 기준은 접근 패턴이다. 위치만 읽어 컬링하는 패스처럼 일부 속성만 건드리는 셰이더는 SoA가 유리할 수 있다. 한 워프가 읽는 위치들이 메모리에서 연속이라 접근이 완전히 합쳐지고, 쓰지 않는 속도·색 데이터로 캐시 라인을 채우지 않는다. 반면 시뮬레이션 셰이더처럼 모든 속성을 함께 읽고 쓰는 경우는 AoS도 좋은 후보가 된다. 한 파티클의 모든 데이터가 인접해 캐시 라인 안에서 함께 읽힌다(필요한 트랜잭션 수는 캐시·정렬·구현에 의존한다). 파티클 시뮬레이션은 보통 갱신 패스가 전부를 만지므로 AoS를 기본으로 하고, 렌더링에만 쓰는 위치·색을 별도 버퍼로 분리하는 절충안도 흔하다.

## 6. Indirect Draw와 동적 카운터

### 활성 파티클 수가 매 프레임 변한다

파티클은 생성되고 수명이 다하면 죽는다. 살아 있는 파티클의 수는 매 프레임 달라지고, 그 수를 결정하는 것은 GPU의 시뮬레이션 셰이더다. CPU는 정확한 수를 모른다. CPU가 알려면 GPU 결과를 읽어 와야 하는데, 그 프레임의 값을 즉시 CPU에서 사용하려면 완료를 기다려야 해 파이프라인을 직렬화할 수 있다. 몇 프레임 늦은 비동기 readback은 기다림을 줄이지만 현재 draw 수를 즉시 결정하지 못한다. 이 직렬화를 피하려면 드로우 호출의 인자 자체를 GPU 버퍼에 두고, CPU는 그 버퍼를 가리키기만 하면 된다. 이것이 간접 드로우(indirect draw)다.

### VkDrawIndirectCommand

간접 드로우 인자는 정해진 구조체로 GPU 버퍼에 놓인다. 컴퓨트 셰이더가 이 구조체의 `instanceCount`를 직접 갱신한다.

```cpp
struct VkDrawIndirectCommand {
    uint32_t vertexCount;       // 파티클 한 개를 그리는 정점 수
    uint32_t instanceCount;     // 살아 있는 파티클 수 — GPU가 채운다
    uint32_t firstVertex;
    uint32_t firstInstance;
};
```

이 버퍼는 `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`, `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`, `VK_BUFFER_USAGE_TRANSFER_DST_BIT`를 함께 가져야 한다. 컴퓨트가 스토리지 버퍼로 쓰고, 그래픽이 간접 인자로 읽으며, 초기화에서 `vkCmdFillBuffer`의 대상이 되기 때문이다. 시뮬레이션 셰이더는 살아 있는 파티클을 별도의 조밀한 배열로 모으면서 카운터를 올린다.

```glsl
layout(std430, binding = 2) buffer DrawArgs {
    uint vertexCount;
    uint instanceCount;     // atomicAdd 대상
    uint firstVertex;
    uint firstInstance;
} drawArgs;

void main() {
    // ... 파티클 업데이트 후 생존 판정 ...
    if (isAlive) {
        uint outIdx = atomicAdd(drawArgs.instanceCount, 1);
        aliveParticles.data[outIdx] = p;   // 살아 있는 것만 연속으로 모은다
    }
}
```

여기서 한 가지 주의가 있다. 인자 버퍼의 네 필드를 모두 초기화해야 한다. 예를 들어 4정점 billboard triangle strip이면 `vertexCount=4, firstVertex=0, firstInstance=0`으로 두고, 파티클 정점 입력의 rate는 INSTANCE로 설정한다. 조밀 출력 버퍼는 최대 생존 수를 담을 수 있어야 한다. `instanceCount`는 매 프레임 디스패치 전에 0으로 초기화해야 한다. `vkCmdFillBuffer`로 카운터 필드를 0으로 채우려면 이 버퍼에 `VK_BUFFER_USAGE_TRANSFER_DST_BIT` usage가 반드시 있어야 한다. transfer write 후 컴퓨트 셰이더의 읽기·쓰기로 가는 배리어, 이어 indirect read로 가는 배리어를 차례로 둔다. 초기화를 빠뜨리면 카운터가 무한히 증가해 버퍼 범위를 넘는다.

### CPU 측 Indirect Draw 발급

그래픽 측에서는 고정된 인자로 `vkCmdDrawIndirect`를 한 번만 부른다. 인스턴스 수는 GPU 버퍼에서 읽히므로 CPU 코드에는 등장하지 않는다.

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipe);
VkDeviceSize offset = 0;
vkCmdBindVertexBuffers(cmd, 0, 1, &aliveParticleBuffer, &offset);

vkCmdDrawIndirect(cmd,
    drawArgsBuffer,    // VkDrawIndirectCommand가 있는 버퍼
    0,                 // 버퍼 내 오프셋
    1,                 // 드로우 명령 개수
    sizeof(VkDrawIndirectCommand));   // stride
```

CPU는 고정 인자로 한 번 호출하고, GPU가 `instanceCount`만큼 인스턴스를 그린다. 수백만 파티클을 그리면서 CPU 드로우 콜은 하나다. 컴퓨트가 인자 버퍼를 쓰고 이 드로우가 읽으므로, 그 사이에 dst 스테이지를 `DRAW_INDIRECT_BIT`, dstAccess를 `INDIRECT_COMMAND_READ_BIT`로 한 배리어가 들어가야 한다.

### Indirect Count

드로우 명령의 개수 자체가 동적인 경우도 있다. 여러 머티리얼 중 이번 프레임에 보이는 것만 그릴 때, 드로우 명령의 수를 GPU가 정한다. `vkCmdDrawIndirectCount`는 그 개수를 또 다른 GPU 버퍼에서 읽는다.

```cpp
vkCmdDrawIndirectCount(cmd,
    drawArgsBuffer, 0,        // 인자 배열
    countBuffer,   0,         // 드로우 개수가 든 버퍼
    maxDrawCount,             // 상한 (안전을 위한 하드 캡)
    sizeof(VkDrawIndirectCommand));
```

`vkCmdDrawIndirect`만으로는 드로우 횟수가 커맨드 버퍼 기록 시점에 고정된다. `vkCmdDrawIndirectCount`는 그 횟수마저 GPU가 결정하게 해, GPU 기반 컬링이 드로우 명령 수를 직접 줄이는 완전한 GPU 구동 렌더링을 가능케 한다. 이 함수는 `VK_KHR_draw_indirect_count` 확장 또는 Vulkan 1.2 코어로 제공되며, `maxDrawCount`는 count 버퍼 값이 어떻든 넘지 않을 상한으로 안전 장치 역할을 한다.

## 7. Atomic 연산과 경합

### 기본 아토믹

여러 스레드가 같은 메모리 위치를 동시에 갱신할 때, 읽고-수정하고-쓰는 과정이 끼어들기 없이 한 덩어리로 일어나야 정확한 결과가 나온다. 아토믹 연산이 그 보장을 제공한다. `atomicAdd`는 누적 카운터에, `atomicMin`/`atomicMax`는 경계 상자 계산에, `atomicCompSwap`은 잠금 없는 자료 구조에 쓴다.

```glsl
layout(std430, binding = 0) buffer Counter { uint count; };

void main() {
    if (someCondition) {
        uint oldValue = atomicAdd(count, 1);
        // oldValue는 증가 전 값, count는 1 증가한 상태가 된다
    }
}
```

`atomicAdd`의 반환값이 증가 전 값이라는 점이 중요하다. 이 반환값을 출력 배열의 인덱스로 쓰면, 여러 스레드가 동시에 호출해도 각자 고유한 슬롯을 받아 충돌 없이 데이터를 모은다. 6절의 살아 있는 파티클 수집이 바로 이 패턴이다.

### 경합과 직렬화

아토믹의 함정은 비용이다. 수만 개의 스레드가 같은 메모리 위치에 아토믹을 발급하면, 하드웨어는 그 갱신들을 한 줄로 세워 차례로 처리한다. 같은 주소에 대한 아토믹은 본질적으로 직렬화되고, 경합이 심하면 이 직렬화가 셰이더 전체의 병목이 된다. 100만 파티클이 모두 단일 전역 카운터를 두드리면, 그 아토믹 한 줄이 디스패치 시간을 좌우한다.

### 직렬화 회피: 서브그룹 단위 집계

해법은 전역 아토믹의 호출 횟수를 줄이는 것이다. 같은 서브그룹(워프/웨이브) 안의 스레드들이 먼저 자기들끼리 합을 구하고, 서브그룹당 대표 한 명만 그 합을 전역 카운터에 한 번 더한다. 64 스레드 서브그룹이라면 64번의 전역 아토믹이 1번으로 줄어든다.

```glsl
uint localCount = subgroupAdd(uint(isAlive));   // 서브그룹 안에서 먼저 합산
uint base = 0;
if (subgroupElect()) {                          // 서브그룹의 한 스레드만
    base = atomicAdd(drawArgs.instanceCount, localCount);
}
base = subgroupBroadcastFirst(base);            // 대표가 받은 시작 인덱스를 공유
uint slot = base + subgroupExclusiveAdd(uint(isAlive));   // 각자의 고유 슬롯
if (isAlive) aliveParticles.data[slot] = p;
```

이 코드는 살아 있지 않은 invocation도 집단 연산에 참여하는 제어 흐름을 전제한다. 출력 순서는 subgroup 실행 순서에 따라 달라지므로 안정 정렬은 아니다. 전역 아토믹 호출이 서브그룹 수만큼으로 줄어 경합을 완화할 수 있다. 대표가 받은 시작 인덱스를 `subgroupBroadcastFirst`로 나누고, 각 스레드는 서브그룹 내 배타적 누적합(exclusive prefix sum)으로 자기 슬롯을 정한다. 이 패턴이 GPU 스트림 컴팩션의 표준 형태다. 경합이 심한 경우 수십 배의 처리량 차이를 낸다.

### 64비트 아토믹

기본 아토믹은 32비트다. 카운터가 40억을 넘거나, 큰 인덱스 공간을 다루거나, 패킹된 깊이-인덱스 값을 하나의 아토믹으로 갱신해야 하면 64비트 아토믹이 필요하다. `VK_KHR_shader_atomic_int64` 확장이 이를 제공한다.

```glsl
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
layout(std430, binding = 0) buffer Counter { uint64_t count; };

uint64_t oldVal = atomicAdd(count, 1ul);
```

확장 지원 여부와 함께, 셰이더가 부동소수점 아토믹을 쓴다면 `VK_EXT_shader_atomic_float`가 별도로 필요하다. 두 확장 모두 모든 하드웨어가 지원하지는 않으므로, 디바이스 기능 질의로 확인한 뒤 분기한다.

### 셰이더 메모리 배리어와 파이프라인 배리어의 구분

배리어라는 이름이 두 층위에서 쓰여 혼동을 부른다. 셰이더의 `memoryBarrier*`는 호출 invocation의 이전 메모리 접근을 정해진 메모리 범위에 대해 순서화하지만 다른 invocation이 그 지점에 도달할 때까지 기다리게 하지는 않는다. `barrier`는 워크그룹의 실행을 맞추고 `groupMemoryBarrier`는 해당 범위의 메모리를 순서화하지만 실행 대기는 하지 않는다. C++의 파이프라인 배리어는 디스패치나 패스 사이의 실행·메모리 의존성을 기술한다. 서로 대체하지 못한다.

| 배리어 | 가시성 범위 | 발급 위치 |
|---|---|---|
| `memoryBarrierShared` | 워크그룹의 공유 메모리 | GLSL |
| `memoryBarrierBuffer` | 디바이스 범위의 스토리지 버퍼 메모리, 실행 동기화 없음 | GLSL |
| `groupMemoryBarrier` | 워크그룹의 모든 메모리 | GLSL |
| `barrier` | 실행 동기화 + 공유 메모리 | GLSL |
| `vkCmdPipelineBarrier` | 디스패치 / 패스 경계 | C++ |

공유 메모리를 쓰는 워크그룹 내 협업에서는 `barrier`가 핵심이다. 이 함수는 워크그룹의 모든 스레드가 그 지점에 도달할 때까지 기다리는 실행 배리어이면서, 공유 메모리 쓰기의 가시성도 보장한다. 다른 디스패치 사이나 컴퓨트와 그래픽 사이의 동기화는 셰이더 배리어로 안 된다. 셰이더 안의 배리어는 디스패치가 끝난 뒤 다음 패스에는 아무 보장도 주지 못한다. 그 경계는 반드시 파이프라인 배리어로 막아야 한다.

```glsl
layout(local_size_x = 256) in; // 장치가 이 크기를 지원한다고 가정
shared float tile[256];

void main() {
    uint lid = gl_LocalInvocationID.x;
    uint id = gl_GlobalInvocationID.x;
    tile[lid] = id < elementCount ? data[id] : 0.0; // tail도 barrier에 참여
    barrier();                       // 모든 스레드가 자기 칸을 채울 때까지 대기

    // 이제 워크그룹의 모든 데이터를 읽어도 안전하다
    float sum = 0.0;
    for (int i = 0; i < 256; ++i) sum += tile[i];
    if (id < elementCount) result[id] = sum;
}
```

`barrier`는 워크그룹에 균일한 제어 흐름에서 호출해야 한다. 모든 invocation이 같은 조건을 따르는 조건문은 가능하지만, 워크그룹의 일부 스레드만 그 지점에 도달하면, 도달하지 못한 스레드를 영원히 기다려 디바이스가 멈춘다. 배리어는 워크그룹의 모든 스레드가 균일하게(uniform) 지나는 제어 흐름에만 두어야 한다.

## 8. Subgroup 인트린식

서브그룹(subgroup)은 구현이 함께 스케줄하는 invocation 묶음으로, NVIDIA의 워프나 AMD의 웨이브에 대응한다. 셰이더는 물리적 레지스터 파일 구조에 의존하지 않고, shuffle·vote·reduction 같은 서브그룹 집단 연산을 통해 값 교환을 요청한다. 이런 연산은 일반적으로 공유 메모리와 워크그룹 전체 배리어보다 싸지만, 서브그룹 크기와 지원 연산, 실행 비용은 디바이스마다 다르다. SPIR-V 1.3과 Vulkan 1.1부터 핵심 질의 구조가 표준화됐다.

### 기능 질의

서브그룹 크기와 지원되는 연산 종류는 디바이스마다 다르므로 런타임에 질의한다.

```cpp
VkPhysicalDeviceSubgroupProperties sub{};
sub.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

VkPhysicalDeviceProperties2 props2{};
props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
props2.pNext = &sub;
vkGetPhysicalDeviceProperties2(gpu, &props2);

uint32_t subgroupSize = sub.subgroupSize;             // NVIDIA 32, AMD GCN 64 등
VkSubgroupFeatureFlags ops = sub.supportedOperations; // 산술/투표/볼롯/셔플 등
```

`supportedOperations`에 `VK_SUBGROUP_FEATURE_ARITHMETIC_BIT`, `VK_SUBGROUP_FEATURE_BALLOT_BIT` 같은 비트가 켜져 있는지로 어떤 인트린식을 쓸 수 있는지 확인한다. `supportedStages`에 `VK_SHADER_STAGE_COMPUTE_BIT`가 있어야 컴퓨트에서 쓸 수 있다. RDNA처럼 웨이브 크기가 32 또는 64로 가변인 하드웨어에서는 `VK_EXT_subgroup_size_control`로 셰이더가 원하는 크기를 요청할 수도 있다.

```cpp
VkPipelineShaderStageRequiredSubgroupSizeCreateInfo reqSize{};
reqSize.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
reqSize.requiredSubgroupSize = 32;  // wave32 요청 (지원 범위 내에서)

// VkPipelineShaderStageCreateInfo 의 pNext에 연결한다
shaderStage.pNext = &reqSize;
```

다만 폭을 강제하면 하드웨어가 자연스럽게 고를 폭을 막아 occupancy나 명령 패킹이 나빠질 수 있으므로 측정 없이 강제하지 않는다.

### 주요 인트린식

서브그룹 연산은 종류별로 GLSL 확장이 나뉘어 있어, 쓰는 만큼만 활성화한다.

```glsl
#extension GL_KHR_shader_subgroup_basic      : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot     : require
#extension GL_KHR_shader_subgroup_vote       : require
#extension GL_KHR_shader_subgroup_shuffle    : require

void main() {
    float v = input[gl_GlobalInvocationID.x];

    float sum  = subgroupAdd(v);          // 서브그룹 전체의 합
    float maxV = subgroupMax(v);          // 최댓값

    uvec4 mask  = subgroupBallot(v > 0.0);          // 조건 참인 레인의 비트마스크
    uint  cnt   = subgroupBallotBitCount(mask);     // 그 개수

    // 이 shuffle은 목표 레인이 활성인 경우에만 유효하다.
    // 마지막 partial subgroup/분기에서는 ballot으로 활성 여부를 확인한다.
    float nb = subgroupShuffle(v, (gl_SubgroupInvocationID + 1) % gl_SubgroupSize);  // 옆 레인 값(마지막 레인은 0번으로 감는다)

    if (subgroupElect()) {                // 서브그룹에서 단 하나의 레인만 참
        atomicAdd(globalCount, cnt);
    }

    if (subgroupAll(v > 0.0))   { /* 모든 레인이 양수 */ }
    if (subgroupAny(v > 100.0)) { /* 하나라도 큰 값 */ }
}
```

`subgroupBallot`는 조건을 만족하는 레인들을 비트마스크로 모은다. 이 마스크로 활성 레인 수를 세거나, 앞쪽 레인의 수를 세어 압축 인덱스를 계산한다. `subgroupShuffle` 계열은 임의 레인의 값을 가져오고, `subgroupElect`는 활성 레인 중 가장 낮은 인덱스 하나만 참을 반환해 대표 선출에 쓴다. 7절의 아토믹 경합 완화가 이 인트린식들의 조합이다.

### 리덕션 방식 비교

100만 개 값의 합을 구하는 세 가지 방식의 비용 차이는 크다.

| 방식 | 핵심 코드 | 비용 |
|---|---|---|
| 스레드별 전역 atomic | `atomicAdd(out, v);` | 전역 직렬화 |
| 공유 메모리 트리 리덕션 | $\log_2 N$ 단계 + `barrier` | 공유 메모리 + 배리어 |
| 서브그룹 합 + atomic | `s = subgroupAdd(v); if (subgroupElect()) atomicAdd(out, s);` | 가장 적은 전역 접근 |

서브그룹 방식은 `subgroupAdd`를 모든 활성 레인이 실행해 합을 만든 뒤 대표 레인만 그 결과를 atomic에 더한다. 합 연산 자체를 `subgroupElect()` 분기 안에 넣으면 활성 invocation이 대표 하나뿐이어서 서브그룹 전체 합이 되지 않는다. 이 방식은 전역 아토믹 호출을 서브그룹 수만큼으로 줄인다. 워크그룹 단위 리덕션이 더 필요하면 서브그룹 합들을 공유 메모리에 모아 한 번 더 합치는 2단계 구조를 쓴다. 실제 이득은 아토믹 경합과 하드웨어의 서브그룹 크기로 측정한다.

### Prefix Sum과 스트림 컴팩션

서브그룹의 누적합 인트린식은 스트림 컴팩션(stream compaction)의 핵심 도구다. 배타적 누적합은 자기 이전 레인들의 합을, 포함 누적합은 자기까지의 합을 준다.

```glsl
float excl = subgroupExclusiveAdd(v);   // 자신 이전까지의 누적 (자신 제외)
float incl = subgroupInclusiveAdd(v);   // 자신까지의 누적 (자신 포함)
```

살아 있는 파티클을 빈틈없는 연속 배열로 모으는 작업이 정확히 이 패턴이다. 각 레인이 생존 여부를 0 또는 1로 두고 배타적 누적합을 구하면, 그 값이 곧 자기 파티클을 쓸 서브그룹 내 상대 슬롯이 된다. 여기에 서브그룹 대표가 전역 아토믹으로 받은 시작 인덱스를 더하면 전역 슬롯이 나온다. 공유 메모리 트리 리덕션으로 같은 일을 하면 단계마다 배리어가 필요하지만, 서브그룹 누적합은 GLSL 집단 연산 하나로 표현된다. 실제 기계 명령이 하나라는 보장은 없고 구현이 여러 shuffle·산술 명령으로 낮출 수 있다.

### 모바일과 폴백

서브그룹 인트린식은 데스크톱 GPU에서는 사실상 모두 지원되지만, 모바일 GPU나 오래된 하드웨어에서는 일부 연산이 빠지거나 서브그룹 크기가 작다. `supportedOperations`에 원하는 비트가 없으면 서브그룹 경로를 쓸 수 없다. 이 경우 공유 메모리 트리 리덕션으로 폴백하는 경로를 함께 두고, 디바이스 기능에 따라 다른 셰이더 변형을 선택한다. 서브그룹 크기를 셰이더 컴파일 타임에 상수로 가정하면 안 된다. 같은 셰이더가 32 레인 하드웨어와 64 레인 하드웨어에서 모두 정확히 동작하도록, 크기에 의존하는 로직은 `gl_SubgroupSize`를 읽어 처리한다.


---

## 결론

이 글의 코드는 100만 파티클이 GPU 측에서 시뮬레이션되고 렌더링되는 시스템을 만든다. CPU는 디스패치 명령과 간접 드로우 인자의 포인터만 발급하고, 활성 파티클 수의 결정과 데이터 수집은 모두 GPU에서 일어난다. 컴퓨트 파이프라인, 스토리지 버퍼, 컴퓨트와 그래픽 사이의 배리어, 간접 드로우, 아토믹, 서브그룹 인트린식이 합쳐져, 그래픽 파이프라인 바깥의 범용 GPU 연산을 다루는 토대가 모였다.

세 가지 트레이드오프가 이 글의 핵심이다. 첫째는 동기화의 정밀도다. 컴퓨트에서 그래픽으로 넘기는 배리어의 dst 스테이지를 정확히 좁히면 드라이버가 작업을 겹쳐 실행할 여지를 얻지만, 넓게 잡으면 안전한 대신 독립 작업을 겹칠 여지를 잃는다. 둘째는 데이터 구조의 크기다. 파티클 한 개의 바이트가 곧 매 프레임 대역폭이며, 64바이트 구조체는 이 예제의 레이아웃이며 AoS·SoA·hot/cold 분리의 손익은 접근 패턴에 따라 달라진다. 셋째는 아토믹 경합이다. 단일 전역 카운터는 정확하지만 직렬화되고, 서브그룹 단위 집계로 호출 횟수를 줄이면 정확성을 유지한 채 경합을 푼다.

남은 함정은 이식성이다. 서브그룹 크기는 하드웨어와 shader 설정에 따라 다르고, 모바일에서는 일부 인트린식과 64비트 아토믹이 빠진다. 디바이스 기능 질의로 확인한 뒤 분기하고, 서브그룹 경로가 없을 때 공유 메모리 리덕션으로 폴백하는 두 갈래를 함께 두는 것이 프로덕션 코드의 형태다. 동기화 검증 레이어와 벤더 프로파일러로 측정하면서 배리어와 점유율을 조율하는 과정이 컴퓨트 셰이더 성능 작업의 실제 모습이다.

## 더 읽을거리

- Khronos, ["Vulkan Subgroup Tutorial"](https://www.khronos.org/blog/vulkan-subgroup-tutorial) — 서브그룹 인트린식의 공식 입문.
- [Sascha Willems: Compute 예제들](https://github.com/SaschaWillems/Vulkan#compute-shader) — 파티클·컬링·N-body 의 완성 구현.
- [Vulkan Synchronization Examples](https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples) — 컴퓨트↔그래픽 배리어의 정답 패턴.
