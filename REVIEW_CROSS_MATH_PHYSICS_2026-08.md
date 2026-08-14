# 교차 대조 검수 보고서 — math ↔ physics (2026-08)

- 대상: `math/` 20편 + `physics/` 20편. **편별 검수를 마친 뒤**, 여러 문서가 같은 주제를 다룰 때
  서로 모순되는 서술을 하는 곳을 찾는 별도 패스다.
- 주제 5개: SAT / GJK·EPA / 충돌 프리미티브·광선 교차·broad phase / 적분기·동역학·제약 / 회전·관성 텐서
- 모순을 찾으면 **어느 쪽이 옳은지 판정**하고 근거를 남겼다.
- ~~**이 보고서는 지적만 한다. 본문은 수정하지 않았다.**~~
  이 선언은 **검수 시점(2026-08)** 기준이며 **더 이상 사실이 아니다**.
- **반영 완료 (2026-08-13)**: 아래 11건의 정정은 모두 본문(`math/`, `physics/`)에 적용되었다.
  검수 2026-08 / 반영 2026-08-13. 항목별 판정·실제 변경·남은 일은 문서 끝
  **[반영 상태 (2026-08-13)](#반영-상태-2026-08-13)** 절에 있다.

> 총 **11건**. 주제별로 SAT 1 · GJK/EPA 2 · 프리미티브 3 · 적분기 4 · 회전 1.
> 편별 검수로는 보이지 않던 것들이며, 그중 여럿은 **한 문서 안에서 두 정의가 갈리는** 형태다.

---

## SAT (분리축 정리) — 1건

### 1. math/part12_collision_sat.md ↔ physics/part06_narrow_phase_sat.md — 문서간모순 (확정)

**위치**: math: 255-257, 260 (§3.3), 사용처 357 (§4.3), 548-552 (§7.1) / physics: 174-188 (§3 'containment의 함정'), 190-204

```text
math/part12 L255-257: "float OverlapAmount(Projection a, Projection b) {\n    return std::min(a.max, b.max) - std::max(a.min, b.min);\n}"
math/part12 L260: "각 축의 겹침 양은 그 축 방향으로 두 도형을 분리하는 데 필요한 이동 거리의 후보이고, 가장 작은 양의 겹침을 주는 축이 최소 이동 벡터(MTV)의 방향을 정한다."
---
physics/part06 L176: "단순 교집합 길이는 다음과 같다." / L179: "o=\\min(a_1,b_1)-\\max(a_0,b_0)"
physics/part06 L182: "이 값은 한 구간이 다른 구간 안에 완전히 들어간 경우 실제로 밖으로 밀어낼 거리가 아니다. 예를 들어 $A=[-2,2]$, $B=[-1,1]$이면 교집합 길이는 2지만 B를 A 밖으로 옮기려면 3만큼 이동해야 한다."
physics/part06 L187: "d_+=a_1-b_0,\\qquad d_-=b_1-a_0"
```

겹침 깊이(MTV depth)의 정의가 두 문서에서 정면으로 충돌한다. math/part12는 `OverlapAmount = min(a.max,b.max) - max(a.min,b.min)` (교집합 길이)를 정의하고, 이어 L260에서 그것이 "두 도형을 분리하는 데 필요한 이동 거리의 후보"라고 명시한 뒤 §4.3 SAT2D의 MTV 산출(L357)에 그대로 쓴다. physics/part06 §3은 바로 그 식을 이름까지 붙여("단순 교집합 길이") containment의 함정으로 지목하고, 반례와 함께 대체 공식 d+=a1-b0, d-=b1-a0을 제시한다. 두 문서를 이어 읽는 독자는 같은 기호(겹침 양/depth)에 대해 서로 반대되는 지시를 받는다.

**판정**: physics/part06이 옳다. 직접 검증: 구간 A=[a0,a1], B=[b0,b1]이 겹칠 때 축 방향으로 B를 A 밖으로 밀어내는 거리는 +방향 d+=a1-b0, -방향 d-=b1-a0이고 필요 이동량은 min(d+,d-)다. 부분 겹침(예: a0<b0<a1<b1)에서는 min(d+,d-)=a1-b0 = 교집합 길이라 두 식이 일치하지만, 한 구간이 다른 구간에 완전히 포함되면 갈라진다. A=[-2,2], B=[-1,1]: 교집합 길이 = min(2,1)-max(-2,-1) = 1-(-1) = 2. 반면 d+=2-(-1)=3, d-=1-(-2)=3 → 실제 필요 이동 3. physics의 수치가 맞다.

결정적 근거는 math/part12 자체 안에 있다. 같은 문서 §5.1(L427)과 §7.2(L569-571)의 OBB 경로는 `overlap = halfA + halfB - dist` (dist=|Δc·n|)를 쓰는데, 중심 cA,cB / 반폭 hA,hB인 구간에 대해 hA+hB-|cB-cA| = min(d+,d-)로 physics의 공식과 정확히 같다(위 예에서 2+1-0 = 3). 즉 math/part12의 3D 경로는 옳은 깊이를, §3.3/§4.3의 일반 다각형 경로는 교집합 길이를 쓰고 있어 한 문서 안에서도 두 정의가 갈린다. 잘못된 쪽은 §3.3의 `OverlapAmount`와 그것을 "분리에 필요한 이동 거리"라고 단정한 L260이다.

실패 재현: 큰 정사각형 A=[-2,2]², 작은 정사각형 B=[-1,1]². 네 후보 축 모두에서 pa=[-2,2], pb=[-1,1] → OverlapAmount=2 → MTV depth=2. §7.4 ResolvePenetration으로 2만큼 분리해도 실제로는 3이 필요하므로 두 도형은 여전히 겹친 채 남는다. 이는 physics/part06 L455가 요구하는 검증 성질 "MTV 검증: B를 `normal * (depth + margin)`만큼 이동하면 분리되어야 한다"를 위반한다.

(참고: math/part12 전체를 검색했으나 containment·포함 구간에 대한 예외 서술은 없다. "포함"은 L724의 Minkowski 차이 원점 포함 판정 한 곳뿐이다. 따라서 '축약으로 인한 생략'이 아니라 실제로 어긋난 서술이다.)

**정정**: math/part12 §3.3을 physics/part06과 같은 양방향 분리 거리 기준으로 고쳐야 한다.

```cpp
// 축 방향으로 b를 a 밖으로 밀어내는 최소 거리 (containment 포함)
float OverlapAmount(Projection a, Projection b) {
    float pushPositive = a.max - b.min;  // b를 +axis로
    float pushNegative = b.max - a.min;  // b를 -axis로
    return std::min(pushPositive, pushNegative);
}
```

그리고 §4.3에서 MTV 법선 부호는 어느 쪽이 작은지에 따라 결정한다(pushPositive가 작으면 +axis, 아니면 -axis). L260의 문장도 "교집합 길이"가 아니라 "두 방향 분리 거리 중 작은 쪽"이 이동 거리 후보임을 명시해야 한다. 부분 겹침에서는 값이 기존과 동일하므로 §4.5 등 기존 서술은 영향받지 않고, 한 도형이 다른 도형에 파묻힌 경우(플랫포머에서 캐릭터가 지형에 깊이 박히는 흔한 상황)만 결과가 교정된다.

> **상태 (2026-08-13): 반영됨 — 잔여 1** · `math/part12_collision_sat.md`
> §3.3 `OverlapAmount` 를 `std::min(pushPositive, pushNegative)` 로 교체(주석 "축 방향으로 b를 a 밖으로 밀어내는 최소 거리 (containment 포함)" 포함), L260 문장을 "두 방향 분리 거리 중 작은 쪽"으로 고치고 $A=[-2,2]$, $B=[-1,1]$ 반례(교집합 2 vs 실제 3)를 본문에 넣었다.
> **잔여**: 정정안이 함께 요구한 "§4.3에서 축별 `pushPositive`/`pushNegative` 비교로 MTV 법선 부호를 정한다"는 **적용되지 않았다**. §4.3은 여전히 `OverlapAmount` 의 *크기*만 보고, 부호는 §4.4의 `Centroid(b) - Centroid(a)` 보정에 맡긴다. 두 방식은 투영 구간의 중점과 도형 무게중심의 투영이 일치하는 대칭 도형에서는 같은 답을 주고 비대칭 도형에서 갈릴 수 있다(깊이 값 자체는 이제 양쪽 다 옳다).

> **[2026-08-13 후속 처리] (2026-08-14): 잔여도 닫혔다.** §4.3 을 축별 부호 결정으로 바꾸고, §4.4 의 무게중심 보정이 근사임을 반례와 함께 명시했다. 상세는 문서 끝 [남은 일](#남은-일-다음에-이-저장소를-여는-사람에게) 절.

---

## GJK / EPA — 2건

### 1. math/part13_collision_gjk_epa.md (오류) / physics/part07_narrow_phase_gjk.md (정확) — 산술오류 (확정)

**위치**: math L482, L486 / physics L179-L183

```text
math L482-483: "//   진행도  = |v| + dot(newPt, dHat)        // 상한 - 하한" "// 진행도 < epsilon 이면 종료, |v| 가 두 도형 사이 최단 거리."  |  math L486: "원점이 Minkowski 차에 *닿거나 안에* 있으면 진행도가 음수에 도달해 거리 0 (교차) 으로 분기."  |  physics L179-183: "    if (step.containsOrigin)\n        return FinishIntersecting(simplex, iteration);\n    if (Converged(step))\n        return FinishSeparatedOrTouching(simplex, iteration);\n    direction = -step.closestPoint;"
```

L482의 진행도 식(수정 후 부호는 옳음)은 정의상 절대 음수가 될 수 없는데, 바로 다음 줄 L486이 "진행도가 음수에 도달해 거리 0(교차)으로 분기"한다고 서술한다. 즉 존재할 수 없는 분기를 거리 GJK의 교차 판정 경로로 제시한다. 같은 거리 루프에서 physics/part07은 교차를 `step.containsOrigin`(심플렉스가 원점을 포함)으로 분기시켜 이 문제가 없다.

**판정**: physics 쪽이 옳다. 증명: v는 현재 심플렉스 conv(W) ⊆ C(=A⊖B)의 원점 최근접점이므로 v ∈ C이고, dHat = -v/|v|일 때 h := dot(newPt, dHat) = max_{x∈C} dHat·x ≥ dHat·v = (-v/|v|)·v = -|v|. 따라서 진행도 = |v| + h ≥ 0 이 항상 성립한다(듀얼리티 갭이므로 정확 연산에서 음수 불가). 특히 원점이 C 내부/경계에 있으면 h ≥ dHat·0 = 0 이라 진행도 ≥ |v| ≥ 0 으로 오히려 더 확실히 비음수다. 게다가 원점이 내부 깊숙이 있으면 h가 0에서 떨어진 양수로 유지되어 진행도 < epsilon 종료조차 걸리지 않는다 — 실제로는 심플렉스가 원점을 품어 v = 0 이 되는 것으로만 분기가 일어난다. L482 식 자체는 (dHat이 단위·원점 방향이므로 상한 |v| − 하한 −dot(newPt,dHat) = |v| + dot(newPt,dHat)) 정확하다. 틀린 것은 L486의 부호 주장뿐이다.

**정정**: L486을 예: "진행도는 항상 0 이상이다. 원점이 Minkowski 차 안에 있으면 진행도가 작아지는 대신 심플렉스가 원점을 포함하게 되어 v = 0 이 되고, 이때 거리 0(교차)으로 분기해 EPA로 넘긴다"로 고친다(physics/part07의 `step.containsOrigin` 분기와 같은 형태). 원점이 경계에 정확히 닿은 touching만이 |v| → 0, dot(newPt, dHat) → 0 으로 진행도가 0에 수렴하는 경우다.

> **상태 (2026-08-13): 반영됨** · `math/part13_collision_gjk_epa.md`
> L486의 "진행도가 음수에 도달해" 서술을 삭제하고 "진행도는 상한 − 하한이므로 *항상 0 이상* 이다 … 심플렉스가 *원점을 포함* 하게 되어 $v = 0$ … EPA 로 넘긴다 … 경계에 정확히 *닿은* touching 만이 진행도가 0 에 수렴" 으로 교체. 정정안 그대로.

### 2. math/part13_collision_gjk_epa.md / physics/part07_narrow_phase_gjk.md — 문서간모순 (높음)

**위치**: math L676, L418 (관련 정책 L241, L424) / physics L139, L166, L170

```text
math L674-677: "    if (length_squared(newPt - existing) < epsilon) {\n        // 같은 점 — 진전 없음, 종료\n        return false;\n    }"  |  math L418: "    return false;  // 안전 fallback"  |  physics L139: "`DuplicateSupport`, `DegenerateSimplex`, `IterationLimit`은 모두 “분리”가 아니다. 현재 best distance와 simplex는 유용할 수 있지만 신뢰 수준을 함께 전달해야 한다."  |  physics L166: "duplicate에서 현재 closest point가 contact slop 안이면 Touching, 충분히 멀면 Separated, 애매하면 별도 실패 상태로 남긴다."  |  physics L170: "반복 상한은 무한 루프 방어다. 상한 도달을 정상적인 `Intersecting`으로 바꾸지 않는다."
```

같은 두 실패 케이스(지원점 중복, 반복 상한 도달)에 대해 두 글이 정반대 결론을 낸다. math는 두 곳 모두 `return false`(= 분리·비충돌)로 처리하고, physics는 이 둘을 "분리가 아니다"라고 명시하며 별도 상태로 보고해야 한다고 못박는다. 두 글을 이어 읽는 독자는 수치 실패를 분리로 접어도 되는지에 대해 상반된 지침을 받는다.

**판정**: physics가 옳다. 정확 연산에서 '완전 중복' 지원점이면 실제로 분리가 증명된다(v가 conv(W)의 원점 최근접점이면 임의 y ∈ conv(W)에 대해 v·y ≥ |v|², 따라서 w ∈ W인 중복점에 대해 h_C(-v/|v|) = -v·w/|v| ≤ -|v| < 0 → 거리 ≥ |v| > 0). 그러나 math의 코드는 완전 일치가 아니라 tolerance 비교(`length_squared(newPt - existing) < epsilon`)를 쓰며, §7.1의 전제 자체가 "부동소수점 오차로 *거의 같은 지원점*"(L665)이다. 근사 중복에는 위 증명이 적용되지 않아 touching이나 epsilon 이하 얕은 침투에서도 걸리고, 그 경우 `return false`는 충돌 누락(터널링·유령 통과)이 된다. 반복 상한 도달은 아예 아무 정보도 주지 않으므로 분리로 단정할 근거가 없다. 더구나 math 자신이 L241·L424에서 "이 글의 코드는 *touch = collision*" 정책이라고 선언했는데, L676의 근사 중복 처리는 바로 그 touching 구간을 분리로 되돌린다 — 문서 내부와도 어긋난다.

**정정**: math §7.1의 중복 검사와 §4.4의 반복 상한 탈출을 '분리'와 구분해야 한다. 최소한 두 곳에 "이 반환은 '분리 확정'이 아니라 '판정 실패'다 — 산업 구현은 별도 상태(DuplicateSupport / IterationLimit)로 보고하고, 현재 최근접 거리가 contact slop 안이면 touching으로 처리한다"는 단서를 달거나, 완전 중복(정확 일치)일 때만 분리로 단정한다고 조건을 좁힌다.

> **상태 (2026-08-13): 반영됨** · `math/part13_collision_gjk_epa.md` — 지적한 두 곳 모두.
> §4.4: `return false;  // 안전 fallback` → `// 안전 fallback — '분리 확정' 이 아니라 '판정 실패'`, 그리고 본문에 "상한 도달은 분리에 대한 아무 정보도 주지 않으므로, 산업 구현은 이 경로를 `false` 로 접지 않고 별도 상태 (`IterationLimit`) 로 보고한다 (7.1절)" 추가.
> §7.1: 중복 검사 코드 뒤에 문단 신설 — 완전 중복이면 분리가 증명되지만 tolerance 비교라 touching·얕은 침투에서도 걸린다는 점, `DuplicateSupport` 별도 상태와 contact slop 처리, 4.4절 반복 상한도 같다는 점까지.

---

## 충돌 프리미티브 · 광선 교차 · broad phase — 3건

### 1. math/part11_collision_primitives.md ↔ physics/part05_broad_phase.md — 문서간모순 (확정)

**위치**: part11: 302 (근거 코드 310~321) / part05: 79

```text
part11 L302: "AABB-AABB 검사는 *6번의 비교 + 분기*. SIMD로 묶으면 *한 명령으로*."  ↔  part05 L79: "AABB 교차는 비교 수가 작지만 실제 비용은 데이터 로드, 분기 예측과 후보 출력에 좌우된다. SoA 배치에서는 SSE/AVX2 레지스터로 각각 4개/8개의 float lane을 비교할 수 있다. 한 페어 전체가 한 명령에 끝난다는 뜻은 아니며 각 축의 여러 비교, mask 결합과 결과 기록이 필요하다."
```

같은 대상(AABB-AABB 겹침 검사의 SIMD화)을 두고 part11은 "한 명령으로" 줄어든다고 단언하고, part05는 "한 페어 전체가 한 명령에 끝난다는 뜻은 아니"라고 명시적으로 부정한다. 두 문서를 이어 읽는 독자는 SIMD로 페어당 1명령이 되는지 아닌지 정반대의 결론을 얻는다.

**판정**: part05가 옳다. part11 자신이 바로 아래(L310~321)에 실은 SSE 코드가 반증이다. 로드 4회(_mm_loadu_ps ×4) 뒤에도 비교가 _mm_cmplt_ps(amax,bmin) 1회 + _mm_cmpgt_ps(amin,bmax) 1회로 **2회**, 여기에 _mm_or_ps로 mask 결합 1회, _mm_movemask_ps + 마스킹/비교로 결과 기록이 더 붙는다. 즉 스칼라 6비교가 1명령이 아니라 '비교 2명령 + mask 결합 + 추출'로 접히는 것이다. 게다가 3축을 4-lane 중 3lane에 태우므로 lane 하나는 버려지고(L320 `& 7`), part05가 말한 "각 축의 여러 비교, mask 결합과 결과 기록"이 그대로 필요하다.

**정정**: part11 L302를 코드와 맞춰 고쳐야 한다. 예: "AABB-AABB 검사는 *6번의 비교 + 분기*. SIMD로 묶으면 축별 비교가 lane 병렬로 접혀 *비교 2명령 + mask 결합 + 부호 추출* 로 줄고, 분기도 사라진다." (또는 part05 L79과 같은 톤으로 '한 명령'이라는 표현 자체를 제거)

> **상태 (2026-08-13): 반영됨** · `math/part11_collision_primitives.md`
> L302를 정정안의 제안 문장 그대로 교체. "SIMD로 묶으면 *한 명령으로*" 표현은 사라졌고 part05 L79와 충돌하지 않는다.

### 2. math/part17_raycasting.md ↔ math/part16_bvh.md — 사실오류 (확정)

**위치**: part17 참조: 664, 709, 739 / part17 자신의 8.2·8.3·8.5: 757, 769, 797 / 실제 의도 대상 part16: 704, 744, 771

```text
part17 L664: "BVH 트래버설을 *광선 트래버설 코드* 로 (8.2절 참조)." / L709: "8.3절. 가까운 자식부터 push 해서 *가까운 hit 빨리 찾기*." / L739: "8.5절. RT Core 가 BVH 트래버설 + 삼각형 교차를 *전용 하드웨어* 로."  ↔  part17 자신의 절 제목 L757 "### 8.2 epsilon 시작 offset", L769 "### 8.3 광선 시작점 보정", L797 "### 8.5 부호 일관성"  ↔  part16 절 제목 L704 "### 8.2 BVH 광선 트래버설", L744 "### 8.3 가까운 자식 우선", L771 "### 8.5 RTX 하드웨어 BVH"
```

part17 7장의 세 상호참조가 문서 이름 없이 "8.2절 / 8.3절 / 8.5절"로만 적혀 있는데, part17에는 실제로 8.2·8.3·8.5절이 모두 존재하며 내용이 전혀 다르다(epsilon offset, 광선 시작점 보정, 부호 일관성). 특히 L709의 "8.3절. 가까운 자식부터 push"는 part17 §8.3(광선 시작점 보정)과 무관하고, L739의 "8.5절. RT Core"는 part17 §8.5(부호 일관성)와 무관하다. 이 시리즈는 part16 §3.5 "분할 위치 — SAH (4절)"처럼 벌거벗은 'N절'을 자기 문서 내부 참조로 쓰고 있어, 독자는 반드시 part17 내부를 찾아보게 된다.

**판정**: part16 쪽이 실제 대상이다. 세 참조의 내용이 part16 §8.2(BVH 광선 트래버설, 스택 기반 TraceRay), §8.3(가까운 자식 우선, tL_near/tR_near 비교 후 push), §8.5(RTX 하드웨어 BVH)와 정확히 1:1로 일치한다. 반면 part17 §8.2/8.3/8.5는 전부 수치 안정성 절이라 어느 것도 대응되지 않는다. 즉 참조 번호가 아니라 '어느 문서인지'가 빠진 것이다.

**정정**: 세 곳 모두 문서를 명시해야 한다. L664 "(BVH 편 8.2절 참조)", L709 "BVH 편 8.3절.", L739 "BVH 편 8.5절." — 그대로 두면 part17 안에서 전혀 다른 절을 가리킨다.

> **상태 (2026-08-13): 반영됨** · `math/part17_raycasting.md` — 세 곳 모두.
> §7.2 "(BVH 편 8.2절 참조)", §7.3 "BVH 편 8.3절.", §7.5 "BVH 편 8.5절." 로 문서명이 붙었다. 정정안 그대로.

### 3. physics/part05_broad_phase.md (§3.3 ↔ §6.1, 같은 문서 두 절) — 규약불일치 (높음)

**위치**: 244 / 493, 497

```text
L244: "객체가 *조금 이동* 하면 *작은 수의 swap* 만. 평균 $O(N + k)$ — k 는 활성 쌍 수."  ↔  L493(표 SAP 행): "축 정렬성이 좋으면 $O(N + K)$ 페어 추출"  ↔  L497: "여기서 $K$ 는 broad phase 가 만든 후보 페어 수, $k$ 는 단일 쿼리의 반환 후보 수."
```

같은 글 안에서 소문자 $k$ 가 두 가지 다른 양을 가리킨다. §3.3은 $k$ = '활성 쌍 수'(= 페어 수)로 쓰고, §6.1은 $k$ = '단일 쿼리의 반환 후보 수'로 정의하면서 페어 수는 대문자 $K$ 로 따로 둔다. 6.1 표의 SAP 행이 같은 SAP 비용을 $O(N + K)$ 로 적고 있어, 앞뒤로 읽으면 §3.3의 $O(N+k)$ 와 표의 $O(N+K)$ 가 같은 식인지 다른 식인지 판별할 수 없다.

**판정**: §6.1(L497)의 정의가 이 글의 기준이다. 기호를 명시적으로 도입·구분하는 유일한 지점이고, 같은 절의 표(L493~495)가 그 구분을 실제로 지켜 SAP는 $K$(페어 수), Spatial Hash·DBVT는 $k$(단일 쿼리 반환 수)를 쓰고 있다. 따라서 어긋난 쪽은 §3.3의 소문자 $k$ 다. SAP의 sweep 비용에 등장하는 양은 '한 쿼리의 반환 수'가 아니라 '추출된 겹침 쌍 수'이므로 $K$ 가 맞다.

**정정**: L244를 "평균 $O(N + K)$ — $K$ 는 추출된 겹침 쌍 수" 로 고쳐 §6.1의 $K$/$k$ 규약에 맞춘다. (덧붙여 L244가 §3.3 '매 step 갱신' 절 안에 있어 표의 '갱신 = 이동량만큼 swap' 과 층위가 달라 보이므로, '한 step 전체(swap 갱신 + sweep 페어 추출) 비용' 임을 한마디 명시하면 표와의 대응이 분명해진다.)

> **상태 (2026-08-13): 반영됨** · `physics/part05_broad_phase.md`
> L244 → "한 step 전체 (swap 갱신 + sweep 페어 추출) 비용이 평균 $O(N + K)$ — $K$ 는 추출된 겹침 쌍 수 (6.1절의 기호 규약)". 기호 교정과 괄호 안 층위 명시 제안까지 함께 반영.

---

## 적분기 · 동역학 · 제약 해법 — 4건

### 1. physics/part12_pbd.md (§1.5) ↔ physics/part12_pbd.md (§2.1·§2.4) / physics/part02_integrators.md (§3.2) — 사실오류 (확정)

**위치**: part12: 81 ↔ 94–99, 119 · part02: 203

```text
part12:81 — "PBD 가 *Position Verlet 변형*. 속도 저장 안 함 / 위치만 추적이라 *Verlet 의 자연스러운 확장*."
part12:96 — "    vec3  velocity;"  (struct Particle 의 영구 필드)
part12:105 — "        p.velocity += gravity * dt;                 // gravity 는 가속도"
part12:119 — "        p.velocity = (p.predicted - p.position) / dt;"
part02:203 — "*속도 저장 안 함* 의 장점이 *Position-Based Dynamics (PBD)* 의 토대 — *위치 자체가 상태*, 제약 (constraint) 으로 직접 위치 조작 가능."
```

part12 §1.5(그리고 part02 §3.2가 깔아 놓은 전제)는 PBD가 속도를 저장하지 않고 위치만 추적한다고 서술한다. 그러나 같은 문서 13줄 뒤의 PBD_Step 코드는 `vec3 velocity` 를 struct Particle 의 영구 상태로 두고, predict 단계에서 그 속도를 읽어 쓰고(105행), correct 단계에서 다시 써 넣어 다음 step 으로 넘긴다(119행). part12 의 들어가며(5행)와 결론(601행)도 "속도를 재구성한다"고 명시한다. 독자가 part02 §3.2 → part12 §1.5 → part12 §2.1 순으로 읽으면 "속도 없음"이라는 전제로 들어와 속도 필드가 있는 코드를 만난다.

**판정**: 코드 쪽(§2.1·§2.4)이 옳다. Müller, Heidelberger, Hennix, Ratcliff (2007) 의 PBD Algorithm 1 은 x_i 와 함께 v_i 를 명시적 상태로 초기화하고 (1) v ← v + Δt·w·f_ext, (2) p ← x + Δt·v, (3) 투영, (4) v ← (p − x)/Δt, x ← p 순으로 돈다. 속도는 predict 단계·damping·마찰·반발 처리에 모두 필요하므로 PBD 는 반드시 속도를 상태로 갖는다. "속도를 저장하지 않는다"는 것은 part02 §3.2 가 정확히 서술한 **position Verlet** 의 성질이며(두 이전 위치만 보관), PBD 는 그 성질을 물려받지 않고 "위치를 직접 투영한다"는 성질만 물려받았다. 즉 계보(位置 투영) 주장은 맞고 속도 저장 주장만 틀렸다.

**정정**: part12:81 을 "PBD 는 위치를 직접 투영한다는 점에서 position Verlet 의 확장이다. 다만 position Verlet 과 달리 속도를 상태로 보관하며, 매 step 끝에서 (predicted − position)/dt 로 재구성한다." 로 고친다. part02:203 도 "속도 저장 안 함의 장점"이 아니라 "위치 자체를 상태로 두고 제약으로 직접 조작한다는 발상"이 PBD 의 토대라고 좁혀 쓴다.

> **상태 (2026-08-13): 반영됨** · `physics/part12_pbd.md` + `physics/part02_integrators.md` — 두 문서 모두.
> part12 §1.5 → "PBD 는 *위치를 직접 투영* 한다는 점에서 *position Verlet 의 확장*. 다만 position Verlet 과 달리 *속도를 상태로 보관* 하며, 매 step 끝에서 `(predicted - position) / dt` 로 재구성한다."
> part02 §3.2 → "*위치 자체를 상태로 두고 제약 (constraint) 으로 직접 조작* 한다는 발상이 … PBD 의 토대. 단 PBD 는 이 발상만 물려받았고 속도는 상태로 보관한다 (position Verlet 의 '속도 저장 안 함' 은 물려받지 않는다)." 둘 다 정정안 그대로.

### 2. physics/part02_integrators.md (§2.2) ↔ physics/part02_integrators.md (§2.3 표 · §6.2 재현표) — 산술오류 (확정)

**위치**: 131 ↔ 140, 404–409, 414

```text
part02:131 — "E_{n+1} \approx E_n + O(\Delta t^3)"
part02:140 — "| Local error | $O(\Delta t^2)$ | $O(\Delta t^2)$ |"
part02:408 — "| 500 | 72.386386 | 0.511911 | 0.499927 | 0.499997 |"
part02:414 — "- **Semi-implicit**: 한 값에 고정되지 않고 진짜 에너지 주변에서 제한된 진동을 보인다."
```

§2.2 는 semi-implicit Euler 의 한 step 에너지 변화를 $O(\Delta t^3)$ 이라고 쓴다. 직접 유도하면 한 차수 낮다. $m=k=1$, $v'=v-hx$, $x'=x+hv'$ 에 대해
E' − E = ½h²(v² − x²) − h³vx + ½h⁴x²
이므로 선행항은 $O(h^2)$ 다. 수치 확인: h=0.1, (x,v)=(1,0) 에서 E₀=0.5 → v₁=−0.1, x₁=0.99 → E₁=0.49505, 즉 ΔE=−0.00495 ≈ ½h²=0.005 이고 h³=0.001 보다 5배 크다. 이는 §2.3 표가 semi-implicit 의 local error 를 $O(\Delta t^2)$ 로 옳게 적은 것과도 어긋난다(1차 방법의 상태 국소오차가 $O(h^2)$인데 그 2차형 함수인 에너지의 한 step 변화만 $O(h^3)$ 일 수 없다).

**판정**: §2.3 표와 §6.2 데이터가 옳고 §2.2 의 지수가 틀렸다. §6.1 의 코드를 그대로 double, h=0.1, 1000 step 으로 돌리면 §6.2 표(0.477815 / 0.511911 / 0.521321)가 소수 6자리까지 정확히 재현되며, 전 구간 에너지 범위는 E ∈ [0.476190, 0.526316] = 0.5/(1 ± h/2) 이다. 즉 장기 편차 진폭은 상대 ±5.3% = $O(h)$ 이고 한 step 변화는 $O(h^2)$ 이며, $O(h^3)$ 은 어느 쪽도 아니다. 부호가 (v²−x²) 를 따라 주기적으로 바뀌기 때문에 누적되지 않고 bounded 로 남는다 — §2.2 의 결론 문장(134행) 자체는 옳다.

**정정**: 131행을 $E_{n+1} = E_n + \tfrac{1}{2}\Delta t^2 (v_n^2 - k x_n^2) + O(\Delta t^3)$ 로 바꾸고, 아래에 "부호가 주기적으로 바뀌어 누적되지 않으며, 장기 에너지 편차는 $O(\Delta t)$ 범위(h=0.1 에서 상대 ±5%)에 갇힌다" 를 덧붙인다. 이렇게 하면 §6.2 표의 0.4778~0.5213 진폭과 정확히 맞물린다.

> **상태 (2026-08-13): 반영됨 (정정안보다 개선된 형태)** · `physics/part02_integrators.md`
> 131행 → $E_{n+1} = E_n + \tfrac{1}{2} k \Delta t^2 (\vec{v}_n^2 - k \vec{x}_n^2) + O(\Delta t^3)$, 134행에 "1.4절의 explicit Euler 와 선행항의 차수는 같고 *부호* 만 다르다 … 장기 에너지 편차는 $O(\Delta t)$ 범위 ($\Delta t = 0.1$ 에서 상대 약 $\pm 5\%$) 에 갇힌다" 추가.
> 적용된 식은 정정안의 $\tfrac12\Delta t^2(v_n^2 - kx_n^2)$ 과 달리 앞에 $k$ 가 붙어 있다. 재유도 결과 **적용된 쪽이 옳다**: $m\ddot x = -kx$ 에 semi-implicit 을 적용하면 $E'-E = \tfrac12 k h^2\!\left(v^2 - \tfrac{k}{m}x^2\right) + O(h^3)$ 이고, 이 절의 설정 $m=k=1$ 에서는 두 표기의 값이 같다. 게다가 §1.4의 explicit Euler 식 $\tfrac12 k\Delta t^2(v_n^2 + k x_n^2)$ 과 형태가 나란해져 새로 붙인 "부호만 다르다" 문장이 성립한다.

### 3. physics/part02_integrators.md (§6.2 Verlet 주석) ↔ physics/part02_integrators.md (§6.1 재현 코드) — 산술오류 (높음)

**위치**: 415 ↔ 345–400, 404–409

```text
part02:415 — "- **Velocity Verlet**: 이 조화진동자와 step 크기에서는 에너지 오차가 약 $10^{-4}$ 규모로 제한된다. 다른 힘과 더 큰 step에도 같은 정확도가 보장되는 것은 아니다."
part02:414 — "- **Semi-implicit**: … 표의 관측 시점마다 위상이 달라 값도 다르다."
part02:400 — "`double`, $h=0.1$초, 1000 step으로 실행한다. 표는 위 코드를 각 `StepFn`에 대해 실행한 값을 반올림한 것이다."
```

"제한된다"는 구간 전체의 상계 주장인데, §6.1 코드를 명시된 조건(double, h=0.1, 1000 step) 그대로 실행하면 velocity Verlet 의 에너지 범위는 E ∈ [0.498750, 0.500000] 이다. 즉 최대 오차는 $1.25\times10^{-3}$ 으로, 주장된 $10^{-4}$ 보다 한 자릿수 크다. 표의 샘플 지점(step 0/100/500/1000)에서만 3.8e-4 / 7.3e-5 / 2.8e-4 로 보이는 것인데, 이는 바로 한 줄 위(414행)에서 semi-implicit 에 대해 저자 스스로 경고한 "표의 관측 시점마다 위상이 달라 값도 다르다" 는 함정에 Verlet 주석이 그대로 빠진 경우다.

**판정**: 코드 실행값이 옳고 주석의 자릿수가 틀렸다. 해석적으로도 확인된다: 조화진동자에 대한 velocity Verlet 의 수정 에너지는 $\tfrac12 v^2 + \tfrac12 x^2(1-h^2/4)$ 이므로 E 는 $E_0$ 과 $E_0(1-h^2/4)$ 사이를 오간다. h=0.1 에서 $E_0 h^2/4 = 0.5\times0.0025 = 1.25\times10^{-3}$ 이고, 실제 최솟값 0.498750 이 $0.5(1-h^2/4)$ 와 정확히 일치한다.

**정정**: 415행을 "이 조화진동자와 step 크기에서는 에너지 오차가 약 $10^{-3}$ 규모($E_0 h^2/4 = 1.25\times10^{-3}$)로 제한된다. 표의 값이 더 작아 보이는 것은 관측 시점의 위상 탓이다." 로 고친다.

> **상태 (2026-08-13): 반영됨** · `physics/part02_integrators.md`
> 415행 → "$10^{-3}$ 규모 ($E_0 h^2 / 4 = 1.25 \times 10^{-3}$) 로 제한된다. 표의 값이 더 작아 보이는 것은 관측 시점의 위상 탓이다." 뒤의 "다른 힘과 더 큰 step에도 같은 정확도가 보장되는 것은 아니다" 는 유지. 정정안 그대로.

### 4. physics/part11_sequential_impulse.md (§1.1·§1.4·§4.2) ↔ physics/part12_pbd.md (§6.2·§6.4) — 규약불일치 (높음)

**위치**: part11: 45, 48, 85–88, 318–320 · part12: 426, 429, 470–477

```text
part11:45 — "K \vec{\lambda} = -J \vec{v} - \vec{b}"
part11:85–88 — "    impulse_acc = std::max(0.0f, impulse_acc + lambdaDelta);" … "    ApplyImpulse(n * lambdaDelta);"
part11:319 — "    a->linearVelocity  -= P * a->invMass;"
part12:426 — "M (\vec{x}^{n+1} - \tilde{\vec{x}}) = J^T \lambda"
part12:474–476 — "        lambda_acc += dLambda;" … "        a.predicted += correction * a.invMass;"
part12:578 — "두 solver의 sub-step 수가 다르면 contact impulse와 XPBD multiplier를 어느 시간 구간에 누적하는지 정의해야 한다."
```

두 문서 모두 제약 승수를 λ 로 쓰고 "누적 λ"(impulse_acc / lambda_acc)로 부르며, Δλ 식의 모양도 거의 같다(part11: effMass·(−Cdot−bias), part12: (−C−α̃λ)/(Σwᵢ|∇ᵢC|²+α̃)). 그러나 차원이 Δt 만큼 다르다. part11 은 λ·invMass 가 **속도** 변화이므로(319행) [λ] = kg·m/s = N·s(임펄스)이고, part12 는 λ·invMass 가 **위치** 변화이므로(476행) [λ] = kg·m = N·s² 다. 어느 문서도 이 Δt 차이를 명시하지 않는다.

**판정**: 둘 다 각자의 원전 관례 안에서 옳다 — 관례가 다름. Catto 의 SI 계열에서 λ 는 한 step 임펄스이고, Macklin·Müller·Chentanez 의 XPBD(2016) 식 (17)–(18) 은 $\Delta t^2$ 를 λ 에 흡수시켜 $M(x^{n+1}-\tilde x)=\nabla C^T\lambda$, $C+\tilde\alpha\lambda=0$, $\tilde\alpha=\alpha/\Delta t^2$ 로 쓴다. part12 §6.2 는 이 논문 식을 정확히 옮긴 것이므로 수정 대상은 식이 아니라 누락된 환산 주석이다. 다만 혼동 위험이 추상적이지 않다: part12 §8.2(578행)가 독자에게 "contact impulse 와 XPBD multiplier 를 어느 시간 구간에 누적할지 정의하라"고 지시하면서 두 양이 Δt 배 차이라는 사실은 말하지 않아, 1:1 로 맞추려는 오해를 유발한다.

**정정**: part12 §6.2 의 431행 뒤("여기서 $\tilde{\alpha} = \alpha/\Delta t^2$" 문장 옆)에 한 줄 추가: "여기서 $\lambda$ 는 $\Delta t^2$ 를 흡수한 값이라 $[\lambda]=\text{kg}\cdot\text{m}$ 이며, 11편 sequential impulse 의 임펄스 $\lambda$(단위 N·s)와 다르다. 임펄스로 환산하려면 $\lambda/\Delta t$ 를 쓴다." 그러면 §8.2 의 결합 계약 문장이 실행 가능해진다.

> **상태 (2026-08-13): 반영됨** · `physics/part12_pbd.md`
> §6.2 의 "$\tilde{\alpha} = \alpha / \Delta t^2$" 문장 바로 뒤에 문단 신설 — "이 $\lambda$ 는 $\Delta t^2$ 를 흡수한 값이라 $[\lambda] = \text{kg} \cdot \text{m}$ 이며, 11편 sequential impulse 의 임펄스 $\lambda$ (단위 $\text{N} \cdot \text{s}$) 와 다르다. 임펄스로 환산하려면 $\lambda / \Delta t$ 를 쓴다." 정정안 그대로. 판정대로 **식은 손대지 않았다**(양쪽 다 각자의 원전 관례 안에서 옳으므로).

---

## 회전 표현 · 관성 텐서 — 1건

### 1. physics/part04_rotational_dynamics.md ↔ physics/part19_ragdoll.md — 규약불일치 (높음)

**위치**: physics/part04: 69–80 (1.3절) / physics/part19: 176–178 (2.4절)

```text
physics/part04 L69–72: "대각 성분 $I_{xx}, I_{yy}, I_{zz}$ = *각 축에 대한 관성 모멘트*.\n비대각 성분 $I_{xy}, I_{xz}, I_{yz}$ = *축 사이 결합항*. 관례적으로 product of inertia 를\n$P_{xy} = \\int \\rho xy \\, dV$ 로 두면 관성 텐서의 비대각 원소는 $I_{xy} = -P_{xy}$ 다.\n즉 아래 행렬의 $I_{xy}$ 는 이미 부호가 포함된 *텐서 원소* 다." — 이어지는 행렬에서 $I_{xy}$ 는 (1,2)/(2,1) 위치.

physics/part19 L176–178: "    float Iz  = 0.5f * mass * r2;                  // long axis\n    float Ixy = mass * (3 * r2 + L * L) / 12.0f;   // 횡축 (실린더 공식)\n    return mat3{Ixy, 0, 0,  0, Ixy, 0,  0, 0, Iz};"
```

같은 물리 시리즈 안에서 기호 `I_xy` 가 정반대 의미로 쓰인다. physics/part04 는 1.3절에서 일부러 한 문단을 할애해 $I_{xy}$ 를 *관성 텐서의 비대각 원소*(부호가 포함된 $-P_{xy}$)로 못박는다. 그런데 physics/part19 2.4절 코드의 `Ixy` 는 비대각 원소가 아니라 회전체의 *횡축 대각 성분*($I_{xx}=I_{yy}$)이며, 반환 행렬에서 [0][0] 과 [1][1] 즉 대각 자리에 들어가고 정작 비대각은 전부 0 이다. part04 의 정의를 그대로 들고 part19 코드를 읽으면 "비대각 원소가 대각에 들어가 있고 진짜 비대각은 0" 이라는 모순된 그림이 된다.

**판정**: 수치 공식 자체는 양쪽이 일치하므로 계산 오류는 없다. 틀린 것은 기호 사용이며 physics/part04 쪽이 옳다: 관성 텐서 성분 표기 $I_{ij}=\int \rho(\delta_{ij}|r|^2 - r_i r_j)dV$ 에서 $I_{xy}$ 는 정의상 $i=x, j=y$ 인 비대각 원소다(part04 L66 의 자기 정의식과도 일치). physics/part19 의 `Ixy` 는 "x·y 두 횡축의 공통 관성모멘트" 라는 비공식 약칭으로, part04 가 명시한 규약과 충돌한다. 참고로 physics/part19 의 값 자체는 검증했다 — 실린더 근사에서 장축 $\tfrac12 m r^2$, 횡축 $\tfrac{1}{12}m(3r^2+L^2)$ 로 physics/part04 2.2절 원기둥 행과 동일하다(축 이름만 y↔z 로 다르며 코드에 `// long axis` 로 명시됨).

**정정**: physics/part19 의 변수명을 `Ixy` → `Itrans`(또는 `Ixx_yy`, `IperpAxis`) 로 바꾼다. 예: `float Itrans = mass * (3 * r2 + L * L) / 12.0f;   // 횡축 (Ixx = Iyy, 실린더 공식)` / `return mat3{Itrans, 0, 0,  0, Itrans, 0,  0, 0, Iz};`

> **상태 (2026-08-13): 반영됨** · `physics/part19_ragdoll.md`
> §2.4 코드의 `Ixy` → `Itrans`, 주석도 `// 횡축 (Ixx = Iyy, 실린더 공식)` 로. 반환 행렬 두 자리도 함께 교체. 판정대로 **수치 공식은 손대지 않았다**(값은 원래 옳았고 기호 사용만 문제였으므로).

---

## 대조했으나 모순이 없던 항목

**SAT (분리축 정리)** (1건 발견)

1) 축 후보 개수·생성 규칙: math §1.5 표(2D AABB 2축 / n각형-m각형 n+m / OBB-OBB 15=3+3+9 / 다면체 f_A+f_B+e_A·e_B), §2.3~2.4, §2.5(정12면체 면12·모서리30 → 24 + 900), 결론 L720 \"15축 (3 + 3 + 9). 면 법선 6 + 모서리 외적 9\" vs physics §3 L150 \"convex polygon은 face normal, convex polyhedron은 양쪽 face normal과 edge-edge cross product\", §8 표 L432 \"box-box | 특화 15축 SAT + clipping\" — 모순 없음. 900 = 30×30 산술 확인.\n2) 축 부분집합 검사 시 오류의 방향(중점 확인 항목): math §6 L481 \"실제로 떨어진 도형을 교차한다고 판정하는 false positive\", §6.1 mermaid L490 \"분리를 못 잡아 교차로 오판\", §6.5 L536 \"검사 축의 부분집합에서 분리를 발견했다면 그 결론은 항상 참이지만, 분리를 발견하지 못했다고 교차가 보장되지는 않는다\", L538 재현 시나리오, 결론 L721 — 모두 올바른 방향. physics/part06은 이 명제를 다루지 않으며(선수 지식으로 명시, L11·L150), 관련 서술 L108 \"후보 축 집합의 완전성이 보장되지 않는다\"와 L421 \"수치 실패를 무조건 충돌이나 분리로 바꾸면 각각 유령 충돌 또는 tunneling\"도 방향이 정확하다. **같은 오류를 공유하지 않음 — 보고할 모순 없음.**\n3) 겹침 깊이·MTV 계산 → 모순 1건 발견(위 findings). physics EvaluateAxis(L191-201) 코드를 직접 재검증: 분리 조건 pushBPositive<-ε ⟺ a1<b0-ε, pushBNegative<-ε ⟺ b1<a0-ε로 L166-170 수식과 일치하고, depth=min(d+,d-)·법선 부호도 정확. math §5.1 OBB 경로 halfA+halfB-|Δc·n| = min(d+,d-) 등가성 직접 유도.\n4) 법선 방향 규약: 양쪽 모두 A→B로 통일(math §4.4 L380-384 `Centroid(b)-Centroid(a)` / physics L64-66 `Dot(centerB - centerA, normal) < 0`), 밀어내는 부호(

**GJK / EPA** (2건 발견)

양쪽에서 실제로 대조하고 모순이 없었던 항목: (1) 지지함수 정의 — math L113 argmax형 vs physics L54 max형, 같은 계약. (2) Minkowski 차 규약 — 양쪽 모두 A⊖B = {a−b}, support_{A⊖B}(d) = support_A(d) − support_B(−d) (math L169 / physics L35-48의 `sa.position - sb.position`, `b.Support(-direction)`) 일치. (3) 도형별 지지함수 표 (math L127-134 / physics L62-68): sphere c+r·d̂ 등 일치, convex hull 복잡도 O(n) vs O(V) 일치. (4) 탐색 방향 규약 — math L480 dHat = −v/|v|, physics L183 `direction = -step.closestPoint` 동일. (5) 거리 모드 수렴 판정은 서로 다른 기준(math=듀얼리티 갭 |v|+dot(newPt,dHat), physics=제곱거리 개선량 Δ=d_old²−d_new² 상대·절대 epsilon)이지만 둘 다 표준 기법(van den Bergen형 vs Bullet형)이고 서로 배타적이지 않아 모순 아님. math L482 식의 부호는 재유도 결과 옳음(상한 |v| − 하한 −dot(newPt,dHat)). (6) EPA 종료 조건 — math L595 `newDist - face.distance < epsilon` vs physics L296 Δ = d_support − d_face ≤ ε_EPA, 동일. (7) EPA 면 법선·거리 규약 — 외향 법선 + distance = dot(n, v_a), winding 뒤집기(math L391 `if (dot(abc, ad) > 0) swap(b, c)` / physics L283) 검산 결과 양쪽 옳고 일치. (8) 법선 부호 방향 — physics L217 분리 법선 n = (p_B−p_A)/‖·‖ 및 L362 `Dot(centerB - centerA, normal) < 0` 보정이 A→B 방향인데, math L643-644의 위치 보정(a는 −n, b는 +n)도 A→B. 구-구 예제(중심 0과 0.5, 반지름 1 → C는 중심 (−0.5,0,0)·반지름 2, 최근접 경계점 (1.5,0,0), 깊이 1.5 = 2−0.5)로 직접 검산해 일치 확인. (9) EPA 시드 — 

**충돌 프리미티브 · 광선 교차 · broad phase** (3건 발견)

[일치 확인되어 보고하지 않은 항목]
1) AABB-AABB 겹침 판정식: part11 L228~233 과 part05 L70~74 가 비교 방향(`a.max.x < b.min.x || a.min.x > b.max.x`)까지 동일. 접촉(max==min)을 교차로 보는 정책도 같음. 비교 횟수도 part05 L77 \"4번 비교(2D) / 6번 비교(3D)\" ↔ part11 L302 \"6번의 비교\"(3D)로 일치.
2) 프리미티브 메모리·표현: part11 Sphere 16B(12+4), AABB 24B(12+12), OBB 60B(12+36+12) 및 quat형 40B(16+12+12), Capsule 28B(12+12+4) — 전부 직접 재계산해 산술 일치. §1.5 표(L111~116)와 본문 수치도 일치.
3) SAT 축 개수: part11 L254 와 L503~515 모두 OBB-OBB 15축(면법선 6 + 외적 9)으로 일치.
4) contact normal 규약: part11 L267~269 에서 'a → b' 로 선언한 뒤 §3.3(L284~292), §4.4(L429~438), §4.5(L456~457)의 해소 코드가 모두 `-normal` 방향으로 일관. §4.4 내부 케이스(가까운 면이 min쪽 → normal +축)도 직접 검산해 부호가 맞음. 침투 깊이 `radius + dx` 도 검산 일치.
5) 광선-도형 식 재유도: Ray-Sphere 2차식(part17 L195, 코드 L203~220), 반(半)-b 단순형(L234~244), Slab test 식(L289~301)과 코드(L306~317), Möller-Trumbore 행렬식·Cramer(L400~430) 모두 직접 유도해 일치. Ray-Capsule 무한 실린더 2차식(L566~586, A/B/C 및 h=(md+t·nd)/dd)도 RTCD 형태와 일치.
6) 연산 횟수 주장: part17 L433 \"cross 두 번 + dot 네 번\" — 코드에서 cross 2(pvec,qvec), dot 4(e1·pvec, tvec·pvec, rd·qvec, e2·qvec) 확인. L498 \"변환 6 dot\" — localRO 3 + localRD 3 확인. L124 Ray-Plane \"1번 나눗셈\" 확인.
7) 부호 관례: Ray-Plane backface(part17 L157~165, denom<0 = front)와 Möl

**적분기 · 동역학 · 제약 해법** (4건 발견)

[대조하여 모순 없음을 확인한 항목] (1) 적분기 식·순서: part02 §2.1 semi-implicit(v→x) ↔ part03 §6.1·§6.5 ↔ part11 §2.6 Integrate ↔ part12 §2.2 predict — 네 문서 모두 v 먼저 x 나중으로 일치. (2) part02 §6.2 재현표 전체를 §6.1 코드로 직접 실행해 소수 6자리까지 검증(explicit 1.352407/72.386386/10479.577819, semi 0.477815/0.511911/0.521321, verlet 0.499625/0.499927/0.499724, rk4 0.499999/0.499997/0.499993 — 모두 일치). (3) explicit Euler 에너지 증배 $E_{n+1}=E_n(1+k\Delta t^2)$ 유도 재확인, 1.01^1000=20959.2, 진폭 √(2E)=144.8≈145, 80행 \"약 21,000배\" 및 413행 \"약 20,959배\" 모두 정합. (4) 안정 한계 ωΔt<2 ↔ \"주기의 1/π\" 환산(Δt<T/π) 검산 일치, velocity Verlet 동일 한계도 맞음. (5) part02 §6.5 등가속 bias ±½aΔt·t 재유도 — explicit/semi 반대 부호, Verlet 정확 일치 확인. (6) symplectic 서술: part02 §5.3·§5.4 는 \"분리 가능 Hamiltonian 에 한해\", \"수정 Hamiltonian\", \"절대 발산·감쇠 안 한다는 보장 아님\" 으로 적절히 한정되어 있어 과단정 없음. (7) PBD stiffness 의존성: part11 §8.2 \"iteration-dependent stiffness는 남음\" ↔ part12 §5 전체 — 일치. $k_{eff}=1-(1-k)^n$ 및 mermaid 의 93.75%/99.9% 수치 검산 일치. (8) XPBD: $\tilde\alpha=\alpha/\Delta t^2$ 가 §6.2·§7.3 에서 동일, [α]=m/N 차원 검산 일치, §6.4 코드가 §6.3 식(분모 Σwᵢ|∇ᵢC|²+α̃, Δxᵢ=wᵢ∇ᵢC·Δλ)과 부호까지 일치, α=0 ↔ PBD stiffness=1 등가 확인. (9) substepping: part11 §6.5(1/240×4=1/60) ↔ part12 §7.2(1/60÷8≈2.08ms) 산술 정확, 방향 주장(작은 dt 

**회전 표현 · 관성 텐서** (1건 발견)

■ 사원수-각속도 미분식 (핵심 대조 항목) — **네 문서 모두 완전 일치, 모순 없음**.
 · math/part05 L582 `q̇ = ½ ω q` (월드), L587 \"body / 로컬 좌표계 기준이면 q̇ = ½ q ω 로 우측 곱\"
 · physics/part04 L385–397 world → `½ ω_q q`, body → `½ q ω_q`, 7.1절 표(L477–480)에서 재확인, 결론 L639 도 동일
 · 직접 재유도해 검증: q 가 body→world 일 때 ω_world = q ω_body q* → ½ω_world q = ½ q ω_body. 두 식 모두 옳다.
 · 코드도 규약과 일치: physics/part04 L416 `QuaternionMultiply(omega_q, body.orientation)` = ω·q (월드), L447 exp map `QuaternionMultiply(dq, body.orientation)` 도 좌측 곱. math/part05 L598 `exp(Δt/2 ω) q(t)` 도 좌측 곱.
 · math/part04, physics/part19 에는 q̇ 서술 자체가 없음(전체 검색 확인).

■ 곱 순서·회전 합성 방향 — 일치. math/part05 L471 \"합성 쿼터니언이 q₂q₁ (오른쪽부터 적용). 행렬 곱과 같은 순서\" / math/part04 L54 \"R₂ ∘ R₁\" / physics/part19 L116 \"세계 transform = 부모의 world transform × local\". 셋 다 \"왼쪽이 나중 적용\".

■ 켤레·역·정규화 — math/part04 5.5절(L424)과 math/part05 2.5절(L220)이 동일($\\|q\\|=1 ⟹ q^{-1}=q^*$). 곱셈 공식도 성분 단위로 대조: math/part04 L414 스칼라-벡터형 = math/part05 L102 = math/part05 L84–96 성분 전개 = L108–115 코드. 외적 항 부호까지 재계산해 일치 확인.

■ 관성 텐서 좌표 변환 — physics/part04 L99 `I_world = R I_body Rᵀ`, L107–110 및 L493–495 의 `R * invInertiaBody * transpose(R)` 와 정합(역행렬도 같은 형태). physics/part19 L182 \"world 에선 회전 변환\" 과 모순 없음.

■

---

## 반영 상태 (2026-08-13)

검수 시점 **2026-08** / 반영 시점 **2026-08-13**.
머리말의 "본문은 수정하지 않았다"는 검수 시점의 기술이며, 이 절이 그 이후를 기록한다.

### 측정 방법

추측이 아니라 백업 대조로 판정했다. 원본은 `VeLog_backup_2026-08-11/` (같은 상대 경로).

```
diff -ru --strip-trailing-cr \
  VeLog_backup_2026-08-11/{math,physics} VeLog/{math,physics} \
  -x "REVIEW_*" -x "REDESIGN_*" -x diagrams -x build
```

줄끝(CRLF/LF)만 다른 hunk 는 `--strip-trailing-cr` 로 걸렀고, `diagrams/` 의 "Only in" 은
백업이 텍스트만 담은 탓이므로 무시했다. 판정이 애매한 곳은 현재 본문을 직접 열어 확인했다
(§4.3 MTV 부호, §2.2 에너지 식의 $k$ 계수 두 곳).

### 집계

| 판정 | 수 | 비고 |
|---|---|---|
| **반영됨** | **11** | 보고서의 11건 전부. diff 에 대응 hunk 확인 |
| **기반영** | 0 | 보고서 작성 후 먼저 반영돼 있던 항목 없음 |
| **기각** | 0 | 이 보고서에는 "검증에서 기각된 것" 류 절이 없다 (아래 주 참조) |
| **이월** | 0 | 미착수로 남은 *지적* 은 없음. 단 **잔여 하위 항목 1개** 있음 (아래) |
| 계 | 11 | |

**이 보고서는 낡아 있지 않았다.** 다른 트랙(math·graphics·physics·ai 편별 검수)에서는 보고서 작성
이후 누군가 이미 본문에 반영해 둔 항목이 다수 발견됐지만, 이 교차 대조 보고서에는 그런 항목이
없다. 근거: **2026-08-11 백업에 11건의 인용된 "수정 전" 텍스트가 모두 그대로 남아 있었다.**
즉 11건 전부가 2026-08-13 배치에서 처음 반영됐다. (백업 시점이 2026-08-11 이므로, 검수 작성과
백업 사이에 선반영된 것이 있었다면 이 방법으로는 잡히지 않는다 — 다만 11건 모두 인용문이
백업과 일치했으므로 실제로 그런 항목은 없다.)

### 항목별

| # | 주제 | 대상 | 판정 | 실제 변경 |
|---|---|---|---|---|
| 1 | SAT | `math/part12_collision_sat.md` §3.3 | 반영됨 (잔여 1) | `OverlapAmount` → `min(push+, push-)`, L260 문장 교체 + containment 반례 삽입 |
| 2 | GJK/EPA | `math/part13_collision_gjk_epa.md` §5.2 | 반영됨 | L486 "진행도 음수" 서술 → "항상 0 이상 / $v=0$ 로 분기" |
| 3 | GJK/EPA | `math/part13_collision_gjk_epa.md` §4.4·§7.1 | 반영됨 | 두 `return false` 를 '판정 실패' 로 재규정, `IterationLimit`·`DuplicateSupport` 문단 신설 |
| 4 | 프리미티브 | `math/part11_collision_primitives.md` L302 | 반영됨 | "SIMD 로 묶으면 한 명령으로" → "비교 2명령 + mask 결합 + 부호 추출" |
| 5 | 광선 | `math/part17_raycasting.md` §7.2·§7.3·§7.5 | 반영됨 | 벌거벗은 "8.2/8.3/8.5절" → "BVH 편 8.2/8.3/8.5절" (3곳) |
| 6 | broad phase | `physics/part05_broad_phase.md` L244 | 반영됨 | $O(N+k)$ → $O(N+K)$ + 한 step 전체 비용임을 명시 |
| 7 | 적분기 | `physics/part12_pbd.md` §1.5 + `physics/part02_integrators.md` §3.2 | 반영됨 | "속도 저장 안 함" 주장 철회, 속도를 상태로 보관함을 두 문서 모두에 명시 |
| 8 | 적분기 | `physics/part02_integrators.md` §2.2 | 반영됨(개선) | $O(\Delta t^3)$ → $\tfrac12 k\Delta t^2(v_n^2-kx_n^2)+O(\Delta t^3)$ + 부호 교대·$\pm5\%$ 설명 |
| 9 | 적분기 | `physics/part02_integrators.md` §6.2 | 반영됨 | Verlet 에너지 오차 $10^{-4}$ → $10^{-3}$ ($E_0h^2/4$) + 위상 단서 |
| 10 | 제약 | `physics/part12_pbd.md` §6.2 | 반영됨 | XPBD $\lambda$ 의 차원($\text{kg}\cdot\text{m}$)과 11편 임펄스와의 $\Delta t$ 환산 주석 신설 |
| 11 | 회전 | `physics/part19_ragdoll.md` §2.4 | 반영됨 | `Ixy` → `Itrans`, 주석 `// 횡축 (Ixx = Iyy, 실린더 공식)` |

8번은 정정안의 식보다 **개선된 형태**로 들어갔다(정정안에 빠져 있던 $k$ 계수가 붙어 §1.4의
explicit Euler 식과 형태가 나란해졌다). 재유도로 확인했다 — 상세는 해당 항목의 상태 블록 참조.

### 남은 일 (다음에 이 저장소를 여는 사람에게)

**미착수로 남은 지적: 없음.** 다만 아래 **잔여 하위 항목 1개**는 여전히 열려 있다.

1. **`math/part12_collision_sat.md` §4.3 — MTV 법선 부호 결정 방식** (항목 1의 하위 항목)
   - 반영된 것: §3.3 의 깊이 계산이 `min(pushPositive, pushNegative)` 로 교정됨.
   - **남은 것**: 정정안이 함께 요구한 "§4.3 에서 축별로 `pushPositive`/`pushNegative` 중
     어느 쪽이 작은지를 보고 MTV 법선 부호를 정한다"는 적용되지 않았다.
     §4.3 은 여전히 `OverlapAmount` 의 *크기*만 비교하고(`if (overlap < result.depth)`),
     부호는 §4.4 의 `Centroid(b) - Centroid(a)` 보정에 맡긴다.
   - 왜 급하지 않은가: 두 방식은 "투영 구간의 중점 비교"와 "무게중심 투영 비교"로,
     $\min(d_+,d_-)$ 가 $+$ 를 고르는 조건은 $\mathrm{mid}(a) < \mathrm{mid}(b)$ 와 동치이므로
     대칭 도형에서는 항상 같은 답을 준다. 깊이 값 자체는 이제 양쪽 다 옳다.
   - 왜 그래도 남는가: 투영 구간 중점과 도형 무게중심의 투영이 어긋나는 **비대칭 볼록 다각형**
     에서 두 판정이 갈릴 수 있다. 이 경우를 닫으려면 §4.3 을 축별 부호 결정으로 바꾸거나,
     §4.4 의 centroid 보정이 근사임을 한 줄 명시해야 한다.
   - 구조 개편이 필요한 것은 아니고, §4.3 코드 한 블록 + §4.4 한 문장 수준의 작업이다.

**[2026-08-13 후속 처리]** (2026-08-14) — **닫혔다. 위에 적힌 두 길(§4.3 을 축별 부호 결정으로 / §4.4 의 centroid
보정이 근사임을 명시)을 둘 다 택했다. 이제 이 보고서에 열려 있는 항목은 없다.**

- **§4.3 코드** — 람다의 `flipNormal` 매개변수와 호출부 `check(a, false)` / `check(b, true)` 인자를 없애고,
  축마다 `pushPositive = pa.max - pb.min` · `pushNegative = pb.max - pa.min` 을 직접 계산해
  `result.normal = (pushPositive < pushNegative) ? axis : -axis;` 로 부호를 정한다
  (주석: "깊이를 고른 밀어내기 방향이 그대로 법선 부호 (a → b)"). 깊이는 `std::min(pushPositive, pushNegative)`
  로 §3.3 의 `OverlapAmount` 와 같은 값이다.
- **§4.4 문장** — "4.3 의 코드는 이 규약을 이미 만족한다 — *깊이를 고른 밀어내기 방향* 이 곧 b 를 밀어낼
  방향이기 때문이다" 를 앞에 두고, 무게중심 보정 코드는 "축별 부호를 추적하지 않은 구현의 사후 보정" 으로
  성격을 바꿔 **남겼다**(지우지 않았다). 그 아래에 **근사임을 밝히는 문단과 반례**를 새로 달았다.
- **§4.2 도 연쇄 수정** — "SAT 의 교차 판정 자체는 축 부호와 무관. 하지만 *MTV (4.3절)* 와 contact normal 부호가
  외향 법선 일관성에 의존한다" 가 더 이상 참이 아니게 됐으므로,
  "*MTV 부호* 도 4.3 절처럼 *밀어내기 방향* 에서 뽑으면 축 부호와 무관하다. 하지만 축 방향을 그대로 법선으로
  쓰는 구현과 *contact manifold 의 reference face 선택* (7.5절) 은 *외향 법선 일관성* 에 의존한다" 로 바꿨다.
- **"두 판정이 갈릴 수 있다" 를 반례로 확정했다.** 이 보고서는 가능성만 적었으나, 직접 계산해 실제로 갈리는
  삼각형 쌍을 얻었다 — $A$ = $(-1,-1),(0,-1),(0,2)$, $B$ = $(-3,1),(0,-1),(3,1)$ 에서 최소 축은 $y$,
  $\text{push}^+ = 3$, $\text{push}^- = 2$ 라 b 를 $-y$ 로 2 만큼 밀어야 하는데, 무게중심
  $c_A = (-1/3,\,0)$ · $c_B = (0,\,1/3)$ 비교는 $+y$ 를 고르고 그 방향으로 2 를 밀면 겹침이 $y \in [1,2]$ 로 남는다.
  "정사각형처럼 중심 대칭인 도형만 쓰는 테스트로는 드러나지 않는 결함" 이라는 문장과 함께 §4.4 본문에 실렸다.
- **`math/part12` §5.1 `OBBOBBSAT` 과 §7.2 `TrackMinOverlap` 은 오류가 아니다 — 고치지 마라.**
  둘 다 `dot(centerDelta, axis) < 0` 형태의 **중심 비교**로 부호를 정하지만, 입력이 **중심 $c$ + 반투영 길이 $h$**
  (OBB·중심 대칭 도형)라 투영 구간의 중점이 중심의 투영과 일치한다. 실제로
  $\text{push}^+ = h_a + h_b + (c_a - c_b)\cdot\text{axis}$, $\text{push}^- = h_a + h_b + (c_b - c_a)\cdot\text{axis}$
  이므로 $\text{push}^+ < \text{push}^-$ 와 $\text{dot}(c_b - c_a,\ \text{axis}) > 0$ 은 **항등**이다.
  §4.3 을 고치게 만든 비대칭 반례가 이 두 곳에서는 성립하지 않는다. 위 「기각 항목에 대한 주」의 목록과
  같은 성격의 "건드리면 오히려 틀리는 것" 이다.

### 기각 항목에 대한 주

이 보고서에는 **"검증에서 기각된 것" 류 절이 존재하지 않는다** — 편별 검수와 달리 교차 대조는
모순을 찾으면 곧바로 어느 쪽이 옳은지 판정했으므로 회수된 오탐이 없다. 따라서 기각 0건이다.

다만 문서 끝의 **"대조했으나 모순이 없던 항목"** 절(5개 주제)은 성격이 다르다. 이는 오탐 회수
목록이 아니라 **대조 결과 두 문서가 일치함을 확인한 기록**이며, 실질적으로 "**고치면 안 되는 것**"
의 근거다. 특히 다음은 나중에 "모순처럼 보인다"는 이유로 건드리기 쉬우니 주의한다.

- **거리 GJK 의 수렴 판정이 두 문서에서 다른 것**(math = 듀얼리티 갭, physics = 제곱거리 개선량)
  — 각각 van den Bergen 형 / Bullet 형 표준 기법이고 서로 배타적이지 않다. 통일 대상이 아니다.
- **XPBD $\lambda$ 와 sequential impulse $\lambda$ 의 $\Delta t$ 차이**(항목 10) — 식은 양쪽 다
  각자의 원전 관례 안에서 옳다. 반영된 것은 환산 주석뿐이고, **식을 맞추려 해서는 안 된다**.
- **`physics/part19` 의 실린더 관성 수치**(항목 11) — 값은 원래 옳았다. 바뀐 것은 변수명뿐이다.
- **$\dot q = \tfrac12\omega q$ (월드) / $\tfrac12 q\omega$ (body)** — 네 문서 모두 일치하며 둘 다
  옳다. 한쪽으로 통일하면 오히려 틀린다.

### 이 보고서 소관 밖의 변경 (같은 diff 에 섞여 있음)

같은 기간에 다른 보고서(편별 검수)의 지적도 반영돼 있어 diff 에 함께 잡힌다. **아래는 이
보고서의 11건에 포함되지 않으며 위 집계에서 제외했다.** 판정은 각 소관 보고서에서 해야 한다.

- `math/part02_vectors.md` — FOV 콘 예제의 미정의 변수 `dot` → `facing`
- `math/part10_procedural_noise.md` — `MultiWarpNoise` 의 `vec2 q` 재선언 → `warped`
- `math/part20_numerical_stability.md` — Kahan mermaid 의 `add 0.0001` → `0.00001` (본문과 일치)
- `physics/part13_joints.md` — 7곳 (`rA`/`rB` 를 구조체 필드로, 야코비안 회전항 부호,
  hinge 축 정렬 조건, `frequency` 를 '주기' → '진동수', Wheel joint DOF 2 → 1)
- `physics/part18_fluids_sph.md` — 4곳 (점성 항에 $1/\rho_i$ 추가 및 코드 반영, 선형 EOS 의
  $c_s^2 = k/\rho_0$ → $c_s^2 = k$)

`math/`·`physics/` 전체에서 이 두 그룹 외의 변경은 없었다. 파일 추가·삭제도 없다.

