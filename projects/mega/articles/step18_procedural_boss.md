# 18. 발이 규칙에 닿을 때

## 들어가며

17편까지 보스는 `enemy_boss.ppm` 한 장이었다. 다른 적보다 세 배 크게 블릿되는 32x32 픽셀. 그것이 위에서 내려다본 화면에서 코어를 향해 미끄러진다.

스프라이트는 **위치**를 갖는다. 하지만 **자세**를 갖지 못한다. 그리고 자세가 없으면 물을 수 없는 질문이 있다.

> 이 보스는 지금 어디를 디디고 있는가.

이 편이 그 질문을 만든다. 보스가 몸통 하나와 다리 넷을 가진 다족 개체가 되고, 다리 끝이 매 걸음 지면의 한 점에 닿는다. 그리고 그 점은 화면에만 있는 것이 아니다 — **그 자리를 원점으로 반경 안의 타워가 피해를 받는다.**

세 헤더가 들어온다.

| 헤더 | 하는 일 |
|---|---|
| `engine/skeleton.hpp` | 본 계층. 로컬 TRS 를 부모 사슬을 따라 월드로 합성 |
| `engine/anim.hpp` | 클립 샘플링(`AnimClip::Sample`)과 포즈 블렌딩(`BlendPoses`) |
| `engine/ik.hpp` | 2본 역운동학. 코사인 법칙으로 무릎을 놓는다 |

그리고 새 파일 둘 — `include/game/boss_rig.hpp`(골격·클립·IK 를 게임 좌표로 잇는 자리)와 `games/defense/assets/boss.pose`(저작된 포즈).

도달점의 수치.

- 호스트가 계산한 스톰프 원점과 **스냅샷만 보고 다시 푼** 발 위치가 **비트 동일**(6회 대조). 와이어 왕복(1/8px 양자화) 뒤에도 0.5px 안.
- 제품 셀프테스트에서 보스가 **2회** 발을 디디고, 스톰프 배선(`sim.stomp`)이 1377틱 중 **432틱** 열린다.
- 보스 40마리 행렬에서 스톰프 **40회**, 타워 **8 → 7**(하나가 부서짐), 최저 체력 **12.0**.
- 한 걸음을 16등분한 64개 다리 표본 중 **9개**가 도달 불가로 곧게 뻗는다 — IK 의 그 가지가 죽은 코드가 아니다.
- 자세 하나 푸는 비용 **1.04µs**, 그중 **54%** 가 클립 샘플링·블렌딩.

---

## 목차
1. [스프라이트가 표현하지 못하는 것](#1-스프라이트가-표현하지-못하는-것)
2. [골격 — 부모는 언제나 자식보다 앞선다](#2-골격--부모는-언제나-자식보다-앞선다)
3. [걸음 위상을 위치의 함수로](#3-걸음-위상을-위치의-함수로)
4. [클립이 방향을, 코드가 보폭을](#4-클립이-방향을-코드가-보폭을)
5. [2본 IK — 닿음과 닿지 못함](#5-2본-ik--닿음과-닿지-못함)
6. [스톰프 — IK 출력이 규칙의 입력](#6-스톰프--ik-출력이-규칙의-입력)
7. [타워에 체력이 생긴다](#7-타워에-체력이-생긴다)
8. [boss.pose — 신뢰 경계 밖 입력](#8-bosspose--신뢰-경계-밖-입력)
9. [표시: 스프라이트 팔은 남는다](#9-표시-스프라이트-팔은-남는다)
10. [설계에서 조정한 것](#10-설계에서-조정한-것)
11. [변이 셋](#11-변이-셋)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 스프라이트가 표현하지 못하는 것

이 게임의 적은 2편부터 "경로 위를 나아가는 점"이었다. 그 표현으로 부족했던 적이 없다 — 타워는 거리를 재고, 발사체는 중심을 겨누고, 렌더는 그 좌표에 그림을 찍는다.

보스에게 다리를 주는 이유는 그림이 아니다. **점은 접촉면을 갖지 않기 때문**이다.

점으로 표현된 개체가 지형이나 건물과 상호작용하는 방법은 하나뿐이다 — 중심에서 반경 R 안. 그 규칙은 대칭이고, 방향이 없고, 개체의 자세와 무관하다. 스톰프를 몸통 중심으로 구현하면 그것은 "보스 주변이 아프다"이고, 15편의 발사체 스플래시와 같은 모양이다.

발을 원점으로 삼으면 달라진다. 발은 몸통에서 **떨어져 있고**(측정: 칸 크기의 0.3배 이상), 걸음마다 **다른 다리**가 디디며, 진행 방향에 따라 **앞뒤가 바뀐다**. 같은 자리에 선 보스라도 어느 발이 내려오느냐에 따라 다른 타워가 맞는다.

그 규칙이 성립하려면 발의 좌표가 필요하고, 발의 좌표를 얻으려면 자세가 필요하다.

## 2. 골격 — 부모는 언제나 자식보다 앞선다

몸통 하나 + 다리 넷 x (고관절·무릎·발) = **13본**.

<!-- snippet: include/game/boss_rig.hpp@boss-skeleton -->
```cpp
inline engine::Skeleton MakeBossSkeleton() {
    engine::Skeleton skeleton;
    const int body = skeleton.AddBone(-1, math::Transform{});
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        math::Transform hipBind;
        hipBind.position = {kBossHipX[leg], kBossHipY[leg], 0.0f};
        const int hip = skeleton.AddBone(body, hipBind);
        math::Transform thigh;
        thigh.position = {0.0f, -kBossThigh, 0.0f};
        const int knee = skeleton.AddBone(hip, thigh);
        math::Transform shin;
        shin.position = {0.0f, -kBossShin, 0.0f};
        skeleton.AddBone(knee, shin);
    }
    return skeleton;
}
```

`Skeleton::ComputeWorld` 는 본을 **한 번의 전방 순회**로 접는다.

```cpp
Pose ComputeWorld(const Pose& localPose) const {
    Pose world(parents_.size());
    for (size_t i = 0; i < parents_.size(); ++i) {
        int p = parents_[i];
        world[i] = (p < 0) ? localPose[i] : world[p] * localPose[i];   // 부모 ∘ 로컬
    }
    return world;
}
```

`world[p]` 를 읽으려면 `p < i` 여야 한다. 원래 헤더는 그것을 **주석의 가정**으로 두었다("본은 부모가 자식보다 먼저 추가된다고 가정한다"). 가정이 깨지면 예외도 크래시도 없이 **기본 구성인 변환**으로 자식이 합성되고, 그 결과는 그럴듯하게 틀린 포즈다.

그래서 가정을 검사로 바꿨다.

<!-- snippet: include/engine/skeleton.hpp@add-bone -->
```cpp
int AddBone(int parent, const math::Transform& bindLocal) {
    if (parent >= (int)parents_.size()) return -1;   // 아직 없는 본을 부모로
    if (parent < -1) return -1;
    parents_.push_back(parent);
    bind_.push_back(bindLocal);
    return (int)parents_.size() - 1;
}
```

거절된 본은 추가되지 않고 `-1` 이 돌아온다. 부분적으로 추가하지 않는 것이 중요하다 — 인덱스가 밀리면 그 뒤의 모든 부모 참조가 다른 본을 가리킨다.

**좌표계.** 보스 로컬 공간은 `+x` = 오른쪽, `+y` = 진행 방향이다. 고관절은 `(±0.50, ±0.34)`, 허벅지와 정강이는 각각 `-y` 로 0.42. 전부 **칸 크기의 배수**라, 기본 맵(칸 30px)과 절차 맵(칸 10px) 어디서도 같은 비율의 보스가 나온다.

## 3. 걸음 위상을 위치의 함수로

보스가 걷는다는 것은 다리가 번갈아 앞뒤로 흔들린다는 뜻이고, 그러려면 "지금 걸음의 어디쯤인가"라는 값이 필요하다.

가장 자연스러운 구현은 시뮬 타이머다 — 매 틱 `phase += speed * dt`. 그런데 그렇게 하면 **클라이언트가 같은 자세를 그릴 수 없다.**

이 게임의 협동 구조를 다시 보면 이유가 분명하다(4·5편). 호스트는 권위 시뮬을 돌리고, 3틱마다 스냅샷을 보낸다. 스냅샷에 담기는 적 정보는 `id`·`pos`·`hp`·`maxHp`·`type` 다섯이다. 걸음 위상은 거기 없다.

넣으면 되지 않는가. 넣을 수 없다. 스냅샷 예산은 이미 `kSnapshotMaxBytes = 1198` 로 꽉 차 있고(13편 §6), 적 하나당 1바이트만 늘려도 64마리에서 64바이트가 더 필요하다. 무엇보다 **와이어 포맷을 바꾸면 협동 테스트가 감시하는 계약이 깨진다**.

그래서 위상을 위치의 함수로 정의했다.

<!-- snippet: include/game/boss_rig.hpp@boss-phase -->
```cpp
inline float BossPhase(const math::Vec2& pos) {
    return (pos.x + pos.y) * (1.0f / kBossStride);
}
```

`x + y` 를 쓰는 이유는 **맵의 어느 방향으로 걸어도 변하는 스칼라**가 필요하기 때문이다. `x` 만 쓰면 세로로만 걷는 구간에서 위상이 멈추고 보스가 미끄러진다. 대각선으로 갈 때 위상이 √2배 빨리 도는 것은 사실이지만, 그것은 "대각선으로 걸을 때 보폭이 커 보인다"일 뿐 규칙을 깨지 않는다.

그리고 **바닥값이 곧 디딤 횟수**다.

```cpp
static uint32_t PlantCount(const math::Vec2& at) {
    const float phase = std::floor(BossPhase(at));
    return phase > 0.0f ? static_cast<uint32_t>(phase) : 0u;
}
```

`kBossStride = 42.0f` 픽셀마다 한 번. 어느 다리가 디디는가는 `plants % 4` 이므로 그것도 위치의 함수다. 호스트와 클라이언트가 각각 계산해도 같은 답이 나온다.

시뮬이 저장하는 것은 **직전 틱의 `plants` 하나**뿐이다(`EnemyRig::plants`). 그 값이 필요한 이유는 "지금 몇 번째 걸음인가"가 아니라 **"이번 틱에 걸음이 넘어갔는가"** — 즉 에지 검출이다. 17편이 나눈 컴포넌트 중 리그가 가장 작은 것(12바이트)이 그래서다.

## 4. 클립이 방향을, 코드가 보폭을

`engine/anim.hpp` 의 등재 사유는 이랬다.

> 에셋 방침이 키프레임 클립을 0개로 못박았다(리깅 0시간, 포즈는 전부 절차 IK). 샘플링·블렌딩할 클립이 만들어지지 않으므로 제품 편입 계획 없음.

그 방침은 폐기된 재설계안의 것이라 더는 근거가 아니다. 하지만 "리깅에 시간을 쓰지 않는다"는 실질적 제약은 남아 있다 — 13본 x 11개 부동소수 x 키프레임 수를 손으로 저작할 수는 없다.

그래서 파일이 저작하는 것을 **여섯 자유도**로 줄였다: 몸통 들림, 몸통 롤, 고관절 요 넷.

```
key 0.50 -0.050   4.0  -21.0   49.0  -49.0   21.0
```

로더가 이 한 줄을 13본짜리 `engine::Pose` 로 편다. 마디 길이와 부모 사슬은 골격이 소유하므로 파일이 만들 수 있는 최악은 "이상한 걸음"이지 "깨진 골격"이 아니다.

**두 클립이 한 포즈로 합쳐진다.**

<!-- snippet: include/game/boss_rig.hpp@boss-sample -->
```cpp
engine::Pose SampleLocal(float frac, float attackWeight) const {
    const engine::Pose stride = walk.Sample(frac * walk.Duration());
    const engine::Pose slam   = attack.Sample(frac * attack.Duration());
    return engine::BlendPoses(stride, slam, math::Saturate(attackWeight));
}
```

`attackWeight` 는 걸음의 마지막 30% 구간에서 0 → 1 로 올라간다(`BossAttackWeight`). 즉 디디기 직전에 몸을 감았다가 디디는 순간 0으로 돌아온다. 이 값도 `frac` 의 함수이므로 클라이언트가 재현한다.

**그리고 클립과 코드가 곱해진다.** 이것이 이 편에서 가장 결정하기 어려웠던 지점이다.

클립이 다리 자세를 전부 정하면 IK 가 할 일이 없다(무릎 각도까지 키프레임에 있으므로). 반대로 IK 가 발 위치를 전부 정하면 클립의 고관절 요는 죽은 값이 된다 — 무릎과 발은 IK 가 덮어쓰므로 화면에 아무 영향이 없다.

해법은 **IK 목표를 고관절 로컬 공간에 두는 것**이다.

```cpp
const float swing = BossLegSwing(out.frac, leg, out.plants);
const math::Vec3 targetLocal{0.0f, -(kBossStance + swing), 0.0f};
const math::Vec3 target = hip.TransformPoint(targetLocal);
```

`targetLocal` 은 "고관절에서 뒤로 (0.55 + swing) 만큼" 이다. 그 점을 고관절의 **월드 변환**으로 올리면, 클립이 정한 고관절 회전이 그대로 목표를 돌린다. 클립은 다리가 **어느 쪽을 가리키는가**를, 코드는 **얼마나 내딛는가**를 정하고 두 값이 곱해진다.

`tests/test_boss_rig.cpp` 가 그 곱셈을 단언한다.

```cpp
std::vector<BossPoseKey> keys = DefaultWalkKeys();
for (BossPoseKey& key : keys) key.hip[0] += 40.0f;   // 앞왼 다리만 크게 벌린다
tilted.walk = ClipFromKeys(keys);
// ...
CHECK(!Near(a.legs[0].foot.x, b.legs[0].foot.x, 1e-2f) || ...);   // 그 발이 움직였다
CHECK(Near(a.legs[2].foot.x, b.legs[2].foot.x, 1e-3f));           // 건드리지 않은 발은 그대로
```

## 5. 2본 IK — 닿음과 닿지 못함

`SolveTwoBone2D` 는 코사인 법칙으로 닫힌 해를 낸다. 반복이 없고 수렴 조건도 없다 — 2본이라 삼각형 하나가 답을 전부 결정하기 때문이다.

중요한 것은 그 함수의 **세 가지 출구**다.

```cpp
if (d < 1e-5f)                { /* 목표가 루트와 겹침 → 방향이 정의되지 않음 */ }
if (d >= reach)               { /* 닿지 못함 → 목표 방향으로 곧게 뻗는다 */ }
if (d <= std::fabs(l1 - l2))  { /* 너무 가까움 → 접힌다 */ }
```

이 게임에서 두 번째 출구가 **실제로 열린다.** 기본 자세 거리 0.55에 진폭 0.32를 더하면 최대 0.87이고, 뻗은 다리 길이는 0.84다. 즉 **가장 앞으로 내딛는 순간의 다리는 목표에 닿지 못한다.**

의도한 것이다. 그 순간이 곧 발을 디디는 순간이고, 다리가 완전히 뻗은 자세로 착지하는 편이 화면에서 읽힌다. 그리고 그 사실이 코드가 아니라 **측정으로** 확인된다.

```
  한 걸음 64 표본 중 뻗은 다리 9개
```

한 걸음을 16등분한 64개(16 x 4다리) 표본 중 9개가 도달 불가 가지를 탄다. 이 수가 0이면 그 가지는 이 게임에서 죽은 코드이고, 64면 IK 가 아무 일도 하지 않는다는 뜻이다.

**닿지 못할 때 무엇을 하는가가 게임 규칙이다.** 도달 불가에서 `end = target` 을 돌려주면(즉 다리가 늘어나면) 발이 몸에서 임의로 멀어지고, 그 자리를 원점으로 하는 스톰프는 보스와 무관한 곳의 타워를 때린다. 그래서 테스트는 발 위치가 아니라 **마디 길이의 보존**을 단언한다.

```cpp
CHECK(Near(math::Length(solved.joint - root), l1, 1e-3f));
CHECK(Near(math::Length(solved.end - root), l1 + l2, 1e-3f));
```

## 6. 스톰프 — IK 출력이 규칙의 입력

<!-- snippet: include/game/boss_rig.hpp@boss-frame -->
```cpp
inline BossFrame SolveBossFrame(const BossPoses& poses, const math::Vec2& pos,
                                const math::Vec2& corePos, float scale) {
    BossFrame out;
    const float phase = BossPhase(pos);
    const float base = std::floor(phase);
    out.plants = base > 0.0f ? static_cast<uint32_t>(base) : 0u;
    out.frac = math::Saturate(phase - base);
    out.plantedLeg = static_cast<int>(out.plants % static_cast<uint32_t>(kBossLegCount));
    out.attackWeight = BossAttackWeight(out.frac);

    const engine::Pose local = poses.SampleLocal(out.frac, out.attackWeight);
    const engine::Pose world = poses.skeleton.ComputeWorld(local);

    math::Vec2 forward = corePos - pos;
    const float span = math::Length(forward);
    out.forward = span > 1e-4f ? forward * (1.0f / span) : math::Vec2{0.0f, 1.0f};
    const math::Vec2 right{out.forward.y, -out.forward.x};
    const math::Vec2 origin = pos;
    auto toWorld = [&](const math::Vec2& p) {
        return origin + right * (p.x * scale) + out.forward * (p.y * scale);
    };

    out.body = toWorld({world[0].position.x, world[0].position.y});
    for (int leg = 0; leg < kBossLegCount; ++leg) {
        const int hipBone = 1 + leg * 3;
        const math::Transform& hip = world[static_cast<size_t>(hipBone)];
        const math::Vec2 hipLocal{hip.position.x, hip.position.y};
        // ...
        const float swing = BossLegSwing(out.frac, leg, out.plants);
        const math::Vec3 targetLocal{0.0f, -(kBossStance + swing), 0.0f};
        const math::Vec3 target = hip.TransformPoint(targetLocal);
        const engine::IkResult solved = engine::SolveTwoBone2D(
            hipLocal, kBossThigh, kBossShin, {target.x, target.y}, hipLocal.x < 0.0f);
        out.legs[leg].hip = toWorld(hipLocal);
        out.legs[leg].knee = toWorld(solved.joint);
        out.legs[leg].foot = toWorld(solved.end);
        out.legs[leg].reachable = solved.reachable;
    }
    return out;
}

// 스톰프의 원점. 호스트는 시뮬에서, 클라이언트는 스냅샷에서 이 함수를 부른다.
inline math::Vec2 BossPlantedFoot(const BossPoses& poses, const math::Vec2& pos,
                                  const math::Vec2& corePos, float scale) {
    const BossFrame frame = SolveBossFrame(poses, pos, corePos, scale);
    return frame.legs[frame.plantedLeg].foot;
}
```

입력이 넷뿐이라는 점을 눈여겨볼 만하다 — 포즈 묶음, 보스 위치, 코어 위치, 칸 크기. **시계도 틱 카운터도 시뮬 전용 상태도 없다.** 그래서 이 함수는 스냅샷을 가진 누구든 부를 수 있다.

진행 방향은 코어 쪽으로 잡았다(`corePos - pos`). 경로를 따라가는 실제 이동 방향이 아니라는 점에서 근사인데, 클라이언트가 경로 배열을 정확히 재현한다는 보장이 없으므로(발파와 잔해가 지형을 바꾼다) **스냅샷 안에 확실히 있는 것**으로 방향을 정의해야 했다. 코어 위치는 맵에서 나오고 맵은 양쪽이 같다.

그리고 시뮬 쪽:

<!-- snippet: include/game/game.hpp@stomp-system -->
```cpp
void StompSystem() {
    const size_t count = enemies_.Size();
    if (count == 0) return;
    // ...
    if (enemies_.RigRows() == 0) return;
    MEGA_PROFILE_SCOPE("sim.stomp");
    const float scale = BossScale();
    const float radius = kStompRadiusCells * scale;
    const float radiusSq = radius * radius;
    bool destroyed = false;
    for (size_t i = 0; i < count; ++i) {
        if (!enemies_.Alive(i)) continue;
        if (!EnemyStore::HasRig(enemies_.Type(i))) continue;
        const math::Vec2 at = enemies_.Pos(i);
        const uint32_t plants = PlantCount(at);
        if (plants == enemies_.Plants(i)) continue;      // 아직 같은 걸음 안
        const math::Vec2 foot = BossPlantedFoot(bossPoses_, at, corePos_, scale);
        enemies_.SetStomp(i, plants, foot);
        ++stomps_;
        Emit(SoundKind::Stomp, foot);
        if (towers_.empty()) continue;
        for (Tower& tower : towers_) {
            if (math::LengthSq(tower.pos - foot) > radiusSq) continue;
            tower.hp -= kStompDamage;
            if (tower.hp <= 0.0f) destroyed = true;
        }
    }
    if (destroyed) RemoveDestroyedTowers();
}
```

`GameWorld::Step` 의 여덟 번째 시스템이고, 자리는 발사체 뒤·정리 앞이다. 발사체 뒤인 것은 보스가 이번 틱의 최종 위치에 있어야 발이 맞기 때문이고, 정리 앞인 것은 부서진 타워가 같은 틱의 잔해 시스템에 입력으로 들어가야 하기 때문이다(16편의 고리 — 철거가 잔해를 남기고 잔해가 칸을 막는다 — 에 스톰프가 새 입구를 낸다).

**행 번호 오름차순 스캔**인 것도 의도다. 17편의 `ForEachRigChunk` 로 리그 청크만 훑을 수도 있지만, 그러면 피해 적용 순서가 archetype 안의 자리 순서가 되고 그 순서는 안정적이지 않다. 타워 체력은 부동소수 감산으로 깎이므로 순서가 결과를 바꾼다. 청크 순회는 **판정**(`RigRows() == 0`)에만 쓴다.

**호스트와 클라이언트가 같은 발을 본다.** `tests/test_stomp.cpp` 가 그것을 매 스톰프마다 확인한다.

```cpp
const math::Vec2 hostFoot = fixture.world.Enemies().LastStomp(row);
const wire::Snapshot view = CaptureView(fixture.world, tick);
const math::Vec2 replayed = BossPlantedFoot(poses, boss->pos, map.Path().back(), scale);
CHECK(hostFoot.x == replayed.x);      // 값 단계에서는 비트 동일
CHECK(hostFoot.y == replayed.y);
```

그리고 실제 와이어를 건너면 위치가 1/8 픽셀로 양자화된다. 그 오차가 발까지 얼마나 번지는지도 함께 잰다.

```cpp
CHECK(Near(overWire.x, hostFoot.x, 0.5f));
```

0.5픽셀 안. 오차가 사라지지도, 커지지도 않는다 — IK 가 위치에 대해 립시츠 연속이라는 사실의 실측이다.

## 7. 타워에 체력이 생긴다

스톰프가 무엇을 하는가. 이 게임에서 타워를 없애는 유일한 방법은 17편까지 플레이어의 철거 명령이었다 — **적이 건물을 건드리지 못했기 때문**이다.

```cpp
float      hp = 0, maxHp = 0;
```

`Tower` 에 두 필드가 생긴다. 갓 지은 타워는 `kTowerBaseHp = 180`, 강화 한 단계마다 `kTowerTierHp = 60` 이 늘고 체력이 가득 찬다(강화가 수리를 겸한다). 스톰프 한 번이 `kStompDamage = 12`, 반경은 칸 크기의 1.5배.

부서진 타워를 걷어내는 것은 판매(`SellTower`)와 같은 규칙이다.

```cpp
void RemoveDestroyedTowers() {
    for (size_t i = towers_.size(); i-- > 0;) {
        if (towers_[i].hp > 0.0f) continue;
        const math::Vec2 where = towers_[i].pos;
        towers_[i] = towers_.back();
        towers_.pop_back();
        OccupyTowerCell(where, false);
        if (mapEnabled_ && map_.Generated()) {
            debris_.SpawnBurst(where, 5, rng_);
            debrisDirty_ = true;
        }
    }
}
```

뒤에서부터 훑는 이유는 swap-remove 가 앞쪽 인덱스를 건드리지 않게 하기 위해서다. `OccupyTowerCell(where, false)` 가 칸을 비우고 경로를 다시 풀므로, **타워가 부서지면 미로가 열린다** — 16편의 고리에 스톰프가 들어간 자리다.

**와이어는 건드리지 않았다.** `wire::TowerState` 에 체력이 없으므로 클라이언트는 타워 체력을 모르고, 부서진 타워는 그냥 스냅샷에서 사라진다. 체력 바를 그리려면 와이어를 바꿔야 하고, 그 대가가 지금 이 기능의 가치보다 크다.

**상태 해시에는 들어간다.**

```cpp
mixFloat(t.hp);
// ...
mix(stomps_);
```

타워 체력과 누적 디딤 횟수가 해시에 섞이므로, 17편의 세 저장 A/B 는 "리그 열을 아무도 건드리지 않았다"가 아니라 "**보스가 실제로 발을 디뎠고 그 결과가 같았다**"를 단언한다.

## 8. boss.pose — 신뢰 경계 밖 입력

`assets/boss.pose` 는 재빌드 없이 교체할 수 있다고 광고되는 파일이다. 그 말은 곧 **누구든 아무 바이트나 넣을 수 있다**는 뜻이고, `level.txt`·`waves.txt`·`balance.script` 와 같은 규칙을 적용해야 한다는 뜻이다(3편·13편).

<!-- snippet: games/defense/assets/boss.pose -->
```
# boss.pose — 보스의 저작 포즈. 재빌드 없이 교체할 수 있다.
#
# 이 파일이 정하는 것은 여섯 자유도뿐이다: 몸통 들림 · 몸통 롤 · 고관절 요 넷.
# 다리 마디 길이와 부모 사슬은 골격(코드)이 소유하고 여기서 건드릴 수 없다.
# 그래서 이 파일로 표현할 수 있는 최악은 "이상한 걸음"이지 "깨진 골격"이 아니다.
#
# 고관절 요는 다리가 **어느 쪽을 가리키는가**를 정한다. 얼마나 내딛는가(보폭)는
# 코드가 정하고, IK 목표가 고관절 로컬 공간에 놓이므로 둘이 곱해진다.
#
# 형식: key <시각> <몸통 들림> <몸통 롤(도)> <고관절0> <고관절1> <고관절2> <고관절3>
#   고관절 0=앞왼 1=앞오른 2=뒤왼 3=뒤오른. 음수 = 왼쪽으로 벌림.
#   한 줄이라도 거부되면 파일 전체가 버려지고 내장 클립으로 돌아간다.
bones 13

# 걷기: 대각선 두 쌍이 번갈아 벌어진다(곤충 보행).
clip walk
key 0.00  0.000   0.0  -35.0   35.0  -35.0   35.0
key 0.25 -0.030   3.0  -24.0   46.0  -46.0   24.0
key 0.50 -0.050   4.0  -21.0   49.0  -49.0   21.0
key 0.75 -0.030   1.5  -30.0   40.0  -40.0   30.0
key 1.00  0.000   0.0  -35.0   35.0  -35.0   35.0

# 내려찍기: 앞다리 둘을 크게 벌려 감았다가 몸을 낮춘다.
clip attack
key 0.00  0.000   0.0  -35.0   35.0  -35.0   35.0
key 0.50 -0.160  -9.0  -52.0   52.0  -28.0   28.0
key 1.00  0.000   0.0  -35.0   35.0  -35.0   35.0
```

파서는 **원자적 폴백**이다.

<!-- snippet: include/game/boss_rig.hpp@pose-parse -->
```cpp
inline bool ParseBossPoses(const std::string& text, BossPoses& out, std::string* diagnostic) {
    auto fail = [&](const std::string& why) {
        if (diagnostic) *diagnostic = why;
        return false;
    };
    // ...
        } else if (word == "key") {
            if (!current) return fail("clip 선언보다 먼저 나온 key");
            BossPoseKey key;
            if (!(parser >> key.time >> key.lift >> key.roll)) return fail("key 필드가 모자라다");
            for (int leg = 0; leg < kBossLegCount; ++leg)
                if (!(parser >> key.hip[leg])) return fail("고관절 각이 모자라다");
            std::string trailing;
            if (parser >> trailing) return fail("key 뒤에 남은 토큰: " + trailing);
            // ...
            if (!current->empty() && !(key.time > current->back().time))
                return fail("key 시각이 단조 증가가 아니다");
    // ...
    out.skeleton = MakeBossSkeleton();
    out.walk = ClipFromKeys(walk);
    out.attack = ClipFromKeys(attack);
    return true;
}
```

**부분 반영을 허용하지 않는 이유는 진단 가능성이다.** 걷기는 파일이고 공격은 내장인 포즈는 아무도 저작한 적이 없는 조합이고, 화면에서 이상한 것을 본 사람이 그 원인을 파일에서도 코드에서도 찾을 수 없다.

검증 항목 중 하나는 다른 것들과 성격이 다르다.

```cpp
if (!current->empty() && !(key.time > current->back().time))
    return fail("key 시각이 단조 증가가 아니다");
```

범위 검사(들림 ±1, 롤 ±90°, 요 ±180°)는 "말이 되는 값인가"를 묻지만, 이것은 **`AnimClip::Sample` 의 전제**를 지킨다. `Sample` 은 `times_` 가 오름차순이라고 믿고 앞에서부터 훑는다.

```cpp
size_t i = 1;
while (i < times_.size() && times_[i] < t) ++i;
```

비단조 키를 넣으면 예외도 크래시도 없다. 뒤에 온 작은 시각의 키가 **영원히 도달 불가**가 되어 조용히 무시될 뿐이다. 자료구조의 전제를 로더가 지키지 않으면 그 전제는 아무도 지키지 않는다.

`tests/test_pose_content.cpp` 가 아홉 가지 거부를 확인한다.

```
  거부(pose_nonmonotonic.pose): ... key 시각이 단조 증가가 아니다
  거부(pose_bones.pose): ... bones 선언이 골격의 본 수와 다르다
  거부(pose_range.pose): ... 몸통 들림이 범위 밖이다
  거부(pose_short.pose): ... 고관절 각이 모자라다
  거부(pose_long.pose): ... key 뒤에 남은 토큰: 99
  거부(pose_nobones.pose): ... bones 선언이 없다
  거부(pose_orphan.pose): ... clip 선언보다 먼저 나온 key
  거부(pose_unknown.pose): ... 알 수 없는 지시자: spine
  거부(pose_oneclip.pose): ... 클립마다 키가 둘 이상이어야 한다
```

그리고 배포되는 파일이 내장 클립과 **다르다**는 것도 함께 확인한다. 같으면 로더가 실제로 쓰이는지 알 수 없다.

```cpp
CHECK(poses.walk.KeyCount() != DefaultWalkKeys().size());   // 파일 5키 vs 내장 3키
```

## 9. 표시: 스프라이트 팔은 남는다

`boss_render=sprite|rig` 스위치를 뒀다. 기본값은 `sprite` — 스위치가 생기기 전의 동작이라는 8편의 규약이다.

<!-- snippet: include/game/defense_view.hpp@draw-boss-rig -->
```cpp
inline void DrawBossRig(render::Framebuffer& framebuffer, const DefenseCamera& camera,
                        const BossFrame& frame, float scale) {
    auto screen = [&camera](const math::Vec2& world) {
        const math::Vec2 p = camera.Project(world);
        return math::Vec2{p.x, p.y};
    };
    auto ix = [](float v) { return static_cast<int>(std::lround(v)); };
    const render::Color limb{170, 60, 210, 255};
    const render::Color shell{205, 55, 190, 255};

    for (int leg = 0; leg < kBossLegCount; ++leg) {
        const math::Vec2 hip = screen(frame.legs[leg].hip);
        const math::Vec2 knee = screen(frame.legs[leg].knee);
        const math::Vec2 foot = screen(frame.legs[leg].foot);
        render::DrawLine(framebuffer, ix(hip.x), ix(hip.y), ix(knee.x), ix(knee.y), limb);
        render::DrawLine(framebuffer, ix(knee.x), ix(knee.y), ix(foot.x), ix(foot.y), limb);
        // 디딘 다리의 발만 채운다. 나머지는 떠 있는 발이라 점만 남긴다.
        const int radius = leg == frame.plantedLeg ? 3 : 1;
        render::FillCircle(framebuffer, ix(foot.x), ix(foot.y), radius,
                           frame.legs[leg].reachable ? limb : shell);
    }
    // ...
}
```

**표시만 바꾼다.** 스톰프는 어느 팔에서도 똑같이 돈다 — 그것이 시뮬이기 때문이다. 스위치를 켜고 끄면서 셀프테스트를 돌리면 `stomps=2` 가 두 팔 모두에서 나온다.

디딘 다리의 발만 반지름 3으로 채우는 것은 장식이 아니다. **그 점이 그 틱의 스톰프 원점**이라, 화면에서 본 자리와 피해를 받은 자리가 같다. 도달 불가로 뻗은 발은 다른 색으로 찍어 §5의 두 번째 출구가 눈에 보이게 했다.

비용:

```
  SolveBossFrame=1.039us/회 (그중 클립 샘플+블렌드 0.558us, 54%)
```

프레임당 보스 한 마리면 1µs, 스프라이트 블릿 한 번과 자릿수가 같다. **절반 이상이 클립 샘플링·블렌딩**인데, `AnimClip::Sample` 이 매번 13본짜리 `std::vector<Transform>` 을 새로 만들고 `BlendPoses` 가 하나 더 만들기 때문이다. 호출당 벡터 할당 셋. 보스가 수십 마리가 되면 그것이 먼저 문제가 된다.

## 10. 설계에서 조정한 것

**1. 걸음 위상을 시뮬 타이머가 아니라 위치의 함수로.** 설계는 리그 컴포넌트에 스톰프 타이머를 두는 그림이었다. 그러면 클라이언트가 재현할 수 없고(§3), 와이어를 넓히면 스냅샷 예산 단언이 컴파일 시간에 깨진다. 위치의 함수로 바꾸면서 리그 컴포넌트가 12바이트로 줄었고, 그 작음이 오히려 17편의 "고정 열 낭비" 논증을 약하게 만들었다 — 정직하게 적어 둔다.

**2. 진행 방향을 이동 방향이 아니라 코어 방향으로.** 실제 이동 방향은 경로 배열에서 나오는데, 발파와 잔해가 경로를 바꾸므로 클라이언트가 같은 배열을 갖는다는 보장이 없다. 코어 위치는 맵에서 나오고 맵은 양쪽이 같다. 대가는 굽은 회랑에서 보스가 살짝 옆을 보고 걷는 것이다.

**3. IK 목표를 고관절 로컬 공간에.** 초안은 목표를 보스 로컬 공간에 놓았는데, 그러면 클립의 고관절 회전이 IK 결과에 아무 영향을 주지 못해 저작 파일의 절반이 죽은 값이 된다(§4).

**4. `AddBone` 에 부모 인덱스 검사를 추가.** 설계의 변이 항목("역순 본을 넣으면 합성이 틀려 실패")은 그 검사가 **있다**는 전제였는데 헤더에는 주석뿐이었다. 검사를 넣고 나서야 변이가 성립한다.

**5. IK 변이 대상을 클램프에서 도달 불가 분기로.** 설계는 "cosA 클램프를 지우면 NaN 이 나온다"고 적었다. 실제로는 그 위의 두 가드(`d >= reach`, `d <= |l1-l2|`)가 코사인의 정의역을 이미 보장하므로, 클램프만 지워도 부동소수 경계에서 우연히 걸리는 경우 말고는 NaN 이 나오지 않는다 — 결정적으로 실패하는 변이가 아니다. 대신 도달 불가 분기를 지우는 변이를 썼고, 그것이 §11에서 확인된다.

**6. 타워 체력을 와이어에 싣지 않는다.** 클라이언트가 체력 바를 그리지 못하는 한계를 감수했다. 와이어 포맷 변경은 협동 테스트가 감시하는 계약이고, 그 계약을 깨는 것이 이 기능의 값보다 크다.

## 11. 변이 셋

**(a) `AddBone` 의 부모 인덱스 검사를 지운다.**

```
> build\bin\Release\test_boss_rig.exe
[test] boss_rig
  FAIL tests\test_boss_rig.cpp:52  CHECK_EQ(skeleton.AddBone(5, math::Transform{}), -1)
  FAIL tests\test_boss_rig.cpp:53  CHECK_EQ(skeleton.AddBone(1, math::Transform{}), -1)
  FAIL tests\test_boss_rig.cpp:54  CHECK_EQ(skeleton.AddBone(-2, math::Transform{}), -1)
  FAIL tests\test_boss_rig.cpp:55  CHECK_EQ(skeleton.BoneCount(), 1)
  FAIL tests\test_boss_rig.cpp:56  CHECK_EQ(skeleton.AddBone(0, math::Transform{}), 1)
  FAIL tests\test_boss_rig.cpp:57  CHECK_EQ(skeleton.BoneCount(), 2)
[test] boss_rig: 90 checks, 6 failures
```

**(b) IK 의 도달 불가 분기를 지운다.** 목표에 닿지 못해도 `end = target` 이 된다.

```
> build\bin\Release\test_boss_rig.exe
[test] boss_rig
  FAIL tests\test_boss_rig.cpp:112  CHECK(!solved.reachable)
  FAIL tests\test_boss_rig.cpp:114  CHECK(Near(math::Length(solved.end - root), l1 + l2, 1e-3f))
  FAIL tests\test_boss_rig.cpp:155  CHECK(Near(math::Length(a.legs[leg].foot - a.legs[leg].knee), kBossShin * 30.0f, 1e-2f))
  FAIL tests\test_boss_rig.cpp:177  CHECK(straight > 0)
  한 걸음 64 표본 중 뻗은 다리 0개
[test] boss_rig: 90 checks, 4 failures
```

**이 변이가 가르쳐 준 것이 하나 있다.** 같은 변이에서 `test_stomp` 는 **통과한다**.

```
> build\bin\Release\test_stomp.exe
  발 대조 6회(값) / 6회(와이어 왕복)
  스톰프 40회, 타워 8 → 7, 최저 체력 12.0
[test] stomp: 171 checks, 0 failures
```

호스트와 클라이언트가 **같은 함수**를 부르므로, 그 함수가 틀려도 둘의 답은 여전히 같기 때문이다. 대조 테스트는 두 경로가 갈라지는 것을 잡지 정확성을 잡지 못한다. 정확성은 `test_boss_rig` 의 불변식 단언(마디 길이 보존)이 잡는다. **같은 배선에 성격이 다른 검사 둘이 필요하다**는 것을 이 변이가 보여 준다.

**(c) 스톰프 원점을 발이 아니라 몸통 중심으로.**

```
> build\bin\Release\test_stomp.exe
[test] stomp
  FAIL tests\test_stomp.cpp:89  CHECK(hostFoot.x == replayed.x)
  FAIL tests\test_stomp.cpp:90  CHECK(hostFoot.y == replayed.y)
  FAIL tests\test_stomp.cpp:102  CHECK(Near(overWire.x, hostFoot.x, 0.5f))
  FAIL tests\test_stomp.cpp:103  CHECK(Near(overWire.y, hostFoot.y, 0.5f))
  발 대조 6회(값) / 6회(와이어 왕복)
  스톰프 22회, 타워 8 → 7, 최저 체력 26.0
  FAIL tests\test_stomp.cpp:147  CHECK(math::Length(foot - body) > probe.world.BossScale() * 0.3f)
[test] stomp: 171 checks, 25 failures
```

마지막 단언이 핵심이다 — 발과 몸통이 같은 점이면 "발 기준"이라는 말이 무의미하다. 그리고 스톰프 횟수가 40에서 22로 준 것도 읽을 만하다: 원점이 몸통이 되면 반경 안의 타워가 달라져 부서지는 시점이 바뀌고, 그 뒤의 매치가 통째로 다른 세계가 된다.

## 12. 실행과 관측

깨끗한 빌드에서.

```
> cmake -S . -B build_verify && cmake --build build_verify --config Release
> ctest -C Release -j 8
100% tests passed out of 84
```

**골격과 IK.**

```
> build_verify\bin\Release\test_boss_rig.exe
[test] boss_rig
  한 걸음 64 표본 중 뻗은 다리 9개
  SolveBossFrame=1.039us/회 (그중 클립 샘플+블렌드 0.558us, 54%)  sink=45943404
[test] boss_rig: 91 checks, 0 failures
```

**스톰프.**

```
> build_verify\bin\Release\test_stomp.exe
[test] stomp
  발 대조 6회(값) / 6회(와이어 왕복)
  스톰프 40회, 타워 8 → 7, 최저 체력 12.0
[test] stomp: 171 checks, 0 failures
```

보스 40마리가 같은 길목을 지나면 타워 하나가 결국 부서지고, 살아남은 타워 중 최저 체력이 12.0(180에서 14회 맞은 값)이다.

**저작 파일.**

```
> build_verify\bin\Release\test_pose_content.exe
[test] pose_content
  배포 파일: walk 5키 attack 3키
  거부(pose_nonmonotonic.pose): ... key 시각이 단조 증가가 아니다
  ... (아홉 가지 거부)
[test] pose_content: 143 checks, 0 failures
```

**제품 안에서.** 두 표시 팔로 각각 한 판.

```
> build_verify\bin\Release\mega_defense.exe --selftest --ab boss_render=sprite
[content] map=external waves=external (5) balance=script pose=external(5키)
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external stomps=2

> build_verify\bin\Release\mega_defense.exe --selftest --ab boss_render=rig
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external stomps=2
```

시뮬 결과가 같다(`ticks=1377 towers=11 wave=6 stomps=2`). 표시만 바뀌었다는 뜻이고, 그것이 이 스위치의 계약이다.

표시 비용은 프레임 합계로는 노이즈에 묻힌다(1377프레임 x 0.52ms 의 소프트웨어 래스터에 다리 여덟 선분이 더해질 뿐이다). 그러나 **최댓값**은 일관되게 갈린다 — 최댓값 프레임이 곧 보스가 화면에 있는 프레임이기 때문이다.

| `render.draw` 최댓값 | 1회 | 2회 | 3회 |
|---|---|---|---|
| `boss_render=sprite` | 1.326ms | 1.674ms | 1.573ms |
| `boss_render=rig` | 2.008ms | 2.760ms | 2.068ms |

**스톰프 배선의 관측점.**

```
> build_verify\bin\Release\mega_defense.exe --selftest --trace-out trace_aos.tsv
sim.stomp	432	0.047	0.014	0.0001
```

`n=432`. 1377틱 중 432틱에서만 스코프가 열렸다 — 나머지는 `RigRows() == 0` 으로 조기 반환했다는 뜻이고, 보스가 살아 있던 시간이 정확히 그만큼이다.

---

## 결론

이 편이 한 일을 한 문장으로 줄이면: **표시 계층의 계산이 권위 시뮬의 입력이 되었다.**

그 방향은 흔치 않다. 보통 시뮬이 상태를 정하고 표시가 그것을 읽는다. 여기서는 IK 가 푼 발 좌표가 피해 판정의 원점이 되므로 반대다. 그것이 성립하려면 그 계산이 **표시 상태에 의존하지 않아야** 했고, 그 제약이 §3의 설계(위상을 위치의 함수로)를 강제했다.

측정에서 나온 것 셋.

**도달 불가 분기가 실제로 열린다**(64 표본 중 9). 보폭을 다리 길이보다 살짝 크게 잡은 결과이고, 그 덕에 IK 의 "닿지 못할 때 무엇을 하는가"가 죽은 코드가 아니라 게임 규칙이 된다.

**와이어 양자화가 발까지 0.5픽셀 안에서 번진다.** 위치 오차 1/8픽셀이 IK 를 지나 발끝에서 커지지 않는다.

**자세 계산의 절반 이상이 벡터 할당이다**(1.04µs 중 0.558µs). `AnimClip::Sample` 이 호출마다 포즈를 새로 만든다.

현재 상태의 한계 다섯.

**클립 샘플링이 매번 할당한다.** `Sample` 이 `std::vector<Transform>` 을 값으로 돌려주고 `BlendPoses` 가 하나 더 만든다. 보스 한 마리에서는 1µs 지만, 다족 개체가 수십이 되면 이 할당이 먼저 문제가 된다. 출력 버퍼를 받는 형태로 바꿔야 하고, 그러려면 `engine/anim.hpp` 의 인터페이스가 바뀐다.

**클라이언트가 타워 체력을 모른다.** 와이어를 넓히지 않았으므로 부서지는 타워가 예고 없이 사라진다.

**진행 방향이 코어 방향이다.** 굽은 회랑에서 보스가 살짝 옆을 보고 걷는다. 실제 이동 방향을 쓰려면 클라이언트가 경로 배열을 정확히 재현해야 하고, 그 보장이 지금 없다.

**보스가 멈추면 걷지 않는다.** 위상이 위치의 함수이므로 제자리에 선 보스는 발을 디디지 않는다. 이 게임의 보스는 항상 움직이므로 드러나지 않지만, 정지 상태가 생기는 순간 자세가 얼어붙는다.

**발이 지형을 읽지 않는다.** 16편이 만든 잔해 더미 위를 지나도 발끝 높이가 바뀌지 않는다. `engine/ik.hpp` 의 원래 등재 사유("불균일 지형을 걷는다")가 절반만 실현된 상태다 — IK 는 들어왔지만 지형 질의가 목표점에 들어가지 않았다.
