// transform — TRS(Translation·Rotation·Scale) 합성 변환.
//
// 씬 그래프의 노드 하나가 갖는 변환이다. 회전을 쿼터니언으로 들고 다니다가
// 행렬이 필요할 때만 펼친다. 부모-자식 합성(operator*)은 자식의 로컬 변환을
// 부모 공간으로 올린다. 균일 스케일에서 ToMatrix(부모*자식) == ToMatrix(부모)*
// ToMatrix(자식) 이 성립한다(비균일 스케일+회전은 행렬만 정확 — 엔진 공통 한계).

#pragma once

#include "math/mat.hpp"
#include "math/quat.hpp"
#include "math/vec.hpp"

namespace mega {
namespace math {

struct Transform {
    Vec3 position{0, 0, 0};
    Quat rotation = Quat::Identity();
    Vec3 scale{1, 1, 1};

    static Transform Identity() { return Transform{}; }

    // 로컬 → 월드 행렬: M = T * R * S.
    Mat4 ToMatrix() const {
        return Mat4::Translation(position) * ToMat4(rotation) * Mat4::Scale(scale);
    }

    Vec3 TransformPoint(const Vec3& p) const {
        return position + Rotate(rotation, Vec3{p.x * scale.x, p.y * scale.y, p.z * scale.z});
    }
    Vec3 TransformDir(const Vec3& d) const { return Rotate(rotation, d); }

    // 역변환으로 월드 점을 로컬로.
    Vec3 InverseTransformPoint(const Vec3& worldP) const {
        Vec3 v = Rotate(Conjugate(rotation), worldP - position);
        return {v.x / scale.x, v.y / scale.y, v.z / scale.z};
    }

    // 부모(this) ∘ 자식: 자식 로컬 변환을 부모 공간으로.
    Transform operator*(const Transform& child) const {
        Transform r;
        r.position = TransformPoint(child.position);
        r.rotation = Normalize(rotation * child.rotation);
        r.scale    = scale * child.scale;   // 성분곱
        return r;
    }

    Transform Inverse() const {
        Transform r;
        r.rotation = Conjugate(rotation);
        r.scale    = Vec3{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
        r.position = Rotate(r.rotation, position * -1.0f);
        r.position = Vec3{r.position.x * r.scale.x, r.position.y * r.scale.y, r.position.z * r.scale.z};
        return r;
    }
};

}  // namespace math
}  // namespace mega
