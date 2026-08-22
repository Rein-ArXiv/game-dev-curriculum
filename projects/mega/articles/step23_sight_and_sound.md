# 23. 보이지 않는 적

## 들어가며

지금까지 이 게임의 모든 적은 언제나 모든 타워에 보였다. 조준 후보는 **기하**가 정했다 — 사거리 안이고, 15편 이후로는 사격선이 정적 기하에 막히지 않으면 후보다. 그것으로 충분했던 이유는 맵에 숨을 곳이 없었기 때문이다. classic 회랑은 한 칸 폭이고, 절차 맵이 들어오기 전에는 내부 벽이라는 것이 없었다.

13편이 절차 지형을, 20편이 통행 격자를 넣으면서 그 전제가 깨진다. 이제 맵에는 바위 벽이 있고, 셀 단위로 "여기서 저기가 보이는가"를 물을 수 있는 자료구조가 있다. 그러면 시야 차폐가 처음으로 **게임 규칙이 될 수 있다.**

`ai/perception/perception.hpp` 가 그 자리를 가져간다. 그리고 그것을 관측 가능하게 만드는 콘텐츠가 함께 온다.

| 새로 생기는 것 | 무엇 |
|---|---|
| `EnemyType::Stalker` | 기본적으로 **조준 불가**인 적. 은신이 곧 방어력이다 |
| `TowerType::Radar` | 쏘지 않는 타워. 시야 원뿔로 은신 개체를 드러낸다 |
| `stealth=0\|1` | 규칙 스위치 |

이 편의 판정은 하나다. **조준 후보가 기하가 아니라 인지로 정해지는가.** 사거리 안에 있고 사격선도 열려 있는 적이, 아무도 그를 감지하지 못했다는 이유만으로 조준되지 않아야 한다.

---

## 목차
1. [규칙만 켜면 아무 일도 일어나지 않는다](#1-규칙만-켜면-아무-일도-일어나지-않는다)
2. [통행 격자와 시야 격자는 다른 질문에 답한다](#2-통행-격자와-시야-격자는-다른-질문에-답한다)
3. [원뿔 · 레이 · 청각](#3-원뿔--레이--청각)
4. [기억이 만드는 지연](#4-기억이-만드는-지연)
5. [조준 후보는 어디서 걸러지는가](#5-조준-후보는-어디서-걸러지는가)
6. [세 색인 팔이 같은 답을 내야 한다](#6-세-색인-팔이-같은-답을-내야-한다)
7. [노출 비트는 바이트를 늘리지 않고 건넌다](#7-노출-비트는-바이트를-늘리지-않고-건넌다)
8. [레이 샘플 간격은 어디에 놓이는가](#8-레이-샘플-간격은-어디에-놓이는가)
9. [셀프테스트가 레이더를 셋 세우는 이유](#9-셀프테스트가-레이더를-셋-세우는-이유)
10. [변이 둘](#10-변이-둘)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 규칙만 켜면 아무 일도 일어나지 않는다

`stealth=1` 은 "은신 개체는 감지되어야 조준된다"는 규칙을 켠다. 그런데 은신하는 적이 하나도 없으면 그 규칙은 한 번도 발동하지 않고, `stealth=0` 과 `stealth=1` 이 같은 게임이 된다. A/B 가 비교할 것을 잃는다.

그래서 스위치가 콘텐츠도 건드린다.

```cpp
inline std::vector<game::WaveDef> ApplyStealth(std::vector<game::WaveDef> waves,
                                               const game::Switches& switches) {
    if (!switches.stealth) return waves;
    for (game::WaveDef& wave : waves)
        if (wave.type == game::EnemyType::Fast) wave.type = game::EnemyType::Stalker;
    return waves;
}
```

콘텐츠 파일을 두 벌 두지 않기 위한 변환이다. `waves.txt` 는 `stalker` 를 직접 쓸 수도 있고, 그때 이 변환은 아무것도 바꾸지 않는다 — 이미 Stalker 다.

Stalker 의 능력치는 Fast 를 그대로 물려받되 보상만 올렸다.

```cpp
{0.7f, 1.5f, 1.0f, 2},    // Stalker: 은신·빠름. 조준되지 않는 것이 방어력이다
```

체력을 올리지 않은 것이 설계다. 은신이 방어력이므로 체력까지 주면 두 축이 같은 방향으로 쌓여, 레이더 없는 판에서 이 적이 사실상 무적이 된다.

## 2. 통행 격자와 시야 격자는 다른 질문에 답한다

`NavGrid` 는 격자를 둘 든다. 20편에서 통행 격자를 세울 때 시야 격자도 함께 세운다.

<!-- snippet: include/game/nav.hpp@nav-walkable -->
```cpp
    // 시야가 지나가는가. 바위만 막는다 — 타워도 잔해도 통행은 막지만 레이더
    // 접시의 시선은 막지 않는다.
    static bool TransparentIn(const DefenseMap& map, int x, int y) {
        return map.InBounds(x, y) && !map.Rock(x, y);
    }
```

하나로 합치면 어떻게 되는지는 실제로 만들어 봤다. 타워가 통행을 막으므로 타워 칸이 시야도 막고, **레이더는 자기가 서 있는 칸에 가려 아무것도 보지 못한다.** 은신 개체가 매치 내내 한 틱도 드러나지 않았고, 노출 카운터가 0 이라 그것이 결함인지 "그런 판이었는지" 구분되지 않았다.

두 격자를 나눈 뒤로 규칙은 이렇게 읽힌다. **통행을 막는 것과 시선을 막는 것은 다른 집합이다.** 바위는 둘 다 막고, 타워와 안착 잔해는 통행만 막는다.

## 3. 원뿔 · 레이 · 청각

감지 조건은 둘의 논리합이다.

```cpp
inline bool CanSee(const path::GridMap& map, const math::Vec2& eye, const math::Vec2& facing,
                   const VisionCone& cone, const math::Vec2& target) {
    if (!InCone(eye, facing, cone, target)) return false;
    math::Vec2 d = target - eye;
    float dist = math::Length(d);
    int steps = (int)std::ceil(dist / 0.25f);
    for (int i = 1; i < steps; ++i) {                  // 끝점 제외하고 경로 샘플
        float t = (float)i / steps;
        math::Vec2 p = eye + d * t;
        if (!map.Walkable((int)std::floor(p.x), (int)std::floor(p.y))) return false;  // 벽에 가림
    }
    return true;
}
```

원뿔은 값싸고(거리 하나 + 내적 하나), 레이캐스트는 그보다 비싸다. 그래서 원뿔이 먼저다 — 원뿔 밖이면 레이를 쏘지 않는다.

단위는 **칸**이다.

```cpp
inline constexpr float kRadarRangeCells = 12.0f;
inline constexpr float kRadarFovDegrees = 140.0f;
inline constexpr float kRadarHearCells  = 2.5f;
```

픽셀로 두면 절차 맵의 셀 크기(10px)와 classic 맵의 셀 크기(30px)에서 규칙이 서로 달라진다. 격자 레이캐스트가 칸 단위로 걷는 이상 사거리도 칸이어야 두 맵에서 같은 규칙이다. 월드 좌표를 격자 실수 좌표로 옮기는 함수가 그 경계에 있다.

```cpp
inline math::Vec2 NavSpace(const DefenseMap& map, const math::Vec2& world) {
    const float cell = static_cast<float>(map.CellSize());
    return {world.x / cell, (world.y - static_cast<float>(map.OriginY())) / cell};
}
```

정수 셀 좌표가 아니라 **실수** 좌표를 내는 것이 요점이다. 레이는 셀 사이를 걸으므로 정수로는 표현할 수 없다.

청각은 훨씬 단순하다. 반경 안이면 들린다.

```cpp
inline bool CanHear(const math::Vec2& ear, const math::Vec2& soundPos, float loudnessRadius) {
    return math::LengthSq(ear - soundPos) <= loudnessRadius * loudnessRadius;
}
```

소리의 출처는 **직전 틱에 발사한 타워의 위치**다. 그래서 "총소리 근처를 지나가면 들킨다"가 되고, 레이더가 없는 판에서도 은신 개체가 완전 무적은 아니게 된다.

## 4. 기억이 만드는 지연

감지가 매 틱 참·거짓이면 은신 개체가 모서리를 스칠 때마다 조준이 켜졌다 꺼졌다 한다. 타워는 조준 대상을 잃으면 다음 대상을 다시 고르므로, 그 깜빡임이 그대로 사격 패턴이 된다.

`Memory` 가 그 사이에 지연을 넣는다.

```cpp
struct Memory {
    bool       hasTarget = false;
    math::Vec2 lastSeen{0, 0};
    float      age = 0;              // 마지막 목격 후 경과(초)

    void Observe(const math::Vec2& pos) { hasTarget = true; lastSeen = pos; age = 0; }
    void Update(float dt, float forgetAfter) {
        if (!hasTarget) return;
        age += dt;
        if (age >= forgetAfter) { hasTarget = false; age = 0; }   // 망각
    }
};
```

인지 시스템은 감지되면 `Observe`, 아니면 `Update` 를 부른다.

<!-- snippet: include/game/game.hpp@perception-system -->
```cpp
            if (sensed) brain.memory.Observe(enemies_.Pos(i));
            else        brain.memory.Update(dt, kStalkerForget);
            brain.revealed = brain.memory.hasTarget ? uint8_t{1} : uint8_t{0};
            revealed_[i] = brain.revealed;
            if (brain.revealed) ++revealTicks_;
```

`kStalkerForget = 1.10f` 초. 그 시간이 "놓친 직후에도 잠깐 쏜다" 의 실물이다.

기억이 개체마다 있다는 것도 규칙이다. 레이더별이 아니라 **개체별**이라, 레이더 A가 봤다가 놓친 적을 레이더 B가 그 사이에 다시 보면 기억이 갱신된다. 레이더별로 두면 같은 적을 두 레이더가 각자 기억해, 레이더를 늘릴수록 망각이 사실상 사라진다.

## 5. 조준 후보는 어디서 걸러지는가

인지의 결과는 행 번호로 색인되는 바이트 배열 하나다. 그 배열이 조준 함수에 `SightTest` 로 들어간다.

```cpp
struct SightTest {
    const StaticGeometry* geometry = nullptr;
    StaticQueryKind       kind = StaticQueryKind::Naive;
    const uint8_t*        revealed = nullptr;
    size_t                revealedCount = 0;
    bool Visible(const math::Vec2& from, const math::Vec2& to) const {
        return !geometry || !geometry->SegmentBlocked(from, to, kind);
    }
    // 이 행이 조준 후보가 되는가. 기하가 아니라 **인지**의 질문이라 위치를
    // 받지 않는다 — 답은 이미 PerceptionSystem 이 계산해 두었다.
    bool Targetable(size_t row) const {
        return !revealed || row >= revealedCount || revealed[row] != 0;
    }
};
```

`Targetable` 이 위치를 받지 않는다는 것이 이 구조의 요점이다. **인지는 조준 시점에 계산되지 않는다.** 타워가 여덟이고 적이 수백이면 조준 판정은 프레임당 수천 번 도는데, 그 안에서 레이캐스트를 다시 하면 인지가 조준 비용의 함수가 된다. 인지 시스템이 개체당 한 번 계산해 두고, 조준은 그 결과를 읽기만 한다.

모르는 행을 **보인다**로 답하는 것도 계약이다. 은신 규칙이 꺼져 있으면 `revealed` 가 `nullptr` 이라 판정이 통째로 사라진다 — 스위치가 생기기 전과 문자 그대로 같은 코드 경로다.

## 6. 세 색인 팔이 같은 답을 내야 한다

15편이 조준 색인 A/B(`target_index=naive|grid|quadtree`)를 넣었다. 그중 두 팔은 후보 목록을 먼저 뽑고 그 목록 안에서 점수를 매긴다. 즉 **조준 경로가 둘**이다.

```cpp
CHECK_EQ(SelectTargetIn({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest, cloaked), -1);
...
CHECK_EQ(SelectTargetAmong({40, 40}, {0, 0}, enemies, 200.0f, TargetMode::Nearest,
                           candidate, 1, cloaked), -1);
```

둘이 갈리면 `target_index` 스위치가 은신 규칙을 **우회하는 통로**가 된다. 색인 팔을 바꿨을 뿐인데 은신이 무력해지는 상태이고, 그 상태에서는 15편의 A/B 도 23편의 규칙도 둘 다 거짓이 된다.

이것이 A/B 스위치가 쌓일 때 생기는 구조적 위험이다. 팔이 N개인 축이 M개면 조합은 곱으로 자라는데, 규칙 하나가 그 조합 중 일부에서만 성립하면 그 사실은 어느 팔에서도 실패로 나타나지 않는다. 방어는 하나뿐이다 — **규칙을 팔이 공유하는 한 곳에 두고, 팔마다 답이 같은지 직접 묻는 것.**

## 7. 노출 비트는 바이트를 늘리지 않고 건넌다

클라이언트도 은신 개체를 다르게 그려야 한다(드러난 개체만 실루엣이 아니라 본체로). 그러려면 노출 비트가 와이어를 건너야 하는데, 스냅샷은 이미 패킷 예산에 맞춰 개수 상한이 잡혀 있다. 필드를 하나 늘리면 적 하나당 1바이트가 늘고, 상한이 그만큼 줄어든다.

그래서 `type` 바이트의 빈 비트를 쓴다.

```cpp
inline constexpr uint8_t kEnemyRevealedBit = 0x80;
inline constexpr uint8_t kEnemyTypeMask = 0x7F;
```

`EnemyType` 은 여섯 값이라 하위 3비트면 충분하고, 최상위 비트는 비어 있다.

```cpp
writer.U8(static_cast<uint8_t>(
    static_cast<uint8_t>(world.Enemies().Type(i)) |
    (world.Revealed(i) ? kEnemyRevealedBit : 0)));
```

디코더는 마스크로 되살린다. 그리고 `enemy.type >= EnemyType::Count` 검사가 그대로 남아 있어야 한다 — 마스크를 빼먹으면 노출된 적의 종류가 `0x84` 같은 값이 되어 그 검사에 걸리고, 패킷 전체가 거부된다. 그 실패 방식이 **조용하지 않다**는 것이 이 설계를 쓸 수 있는 이유다.

## 8. 레이 샘플 간격은 어디에 놓이는가

`CanSee` 의 레이는 0.25칸 간격으로 샘플한다. 이 상수가 정확성과 비용 사이의 유일한 손잡이다.

간격이 넓으면 얇은 벽을 **뛰어넘는다.** 벽이 한 칸 두께이고 샘플 간격이 1.0칸이면, 레이가 벽의 앞뒤 칸만 찍고 벽 칸을 건너뛸 수 있다. 그러면 벽 뒤의 적이 보인다.

간격이 좁으면 비용이 그만큼 는다. 사거리 12칸이면 0.25 간격에서 48샘플이고, 그것이 레이더 수 x 은신 개체 수만큼 돈다. 레이더가 셋이고 은신 개체가 열둘이면 프레임당 1728샘플이다.

0.25 를 고른 근거는 벽의 최소 두께다. 절차 맵에서 벽은 언제나 한 칸 이상이므로, 간격이 1.0 미만이면 어떤 벽도 건너뛸 수 없다. 0.5 로도 성립하지만 대각선으로 스치는 경우 샘플이 벽 칸의 귀퉁이만 스칠 수 있어 여유를 뒀다.

비용 쪽 방어는 별도로 있다. 인지 시스템은 **Stalker 행만** 돈다.

<!-- snippet: include/game/game.hpp@perception-system -->
```cpp
        for (size_t i = 0; i < count && i < brains_.size(); ++i) {
            if (enemies_.Type(i) != EnemyType::Stalker) {
                brains_[i].revealed = 1;
                revealed_[i] = 1;
                continue;
            }
```

그리고 감지된 순간 `break` 한다 — 레이더 셋 중 하나가 보면 나머지 둘의 레이는 쏘지 않는다.

## 9. 셀프테스트가 레이더를 셋 세우는 이유

제품 안에서 이 규칙이 도는지 확인하려면 셀프테스트 스크립트가 레이더를 세워야 한다. 하나가 아니라 셋이다.

<!-- snippet: include/game/defense_app.hpp@selftest-tower-key -->
```cpp
inline int SelftestTowerKey(size_t cursor, const Switches& switches) {
    if (switches.stealth && (cursor == 3 || cursor == 7 || cursor == 11)) return '6';
    if (switches.pathing != PathingKind::Polyline && cursor == 5) return '5';
    return static_cast<int>('1' + (cursor % 4));
}
```

은신 웨이브에서는 **아무 타워도 쏘지 않는다.** 조준 후보가 없으니 발사가 없고, 발사가 없으니 청각도 발동하지 않는다. 남는 감지 수단은 시야뿐이다. 레이더 하나의 원뿔은 사거리 12칸 x 140도라, 79칸짜리 절차 경로의 한 토막만 덮는다. 나머지는 그대로 샌다.

셋을 경로를 따라 흩어 세우면 경로 대부분이 어느 레이더의 원뿔 안에 들어가고, 그때부터 청각도 연쇄로 작동한다 — 한 곳에서 들킨 적이 사격을 유발하고, 그 소리가 다음 구간에서 다시 들킨다.

`cursor == 5` 자리에 빙결 타워(`'5'`)를 끼우는 것도 같은 계산이다. 20편의 잡몹 상태 기계에 `Stunned` 상태가 있는데, 빙결 타워가 하나도 없으면 그 상태는 매치 내내 한 번도 진입되지 않는다.

## 10. 변이 둘

**변이 1 — `CanSee` 의 레이 샘플 루프를 지운다.**

```cpp
inline bool CanSee(const path::GridMap&, const math::Vec2& eye, const math::Vec2& facing,
                   const VisionCone& cone, const math::Vec2& target) {
    return InCone(eye, facing, cone, target);
}
```

원뿔 안이면 벽 뒤라도 보인다. `test_stealth` 의 `TestOcclusionOnNavGrid` 가 첫 단언에서 죽는다. 제품 쪽 영향도 관측 가능하다 — 노출 틱이 늘고 코어 체력이 올라간다.

**변이 2 — `SightTest::Targetable` 을 언제나 참으로 만든다.**

```cpp
bool Targetable(size_t) const { return true; }
```

인지 시스템은 그대로 돌고 노출 카운터도 그대로 오른다. 다만 그 결과를 아무도 읽지 않는다. `test_stealth` 의 `TestTargetableFilter` 가 죽고, 레이더 유무 대조도 코어 체력이 같아져 죽는다. **계산은 하는데 쓰지 않는** 배선의 전형이라, 이 변이가 죽는지가 곧 인지 축이 진짜 편입됐는지의 판정이다.

## 11. 실행과 관측

**원뿔 · 레이 · 기억의 단위 계약.**

```
> build\bin\Release\test_ai_perception.exe
[test] ai_perception
[test] ai_perception: 17 checks, 0 failures
```

**은신 규칙 전체.**

```
> build\bin\Release\test_stealth.exe
[test] stealth
  망각까지 67틱(1.12s), forgetAfter=1.10s
  레이더 없음  노출틱=0 코어=3880
  레이더 있음  노출틱=1548 코어=3960
  스냅샷 82바이트(예산 1200) 적 4마리
[test] stealth: 33 checks, 0 failures
```

읽을 것이 넷 있다.

**`망각까지 67틱(1.12s)`.** `forgetAfter = 1.10s` 에 대해 66틱이 이론값이고 관측이 67틱이다. `age >= forgetAfter` 를 만족시키려면 한 틱을 더 먹어야 하기 때문이고, 단언은 ±1틱만 허용한다. 이 창이 없으면 조준이 모서리마다 깜빡인다.

**`노출틱 0 → 1548`.** 같은 맵, 같은 자리, 같은 타워 수인데 절반을 레이더로 바꿨다. 레이더가 없으면 은신 개체가 **한 틱도** 드러나지 않는다 — 청각조차 발동하지 않는데, 아무도 조준하지 못하니 아무도 쏘지 않고 소리가 나지 않기 때문이다. 은신 규칙의 자기 강화 고리가 그 0에 있다.

**`코어 3880 → 3960`.** 그 노출이 실제로 게임 결과를 바꿨다. 노출 카운터만 보면 "계산은 했다"까지이고, 코어 체력 차이가 "그 계산이 쓰였다"이다.

**`스냅샷 82바이트(예산 1200)`.** 노출 비트를 `type` 바이트에 실었으므로 적 하나당 바이트가 늘지 않았다. 필드를 따로 뒀다면 적 4마리에 4바이트, 상한인 40마리면 40바이트가 늘고 그만큼 적 상한이 줄었을 것이다.

**제품.**

```
> build\bin\Release\mega_defense.exe --selftest 25000 --ab map=procgen --ab pathing=flowfield --ab steering=1 --ab stealth=1 --ab elite_brain=bt
[terrain] seed=7 41x41 maze=799 carved=318 rock=564 veins=148 route=79 bvh_prims=564/nodes=359
[selftest] result=CLEAR ticks=4921 towers=10 wave=6 packet=164 protocol=OK render=OK assets=10/10 content=external stomps=3
[selftest] ai field_builds=40 searches=0 melee=0 slams=3 reveal=122
```

`reveal=122` — 은신 개체가 드러나 있던 (개체 x 틱) 수다. 41x41 미로에 바위가 564칸이고 경로가 79칸이니 시야를 막을 벽은 충분하다. 이 수가 0이면 규칙이 켜져 있는데 아무 일도 하지 않은 것이고, 스크립트가 레이더를 세 개 세우는 이유가 그것이다.

`towers=10` 중 셋이 레이더다. 레이더는 쏘지 않으므로 사격 타워는 일곱이고, 그럼에도 `result=CLEAR` 다.

**변이 확인.**

`CanSee` 의 레이 샘플 루프를 지워 실제로 주입했다.

```
[test] stealth
  FAIL tests\test_stealth.cpp:39  CHECK(!ai::perceive::CanSee(grid, eye, facing, cone, {4.5f, 2.5f}))
  레이더 없음  노출틱=0 코어=3880
  레이더 있음  노출틱=2578 코어=3970
[test] stealth: 33 checks, 1 failures

[test] ai_perception
  FAIL tests\test_ai_perception.cpp:34  CHECK(!CanSee(m, eye, facing, cone, {4.5f, 2.5f}))
[test] ai_perception: 17 checks, 1 failures
```

단언이 죽는 것 말고도 읽을 것이 있다. **노출틱이 1548 에서 2578 로 66% 늘었다.** 벽이 더 이상 시선을 막지 않으니 원뿔 안의 모든 은신 개체가 언제나 드러난다. 그 수치가 규칙이 실제로 무엇을 하고 있었는지를 역으로 보여 준다.

두 번째 변이 — `SightTest::Targetable` 을 언제나 참으로 만들기 — 는 인지 시스템을 그대로 두고 그 **결과만** 버린다. 노출 카운터는 계속 오르고 레이캐스트도 계속 돌지만 게임은 은신 이전으로 돌아간다. 계산하는데 쓰지 않는 배선의 전형이고, `TestTargetableFilter` 와 레이더 유무 대조가 그것을 잡는다.

---

## 결론

인지를 넣으면서 바뀐 것은 조준 규칙 한 줄이 아니라 **조준 후보를 누가 정하는가**다. 기하가 정하던 것을 인지가 정하게 되었고, 그 결과 사거리 안에 있고 사격선도 열린 적이 조준되지 않는 상태가 합법이 됐다.

그 변경이 가능했던 이유는 20편이 통행 격자를 세워 뒀기 때문이다. 시야 차폐는 "여기서 저기가 보이는가"에 답할 자료구조를 요구하는데, 폴리라인에는 그런 것이 없었다. 축 하나가 다른 축의 자료구조 위에서만 성립하는 자리이고, 그것이 경로를 인지보다 먼저 넣은 이유다.

측정에서 나온 것 셋.

**통행과 시야를 한 격자로 합치면 레이더가 자기에게 가린다.** 코드가 아니라 그림으로만 드러나는 종류의 결함이고, 노출 카운터가 없었다면 "그런 판"과 구분되지 않았다.

**규칙만 켜면 A/B 가 성립하지 않는다.** 은신하는 적이 없으면 두 팔이 같은 게임이다. 스위치가 콘텐츠를 함께 바꾸는 것이 그 축을 관측 가능하게 만드는 조건이었다.

**색인 팔이 규칙의 우회로가 될 수 있다.** 조준 경로가 둘이므로 인지 필터도 두 곳에 있어야 하고, 둘이 같은 답을 내는지는 직접 물어야만 알 수 있다.

현재 상태의 한계 넷.

**레이더는 방향이 고정이다.** 배치 순간 스폰 쪽으로 향하고 그대로다. 회전하는 레이더는 원뿔을 시간의 함수로 만들어 "지금 어디를 보고 있는가"가 상태가 되고, 그 상태는 와이어를 건너야 한다.

**청각은 직전 틱의 발사만 듣는다.** 소리가 시간에 따라 감쇠하지 않고, 벽을 통과한다. 실제로는 그 둘이 시야보다 흥미로운 규칙을 만들 여지가 있지만, 소리의 전파를 격자 위에서 풀면 그것은 두 번째 경로 탐색이 된다.

**은신은 Stalker 한 종류뿐이다.** 다른 적에게 붙는 은신(예: 특정 웨이브 동안만)은 지금 표현할 수 없다. `EnemyType` 이 은신 여부를 겸하고 있기 때문이다.

**클라이언트는 노출 비트를 받지만 그것으로 아무것도 하지 않는다.** 와이어를 건너는 것과 그려지는 것은 다른 문제이고, 지금 뷰는 노출 여부와 무관하게 같은 스프라이트를 그린다.
