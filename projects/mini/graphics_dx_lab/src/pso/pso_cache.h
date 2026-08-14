#pragma once

// PsoCache — Step 3 의 PSO 변경 빈도 토글용.
//
// 두 PSO 보유:
//   - psoLambert_   : Lambert 셰이딩.
//   - psoBlinn_     : Blinn-Phong 셰이딩.
//
// Step 3 토글:
//   - Per-Pass   : 패스당 PSO set 한 번. (모든 드로우 같은 모델)
//   - Per-Material : 머티리얼 그룹마다 변경 (가상 — 토글로 시뮬레이션)
//   - Per-Draw   : 드로우당 변경 (의도적 최악)

#include <d3d12.h>
#include <wrl/client.h>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

enum class PsoSwitchPolicy {
    PerPass,
    PerMaterial,
    PerDraw,
};

const char* PsoSwitchPolicyName(PsoSwitchPolicy p);

class PsoCache {
public:
    void Init(ID3D12Device* device,
               ID3D12RootSignature* rootSig,
               DXGI_FORMAT rtvFormat,
               DXGI_FORMAT depthFormat);

    ID3D12PipelineState* Lambert()    const { return psoLambert_.Get(); }
    ID3D12PipelineState* BlinnPhong() const { return psoBlinn_.Get(); }

private:
    void CompileShader(const wchar_t* file, const char* entry,
                        const char* profile,
                        ComPtr<ID3DBlob>& outBlob);

    ComPtr<ID3D12PipelineState> psoLambert_;
    ComPtr<ID3D12PipelineState> psoBlinn_;
};

}  // namespace dxlab
