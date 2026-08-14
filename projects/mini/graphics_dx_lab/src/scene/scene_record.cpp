// Scene 의 디바이스 비의존 부분 — 오브젝트 격자 생성, 애니메이션, 명령 기록.
//
// 이 TU 는 D3D12Context / 리소스 생성 코드에 의존하지 않는다. 덕분에 계약
// 테스트 타깃이 GPU·디바이스 없이 이 제품 코드를 그대로 링크해 검증한다.

#include "scene/scene.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <random>

using namespace DirectX;

namespace dxlab {

const char* DrawModeName(DrawMode m) {
    switch (m) {
        case DrawMode::PerDraw:   return "Per-Draw (N root constants)";
        case DrawMode::Instanced: return "Instanced (1 draw)";
    }
    return "?";
}

void Scene::BuildObjects(int count) {
    objects_.clear();
    objects_.reserve((size_t)count);
    int side = (int)std::ceil(std::cbrt((double)count));
    float spacing = 2.5f;
    float origin  = -0.5f * spacing * (float)(side - 1);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> uColor(0.3f, 1.0f);
    std::uniform_real_distribution<float> uAxis(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uSpeed(0.3f, 1.5f);

    int placed = 0;
    for (int z = 0; z < side && placed < count; ++z)
    for (int y = 0; y < side && placed < count; ++y)
    for (int x = 0; x < side && placed < count; ++x) {
        Object o;
        o.position = { origin + x * spacing, origin + y * spacing, origin + z * spacing };
        XMFLOAT3 a{uAxis(rng), uAxis(rng), uAxis(rng)};
        float len = std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
        if (len < 1e-4f) { a = {0, 1, 0}; len = 1; }
        o.axis = {a.x/len, a.y/len, a.z/len};
        o.angle = 0;
        o.angularSpeed = uSpeed(rng);
        o.color = {uColor(rng), uColor(rng), uColor(rng)};
        objects_.push_back(o);
        ++placed;
    }
}

void Scene::BindInstanceStorage(Instance* mapped, size_t capacity) {
    instanceMapped_   = mapped;
    instanceCapacity_ = capacity;
}

void Scene::Update(float dt) {
    for (auto& o : objects_) o.angle += o.angularSpeed * dt;
}

void Scene::Draw(ID3D12GraphicsCommandList* cmd,
                  D3D12Context& /*ctx*/,
                  DrawMode mode,
                  RootSigPolicy rsPolicy,
                  PsoSwitchPolicy psoPolicy,
                  ID3D12RootSignature* rootSig,
                  ID3D12PipelineState* primaryPso,
                  ID3D12PipelineState* secondaryPso,
                  float /*t*/,
                  GlCounters& counters) {
    RecordDraw(cmd, mode, rsPolicy, psoPolicy, rootSig, primaryPso, secondaryPso,
               counters);
}

void Scene::RecordDraw(ID3D12GraphicsCommandList* cmd,
                        DrawMode mode,
                        RootSigPolicy rsPolicy,
                        PsoSwitchPolicy psoPolicy,
                        ID3D12RootSignature* rootSig,
                        ID3D12PipelineState* primaryPso,
                        ID3D12PipelineState* secondaryPso,
                        GlCounters& counters) {
    // snippet-begin: instance-upload
    // 매 프레임 instance 데이터 업데이트 — Upload heap 영구 map.
    // 저장소가 연결되지 않은 헤드리스 경로에서는 건너뛴다.
    if (instanceMapped_ != nullptr) {
        const size_t writable = (std::min)(objects_.size(), instanceCapacity_);
        for (size_t i = 0; i < writable; ++i) {
            const auto& o = objects_[i];
            XMMATRIX m = ComputeInstanceModel(o.axis, o.angle, o.position);
            XMStoreFloat4x4(&instanceMapped_[i].model, XMMatrixTranspose(m));
            instanceMapped_[i].color = { o.color.x, o.color.y, o.color.z, 1.0f };
        }
    }
    // snippet-end: instance-upload

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);
    cmd->IASetIndexBuffer(&ibv_);

    // snippet-begin: draw-lambdas
    auto setRootSigForDraw = [&] {
        if (rsPolicy == RootSigPolicy::PerDraw && rootSig) {
            cmd->SetGraphicsRootSignature(rootSig);
            counters.rootSigSets++;
        }
    };

    auto setPsoForDraw = [&](size_t drawIndex) {
        if (psoPolicy == PsoSwitchPolicy::PerDraw && primaryPso) {
            ID3D12PipelineState* pso =
                (secondaryPso && (drawIndex & 1)) ? secondaryPso : primaryPso;
            cmd->SetPipelineState(pso);
            counters.pipelineSets++;
        }
    };
    // snippet-end: draw-lambdas

    // snippet-begin: instanced-path
    if (mode == DrawMode::Instanced) {
        setRootSigForDraw();
        if (psoPolicy == PsoSwitchPolicy::PerMaterial && primaryPso) {
            cmd->SetPipelineState(primaryPso);
            counters.pipelineSets++;
        } else {
            setPsoForDraw(0);
        }
        // Instance buffer 를 Root SRV (t0) 로 노출 — 셰이더가 SV_InstanceID 로 인덱싱.
        if (instanceBuf_) {
            cmd->SetGraphicsRootShaderResourceView(
                2, instanceBuf_->GetGPUVirtualAddress());
        }
        // Root constants 의 첫 DWORD 를 *Instanced 모드 표시* 로 사용.
        UINT useInstancing = 1;
        cmd->SetGraphicsRoot32BitConstants(0, kRootConstUseInstancingCount,
                                           &useInstancing,
                                           kRootConstUseInstancingOffset);
        counters.rootConstantUpdates++;
        cmd->DrawIndexedInstanced(indexCount_, (UINT)objects_.size(), 0, 0, 0);
        counters.instancedDraws++;
        // snippet-end: instanced-path
    } else if (psoPolicy == PsoSwitchPolicy::PerMaterial && secondaryPso) {
        for (int material = 0; material < 2; ++material) {
            cmd->SetPipelineState(material == 0 ? primaryPso : secondaryPso);
            counters.pipelineSets++;

            for (size_t i = (size_t)material; i < objects_.size(); i += 2) {
                setRootSigForDraw();
                UINT useInstancing = 0;
                cmd->SetGraphicsRoot32BitConstants(0, kRootConstUseInstancingCount,
                                                   &useInstancing,
                                                   kRootConstUseInstancingOffset);
                counters.rootConstantUpdates++;
                const auto& o = objects_[i];
                XMMATRIX m = ComputeInstanceModel(o.axis, o.angle, o.position);
                XMFLOAT4X4 mt;
                XMStoreFloat4x4(&mt, XMMatrixTranspose(m));
                // cbuffer 패킹: model 은 DWORD 4, color 는 DWORD 20 에서 시작.
                cmd->SetGraphicsRoot32BitConstants(0, kRootConstModelCount, &mt,
                                                   kRootConstModelOffset);
                float c[4] = { o.color.x, o.color.y, o.color.z, 1.0f };
                cmd->SetGraphicsRoot32BitConstants(0, kRootConstColorCount, c,
                                                   kRootConstColorOffset);
                counters.rootConstantUpdates += 2;
                cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
                counters.drawCalls++;
            }
        }
    } else {
        // snippet-begin: per-draw-loop
        for (size_t i = 0; i < objects_.size(); ++i) {
            setRootSigForDraw();
            setPsoForDraw(i);
            UINT useInstancing = 0;
            cmd->SetGraphicsRoot32BitConstants(0, kRootConstUseInstancingCount,
                                               &useInstancing,
                                               kRootConstUseInstancingOffset);
            counters.rootConstantUpdates++;
            const auto& o = objects_[i];
            XMMATRIX m = ComputeInstanceModel(o.axis, o.angle, o.position);
            XMFLOAT4X4 mt;
            XMStoreFloat4x4(&mt, XMMatrixTranspose(m));
            // Root Constants: [useInstancing(1) + pad(3) + mat4(16) + color(4)]
            // = 24 DWORD. cbuffer 패킹 규칙상 model 은 DWORD 4 에서 시작한다.
            cmd->SetGraphicsRoot32BitConstants(0, kRootConstModelCount, &mt,
                                               kRootConstModelOffset);
            float c[4] = { o.color.x, o.color.y, o.color.z, 1.0f };
            cmd->SetGraphicsRoot32BitConstants(0, kRootConstColorCount, c,
                                               kRootConstColorOffset);
            counters.rootConstantUpdates += 2;
            cmd->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
            counters.drawCalls++;
        }
        // snippet-end: per-draw-loop
    }
}

}  // namespace dxlab
