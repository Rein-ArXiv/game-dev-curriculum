#include "scene/scene.h"

#include "device/d3d12_context.h"
#include "device/upload_buffer.h"

#include <DirectXMath.h>

using namespace DirectX;

namespace dxlab {

namespace {

struct Vertex {
    XMFLOAT3 pos;
    XMFLOAT3 normal;
};

const Vertex kCube[] = {
    // -X
    {{-0.5f,-0.5f,-0.5f},{-1, 0, 0}}, {{-0.5f, 0.5f,-0.5f},{-1, 0, 0}},
    {{-0.5f, 0.5f, 0.5f},{-1, 0, 0}}, {{-0.5f,-0.5f, 0.5f},{-1, 0, 0}},
    // +X
    {{ 0.5f,-0.5f,-0.5f},{ 1, 0, 0}}, {{ 0.5f, 0.5f,-0.5f},{ 1, 0, 0}},
    {{ 0.5f, 0.5f, 0.5f},{ 1, 0, 0}}, {{ 0.5f,-0.5f, 0.5f},{ 1, 0, 0}},
    // -Y
    {{-0.5f,-0.5f,-0.5f},{ 0,-1, 0}}, {{ 0.5f,-0.5f,-0.5f},{ 0,-1, 0}},
    {{ 0.5f,-0.5f, 0.5f},{ 0,-1, 0}}, {{-0.5f,-0.5f, 0.5f},{ 0,-1, 0}},
    // +Y
    {{-0.5f, 0.5f,-0.5f},{ 0, 1, 0}}, {{ 0.5f, 0.5f,-0.5f},{ 0, 1, 0}},
    {{ 0.5f, 0.5f, 0.5f},{ 0, 1, 0}}, {{-0.5f, 0.5f, 0.5f},{ 0, 1, 0}},
    // -Z
    {{-0.5f,-0.5f,-0.5f},{ 0, 0,-1}}, {{ 0.5f,-0.5f,-0.5f},{ 0, 0,-1}},
    {{ 0.5f, 0.5f,-0.5f},{ 0, 0,-1}}, {{-0.5f, 0.5f,-0.5f},{ 0, 0,-1}},
    // +Z
    {{-0.5f,-0.5f, 0.5f},{ 0, 0, 1}}, {{ 0.5f,-0.5f, 0.5f},{ 0, 0, 1}},
    {{ 0.5f, 0.5f, 0.5f},{ 0, 0, 1}}, {{-0.5f, 0.5f, 0.5f},{ 0, 0, 1}},
};

const uint16_t kCubeIdx[] = {
     0, 2, 1,  0, 3, 2,
     4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11,
    12,14,13, 12,15,14,
    16,18,17, 16,19,18,
    20,21,22, 20,22,23,
};

}  // namespace

void Scene::BuildMesh(D3D12Context& ctx,
                      ID3D12GraphicsCommandList* uploadCmd,
                      std::vector<ComPtr<ID3D12Resource>>& keepAlive) {
    ComPtr<ID3D12Resource> stagingVB, stagingIB;
    vb_ = CreateBufferStatic(ctx.Device(), uploadCmd, kCube, sizeof(kCube),
                              D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                              stagingVB);
    ib_ = CreateBufferStatic(ctx.Device(), uploadCmd, kCubeIdx, sizeof(kCubeIdx),
                              D3D12_RESOURCE_STATE_INDEX_BUFFER,
                              stagingIB);
    keepAlive.push_back(stagingVB);
    keepAlive.push_back(stagingIB);

    vbv_.BufferLocation = vb_->GetGPUVirtualAddress();
    vbv_.SizeInBytes    = sizeof(kCube);
    vbv_.StrideInBytes  = sizeof(Vertex);

    ibv_.BufferLocation = ib_->GetGPUVirtualAddress();
    ibv_.SizeInBytes    = sizeof(kCubeIdx);
    ibv_.Format         = DXGI_FORMAT_R16_UINT;

    indexCount_ = sizeof(kCubeIdx) / sizeof(kCubeIdx[0]);
}

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

void Scene::Init(D3D12Context& ctx, ID3D12GraphicsCommandList* uploadCmd,
                  std::vector<ComPtr<ID3D12Resource>>& keepAlive) {
    BuildMesh(ctx, uploadCmd, keepAlive);
    BuildObjects(10000);
    EnsureInstanceBuffer(ctx);
}

void Scene::Rebuild(int objectCount, D3D12Context& ctx,
                     ID3D12GraphicsCommandList* /*uploadCmd*/,
                     std::vector<ComPtr<ID3D12Resource>>& /*keepAlive*/) {
    BuildObjects(objectCount);
    EnsureInstanceBuffer(ctx);
}

}  // namespace dxlab
