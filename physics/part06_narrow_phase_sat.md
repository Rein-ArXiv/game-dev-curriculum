## 들어가며

SAT(Separating Axis Theorem)를 수식으로 구현해 `bool Intersects(a, b)`를 얻는 것과, 물리 solver가 사용할 접촉 정보를 만드는 것은 다른 문제다. solver에는 단순한 참·거짓보다 훨씬 강한 계약이 필요하다.

- 법선은 항상 A에서 B를 향해야 한다.
- 침투 깊이는 containment에서도 실제 분리 거리를 나타내야 한다.
- 접촉점은 face-face 접촉에서 흔들리지 않는 manifold를 이루어야 한다.
- 각 점은 프레임 사이 impulse를 재사용할 수 있는 feature ID를 가져야 한다.
- 퇴화 축, 거의 평행한 edge, 매우 작은 도형에서도 NaN을 만들지 않아야 한다.

SAT의 기하학적 정리와 후보 축 유도는 선수 지식으로 두고, 여기서는 그 결과를 *물리 엔진의 narrow phase 계약* 으로 바꾸는 과정에 집중한다. 핵심은 축 검사 자체보다 shape adapter, containment 처리, reference/incident feature 선택, clipping, persistent manifold, 실패 상태다.

---

## 목차

1. [Narrow phase 출력 계약](#1-narrow-phase-출력-계약)
2. [Shape adapter와 좌표 변환](#2-shape-adapter와-좌표-변환)
3. [SAT 실행과 containment MTV](#3-sat-실행과-containment-mtv)
4. [Reference·Incident feature 선택](#4-referenceincident-feature-선택)
5. [클리핑과 접촉점 생성](#5-클리핑과-접촉점-생성)
6. [Feature ID와 persistent manifold](#6-feature-id와-persistent-manifold)
7. [수치 안정성과 실패 정책](#7-수치-안정성과-실패-정책)
8. [도형별 라우팅과 검증](#8-도형별-라우팅과-검증)

---

## 1. Narrow phase 출력 계약

물리 엔진의 접촉 출력은 다음과 같은 형태가 된다.

```cpp
struct ContactPoint {
    Vec3 positionA;
    Vec3 positionB;
    float separation;       // 양수: 분리, 0: 접촉, 음수: 침투
    FeaturePair feature;
    float normalImpulse;    // warm start cache
    Vec2 tangentImpulse;
};

struct ContactManifold {
    BodyHandle bodyA;
    BodyHandle bodyB;
    Vec3 normal;            // A -> B
    SmallVector<ContactPoint, 4> points;
    ContactStatus status;
};
```

접촉점 하나에 world position 하나만 저장하는 것보다 A와 B의 witness point를 각각 저장하는 편이 강하다.

$$
s_i=(p_i^B-p_i^A)\cdot n
$$

$n$은 A에서 B를 향하는 단위 법선이고 $s_i$는 signed separation이다. 이 표현은 body transform이 조금 바뀌었을 때 각 local anchor를 다시 world로 변환해 manifold를 갱신하기 쉽다.

### 법선 방향

검출 함수가 입력 순서에 따라 법선 방향을 바꾸면 solver의 부호 계약이 깨진다. 최소 겹침 축 $n$의 방향은 그 축에서 깊이를 결정한 밀어내기 방향, 즉 두 투영 구간 중 어느 쪽 끝이 더 가까운지로 고정한다(`EvaluateAxis`가 고르는 부호). 중심 차이로 부호를 다시 정하면 비대칭 도형에서 깊이와 방향이 서로 다른 축 정보를 쓰게 되어 MTV 검증이 깨진다.

중심이 같은 대칭 도형은 이 규칙만으로 방향을 결정하지 못한다. 이전 manifold 법선, feature 순서, canonical axis 순서 같은 결정적 tie-breaker가 필요하다.

### 접촉·침투·분리

부동소수점에서는 정확한 0 하나로 상태를 나누지 않는다.

```cpp
if (separation > speculativeDistance)
    return ContactStatus::Separated;
if (separation > 0.0f)
    return ContactStatus::Speculative;
return ContactStatus::Penetrating;
```

speculative contact는 아직 겹치지 않았지만 한 time step 안에 겹칠 가능성이 있는 가까운 쌍이다. solver가 상대 속도까지 고려해 tunneling을 줄이는 데 사용할 수 있다. SAT 검출 결과와 연속 충돌 검출을 혼동하지 않도록 상태를 분리한다.

## 2. Shape adapter와 좌표 변환

SAT 구현은 모든 collider의 저장 방식을 알아서는 안 된다. convex polytope adapter는 최소한 다음 질의를 제공한다.

```cpp
struct ConvexAdapter {
    Span<const Vec3> LocalVertices() const;
    Span<const Face> Faces() const;
    Span<const Edge> UniqueEdges() const;
    Transform WorldTransform() const;
    ShapeRevision Revision() const;
};
```

정점·면·edge adjacency는 런타임에 매 프레임 만들지 않고 content build 또는 shape 생성 시 검증한다.

### 볼록성과 winding

face 정점 순서가 일관되지 않으면 외향 법선이 뒤집힌다. 각 face 평면 $(n_i,d_i)$에 대해 모든 정점 $v$가 허용 오차 안에서 내부 half-space에 있는지 검사한다.

$$
n_i\cdot v-d_i\le\epsilon_{\text{shape}}
$$

오목 mesh를 SAT에 그대로 넣으면 후보 축 집합의 완전성이 보장되지 않는다. convex decomposition 조각이나 triangle mesh 전용 detector로 라우팅한다.

### world 축과 local projection

정점을 모두 world로 변환해 투영할 수도 있지만, rigid transform이라면 축을 local로 옮겨 support 또는 projection을 계산할 수 있다.

$$
\max_{x\in S}(d\cdot(Rx+t))
=d\cdot t+\max_{x\in S}((R^Td)\cdot x)
$$

```cpp
Projection Project(const ConvexAdapter& shape, Vec3 worldAxis) {
    const Transform& xf = shape.WorldTransform();
    const Vec3 localAxis = Transpose(xf.rotation) * worldAxis;
    float minimum = Infinity;
    float maximum = -Infinity;
    for (Vec3 local : shape.LocalVertices()) {
        const float value = Dot(localAxis, local) +
                            Dot(worldAxis, xf.translation);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return {minimum, maximum};
}
```

비균일 scale이 있는 affine transform $M$에서는 방향 변환이 단순 rotation transpose가 아니라 $M^Td$다. physics collider에 arbitrary shear를 허용하면 face normal과 inertia도 함께 복잡해진다. 제품 엔진은 대개 collider scale을 bake하거나 허용 범위를 제한한다.

### 축 정규화

projection 간격은 축 길이에 비례한다. 침투 깊이를 world distance로 사용하려면 축을 정규화하거나 길이로 나눠야 한다.

$$
\operatorname{depth}_{world}
=\frac{\operatorname{depth}_{axis}}{\|a\|}
$$

edge cross product가 거의 0이면 정규화가 노이즈를 증폭한다. face unit normal과 원시 edge×edge는 크기와 단위가 다르므로 같은 $\epsilon_{\text{axis}}^2$로 검사하면 형상을 축소했을 때 모든 edge-cross 후보를 버릴 수 있다. edge 방향을 정규화한 뒤 $\sin\theta$ 기반 각도 임계값을 쓰거나, $\|e_a\times e_b\|^2\le\epsilon_{\text{angle}}^2\|e_a\|^2\|e_b\|^2$처럼 차원이 맞는 각도 기준으로 검사한다. zero edge는 별도로 검증한다.

## 3. SAT 실행과 containment MTV

convex polygon은 face normal, convex polyhedron은 양쪽 face normal과 edge-edge cross product를 후보 축으로 사용한다. 이 후보 집합의 수학적 완전성 자체를 다시 유도하지 않고, 구현에서는 중복 축과 퇴화 축을 안전하게 다루는 것이 중요하다.

```cpp
struct AxisCandidate {
    Vec3 normal;
    AxisKind kind;
    FeatureId featureA;
    FeatureId featureB;
};
```

### 분리 판정

축 $n$에서 두 projection을 $[a_0,a_1]$, $[b_0,b_1]$라 하자.

$$
\operatorname{separated}
\iff a_1 < b_0-\epsilon
\;\lor\;
b_1 < a_0-\epsilon
$$

touching을 충돌로 볼지 분리로 볼지는 engine 정책이다. broad phase와 solver의 contact slop이 같은 의미를 공유해야 경계에서 pair가 깜빡이지 않는다.

### containment의 함정

단순 교집합 길이는 다음과 같다.

$$
o=\min(a_1,b_1)-\max(a_0,b_0)
$$

이 값은 한 구간이 다른 구간 안에 완전히 들어간 경우 실제로 밖으로 밀어낼 거리가 아니다. 예를 들어 $A=[-2,2]$, $B=[-1,1]$이면 교집합 길이는 2지만 B를 A 밖으로 옮기려면 3만큼 이동해야 한다.

두 방향의 분리 거리를 직접 계산한다.

$$
d_+=a_1-b_0,\qquad d_-=b_1-a_0
$$

```cpp
AxisResult EvaluateAxis(Projection a, Projection b, Vec3 axis) {
    const float pushBPositive = a.maximum - b.minimum;
    const float pushBNegative = b.maximum - a.minimum;

    if (pushBPositive < -epsilon || pushBNegative < -epsilon)
        return AxisResult::Separated();

    if (pushBPositive < pushBNegative)
        return {.depth = pushBPositive, .normal = axis};
    return {.depth = pushBNegative, .normal = -axis};
}
```

`EvaluateAxis`는 discrete overlap의 MTV를 만드는 경로다. 여기서 `epsilon`은 투영 반올림을 흡수하는 *수치 허용오차*이고, contact slop/skin처럼 실제 물리 거리를 뜻하는 값이 아니다. 그래서 이 경로는 `push < -epsilon`이면 Separated로 확정한다. 양의 separation을 가진 speculative 접촉은 별도 경로로 분리한다. 두 도형의 witness separation을 signed 값으로 유지한 채 $0 < \text{separation} \le \text{speculativeDistance}$인 축을 Speculative 상태로 넘기고, `epsilon`보다 작은 작은 음수 depth가 overlap으로 통과하는 경우와 섞지 않는다.

overlap 경로에서는 모든 후보 중 가장 작은 양의 분리 거리를 가진 축이 MTV 후보다. 법선 방향은 최종적으로 A에서 B로 canonicalize한다.

### 중복 축

box처럼 평행한 face가 많으면 동일하거나 반대인 축이 반복된다. 정규화된 축 $a,b$에 대해

$$
|a\cdot b|>1-\epsilon_{\parallel}
$$

이면 같은 방향 family로 볼 수 있다. 중복 제거는 성능뿐 아니라 거의 같은 깊이의 축 사이에서 법선이 프레임마다 바뀌는 것을 줄인다. tie에서는 face axis를 edge-edge axis보다 우선하는 정책이 manifold 안정성에 유리하다.

### 축 출처 보존

최소 축의 숫자만 저장하면 접촉점을 만들 때 어떤 feature가 축을 만들었는지 다시 찾아야 한다. `AxisCandidate`에 face/edge ID를 보존해 reference feature 선택으로 연결한다.

## 4. Reference·Incident feature 선택

MTV 법선만으로는 face-face 접촉의 여러 점을 얻을 수 없다. reference face와 incident face를 선택해 incident polygon을 reference side plane으로 자른다.

### Reference face

최소 축이 A의 face normal에서 왔다면 A face가 자연스러운 reference다. B face에서 왔다면 manifold 법선 계약은 A에서 B로 유지하되 reference shape와 clipping 방향을 별도로 기록한다.

```cpp
struct ReferenceFace {
    BodySide owner;
    FaceId face;
    Vec3 outwardNormal;
    Plane plane;
    SmallVector<Plane, 8> sidePlanes;
};
```

reference face는 MTV 법선과 가장 잘 정렬되는 face로 고른다.

$$
f_r=\arg\max_f n_f\cdot n_{\text{target}}
$$

### Incident face

incident face는 reference normal에 가장 반대인 normal을 가진다.

$$
f_i=\arg\min_f n_f\cdot n_r
$$

이 선택은 contact patch를 reference face 위로 투영할 polygon을 준다.

### Edge-edge 축

최소 축이 edge cross product에서 왔다면 face clipping이 아니라 두 선분의 closest points를 구한다. 무한 직선 해를 구한 뒤 parameter를 $[0,1]$로 clamp하고, clamp 때문에 상대 선분의 최적점이 바뀌므로 한 번 더 갱신한다.

```cpp
ClosestSegmentResult ClosestPoints(
    Segment edgeA, Segment edgeB, float epsilon);
```

거의 평행한 edge는 분모가 작다. 이 경우 endpoint-to-segment 후보들을 평가해 가장 가까운 쌍을 고르는 fallback이 안정적이다.

## 5. 클리핑과 접촉점 생성

incident polygon을 reference face의 side plane들로 순차 clipping한다. 한 평면 $n\cdot x\le d$에 대해 각 edge의 시작점과 끝점이 내부인지 검사한다.

```cpp
SmallVector<ClipVertex, 8> ClipAgainstPlane(
    Span<const ClipVertex> input, Plane plane, float epsilon) {
    SmallVector<ClipVertex, 8> output;
    for (size_t i = 0; i < input.size(); ++i) {
        const ClipVertex& a = input[i];
        const ClipVertex& b = input[(i + 1) % input.size()];
        // 판정과 교차 보간이 같은 확장 평면을 쓰도록 ε을 먼저 뺀다.
        const float da = SignedDistance(plane, a.position) - epsilon;
        const float db = SignedDistance(plane, b.position) - epsilon;
        const bool insideA = da <= 0.0f;
        const bool insideB = db <= 0.0f;

        if (insideA) output.push_back(a);
        if (insideA != insideB) {
            const float t = da / (da - db);
            output.push_back(InterpolateClipVertex(a, b, t));
        }
    }
    return output;
}
```

이때 inside 판정과 교차 보간은 *같은 경계*를 사용해야 한다. `da`, `db`에서 ε을 미리 빼 두면 inside 판정(`da <= 0`)과 교차 파라미터 $t=d_a/(d_a-d_b)$가 동일한 확장 평면을 기준으로 계산되므로, ε 때문에 교차점이 선분 밖($t\notin[0,1]$)으로 나가는 일이 생기지 않는다. 분모 $d_a-d_b$가 매우 작으면 edge가 평면과 거의 평행하다. `insideA != insideB`인 정상 부호 교차에서는 분모가 0이 아니지만 epsilon 분류가 개입하면 매우 작을 수 있으므로 제한과 유한성 검사가 필요하다.

side plane clipping이 끝난 점을 reference face plane과 비교한다.

$$
s=n_r\cdot p-d_r
$$

$s\le\text{contactSlop}$인 점만 접촉 후보로 남기고, A/B witness를 법선 방향으로 복원한다.

```cpp
point.positionIncident = clipped.position;
point.positionReference = clipped.position - reference.outwardNormal * separation;
point.separation = separation;
```

여기서 separation은 reference의 외향 단위 법선 n_r로 계산한 값이다. projection에도 같은 n_r를 써야 한다. reference owner가 B이면 global manifold normal은 −n_r이므로 이를 그대로 projection에 쓰면 반대편으로 점을 옮기는 오류가 난다. A/B 저장 순서를 뒤집되 manifold normal은 계속 A에서 B를 향하게 한다.

### 점 수 줄이기

3D polygon clipping은 4개보다 많은 점을 만들 수 있다. sequential impulse solver는 보통 최대 4점을 사용한다. 무작정 앞의 4개를 고르면 vertex 순서에 따라 manifold가 바뀐다.

대표적인 축소 정책은 다음과 같다.

1. 가장 깊은 점을 유지한다.
2. 그 점에서 가장 먼 점을 선택한다.
3. 두 점이 만드는 선에서 삼각형 넓이가 가장 큰 점을 선택한다.
4. 세 점의 patch를 가장 넓히는 점을 선택한다.

이 정책은 contact patch의 공간 범위를 보존한다.

## 6. Feature ID와 persistent manifold

warm start는 “비슷한 위치의 점”이 아니라 같은 기하 feature의 접촉을 찾아야 안정적이다.

```cpp
struct FeaturePair {
    FeatureId featureA;
    FeatureId featureB;
    uint16_t childA;
    uint16_t childB;
};
```

clip vertex는 incident edge·vertex와 reference clipping plane의 출처를 전달한다. 새 교차점은 incident edge와 reference side feature처럼 두 형상의 feature 조합으로 식별하고, child ID와 feature 종류도 키에 포함한다. 서로 다른 형상의 숫자 ID를 단순히 작은 값·큰 값으로 정렬하면 소속이 사라지고 다른 점이 같은 키가 될 수 있다. body pair를 안정 ID로 canonicalize한 뒤 그 순서에 맞춰 feature를 보관한다. A/B를 교환할 때는 featureA·featureB와 childA·childB를 함께 바꾼다. reference face가 바뀌어 대응 feature를 복원할 수 없으면 새 접촉으로 처리하거나 anchor 기반 별도 매칭을 쓴다. 이 규칙은 폴리헤드럴 face pair의 clipping 점을 서로 구별하기 위한 것이며, vertex ID가 없는 smooth shape에는 그대로 적용되지 않는다.

### manifold 갱신

새 narrow phase 결과와 이전 manifold를 feature ID로 매칭한다.

```cpp
for (ContactPoint& fresh : newManifold.points) {
    if (const ContactPoint* old =
            FindByFeature(oldManifold, fresh.feature)) {
        fresh.normalImpulse = old->normalImpulse;
        fresh.tangentImpulse = old->tangentImpulse;
    }
}
```

feature가 같아도 local anchor가 너무 멀어졌거나 normal이 크게 바뀌면 cache를 버린다.

$$
n_{\text{old}}\cdot n_{\text{new}} < \cos\theta_{\max}
$$

또는

$$
\|p_{\text{old}}-p_{\text{new}}\| > d_{\max}
$$

이면 stale contact로 본다.

feature ID가 없는 smooth shape의 접촉은 vertex ID 대신 이전 접촉점과의 local anchor 근접성으로 매칭한다. 새로 생긴 점은 normal·tangent impulse를 0으로 초기화하고, 매칭된 점만 이전 값을 승계한다.

### 정렬과 결정론

point 순서를 penetration, feature ID, local coordinate 같은 canonical key로 정렬한다. parallel solver와 네트워크 결정론에서는 container iteration 순서에 맡기지 않는다.

### 접선 impulse

normal이 조금 변하면 이전 tangent basis도 변한다. tangent impulse를 두 scalar 그대로 재사용하기보다 이전 world friction impulse를 새 tangent basis에 재투영할 수 있다.

## 7. 수치 안정성과 실패 정책

epsilon 하나를 모든 크기에 쓰면 작은 객체는 과도하게 붙고 큰 객체는 접촉을 놓친다. shape scale $L$과 machine epsilon을 고려한 상대·절대 tolerance를 사용한다.

$$
\epsilon(L)=\epsilon_{\text{abs}}+\epsilon_{\text{rel}}L
$$

### 검증해야 할 입력

- NaN 또는 infinity transform
- 0 scale과 거의 0인 edge
- 중복 vertex
- 면적이 거의 0인 face
- 잘못된 winding
- non-convex polytope
- 지나치게 큰 좌표

제품 빌드에서 무조건 assert로 종료할지, collider를 비활성화하고 telemetry를 남길지 정책을 정한다.

### 축 tie와 법선 흔들림

깊이 차이가 tolerance 안인 두 축이 있으면 이전 프레임 법선과 더 가까운 축을 고를 수 있다.

```cpp
if (NearlyEqual(candidate.depth, best.depth, tieEpsilon)) {
    if (Dot(candidate.normal, cachedNormal) >
        Dot(best.normal, cachedNormal))
        best = candidate;
}
```

이 coherence bias는 명확히 더 얕은 축을 무시해서는 안 된다.

### 실패 상태

함수가 `false` 하나만 반환하면 “분리”와 “입력 불량”을 구분할 수 없다.

```cpp
enum class ContactStatus {
    Separated,
    Speculative,
    Penetrating,
    InvalidShape,
    NumericalFailure,
};
```

수치 실패를 무조건 충돌이나 분리로 바꾸면 각각 유령 충돌 또는 tunneling을 만든다. shape pair별 analytic detector, 이전 안정 manifold의 짧은 유지, 보수적 fallback 중 하나를 명시한다.

## 8. 도형별 라우팅과 검증

모든 쌍을 SAT로 처리하는 것은 좋은 추상화가 아니다.

| 도형 쌍 | 우선 detector |
|---|---|
| sphere-sphere | 중심 거리 analytic |
| sphere-capsule | point-segment 거리 |
| capsule-capsule | segment-segment 거리 |
| box-box | 특화 15축 SAT + clipping |
| convex polytope-polytope | 일반 SAT 또는 support 기반 detector |
| convex-triangle | triangle feature를 보존하는 특화 경로 |
| concave mesh-convex | BVH로 triangle 후보 후 개별 detector |

dispatcher는 type 순서와 같은 type 사이의 안정 body ID로 입력 순서를 canonicalize하되 출력 normal을 원래 A에서 B 방향으로 되돌린다. 중심이 같은 대칭 쌍도 같은 canonical 순서에서 tie를 풀어야 A/B 교환 시 일관되게 Flip할 수 있다.

```cpp
ContactManifold Collide(const Collider& a, const Collider& b) {
    const PairKey key = CanonicalPair(a.Type(), b.Type());
    ContactManifold result = key.swapped ? table[key](b, a) : table[key](a, b);
    if (key.swapped) result.Flip();
    return result;
}
```

### 속성 기반 검증

SAT 결과는 예제 몇 개보다 성질로 검증한다.

1. **대칭성**: `Collide(A,B)`와 `Collide(B,A)`의 상태는 같고 normal은 반대다.
2. **평행 이동 불변성**: 두 도형을 같은 $t$만큼 옮겨도 depth와 feature 관계가 같다.
3. **회전 불변성**: 같은 rigid rotation을 적용하면 normal과 points도 같이 회전한다.
4. **MTV 검증**: B를 `normal * (depth + margin)`만큼 이동하면 분리되어야 한다.
5. **manifold plane**: 모든 point의 separation이 허용 오차 안에서 일관된다.
6. **유한성**: 모든 출력은 finite다.
7. **feature 안정성**: 작은 transform 변화에서 동일 접촉은 가능한 한 같은 ID를 유지한다.

box-box는 알려진 analytic 결과와 비교하고, 무작위 convex hull은 독립 detector와 differential test한다. 실패 seed, transform, vertex를 직렬화해 재현 fixture로 보존한다.

### 계측

- pair type별 호출 수와 P95 시간
- 검사한 axis 수
- early-out axis 위치
- clipping 전후 point 수
- manifold cache hit율
- invalid shape와 numerical failure 수
- normal flip과 feature churn 비율

평균 시간만 보면 거의 평행한 edge가 많은 최악 입력을 놓친다. 후보 축 수와 긴 꼬리를 함께 기록해야 한다.

---

## 결론

물리 엔진에서 SAT의 핵심은 분리축 정리를 다시 증명하는 데 있지 않다. projection과 후보 축을 solver가 신뢰할 수 있는 법선·침투 깊이·witness point·feature ID로 바꾸고, containment와 퇴화 입력에서도 그 계약을 유지하는 데 있다.

face 접촉은 reference/incident 선택과 clipping으로 patch를 만들고, edge 접촉은 closest points로 처리한다. persistent manifold는 feature ID와 local anchor를 이용해 impulse를 재사용하되, normal·형상 revision·거리 변화에서 cache를 무효화해야 한다. 이 경계까지 갖춰야 SAT가 실제 rigid-body solver의 narrow phase가 된다.

## 더 읽을거리

- Christer Ericson, *Real-Time Collision Detection* — SAT, projection, clipping, closest feature 계산
- David Eberly, *3D Game Engine Design* — convex polyhedra와 contact set
- Erin Catto, “Contact Manifolds” 및 Box2D 기술 자료 — clipping, feature ID, persistent manifold
- Gino van den Bergen, *Collision Detection in Interactive 3D Environments* — 수치적으로 강건한 convex collision
- Dirk Gregorius, “The Separating Axis Test” — box·convex polyhedra의 SAT 구현 세부
