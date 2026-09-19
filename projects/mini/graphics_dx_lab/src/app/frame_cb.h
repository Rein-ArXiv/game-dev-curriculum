#pragma once

// FrameCB — 프레임 단위 상수(b1). 카메라/조명은 순수 함수로 만든다.
//
// 이 헤더는 D3D12·Win32 에 의존하지 않는다. 덕분에 계약 테스트가 뷰·투영
// 합성 결과를 GPU 없이 그대로 검증한다.

#include <DirectXMath.h>

#include <algorithm>

namespace dxlab {

struct FrameCB {
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMFLOAT3   cameraPos;
    float               time;
    DirectX::XMFLOAT3   lightDir;
    float               pad;
    // 루트 CBV 에 넘기는 GPU 가상 주소는 256바이트 정렬이어야 하므로
    // (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) 프레임 슬롯 stride 인
    // sizeof(FrameCB) 를 256 으로 채운다. 셰이더는 앞 96바이트만 읽는다.
    float               slotPad[40];
};
static_assert(sizeof(FrameCB) == 256, "FrameCB slot stride must be 256 bytes");

struct CameraSetup {
    DirectX::XMFLOAT3 eye{60.0f, 40.0f, 60.0f};
    DirectX::XMFLOAT3 at{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};
    float fovDegrees = 45.0f;
    float nearZ      = 0.1f;
    float farZ       = 1000.0f;
};

// 이 lab 의 기본 조명 방향(정규화 전 원본 벡터).
inline DirectX::XMFLOAT3 DefaultLightDir() { return {0.4f, 0.8f, 0.2f}; }

// 뷰(RH) × 투영(RH, [0,1] 깊이)을 합성해 FrameCB 를 만든다. viewProj 는 HLSL
// cbuffer 규약에 맞춰 transpose 해 저장한다.
inline FrameCB MakeFrameCB(int width, int height, const CameraSetup& cam,
                           DirectX::XMFLOAT3 lightDirRaw, float time) {
    using namespace DirectX;
    FrameCB cb{};
    XMVECTOR camPos = XMLoadFloat3(&cam.eye);
    XMVECTOR camAt  = XMLoadFloat3(&cam.at);
    XMVECTOR up     = XMLoadFloat3(&cam.up);
    XMMATRIX view = XMMatrixLookAtRH(camPos, camAt, up);
    XMMATRIX proj = XMMatrixPerspectiveFovRH(
        XMConvertToRadians(cam.fovDegrees),
        (float)width / (float)(std::max)(height, 1),
        cam.nearZ, cam.farZ);
    XMMATRIX vp = XMMatrixMultiply(view, proj);
    XMStoreFloat4x4(&cb.viewProj, XMMatrixTranspose(vp));
    XMStoreFloat3(&cb.cameraPos, camPos);
    cb.time = time;
    XMVECTOR L = XMVector3Normalize(XMLoadFloat3(&lightDirRaw));
    XMStoreFloat3(&cb.lightDir, L);
    return cb;
}

}  // namespace dxlab
