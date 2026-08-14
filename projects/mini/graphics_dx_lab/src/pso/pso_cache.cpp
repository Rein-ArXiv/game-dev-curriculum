#include "pso/pso_cache.h"

#include <d3dcompiler.h>
#include <stdexcept>
#include <string>

namespace dxlab {

const char* PsoSwitchPolicyName(PsoSwitchPolicy p) {
    switch (p) {
        case PsoSwitchPolicy::PerPass:     return "Per-Pass (1 set)";
        case PsoSwitchPolicy::PerMaterial: return "Per-Material";
        case PsoSwitchPolicy::PerDraw:     return "Per-Draw (worst)";
    }
    return "?";
}

void PsoCache::CompileShader(const wchar_t* file, const char* entry,
                              const char* profile,
                              ComPtr<ID3DBlob>& outBlob) {
    UINT flags = 0;
#ifndef NDEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompileFromFile(file, nullptr, nullptr, entry, profile,
                                     flags, 0, &outBlob, &err);
    if (FAILED(hr)) {
        std::string msg = "D3DCompileFromFile failed: ";
        if (err) msg += (const char*)err->GetBufferPointer();
        throw std::runtime_error(msg);
    }
}

void PsoCache::Init(ID3D12Device* device,
                     ID3D12RootSignature* rootSig,
                     DXGI_FORMAT rtvFormat,
                     DXGI_FORMAT depthFormat) {
    ComPtr<ID3DBlob> vs, psL, psB;
    CompileShader(L"shaders/cube.hlsl", "VSMain", "vs_5_1", vs);
    CompileShader(L"shaders/cube.hlsl", "PSLambert",     "ps_5_1", psL);
    CompileShader(L"shaders/cube.hlsl", "PSBlinnPhong",  "ps_5_1", psB);

    D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSig;
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.InputLayout = { il, _countof(il) };
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.FrontCounterClockwise = TRUE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = rtvFormat;
    desc.DSVFormat     = depthFormat;
    desc.SampleMask = UINT_MAX;
    desc.SampleDesc.Count = 1;

    desc.PS = { psL->GetBufferPointer(), psL->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&psoLambert_)))) {
        throw std::runtime_error("CreateGraphicsPipelineState(Lambert) failed");
    }

    desc.PS = { psB->GetBufferPointer(), psB->GetBufferSize() };
    if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&psoBlinn_)))) {
        throw std::runtime_error("CreateGraphicsPipelineState(Blinn) failed");
    }
}

}  // namespace dxlab
