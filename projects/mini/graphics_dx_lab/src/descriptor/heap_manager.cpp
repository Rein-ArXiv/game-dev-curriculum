#include "descriptor/heap_manager.h"

#include <algorithm>
#include <stdexcept>

namespace dxlab {

const char* HeapPolicyName(HeapPolicy p) {
    switch (p) {
        case HeapPolicy::RebuildEachFrame: return "Transient rebuild";
        case HeapPolicy::PersistentTable:  return "Persistent table";
    }
    return "?";
}

void HeapManager::Init(ID3D12Device* device, UINT capacity) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = capacity;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_)))) {
        throw std::runtime_error("CreateDescriptorHeap(SRV) failed");
    }
    increment_ = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    capacity_ = capacity;
}

void HeapManager::WriteNullSrvs(ID3D12Device* device, UINT first, UINT count) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    auto cpu = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(first) * increment_;
    for (UINT i = 0; i < count; ++i) {
        // nullptr resource + 완전한 desc는 사양상 유효한 null descriptor다.
        device->CreateShaderResourceView(nullptr, &srv, cpu);
        cpu.ptr += increment_;
    }
}

UINT HeapManager::PrepareNullSrvWorkload(ID3D12Device* device,
                                         HeapPolicy policy,
                                         UINT frameIndex,
                                         UINT framesInFlight,
                                         UINT requestedCount) {
    if (!device || framesInFlight == 0 || capacity_ == 0) return 0;

    const DescriptorWritePlan plan = PlanNullSrvWrites(
        policy, frameIndex, framesInFlight, requestedCount, capacity_,
        persistentCount_, lastPolicy_);
    if (plan.writes != 0) WriteNullSrvs(device, plan.first, plan.writes);
    persistentCount_ = plan.persistentCount;
    lastPolicy_ = policy;
    return plan.writes;
}

}  // namespace dxlab
