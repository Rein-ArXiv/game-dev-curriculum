## 들어가며

GJK를 “Minkowski 차가 원점을 포함하는지 simplex로 검사하는 알고리즘”이라고만 구현하면 물리 엔진에 필요한 정보가 부족하다. 실제 narrow phase는 지원점이 어느 도형 feature에서 왔는지 보존하고, 분리 상태에서는 두 도형의 witness point와 거리를 내며, 침투 상태에서는 EPA 또는 다른 detector로 안정적인 법선과 깊이를 만들고, 반복 한도와 퇴화 simplex를 정상적인 상태로 보고해야 한다.

이 글은 Minkowski 차와 simplex의 기하 유도를 반복하지 않는다. 대신 support mapping을 collider와 transform에 연결하는 방법, 종료 상태 계약, witness point 복원, EPA polytope의 topology, manifold 생성, warm start, 실패 fallback과 검증을 다룬다.

---

## 목차

1. [Support vertex 계약](#1-support-vertex-계약)
2. [변환된 도형과 feature 보존](#2-변환된-도형과-feature-보존)
3. [GJK 상태와 종료 조건](#3-gjk-상태와-종료-조건)
4. [거리와 witness point 복원](#4-거리와-witness-point-복원)
5. [EPA의 입력·출력·topology](#5-epa의-입력출력topology)
6. [EPA에서 접촉 manifold로](#6-epa에서-접촉-manifold로)
7. [Warm start와 캐시 무효화](#7-warm-start와-캐시-무효화)
8. [실패 fallback과 검증](#8-실패-fallback과-검증)

---

## 1. Support vertex 계약

Minkowski support point $p=a-b$만 저장하면 최종 거리와 접촉 위치를 원래 도형 위에서 복원하기 어렵다. 각 도형의 witness와 feature를 함께 보존한다.

```cpp
struct SupportVertex {
    Vec3 minkowski;       // pointA - pointB
    Vec3 pointA;
    Vec3 pointB;
    FeatureId featureA;
    FeatureId featureB;
};

SupportVertex SupportMinkowski(
    const SupportShape& a,
    const SupportShape& b,
    Vec3 direction) {
    SupportPoint sa = a.Support(direction);
    SupportPoint sb = b.Support(-direction);
    return {
        .minkowski = sa.position - sb.position,
        .pointA = sa.position,
        .pointB = sb.position,
        .featureA = sa.feature,
        .featureB = sb.feature,
    };
}
```

support 함수의 계약은 다음과 같다.

$$
d\cdot \operatorname{support}(d)
=\max_{x\in S}d\cdot x
$$

$d$를 단위 벡터로 만들 필요는 없지만 0 벡터는 의미가 없다. direction이 너무 작으면 cached direction, 중심 차이, canonical axis 중 결정적인 fallback을 사용한다.

### 도형별 support

| 도형 | support |
|---|---|
| sphere | $c+r\hat d$ |
| capsule | 축 선분 끝점 중 큰 dot + $r\hat d$ |
| box | local direction 부호에 따른 corner |
| convex hull | 최대 dot vertex |
| scaled convex | affine transpose로 local direction 변환 |

convex hull의 선형 검색은 $O(V)$다. adjacency가 있는 convex polytope는 이전 support vertex에서 hill climbing할 수 있지만, 잘못된 adjacency나 비볼록 입력에서는 최대점을 보장하지 못한다. debug build에서 표본 선형 검색과 비교할 수 있다.

### tie-breaker

여러 vertex가 같은 최대 dot을 가질 때 임의 iteration 순서로 하나를 고르면 feature ID와 simplex가 흔들린다. dot 차이가 tolerance 안이면 가장 작은 stable vertex ID를 선택하는 방식으로 canonicalize한다.

## 2. 변환된 도형과 feature 보존

local shape $x$에 affine transform $Mx+t$가 적용되면 world direction $d$의 support는 local direction $M^Td$로 구한다.

$$
\arg\max_x d\cdot(Mx+t)
=\arg\max_x (M^Td)\cdot x
$$

```cpp
SupportPoint TransformedShape::Support(Vec3 worldDirection) const {
    const Vec3 localDirection = Transpose(linear_) * worldDirection;
    SupportPoint local = shape_->Support(localDirection);
    return {
        .position = linear_ * local.position + translation_,
        .feature = local.feature,
    };
}
```

normal 변환에 쓰는 inverse transpose와 support direction의 transpose를 혼동하면 안 된다. support는 점의 affine transform을 목적 함수에 대입해 $M^T$가 나온다.

### margin

GJK shape margin은 단순 epsilon이 아니다. core shape support에 방향별 margin을 더해 접촉 skin을 만든다.

$$
\operatorname{support}_{expanded}(d)
=\operatorname{support}_{core}(d)+m\hat d
$$

margin을 거리 결과에서 빼 원래 도형 separation을 복원한다. 구·캡슐처럼 radius가 의미 있는 도형에서 margin과 실제 radius를 중복 적용하지 않는다.

### Compound와 concave

GJK는 convex support shape를 전제로 한다. compound는 child별 broad phase 후 convex pair를 검사하고, triangle mesh는 BVH로 triangle 후보를 줄인다. concave 전체에 “가장 먼 점” support를 제공해도 원래 concave 도형의 충돌 의미를 보존하지 않는다.

## 3. GJK 상태와 종료 조건

`bool` 대신 알고리즘 상태를 반환한다.

```cpp
enum class GjkStatus {
    Separated,
    Touching,
    Intersecting,
    DuplicateSupport,
    DegenerateSimplex,
    IterationLimit,
    InvalidInput,
};

struct GjkResult {
    GjkStatus status;
    float distance;
    Vec3 normal;
    Vec3 witnessA;
    Vec3 witnessB;
    Simplex simplex;
    uint32_t iterations;
};
```

`DuplicateSupport`, `DegenerateSimplex`, `IterationLimit`은 모두 “분리”가 아니다. 현재 best distance와 simplex는 유용할 수 있지만 신뢰 수준을 함께 전달해야 한다.

### Progress 기준

단순히 새 support가 search direction을 지나지 못했다는 `dot(support, direction) < 0`만 사용하면 touching과 margin에서 민감하다. 거리 모드에서는 원점과 현재 simplex closest point 사이 개선량을 본다.

$$
\Delta=d_{\text{old}}^2-d_{\text{new}}^2
$$

$$
\Delta \le
\epsilon_{\text{abs}}^2+
\epsilon_{\text{rel}}d_{\text{old}}^2
$$

이면 수렴으로 본다. scale에 독립적인 절대 epsilon 하나만 쓰지 않는다.

### Duplicate support

새 support가 simplex의 기존 점과 거의 같으면 더 진행하지 못한다.

$$
\|p_{\text{new}}-p_i\|^2
\le\epsilon_{\text{duplicate}}^2
$$

feature pair가 같으면 위치 tolerance보다 더 강한 duplicate 신호가 된다. duplicate에서 현재 closest point가 contact slop 안이면 Touching, 충분히 멀면 Separated, 애매하면 별도 실패 상태로 남긴다.

### Iteration cap

반복 상한은 무한 루프 방어다. 상한 도달을 정상적인 `Intersecting`으로 바꾸지 않는다. 평균 iteration뿐 아니라 상한 도달 shape pair와 seed를 기록한다.

```cpp
for (uint32_t iteration = 0; iteration < maxIterations; ++iteration) {
    SupportVertex vertex = SupportMinkowski(a, b, direction);
    if (IsDuplicate(simplex, vertex))
        return FinishDuplicate(simplex, iteration);
    simplex.Add(vertex);
    SimplexStep step = ReduceSimplexTowardOrigin(simplex);
    if (step.containsOrigin)
        return FinishIntersecting(simplex, iteration);
    if (Converged(step))
        return FinishSeparatedOrTouching(simplex, iteration);
    direction = -step.closestPoint;
}
return FinishIterationLimit(simplex);
```

## 4. 거리와 witness point 복원

reduced simplex의 Minkowski 점 $p_i=a_i-b_i$와 barycentric weight $\lambda_i$가 있으면 closest witnesses를 원래 도형 위에서 복원한다.

$$
p=\sum_i\lambda_i p_i,\qquad
p_A=\sum_i\lambda_i a_i,\qquad
p_B=\sum_i\lambda_i b_i
$$

$$
\sum_i\lambda_i=1,\qquad \lambda_i\ge0
$$

```cpp
WitnessPair RecoverWitness(const Simplex& simplex,
                           Span<const float> weights) {
    Vec3 a{0, 0, 0};
    Vec3 b{0, 0, 0};
    for (size_t i = 0; i < simplex.size; ++i) {
        a += simplex[i].pointA * weights[i];
        b += simplex[i].pointB * weights[i];
    }
    return {a, b};
}
```

분리 상태의 법선은 다음처럼 정의한다.

$$
n=\frac{p_B-p_A}{\|p_B-p_A\|}
$$

거리가 거의 0이면 정규화할 수 없다. cached normal이나 simplex feature normal을 사용하고 Touching으로 분류한다.

### Barycentric 안정성

triangle 또는 tetrahedron의 퇴화에서 면적·부피 분모가 작아진다. 가장 낮은 차원의 유효 feature로 축소한다.

- tetrahedron 부피가 작음: 유효 triangle face 평가
- triangle 면적이 작음: 유효 edge 평가
- edge 길이가 작음: point 평가

weight는 작은 음수 오차를 clamp한 뒤 다시 정규화할 수 있지만, 큰 음수는 region 분류 오류다. 무조건 clamp하면 잘못된 witness를 숨긴다.

### 거리와 margin

core support로 구한 core distance에서 두 margin을 뺀다.

$$
d_{\text{surface}}=d_{\text{core}}-m_A-m_B
$$

$d_{\text{surface}}\le0$이면 실제 core가 분리되어 있어도 contact skin은 겹친다. solver가 margin contact를 실제 penetration과 어떻게 구분하는지 status에 반영한다.

## 5. EPA의 입력·출력·topology

GJK가 원점을 포함하는 simplex를 만들면 EPA는 그 simplex를 둘러싼 polytope를 바깥으로 확장해 원점에 가장 가까운 boundary face를 찾는다. 여기서는 수학적 유도보다 입력 조건과 topology 보존이 중요하다.

```cpp
struct EpaResult {
    EpaStatus status;
    Vec3 normal;
    float depth;
    Vec3 witnessA;
    Vec3 witnessB;
    FeaturePair feature;
    uint32_t iterations;
};
```

### Seed polytope

3D EPA는 non-degenerate tetrahedron이 필요하다. GJK가 line이나 triangle 상태에서 intersection을 판정했다면 추가 support direction으로 tetrahedron을 확장한다.

seed 검증:

- 네 점이 중복되지 않음
- signed volume 절댓값이 tolerance보다 큼
- face winding이 원점에 대해 일관됨
- 원점이 모든 face의 내부 half-space에 있음

실패하면 EPA를 시작하지 않고 `InvalidSeed`를 반환한다.

### Face 구조

```cpp
struct EpaFace {
    uint32_t a, b, c;
    Vec3 normal;
    float distance;
    bool obsolete;
};
```

face normal은 원점에서 바깥으로 향하고 `distance = Dot(normal, vertex[a])`가 음수면 winding을 뒤집는다. distance가 finite인지 검사한다.

### 확장

1. 원점에 가장 가까운 face를 priority queue에서 꺼낸다.
2. 그 face normal 방향으로 새 support를 구한다.
3. support distance 개선량을 계산한다.
4. 수렴하지 않았으면 새 점에서 보이는 face를 제거한다.
5. 제거된 영역의 boundary horizon edge를 찾는다.
6. horizon edge와 새 점으로 face를 만든다.

$$
\Delta=d_{\text{support}}-d_{\text{face}}
$$

$\Delta\le\epsilon_{\text{EPA}}$이면 수렴한다.

### Horizon edge

보이는 face의 모든 directed edge를 모으고 반대 방향 edge가 있으면 내부 edge로 상쇄한다. 한 번만 남은 edge가 horizon이다.

```cpp
void AddBoundaryEdge(
    SmallVector<DirectedEdge, 64>& edges,
    DirectedEdge edge) {
    auto reverse = Find(edges, DirectedEdge{edge.to, edge.from});
    if (reverse != edges.end())
        edges.erase(reverse);
    else
        edges.push_back(edge);
}
```

non-manifold 입력이나 수치 오차로 horizon이 닫힌 loop를 이루지 않으면 확장을 중단한다. 임의로 face를 이어 붙이면 self-intersecting polytope가 생긴다.

### 용량과 상한

EPA는 vertex·face 수가 증가한다. 고정 용량을 사용하면 초과를 `OutOfCapacity`로 보고하고 현재 best face를 보수적 근사로 사용할지 dispatcher가 결정한다. silent overwrite는 메모리 손상이다.

## 6. EPA에서 접촉 manifold로

수렴 face의 normal과 distance는 Minkowski 공간의 penetration vector를 준다.

$$
\operatorname{MTV}=n\,d
$$

face의 원점 closest point에 대한 barycentric weight를 구해 세 support vertex의 A/B witness를 복원한다.

```cpp
const Barycentric w =
    ClosestPointBarycentricToOrigin(facePoints);
result.witnessA =
    w.x * v0.pointA + w.y * v1.pointA + w.z * v2.pointA;
result.witnessB =
    w.x * v0.pointB + w.y * v1.pointB + w.z * v2.pointB;
```

### 한 점은 manifold가 아니다

EPA witness 하나는 법선과 대표 접촉 위치를 주지만 box가 바닥에 놓인 face-face 접촉에서 안정적인 네 점을 자동으로 만들지 않는다. polyhedral shape는 EPA normal을 이용해 reference/incident face를 고르고 clipping해 manifold를 확장할 수 있다.

smooth shape는 한 점이 자연스럽다.

| 도형 쌍 | manifold 생성 |
|---|---|
| sphere-sphere | 중심선 위 한 점 |
| capsule-capsule | 축 closest points와 radius |
| smooth convex | EPA witness 한 점 |
| polyhedron-polyhedron | EPA/SAT normal + face clipping |
| edge-edge | closest segment points |

따라서 GJK/EPA가 모든 도형 쌍에 완전한 contact manifold를 자동 제공한다는 설명은 과장이다.

### 법선 방향

Minkowski 차를 $A-B$로 정의했는지 $B-A$로 정의했는지에 따라 EPA normal 부호가 달라진다. 침투 상태에서는 `witnessB - witnessA`가 법선과 반대 방향일 수 있으므로 그 벡터만으로 방향을 고르면 안 된다. 최종 API에서는 support convention을 고정하고, 그 convention($A-B$)에서 EPA가 반환한 face의 바깥 방향 법선을 그대로 A에서 B 방향으로 쓴다. 중심 차이로 부호를 다시 정하면 비대칭 도형에서 옳은 법선을 뒤집어 B를 `normal * (depth + margin)`만큼 옮겨도 분리되지 않는다.

깊은 대칭 penetration에서 원점까지 거리가 같은 face가 여럿이라 법선이 유일하지 않으면 cached normal과 canonical feature ordering을 사용한다.

## 7. Warm start와 캐시 무효화

GJK warm start는 이전 search direction 또는 simplex feature pair를 재사용한다. 물체가 조금 움직였다면 원점에 가까운 simplex도 비슷할 가능성이 높다.

```cpp
struct GjkCache {
    BodyHandle bodyA;
    BodyHandle bodyB;
    ShapeRevision revisionA;
    ShapeRevision revisionB;
    SmallVector<FeaturePair, 4> simplexFeatures;
    Vec3 separatingAxis;
    uint64_t lastUsedTick;
};
```

world position을 그대로 cache하기보다 feature pair를 저장하고 현재 transform에서 support를 다시 만든다. 이전 Minkowski 점은 물체가 움직이면 stale하다.

### 무효화 조건

- collider shape revision 변경
- body handle generation 변경
- teleport 또는 world-origin discontinuity
- scale·margin 변경
- cache age 초과
- pair ordering 변경
- cached feature가 현재 hull에 없음

작은 rigid transform에서는 cache를 유지하되, 첫 iteration의 progress가 나쁘면 cold start로 재시작한다. 잘못된 warm start를 끝까지 끌고 가지 않는다.

### Separating axis cache

분리된 pair는 이전 separating axis로 support를 한 번 검사해 빠르게 reject할 수 있다. 하지만 이 축이 더 이상 분리축이 아니면 정상 GJK로 들어가야 한다.

### Contact cache와 구분

GJK simplex cache와 solver manifold cache는 목적이 다르다.

- simplex cache: collision query의 반복 수 감소
- manifold cache: 접촉점 impulse warm start와 안정성

하나의 cache lifetime으로 묶으면 shape query는 유효하지만 contact impulse는 stale인 경우를 구분하기 어렵다.

## 8. 실패 fallback과 검증

도형 쌍에 가장 강한 detector를 우선한다.

| 도형 쌍 | 우선 경로 | GJK/EPA 역할 |
|---|---|---|
| sphere-sphere | analytic | 일반 fallback 불필요 |
| capsule-capsule | segment distance | 복잡한 변형에서 선택 |
| box-box | 특화 SAT | differential 검증·fallback |
| convex hull-hull | GJK distance + EPA | 주 경로 |
| convex-triangle | 특화 feature 보존 경로 | 후보 경로 |
| concave-convex | BVH + child convex query | child detector |

### 실패 정책

`IterationLimit`이나 `InvalidSeed`에서 선택 가능한 정책:

1. 이전 안정 manifold를 짧은 기간 유지한다.
2. box/polytope라면 SAT로 다시 검사한다.
3. distance best estimate가 slop보다 크면 분리로 처리한다.
4. 매우 가까운 애매한 경우 speculative contact를 만든다.
5. 오류 telemetry와 재현 seed를 저장한다.

무조건 hit로 처리하면 유령 벽이 생기고, 무조건 miss로 처리하면 빠른 물체가 통과한다. fallback은 shape pair와 gameplay 위험에 따라 달라야 한다.

### Differential test

box-box와 convex polygon은 SAT와 GJK 결과를 비교할 수 있다.

```cpp
for (uint64_t seed = 0; seed < sampleCount; ++seed) {
    Pair pair = RandomConvexPair(seed);
    SatResult sat = RunSat(pair);
    GjkResult gjk = RunGjk(pair);
    if (!EquivalentWithinSlop(sat, gjk))
        SaveFixture(seed, pair, sat, gjk);
}
```

두 알고리즘이 tolerance 경계에서 다른 상태를 낼 수 있으므로 exact boolean만 비교하지 않는다. separation, touching band, penetration으로 구간을 나눈다.

### 변환 성질

1. 두 도형에 같은 translation을 적용해도 distance와 depth가 같다.
2. 같은 rigid rotation을 적용하면 normal과 witnesses도 같이 회전한다.
3. 입력 순서를 바꾸면 상태는 같고 A/B witness와 normal이 뒤집힌다.
4. 분리 결과에서 $\|p_B-p_A\|$는 reported distance와 일치한다.
5. 침투 결과에서 B를 `normal * (depth + margin)`만큼 옮기면 분리되어야 한다.
6. 모든 status에서 반환 숫자는 finite다.

### 퇴화 fuzzing

- 거의 동일한 vertex
- 매우 얇은 tetrahedron
- 평행 face와 edge
- 큰 좌표와 작은 shape
- 0에 가까운 direction
- nonuniform scale
- touching과 작은 penetration

iteration count, duplicate support, degenerate simplex, EPA capacity, fallback 사용률의 P95/P99를 기록한다. 평균 반복 수만으로 강건성을 평가하지 않는다.

### 결정론

support tie-breaker, face priority queue tie, horizon edge 순서, cache pair ordering을 stable ID로 정렬한다. 같은 부동소수점 연산 환경에서도 unordered container iteration에 따라 다른 simplex를 고르면 결과가 갈릴 수 있다.

---

## 결론

물리 엔진의 GJK/EPA 구현은 원점 포함 검사 하나로 끝나지 않는다. support vertex에 A/B witness와 feature를 보존하고, distance·touching·intersection·수치 실패를 구분하며, barycentric weight로 원래 도형의 접촉 정보를 복원해야 한다.

EPA는 안정적인 seed와 manifold topology를 요구하고, 그 결과의 한 점은 polyhedral face contact의 완전한 manifold가 아니다. shape별 analytic·SAT·clipping 경로, cache 무효화, 반복 상한, differential test, 실패 fallback까지 갖춰야 support 기반 narrow phase가 solver에 신뢰할 수 있는 입력을 제공한다.

## 더 읽을거리

- E. G. Gilbert, D. W. Johnson, S. S. Keerthi, “A Fast Procedure for Computing the Distance Between Complex Objects in Three-Dimensional Space” — GJK 원 논문
- Gino van den Bergen, *Collision Detection in Interactive 3D Environments* — support mapping, GJK, 수치 안정성
- Christer Ericson, *Real-Time Collision Detection* — closest point, barycentric coordinate, convex query
- Gino van den Bergen, “A Fast and Robust GJK Implementation for Collision Detection of Convex Objects” — 구현 종료 조건과 강건성
- Bullet Physics 소스의 `btGjkPairDetector`, `btGjkEpa2` — 산업 구현의 상태와 fallback 구조
- Erin Catto, Box2D 기술 자료 — distance proxy, simplex cache, witness point
