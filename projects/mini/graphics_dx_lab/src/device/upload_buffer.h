#pragma once

// UploadBuffer — Default Heap 자원에 데이터를 올리는 staging 헬퍼.
//
// 매 프레임 갱신되는 인스턴스 데이터는 *Upload Heap 의 영구 Map* 패턴이 더 빠름.
// 정적 정점 / 인덱스 버퍼는 *Default Heap + 한 번 staging copy*.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

// Default Heap 의 정적 버퍼 생성 + staging upload 한 번 수행.
// commandList 가 Close 되지 않은 상태에서 호출. upload heap 의 수명은
// caller 가 fence 완료 후 보장.
ComPtr<ID3D12Resource> CreateBufferStatic(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    const void* data,
    size_t bytes,
    D3D12_RESOURCE_STATES finalState,
    ComPtr<ID3D12Resource>& outStagingKeepAlive);

// Upload Heap 의 동적 버퍼 — 영구 Map. 매 프레임 memcpy.
ComPtr<ID3D12Resource> CreateBufferUpload(
    ID3D12Device* device,
    size_t bytes,
    void** outMapped);

}  // namespace dxlab
