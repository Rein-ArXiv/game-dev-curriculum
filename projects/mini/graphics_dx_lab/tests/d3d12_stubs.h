#pragma once

// D3D12 인터페이스 스텁 — 계약 테스트가 제품 코드에 넘길 대역 객체.
//
// 모든 순수 가상 함수를 기본 구현으로 채운 베이스만 제공한다. 관심 있는
// 메서드는 파생 fake 가 override 해서 호출을 기록하거나 값을 주입한다.
// (Windows SDK d3d12.h 의 선언에서 기계적으로 만든 파일이다.)

#include <d3d12.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)   // 참조되지 않은 형식 매개 변수.
#endif

namespace dxlab_test {

// ---- ID3D12GraphicsCommandList ----------------------------------------------
struct StubGraphicsCommandList : public ID3D12GraphicsCommandList {
    virtual ~StubGraphicsCommandList() = default;
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // ID3D12Object
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData( 
            _In_  REFGUID guid,
            _Inout_  UINT *pDataSize,
            _Out_writes_bytes_opt_( *pDataSize )  void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData( 
            _In_  REFGUID guid,
            _In_  UINT DataSize,
            _In_reads_bytes_opt_( DataSize )  const void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( 
            _In_  REFGUID guid,
            _In_opt_  const IUnknown *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetName( 
            _In_z_  LPCWSTR Name) override { return {}; }
    // ID3D12DeviceChild
    virtual HRESULT STDMETHODCALLTYPE GetDevice( 
            REFIID riid,
            _COM_Outptr_opt_  void **ppvDevice) override { return {}; }
    // ID3D12CommandList
    virtual D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE GetType( void) override { return {}; }
    // ID3D12GraphicsCommandList
    virtual HRESULT STDMETHODCALLTYPE Close( void) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE Reset( 
            _In_  ID3D12CommandAllocator *pAllocator,
            _In_opt_  ID3D12PipelineState *pInitialState) override { return {}; }
    virtual void STDMETHODCALLTYPE ClearState( 
            _In_opt_  ID3D12PipelineState *pPipelineState) override {}
    virtual void STDMETHODCALLTYPE DrawInstanced( 
            _In_  UINT VertexCountPerInstance,
            _In_  UINT InstanceCount,
            _In_  UINT StartVertexLocation,
            _In_  UINT StartInstanceLocation) override {}
    virtual void STDMETHODCALLTYPE DrawIndexedInstanced( 
            _In_  UINT IndexCountPerInstance,
            _In_  UINT InstanceCount,
            _In_  UINT StartIndexLocation,
            _In_  INT BaseVertexLocation,
            _In_  UINT StartInstanceLocation) override {}
    virtual void STDMETHODCALLTYPE Dispatch( 
            _In_  UINT ThreadGroupCountX,
            _In_  UINT ThreadGroupCountY,
            _In_  UINT ThreadGroupCountZ) override {}
    virtual void STDMETHODCALLTYPE CopyBufferRegion( 
            _In_  ID3D12Resource *pDstBuffer,
            UINT64 DstOffset,
            _In_  ID3D12Resource *pSrcBuffer,
            UINT64 SrcOffset,
            UINT64 NumBytes) override {}
    virtual void STDMETHODCALLTYPE CopyTextureRegion( 
            _In_  const D3D12_TEXTURE_COPY_LOCATION *pDst,
            UINT DstX,
            UINT DstY,
            UINT DstZ,
            _In_  const D3D12_TEXTURE_COPY_LOCATION *pSrc,
            _In_opt_  const D3D12_BOX *pSrcBox) override {}
    virtual void STDMETHODCALLTYPE CopyResource( 
            _In_  ID3D12Resource *pDstResource,
            _In_  ID3D12Resource *pSrcResource) override {}
    virtual void STDMETHODCALLTYPE CopyTiles( 
            _In_  ID3D12Resource *pTiledResource,
            _In_  const D3D12_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate,
            _In_  const D3D12_TILE_REGION_SIZE *pTileRegionSize,
            _In_  ID3D12Resource *pBuffer,
            UINT64 BufferStartOffsetInBytes,
            D3D12_TILE_COPY_FLAGS Flags) override {}
    virtual void STDMETHODCALLTYPE ResolveSubresource( 
            _In_  ID3D12Resource *pDstResource,
            _In_  UINT DstSubresource,
            _In_  ID3D12Resource *pSrcResource,
            _In_  UINT SrcSubresource,
            _In_  DXGI_FORMAT Format) override {}
    virtual void STDMETHODCALLTYPE IASetPrimitiveTopology( 
            _In_  D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) override {}
    virtual void STDMETHODCALLTYPE RSSetViewports( 
            _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT NumViewports,
            _In_reads_( NumViewports)  const D3D12_VIEWPORT *pViewports) override {}
    virtual void STDMETHODCALLTYPE RSSetScissorRects( 
            _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT NumRects,
            _In_reads_( NumRects)  const D3D12_RECT *pRects) override {}
    virtual void STDMETHODCALLTYPE OMSetBlendFactor( 
            _In_reads_opt_(4)  const FLOAT BlendFactor[ 4 ]) override {}
    virtual void STDMETHODCALLTYPE OMSetStencilRef( 
            _In_  UINT StencilRef) override {}
    virtual void STDMETHODCALLTYPE SetPipelineState( 
            _In_  ID3D12PipelineState *pPipelineState) override {}
    virtual void STDMETHODCALLTYPE ResourceBarrier( 
            _In_  UINT NumBarriers,
            _In_reads_(NumBarriers)  const D3D12_RESOURCE_BARRIER *pBarriers) override {}
    virtual void STDMETHODCALLTYPE ExecuteBundle( 
            _In_  ID3D12GraphicsCommandList *pCommandList) override {}
    virtual void STDMETHODCALLTYPE SetDescriptorHeaps( 
            _In_  UINT NumDescriptorHeaps,
            _In_reads_(NumDescriptorHeaps)  ID3D12DescriptorHeap *const *ppDescriptorHeaps) override {}
    virtual void STDMETHODCALLTYPE SetComputeRootSignature( 
            _In_opt_  ID3D12RootSignature *pRootSignature) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRootSignature( 
            _In_opt_  ID3D12RootSignature *pRootSignature) override {}
    virtual void STDMETHODCALLTYPE SetComputeRootDescriptorTable( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override {}
    virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstant( 
            _In_  UINT RootParameterIndex,
            _In_  UINT SrcData,
            _In_  UINT DestOffsetIn32BitValues) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant( 
            _In_  UINT RootParameterIndex,
            _In_  UINT SrcData,
            _In_  UINT DestOffsetIn32BitValues) override {}
    virtual void STDMETHODCALLTYPE SetComputeRoot32BitConstants( 
            _In_  UINT RootParameterIndex,
            _In_  UINT Num32BitValuesToSet,
            _In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void *pSrcData,
            _In_  UINT DestOffsetIn32BitValues) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants( 
            _In_  UINT RootParameterIndex,
            _In_  UINT Num32BitValuesToSet,
            _In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void *pSrcData,
            _In_  UINT DestOffsetIn32BitValues) override {}
    virtual void STDMETHODCALLTYPE SetComputeRootConstantBufferView( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {}
    virtual void STDMETHODCALLTYPE SetComputeRootShaderResourceView( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {}
    virtual void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {}
    virtual void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView( 
            _In_  UINT RootParameterIndex,
            _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override {}
    virtual void STDMETHODCALLTYPE IASetIndexBuffer( 
            _In_opt_  const D3D12_INDEX_BUFFER_VIEW *pView) override {}
    virtual void STDMETHODCALLTYPE IASetVertexBuffers( 
            _In_  UINT StartSlot,
            _In_  UINT NumViews,
            _In_reads_opt_(NumViews)  const D3D12_VERTEX_BUFFER_VIEW *pViews) override {}
    virtual void STDMETHODCALLTYPE SOSetTargets( 
            _In_  UINT StartSlot,
            _In_  UINT NumViews,
            _In_reads_opt_(NumViews)  const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews) override {}
    virtual void STDMETHODCALLTYPE OMSetRenderTargets( 
            _In_  UINT NumRenderTargetDescriptors,
            _In_opt_  const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors,
            _In_  BOOL RTsSingleHandleToDescriptorRange,
            _In_opt_  const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor) override {}
    virtual void STDMETHODCALLTYPE ClearDepthStencilView( 
            _In_  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
            _In_  D3D12_CLEAR_FLAGS ClearFlags,
            _In_  FLOAT Depth,
            _In_  UINT8 Stencil,
            _In_  UINT NumRects,
            _In_reads_(NumRects)  const D3D12_RECT *pRects) override {}
    virtual void STDMETHODCALLTYPE ClearRenderTargetView( 
            _In_  D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
            _In_  const FLOAT ColorRGBA[ 4 ],
            _In_  UINT NumRects,
            _In_reads_(NumRects)  const D3D12_RECT *pRects) override {}
    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewUint( 
            _In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
            _In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
            _In_  ID3D12Resource *pResource,
            _In_  const UINT Values[ 4 ],
            _In_  UINT NumRects,
            _In_reads_(NumRects)  const D3D12_RECT *pRects) override {}
    virtual void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat( 
            _In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
            _In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
            _In_  ID3D12Resource *pResource,
            _In_  const FLOAT Values[ 4 ],
            _In_  UINT NumRects,
            _In_reads_(NumRects)  const D3D12_RECT *pRects) override {}
    virtual void STDMETHODCALLTYPE DiscardResource( 
            _In_  ID3D12Resource *pResource,
            _In_opt_  const D3D12_DISCARD_REGION *pRegion) override {}
    virtual void STDMETHODCALLTYPE BeginQuery( 
            _In_  ID3D12QueryHeap *pQueryHeap,
            _In_  D3D12_QUERY_TYPE Type,
            _In_  UINT Index) override {}
    virtual void STDMETHODCALLTYPE EndQuery( 
            _In_  ID3D12QueryHeap *pQueryHeap,
            _In_  D3D12_QUERY_TYPE Type,
            _In_  UINT Index) override {}
    virtual void STDMETHODCALLTYPE ResolveQueryData( 
            _In_  ID3D12QueryHeap *pQueryHeap,
            _In_  D3D12_QUERY_TYPE Type,
            _In_  UINT StartIndex,
            _In_  UINT NumQueries,
            _In_  ID3D12Resource *pDestinationBuffer,
            _In_  UINT64 AlignedDestinationBufferOffset) override {}
    virtual void STDMETHODCALLTYPE SetPredication( 
            _In_opt_  ID3D12Resource *pBuffer,
            _In_  UINT64 AlignedBufferOffset,
            _In_  D3D12_PREDICATION_OP Operation) override {}
    virtual void STDMETHODCALLTYPE SetMarker( 
            UINT Metadata,
            _In_reads_bytes_opt_(Size)  const void *pData,
            UINT Size) override {}
    virtual void STDMETHODCALLTYPE BeginEvent( 
            UINT Metadata,
            _In_reads_bytes_opt_(Size)  const void *pData,
            UINT Size) override {}
    virtual void STDMETHODCALLTYPE EndEvent( void) override {}
    virtual void STDMETHODCALLTYPE ExecuteIndirect( 
            _In_  ID3D12CommandSignature *pCommandSignature,
            _In_  UINT MaxCommandCount,
            _In_  ID3D12Resource *pArgumentBuffer,
            _In_  UINT64 ArgumentBufferOffset,
            _In_opt_  ID3D12Resource *pCountBuffer,
            _In_  UINT64 CountBufferOffset) override {}
};

// ---- ID3D12Fence ----------------------------------------------
struct StubFence : public ID3D12Fence {
    virtual ~StubFence() = default;
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // ID3D12Object
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData( 
            _In_  REFGUID guid,
            _Inout_  UINT *pDataSize,
            _Out_writes_bytes_opt_( *pDataSize )  void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData( 
            _In_  REFGUID guid,
            _In_  UINT DataSize,
            _In_reads_bytes_opt_( DataSize )  const void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( 
            _In_  REFGUID guid,
            _In_opt_  const IUnknown *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetName( 
            _In_z_  LPCWSTR Name) override { return {}; }
    // ID3D12DeviceChild
    virtual HRESULT STDMETHODCALLTYPE GetDevice( 
            REFIID riid,
            _COM_Outptr_opt_  void **ppvDevice) override { return {}; }
    // ID3D12Pageable
    // ID3D12Fence
    virtual UINT64 STDMETHODCALLTYPE GetCompletedValue( void) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetEventOnCompletion( 
            UINT64 Value,
            HANDLE hEvent) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE Signal( 
            UINT64 Value) override { return {}; }
};

// ---- ID3D12Resource ----------------------------------------------
struct StubResource : public ID3D12Resource {
    virtual ~StubResource() = default;
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // ID3D12Object
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData( 
            _In_  REFGUID guid,
            _Inout_  UINT *pDataSize,
            _Out_writes_bytes_opt_( *pDataSize )  void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData( 
            _In_  REFGUID guid,
            _In_  UINT DataSize,
            _In_reads_bytes_opt_( DataSize )  const void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( 
            _In_  REFGUID guid,
            _In_opt_  const IUnknown *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetName( 
            _In_z_  LPCWSTR Name) override { return {}; }
    // ID3D12DeviceChild
    virtual HRESULT STDMETHODCALLTYPE GetDevice( 
            REFIID riid,
            _COM_Outptr_opt_  void **ppvDevice) override { return {}; }
    // ID3D12Pageable
    // ID3D12Resource
    virtual HRESULT STDMETHODCALLTYPE Map( 
            UINT Subresource,
            _In_opt_  const D3D12_RANGE *pReadRange,
            _Outptr_opt_result_bytebuffer_(_Inexpressible_("Dependent on resource"))  void **ppData) override { return {}; }
    virtual void STDMETHODCALLTYPE Unmap( 
            UINT Subresource,
            _In_opt_  const D3D12_RANGE *pWrittenRange) override {}
#if defined(_MSC_VER) || !defined(_WIN32)
    virtual D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc( void) override { return {}; }
#else
    virtual D3D12_RESOURCE_DESC *STDMETHODCALLTYPE GetDesc( 
            D3D12_RESOURCE_DESC * RetVal) override { return {}; }
#endif
    virtual D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress( void) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE WriteToSubresource( 
            UINT DstSubresource,
            _In_opt_  const D3D12_BOX *pDstBox,
            _In_  const void *pSrcData,
            UINT SrcRowPitch,
            UINT SrcDepthPitch) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE ReadFromSubresource( 
            _Out_  void *pDstData,
            UINT DstRowPitch,
            UINT DstDepthPitch,
            UINT SrcSubresource,
            _In_opt_  const D3D12_BOX *pSrcBox) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE GetHeapProperties( 
            _Out_opt_  D3D12_HEAP_PROPERTIES *pHeapProperties,
            _Out_opt_  D3D12_HEAP_FLAGS *pHeapFlags) override { return {}; }
};

// ---- ID3D12QueryHeap ----------------------------------------------
struct StubQueryHeap : public ID3D12QueryHeap {
    virtual ~StubQueryHeap() = default;
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // ID3D12Object
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData( 
            _In_  REFGUID guid,
            _Inout_  UINT *pDataSize,
            _Out_writes_bytes_opt_( *pDataSize )  void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData( 
            _In_  REFGUID guid,
            _In_  UINT DataSize,
            _In_reads_bytes_opt_( DataSize )  const void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( 
            _In_  REFGUID guid,
            _In_opt_  const IUnknown *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetName( 
            _In_z_  LPCWSTR Name) override { return {}; }
    // ID3D12DeviceChild
    virtual HRESULT STDMETHODCALLTYPE GetDevice( 
            REFIID riid,
            _COM_Outptr_opt_  void **ppvDevice) override { return {}; }
    // ID3D12Pageable
    // ID3D12QueryHeap
};

// ---- ID3D12CommandQueue ----------------------------------------------
struct StubCommandQueue : public ID3D12CommandQueue {
    virtual ~StubCommandQueue() = default;
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    virtual ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    virtual ULONG STDMETHODCALLTYPE Release() override { return 1; }
    // ID3D12Object
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData( 
            _In_  REFGUID guid,
            _Inout_  UINT *pDataSize,
            _Out_writes_bytes_opt_( *pDataSize )  void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData( 
            _In_  REFGUID guid,
            _In_  UINT DataSize,
            _In_reads_bytes_opt_( DataSize )  const void *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface( 
            _In_  REFGUID guid,
            _In_opt_  const IUnknown *pData) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE SetName( 
            _In_z_  LPCWSTR Name) override { return {}; }
    // ID3D12DeviceChild
    virtual HRESULT STDMETHODCALLTYPE GetDevice( 
            REFIID riid,
            _COM_Outptr_opt_  void **ppvDevice) override { return {}; }
    // ID3D12Pageable
    // ID3D12CommandQueue
    virtual void STDMETHODCALLTYPE UpdateTileMappings( 
            _In_  ID3D12Resource *pResource,
            UINT NumResourceRegions,
            _In_reads_opt_(NumResourceRegions)  const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
            _In_reads_opt_(NumResourceRegions)  const D3D12_TILE_REGION_SIZE *pResourceRegionSizes,
            _In_opt_  ID3D12Heap *pHeap,
            UINT NumRanges,
            _In_reads_opt_(NumRanges)  const D3D12_TILE_RANGE_FLAGS *pRangeFlags,
            _In_reads_opt_(NumRanges)  const UINT *pHeapRangeStartOffsets,
            _In_reads_opt_(NumRanges)  const UINT *pRangeTileCounts,
            D3D12_TILE_MAPPING_FLAGS Flags) override {}
    virtual void STDMETHODCALLTYPE CopyTileMappings( 
            _In_  ID3D12Resource *pDstResource,
            _In_  const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate,
            _In_  ID3D12Resource *pSrcResource,
            _In_  const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate,
            _In_  const D3D12_TILE_REGION_SIZE *pRegionSize,
            D3D12_TILE_MAPPING_FLAGS Flags) override {}
    virtual void STDMETHODCALLTYPE ExecuteCommandLists( 
            _In_  UINT NumCommandLists,
            _In_reads_(NumCommandLists)  ID3D12CommandList *const *ppCommandLists) override {}
    virtual void STDMETHODCALLTYPE SetMarker( 
            UINT Metadata,
            _In_reads_bytes_opt_(Size)  const void *pData,
            UINT Size) override {}
    virtual void STDMETHODCALLTYPE BeginEvent( 
            UINT Metadata,
            _In_reads_bytes_opt_(Size)  const void *pData,
            UINT Size) override {}
    virtual void STDMETHODCALLTYPE EndEvent( void) override {}
    virtual HRESULT STDMETHODCALLTYPE Signal( 
            ID3D12Fence *pFence,
            UINT64 Value) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE Wait( 
            ID3D12Fence *pFence,
            UINT64 Value) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE GetTimestampFrequency( 
            _Out_  UINT64 *pFrequency) override { return {}; }
    virtual HRESULT STDMETHODCALLTYPE GetClockCalibration( 
            _Out_  UINT64 *pGpuTimestamp,
            _Out_  UINT64 *pCpuTimestamp) override { return {}; }
#if defined(_MSC_VER) || !defined(_WIN32)
    virtual D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc( void) override { return {}; }
#else
    virtual D3D12_COMMAND_QUEUE_DESC *STDMETHODCALLTYPE GetDesc( 
            D3D12_COMMAND_QUEUE_DESC * RetVal) override { return {}; }
#endif
};

}  // namespace dxlab_test

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
