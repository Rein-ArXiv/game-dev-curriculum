# 게임 물리 시리즈 지도

`physics/`는 물리 공식 모음이 아니다. *시간을 이산화한 상태 갱신*, *접촉이라는 불연속 사건*, *제약을 만족시키는 반복 해법*이라는 세 모델을 소유하고, 각 모델이 언제 발산·진동·터널링·비결정으로 실패하는지를 다룬다. 완료 조건은 엔진 API 사용법이 아니라, 시뮬레이션이 이상하게 보일 때 *어느 계층의 어떤 가정이 깨졌는지* 짚을 수 있는 것이다.

기하 정리와 선형대수는 이 축의 소유가 아니다. 이 축은 그 결과를 *solver 입력 계약*으로 바꾸고, *한 프레임 예산 안에서 푸는 대가*를 소유한다.

## 읽는 순서

파일 번호는 대체로 의존 순서지만 그대로 직진하는 사슬은 아니다.

```mermaid
flowchart LR
    A["A 시간과 상태<br/>01 - 04"] --> B["B 충돌 검출<br/>05 - 07"]
    B --> C["C 접촉 응답과 제약 솔버<br/>08 - 11 · 13"]
    C --> D["D 위치 기반 solver와 변형체<br/>12 · 16 - 18"]
    A --> E["E 게임플레이 물리<br/>14 · 15"]
    C --> F["19 Ragdoll"]
    D --> G["20 엔진 통합"]
    E --> G
    F --> G
```

번호순이 읽는 순서와 **어긋나는 지점**은 넷이다.

- **06과 07은 전후 관계가 아니다.** 같은 자리(narrow phase)의 두 대안이다. 볼록 다면체 위주면 06, support mapping 기반 범용 detector가 필요하면 07. 한쪽만 읽어도 C 트랙으로 넘어갈 수 있다.
- **12는 11에서 갈라지는 대안 solver 계열이다.** 11까지 읽고 13(joint)으로 직진해도 되고, 12로 빠져 D 트랙(16~18)으로 가도 된다. 13은 12를 전제하지 않는다.
- **14와 15는 곁가지다.** 캐릭터 컨트롤러와 발사체는 강체 접촉 솔버를 거치지 않고 게임 코드가 직접 적분·sweep한다. 01~02와 sweep 질의만 있으면 08~13 없이 읽힌다. 게임을 만들면서 가장 먼저 필요한 두 편이기도 하다.
- **16~18은 자기들끼리 사슬을 이룬다.** 16(입자·grid hash) → 18(SPH), 12+16 → 17(soft body). 08~11의 강체 접촉 솔버를 전제하지 않는다.

| # | 문서 | 소유하는 모델 |
|---|---|---|
| 01 | [운동학](part01_kinematics.md) | $x/v/a$의 이산화, 고정 dt를 쓰는 근거, dt clamp와 프레임 카운트 기반 시간, NaN·Inf 방어 |
| 02 | [적분기](part02_integrators.md) | Euler·semi-implicit·Verlet·RK4의 유도와 에너지 거동, symplectic의 의미, 상황별 선택, 통일된 한 step 인터페이스 |
| 03 | [강체 동역학](part03_rigid_body_dynamics.md) | 강체 가정, 질량중심, 운동량·충격량, König 자유도 분리, invMass 관례와 motion type, force accumulator, hot/cold 상태 레이아웃 |
| 04 | [회전 동역학](part04_rotational_dynamics.md) | 관성 모멘트·텐서, 평행축 정리, $\omega/L/\tau$, 2D 각도 적분, 쿼터니언 적분·정규화·frame 함정 |
| 05 | [Broad phase](part05_broad_phase.md) | AABB와 fat AABB, brute force·SAP·spatial hash·dynamic AABB tree, 갱신 비용과 캐시 동작, 정적·동적 집합 분리 |
| 06 | [Narrow phase — SAT](part06_narrow_phase_sat.md) | narrow phase 출력 계약, shape adapter, containment MTV, reference·incident feature, clipping, feature ID와 persistent manifold, 실패 상태 |
| 07 | [Narrow phase — GJK/EPA](part07_narrow_phase_gjk.md) | support 계약과 feature 보존, 종료 조건, witness point 복원, EPA polytope topology, simplex cache와 contact cache의 구분, fallback |
| 08 | [충돌 응답](part08_collision_response.md) | penalty의 한계, 임펄스 모델, normal impulse의 유도, 회복 계수의 정의, position correction, Baumgarte bias, 접촉점이 질량중심 밖일 때의 유효 질량 |
| 09 | [마찰과 회복](part09_friction_restitution.md) | Coulomb 모델, 정·동마찰, tangential impulse, friction cone과 pyramid, resting contact의 jitter, 재질의 $(\mu, e)$ 쌍 |
| 10 | [제약과 Lagrangian](part10_constraints_lagrangian.md) | 제약의 분류, 속도 제약과 Jacobian, 미정승수와 $J^T\lambda$, effective mass, LCP·MLCP, 제약 인터페이스 추상 |
| 11 | [Sequential Impulse](part11_sequential_impulse.md) | Gauss-Seidel 반복, velocity·position iteration, 누적 임펄스와 warm start, 수렴 조건과 질량비, iteration 예산, island 분할 |
| 12 | [PBD와 XPBD](part12_pbd.md) | predict-project-correct, 위치 제약 투영, stiffness의 iteration·dt 의존성, compliance, substep과 iteration의 교환 |
| 13 | [Joint](part13_joints.md) | distance·ball·hinge·slider의 제약식과 Jacobian, motor, inequality limit의 상태 분기, soft constraint |
| 14 | [캐릭터 컨트롤러](part14_character_controller.md) | kinematic 캡슐, 컨트롤러가 캐스트에 거는 계약, move-and-slide, step height, slope limit과 ground snap, push out, 점프의 수직 운동 분리 |
| 15 | [발사체와 궤적](part15_projectile_trajectory.md) | 포물선과 항력 모델, 움직이는 표적 예측, 발사각 역산, 발사체 ToI와 sweep, 궤적 시각화, 발사체 수명 관리 |
| 16 | [입자 물리](part16_particle_physics.md) | 입자 SoA 배치, Verlet의 분기 없는 갱신, GPU compute 경계, grid hash 이웃 검색, cloth의 입자·제약 구성, self-collision의 비용 |
| 17 | [Soft body](part17_soft_body.md) | 모델 선택(입자격자·tet mesh·FEM), 거리와 부피 제약의 경합, shape matching, 시뮬 mesh와 렌더 mesh의 결합, 성능 계약 |
| 18 | [유체와 SPH](part18_fluids_sph.md) | smoothing kernel, 밀도와 압력, 점성·표면장력, CFL 시간 제약, PCISPH·IISPH·PBF, 표현 방식 선택 |
| 19 | [Ragdoll](part19_ragdoll.md) | 본 계층과 인체 비례, 본별 형상·관성, 부위별 joint 배치와 해부학적 limit, 본 사이 충돌 그룹, 애니메이션과의 blend, articulation 비교 |
| 20 | [엔진 통합](part20_engine_integration.md) | backend 적합성 계약, 통합 아키텍처, 물리와 렌더의 시간 정합, 물리와 게임 로직의 이벤트·질의, 결정론의 범위, 멀티스레드, 디버그 도구 |

## 개념 소유권 지도

### 한 개념을 여러 편이 쓰는 경우

아래는 **필요한 재사용**이다. 소유 편이 모델을 세우고, 다른 편이 *다른 실패 조건*에 그 모델을 적용한다.

| 개념 | 소유 | 다른 편의 역할 |
|---|---|---|
| 접촉점 속도 $\vec{v}_p=\vec{v}+\vec{\omega}\times\vec{r}$ | 04 | 08은 normal impulse의 입력으로, 09는 접선 방향 결정의 입력으로 쓴다 |
| 적분기 선택 | 02 | 01은 한 줄 식만 훑고, 03은 강체 step 안의 적용 순서, 16은 SIMD 친화성, 18은 CFL이 dt에 거는 상한을 각자의 선택 근거로 삼는다 |
| Effective mass $(JM^{-1}J^T)^{-1}$ | 10 | 08은 normal 방향, 13은 joint 방향, 11은 pre-step 캐싱 대상으로 쓴다 |
| Warm start | 11(솔버 λ 재사용) | 06은 feature ID로 manifold를 매칭하고, 07은 GJK simplex cache로 질의 반복을 줄인다. 07이 simplex cache와 contact cache의 구분을 본문에서 이미 선언한다 |
| 공간 해시 | 05(페어 열거) · 16(반경 이웃 질의) | 질의 형태가 달라 셀 크기 결론도 다르다. 05는 평균 객체 크기, 16은 상호작용 반지름 기준. 18은 16의 구현을 호출만 한다 |
| AoS와 SoA | 16(SIMD·대역폭) | 03은 handle API와 swap-remove라는 별개 실패 조건을 다룬다 |
| Substep과 iteration의 교환 | 12 | 11은 solver 예산 조정, 18은 CFL 하에서의 sub-step으로 적용한다 |
| 결정론 | 20 | 01(고정 dt), 06(접촉점 정렬), 09(마찰 해의 비유일성), 18(adaptive dt), 19(시뮬 결과의 권위 경계)가 각자 계층의 파괴 요인을 소유한다 |
| Self-collision | 16(천) · 17(부피체) · 19(본 그룹) | 세 곳의 회피 메커니즘이 서로 다르다(인접 무시 · 표면 근사 · bit mask) |

### 중복 판정

같은 정의와 유도가 두 번 나오는 곳이다. 아래는 판정과 근거이며 실제 제거는 이 문서가 하지 않는다.

| 중복 | 소유 판정 | 근거 |
|---|---|---|
| 고정 dt + accumulator + spiral of death + alpha 보간 (01 §4.2~4.3 ↔ 20 §4.2~4.5) | 물리 dt를 고정하는 근거·dt clamp·시간 누적 오차는 **01**, 렌더 보간과 외삽·time scale·시간 부채 정책은 **20** | 두 곳의 accumulator 루프가 변수명 외에 동일하다. 최대 step 제한과 `fmod` 부채 폐기까지 같다. 프레임 루프 자체는 물리 축의 소유도 아니다 |
| Island 정의와 union-find 식별 (11 §7.1~7.2 ↔ 20 §7.1~7.2) | **11**. island는 Gauss-Seidel의 순서·수렴 논의의 일부이고 reordering도 11이 소유한다 | union-find 코드와 mermaid 구조가 같다. 20이 고유하게 소유하는 것은 thread 분배, 병렬 결정성, broad phase 병렬, GPU 오프로드다 |
| Sleep 상태기계 (03 §7.4 ↔ 09 §6.4 ↔ 11 §7.4 ↔ 19 §6.6 ↔ 20 §7.5) | body 단위 상태기계와 wake 조건은 **03**, island 단위 게이팅은 **11** | 03과 09의 `sleepTimer` 코드가 임계 상수까지 같다. 09·19·20의 것은 각 문맥의 한 줄 참조로 충분하다 |
| 회복 계수 합성 규칙과 velocity threshold (08 §4.4~4.5 ↔ 09 §5.2·§5.5) | 정의와 물리적 의미는 **08**(normal impulse 유도의 입력), 합성 규칙·재질 값·threshold는 **09**(resting jitter라는 실패 조건의 소유자) | `kRestThreshold` 코드 블록이 상수와 주석까지 같고, "Box2D는 max" 주장과 재질별 값 표가 양쪽에 있다 |
| DistanceJoint 전체 구현 (10 §6.2 ↔ 13 §1.5) | **13** | `PrepareVelocityConstraint`·`SolveVelocityConstraint` 본문이 변수명 외 동일하다. 10은 인터페이스 시그니처만으로 논지가 닫힌다 |
| 누적 임펄스 클램프와 warm start 코드 (10 §7.3~7.5 ↔ 11 §1.4·§2.2·§4.2) | **11** | 클램프 코드 블록이 같다. 10이 소유할 것은 $\lambda_n\ge 0$과 $|\lambda_t|\le\mu\lambda_n$이 상보성 조건이라는 사실이지 그 루프 구현이 아니다 |
| normal impulse 유도 (08 §3.4 ↔ 10 §7.2) | 유도는 **08** | 10 §7.2는 스스로를 "재유도"라 부른다. 다만 *충돌이 일반론의 특수 케이스*라는 것이 10의 논지이므로 대응 확인 자체는 남아야 한다. 삭제가 아니라 축약 대상이다 |
| Position 보정 (08 §5 ↔ 11 §3.3) | 1회 보정의 기본형·slop·percentage·pseudo-velocity 비교는 **08**, 반복 pass와 조기 종료·파라미터 의미는 **11** | 같은 연산이 두 번 코드로 나온다. 경계 자체는 타당하므로 두 번째 코드만 정리 대상이다 |
| Contact 식별자 (06 feature ID ↔ 11 §4.4 `ContactKey`) | **06** | 06이 clip vertex에서 feature ID를 만드는 규칙과 무효화 조건을 소유한다 |
| Joint limit (13 §6 ↔ 19 §4) | inequality 상태 분기·위치 보정·soft limit 메커니즘은 **13**, 해부학적 배치와 값은 **19** | 겹치는 것은 soft limit뿐이다. 다만 두 편의 soft limit 서술이 어긋난다(13은 토크를 각임펄스로 바꿔 적용하라고 못박고, 19는 토크를 그대로 적용한다) |

**통일한 서술 하나.** 04 §8.5는 한때 "충돌점이 멀수록 큰 회전 + 작은 병진"이라 그렸고, 08 §7.5는 "같은 임펄스면 COM 병진 변화 $\Delta\vec{v}=\vec{J}/m$는 동일하고 회전만 달라진다"고 못박는다. $\vec{J}$가 고정이면 후자가 맞으므로 04 §8.5를 "더 큰 회전 + 같은 병진"으로 고쳐 후자로 통일했다. *접촉 제약이 같은 법선 속도 변화를 요구할 때 풀리는 $j$가 달라지는* 경우는 08이 별도로 구분한다.

### 판정 보류

- **충돌 필터(layer·mask).** 19 §5.2가 bit mask 검사 구현을 갖고 있고 20 §3.5가 게임 layer 분류를 갖는데, 필터가 실제로 페어를 걸러야 하는 지점인 broad phase(05)에는 없다. 소유자를 19로 두면 ragdoll 문맥에 갇히고, 05로 옮기면 19의 본 그룹 표가 갈 곳을 잃는다. 근거가 더 필요하다.
- **회전 임펄스 항등식 $\Delta\vec{\omega}=\mathbf{I}^{-1}(\vec{r}\times\vec{J})$** (04 §8.3 ↔ 08 §7.1). 회전 동역학의 항등식으로 보면 04, 충돌 응답의 구성 요소로 보면 08이다. 위의 어긋난 서술과 함께 판정하는 편이 낫다.

## 이 축의 범위와 비범위

### 다른 축이 소유하는 인접 주제

| 인접 주제 | 소유 | 이 축이 남기는 것 |
|---|---|---|
| 벡터·행렬·쿼터니언 대수, 부동소수점 오차 모델 | `math` | 회전 적분의 정규화 빈도, 솔버의 에너지·안정성 |
| 분리축 정리의 축 후보 완전성, GJK·EPA의 기하 유도 | `math` | narrow phase의 출력 계약, containment, manifold, 실패 상태 |
| 공간 분할 자료구조, BVH, 광선 교차 판정 | `math` | 동적 갱신 정책, 페어 추출, 워크로드별 전환점 |
| 형상 캐스트(sweep)의 계약, ToI의 수렴과 종료, conservative advancement | `math` | 캐스트를 몇 번 어떤 순서로 부를지, ToI 이벤트를 solver 예산 안에서 어떻게 처리할지 |
| 프레임 루프 오케스트레이션, 애니메이션 blend, 카메라 | `engine` | 물리 dt의 고정 근거, 물리 상태와 렌더 pose의 경계 |
| AoS/SoA와 SIMD의 일반 비용 모델, 할당·컨테이너 | `parts` | 입자·강체 워크로드에서 그 선택이 바뀌는 조건 |
| GPU 실행 모델, 표면 렌더링(marching cubes, screen-space) | `graphics` | 유체 표현 방식을 고르는 기준과 시뮬 쪽 비용 |
| lockstep·rollback·상태 복제 | `network` | 시뮬레이션이 결정적이기 위한 조건과 그 범위 |
| 경로 탐색, 로컬 회피, 군집 | `ai` | 캐릭터 이동의 충돌 해소와 접지 판정 |

### 아직 소유자가 없는 것

개별 편의 오류가 아니라 **편들 사이의 공백**이다.

- **강체 CCD의 solver 쪽 절반.** 원래 이 항목은 sweep·conservative advancement·speculative contact·sub-step ToI를 한 덩어리로 묶고 있었는데, 그중 *질의* 는 이 축의 것이 아니었다. 캐스트 계약, 터널링의 임계 조건, conservative advancement의 안전 전진과 수렴, ToI의 종료 밴드는 `math`가 소유하도록 정리했다 — 14와 15가 유도 없이 부르던 원시 연산이 그것이다. 남는 공백은 *동역학 쪽 절반* 이다. 일반 강체의 ToI 이벤트를 한 프레임 예산 안에서 어떤 순서로 처리할지(ToI island, 재진입 상한), speculative contact가 만드는 양의 간격 제약을 sequential impulse 루프에 어떻게 끼울지, 그것이 warm start·회복 계수와 어떻게 상호작용하는지는 여전히 어느 편도 유도하지 않는다. 06과 07은 speculative contact를 "구분해야 할 상태"로만 언급하고, 20은 적합성 테스트 항목으로만 다룬다.
- **Swing-twist 분해.** 19 §4.1·§4.4가 `ComputeSwingDirection`, `ComputeTwistAngle`을 정의 없이 호출한다. 13은 1 자유도 각도 한도만 유도한다. 3D cone-twist limit의 제약식과 특이점 처리에 소유자가 없다.
- **충돌 필터의 적용 지점.** 위의 판정 보류와 같은 문제다.
- (확신 낮음) **raycast vehicle.** 13이 차량 휠과 서스펜션을 활용 예로 여러 절에서 언급하지만(들어가며 활용 목록, §3.5, §5.3, §5.6, §7.5, §8.5 표), 바퀴 접촉·하중 이동 모델을 소유하는 편은 없다. 20편 예산 안에서 별도 편이 필요한지는 따로 판단해야 한다.

## 완료 조건

- 시뮬레이션이 발산할 때 적분기·dt·제약 solver·질량비 중 어디가 원인인지 측정으로 좁힐 수 있다.
- 접촉이 흔들릴 때 narrow phase의 manifold, warm start 캐시, position 보정, sleep 임계 중 무엇을 볼지 안다.
- 반복 수와 sub-step 수를 장르 이름이 아니라 최대 침투·잔차·프레임 예산으로 정한다.
- 강체·PBD·kinematic 중 어떤 표현을 쓸지 게임 오브젝트별로 근거를 대고 고른다.
- "결정적"이라는 말의 범위(같은 빌드·같은 thread 수·같은 ISA·직렬화 이후)를 구분해 말한다.
