## 들어가며

`math_lab` 의 여섯 번째 단계 — *Spatial / BVH Scene*. math 시리즈의 *part15 (공간 분할)* + *part16 (BVH)* + *part17 의 Slab Test* 가 *마우스로 광선을 끌면 BVH 가 가지치기되는 모습 + 빌드 깊이 슬라이더로 트리 구조 갱신* 의 시각으로 살아난다.

이 단계의 결과물.

- **N 개의 작은 AABB 분포**: seed + N 슬라이더로 *결정론적* 으로 캔버스에 배치 (4~500 개).
- **2D BVH 빌드** (top-down median split, leaf threshold 슬라이더).
- **빌드 깊이 슬라이더**: *0 ~ 최대 깊이* — 표시할 BVH 노드 박스를 깊이별로 점진적 노출. *빌드 단계 애니메이션* 효과.
- **드래그 가능한 광선**: 시작점 (`ro`) + 끝점 (`rd`). 끌면 광선 즉시 갱신.
- **BVH 트래버설 시각**: 방문한 노드 *하늘색 강조*, leaf 는 *진한 강조*, hit 된 객체는 *빨강*.
- **`only visited` 토글**: BVH 의 *방문 노드만* 표시 — 가지치기가 *얼마나 효율적인가* 시각.

핵심 통찰들 — *BVH 의 노드 박스가 자식들의 합집합*, *median split 으로 균형 트리*, *Slab Test 가 매 노드 한 번*, *광선이 닿지 않는 가지를 통째로 cut*, *leaf 의 객체만 정밀 검사* — 모두 *마우스에 따라 즉시 갱신*.

---

## 목차
1. [2D BVH 자료구조](#1-2d-bvh-자료구조)
2. [Top-down Median Split 빌드](#2-top-down-median-split-빌드)
3. [Slab Test 와 Ray-AABB 2D](#3-slab-test-와-ray-aabb-2d)
4. [트래버설 + 방문 노드 trace](#4-트래버설--방문-노드-trace)
5. [SpatialScene 의 결정론적 객체 분포](#5-spatialscene-의-결정론적-객체-분포)
6. [빌드 깊이 슬라이더로 애니메이션](#6-빌드-깊이-슬라이더로-애니메이션)
7. [방문 노드 / hit 객체 강조](#7-방문-노드--hit-객체-강조)
8. [빌드와 실행 결과](#8-빌드와-실행-결과)

---

## 1. 2D BVH 자료구조

`algo/bvh2d.h` 의 핵심 구조.

<!-- snippet: src/algo/bvh2d.h -->
```cpp
struct AABB2D {
    Vector2 min;
    Vector2 max;
    Vector2 Center() const { return {(min.x + max.x) * 0.5f,
                                       (min.y + max.y) * 0.5f}; }
    float   Width()  const { return max.x - min.x; }
    float   Height() const { return max.y - min.y; }
};
// ... AABBUnion / RayAABB2D — §3 ...
struct BVHNode2D {
    AABB2D bounds;
    int    leftChild  = -1;     // -1 면 leaf
    int    rightChild = -1;
    int    firstObj   = 0;      // leaf 의 객체 시작 인덱스 (order_ 안)
    int    objCount   = 0;
    int    depth      = 0;      // 시각화용
};

struct RayTraversalResult {
    bool                  hit;
    float                 hitT;
    int                   hitObjIdx;
    std::vector<int>      visitedNodes;
};

class BVH2D {
public:
    void Build(const std::vector<AABB2D>& boxes,
                int leafThreshold = 4);

    const std::vector<BVHNode2D>& Nodes()        const { return nodes_; }
    const std::vector<int>&        ObjectIndex() const { return order_; }

    RayTraversalResult Trace(const std::vector<AABB2D>& boxes,
                               Vector2 ro, Vector2 rd,
                               float tMax = 1e30f) const;

    int NumNodes() const { return (int)nodes_.size(); }

private:
    std::vector<BVHNode2D>  nodes_;
    std::vector<int>        order_;     // median 분할로 객체 인덱스 재배열
    int                     leafThreshold_ = 4;

    int BuildRec(const std::vector<AABB2D>& boxes,
                  int begin, int end, int depth);
};
```

설계 결정.

- **`order_`**: 객체 자체를 *재배열* 안 함 — 정수 인덱스 배열만. 외부의 boxes 가 *불변*. 시각화에서 *원본 인덱스* 가 의미 있다.
- **`depth` 필드**: 시각화 전용. 빌드 시 기록해 두면 *깊이 슬라이더* 가 단순 비교.
- **포인터 대신 인덱스**: 캐시 친화 + 직렬화 쉬움. parts_perf_lab 에서도 같은 패턴.

## 2. Top-down Median Split 빌드

<!-- snippet: src/algo/bvh2d.h#BVH2D::BuildRec -->
```cpp
inline int BVH2D::BuildRec(const std::vector<AABB2D>& boxes,
                             int begin, int end, int depth) {
    BVHNode2D node{};
    node.depth = depth;
    AABB2D b = boxes[order_[begin]];
    for (int i = begin + 1; i < end; ++i) {
        b = AABBUnion(b, boxes[order_[i]]);
    }
    node.bounds = b;

    int count = end - begin;
    if (count <= leafThreshold_ || depth > 24) {
        node.firstObj = begin;
        node.objCount = count;
        nodes_.push_back(node);
        return (int)nodes_.size() - 1;
    }

    int axis = (b.Width() >= b.Height()) ? 0 : 1;
    int mid = (begin + end) / 2;
    std::nth_element(order_.begin() + begin,
                      order_.begin() + mid,
                      order_.begin() + end,
                      [&](int a, int c) {
                          float va = (axis == 0)
                                          ? boxes[a].Center().x
                                          : boxes[a].Center().y;
                          float vc = (axis == 0)
                                          ? boxes[c].Center().x
                                          : boxes[c].Center().y;
                          return va < vc;
                      });

    int idx = (int)nodes_.size();
    nodes_.push_back(node);   // placeholder

    int left  = BuildRec(boxes, begin, mid, depth + 1);
    int right = BuildRec(boxes, mid, end,  depth + 1);
    nodes_[idx].leftChild  = left;
    nodes_[idx].rightChild = right;
    return idx;
}
```

`std::nth_element` 가 *median 위치에 중앙값* 을 두는 partial sort. $O(n)$ 평균. *전체 정렬 안 함*.

분할 축은 *현재 노드 박스의 가장 긴 축*. 균형 잡힌 트리 — math part16 의 표준.

## 3. Slab Test 와 Ray-AABB 2D

광선이 AABB 와 만나는지 + 거리 t.

<!-- snippet: src/algo/bvh2d.h#RayAABB2D -->
```cpp
inline bool RayAABB2D(Vector2 ro, Vector2 rd, AABB2D box,
                       float& tNear, float& tFar) {
    double near = -std::numeric_limits<double>::infinity();
    double far = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 2; ++i) {
        double ri = (i == 0) ? ro.x : ro.y;
        double di = (i == 0) ? rd.x : rd.y;
        double lo = (i == 0) ? box.min.x : box.min.y;
        double hi = (i == 0) ? box.max.x : box.max.y;
        if (di == 0.0) {
            if (ri < lo || ri > hi) return false;   // 평행 + 밖
        } else {
            double t0 = (lo - ri) / di;
            double t1 = (hi - ri) / di;
            if (t0 > t1) std::swap(t0, t1);
            near = std::max(near, t0);
            far = std::min(far, t1);
            if (near > far) return false;
        }
    }
    const auto toFloat = [](double value) {
        const double limit = std::numeric_limits<float>::max();
        if (value > limit) return std::numeric_limits<float>::infinity();
        if (value < -limit) return -std::numeric_limits<float>::infinity();
        return static_cast<float>(value);
    };
    tNear = toFloat(near);
    tFar = toFloat(far);
    return far >= 0;
}
```

입력 좌표와 방향은 유한하며 AABB의 min ≤ max를 전제한다. `<limits>`를 포함하고, 정확히 0인 방향 성분만 평행으로 처리한다. 작은 비영 방향도 충분히 큰 t에서 slab에 도달할 수 있으므로 epsilon으로 0 취급하면 유효 교차를 놓친다. 차이와 나눗셈은 double로 계산해 float 역수의 overflow와 `0 * infinity`를 피한다. 반환 t가 float 표현 범위를 넘으면 명시적으로 infinity로 변환한다. BVH는 닫힌 구간 `[0, tMax]`를 검사하므로 끝점 접촉과 시작점 내부의 t=0도 hit이다.

## 4. 트래버설 + 방문 노드 trace

<!-- snippet: src/algo/bvh2d.h#BVH2D::Trace -->
```cpp
inline RayTraversalResult BVH2D::Trace(const std::vector<AABB2D>& boxes,
                                          Vector2 ro, Vector2 rd,
                                          float tMax) const {
    RayTraversalResult r{};
    r.hit       = false;
    r.hitT      = tMax;
    r.hitObjIdx = -1;
    if (nodes_.empty()) return r;

    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        const BVHNode2D& n = nodes_[(size_t)idx];

        float tn, tf;
        if (!RayAABB2D(ro, rd, n.bounds, tn, tf)) continue;
        if (tn > r.hitT) continue;          // 가지치기
        r.visitedNodes.push_back(idx);

        if (n.leftChild < 0) {
            // leaf — 객체별 정밀 검사
            for (int i = 0; i < n.objCount; ++i) {
                int objIdx = order_[(size_t)(n.firstObj + i)];
                float oTn, oTf;
                if (RayAABB2D(ro, rd, boxes[(size_t)objIdx], oTn, oTf)) {
                    float t = std::max(oTn, 0.0f);
                    if (t <= r.hitT && (!r.hit || t < r.hitT)) {
                        r.hitT = t;
                        r.hit  = true;
                        r.hitObjIdx = objIdx;
                    }
                }
            }
        } else {
            stack.push_back(n.rightChild);
            stack.push_back(n.leftChild);
        }
    }
    return r;
}
```

스택 기반 트래버설. 방문한 노드를 *visitedNodes* 에 누적 — 시각화의 핵심 입력. *가지치기* 는 두 검사로:

1. `RayAABB2D` 가 false → 이 노드 + 자식 모두 무시.
2. `tn > r.hitT` → 이미 가까운 hit 가 있으면 더 먼 노드 무시.

## 5. SpatialScene 의 결정론적 객체 분포

`seed` 슬라이더로 *재현 가능한* 분포. 매 프레임 같은 seed → 같은 박스들.

<!-- snippet: src/scenes/spatial_scene.cpp#SpatialScene::Regenerate -->
```cpp
void SpatialScene::Regenerate(int cx, int cy, int cw, int ch) {
    boxes_.clear();
    boxes_.reserve((size_t)numObjects_);
    for (int i = 0; i < numObjects_; ++i) {
        float u  = nz::HashTo01(i, 0, (uint32_t)seed_);
        float v  = nz::HashTo01(i, 1, (uint32_t)seed_);
        float wf = nz::HashTo01(i, 2, (uint32_t)seed_);
        float hf = nz::HashTo01(i, 3, (uint32_t)seed_);
        float w  = 8.0f + wf * 30.0f;
        float h  = 8.0f + hf * 30.0f;
        float xMin = (float)cx + 30 + u * (cw - 60 - w);
        float yMin = (float)cy + 30 + v * (ch - 60 - h);
        boxes_.push_back({{xMin, yMin}, {xMin + w, yMin + h}});
    }
    bvh_.Build(boxes_, leafThreshold_);

    // maxDepth_ 가 0 이면 빌드 직후 모든 노드 표시.
    int maxD = 0;
    for (auto& n : bvh_.Nodes()) maxD = std::max(maxD, n.depth);
    if (maxDepth_ <= 0 || maxDepth_ > maxD) maxDepth_ = maxD;
}
```

Step 4 의 `nz::HashTo01` 재활용 — 시각화 lab 안의 *모듈 재사용*. 재생성은 *seed / N / leafThreshold / 캔버스 크기* 가 변할 때만. 빌드가 끝나면 *현재 트리의 최대 깊이* 를 재서 §6 의 깊이 슬라이더 범위를 그 자리에서 맞춘다.

## 6. 빌드 깊이 슬라이더로 애니메이션

`maxDepth_` 슬라이더로 *그릴 깊이* 제한 — *0 (루트만), 1 (루트 + 두 자식), …* 으로 트리가 자라는 모습.

<!-- snippet: src/scenes/spatial_scene.cpp@bvh-traversal-viz -->
```cpp
for (size_t i = 0; i < bvh_.Nodes().size(); ++i) {
    const BVHNode2D& n = bvh_.Nodes()[i];
    if (n.depth > maxDepth_) continue;
    // ... 색 결정 + DrawRectangleLinesEx ...
}
```

`maxDepth_` 가 자동으로 *현재 BVH 의 최대 깊이* 를 기억해 슬라이더 범위가 *재생성마다 갱신*. 사용자가 슬라이더를 끌면 *트리 빌드의 단계* 를 *실시간으로 되감기 / 재생*.

## 7. 방문 노드 / hit 객체 강조

<!-- snippet: src/scenes/spatial_scene.cpp@bvh-traversal-viz -->
```cpp
Vector2 ro = rayStart_.Position();
Vector2 rd = {rayEnd_.Position().x - ro.x, rayEnd_.Position().y - ro.y};
float L = std::sqrt(rd.x * rd.x + rd.y * rd.y);
Vector2 rdN = (L > 1e-6f) ? Vector2{rd.x / L, rd.y / L} : Vector2{1, 0};
auto trav = bvh_.Trace(boxes_, ro, rdN, L);
std::vector<bool> visited((size_t)bvh_.NumNodes(), false);
for (int i : trav.visitedNodes) visited[(size_t)i] = true;

// BVH 노드 그리기 (depth 제한).
for (size_t i = 0; i < bvh_.Nodes().size(); ++i) {
    const BVHNode2D& n = bvh_.Nodes()[i];
    if (n.depth > maxDepth_) continue;
    if (showOnlyVisited_ && !visited[i]) continue;

    Color c{120, 130, 150, 80};
    if (visited[i]) c = Color{120, 220, 255, 160};
    if (n.leftChild < 0) {
        // leaf — 더 강조
        c = visited[i] ? Color{120, 255, 180, 200}
                        : Color{180, 200, 220, 120};
    }
    DrawRectangleLinesEx(
        Rectangle{n.bounds.min.x, n.bounds.min.y,
                   n.bounds.Width(), n.bounds.Height()},
        1.0f, c);
}

// 객체 그리기 (hit 된 객체는 빨강).
for (size_t i = 0; i < boxes_.size(); ++i) {
    const AABB2D& b = boxes_[i];
    Color c = ((int)i == trav.hitObjIdx)
                ? Color{255, 100, 100, 220}
                : Color{255, 200, 80, 140};
    DrawRectangleRec(
        Rectangle{b.min.x, b.min.y, b.Width(), b.Height()}, c);
}
```

`only visited` 토글이 켜지면 *방문 안 한 노드는 안 그림* — *가지치기 효과* 를 시각으로 즉시. N = 500 일 때 광선이 가는 길 위 *수 개의 노드* 만 보임 → BVH 의 *log N 가지치기* 가 어떻게 동작하는지 직관.

## 8. 빌드와 실행 결과

```bash
cmake --build build --config Release
./build/Release/math_lab.exe
```

탭이 *Welcome / Rotation / Interpolation / Noise / Collision / Spatial / BVH* 여섯. Spatial 탭에서:

- 화면 가득 *결정론적 박스들* (seed 슬라이더로 패턴 변경).
- 회색 외곽선이 *BVH 노드 박스*.
- *광선* (노란 실선) 끝점을 끌면 *방문 노드 (하늘색)* 가 즉시 변함.
- 광선이 객체와 만나면 그 객체 *빨강* + hit 점에 빨간 원.
- *only visited* 토글: 방문 안 한 노드 사라짐 → *광선 따라가는 가지치기 경로* 만 보임.
- *show depth ≤ N* 슬라이더: 트리 구조가 *0 → max* 까지 자라는 모습.
- *leaf threshold*: 1 (객체당 leaf) → 16 (큰 leaf). 변하면 트리 *깊이가 줄어드는* 시각.

빌드: MSVC `/W4`, 새 Scene 추가 후도 *경고 0 (third-party 외)* + `math_lab.exe` 정상.

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `bvh/median_split_and_traversal` 이다. 가속 구조에는 함정이 하나 있다 — *가속을 전부 버려도 답은 맞는다*. 트리를 단일 leaf 로 만들고 모든 객체를 훑어도 hit 은 같다. 그래서 검증을 두 층으로 나눈다.

첫 층은 *답이 맞는가*. 무작위 박스 40 개 위로 광선 3,000 개를 쏘아 이 파일 안에 따로 구현한 brute force 오라클과 대조한다 (제품의 `RayAABB2D` 를 다시 부르지 않고 double 로 쓴 독립 slab 구현이다).

둘째 층은 *가속이 실제로 일어났는가*. 광선당 평균 방문 노드 수가 전체 노드 수의 절반보다 작아야 한다.

<!-- snippet: tests/core_tests.cpp@step06-bvh-vs-brute-force -->
```cpp
CHECK(mismatch == 0);
CHECK(hitCount > kRays / 4);   // 대부분 빗나가면 위 대조가 자명해진다
// 가지치기가 실제로 동작하면 광선당 방문 노드는 전체 노드 수보다 훨씬 적다.
const double avgVisited = (double)visitedTotal / (double)kRays;
// ...
CHECK(avgVisited < 0.5 * (double)big.Nodes().size());
```

여기에 트리 자체의 불변식을 따로 건다 — 노드 박스는 그 아래 모든 객체를 포함하고, 내부 노드의 좌/우 부분집합은 *분할축에서 완전히 갈라져* 있고 (`nth_element` 의 계약), leaf 크기가 `leafThreshold` 를 넘지 않고, 모든 객체가 정확히 한 leaf 에 한 번씩 들어간다. `splitOnY > 0` 도 함께 본다 — 축 선택이 x 로 고정돼 있지 않다는 확인이다.

```text
$ ctest --test-dir build -C Release -R math_step06_spatial --output-on-failure
    Start 6: math_step06_spatial
1/1 Test #6: math_step06_spatial ..............   Passed    0.04 sec

100% tests passed out of 1
```

```text
$ ./build/Release/math_core_tests.exe step06
[step06] bvh/median_split_and_traversal .. 87 checks, 0 failed
step06: 1 case(s), 87 check(s), 0 failure(s)
```

87 개 중 40 개는 "각 객체가 정확히 한 leaf 에 있다" 는 객체별 확인이고, 30 개가 구조 불변식 (노드 수·순열 길이 2, 내부 노드 23 개의 구간 연속, 집계 5), 3 개가 3,000 광선 대조의 집계, 나머지 14 개가 slab 단독·3 박스 트리·거리 가지치기·빈 빌드의 소형 단위 검사다.

---

## 결론

Step 6 에서 만든 것.

1. **`algo/bvh2d.h`** — `AABB2D`, `RayAABB2D` (NaN-safe), `BVHNode2D`, `BVH2D::Build` (median split), `BVH2D::Trace` (방문 노드 trace).
2. **`scenes/spatial_scene.{h,cpp}`** — 결정론적 객체 분포 (Hash2 재활용), 빌드 깊이 슬라이더, 광선 핸들 두 개, 방문 노드 강조 + only-visited 토글.
3. **재사용 패턴들** — *결정론적 hash 분포*, *방문 노드 trace 와 시각화 분리*, *깊이 슬라이더로 애니메이션* — 이후 Scene 들에서 활용 가능.

math 시리즈 part15 (공간 분할) + part16 (BVH) + part17 의 Slab Test 가 *광선을 끌면 BVH 가 즉시 가지치기되는 모습* 으로 살아 있다. *median split 의 균형*, *방문 노드 vs 전체 노드의 비율*, *leaf threshold 가 트리 깊이에 미치는 영향* 을 사용자가 *직접 본다*.

이 step 의 한계. 빌드 전략이 *median split 한 가지* 뿐이라 SAH 같은 다른 분할과 비교할 수단이 없고, 트래버설도 스택 순서를 광선 방향으로 정렬하지 않는다. 자동 검증이 세는 것도 *방문 노드 수* 라는 대리 지표다 — 가지치기가 절약한 *실제 시간* 은 재지 않으므로, 노드당 비용이 커지는 변경은 방문 수가 그대로여도 느려질 수 있다. 박스 분포도 균일 난수 하나뿐이라 한쪽에 몰린 분포에서 median split 이 얼마나 나빠지는지는 화면에서 seed 를 돌려 보는 것으로만 안다.

## 더 읽을거리

- Christer Ericson, *Real-Time Collision Detection*, Ch.6-7 — BVH·공간 분할의 표준 교과서.
- Amy Williams 외, "An Efficient and Robust Ray-Box Intersection Algorithm" (2005) — slab test 의 NaN-safe 처리 원전.
- [PBRT: Bounding Volume Hierarchies](https://www.pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies) — median/SAH 빌드의 깊은 해설(무료 공개).
