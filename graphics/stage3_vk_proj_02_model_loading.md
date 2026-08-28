## 들어가며

큐브 한 개를 그리던 단계에서 임의의 메시를 그리는 단계로 넘어가려면 세 가지가 동시에 필요하다. 외부 모델 파일을 읽어 정점·인덱스로 변환하고, 깊이 비교로 카메라에 가려진 면을 잘라내고, 거친 가장자리를 부드럽게 만드는 멀티 샘플 안티앨리어싱(Multi-Sampled Anti-Aliasing, MSAA)을 도입하는 것이다. 이 셋은 서로 얽혀 있다. 메시를 읽으면 면이 겹치므로 깊이 버퍼가 필요해지고, 깊이 버퍼가 생기면 깊이 첨부의 샘플 수가 컬러 첨부와 일치해야 하므로 MSAA 설정이 깊이 자원까지 끌고 들어간다.

여기까지의 코드 상태는 삼각형 하나에서 회전하는 텍스처 큐브까지의 흐름이 동작하는 단계다. Vulkan 인스턴스, 논리 디바이스, 스왑체인, 렌더 패스, 그래픽스 파이프라인, 디스크립터 세트, 이미지 레이아웃 전이의 기본 패턴이 한 번 통과한 상태다. 다만 그려지는 형상은 코드에 하드코딩된 정점 배열이고, 깊이 테스트가 없어 그리는 순서대로 면이 덮어써지며, 가장자리는 계단 모양으로 거칠다.

이 글에서 도달할 결과물은 외부 메시가 깊이 정렬된 상태로 부드러운 가장자리를 가지고 그려지는 장면이다. 도달 경로는 여덟 단계로 쌓는다. 모델을 읽는 두 경로(tinyobjloader와 tinygltf), 정점 중복 제거와 인덱스 빌드, 깊이 첨부 자원 생성과 포맷 선택, 렌더 패스에 깊이 추가, 파이프라인의 깊이/스텐실 상태, MSAA 샘플 수 결정, Resolve 첨부와 서브패스, 그리고 알파 가장자리를 다듬는 샘플 셰이딩이다.


---

## 목차
1. [tinyobjloader와 tinygltf](#1-tinyobjloader와-tinygltf)
2. [정점 중복 제거와 인덱스 빌드](#2-정점-중복-제거와-인덱스-빌드)
3. [깊이 첨부 자원 생성](#3-깊이-첨부-자원-생성)
4. [Render Pass에 깊이 추가](#4-render-pass에-깊이-추가)
5. [Pipeline의 Depth Stencil State](#5-pipeline의-depth-stencil-state)
6. [MSAA 활성화](#6-msaa-활성화)
7. [Resolve Attachment 구성](#7-resolve-attachment-구성)
8. [샘플 셰이딩](#8-샘플-셰이딩)


---

## 1. tinyobjloader와 tinygltf

GPU는 정점 버퍼와 인덱스 버퍼라는 두 개의 평탄한 배열만 받는다. 정점 버퍼는 위치·법선·UV를 한 정점에 묶어 인터리브한 구조체의 연속이고, 인덱스 버퍼는 그 구조체를 가리키는 32비트 정수의 연속이다. 외부 모델 포맷이 이 형태와 얼마나 가까운지가 로더의 복잡도를 결정한다. OBJ와 glTF는 그 거리가 정반대다.

### OBJ — 텍스트 포맷, 분리된 인덱스

OBJ는 1990년대 Wavefront가 정의한 텍스트 포맷이다. 한 줄에 하나의 요소를 적고, 위치(`v`), 텍스처 좌표(`vt`), 법선(`vn`)을 각각 독립된 배열에 쌓은 뒤, 면(`f`)에서 세 배열의 인덱스를 슬래시로 묶어 참조한다.

```text
v 1.0 0.0 -1.0
vt 0.5 0.5
vn 0.0 1.0 0.0
f 1/1/1 2/2/1 3/3/1
```

마지막 줄의 `1/1/1`은 위치 1번, UV 1번, 법선 1번을 묶은 한 정점이다. 인덱스가 1부터 시작한다는 점, 그리고 위치·UV·법선이 서로 다른 인덱스를 가질 수 있다는 점이 핵심이다. 한 정점이 세 개의 독립 인덱스로 표현되므로, GPU가 요구하는 단일 인덱스 형태로 만들려면 세 인덱스의 조합마다 하나의 정점을 만들어 재패킹해야 한다.

tinyobjloader는 헤더 하나로 이 파싱을 처리한다. 구현부는 단 한 곳의 번역 단위에서 매크로로 활성화한다.

```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

tinyobj::attrib_t attrib;
std::vector<tinyobj::shape_t> shapes;
std::vector<tinyobj::material_t> materials;
std::string warn, err;

bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials,
                           &warn, &err, "model.obj");
if (!ok) {
    // err 에 치명 오류, warn 에 비치명 경고가 담긴다
    throw std::runtime_error(warn + err);
}
```

`attrib.vertices`, `attrib.texcoords`, `attrib.normals`는 각각 `float`의 평행 배열이다. 위치는 3개, UV는 2개, 법선은 3개씩 묶여 있다. 도형(shape)마다 `mesh.indices`가 `(vertex_index, texcoord_index, normal_index)` 튜플의 배열로 면 구성을 담는다. tinyobjloader는 기본적으로 모든 면을 삼각형으로 분해(triangulate)해 주므로, 사각형이나 다각형 면도 GPU가 바로 받을 수 있는 삼각형 단위로 나온다. UV나 법선이 없는 정점은 해당 인덱스가 음수(-1)로 들어오므로, 음수 검사로 누락을 처리한다.

### glTF 2.0 — JSON과 바이너리, GPU 친화 레이아웃

glTF는 Khronos가 정의한 런타임 전송 포맷이다. 이름 그대로 "전송(transmission)"을 목표로 하므로, 정점 데이터를 이미 GPU가 원하는 형태로 담는다. 구조는 JSON 메타데이터와 바이너리 버퍼의 두 층이다. JSON은 씬·노드·메시·재질의 그래프를 기술하고, 실제 정점 숫자는 바이너리 버퍼에 들어간다.

```json
{
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 1, "NORMAL": 2, "TEXCOORD_0": 3},
    "indices": 4,
    "material": 0
  }]}],
  "materials": [{"pbrMetallicRoughness": {
    "baseColorTexture": {"index": 0},
    "metallicRoughnessTexture": {"index": 1}
  }}],
  "accessors": [...],
  "bufferViews": [...],
  "buffers": [{"uri": "model.bin"}]
}
```

`attributes`의 숫자는 접근자(accessor) 인덱스다. 접근자는 컴포넌트 타입(`FLOAT`, `UNSIGNED_SHORT` 등), 원소 형상(`VEC3`, `SCALAR`), 개수, 그리고 버퍼 뷰(bufferView)로의 오프셋을 담는다. 버퍼 뷰는 다시 바이너리 버퍼의 한 슬라이스를 가리키며, 바이트 오프셋·길이·간격(stride)을 명시한다. 이 간접 참조 사슬을 따라가면 정점 데이터의 정확한 바이트 위치가 나온다. 데이터는 이미 인터리브되어 있을 수 있고, 인덱스 또한 하나의 통합 인덱스로 들어 있어 재패킹이 거의 필요 없다.

glTF가 OBJ와 결정적으로 다른 지점은 표준 재질 모델이다. glTF의 `pbrMetallicRoughness`는 베이스 컬러, 금속성(metallic), 거칠기(roughness)를 표준 텍스처 슬롯으로 정의한다. 스킨 애니메이션, 모르프 타깃, 노드 계층 변환까지 포맷 안에 들어 있어, 캐릭터나 복잡한 씬을 추가 변환 없이 옮길 수 있다.

```cpp
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

tinygltf::TinyGLTF loader;
tinygltf::Model model;
std::string err, warn;

// .gltf 는 JSON + 외부 .bin, .glb 는 단일 바이너리 컨테이너
bool ok = loader.LoadASCIIFromFile(&model, &err, &warn, "model.gltf");
// .glb 라면 loader.LoadBinaryFromFile(...)
```

`.gltf` 확장자는 JSON 텍스트와 외부 `.bin` 파일을 따로 두는 형태이고, `.glb`는 JSON과 바이너리를 한 파일에 담은 컨테이너다. 배포 시에는 단일 파일인 `.glb`가 다루기 편하다. tinygltf는 두 경우를 별도 함수로 받는다.

### OBJ와 glTF 비교

| 항목 | OBJ | glTF 2.0 |
|---|---|---|
| 포맷 | 텍스트 | JSON + 바이너리 |
| 정점 인덱스 | (pos, uv, normal) 분리 | 통합 인덱스 1개 |
| 재패킹 | 필요 | 거의 불필요 |
| PBR 재질 | 미지원 (MTL은 Phong 계열) | 표준 metallic/roughness |
| 스킨 애니메이션 | 미지원 | 표준 |
| 모르프 타깃 | 미지원 | 표준 |
| 노드 계층/변환 | 평면적 | 트리 + 행렬 |
| 압축 | 비표준 | KTX2 + Basis / Draco |
| 학습 난이도 | OBJ가 단순 | glTF가 현실적 |

OBJ는 구조가 단순해 로더의 동작 원리를 드러내기에 좋고, glTF는 실제 자산 파이프라인에서 쓰이는 표준이다. 이 글은 OBJ로 재패킹의 원리를 보인 뒤, 같은 정점 구조를 glTF로 확장할 수 있다는 흐름으로 진행한다. 어느 쪽이든 최종적으로는 동일한 정점 버퍼와 인덱스 버퍼를 GPU에 올린다.

## 2. 정점 중복 제거와 인덱스 빌드

OBJ를 그대로 펼치면 면 하나당 정점 세 개가 그대로 나온다. 인접한 면이 정점을 공유해도 각각 별도의 정점으로 복제된다. 큐브는 8개의 모서리 점밖에 없지만, 12개 삼각형 × 3 = 36개의 정점이 나온다. 같은 위치·UV·법선을 가진 정점을 하나로 합치고 인덱스만 재활용하면 메모리와 GPU 캐시 효율이 함께 올라간다.

### 해시 키로 중복 제거

OBJ의 `(pos_idx, uv_idx, normal_idx)` 튜플이 같으면 같은 정점이다. 다만 인덱스 튜플로 직접 비교하기보다, 펼쳐진 정점 구조체 자체를 해시 맵의 키로 쓰는 편이 일반적이다. glTF 경로에서도 같은 방식이 그대로 적용되기 때문이다.

```cpp
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;

    bool operator==(const Vertex& o) const {
        return pos == o.pos && normal == o.normal && uv == o.uv;
    }
};

namespace std {
template<> struct hash<Vertex> {
    size_t operator()(const Vertex& v) const {
        size_t h1 = hash<glm::vec3>()(v.pos);
        size_t h2 = hash<glm::vec3>()(v.normal);
        size_t h3 = hash<glm::vec2>()(v.uv);
        // XOR + shift 로 세 해시를 섞는다. 충돌은 operator== 가 걸러낸다
        return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
    }
};
}
```

해시 함수는 `glm::vec3`와 `glm::vec2`의 해시를 시프트해 섞는다. 충돌이 나도 `operator==`가 최종적으로 동일성을 판정하므로 정확성에는 영향이 없고, 해시 품질이 낮으면 같은 버킷에 몰려 성능만 떨어진다. 부동소수 비교를 직접 쓰는 점은 주의가 필요하다. 같은 정점이라도 파일에서 읽은 비트가 정확히 같아야 같은 키로 인식되므로, 로더가 값을 변형하지 않고 그대로 넘긴다는 전제 위에서 동작한다.

```cpp
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;
std::unordered_map<Vertex, uint32_t> uniqueVertices;

for (const auto& shape : shapes) {
    for (const auto& idx : shape.mesh.indices) {
        Vertex v;
        v.pos    = { attrib.vertices[3 * idx.vertex_index + 0],
                     attrib.vertices[3 * idx.vertex_index + 1],
                     attrib.vertices[3 * idx.vertex_index + 2] };
        v.normal = idx.normal_index >= 0
                   ? glm::vec3{ attrib.normals[3 * idx.normal_index + 0],
                                attrib.normals[3 * idx.normal_index + 1],
                                attrib.normals[3 * idx.normal_index + 2] }
                   : glm::vec3{ 0, 1, 0 };
        v.uv     = idx.texcoord_index >= 0
                   ? glm::vec2{ attrib.texcoords[2 * idx.texcoord_index + 0],
                                1.0f - attrib.texcoords[2 * idx.texcoord_index + 1] }
                   : glm::vec2{ 0, 0 };

        auto [it, inserted] =
            uniqueVertices.try_emplace(v, (uint32_t)vertices.size());
        if (inserted) {
            vertices.push_back(v);
        }
        indices.push_back(it->second);
    }
}
```

`try_emplace`는 키가 없을 때만 삽입하고, 삽입 여부를 `inserted`로 알려 준다. 처음 보는 정점이면 새 인덱스를 부여하고 `vertices`에 추가하며, 이미 있는 정점이면 기존 인덱스를 재사용한다. 어느 쪽이든 `it->second`가 그 정점의 최종 인덱스이므로 인덱스 버퍼에 그대로 밀어 넣는다.

UV의 V 좌표를 `1.0f -`로 뒤집는 한 줄이 중요하다. OBJ의 텍스처 좌표는 좌하단을 원점으로 삼지만, Vulkan의 텍스처 좌표계는 좌상단을 원점으로 삼는다. V축의 방향이 반대이므로, 뒤집지 않으면 텍스처가 상하로 뒤집혀 매핑된다. glTF는 이미 좌상단 원점을 따르므로 glTF 경로에서는 이 뒤집기를 적용하지 않는다.

### 효과

중복 제거가 실제로 줄이는 정점 수는 모델에 따라 차이가 크다. 면을 공유하는 폐곡면일수록 효과가 크다.

| 모델 | 원본 정점 (face × 3) | 중복 제거 후 | 감소율 |
|---:|---:|---:|---:|
| 큐브 | 36 | 24 | 33% |
| Stanford Bunny | 208,353 | 35,947 | 83% |
| Sponza | 786,432 | 152,015 | 81% |
| Suzanne (Blender) | 2,904 | 507 | 83% |

대형 폐곡면 모델에서 정점 수가 80% 안팎까지 줄어든다. 메모리 절감도 의미 있지만, 더 큰 이득은 GPU의 변환 후 정점 캐시(post-transform vertex cache)다. 인덱스 버퍼를 통해 같은 정점을 여러 면이 참조하면, GPU가 한 번 변환한 정점 셰이더 결과를 캐시에 두고 재활용한다. 중복 정점이 펼쳐져 있으면 같은 위치를 매번 다시 변환하므로 캐시가 무력화된다. 작은 코드 한 벌로 메모리와 정점 셰이더 부하를 동시에 줄이는 셈이다.

법선이 분리된 정점에서는 의도된 중복이 남는다. 큐브의 한 모서리 점은 세 면에 속하지만 면마다 법선이 다르므로, 위치는 같아도 법선이 달라 별개의 정점으로 유지된다. 큐브가 8개가 아니라 24개로 남는 이유가 이것이다. 부드러운(smooth) 법선을 쓰는 모델일수록 더 많이 합쳐진다.

### Vulkan 버퍼 업로드

정점·인덱스 데이터는 CPU 메모리에 만들어졌으므로 GPU가 빠르게 접근하는 디바이스 로컬 메모리(VRAM)로 옮겨야 한다. Vulkan은 메모리 종류를 명시적으로 구분하므로, CPU가 쓸 수 있는 스테이징 버퍼에 먼저 채운 뒤 GPU 전용 버퍼로 복사하는 두 단계 패턴을 쓴다.

```cpp
// 1단계: Host Visible 스테이징 버퍼에 채운다
VkBuffer stagingBuf;
VkDeviceMemory stagingMem;
CreateBuffer(bufferSize,
             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
             | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
             stagingBuf, stagingMem);

void* data;
vkMapMemory(device, stagingMem, 0, bufferSize, 0, &data);
memcpy(data, vertices.data(), bufferSize);
vkUnmapMemory(device, stagingMem);

// 2단계: Device Local 최종 버퍼를 만들고 복사한다
CreateBuffer(bufferSize,
             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
             | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
             vertexBuffer, vertexMemory);

CopyBuffer(stagingBuf, vertexBuffer, bufferSize);  // 전송 커맨드 버퍼

vkDestroyBuffer(device, stagingBuf, nullptr);
vkFreeMemory(device, stagingMem, nullptr);
```

`HOST_VISIBLE`은 CPU가 매핑해 쓸 수 있는 메모리이고, `HOST_COHERENT`는 `vkFlushMappedMemoryRanges` 없이도 쓰기가 GPU에 보이도록 보장한다. `DEVICE_LOCAL`은 VRAM이며 CPU가 직접 매핑하지 못하는 대신 GPU 접근 대역폭이 가장 높다. DX12의 업로드 힙에서 디폴트 힙으로 복사하는 패턴과 정확히 같은 구조다. 스테이징 버퍼는 복사가 끝나면 즉시 해제하는데, 실제로는 복사 커맨드가 완료된 뒤에 해제해야 하므로 펜스나 큐 대기로 완료를 보장한 다음 정리한다.

작은 정점 데이터마다 별도 `vkAllocateMemory`를 호출하는 것은 학습 단계에서는 명료하지만, 드라이버가 보장하는 동시 할당 수(`maxMemoryAllocationCount`, 보통 4,096)에 묶인다. 실제 프로젝트에서는 큰 메모리 블록 하나를 할당해 서브 할당하는 할당자로 묶는다.

## 3. 깊이 첨부 자원 생성

깊이 버퍼는 화면의 각 픽셀이 현재 가장 가까운 표면의 깊이를 기록하는 이미지다. 프래그먼트가 들어올 때마다 깊이 테스트로 기존 값과 비교해, 더 가까우면 색을 쓰고 깊이를 갱신하며 더 멀면 버린다. 이 비교 한 번으로 그리는 순서와 무관하게 가려진 면이 잘려 나간다. Vulkan에서 깊이 버퍼는 별도의 이미지 자원이며, 컬러 첨부와 마찬가지로 이미지, 이미지 뷰, 메모리를 직접 만든다.

### 포맷 선택

Vulkan의 표준 깊이 포맷은 정밀도와 스텐실 동반 여부로 나뉜다.

| 포맷 | 깊이 | 스텐실 | 바이트/픽셀 | 비고 |
|---|---|---|---:|---|
| `D32_SFLOAT` | 32비트 float | 없음 | 4 | 가장 정밀, 스텐실 없음 |
| `D24_UNORM_S8_UINT` | 24비트 정규화 | 8비트 | 4 | 흔함, GL과 호환 |
| `D32_SFLOAT_S8_UINT` | 32비트 float | 8비트 | 5~8 | 정밀 + 스텐실 |
| `D16_UNORM` | 16비트 정규화 | 없음 | 2 | 모바일/저정밀 |

모든 GPU가 모든 포맷을 지원하지는 않는다. 특히 `D24_UNORM_S8_UINT`와 `D32_SFLOAT_S8_UINT` 중 어느 쪽이 최적 타일링으로 지원되는지는 벤더마다 갈린다. NVIDIA는 전통적으로 `D24_UNORM_S8_UINT`를 32비트에 패킹해 다뤘고, AMD는 깊이와 스텐실을 분리 저장해 두 포맷의 비용 특성이 다르다. 따라서 후보 목록을 두고 첫 지원 포맷을 고르는 질의가 필요하다.

```cpp
VkFormat FindDepthFormat(VkPhysicalDevice gpu) {
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat f : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(gpu, f, &props);
        // optimalTiling 으로 깊이/스텐실 첨부가 가능한지 확인한다
        if (props.optimalTilingFeatures
            & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return f;
        }
    }
    return VK_FORMAT_UNDEFINED;
}
```

`optimalTilingFeatures`를 검사하는 이유는 깊이 첨부가 항상 최적 타일링(드라이버가 압축·재배치한 불투명 레이아웃)으로 만들어지기 때문이다. 선형 타일링은 CPU 접근용이라 깊이 첨부로는 거의 지원되지 않는다.

### Image, ImageView, Memory

깊이 이미지는 컬러와 동일한 절차로 만들되, 용도 플래그가 `DEPTH_STENCIL_ATTACHMENT_BIT`이고 이미지 뷰의 aspect가 깊이를 가리킨다는 점이 다르다.

```cpp
void CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat(gpu);

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = { swapWidth, swapHeight, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = depthFormat;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.samples = VK_SAMPLE_COUNT_1_BIT;       // MSAA 도입 전
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(device, &info, nullptr, &depthImage);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, depthImage, &memReq);
    // DEVICE_LOCAL 메모리 할당 + vkBindImageMemory (생략)

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(device, &viewInfo, nullptr, &depthImageView);
}
```

스텐실을 동반하는 포맷이면 이미지 뷰의 aspect에 `VK_IMAGE_ASPECT_STENCIL_BIT`을 함께 켠다. 다만 한 이미지 뷰가 깊이와 스텐실을 동시에 가리키는 것은 첨부 용도에서만 허용되고, 셰이더에서 샘플링할 때는 깊이 또는 스텐실 중 하나만 가리키는 별도 뷰가 필요하다. 깊이 버퍼의 메모리 크기는 해상도와 픽셀당 바이트로 직접 계산된다. 1920×1080 해상도에서 `D32_SFLOAT`은 약 8MB, `D24_UNORM_S8_UINT`는 약 8MB다. MSAA를 4×로 켜면 이 값이 4배가 되므로 깊이 첨부만으로 32MB에 이른다.

### Layout 전이

방금 만든 깊이 이미지의 초기 내용은 정의되지 않았다. 깊이 첨부로 쓰려면 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` 같은 호환 레이아웃이 필요하다. 렌더 패스의 attachment `initialLayout`을 `UNDEFINED`, 첫 subpass의 depth reference layout을 attachment-optimal로 선언하면 render pass 시작의 implicit transition이 이를 수행한다. 이전 내용을 버리고 매 프레임 clear하는 경우에만 `UNDEFINED`와 `DONT_CARE`가 맞으며, 내용을 보존해 읽어야 한다면 올바른 이전 레이아웃과 동기화를 제공해야 한다.

## 4. Render Pass에 깊이 추가

렌더 패스는 프레임버퍼 첨부의 집합, 그 첨부를 사용하는 서브패스, 서브패스 간 의존성을 한 객체로 묶은 것이다. 깊이 테스트를 켜려면 컬러 첨부 하나만 있던 렌더 패스에 깊이 첨부를 더하고, 서브패스가 그 깊이 첨부를 깊이/스텐실 용도로 참조하게 만든다.

### Attachment 두 개 — 컬러와 깊이

각 첨부는 포맷, 샘플 수, 로드/스토어 연산, 시작·종료 레이아웃을 기술한다. 컬러는 그려서 화면에 표시하므로 `STORE`하고, 깊이는 이번 프레임의 가시성 판정에만 쓰고 다음 프레임에 다시 클리어하므로 `DONT_CARE`로 버린다.

```cpp
VkAttachmentDescription colorAtt{};
colorAtt.format = swapchainFormat;
colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorAtt.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

VkAttachmentDescription depthAtt{};
depthAtt.format = depthFormat;
depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
depthAtt.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
```

깊이의 `storeOp`를 `DONT_CARE`로 두는 것은 단순한 절약이 아니다. 타일 기반 GPU에서는 깊이 버퍼를 DRAM에 기록하지 않고 온칩 타일 메모리에만 두었다가 버릴 수 있어, 깊이 첨부 전체의 메모리 대역폭이 사라진다. 깊이를 후처리에서 다시 읽어야 한다면 그때만 `STORE`로 바꾼다.

### Subpass

서브패스는 첨부를 어떤 용도로 쓰는지 참조로 연결한다. 컬러는 `pColorAttachments`, 깊이는 `pDepthStencilAttachment`로 분리된다. 참조의 `attachment` 인덱스는 렌더 패스 첨부 배열의 위치이고, `layout`은 그 서브패스 동안의 레이아웃이다.

```cpp
VkAttachmentReference colorRef{};
colorRef.attachment = 0;
colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

VkAttachmentReference depthRef{};
depthRef.attachment = 1;
depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

VkSubpassDescription subpass{};
subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
subpass.colorAttachmentCount = 1;
subpass.pColorAttachments = &colorRef;
subpass.pDepthStencilAttachment = &depthRef;
```

컬러는 여러 개를 동시에 출력할 수 있어 배열과 개수를 받지만, 깊이/스텐실 첨부는 서브패스당 정확히 하나여서 단일 포인터다. `pDepthStencilAttachment`를 비워 두면 깊이 테스트가 동작하지 않으므로, 깊이 이미지를 만들고 프레임버퍼에 붙여도 이 한 줄을 빠뜨리면 깊이 비교가 일어나지 않는다.

### Subpass Dependency

서브패스 의존성은 렌더 패스 바깥의 작업과 안의 작업 사이, 또는 서브패스 사이의 실행·메모리 순서를 정한다. 깊이 첨부가 이전 프레임의 깊이 테스트를 끝낸 뒤에 새 프레임의 클리어와 쓰기가 일어나도록 보장해야 한다.

```cpp
VkSubpassDependency dep{};
dep.srcSubpass = VK_SUBPASS_EXTERNAL;
dep.dstSubpass = 0;
dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                 | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                 | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
dep.srcAccessMask = 0;
dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                  | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
```

깊이 쓰기는 `EARLY_FRAGMENT_TESTS` 단계에서 일어나므로, 컬러 출력 단계뿐 아니라 이 단계까지 동기화 범위에 포함한다. 이 의존성이 없으면 검증 레이어가 레이아웃 전이와 첨부 쓰기 사이의 위험을 경고하고, 일부 드라이버에서는 깊이 클리어가 직전 프레임의 읽기와 겹쳐 깜빡임이 나타난다.

### Render Pass와 Framebuffer 생성

첨부 배열을 모아 렌더 패스를 만들고, 같은 순서로 이미지 뷰를 프레임버퍼에 붙인다. 첨부 인덱스와 이미지 뷰 배열의 순서가 일치해야 한다.

```cpp
VkAttachmentDescription atts[] = { colorAtt, depthAtt };

VkRenderPassCreateInfo rpInfo{};
rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
rpInfo.attachmentCount = 2;
rpInfo.pAttachments = atts;
rpInfo.subpassCount = 1;
rpInfo.pSubpasses = &subpass;
rpInfo.dependencyCount = 1;
rpInfo.pDependencies = &dep;
vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass);

// 인덱스 0 = 컬러, 인덱스 1 = 깊이. 첨부 기술과 순서 일치
VkImageView attachments[] = { swapchainImageView, depthImageView };

VkFramebufferCreateInfo fbInfo{};
fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
fbInfo.renderPass = renderPass;
fbInfo.attachmentCount = 2;
fbInfo.pAttachments = attachments;
fbInfo.width = swapWidth;
fbInfo.height = swapHeight;
fbInfo.layers = 1;
vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer);
```

스왑체인 이미지는 여러 장이므로 프레임버퍼도 이미지마다 하나씩 만든다. 깊이 이미지는 한 장만 두고 모든 프레임버퍼가 공유해도 무방한데, GPU가 한 프레임을 끝낸 뒤 다음 프레임이 깊이를 다시 클리어하므로 프레임 간 깊이 내용이 보존될 필요가 없기 때문이다. 다만 여러 프레임을 동시에 비행(in-flight)시키면 깊이 충돌이 생기므로, 그 경우에는 깊이도 프레임 수만큼 둔다.

### Clear Value

`loadOp`를 `CLEAR`로 둔 첨부는 렌더 패스 시작 시 클리어 값으로 초기화된다. 클리어 값 배열의 순서도 첨부 인덱스를 따른다.

```cpp
VkClearValue clear[2];
clear[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
clear[1].depthStencil = { 1.0f, 0 };   // 깊이 1.0 = 가장 멀리

VkRenderPassBeginInfo begin{};
begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
begin.renderPass = renderPass;
begin.framebuffer = framebuffer;
begin.renderArea.extent = { swapWidth, swapHeight };
begin.clearValueCount = 2;
begin.pClearValues = clear;

vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
```

깊이 클리어 값 1.0은 NDC 깊이 범위 `[0, 1]`에서 가장 먼 값이다. 비교 연산이 `LESS`일 때 처음 값을 1.0으로 두면 더 가까운 프래그먼트가 통과한다. Reverse-Z는 near를 큰 깊이, far를 작은 깊이에 매핑하므로 clear를 0.0으로 바꾸고 비교 연산도 `GREATER` 또는 `GREATER_OR_EQUAL` 계열로 맞춘다.

## 5. Pipeline의 Depth Stencil State

렌더 패스가 깊이 첨부를 갖췄어도, 파이프라인이 깊이 테스트를 켜지 않으면 비교가 일어나지 않는다. 그래픽스 파이프라인 생성 정보의 `pDepthStencilState`가 이 동작을 정의한다.

```cpp
VkPipelineDepthStencilStateCreateInfo dss{};
dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
dss.depthTestEnable = VK_TRUE;     // 깊이 비교 수행
dss.depthWriteEnable = VK_TRUE;    // 통과 시 깊이 갱신
dss.depthCompareOp = VK_COMPARE_OP_LESS;
dss.depthBoundsTestEnable = VK_FALSE;
dss.stencilTestEnable = VK_FALSE;
dss.minDepthBounds = 0.0f;
dss.maxDepthBounds = 1.0f;

// pipelineCreateInfo.pDepthStencilState = &dss;
```

`depthTestEnable`과 `depthWriteEnable`은 독립적이다. 불투명 물체는 둘 다 켜고, 반투명 물체는 테스트는 하되 쓰기는 끄는 경우가 흔하다. 반투명 면이 서로 가려져도 깊이를 갱신하지 않아야 뒤에 그릴 면이 올바르게 블렌딩되기 때문이다. `depthCompareOp`가 `LESS`이면 "더 작은(가까운) 깊이만 통과"가 되어, 클리어 값 1.0에서 시작해 가까운 면이 먼 면을 덮는다.

### Reverse-Z 설정

원근 투영에서 깊이 값은 비선형으로 분포한다. 표준 매핑(near → 0, far → 1)에서는 정밀도가 near 근처에 과도하게 몰리고 far 근처가 성겨, 멀리 있는 두 표면이 같은 깊이로 양자화되어 Z-fighting이 발생한다. 부동소수의 비트 분포가 0 근처에서 촘촘하다는 성질을 이용해, near를 1.0에, far를 0.0에 매핑하면 부동소수 정밀도와 원근 비선형성이 상쇄되어 전 범위에 고른 정밀도를 얻는다. 이를 Reverse-Z라 한다.

Reverse-Z는 다섯 가지를 모두 함께 맞춰야 효과가 난다. 하나라도 어긋나면 화면이 깨지거나 정밀도 이득이 사라진다.

첫째, 투영 행렬을 near → 1.0, far → 0.0으로 재구성한다.

```cpp
glm::mat4 proj = glm::perspective(fov, aspect, nearZ, farZ);
// 깊이 매핑을 뒤집는다
proj[2][2] = nearZ / (farZ - nearZ);
proj[3][2] = (nearZ * farZ) / (farZ - nearZ);
// 또는 GLM_FORCE_DEPTH_ZERO_TO_ONE 빌드 + reverse 헬퍼
```

둘째, `depthCompareOp`를 `VK_COMPARE_OP_GREATER` 또는 `VK_COMPARE_OP_GREATER_OR_EQUAL`로 바꾼다. 가까운 면이 큰 값을 가지므로 비교 방향이 뒤집힌다. 셋째, 깊이 클리어 값을 0.0으로 둔다. 넷째, `loadOp`는 그대로 `CLEAR`를 유지한다. 다섯째, 깊이 포맷은 `VK_FORMAT_D32_SFLOAT`을 권장한다. Reverse-Z의 이득은 부동소수 비트 분포에서 나오므로, 정규화 정수 포맷인 `D24_UNORM`에서는 이득이 거의 없다.

다섯이 모두 맞으면 far plane을 수십 km까지 늘려도 Z-fighting이 사라진다. 표준 24비트 깊이에서 near를 0.1로 두면 수백 m 밖에서 깊이 충돌이 시작되지만, 32비트 Reverse-Z는 같은 near에서 훨씬 먼 거리까지 안정적이다.

### Viewport의 minDepth와 maxDepth

Vulkan의 NDC 깊이 범위는 `[0, 1]`이다. OpenGL의 `[-1, 1]`과 다르므로, OpenGL용으로 만든 투영 행렬을 그대로 쓰면 깊이가 절반만 매핑되어 가까운 절반이 잘린다. 뷰포트의 `minDepth`와 `maxDepth`는 이 `[0, 1]` 범위 안에서 다시 매핑할 하위 구간을 정하며, 기본값은 전체 구간이다.

```cpp
VkViewport vp{};
vp.x = 0; vp.y = 0;
vp.width  = (float)swapWidth;
vp.height = (float)swapHeight;
vp.minDepth = 0.0f;
vp.maxDepth = 1.0f;
```

또 하나의 컨벤션 차이는 Y축이다. Vulkan의 클립 공간 Y는 아래로 증가해, OpenGL용 셰이더를 그대로 쓰면 화면이 상하로 뒤집힌다. 뷰포트의 높이를 음수로 두고 원점 y를 화면 높이로 올리면 Y가 뒤집혀, OpenGL에서 가져온 셰이더와 정점 데이터가 수정 없이 동작한다. 이 기능은 Vulkan 1.1 코어이거나 `VK_KHR_maintenance1` 확장에서 제공된다.

```cpp
vp.y = (float)swapHeight;
vp.height = -(float)swapHeight;
```

## 6. MSAA 활성화

깊이 정렬까지 마치면 형상은 올바르게 가려지지만 가장자리가 계단처럼 거칠다. 래스터화가 픽셀 중심 하나로 삼각형 안팎을 판정하므로, 경계 픽셀이 전부 채워지거나 전부 비는 이진 결정만 내리기 때문이다. MSAA는 한 픽셀 안에 여러 표본점을 두고 각 표본이 삼각형 안에 드는지를 따로 판정해, 경계 픽셀의 커버리지를 단계적으로 표현한다.

### 지원 샘플 수 확인

샘플 수는 GPU가 지원하는 범위와 제품의 품질 설정 안에서 골라야 한다. 컬러 첨부와 깊이 첨부가 같은 샘플 수를 가져야 하므로 두 한계의 교집합에서 요청한 값 이하를 선택한다. 지원하는 최대값이 기본값이라는 뜻은 아니다.

```cpp
VkSampleCountFlagBits PickSampleCount(VkPhysicalDevice gpu,
                                      VkSampleCountFlagBits requested) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);

    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts
                              & props.limits.framebufferDepthSampleCounts;

    const VkSampleCountFlagBits candidates[] = {
        VK_SAMPLE_COUNT_64_BIT, VK_SAMPLE_COUNT_32_BIT,
        VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_8_BIT,
        VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_2_BIT,
        VK_SAMPLE_COUNT_1_BIT
    };
    for (auto candidate : candidates) {
        if (candidate <= requested && (counts & candidate)) return candidate;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}
```

MSAA 표본 수를 늘리면 다중샘플 첨부의 저장량과 일부 래스터·깊이·블렌드 작업량이 증가하지만, 실제 비용은 타일 메모리, 압축, resolve 방식, sample shading 사용 여부에 따라 달라진다. 4×는 흔한 시작점일 뿐 보편적 최적값이 아니다. 1×·2×·4×·8×을 품질 설정으로 두고 목표 GPU에서 GPU 시간과 메모리, 최종 이미지 품질을 함께 측정한다.

### 컬러 첨부 MSAA 이미지

MSAA를 켜면 화면에 표시되는 단일 샘플 스왑체인 이미지 외에 멀티 샘플 컬러 첨부가 필요하다. Subpass의 resolve attachment를 지정하면 각 픽셀의 여러 color sample을 단일 샘플 이미지로 resolve한다. 정수 포맷이나 depth/stencil resolve는 지원 방식과 연산이 다르므로 color 평균과 같은 계약으로 가정하지 않는다.

```cpp
VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;

VkImageCreateInfo colorImgInfo{};
colorImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
colorImgInfo.imageType = VK_IMAGE_TYPE_2D;
colorImgInfo.extent = { swapWidth, swapHeight, 1 };
colorImgInfo.mipLevels = 1;
colorImgInfo.arrayLayers = 1;
colorImgInfo.format = swapchainFormat;
colorImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
colorImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorImgInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
                   | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
colorImgInfo.samples = samples;
colorImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

vkCreateImage(device, &colorImgInfo, nullptr, &colorMsaaImage);
```

`VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`는 이 첨부가 렌더 패스 안에서만 살아 있고 밖으로 저장되지 않음을 드라이버에 알린다. 타일 기반 GPU에서는 이 힌트와 함께 메모리를 `LAZILY_ALLOCATED`로 할당하면 멀티 샘플 컬러가 온칩 타일 메모리에만 머물러 DRAM을 거의 차지하지 않는다. Resolve 결과만 DRAM에 기록되므로, 멀티 샘플 첨부의 대역폭 비용이 사라진다.

### 깊이도 같은 샘플 수로

서브패스의 모든 첨부는 샘플 수가 같아야 한다. 컬러를 4×로 만들면 깊이도 4×로 다시 만든다.

```cpp
VkImageCreateInfo depthImgInfo{};   // 위와 같은 base 설정
depthImgInfo.format = depthFormat;
depthImgInfo.samples = samples;     // 컬러와 동일
depthImgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                   | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

vkCreateImage(device, &depthImgInfo, nullptr, &depthMsaaImage);
```

깊이는 화면에 직접 표시되지 않으므로 resolve할 필요가 없고, MSAA 깊이 그대로 가시성 판정에만 쓰인다. 깊이도 `TRANSIENT`로 두면 DRAM 비용을 더 줄인다.

### Pipeline의 multisample state

파이프라인이 래스터화할 표본 수를 첨부와 일치시킨다.

```cpp
VkPipelineMultisampleStateCreateInfo ms{};
ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
ms.rasterizationSamples = samples;     // 첨부와 동일해야 한다
ms.sampleShadingEnable = VK_FALSE;
ms.minSampleShading = 1.0f;
ms.pSampleMask = nullptr;
ms.alphaToCoverageEnable = VK_FALSE;
ms.alphaToOneEnable = VK_FALSE;
```

`rasterizationSamples`가 첨부의 샘플 수와 다르면 파이프라인 생성이 실패한다. 이 상태에서 기본 MSAA는 프래그먼트 셰이더를 픽셀당 한 번만 실행하고, 깊이·스텐실·커버리지만 표본 단위로 평가한다. 셰이더 비용은 늘지 않으면서 기하 가장자리만 부드러워지는 이유다.

## 7. Resolve Attachment 구성

멀티 샘플 컬러 이미지는 한 픽셀에 4개의 색 표본을 들고 있어 화면에 바로 표시할 수 없다. 스왑체인 이미지는 단일 샘플이기 때문이다. 4개 표본을 평균 내어 단일 샘플로 만드는 과정을 resolve라 하고, Vulkan은 이를 서브패스 종료 시 자동으로 수행한다. Resolve 대상이 될 단일 샘플 첨부를 하나 더 둔다.

### 첨부 세 개 — MSAA 컬러, 깊이, Resolve

멀티 샘플 컬러는 resolve 후 버려지므로 `storeOp`가 `DONT_CARE`이고, resolve 대상인 단일 샘플 컬러가 실제로 화면에 표시되어 `STORE`된다.

```cpp
VkAttachmentDescription colorMsaa{};
colorMsaa.format = swapchainFormat;
colorMsaa.samples = samples;
colorMsaa.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
colorMsaa.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // resolve 후 버림
colorMsaa.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorMsaa.finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

VkAttachmentDescription depthMsaa{};
depthMsaa.format = depthFormat;
depthMsaa.samples = samples;
depthMsaa.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
depthMsaa.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
depthMsaa.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
depthMsaa.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

VkAttachmentDescription colorResolve{};
colorResolve.format = swapchainFormat;
colorResolve.samples = VK_SAMPLE_COUNT_1_BIT;          // 단일 샘플
colorResolve.loadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
colorResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
colorResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
colorResolve.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
```

Resolve 첨부의 `loadOp`는 `DONT_CARE`다. resolve가 전체 픽셀을 덮어쓰므로 이전 내용을 클리어할 이유가 없다. `finalLayout`은 화면 표시용인 `PRESENT_SRC_KHR`이며, 이제 스왑체인 이미지가 멀티 샘플 컬러가 아니라 이 resolve 첨부에 연결된다.

### Subpass에 Resolve 추가

서브패스의 `pResolveAttachments` 배열이 컬러 첨부와 1대 1로 대응한다. `pResolveAttachments[i]`가 `pColorAttachments[i]`의 resolve 대상이다.

```cpp
VkAttachmentReference colorMsaaRef{};
colorMsaaRef.attachment = 0;
colorMsaaRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

VkAttachmentReference depthMsaaRef{};
depthMsaaRef.attachment = 1;
depthMsaaRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

VkAttachmentReference colorResolveRef{};
colorResolveRef.attachment = 2;
colorResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

VkSubpassDescription subpass{};
subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
subpass.colorAttachmentCount = 1;
subpass.pColorAttachments = &colorMsaaRef;
subpass.pDepthStencilAttachment = &depthMsaaRef;
subpass.pResolveAttachments = &colorResolveRef;   // 핵심
```

이 한 포인터로 서브패스가 종료될 때 GPU가 멀티 샘플 컬러를 단일 샘플로 평균 내어 resolve 첨부에 기록한다. 프레임버퍼에는 세 이미지 뷰를 첨부 인덱스 순서(MSAA 컬러, MSAA 깊이, resolve)로 붙이고, resolve 자리에 스왑체인 이미지 뷰를 넣는다. 명령 측에서는 추가 호출이 없다. `vkCmdEndRenderPass`가 resolve를 포함해 처리한다.

깊이는 일반적으로 컬러처럼 평균 resolve하지 않는다. 후처리에서 단일 샘플 깊이가 필요하면 Vulkan 1.2의 `VkSubpassDescriptionDepthStencilResolve`로 min/max/sample-zero 같은 지원 resolve mode를 선택하거나, 멀티샘플 깊이를 셰이더에서 직접 읽어 목적에 맞게 resolve한다. `vkCmdResolveImage`는 color aspect 전용이므로 깊이·스텐실 이미지에는 사용할 수 없다.

### 타일 기반 GPU의 최적화

모바일 GPU인 ARM Mali, Qualcomm Adreno, Apple GPU는 프레임버퍼를 16×16에서 64×64 정도의 작은 타일 단위로 처리한다. 멀티 샘플 컬러와 깊이가 온칩 타일 메모리에 머물고, 서브패스가 끝날 때 타일 안에서 resolve를 수행해 단일 샘플 결과만 DRAM에 기록한다. 멀티 샘플 데이터가 외부 메모리를 왕복하지 않으므로, 4× MSAA의 실효 비용이 4배가 아니라 1.5배 안팎에 그친다. 데스크톱의 즉시 모드(immediate mode) GPU에서는 멀티 샘플 첨부가 VRAM을 그대로 차지하므로 비용이 더 든다. 타일 기반 GPU에서 MSAA가 현실적인 선택지인 이유가 이 메모리 모델의 차이다.

이 차이는 Vulkan의 렌더 패스·서브패스·resolve 추상이 왜 명시적인지를 설명한다. 첨부의 로드/스토어 연산, transient 힌트, 서브패스 경계가 모두 타일 메모리의 적재·기록 시점을 드라이버에 알리는 신호다. DX12가 자동으로 처리하는 부분을 Vulkan이 코드 표면에 노출한 결과다.

## 8. 샘플 셰이딩

기본 MSAA는 프래그먼트 셰이더를 픽셀당 한 번만 실행한다. 한 픽셀의 4개 표본은 같은 셰이더 출력 색을 공유하고, 표본 단위로 다른 것은 커버리지·깊이·스텐실뿐이다. 그래서 삼각형 기하의 가장자리는 부드러워지지만, 알파 테스트로 잘라낸 텍스처 경계나 셰이더 안에서 만든 고주파 패턴의 가장자리는 여전히 거칠다. 그 경계는 기하가 아니라 셰이더 출력에서 생기기 때문이다.

### Sample Shading 활성화

샘플 셰이딩은 프래그먼트 셰이더를 표본 단위로 실행해 이 한계를 해소한다. 먼저 디바이스 기능으로 켜야 한다.

```cpp
VkPhysicalDeviceFeatures features{};
features.sampleRateShading = VK_TRUE;
// vkCreateDevice 의 pEnabledFeatures 에 연결
```

그다음 파이프라인 멀티 샘플 상태에서 셰이딩 비율을 정한다.

```cpp
VkPipelineMultisampleStateCreateInfo ms{};
ms.rasterizationSamples = samples;
ms.sampleShadingEnable = VK_TRUE;
ms.minSampleShading = 0.25f;   // 표본의 최소 25% 에서 셰이더 실행
```

`minSampleShading`은 한 픽셀에서 최소 몇 비율의 표본을 독립 실행할지의 하한이다. 4× MSAA에서 `1.0`이면 4개 표본 모두 셰이더를 실행해 픽셀당 셰이더 비용이 네 배가 된다. `0.25`이면 표본 중 한 개꼴로 실행해 비용은 기본 MSAA와 비슷하면서도, 표본별로 보간 위치가 달라져 알파 경계의 표현이 개선된다.

### 셰이더 측

표본 단위 입력을 받으려면 보간 한정자 `sample`을 붙인다. 그러면 같은 픽셀의 표본마다 다른 위치에서 보간된 값을 받는다.

```glsl
layout(location = 0) sample in vec2 vUv;

void main() {
    // 같은 픽셀이라도 표본 위치마다 vUv 가 다르다
    outColor = texture(diffuseMap, vUv);
}
```

알파 텍스트나 잎사귀 텍스처처럼 텍스처 알파로 경계를 만드는 표면에서 가장자리가 눈에 띄게 매끄러워진다. 비용은 실제로 실행되는 프래그먼트 셰이더 호출 수에 비례하므로, 화면 전체에 무차별로 켜기보다 알파 경계가 많은 머티리얼에만 적용하고 프로파일링으로 비율을 정한다.

### Alpha to Coverage — 대안

셰이더 비용을 늘리지 않고 알파 경계를 다듬는 다른 방법이 alpha to coverage다. 프래그먼트의 알파 값을 MSAA 커버리지 마스크로 변환해, 알파 1.0이면 모든 표본을 활성화하고 0.5면 절반, 0.0이면 모두 비활성화한다.

```cpp
ms.alphaToCoverageEnable = VK_TRUE;
```

셰이더는 여전히 픽셀당 한 번만 실행되므로 추가 셰이더 비용이 없다. 알파를 커버리지로 양자화하므로 부드러움의 단계가 표본 수에 묶이지만(4×면 5단계), 잎사귀나 머리카락처럼 경계가 많고 정렬이 어려운 표면에 잘 맞는다. 알파 블렌딩과 달리 그리는 순서에 의존하지 않아 깊이 정렬 문제를 피한다는 장점도 있다.


---

## 결론

여기까지의 코드는 외부 메시가 깊이 정렬된 상태로 부드러운 가장자리를 가지고 그려지는 장면을 만든다. tinyobjloader와 tinygltf로 OBJ와 glTF를 읽고, 해시 맵으로 정점 중복을 제거해 정점 수와 정점 캐시 부하를 함께 낮추고, 깊이 첨부를 렌더 패스에 통합해 가려진 면을 잘라냈다. MSAA 멀티 샘플 첨부와 자동 resolve로 기하 가장자리를 다듬고, 샘플 셰이딩과 alpha to coverage로 셰이더 출력의 가장자리까지 손댈 수 있게 됐다. Vulkan의 명시적 명령 모델 위에 현실적인 메시 렌더링의 토대가 모인 셈이다.

이 글의 트레이드오프는 세 축이다. 첫째, 포맷 선택이다. OBJ는 단순해 원리를 보이기 좋지만 PBR과 애니메이션을 담지 못하고, glTF는 표준이지만 접근자·버퍼 뷰의 간접 참조를 다뤄야 한다. 둘째, 깊이 정밀도다. 24비트 정규화 깊이는 흔하고 호환성이 높지만, 넓은 야외 장면에서는 32비트 부동소수 Reverse-Z가 Z-fighting을 막는다. 셋째, MSAA 비용이다. 타일 기반 GPU에서는 4×가 1.5배 비용에 그치지만 즉시 모드 데스크톱 GPU에서는 첨부 메모리와 대역폭이 그대로 늘어, 같은 4×라도 플랫폼에 따라 비용이 다르다.

셰이딩은 텍스처를 그대로 복사하는 수준이라 빛의 방향이나 표면 거칠기를 반영하지 못한다. 물리 기반 재질은 베이스 컬러·금속성·거칠기를 Cook-Torrance BRDF에 연결하고, IBL을 사용한다면 irradiance map, 정반사 prefilter와 BRDF LUT의 생성·수명·descriptor binding을 함께 관리해야 한다. 전처리를 compute queue에 보낼 때도 queue-family ownership, semaphore와 결과 이미지의 layout 전이가 소비 graphics queue의 읽기보다 먼저 완료되어야 한다.

## 더 읽을거리

- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) — 접근자·버퍼 뷰·PBR 머티리얼의 표준.
- [tinygltf](https://github.com/syoyo/tinygltf) / [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) — 본문 로더의 저장소.
- [Vulkan Tutorial: Depth buffering / Loading models / Multisampling](https://vulkan-tutorial.com/Depth_buffering) — 같은 경로의 입문 구현 자료.
- ARM, [Mali GPU Best Practices](https://developer.arm.com/documentation/101897/latest/) — 타일 기반 GPU 에서의 MSAA 비용 근거.
