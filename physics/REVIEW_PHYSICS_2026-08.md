# physics/ 시리즈 검수 보고서 (2026-08)

- 대상: physics/ 20편 (13,083줄)
- 기준: **확정 가능한 사실오류·산술오류·컴파일 불가 코드·서지오류·내부모순**만. 문체·구성 취향은 대상 아님.
- 방법: 파일별 전문 정독 → 모든 수치·유도 재계산 → 서지 저자/연도/게재지/장절 대조 → 코드 컴파일 가능성·논리 검증 → 문서 내부·문서 간 교차 대조.
- **본문 반영 완료 — 이 보고서는 더 이상 "지적만 하고 본문은 건드리지 않은 문서"가 아니다.** 검수 시점은 **2026-08**, 본문 반영은 두 차례로 나뉜다 — **2026-08-09에 17건**(part02·04·05·12·14·17·19·20), **2026-08-13에 7건**(part13 5건 + part18 2건). 건별 판정·근거·남은 일은 문서 끝 [반영 상태 (2026-08-13)](#반영-상태-2026-08-13) 절에 있다.
- 본문에 인용된 원문 조각은 모두 **수정 전** 상태다. 현재 본문과 글자가 다른 것은 정상이며, 나중에 판정을 재검토할 근거로 **일부러 그대로 둔다.**

---

## physics/part02_integrators.md — 확정 3건

### 1. L585 — Eberly *Game Physics*에 없는 장

**[기반영 · 2026-08-09 반영분]** 현재 L585 = `David Eberly, *Game Physics*, 2nd ed., Ch. 11, 13 — 미분방정식과 수치 적분.` — 지적대로 Ch.13 중심에 Ch.11 병기.

> David Eberly, *Game Physics*, 2nd ed., **Ch. 16** — 게임 물리의 적분기.

*Game Physics* 2nd ed.(Morgan Kaufmann, 2010)은 **총 14장**이라 Ch.16이 존재하지 않는다
(1 Introduction / 2 Basic Concepts from Physics / 3 Rigid Body Motion / 4 Deformable Bodies /
5 Fluids and Gases / 6 Physics Engines / 7 Linear Algebra / 8 Affine Algebra / 9 Calculus /
10 Quaternions / 11 Differential Equations / 12 Ordinary Difference Equations /
13 Numerical Methods / 14 Linear Complementarity and Mathematical Programming).
적분기는 **Ch. 13 "Numerical Methods"** 다(필요하면 Ch. 11 Differential Equations 병기).

### 2. L32 — 목차 링크가 깨짐 (개정 잔재)

**[기반영 · 2026-08-09 반영분]** 현재 L32 = `8. [통일된 적분기 인터페이스](#8-통일된-적분기-인터페이스)` — L478 제목과 앵커가 일치.

```
8. [Integrator 인터페이스 추상화](#8-integrator-인터페이스-추상화)
```

실제 §8 제목은 L478 **"## 8. 통일된 적분기 인터페이스"** 다.
생성되는 앵커는 `#8-통일된-적분기-인터페이스` 인데 링크는 `#8-integrator-인터페이스-추상화` 를 가리켜
**클릭하면 이동하지 않는다.** 1~7번 항목은 모두 본문 제목과 정확히 일치하므로 8번만 잔재다.

### 3. L16 — 존재하지 않는 산출물을 예고 (개정 잔재)

**[기반영 · 2026-08-09 반영분]** 현재 산출물 2번 = "*조화진동자* 시뮬에서 step별 에너지 증가를 **표로** 비교" — 없는 그래프 예고가 사라짐.

> 2. 조화진동자 시뮬에서 에너지 곡선을 **그래프로**.

문서에 **에너지 곡선 그래프가 없다.** §6.2의 산출물은 step 0/100/500/1000 **네 행짜리 표**이고,
문서의 도식 2개(L266-273, L307-311)는 mermaid flowchart로 에너지와 무관하다.

> 참고 — §6.2 표의 수치는 전부 정확했다. $E_n = 0.5(1+k\Delta t^2)^n = 0.5\cdot1.01^n$ 로
> $n=100 \to 1.352407$, $n=500 \to 72.386386$, $n=1000 \to 10{,}479.577819$ 가 모두 일치하고,
> "초기값의 약 20,959배", 위상공간 반지름 $\sqrt{2E} \approx 145$ 도 정확하다.

---

## physics/part05_broad_phase.md — 확정 3건

### 1. L168-180 — sleeping 객체를 broad phase에서 통째로 빼면 깨울 수 없음

**[기반영 · 2026-08-09 반영분]** §2.5가 통째로 다시 쓰였다 — "Sleep 상태 객체의 proxy도 broad phase에 남겨야 … 깨울 수 있다", 코드도 `active-active와 active-sleeping 모두 생성`, 비용도 $O(N_{active}N)$ 로 정정.

> 게임의 Sleep 상태 객체는 broad phase에서 **제외**. 활성 객체 수 $N_{active}$ 만으로 검사

코드도 `active × active` 이중 루프만 돈다. 그러면 **"깨어 있는 객체 vs 잠든 객체" 페어가
아예 생성되지 않는다** — 접촉이 검출되지 않으니 **잠든 객체를 깨울 수 없고(wake-on-contact 불가),
움직이는 body가 잠든 body를 그대로 통과한다.**

실제 엔진(Box2D)은 sleeping body의 proxy를 broad phase 구조에 **그대로 유지**하고,
**"양쪽 모두 잠든" 페어만** 이후 단계(island/solver)에서 건너뛴다.
비용도 $O(N_{active}^2)$ 가 아니라 $O(N_{active} \times N)$ 이다.

### 2. L473 — PhysX broad phase에 없는 알고리즘

**[기반영 · 2026-08-09 반영분]** 현재 L480 = "**PhysX**: SAP, MBP, ABP/PABP, GPU broad phase를 scene 특성에 맞게 선택. AABB tree는 scene query 가속 구조에서 사용한다." — DBVT 표기 삭제, L281·L636과의 내부모순 해소.

> **PhysX**: SAP + **DBVT** hybrid

PhysX의 `PxBroadPhaseType`은 **eSAP, eMBP(Multi Box Pruning), eABP(Automatic Box Pruning),
ePABP, eGPU** 이며 **dynamic AABB tree(DBVT) 기반 broad phase는 없다.**
MBP/ABP는 월드를 region으로 나눠 각 영역에 box pruning을 적용하는 **grid/region 계열**이다.
PhysX가 AABB tree를 쓰는 곳은 broad phase가 아니라 **scene query 가속 구조**다.
(DBVT broad phase는 Bullet의 `btDbvtBroadphase`다.)

**같은 문서의 L274("PhysX는 scene 특성에 따라 SAP / grid 계열 broad phase를 조합한다")와
L629("PhysX의 SAP + grid hybrid")가 정확하게 적고 있어 내부에서도 어긋난다.**

### 3. L485 — 복잡도 표의 brute force 행

**[기반영 · 2026-08-09 반영분]** 현재 L492 = `| Brute force | $O(1)$ | $O(N^2)$ 페어 추출 | $O(1)$ |`.

| 알고리즘 | 빌드 | **쿼리 / 페어 추출** | 갱신 |
|---|---|---|---|
| Brute force | $O(1)$ | **$O(N)$** ← | $O(1)$ |
| SAP | $O(N\log N)$ | $O(N+K)$ 페어 추출 | … |

열 머리글이 "쿼리 / 페어 추출"이고 SAP 행이 **페어 추출 비용**을 적고 있으므로 열의 의미는 명확한데,
brute force의 페어 추출은 정의상 $\binom{N}{2}$ 회 검사라 **$O(N^2)$** 다.
본문 L138("복잡도는 $O(N^2)$"), L5, L636("naive $O(N^2)$")과도 모순이다.
$O(N)$은 **단일 영역 쿼리 1회** 비용일 뿐이다.

---

## physics/part12_pbd.md — 확정 1건

### L344 — 제약 개수가 코드와 어긋남

**[기반영 · 2026-08-09 반영분]** 현재 L344 = "9 거리 제약 + 끝점 고정 = rope 시뮬." — 코드의 `i < 9` 와 일치.

```cpp
// 10 노드 rope
for (int i = 0; i < 9; ++i) {                 // i = 0..8 → 9개
    constraints.push_back(DistanceConstraint{i, i+1, segmentLength, 1.0f});
}
```

> **10 거리 제약** + 끝점 고정 = rope 시뮬.

노드 10개짜리 **열린 체인의 세그먼트는 10 − 1 = 9개**이고 코드도 정확히 9개를 만든다.
본문만 10이다.

---

## physics/part14_character_controller.md — 확정 4건

이 문서는 **코드 정합성 결함이 집중된 편**이다. 문서 안의 코드를 그대로 모으면 빌드되지 않는다.

### 1. L213 / L303 / L474 — 정의되지 않은 멤버 `center` (컴파일 불가)

**[기반영 · 2026-08-09 반영분]** 세 시그니처가 모두 `UprightCapsule` 로 교체됨 — 현재 L213 `vec3 MoveAndSlide(UprightCapsule cap, …)`, L303 `MoveWithStep(UprightCapsule cap, …)`, L476 `PushOutFromOverlaps(UprightCapsule& cap, …)`.

§2.2(L130-134)의 정의:

```cpp
struct Capsule       { vec3 bottom; vec3 top; float radius; };   // center 없음
struct UprightCapsule{ vec3 center; float halfHeight; float radius; };
```

그런데 `MoveAndSlide(Capsule cap, ...)`(L213), `MoveWithStep(Capsule cap, ...)`(L303),
`PushOutFromOverlaps(Capsule& cap, ...)`(L474)가 모두 **`cap.center`를 읽고 쓴다**
(L218, L224, L229-232, L311-342, L418, L480-481, L514).
시그니처를 `UprightCapsule`로 바꿔야 §2.5의 정의와 맞는다.

### 2. L380 — 인자 3개 정의, 호출은 전부 1개 (컴파일 불가)

**[기반영 · 2026-08-09 반영분]** 기본 인자가 붙었다 — 현재 L379-381 `bool IsWalkable(vec3 groundNormal, vec3 up = vec3{0, 1, 0}, float maxSlopeRadians = 0.78539816339f)`. 호출부 3곳은 인자 1개 그대로 두어도 컴파일된다.

```cpp
bool IsWalkable(vec3 groundNormal, vec3 up, float maxSlopeRadians) { ... }
```

기본 인자 선언이 없는데 문서 안의 호출은 **셋 다 인자 1개**다 —
L333 `!IsWalkable(downHit.normal)`, L417 `IsWalkable(hit.normal)`, L442 `!IsWalkable(groundNormal)`.
`vec3 up = {0,1,0}, float maxSlopeRadians = kMaxSlope` 처럼 기본값을 주거나 호출부를 고쳐야 한다.

### 3. L300 / L322 / L336 — 같은 타입의 멤버 이름이 절마다 다름

**[기반영 · 2026-08-09 반영분]** `fraction` 이 문서 전체에서 사라지고 `hit.t` 로 통일됐다 (현재 파일에 `fraction` 출현 0회).

`SweepHit`의 정규화 비율 멤버가 **§3.2·§5.3·§6.5에서는 `hit.t`**(L229 `hit.t * maxDist`,
L232, L418, L514), **§4.2에서는 `fraction`**(L300 설명, L322, L336)이다.
의미는 같지만 한 타입에 두 이름이 공존할 수 없어 문서의 코드를 모으면 컴파일되지 않는다.

### 4. L3 — 본문 첫 문장에 주어가 없음 (개정 잔재)

**[기반영 · 2026-08-09 반영분]** 현재 L3 = "강체 시뮬레이션은 *물리적으로 정확한* 응답을 만든다." — 주어 복원.

> **들의** 강체 시뮬레이션이 물리적으로 정확한 응답을 만든다.

앞선 편을 가리키던 구절(예: "이전 편들의")이 삭제되면서 조사만 남았다.
문서를 여는 첫 문장이라 눈에 띈다.

---

## physics/part17_soft_body.md — 확정 1건

### L382 — const 한정자를 버려 컴파일 불가

**[기반영 · 2026-08-09 반영분]** 현재 L382 = `const int* idx = &tets[v.tetIdx * 4];`.

```cpp
vec3 ComputeRenderPos(const RenderVertex& v,
                       const std::vector<vec3>& simParticles,
                       const std::vector<int>& tets) {
    int* idx = &tets[v.tetIdx * 4];        // ← const int* → int*
```

`tets`가 `const std::vector<int>&` 이므로 `tets[i]`는 `const int&`, `&tets[i]`는 **`const int*`** 다.
`int*`로 초기화하면 **GCC/Clang `invalid conversion from 'const int*' to 'int*'`, MSVC C2440**.
함수는 `idx`를 읽기만 하므로 비-const 포인터가 필요하지도 않다 —
`const int* idx = ...` 또는 `auto* idx = ...` 로 충분하다.

---

## physics/part19_ragdoll.md — 확정 2건

### 1. L53-70 vs L72 — 다이어그램의 본 개수가 18인데 "총 15 본"

**[기반영 · 2026-08-09 반영분]** 보고서 권고대로 **다이어그램을 줄이는 쪽**으로 정리됐다 — Chest·LShoulder·RShoulder 3노드가 빠져 현재 mermaid 노드는 정확히 15개, L69 "총 15 본" 및 L376/L423/L587의 15 전제와 일치.

mermaid 노드를 세면 **18개**다 — Pelvis, Spine, Chest, Head, LShoulder, LUpperArm, LLowerArm,
LHand, RShoulder, RUpperArm, RLowerArm, RHand, LThigh, LCalf, LFoot, RThigh, RCalf, RFoot.

**문서의 나머지 수치는 전부 15를 전제로 내부 일관성이 있다** —
L94 "15 본만", L379 $15\times14/2 = 105$, L426 "15 본의 충돌 검사",
L590 $15\times6 = 90$ DoF / 14 joint / 6+42 = 48.
즉 **다이어그램만 3개 많다.** 18로 맞추면 페어 153, maximal DoF 108, joint 17로 전부 바뀌므로
**다이어그램을 15개로 줄이는 쪽이 파급이 적다.**

### 2. L655 — 결론 불릿이 본문의 hedge를 뒤집음

**[기반영 · 2026-08-09 반영분]** 현재 L652 = "**Articulation** — tree 동역학에는 유리하지만 접촉·joint limit·큰 mass ratio의 conditioning은 별도 검증 — Featherstone 기반." — §7.2의 hedge와 일치.

> 7. **Articulation** — **큰 mass ratio 안정** — Featherstone 기반.

본문 §7.2(L568)가 이 주장을 **명시적으로 부정**한다 — "reduced coordinates도 접촉, joint limit,
actuator stiffness, 긴 사슬과 큰 질량비에서 conditioning 문제가 사라지는 것은 아니다.
**'mass ratio 무관'한 해법이 아니라**, tree 구조의 동역학을 더 직접 풀 수 있는 표현이다."

비교표(L579)도 "안정성: tree 동역학에는 유리, **접촉 조건은 별도**"로 hedge했고
L584도 "비교할 가치가 커진다"로만 말한다. **결론 불릿만 단정한다.**

---

## physics/part20_engine_integration.md — 확정 1건

### L349 — 불투명 핸들에 없는 멤버 접근 (컴파일 불가)

**[기반영 · 2026-08-09 반영분]** 현재 L349 = `PlaySound(e.point);` — 보고서가 제안한 그대로.

```cpp
struct CollisionEvent {
    EntityHandle a, b;
    vec3         point, normal;
    float        impulse;
};
...
    if (HasComponent<SoundOnHit>(e.a)) {
        PlaySound(e.a.position);          // ← EntityHandle에 position 없음
    }
```

문서의 다른 곳(L154-160, L178-184)에서도 위치는 `TransformComponent`가 소유하고
`EntityHandle`은 `w.Get<...>(e)` 로만 역참조되는 핸들이라, **핸들이 position을 가진다는
해석은 문서 자체와 모순**된다.
접촉 위치는 이벤트가 이미 들고 있으므로 `PlaySound(e.point);` 가 자연스럽다.

---

## physics/part04_rotational_dynamics.md — 확정 2건

### 1. L163 — 절댓값이 부호 있는 팬 분할을 무너뜨림 (오목 다각형에서 오답)

**[기반영 · 2026-08-09 반영분]** 항별 절댓값이 제거되고 **마지막에 한 번만** 부호 보정하는 방식으로 바뀌었다 — 현재 L163 `numerator += crossZ * term;`, L165 `return (mass / (6.0f * A * 2.0f)) * std::fabs(numerator);`. 보고서가 지목한 면적 계산부(L154 `A = std::fabs(A) * 0.5f;`)와 같은 방식.

L142 수식은 **부호 있는** 외적을 쓴다:

$$I = \frac{\rho}{12}\sum (\vec v_i \times \vec v_{i+1})(|\vec v_i|^2 + \vec v_i\cdot\vec v_{i+1} + |\vec v_{i+1}|^2)$$

그런데 코드는:

```cpp
numerator += std::fabs(crossZ) * term;
```

**항마다 부호를 지운다.** 부호 있는 팬(fan) 분할이 오목부에서 서로 상쇄되는 것이 이 공식의 핵심이므로,
절댓값을 씌우면 **오목 다각형에서 관성모멘트가 과대평가**된다.
§2.3 제목이 "임의 다각형 (2D)"이라 볼록 가정도 명시돼 있지 않다.
(볼록이고 원점이 내부일 때만 모든 `crossZ` 부호가 같아 우연히 맞는다.)

면적 계산부(L149-154)는 `A = fabs(A) * 0.5f` 로 **전체 부호만** 보정해 정확하다. 같은 방식으로
분자도 항별이 아니라 **마지막에 한 번만** 부호를 보정해야 한다.
반환 상수 `mass/(6·A·2)` = $m/(12A)$ 는 $\rho = m/A$ 대입 결과와 일치해 정확하다.

### 2. L502 — "정밀 시뮬레이션만 포함"이라는 배타 서술

**[기반영 · 2026-08-09 반영분]** 현재 L502 = "게임은 *시각적 차이를 무시할 수 있는 경우* 이 항을 기본적으로 생략하기도 하지만, Bullet과 PhysX 같은 엔진은 … 자이로스코픽 항을 옵션으로 제공한다." — 배타 서술 '만' 삭제.

> 정밀 시뮬레이션 (NASA, 항공 시뮬) **만** 이 항 포함.

주요 게임 물리 엔진이 자이로스코픽 항을 **옵션으로 제공**한다 —
Bullet은 `btRigidBody::setFlags()` 의 `BT_ENABLE_GYROSCOPIC_FORCE_EXPLICIT` /
`_IMPLICIT_WORLD` / `_IMPLICIT_BODY`, PhysX는 `PxRigidBodyFlag::eENABLE_GYROSCOPIC_FORCES`.
문서 자신이 L653-654에서 Bullet을 참고문헌으로 인용하고 있어 내부적으로도 어긋난다.
"게임은 기본적으로 끄지만 엔진이 옵션으로 제공한다" 정도가 정확하다.
바로 앞 L500의 "길고 얇은 강체의 tumbling이 이 항 때문"은 정확하다.

---

---

## physics/part13_joints.md — 확정 5건

### 1. L171-172 — Ball joint Jacobian의 **각속도 열 4개가 전부 부호 반대**

**[반영됨 · 2026-08-13]** 3·6번째 열의 부호가 모두 뒤집혔다 — 현재 L171-172 `-1 & 0 & r_{A,y} & 1 & 0 & -r_{B,y}` / `0 &-1 & -r_{A,x} & 0 & 1 & r_{B,x}`. §1.2·§1.5 코드와 부호 규약이 맞는다.

L160이 $C = \vec b^{world} - \vec a^{world}$ 로 정의했으므로, 2D에서 $\omega \times \vec r = (-\omega r_y,\ +\omega r_x)$ 를 대입하면

$$\dot C_x = v_{Bx} - \omega_B r_{B,y} - v_{Ax} + \omega_A r_{A,y}$$
$$\dot C_y = v_{By} + \omega_B r_{B,x} - v_{Ay} - \omega_A r_{A,x}$$

$$J = \begin{pmatrix} -1 & 0 & +r_{A,y} & 1 & 0 & -r_{B,y} \\ 0 & -1 & -r_{A,x} & 0 & 1 & +r_{B,x} \end{pmatrix}$$

그런데 문서는:

$$J = \begin{pmatrix} -1 & 0 & \mathbf{-}r_{A,y} & 1 & 0 & \mathbf{+}r_{B,y} \\ 0 & -1 & \mathbf{+}r_{A,x} & 0 & 1 & \mathbf{-}r_{B,x} \end{pmatrix}$$

**3·6번째 열 네 성분이 모두 반대**다. 선형 항($-1, +1$)은 $C = b - a$ 규약을 따르는데
각속도 항만 뒤집혀 있어 **행렬 내부에서도 일관되지 않는다.**

수치 반증: A 고정, $\vec r_B = (1,0)$, $\omega_B = 1$ 이면 실제 앵커 속도는
$\omega \times \vec r = (0, 1)$ 이라 $\dot C = (0,1)$ 이어야 한다.
문서 $J$의 2행은 $-r_{B,x}\,\omega_B = -1$ 을 준다.

**문서 자신의 다른 부분은 옳다** — §1.2의 $J = (-\hat u,\ -(r_A \times \hat u),\ \hat u,\ (r_B \times \hat u))$ 도,
§1.5 코드의 `Cross2D(rA, P)`를 A에 `-=` / B에 `+=` 하는 부호도 표준 규약과 맞는다.
**§2.2 행렬 하나만 어긋난다.**

**정정**: 3번째·6번째 열의 부호를 모두 뒤집는다.

### 2. L115-116 ↔ L131-132 — `rA`/`rB`가 지역 변수인데 다른 함수에서 사용

**[반영됨 · 2026-08-13]** 보고서의 정정안 그대로 — 구조체(L65)에 `vec2  rA, rB;          // Prepare 에서 계산해 Solve 에서 사용` 가 추가되고, `Prepare` 의 두 줄에서 `vec2` 타입이 빠져 멤버 대입이 됐다.

```cpp
void DistanceJoint::PrepareVelocityConstraint(float dt) {
    vec2 rA = Rotate(localA, bodyA->angle);   // ← 타입 붙은 지역 선언
    vec2 rB = Rotate(localB, bodyB->angle);
    ...
    u = (L > 1e-6f) ? d / L : vec2{0, 0};     // ← 타입 없음 = 멤버
}

void DistanceJoint::SolveVelocityConstraint() {
    vec2 vA = bodyA->linearVelocity + PerpV(rA, bodyA->angularVelocity);   // ← rA 사용
```

같은 블록에서 `u`(L119)·`effMass`·`bias`·`impulse_acc`(L135)는 **타입 없이 대입**해 멤버임을 드러내는데,
`rA`/`rB`만 `vec2`를 붙였다. 어느 쪽이든 결함이다:

- 멤버로 선언돼 있지 않다면 → `Solve`에서 **미선언 식별자, 컴파일 불가**
- 멤버로 선언돼 있다면 → `Prepare`의 지역 선언이 멤버를 **가려 영원히 갱신되지 않는 논리 오류**

**정정**: `Prepare`에서 타입을 빼고 멤버에 대입한다(구조체에 `vec2 rA, rB;` 추가).

### 3. L234 — 3D 축 정렬 조건이 필요충분이 아님

**[반영됨 · 2026-08-13]** 현재 L235 = "축 정렬 = $\hat{n}_A$ 와 $\hat{n}_B$ 가 평행 ⇔ $\hat{a}_1 \cdot \hat{n}_B = 0$ 이고 $\hat{a}_2 \cdot \hat{n}_B = 0$." — 바로 위 2행 제약과 일치.

> 축 정렬 = $\hat n_A$ 와 $\hat n_B$ 가 평행 ⇔ $\hat n_A^{\perp} \cdot \hat n_B = 0$

**3D에서 한 축에 수직인 벡터는 유일하지 않고 평면 전체를 이룬다.**
특정 수직 벡터 하나에 대한 $\hat n_A^{\perp} \cdot \hat n_B = 0$ 은 $\hat n_B$ 를 한 평면 안으로만 묶을 뿐
평행을 함의하지 않으므로 '⇔'가 성립하지 않는다.

**바로 위 L229가 이미 정답을 보여준다** — $C_{axis}$ 를 서로 독립인 두 수직 벡터
$\hat a_1, \hat a_2$ 를 써서 **2행 제약**으로 쓴다. 2행이 필요하다는 사실 자체가
1개 조건으로 부족함을 뜻한다.

**정정**: $\hat a_1 \cdot \hat n_B = 0$ **이고** $\hat a_2 \cdot \hat n_B = 0$.

### 4. L616 — Box2D `b2WheelJoint`가 제거하는 자유도

**[반영됨 · 2026-08-13]** 현재 L617 = `| Wheel | 1 (측면) | spring + motor |`.

> \| Wheel \| **2 (서스펜션)** \| spring + motor \|

`b2WheelJoint`는 공식 설명대로 **"point to line constraint with a line spring"** 이며
**하드하게 제거하는 자유도는 측면(수직축) 병진 1개뿐**이다.
축 방향 병진은 스프링/댐퍼로 **부드럽게** 다루고 회전은 모터로 구동할 뿐 **제거되지 않는다**
— 표의 '추가 기능' 열에 이미 `spring + motor`로 따로 적혀 있다.

같은 표에서 Prismatic이 측면+회전 2개를 제거해 '2'인 것과 대비하면 Wheel은 회전을 제거하지 않으므로 1이다.

**정정**: `| Wheel | 1 (측면) | spring + motor |`

### 5. L562 / L584 — Hz를 "주기"라고 부름

**[반영됨 · 2026-08-13]** **부분 반영.** 용어는 두 곳 모두 정정됐다 — L560 주석 `// Hz — spring 진동수`, L582 "*진동수 1 Hz + 감쇠 0.5*". 다만 **부수 지적(예시 "1 Hz" ↔ 코드 기본값 `frequency = 0.0f` 불일치)은 손대지 않았다 → 이월.** 아래 [이월 목록](#이월--아직-남은-일-1건) 참조.

**[2026-08-13 후속 처리]** 부수 지적도 **닫혔다 (2026-08-14).** 코드 기본값 `frequency = 0.0f` 는 그대로 두고 L582 를 조건부 서술로 고쳤다 — 아래 [이월 목록](#이월--아직-남은-일-1건) 참조.

```cpp
float frequency = 0.0f;      // Hz — spring 주기
```

**Hz(1/s)는 진동수의 단위이고 주기의 단위는 s다.** 변수명도 `frequency`이고,
L571이 `omega = 2 * kPi * frequency`로 각진동수를 만들며,
**§7.4(L588)는 같은 값을 "고유 진동수 $f$"라고 올바르게 부른다.**
L584도 같은 오류를 반복한다 — "*주기 1 Hz + 감쇠 0.5*".

부수적으로 L584의 예시 "1 Hz"는 코드 기본값 `frequency = 0.0f` 와 맞지 않는다
(0이면 L568 `if (frequency > 0)` 때문에 soft 모드 자체가 꺼진다).

---

## physics/part18_fluids_sph.md — 확정 2건

### 1. L466 — 선형 EOS의 음속 식이 $\rho_0$ 배 틀리고 차원도 안 맞음

**[반영됨 · 2026-08-13]** 현재 L467 = "선형 EOS에서는 대략 $c_s^2 = k$, 위 Tait EOS에서는 기준 밀도에서 $c_s^2 = B\gamma/\rho_0$ 다." 보고서가 제시한 두 정정안 중 **§3.2의 EOS는 $p_i = k(\rho_i - \rho_0)$ 로 그대로 두고 음속 식만 고치는 쪽**을 택했다 — 둘은 일관된다.

> 선형 EOS에서는 대략 $c_s^2 = k/\rho_0$

이 문서가 §3.2(L226)에서 정의한 선형 상태 방정식은 $p_i = k(\rho_i - \rho_0)$ 다.
음속의 정의가 $c_s^2 = \partial p / \partial \rho$ 이므로 이 EOS에서는 **$c_s^2 = k$** 다.

**차원 검증**: $[k] = [p]/[\rho] = \dfrac{\mathrm{kg/(m\,s^2)}}{\mathrm{kg/m^3}} = \mathrm{m^2/s^2}$
— **$k$ 자체가 이미 속도의 제곱**이다. 여기서 $\rho_0$ 로 한 번 더 나누면
$\mathrm{m^5/(kg\,s^2)}$ 가 되어 $c_s^2$ 가 될 수 없다.

($c_s^2 = k/\rho_0$ 는 EOS를 $p = k(\rho/\rho_0 - 1)$ 로 써서 $k$ 를 **체적 탄성률**로 둘 때만 성립한다.)

**같은 문장의 Tait 항은 정확하다** — $p = B[(\rho/\rho_0)^\gamma - 1]$ 을 미분하면
$\partial p/\partial \rho = B\gamma(\rho/\rho_0)^{\gamma-1}/\rho_0$ 이고 $\rho = \rho_0$ 에서 $B\gamma/\rho_0$ ✓

**정정**: $c_s^2 = k$ (또는 §3.2의 EOS를 $p_i = k(\rho_i/\rho_0 - 1)$ 로 바꿔 $k$ 를 압력 차원으로 재정의).

### 2. L335-340 ↔ L442 — 압력 항과 점성 항의 **차원이 서로 다름**

**[반영됨 · 2026-08-13]** 보고서의 두 정정안 중 **$\rho_i$ 로 한 번 더 나누는 쪽**을 택했다 — 수식은 `\vec{a}_i^{viscosity} = \frac{\mu}{\rho_i} \sum_j m_j …`, 코드도 `float rhoI = std::max(pi.density, kMinDensity);` 를 추가해 `accel += mu * m * (pj.velocity - pi.velocity) / (rhoI * rhoJ) * lap;` 로 함께 고쳤다. $\mu$ 기호는 동역학 점성 그대로.

L436-439는 두 항을 **같은 가속도 합**에 넣는다:

$$\vec a_i = \vec a_i^{pressure} + \vec a_i^{viscosity} + \vec a_i^{tension} + \vec g$$

그리고 L442가 못박는다 — "pressure / viscosity 식에서 **이미 density 항을 반영했으므로**
적분 단계에서 다시 density로 나누지 않는다."

**압력 항은 맞다.** L252의 쌍대칭 형태
$\vec a_i^{pressure} = -\sum_j m_j\left(\frac{p_i}{\rho_i^2} + \frac{p_j}{\rho_j^2}\right)\nabla W$ 는
$\mathrm{kg} \cdot \frac{\mathrm{m^5}}{\mathrm{kg\,s^2}} \cdot \frac{1}{\mathrm{m^4}} = \mathrm{m/s^2}$ ✓ **가속도**다.

**점성 항은 아니다.** L335-338의
$\vec a_i^{viscosity} = \mu \sum_j m_j \frac{\vec v_j - \vec v_i}{\rho_j}\nabla^2 W$ 에
L340의 정의($\mu$ = 점성 계수, 관례상 **동역학 점성**)를 넣으면

$$\frac{\mathrm{kg}}{\mathrm{m\,s}} \cdot \mathrm{kg} \cdot \frac{\mathrm{m^4}}{\mathrm{kg\,s}} \cdot \frac{1}{\mathrm{m^5}} = \frac{\mathrm{kg}}{\mathrm{m^2 s^2}} = \mathrm{N/m^3}$$

**힘 밀도**다(Müller 2003의 $f_i^{visc}$ 형태 그대로). 가속도가 되려면 $\rho_i$ 로 한 번 더 나눠야 한다.

**정정**: 둘 중 하나 —
$\vec a_i^{viscosity} = \dfrac{\mu}{\rho_i}\sum_j m_j \dfrac{\vec v_j - \vec v_i}{\rho_j}\nabla^2 W$ 로 고치거나,
L340의 기호를 **동점성 계수 $\nu = \mu/\rho$** 로 바꾼다(그러면 $\mathrm{m^2/s}$ 라 차원이 맞는다).
현재는 $\mu$ 라는 기호와 가속도 좌변이 함께 있어 어느 쪽으로도 읽히지 않는다.

---

## 반영 상태 (2026-08-13)

이 절은 위 지적들이 **실제로 본문에 반영됐는지**를 기록한다. 다음에 이 저장소를 여는 사람이
"무엇이 아직 남았는가"를 이 절만 읽고 알 수 있게 하는 것이 목적이다.

판정 근거는 둘뿐이다 —

1. `VeLog_backup_2026-08-11/physics/` 와 현재 `physics/` 의 `diff -ru --strip-trailing-cr` (줄끝 차이 제외),
2. 그 diff 에 안 잡히는 건은 **현재 본문 파일을 직접 열어 확인**.

추정으로 채운 칸은 없다.

### 집계

| 분류 | 건수 | 뜻 |
|---|---|---|
| **반영됨** | 7 | 2026-08-13 배치에서 고쳤다. 백업 대비 diff 에 변경이 남아 있다 |
| **기반영** | 17 | diff 에는 없지만 현재 본문이 이미 정정안대로다 — 2026-08-09 반영분 |
| **기각** | 0 | 이 보고서에는 "검증에서 기각된 것" 류 절이 없다. 오탐으로 분류된 지적이 애초에 없다 |
| **이월** | 1 | 아래 목록 참조. 지적 5(part13)의 **부수 항목**이다 |

지적 총수는 **24건**이다. 표의 합이 25인 것은 part13 지적 5가 **주 지적(용어)은 반영 / 부수 지적(기본값)은 이월** 로 갈렸기 때문이며,
중복 계상이 아니라 한 건이 두 칸에 걸쳐 있다는 뜻이다.

### 파일별

| 파일 | 지적 | 반영됨 | 기반영 | 이월 |
|---|---:|---:|---:|---:|
| part02_integrators.md | 3 | 0 | 3 | 0 |
| part04_rotational_dynamics.md | 2 | 0 | 2 | 0 |
| part05_broad_phase.md | 3 | 0 | 3 | 0 |
| part12_pbd.md | 1 | 0 | 1 | 0 |
| part13_joints.md | 5 | 5 | 0 | 1 (부수) |
| part14_character_controller.md | 4 | 0 | 4 | 0 |
| part17_soft_body.md | 1 | 0 | 1 | 0 |
| part18_fluids_sph.md | 2 | 2 | 0 | 0 |
| part19_ragdoll.md | 2 | 0 | 2 | 0 |
| part20_engine_integration.md | 1 | 0 | 1 | 0 |
| **합계** | **24** | **7** | **17** | **1** |

### 이월 — 아직 남은 일 (1건)

**part13_joints.md §7.3 — soft joint 예시 값이 코드 기본값으로는 재현되지 않는다**
(위 [part13 지적 5](#5-l562--l584--hz를-주기라고-부름)의 부수 항목)

- **현재 상태**: L560 `float frequency = 0.0f;      // Hz — spring 진동수`,
  L568 `if (frequency > 0) {`, L582 "물리적 의미: distance joint 의 *진동수 1 Hz + 감쇠 0.5*."
- **문제**: 기본값이 0 이면 L568 게이트 때문에 soft 모드 자체가 켜지지 않는다.
  독자가 코드를 그대로 돌리면 L582가 말하는 "1 Hz" 거동을 볼 수 없다.
- **남은 선택지 (둘 중 하나)**:
  1. 코드 기본값을 `1.0f` 로 올린다 → 예시와 코드가 바로 맞지만, "기본은 hard mode" 라는 현재 설계 의도가 바뀐다.
  2. 본문을 조건부로 고친다 (예: "`frequency` 를 1 Hz 로 설정하면 …") → 설계 의도는 유지되고 문장만 바뀐다.
- **이번에 손대지 않은 이유**: 어느 쪽을 택하느냐가 **§7.4가 말하는 API 계약(hard/soft 전환 조건)에 대한 설계 판단**이라,
  사실오류 정정 범위를 넘는다. 용어 오류(Hz=진동수)만 고치고 남겼다.

**[2026-08-13 후속 처리]** (2026-08-14) — **닫혔다. 위 선택지 2(본문을 조건부 서술로)를 택했다.**

- **실제 변경 (백업 대비 diff `@@ -581,7 +582,7 @@`)**: L582 의
  "물리적 의미: distance joint 의 *주기 1 Hz + 감쇠 0.5*. 위반량이 *고무처럼 응답*." 이
  "기본값 `frequency = 0` 은 위 게이트에 걸려 soft 모드가 꺼진 상태 — 즉 *rigid 모드* 다. soft 로 쓰려면
  `frequency` 에 양수를 준다. 예를 들어 *진동수 1 Hz + 감쇠 0.5* 로 두면 distance joint 의 위반량이 *고무처럼 응답*." 으로 바뀌었다.
- **선택지 1(코드 기본값을 `1.0f` 로 올린다)은 기각했다.** 근거는 **§7.4** 다 — 그 절이 이미
  "엔진과 버전에 따라 `hertz`, `frequency`, `dampingRatio`, `stiffness` 같은 이름과 **hard mode 전환 조건이 다르므로**,
  직렬화 데이터에는 단위와 변환식을 함께 고정해야 한다" 고 못 박고 있다. 즉 `frequency = 0` = rigid 는
  이 문서가 스스로 선언한 API 관례이고, 기본값을 올리면 §7.3 과 §7.4 가 새로 어긋난다.
- 결과: 독자가 코드를 그대로 돌렸을 때의 거동(rigid)과 본문 서술이 일치하고, "1 Hz" 예시는 조건절 안으로 들어갔다.
  **이 보고서에 남은 본문 작업은 이제 0건이다.**

### 이 보고서가 낡아 있었다는 기록

위 표의 **기반영 17건**은 이 보고서가 지적한 뒤, 그리고 2026-08-13 배치가 시작되기 **전에** 이미 본문에 반영돼 있었다.
2026-08-11 백업에도 이미 정정된 상태로 들어 있어 **diff 에는 전혀 나타나지 않는다.**

그 결과 2026-08-13 배치는 이 17건에 대해 "보고서가 인용한 원문이 파일에 없다" 를 만났고,
그것이 *이미 고쳐진 것* 인지 *지적이 틀린 것* 인지 판단하는 데 시간을 썼다. 이 절은 그 재발을 막기 위한 기록이다.

반영 시점을 **2026-08-09** 로 적은 근거는 두 가지다 — 이 보고서 머리말의 옛 문장이 "확정 17건을 반영했다" 고 적었고 그 건수가 위 집계와 정확히 일치한다는 점, 
그리고 백업(2026-08-11) 안의 관련 10개 파일 mtime 이 모두 **2026-08-09 17:45~17:51** 이라는 점이다.

단 **mtime 은 어느 지적이 반영됐는지까지는 말해주지 않는다** — part13·part18 도 같은 08-09 세션(17:51)에 손댄 흔적이 있지만, 
백업에는 위 7건이 고쳐지지 않은 채 남아 있었다. 즉 08-09 세션은 그 두 파일을 만졌으되 이 7건은 처리하지 않았다. 
어느 건이 언제 반영됐는지의 **확정 근거는 mtime 이 아니라 diff** 다.

다만 **같은 옛 문장이 "part13·part18도 별도로 검수해 수정했다" 고 적은 부분은 사실이 아니었다** —
백업(2026-08-11) 시점의 part13·part18 에는 위 7건이 **그대로 남아 있었고**, 실제 수정은 2026-08-13 09:38 에 이뤄졌다.
머리말은 이에 맞게 고쳤다.

### 이 보고서 밖에서 함께 바뀐 것 (참고, 7 hunk)

2026-08-13 diff 에는 이 보고서의 어떤 지적에도 대응하지 않는 변경이 7 hunk 섞여 있다.
출처는 이 보고서가 아니므로 위 집계에 넣지 않았다. 나중에 "누가 왜 고쳤나" 를 되짚을 때를 위해 목록만 남긴다.

| 파일 | 위치 | 변경 |
|---|---|---|
| part02_integrators.md | §2.2 | semi-implicit Euler 에너지 식을 `$E_{n+1} \approx E_n + O(\Delta t^3)$` 에서 선행항을 드러낸 형태로 교체 + 부호 교대·유계성 설명 추가 |
| part02_integrators.md | §3.2 | "PBD 는 속도를 저장하지 않는다" 는 함의를 철회 — "PBD 는 위치 조작 발상만 물려받고 속도는 상태로 보관" |
| part02_integrators.md | §6.2 해설 | Velocity Verlet 에너지 오차 규모 $10^{-4}$ → $10^{-3}$ ($E_0h^2/4 = 1.25\times10^{-3}$), 표의 값이 작아 보이는 이유를 위상으로 설명 |
| part05_broad_phase.md | §3.3 | SAP 평균 비용 표기를 `$O(N + k)$ — k 는 활성 쌍 수` 에서 "한 step 전체 비용 $O(N+K)$, $K$ = 추출된 겹침 쌍 수(6.1절 기호 규약)" 로 명확화 |
| part12_pbd.md | §1.5 | part02 §3.2 와 같은 정정 — "속도 저장 안 함" 철회, `(predicted - position) / dt` 로 속도 재구성함을 명시 |
| part12_pbd.md | §6.2 | XPBD 의 $\lambda$ 단위($\text{kg}\cdot\text{m}$)가 11편 sequential impulse 의 임펄스 $\lambda$($\text{N}\cdot\text{s}$)와 다름을 밝히는 문단 신설 |
| part19_ragdoll.md | §2 코드 | 캡슐 관성텐서의 지역 변수 `Ixy` → `Itrans` 개명 (`// 횡축 (Ixx = Iyy, 실린더 공식)`) — $I_{xy}$ 가 관성곱으로 오독되던 이름 |

### 재현

```
diff -ru --strip-trailing-cr \
  "C:/Users/REIN/Documents/VeLog_backup_2026-08-11/physics" \
  "C:/Users/REIN/Documents/VeLog/physics" \
  -x "REVIEW_*" -x "REDESIGN_*" -x diagrams -x build
```

`diagrams/` 가 "Only in" 으로 뜨는 것은 백업이 텍스트만 담은 탓이며 변경이 아니다.
