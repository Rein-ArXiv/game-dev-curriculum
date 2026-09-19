#include "device/upload_buffer.h"

#include <cstring>
#include <stdexcept>

namespace dxlab {

namespace {

D3D12_RESOURCE_DESC BufferDesc(size_t bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width  = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    return d;
}

}  // namespace

ComPtr<ID3D12Resource> CreateBufferStatic(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    const void* data,
    size_t bytes,
    D3D12_RESOURCE_STATES finalState,
    ComPtr<ID3D12Resource>& outStagingKeepAlive)
{
    D3D12_HEAP_PROPERTIES heapDefault{}; heapDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES heapUpload{};  heapUpload.Type  = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC   desc = BufferDesc(bytes);

    ComPtr<ID3D12Resource> dst;
    if (FAILED(device->CreateCommittedResource(
            &heapDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&dst)))) {
        throw std::runtime_error("CreateBufferStatic: default failed");
    }

    ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)))) {
        throw std::runtime_error("CreateBufferStatic: upload failed");
    }

    void* mapped = nullptr;
    D3D12_RANGE rd{0, 0};
    if (FAILED(upload->Map(0, &rd, &mapped))) {
        throw std::runtime_error("CreateBufferStatic: map failed");
    }
    std::memcpy(mapped, data, bytes);
    upload->Unmap(0, nullptr);

    cmd->CopyBufferRegion(dst.Get(), 0, upload.Get(), 0, bytes);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = dst.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter  = finalState;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);

    outStagingKeepAlive = upload;     // GPU 가 copy 완료할 때까지 살아 있어야 함.
    return dst;
}

ComPtr<ID3D12Resource> CreateBufferUpload(
    ID3D12Device* device,
    size_t bytes,
    void** outMapped)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC   desc = BufferDesc(bytes);

    ComPtr<ID3D12Resource> buf;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&buf)))) {
        throw std::runtime_error("CreateBufferUpload failed");
    }
    D3D12_RANGE rd{0, 0};
    if (FAILED(buf->Map(0, &rd, outMapped))) {     // 영구 Map.
        throw std::runtime_error("CreateBufferUpload: map failed");
    }
    return buf;
}

}  // namespace dxlab
