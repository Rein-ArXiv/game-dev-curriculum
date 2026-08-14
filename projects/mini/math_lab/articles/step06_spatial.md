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

```cpp
struct AABB2D {
    Vector2 min, max;
    Vector2 Center() const { return {(min.x+max.x)*0.5f,
                                       (min.y+max.y)*0.5f}; }
};

struct BVHNode2D {
    AABB2D bounds;
    int    leftChild  = -1;     // -1 면 leaf
    int    rightChild = -1;
    int    firstObj   = 0;      // leaf 의 객체 시작 인덱스 (order_ 안)
    int    objCount   = 0;
    int    depth      = 0;      // 시각화용
};

class BVH2D {
public:
    void Build(const std::vector<AABB2D>& boxes, int leafThreshold = 4);
    RayTraversalResult Trace(...) const;
    const std::vector<BVHNode2D>& Nodes()        const { return nodes_; }
    const std::vector<int>&        ObjectIndex() const { return order_; }
private:
    std::vector<BVHNode2D>  nodes_;
    std::vector<int>        order_;     // median 분할로 객체 인덱스 재배열
};
```

설계 결정.

- **`order_`**: 객체 자체를 *재배열* 안 함 — 정수 인덱스 배열만. 외부의 boxes 가 *불변*. 시각화에서 *원본 인덱스* 가 의미 있다.
- **`depth` 필드**: 시각화 전용. 빌드 시 기록해 두면 *깊이 슬라이더* 가 단순 비교.
- **포인터 대신 인덱스**: 캐시 친화 + 직렬화 쉬움. parts_perf_lab 에서도 같은 패턴.

## 2. Top-down Median Split 빌드

```cpp
inline int BVH2D::BuildRec(const std::vector<AABB2D>& boxes,
                             int begin, int end, int depth) {
    BVHNode2D node{};
    node.depth = depth;
    AABB2D b = boxes[order_[begin]];
    for (int i = begin + 1; i < end; ++i) b = AABBUnion(b, boxes[order_[i]]);
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
                          float va = (axis == 0) ? boxes[a].Center().x
                                                  : boxes[a].Center().y;
                          float vc = (axis == 0) ? boxes[c].Center().x
                                                  : boxes[c].Center().y;
                          return va < vc;
                      });

    int idx = (int)nodes_.size();
    nodes_.push_back(node);
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

```cpp
inline bool RayAABB2D(Vector2 ro, Vector2 rd, AABB2D box,
                       float& tNear, float& tFar) {
    tNear = -1e30f;
    tFar  =  1e30f;
    for (int i = 0; i < 2; ++i) {
        float ri = (i == 0) ? ro.x : ro.y;
        float di = (i == 0) ? rd.x : rd.y;
        float lo = (i == 0) ? box.min.x : box.min.y;
        float hi = (i == 0) ? box.max.x : box.max.y;
        if (std::fabs(di) < 1e-8f) {
            if (ri < lo || ri > hi) return false;   // 평행 + 밖
        } else {
            float invD = 1.0f / di;
            float t0 = (lo - ri) * invD;
            float t1 = (hi - ri) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tNear = std::max(tNear, t0);
            tFar  = std::min(tFar,  t1);
            if (tNear > tFar) return false;
        }
    }
    return tFar >= 0;
}
```

math part17 의 *NaN-safe 분기 버전*. 광선 방향이 *축에 평행* (rd ≈ 0) 이면 *ro 가 slab 안* 검사 — NaN 회피.

## 4. 트래버설 + 방문 노드 trace

```cpp
inline RayTraversalResult BVH2D::Trace(const std::vector<AABB2D>& boxes,
                                          Vector2 ro, Vector2 rd,
                                          float tMax) const {
    RayTraversalResult r{};
    r.hitT = tMax;
    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        int idx = stack.back(); stack.pop_back();
        const BVHNode2D& n = nodes_[idx];
        float tn, tf;
        if (!RayAABB2D(ro, rd, n.bounds, tn, tf)) continue;
        if (tn > r.hitT) continue;          // 가지치기
        r.visitedNodes.push_back(idx);

        if (n.leftChild < 0) {
            // leaf — 객체별 정밀 검사
            for (int i = 0; i < n.objCount; ++i) {
                int objIdx = order_[n.firstObj + i];
                float oTn, oTf;
                if (RayAABB2D(ro, rd, boxes[objIdx], oTn, oTf)) {
                    float t = std::max(oTn, 0.0f);
                    if (t < r.hitT) {
                        r.hitT = t;
                        r.hit = true;
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

```cpp
void SpatialScene::Regenerate(int cx, int cy, int cw, int ch) {
    boxes_.clear();
    for (int i = 0; i < numObjects_; ++i) {
        float u  = nz::HashTo01(i, 0, seed_);
        float v  = nz::HashTo01(i, 1, seed_);
        float wf = nz::HashTo01(i, 2, seed_);
        float hf = nz::HashTo01(i, 3, seed_);
        float w  = 8 + wf * 30;
        float h  = 8 + hf * 30;
        float xMin = cx + 30 + u * (cw - 60 - w);
        float yMin = cy + 30 + v * (ch - 60 - h);
        boxes_.push_back({{xMin, yMin}, {xMin + w, yMin + h}});
    }
    bvh_.Build(boxes_, leafThreshold_);
}
```

Step 4 의 `nz::HashTo01` 재활용 — 시각화 lab 안의 *모듈 재사용*. 재생성은 *seed / N / leafThreshold / 캔버스 크기* 가 변할 때만.

## 6. 빌드 깊이 슬라이더로 애니메이션

`maxDepth_` 슬라이더로 *그릴 깊이* 제한 — *0 (루트만), 1 (루트 + 두 자식), …* 으로 트리가 자라는 모습.

```cpp
for (size_t i = 0; i < bvh_.Nodes().size(); ++i) {
    const BVHNode2D& n = bvh_.Nodes()[i];
    if (n.depth > maxDepth_) continue;
    // ... 그리기 ...
}
```

`maxDepth_` 가 자동으로 *현재 BVH 의 최대 깊이* 를 기억해 슬라이더 범위가 *재생성마다 갱신*. 사용자가 슬라이더를 끌면 *트리 빌드의 단계* 를 *실시간으로 되감기 / 재생*.

## 7. 방문 노드 / hit 객체 강조

```cpp
auto trav = bvh_.Trace(boxes_, ro, rdN, L);
std::vector<bool> visited(bvh_.NumNodes(), false);
for (int i : trav.visitedNodes) visited[i] = true;

for (size_t i = 0; i < bvh_.Nodes().size(); ++i) {
    Color c = visited[i] ? Color{120, 220, 255, 160}
                            : Color{120, 130, 150, 80};
    DrawRectangleLinesEx(...);
}

// 객체: hit 된 객체만 빨강
for (size_t i = 0; i < boxes_.size(); ++i) {
    Color c = ((int)i == trav.hitObjIdx)
                ? Color{255, 100, 100, 220}
                : Color{255, 200, 80, 140};
    DrawRectangleRec(...);
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

이 step 이 `tests/core_tests.cpp` 에 추가한 것은 `TestBvh()` — `RayAABB2D` 가 박스를 관통하는 광선에서 `tNear=1`·`tFar=3` 을 돌려주고 빗나가는 광선에서 거짓인지, 박스 3 개로 빌드한 `BVH2D` 의 `Trace` 가 *가장 가까운* 객체 (인덱스 0, t=1) 를 찾는지, 빈 입력으로 빌드하면 노드가 0 개인지.

```text
$ ctest --test-dir build -C Release --output-on-failure
    Start 1: math_core_tests
1/1 Test #1: math_core_tests ..................   Passed    0.04 sec
100% tests passed out of 1
```

---

## 결론

Step 6 에서 만든 것.

1. **`algo/bvh2d.h`** — `AABB2D`, `RayAABB2D` (NaN-safe), `BVHNode2D`, `BVH2D::Build` (median split), `BVH2D::Trace` (방문 노드 trace).
2. **`scenes/spatial_scene.{h,cpp}`** — 결정론적 객체 분포 (Hash2 재활용), 빌드 깊이 슬라이더, 광선 핸들 두 개, 방문 노드 강조 + only-visited 토글.
3. **재사용 패턴들** — *결정론적 hash 분포*, *방문 노드 trace 와 시각화 분리*, *깊이 슬라이더로 애니메이션* — 이후 Scene 들에서 활용 가능.

math 시리즈 part15 (공간 분할) + part16 (BVH) + part17 의 Slab Test 가 *광선을 끌면 BVH 가 즉시 가지치기되는 모습* 으로 살아 있다. *median split 의 균형*, *방문 노드 vs 전체 노드의 비율*, *leaf threshold 가 트리 깊이에 미치는 영향* 을 사용자가 *직접 본다*.

이 step 의 한계. 빌드 전략이 *median split 한 가지* 뿐이라 SAH 같은 다른 분할과 비교할 수단이 없고, 트래버설도 스택 순서를 광선 방향으로 정렬하지 않는다. 자동 검증은 *일렬로 놓인 박스 3 개 + 광선 1 개* 뿐이라 `leafThreshold` 를 무시해 트리를 단일 leaf 로 만들거나 `nth_element` 호출을 지워도 `ctest` 는 통과한다 — 무작위 박스 분포를 brute force 와 대조하는 검증이 없다. 화면에 표시되는 것도 *방문 노드 수* 이고, 가지치기가 절약한 *실제 시간* 은 재지 않는다.

## 더 읽을거리

- Christer Ericson, *Real-Time Collision Detection*, Ch.6-7 — BVH·공간 분할의 표준 교과서.
- Amy Williams 외, "An Efficient and Robust Ray-Box Intersection Algorithm" (2005) — slab test 의 NaN-safe 처리 원전.
- [PBRT: Bounding Volume Hierarchies](https://www.pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies) — median/SAH 빌드의 깊은 해설(무료 공개).
