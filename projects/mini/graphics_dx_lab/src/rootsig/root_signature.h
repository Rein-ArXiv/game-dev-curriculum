#pragma once

// Root Signature 설계 — Step 4 의 RootSig 변경 빈도 토글용.
//
// 두 변형:
//   - Global RootSig : 패스 시작 시 한 번만 SetGraphicsRootSignature.
//   - Per-draw RootSig: 매 드로우마다 동일 SetGraphicsRootSignature (no-op 같지만
//                       카운터에 기록).
// 실제 변경되는 시그니처는 *하나* 라 시각 결과 동일. CPU 호출 빈도만 비교.

#include <d3d12.h>
#include <wrl/client.h>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

enum class RootSigPolicy {
    Global,        // 패스당 1 회 set.
    PerDraw,       // 드로우당 set (의도적 최악).
};

const char* RootSigPolicyName(RootSigPolicy p);

// Root Constants(b0) 슬롯 규약 — 루트 시그니처 선언과 드로우 기록이 같은
// 숫자를 쓰도록 한 곳에 모은다. cbuffer 패킹 규칙상 float4x4 가 16바이트
// 경계에서 시작하므로 uint 뒤에 3 DWORD 패딩이 들어간다.
//   [useInstancing(1) + pad(3)] [model mat4(16)] [color float4(4)] = 24 DWORD.
// snippet-begin: root-const-slots
constexpr UINT kRootConstUseInstancingOffset = 0;
constexpr UINT kRootConstUseInstancingCount  = 1;
constexpr UINT kRootConstModelOffset         = 4;
constexpr UINT kRootConstModelCount          = 16;
constexpr UINT kRootConstColorOffset         = 20;
constexpr UINT kRootConstColorCount          = 4;
constexpr UINT kRootConstTotalDwords         = 24;
// snippet-end: root-const-slots

class RootSignatureCache {
public:
    void Init(ID3D12Device* device);

    ID3D12RootSignature* Get() const { return root_.Get(); }

private:
    ComPtr<ID3D12RootSignature> root_;
};

}  // namespace dxlab
