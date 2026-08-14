// 투영 깊이 매핑 — 표준 [0,1] 과 Reverse-Z [1,0].
// math part18 의 원근 투영 z 항을 그대로. 렌더링과 무관한 순수 함수라
// GUI 없이 검증 가능하다.
//
// 규약: view 공간은 오른손 좌표계, 카메라가 -Z 를 본다.
//       가시 범위는 viewZ ∈ [-far, -near] (near, far > 0).

#pragma once

#include <cmath>

namespace mathlab {

// 표준 [0, 1] depth (DX/Vulkan 관례). z = -n → 0, z = -f → 1.
inline float StdNdcZ(float viewZ, float n, float f) {
    float A = -f / (f - n);
    float B = -f * n / (f - n);
    float w = -viewZ;
    if (std::fabs(w) < 1e-12f) return 0.0f;
    return (A * viewZ + B) / w;
}

// Reverse-Z [1, 0]. z = -n → 1, z = -f → 0.
// 항등식: ReverseNdcZ(z, n, f) == 1 - StdNdcZ(z, n, f) (정확 산술 기준).
inline float ReverseNdcZ(float viewZ, float n, float f) {
    float A = n / (f - n);
    float B = f * n / (f - n);
    float w = -viewZ;
    if (std::fabs(w) < 1e-12f) return 1.0f;
    return (A * viewZ + B) / w;
}

}  // namespace mathlab
