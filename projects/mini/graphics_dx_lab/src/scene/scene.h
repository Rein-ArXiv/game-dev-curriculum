#pragma once

// Scene — DX12 의 1만 오브젝트 격자 + 회전 + 그리기.
//
// DX12 에선 *드로우 모드 토글* 이 OpenGL 보다 단순 — Per-Draw + Instanced 두 종.
// Indirect(ExecuteIndirect) 는 이 lab 의 범위 밖이다.

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "pso/pso_cache.h"
#include "rootsig/root_signature.h"

#include <vector>
#include <cstdint>

namespace dxlab {

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

class D3D12Context;
struct GlCounters {
    int drawCalls = 0;
    int instancedDraws = 0;
    int pipelineSets = 0;
    int rootSigSets  = 0;
    int rootConstantUpdates = 0;
    int barrierCalls = 0;
    int descriptorHeapSets = 0;
    int descriptorWrites = 0;
    void Reset() {
        drawCalls = instancedDraws = pipelineSets = rootSigSets = 0;
        rootConstantUpdates = barrierCalls = descriptorHeapSets = 0;
        descriptorWrites = 0;
    }
};

enum class DrawMode {
    PerDraw,
    Instanced,
};

const char* DrawModeName(DrawMode m);

inline int ExpectedDrawSubmissions(DrawMode mode, int objectCount) {
    if (objectCount <= 0) return 0;
    return mode == DrawMode::PerDraw ? objectCount : 1;
}

struct Instance {
    DirectX::XMFLOAT4X4 model;
    DirectX::XMFLOAT4   color;
};

// 인스턴스 model 행렬 — 축·각도 회전 뒤 위치 이동. Per-Draw 루트 상수 경로와
// Instanced 업로드 경로가 같은 식을 쓰도록 한 곳에 모은다.
inline DirectX::XMMATRIX ComputeInstanceModel(const DirectX::XMFLOAT3& axis,
                                               float angle,
                                               const DirectX::XMFLOAT3& position) {
    return DirectX::XMMatrixRotationAxis(DirectX::XMLoadFloat3(&axis), angle)
         * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
}

class Scene {
public:
    void Init(D3D12Context& ctx, ID3D12GraphicsCommandList* uploadCmd,
               std::vector<ComPtr<ID3D12Resource>>& keepAlive);

    void Update(float dt);
    void Rebuild(int objectCount, D3D12Context& ctx,
                  ID3D12GraphicsCommandList* uploadCmd,
                  std::vector<ComPtr<ID3D12Resource>>& keepAlive);

    int  ObjectCount() const { return (int)objects_.size(); }
    UINT IndexCount()  const { return indexCount_; }

    // 오브젝트 격자 생성 — 디바이스가 필요 없는 순수 CPU 단계라 헤드리스
    // 검증에서도 그대로 호출한다.
    void BuildObjects(int count);

    // Instance upload 저장소 연결. 실행 시에는 Upload heap 의 영구 map 주소를,
    // 헤드리스 검증에서는 테스트 버퍼를 넘긴다. nullptr 이면 RecordDraw 가
    // 인스턴스 업로드 루프를 건너뛴다.
    void BindInstanceStorage(Instance* mapped, size_t capacity);

    // Draw — Caller 가 RootSig + PSO + Heap 을 설정한 상태에서 호출.
    void Draw(ID3D12GraphicsCommandList* cmd,
              D3D12Context& ctx,
              DrawMode mode,
              RootSigPolicy rsPolicy,
              PsoSwitchPolicy psoPolicy,
              ID3D12RootSignature* rootSig,
              ID3D12PipelineState* primaryPso,
              ID3D12PipelineState* secondaryPso,
              float t,
              GlCounters& counters);

    // Draw 의 명령 기록부. objects_/indexCount_ 와 command list 만 사용하므로
    // D3D12Context·리소스 생성 없이도 호출할 수 있다.
    void RecordDraw(ID3D12GraphicsCommandList* cmd,
                    DrawMode mode,
                    RootSigPolicy rsPolicy,
                    PsoSwitchPolicy psoPolicy,
                    ID3D12RootSignature* rootSig,
                    ID3D12PipelineState* primaryPso,
                    ID3D12PipelineState* secondaryPso,
                    GlCounters& counters);

private:
    void BuildMesh(D3D12Context& ctx, ID3D12GraphicsCommandList* uploadCmd,
                    std::vector<ComPtr<ID3D12Resource>>& keepAlive);
    void EnsureInstanceBuffer(D3D12Context& ctx);

    struct Object {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 axis;
        float angle;
        float angularSpeed;
        DirectX::XMFLOAT3 color;
    };
    std::vector<Object> objects_;

    ComPtr<ID3D12Resource> vb_;
    ComPtr<ID3D12Resource> ib_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW  ibv_{};
    UINT indexCount_ = 0;

    ComPtr<ID3D12Resource>     instanceBuf_;
    Instance*                   instanceMapped_ = nullptr;
    size_t                      instanceCapacity_ = 0;
    D3D12_VERTEX_BUFFER_VIEW   instanceVbv_{};
};

}  // namespace dxlab
