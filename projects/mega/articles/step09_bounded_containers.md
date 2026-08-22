## 들어가며

8편의 마지막 측정이 이 편의 순서를 정했다. 시뮬 전체가 프레임의 0.2%였고, 그 이유는 규모다 — 이 게임의 콘텐츠는 동시에 살아 있는 적이 스물몇 마리를 넘지 않는다. 그 규모에서는 어떤 자료구조를 써도 전부 L1 캐시 안에 들어간다.

그래서 이 편은 두 가지를 함께 한다.

1. **규모를 올릴 수 있게 만든다.** 동시 생존 적 상한이라는 개념 자체가 없던 코드에 상한을 넣고, `scale=small|large` 로 64 ↔ 2048 을 고른다. 상한이 없으면 "적 2048에서 재라"는 문장을 쓸 수 없다.
2. **그 규모에서 값을 하는 자료구조로 갈아탄다.** 표준 컨테이너 셋을 게임이 이미 아는 상한이 있는 자료구조로 바꾼다.

바꾸는 셋은 이렇다.

| 자리 | 지금 | 바꾼 뒤 | 스위치 |
|---|---|---|---|
| 적 저장 | `std::vector<Enemy>` | `SoAVector<...>` 열 배열 | `entity_store=aos\|soa` |
| id → 대응 | 전수 스캔 | 개방 주소법 해시맵 | `id_map=linear\|hash` |
| 스냅샷 보관 | `std::deque<Snap>` | 고정 용량 링 | (교체, A/B 없음) |
| 파티클 이전 프레임 | `std::unordered_map` | 개방 주소법 해시맵 | (교체, A/B 없음) |

세 번째와 네 번째에 스위치가 없는 이유는 §6·§7 에 있다 — 둘은 "다른 비용"이 아니라 **애초에 자료구조가 요구에 맞지 않았던 자리**다.

시작 상태에서 `GameWorld` 는 `std::vector<Enemy> enemies_` 를 갖고 있고, `Enemies()` 가 그 벡터의 참조를 그대로 돌려준다. 도달점은 저장이 `EnemyStore` 뒤로 숨고, 그 뒤에 두 배치가 나란히 살아 있으며, 3000틱을 돌린 두 배치의 상태 해시가 비트 동일한 상태다.

---

## 목차
1. [상한이 없으면 규모를 말할 수 없다](#1-상한이-없으면-규모를-말할-수-없다)
2. [저장을 규칙에서 떼어낸다](#2-저장을-규칙에서-떼어낸다)
3. [산술은 한 벌만 있어야 한다](#3-산술은-한-벌만-있어야-한다)
4. [swap-remove 를 기각한 이유](#4-swap-remove-를-기각한-이유)
5. [측정: SoA 가 이기는 곳과 지는 곳](#5-측정-soa-가-이기는-곳과-지는-곳)
6. [해시맵 두 자리 — 하나는 A/B, 하나는 교체](#6-해시맵-두-자리--하나는-ab-하나는-교체)
7. [덱을 링으로: 상한이 이미 있던 자리](#7-덱을-링으로-상한이-이미-있던-자리)
8. [해시맵 순회를 시뮬 경로에 넣지 않는다](#8-해시맵-순회를-시뮬-경로에-넣지-않는다)
9. [측정: 해시가 지는 워크로드](#9-측정-해시가-지는-워크로드)
10. [변이: 배선이 죽으면 테스트가 죽는가](#10-변이-배선이-죽으면-테스트가-죽는가)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 상한이 없으면 규모를 말할 수 없다

`SpawnSystem` 은 웨이브가 시키는 대로 적을 만들었다. 상한이 없었다. 콘텐츠(`assets/waves.txt`)의 최대 웨이브가 26마리라서 문제가 되지 않았을 뿐이다.

상한을 넣는 이유는 둘이다.

**규모를 고를 수 있어야 한다.** `scale=large` 는 상한을 2048로 올린다. 그것이 SoA·워커·아레나가 잴 수 있는 규모다.

**상한이 없으면 예산도 없다.** 프레임 스크래치 크기(10편), 발사체 풀 크기, 스냅샷 잘림 판정이 전부 "동시에 몇 개까지인가"에 달려 있다. 그 수가 없으면 버퍼를 미리 잡을 수 없고, 미리 잡을 수 없으면 매치 중에 다시 잡아야 한다.

```cpp
static constexpr size_t kEnemyCapSmall = 64;
static constexpr size_t kEnemyCapLarge = 2048;
```

`small` 의 64는 임의의 수가 아니다. 와이어 포맷의 `kSnapshotMaxEnemies` 와 같은 값이다 — 그래서 small 규모에서는 스냅샷 잘림이 구성상 일어나지 않는다. `large` 에서는 일어나고, 그것을 세는 카운터가 4편부터 이미 있다.

상한에 닿았을 때 무엇을 하는가가 미묘하다. 스폰 루프는 `spawnTimer_ <= 0` 인 동안 도는데, 상한에서 그냥 `continue` 하면 타이머가 줄지 않아 무한 루프가 된다.

```cpp
while (spawned_ < w.count && spawnTimer_ <= 0.0f) {
    // 상한에 닿으면 이번 틱은 더 넣지 않는다. spawned_ 를 올리지 않으므로
    // 자리가 나는 다음 틱에 같은 개체를 다시 시도한다.
    if (enemies_.Full()) break;
    // ...
}
```

`break` 하고 `spawned_` 를 올리지 않는다. 다음 틱에 자리가 나면 같은 개체를 다시 시도한다 — 웨이브가 사라지지 않고 **밀린다**.

## 2. 저장을 규칙에서 떼어낸다

`GameWorld` 는 규칙을 안다. "이 적을 speed*dt 만큼 경로를 따라 전진시킨다"가 규칙이다. 그 적이 메모리에 어떻게 놓이는가는 **다른 질문**이고, 답이 하나가 아니다.

그래서 `game/entity_store.hpp` 를 새로 만들어 저장만 옮겼다. 열 순서는 이렇다.

```cpp
using Columns = SoAVector<math::Vec2,  // 0 pos
                          float,        // 1 hp
                          float,        // 2 maxHp
                          float,        // 3 speed
                          float,        // 4 damage
                          int,          // 5 reward
                          uint32_t,     // 6 id
                          uint32_t,     // 7 pathIndex
                          EnemyType,    // 8 type
                          uint8_t>;     // 9 alive
```

`Enemy` 한 행은 40바이트다. 전진 루프가 실제로 읽는 것은 `pos`(8) + `speed`(4) + `damage`(4) + `pathIndex`(4) + `alive`(1) = **21바이트**다. 조준 스캔(`Nearest` 모드)은 `pos`(8) + `alive`(1) = **9바이트**만 읽는다.

AoS 에서는 둘 다 40바이트를 끌고 온다. 64바이트 캐시 라인 하나에 적이 1.6마리씩 들어간다. SoA 에서는 조준 스캔이 같은 라인에 적 7마리 분량의 좌표를 담는다.

두 필드의 타입을 바꿨다. `alive` 가 `bool` 이 아니라 `uint8_t` 인 것은 `std::vector<bool>` 이 비트 프록시라 원소의 참조를 만들 수 없기 때문이다 — 참조를 만들 수 없으면 두 저장이 같은 전진 함수를 공유할 수 없다. `pathIndex` 가 `size_t` 가 아닌 것은 열 하나가 8바이트면 2048행에서 16KB 가 되기 때문이다. 그 열은 전진 루프가 매 틱 전수로 읽는다.

## 3. 산술은 한 벌만 있어야 한다

두 저장이 같은 결과를 내야 한다. 그러려면 산술이 한 곳에만 있어야 한다.

이것이 이 편에서 가장 중요한 구조 결정이다. AoS 루프와 SoA 루프에 같은 수식을 두 번 쓰면, 그 순간부터 두 팔은 **비교 대상이 아니라 서로 다른 게임**이다. 한쪽만 고친 버그가 A/B 를 통과하지 못하고, 통과하지 못하는 이유를 찾는 데 며칠이 든다.

<!-- snippet: include/game/entity_store.hpp@advance-math -->
```cpp
inline void AdvanceEnemyOnPath(math::Vec2& pos, uint32_t& pathIndex, uint8_t& alive,
                               float speed, float damage,
                               const math::Vec2* path, size_t pathCount,
                               float dt, float& damageOut) {
    float remaining = speed * dt;
    while (remaining > 0.0f && alive) {
        if (pathIndex >= pathCount) {
            damageOut += damage;
            alive = 0;
            break;
        }
        math::Vec2 d = path[pathIndex] - pos;
        float dist = math::Length(d);
        if (dist <= remaining + 1e-4f) {
            pos = path[pathIndex++];
            remaining -= dist;
        } else {
            pos += d * (remaining / dist);
            remaining = 0.0f;
        }
    }
}

inline void AdvanceEnemyTowardCore(math::Vec2& pos, uint8_t& alive, float speed,
                                   float damage, const math::Vec2& corePos, float dt,
                                   float& damageOut) {
    math::Vec2 d = corePos - pos;
    float dist = math::Length(d);
    if (dist < 6.0f) {                     // 코어 도달
        damageOut += damage;
        alive = 0;
        return;
    }
    pos += d * (speed * dt / dist);        // 코어로 직진
}
```

두 팔은 **어디서 값을 꺼내는지만** 다르다.

<!-- snippet: include/game/game.hpp@enemy-advance -->
```cpp
void EnemyAdvanceSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.enemy_advance");
    const size_t count = enemies_.Size();
    if (count == 0) return;

    // 개체별 코어 피해를 따로 받아 둔다. 워커가 coreHp_ 를 직접 깎으면 감산
    // 순서가 실행마다 달라져 부동소수 결과가 갈린다.
    float* damage = scratch_.Alloc<float>(count);
    for (size_t i = 0; i < count; ++i) damage[i] = 0.0f;

    const math::Vec2* path = path_.Data();
    const size_t pathCount = path_.Size();
    const bool onPath = mapEnabled_ && pathCount > 0;
    const math::Vec2 core = corePos_;

    // ...
    } else if (enemies_.Kind() == EntityStoreKind::Soa) {
        math::Vec2*  pos   = enemies_.PosColumn();
        uint32_t*    index = enemies_.PathIndexColumn();
        uint8_t*     alive = enemies_.AliveColumn();
        const float* speed = enemies_.SpeedColumn();
        const float* hurt  = enemies_.DamageColumn();
        RunChunks(count, [=](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (!alive[i]) continue;
                if (onPath)
                    AdvanceEnemyOnPath(pos[i], index[i], alive[i], speed[i], hurt[i],
                                       path, pathCount, dt, damage[i]);
                else
                    AdvanceEnemyTowardCore(pos[i], alive[i], speed[i], hurt[i],
                                           core, dt, damage[i]);
            }
        });
    } else {
        Enemy* rows = enemies_.RowArray();
        RunChunks(count, [=](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                Enemy& e = rows[i];
                if (!e.alive) continue;
                if (onPath)
                    AdvanceEnemyOnPath(e.pos, e.pathIndex, e.alive, e.speed, e.damage,
                                       path, pathCount, dt, damage[i]);
                else
                    AdvanceEnemyTowardCore(e.pos, e.alive, e.speed, e.damage,
                                           core, dt, damage[i]);
            }
        });
    }

    // 병합은 인덱스 오름차순 — 직렬 실행의 감산 순서와 정확히 같다.
    for (size_t i = 0; i < count; ++i)
        if (damage[i] != 0.0f) coreHp_ -= damage[i];
}
```

(`RunChunks` 와 `damage[]` 배열의 이유는 각각 11편·10편에 있다. 여기서는 두 팔이 같은 함수를 부른다는 것만 보면 된다.)

조준도 같은 규율을 따른다. `std::vector<Enemy>` 위의 `SelectTarget` 은 지우지 않고 **오라클로 남긴다.** 저장 위의 `SelectTargetIn` 이 같은 점수 함수를 쓰고, 테스트가 둘의 답을 대조한다.

<!-- snippet: include/game/game.hpp@select-target-store -->
```cpp
inline int SelectTargetIn(const math::Vec2& towerPos, const math::Vec2& corePos,
                          const EnemyStore& enemies, float range, TargetMode mode,
                          const SightTest& sight = SightTest{}) {
    int best = -1;
    float bestScore = 0;
    float r2 = range * range;
    const size_t count = enemies.Size();
    for (size_t i = 0; i < count; ++i) {
        if (!enemies.Alive(i)) continue;
        float d2 = math::LengthSq(enemies.Pos(i) - towerPos);
        if (d2 > r2) continue;
        const float score = TargetScore(enemies, i, towerPos, corePos, mode);
        if (best >= 0 && score >= bestScore) continue;
        // ...
        bestScore = score;
        best = static_cast<int>(i);
    }
    return best;
}
```

## 4. swap-remove 를 기각한 이유

`SoAVector` 는 `SwapRemove` 를 갖고 있다. O(1)이고, 열 하나당 대입 한 번과 `pop_back` 한 번이다. 압축에 그것을 쓰는 것이 자연스러워 보인다.

쓰지 않았다. 이유는 **순서**다.

AoS 팔의 압축은 `std::remove_if` 이고 그것은 순서를 보존한다. SoA 팔이 swap-remove 를 쓰면 살아남은 행의 상대 순서가 달라진다. 그 순서는 두 곳에서 관측된다.

- **동점 조준.** `SelectTargetIn` 은 `score < bestScore` 로 비교한다(등호 없음). 같은 점수의 적이 둘이면 **먼저 나온 쪽**이 이긴다. 순서가 다르면 다른 적이 맞는다.
- **스냅샷 순서.** `WriteSnapshot` 은 행 번호 순으로 적을 싣는다. 순서가 다르면 클라이언트가 받는 바이트가 다르다.

즉 swap-remove 를 쓰면 두 팔이 서로 다른 세계를 돈다. A/B 의 전제가 무너진다.

그래서 순서를 보존하는 압축을 쓴다. 컬럼 수만큼 같은 코드를 반복해야 하는데, 컬럼 목록을 아는 것은 `SoAVector` 뿐이라 그 반복을 컨테이너 쪽에 접었다.

<!-- snippet: include/containers/soa_vector.h#ForEachColumn -->
```cpp
void ForEachColumn(Fn&& fn) { ForEachColImpl(static_cast<Fn&&>(fn), Indices{}); }
```

<!-- snippet: include/game/entity_store.hpp@compact-dead -->
```cpp
void CompactDead() {
    // ...
    if (kind_ == EntityStoreKind::Aos) {
        aos_.erase(std::remove_if(aos_.begin(), aos_.end(),
                                  [](const Enemy& e) { return !e.alive; }),
                   aos_.end());
    } else {
        const std::vector<uint8_t>& alive = soa_.Column<9>();
        size_t write = 0;
        for (size_t read = 0; read < alive.size(); ++read) {
            if (!alive[read]) continue;
            if (write != read)
                soa_.ForEachColumn([&](auto& column) { column[write] = column[read]; });
            ++write;
        }
        soa_.ForEachColumn([&](auto& column) { column.resize(write); });
    }
    RebuildIndex();
}
```

이 결정의 대가는 §5 의 측정에 그대로 나온다 — SoA 팔의 압축이 AoS 팔보다 두 배 가까이 비싸다. 열 열 개를 각각 훑기 때문이다. 그 대가를 알고 치른다.

## 5. 측정: SoA 가 이기는 곳과 지는 곳

적 2048마리를 상한까지 채우고 게이틀링 여덟 대를 세운 뒤 600틱을 돌렸다. 8편의 스코프 타이머가 그대로 계량기가 된다.

```
[entity_store scale=large]
  aos  enemies=2048 ticks=600 total=15.4ms  advance=3.65ms fire=8.37ms projectile=2.74ms cleanup=0.40ms
  soa  enemies=2048 ticks=600 total=14.8ms  advance=4.00ms fire=7.32ms projectile=2.62ms cleanup=0.67ms
```

세 번 돌려도 방향이 바뀌지 않는다(§11 에 전량). 시스템별로 읽으면 이렇다.

| 시스템 | aos | soa | 방향 | 이유 |
|---|---|---|---|---|
| `sim.tower_fire` | 8.37ms | **7.32ms** | SoA 승 (−13%) | 조준 스캔이 `pos`+`alive` 9바이트만 읽는다 |
| `sim.enemy_advance` | **3.65ms** | 4.00ms | AoS 승 (−9%) | 다섯 열을 동시에 스트리밍한다 |
| `sim.cleanup` | **0.40ms** | 0.67ms | AoS 승 (−40%) | 열 열 개를 각각 훑는다 |
| 합계 | 15.4ms | **14.8ms** | SoA 승 (−4%) | |

**예상과 다르다.** parts 시리즈의 캐시 측정은 SoA 가 AoS 대비 2~5배라고 적고, 나도 전진 루프에서 그것을 기대했다. 측정은 전진 루프에서 SoA 가 **진다**고 말한다.

이유를 따라가면 납득이 된다. `AdvanceEnemyOnPath` 는 다섯 개의 서로 다른 배열에서 값을 하나씩 꺼낸다 — 하드웨어 프리페처가 다섯 개의 스트림을 동시에 따라가야 한다. AoS 는 스트림이 하나고, 40바이트 stride 는 프리페처가 가장 잘 다루는 형태다. 2048행이면 AoS 는 80KB, SoA 는 43KB 를 만지는데 **둘 다 L2 안**이라 그 차이가 지연으로 나타나지 않는다.

반대로 조준 스캔은 SoA 가 이긴다. 그 루프는 열 두 개(`pos`, `alive`)만 읽고, 타워 열여섯 대가 **같은 열을 열여섯 번** 훑는다. AoS 는 매번 40바이트/개체를 끌고 오고 SoA 는 9바이트를 끌고 온다. 그리고 이쪽이 시뮬에서 가장 비싼 시스템이라, 합계는 SoA 쪽으로 기운다.

정직하게 적으면 이 편의 결론은 "SoA 로 갈아탔더니 4% 빨라졌다"이다. 2~5배가 아니다. 그리고 그 4%도 전진 루프가 아니라 **조준 스캔에서** 나왔다.

이것이 A/B 스위치가 존재하는 이유다. 스위치가 없었다면 SoA 로 갈아타고 "캐시 친화적이니 빨라졌겠지"라고 적었을 것이고, 그 문장은 세 부분 중 두 부분이 틀렸을 것이다.

## 6. 해시맵 두 자리 — 하나는 A/B, 하나는 교체

`containers/hash_map.h` 가 들어가는 자리는 둘인데, 성격이 다르다.

**(1) id → 행 대응 — A/B.** 발사체는 매 틱 자기 표적을 찾는다. 지금은 적 배열을 전수 스캔한다. 발사체 m 개·적 n 마리면 곱이다.

<!-- snippet: include/game/entity_store.hpp@find-row -->
```cpp
int FindRow(uint32_t id) const {
    if (idMap_ == IdMapKind::Hash) {
        const uint32_t* row = index_.Find(id);
        if (!row) return -1;
        return static_cast<int>(*row);
    }
    const size_t count = Size();
    for (size_t i = 0; i < count; ++i)
        if (Id(i) == id) return static_cast<int>(i);
    return -1;
}
```

클라이언트 보간에도 같은 곱이 있다. `wire::Lerp` 는 b 의 개체 하나마다 a 를 전수 스캔한다.

<!-- snippet: include/game/defense_protocol.hpp@snapshot-interpolator -->
```cpp
class SnapshotInterpolator {
public:
    void SetMode(IdMapKind mode) { mode_ = mode; }
    IdMapKind Mode() const { return mode_; }

    Snapshot Blend(const Snapshot& a, const Snapshot& b, float t) {
        if (mode_ == IdMapKind::Linear) return Lerp(a, b, t);

        enemyIndex_.Clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(a.enemies.size()); ++i)
            enemyIndex_.Insert(a.enemies[i].id, i);
        projectileIndex_.Clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(a.projectiles.size()); ++i)
            projectileIndex_.Insert(a.projectiles[i].id, i);

        Snapshot out = b;
        out.coreHp = a.coreHp + (b.coreHp - a.coreHp) * t;
        for (EnemyState& enemy : out.enemies) {
            const uint32_t* row = enemyIndex_.Find(enemy.id);
            if (!row) continue;
            const EnemyState& previous = a.enemies[*row];
            enemy.pos = previous.pos + (enemy.pos - previous.pos) * t;
            enemy.hp = previous.hp + (enemy.hp - previous.hp) * t;
        }
        for (ProjectileState& projectile : out.projectiles) {
            const uint32_t* row = projectileIndex_.Find(projectile.id);
            if (!row) continue;
            const ProjectileState& previous = a.projectiles[*row];
            projectile.pos = previous.pos + (projectile.pos - previous.pos) * t;
        }
        return out;
    }

private:
    OpenAddressingHashMap<uint32_t, uint32_t> enemyIndex_;
    OpenAddressingHashMap<uint32_t, uint32_t> projectileIndex_;
    IdMapKind mode_ = IdMapKind::Linear;
};
```

맵을 멤버로 들고 있는 것이 의도다. 매 프레임 새로 만들면 그 할당이 절약분을 먹는다. `Clear` 가 용량을 유지하므로 두 번째 프레임부터 할당이 0이다.

**(2) 사라진 발사체 추적 — 교체.** 파티클 시스템은 "지난 프레임에 있었는데 지금 없는" 발사체 자리에 파편을 뿌린다. 그 보관소가 `std::unordered_map<uint32_t, math::Vec2>` 였고, **매 프레임 통째로 비우고 다시 채운다.**

체이닝 맵에서 그것은 프레임마다 노드 n 개 반납 + n 개 할당이다. 개방 주소법에서는 한 연속 배열의 `used` 비트를 내렸다 올리는 것이 전부다. A/B 로 둘 이유가 없다 — 이 워크로드에서 체이닝이 이길 수 있는 축이 없다.

## 7. 덱을 링으로: 상한이 이미 있던 자리

`net::SnapshotBufferT` 는 최근 스냅샷을 `std::deque` 에 쌓고 `keep_` 을 넘으면 `pop_front` 했다.

이 자리의 특징은 **상한이 처음부터 정해져 있었다**는 것이다. 제품은 8개를 보관한다. 그런데 덱은 그 사실을 모르므로, 스냅샷이 하나 들어올 때마다 노드 하나를 할당하고 밀려난 노드 하나를 반납했다. 3틱마다 스냅샷이 오면 초당 20번이다.

링은 같은 자리를 덮어쓴다.

<!-- snippet: include/containers/ring_buffer.h@ring-push -->
```cpp
void Push(const T& v) {
    if (Full()) ++head_;          // 오래된 것 버림
    buf_[tail_ & mask_] = v;
    ++tail_;
}
void Push(T&& v) {
    if (Full()) ++head_;
    buf_[tail_ & mask_] = std::move(v);
    ++tail_;
}
```

"가득 차면 가장 오래된 것을 버린다"는 의미론이 `SnapshotBufferT` 가 손으로 하던 `while (size > keep) pop_front()` 와 정확히 같다. 그래서 버퍼 쪽에서 자를 것이 없어졌다.

<!-- snippet: include/net/snapshot.hpp#Add -->
```cpp
bool Add(Snap s) {
    if (!snaps_.Empty()) {
        if (s.tick == snaps_.Newest().tick) {
            snaps_.Newest() = std::move(s);
            return true;
        }
        if (!SequenceMoreRecent(s.tick, snaps_.Newest().tick)) return false;
        // float render ticks cannot interpolate across uint32 wrap. Start a
        // fresh interpolation epoch when the monotonic counter wraps.
        if (s.tick < snaps_.Newest().tick) snaps_.Clear();
    }
    if (snaps_.Empty()) ++epoch_;
    snaps_.Push(std::move(s));
    return true;
}
```

의미론이 하나 바뀌었고, 그것을 숨기지 않는다. **`keep` 은 2의 거듭제곱으로 올림된다.** 링의 모듈로가 비트 마스크가 되려면 용량이 2의 거듭제곱이어야 하기 때문이다. 그래서 `keep` 은 "적어도 이만큼 보관한다"는 하한이고, 실제 보관 수는 `Capacity()` 다. 제품이 쓰는 값(8)과 기본값(16)이 둘 다 2의 거듭제곱이라 제품 동작은 바뀌지 않는다.

카운터 순환에서 링을 비우는 경로도 그대로다. `Clear()` 는 인덱스 두 개를 0으로 되돌리는 것이 전부라 이 동작에 할당이 없다.

## 8. 해시맵 순회를 시뮬 경로에 넣지 않는다

해시맵을 들여올 때 지켜야 할 규칙이 하나 있다. **순회 순서가 삽입 순서와 무관한 자료구조를 시뮬 갱신 경로에 넣지 않는다.**

이유는 결정성이다. 해시맵의 순회 순서는 키의 해시값과 용량에 달려 있다. 용량은 삽입 이력에 달려 있다. 시뮬이 그 순서로 상태를 갱신하면, 같은 논리적 상태가 다른 순서로 처리되는 순간이 생기고, 골든 해시가 깨진다.

그래서 두 해시맵 모두 **조회에만** 쓴다. `FindRow` 는 행 번호를 돌려줄 뿐이고, 그 번호를 쓰는 루프는 언제나 행 번호 오름차순으로 돈다. `SnapshotInterpolator` 도 b 의 벡터를 순서대로 돌면서 a 를 조회한다.

한 곳만 순회를 한다 — 파티클이다. 그리고 그 자리는 시뮬이 아니라 표시다.

<!-- snippet: include/game/defense_view.hpp@impact-particles -->
```cpp
inline void UpdateImpactParticles(
        const wire::Snapshot& view,
        OpenAddressingHashMap<uint32_t, math::Vec2>& previousProjectiles,
        engine::ParticleSystem& particles, float dt, FrameScratch& scratch) {
    const size_t previousCount = previousProjectiles.Size();
    math::Vec2* impacts = previousCount ? scratch.Alloc<math::Vec2>(previousCount) : nullptr;
    size_t vanished = 0;
    previousProjectiles.ForEach([&](uint32_t id, math::Vec2& position) {
        if (wire::FindById(view.projectiles, id)) return;
        impacts[vanished++] = position;
    });
    for (size_t k = 0; k < vanished; ++k) {
        for (int i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * 0.78539816339f;
            particles.Emit(
                {impacts[k].x, impacts[k].y, 0.0f},
                {std::cos(angle) * 35.0f, std::sin(angle) * 35.0f, 0.0f},
                0.25f, 1.5f);
        }
    }
    previousProjectiles.Clear();
    for (const auto& projectile : view.projectiles)
        previousProjectiles.Insert(projectile.id, projectile.pos);
    particles.Update(dt);
}
```

방출을 두 단계로 나눈 것도 이 규율의 일부다. 먼저 사라진 좌표를 프레임 스크래치에 모으고, 그다음에 방출한다. 순회 중에 맵을 건드리지 않으므로 `Clear`·재삽입이 방출 순서에 끼어들 수 없다.

## 9. 측정: 해시가 지는 워크로드

`id_map` 스위치의 측정 결과가 이 편에서 가장 놀라웠다. 두 워크로드에서 쟀다 — 타워 3대(발사체가 거의 없다)와 타워 16대(발사체가 상시 날아다닌다).

```
[id_map scale=large towers=3]
  linear enemies=2048 projectiles=0 ticks=600 total=8.1ms  projectile=0.06ms cleanup=0.33ms
  hash   enemies=2048 projectiles=0 ticks=600 total=13.8ms  projectile=0.10ms cleanup=5.29ms
[id_map scale=large towers=16]
  linear enemies=2048 projectiles=17 ticks=600 total=22.2ms  projectile=9.19ms cleanup=0.40ms
  hash   enemies=2048 projectiles=17 ticks=600 total=22.0ms  projectile=4.67ms cleanup=5.17ms
```

읽으면 이렇다.

**타워 3대에서 해시는 명백한 손해다.** 조회 절약은 0.04ms 인데 색인 유지가 4.96ms 를 먹는다. 총 시간이 1.7배가 된다.

**타워 16대에서 정확히 손익분기다.** 조회가 9.19 → 4.67ms 로 4.52ms 줄고, 압축이 0.40 → 5.17ms 로 4.77ms 는다. 총 시간은 22.2 vs 22.0 — 오차 범위다.

원인은 `CompactDead` 가 매 틱 색인을 통째로 다시 만든다는 데 있다. 순서 보존 압축은 살아남은 모든 행의 번호를 바꾸므로, 색인의 값 전부가 무효가 된다. 적 2048마리면 매 틱 2048번의 삽입이고, 그것이 5ms 다.

그래서 교차점은 **조회 횟수가 개체 수에 근접할 때**다. 발사체 17개가 적 2048마리를 스캔하면 34,816회 비교인데, 그것을 없애려고 2048회 삽입을 매 틱 치른다. 발사체가 개체 수의 1/2 쯤 되어야 확실히 이긴다.

이 게임의 현재 밸런스에서 그런 상태는 오지 않는다. 그래서 **기본값은 `linear` 로 둔다.** 해시 팔은 지우지 않는다 — 규모나 밸런스가 바뀌면 이 표를 다시 만들면 되고, 그때 코드를 새로 쓸 필요가 없다는 것이 스위치를 남기는 값어치다.

여기서 8편의 계측이 값을 했다. 벽시계 총합만 봤다면 "해시가 1.7배 느리다"까지만 알고 이유를 몰랐을 것이다. `projectile` 과 `cleanup` 을 따로 재니 **조회는 실제로 빨라졌고 색인 유지가 그것을 먹었다**는 것이 한눈에 보인다.

## 10. 변이: 배선이 죽으면 테스트가 죽는가

두 가지를 실제로 주입했다.

**변이 1 — SoA 압축을 swap-remove 로.** §4 에서 기각한 그 구현으로 되돌린다.

```cpp
if (write != read)
    soa_.SwapRemove(read);   // [MUTATION] 순서 보존 압축 -> swap-remove
```

```
[test] entity_store_ab
  FAIL ...\tests\test_entity_store_ab.cpp:93  CHECK_EQ(store.Size(), static_cast<size_t>(5))
  FAIL ...\tests\test_entity_store_ab.cpp:96  CHECK_EQ(store.Id(i), expected[i])
  FAIL ...\tests\test_entity_store_ab.cpp:97  CHECK_EQ(store.FindRow(expected[i]), static_cast<int>(i))
```

**변이 2 — 링의 "가장 오래된 것 버림"을 죽인다.**

```cpp
void Push(const T& v) {
    if (false) ++head_;          // [MUTATION] 오래된 것 버림
```

```
[test] snapshot_ring
  FAIL ...\tests\test_snapshot_ring.cpp:42  CHECK_EQ(buffer.Size(), static_cast<size_t>(8))
  FAIL ...\tests\test_snapshot_ring.cpp:44  CHECK_EQ(buffer.OldestTick(), tick - 7)
```

`OldestTick() == tick - 7` 이 무너지는 것이 이 변이의 핵심이다. 보간 창의 시작점이 어긋나면 클라이언트는 있지도 않은 과거를 그린다.

둘 다 복원 후 재통과했다(§11).

## 11. 실행과 관측

### 두 저장이 같은 세계를 돌리는가

```powershell
cd C:/Users/REIN/Documents/VeLog/projects/mega
cmake --build build --config Release
.\build\bin\Release\test_entity_store_ab.exe
```

```
[test] entity_store_ab
[entity_store scale=large]
  aos  enemies=2048 ticks=600 total=15.4ms  advance=3.65ms fire=8.37ms projectile=2.74ms cleanup=0.40ms
  soa  enemies=2048 ticks=600 total=14.8ms  advance=4.00ms fire=7.32ms projectile=2.62ms cleanup=0.67ms
[test] entity_store_ab: 75 checks, 0 failures
```

세 번 연속 실행의 시스템별 수치:

| 회차 | 팔 | total | advance | fire | projectile | cleanup |
|---|---|---|---|---|---|---|
| 1 | aos | 15.4 | 3.65 | 8.37 | 2.74 | 0.40 |
| 1 | soa | 14.8 | 4.00 | 7.32 | 2.62 | 0.67 |
| 2 | aos | 15.9 | 3.80 | 8.70 | 2.78 | 0.38 |
| 2 | soa | 15.0 | 4.00 | 7.38 | 2.65 | 0.70 |
| 3 | aos | 14.5 | 3.40 | 7.97 | 2.54 | 0.35 |
| 3 | soa | 13.9 | 3.77 | 6.83 | 2.47 | 0.63 |

방향이 세 번 다 같다 — `fire` 는 SoA, `advance`·`cleanup` 은 AoS, 합계는 SoA.

`75 checks` 안에는 3000틱 매치의 해시 동일성, 규모를 올린 1500틱 매치의 해시 동일성, 다른 네 스위치를 전부 켠 조합의 해시 동일성, 그리고 조준 오라클 대조(저장 2종 × 색인 2종 × 모드 3개)가 들어 있다.

### id_map 두 워크로드

```powershell
.\build\bin\Release\test_id_map_ab.exe
```

```
[test] id_map_ab
[id_map scale=large towers=3]
  linear enemies=2048 projectiles=0 ticks=600 total=8.1ms  projectile=0.06ms cleanup=0.33ms
  hash   enemies=2048 projectiles=0 ticks=600 total=13.8ms  projectile=0.10ms cleanup=5.29ms
[id_map scale=large towers=16]
  linear enemies=2048 projectiles=17 ticks=600 total=22.2ms  projectile=9.19ms cleanup=0.40ms
  hash   enemies=2048 projectiles=17 ticks=600 total=22.0ms  projectile=4.67ms cleanup=5.17ms
[test] id_map_ab: 2617 checks, 0 failures
```

### 링 의미론

```powershell
.\build\bin\Release\test_snapshot_ring.exe
```

```
[test] snapshot_ring
[test] snapshot_ring: 137 checks, 0 failures
```

### 기본 팔이 그대로인가

```powershell
.\build\bin\Release\mega_defense.exe --selftest
.\build\bin\Release\mega_defense.exe --ab entity_store=soa --selftest
.\build\bin\Release\mega_defense.exe --ab id_map=hash --selftest
```

세 실행 모두:

```
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

`ticks` · `towers` · `wave` · `packet` 네 값이 세 팔에서 같다. 저장을 바꿔도, 색인을 바꿔도 게임은 같은 판을 돈다.

### 변이 복원 후

```powershell
.\build\bin\Release\test_entity_store_ab.exe
.\build\bin\Release\test_snapshot_ring.exe
```

```
[test] entity_store_ab: 75 checks, 0 failures
[test] snapshot_ring: 137 checks, 0 failures
```

---

## 결론

이 편이 바꾼 것은 네 자리다.

**적 저장이 배치를 고를 수 있게 됐다.** 규칙과 저장이 분리되고, 산술은 여전히 한 벌이다. 두 배치가 3000틱 뒤에 비트 동일한 상태에 도달한다.

**상한이 생겼다.** 동시 생존 적 64 ↔ 2048. 그 상한이 있어야 다음 두 편의 버퍼 예산이 성립한다.

**id 대응이 두 구현을 갖는다.** 그리고 측정이 "지금 밸런스에서는 선형 스캔이 낫다"고 답했다. 그 답은 코드가 아니라 표에 적혀 있고, 밸런스가 바뀌면 표만 다시 만들면 된다.

**스냅샷 보관과 파티클 추적이 요구에 맞는 자료구조로 갔다.** 둘 다 A/B 가 아니라 교체다 — 상한이 이미 있는 자리에 가변 컨테이너를 쓰고 있었던 것이 애초에 어긋남이었다.

측정에서 배운 것을 그대로 적으면 이렇다. **SoA 의 이득은 전진 루프가 아니라 조준 스캔에서 나왔고, 크기는 4%다.** 그리고 **해시 색인은 이 워크로드에서 손해다.** 둘 다 착수 전의 예상과 달랐다.

현재 상태의 한계.

**두 팔의 비교는 언제나 다른 프로세스다.** 같은 실행 안에서 번갈아 재면 캐시 상태가 섞이므로 팔마다 새 프로세스를 띄운다. 대신 실행 간 편차(±5%)가 측정 하한이 된다.

**SoA 의 열 목록은 손으로 고른 것이다.** 어느 열이 함께 읽히는지를 재서 정한 것이 아니라, 루프를 읽고 판단했다. 열이 늘거나 루프가 바뀌면 그 판단은 다시 해야 하고, 그 재판단을 강제하는 장치는 없다.

**색인 재구축이 압축 안에 있다.** 순서 보존 압축이 모든 행 번호를 바꾸므로 색인 전체가 무효가 된다. 행 번호가 아니라 안정된 슬롯을 키로 쓰면 이 비용이 사라지지만, 그러면 스냅샷 순서가 슬롯 순서가 되어 와이어 포맷이 바뀐다.

**규모를 올릴 수는 있어도 그 규모에서 게임이 성립하지는 않는다.** `scale=large` 는 측정용이다. 적 2048마리는 와이어 상한 64를 훨씬 넘으므로 협동 클라이언트에는 그중 64마리만 보인다 — 잘림 카운터가 그 사실을 세지만, 그 상태로 플레이할 수 있는 것은 아니다.
