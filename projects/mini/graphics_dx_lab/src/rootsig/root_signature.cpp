#include "rootsig/root_signature.h"

#include <d3dcompiler.h>
#include <stdexcept>

namespace dxlab {

const char* RootSigPolicyName(RootSigPolicy p) {
    switch (p) {
        case RootSigPolicy::Global:  return "Global (per-pass)";
        case RootSigPolicy::PerDraw: return "Per-draw (worst case)";
    }
    return "?";
}

void RootSignatureCache::Init(ID3D12Device* device) {
    // Root parameter layout:
    //   0: Root Constants  (b0) — per-draw [useInstancing(1) + pad(3) + mat4(16)
    //                              + color(4)] = 24 DWORD. cbuffer 패킹 규칙상
    //                              float4x4 가 16바이트 경계에서 시작하므로 uint
    //                              뒤에 3 DWORD 패딩 — 셰이더 DrawCB 와 동기.
    //   1: Root CBV        (b1) — frame constants.
    //   2: Root SRV        (t0) — instance buffer (Instanced 모드용).
    //   3: Descriptor Table (t1~t4) — 예약 슬롯. 이 lab 은 텍스처를 읽지 않으므로
    //                              어디에서도 SetGraphicsRootDescriptorTable 로
    //                              바인딩하지 않는다(예산 1 DWORD 만 점유).
    D3D12_ROOT_PARAMETER1 params[4] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace  = 0;
    // useInstancing + pad3 + mat4 + color — 슬롯 규약은 root_signature.h 참조.
    params[0].Constants.Num32BitValues = kRootConstTotalDwords;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace  = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;
    srvRange.BaseShaderRegister = 1;            // t1~t4
    srvRange.RegisterSpace = 0;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    srvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &srvRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samp{};
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
    versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1.NumParameters = _countof(params);
    versioned.Desc_1_1.pParameters = params;
    versioned.Desc_1_1.NumStaticSamplers = 1;
    versioned.Desc_1_1.pStaticSamplers = &samp;
    versioned.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&versioned, &blob, &error);
    if (FAILED(hr)) {
        std::string msg = "SerializeRootSignature failed";
        if (error) msg += std::string(": ") + (const char*)error->GetBufferPointer();
        throw std::runtime_error(msg);
    }
    if (FAILED(device->CreateRootSignature(0,
            blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&root_)))) {
        throw std::runtime_error("CreateRootSignature failed");
    }
}

}  // namespace dxlab
