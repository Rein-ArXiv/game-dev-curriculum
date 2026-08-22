## 들어가며

1편의 도달점은 "그릴 수 있다"였다. 픽셀·도형·스프라이트·글자·창이 있고, `DrawGame` 이 참조하는 상태 구조체는 비어 있었다.

이 편의 도달점은 **그 구조체를 채우는 규칙**이다. 끝나면 창 없이도 게임 한 판이 완주한다 — 적이 스폰되고, 정해진 경로를 걷고, 타워가 조준해 쏘고, 발사체가 맞고, 골드가 늘고, 웨이브가 넘어가고, 코어가 버티거나 부서진다. 이 편의 코드는 렌더를 한 번도 부르지 않는다.

세워야 할 것이 넷이다.

1. **`DefenseMap`** — 격자와 경로. 그리고 텍스트 맵이 *유효한 맵인가*를 판정하는 불변식.
2. **엔티티와 스펙 표** — 여러 종류의 타워와 적. 숫자가 어디 있어야 하는가.
3. **`GameWorld::Step`** — 한 틱을 여섯 시스템으로 쪼갠 것. 그 **순서**가 왜 그 순서인가.
4. **시간** — 실시간 프레임과 시뮬 틱을 분리하는 누적기, 그리고 그 누적기가 죽지 않게 하는 상한.

이 편이 다루지 않는 것: 입력이 이 규칙에 닿는 경로(마우스·키보드·명령), 콘텐츠 파일에서 웨이브를 읽어 오는 경로, 네트워크. 이 편의 `GameWorld` 는 인자로 받은 웨이브 목록을 그대로 돌릴 뿐이고, 그것을 누가 어떻게 만들었는지는 모른다.

---

## 목차
1. [자유 좌표를 버리고 격자를 택했다](#1-자유-좌표를-버리고-격자를-택했다)
2. [FromAscii: 한 번의 걷기로 네 가지를 거부한다](#2-fromascii-한-번의-걷기로-네-가지를-거부한다)
3. [숫자는 표에 있고 코드에 없다](#3-숫자는-표에-있고-코드에-없다)
4. [한 틱의 여섯 시스템과 그 순서](#4-한-틱의-여섯-시스템과-그-순서)
5. [타게팅: 세 모드를 하나의 점수 함수로](#5-타게팅-세-모드를-하나의-점수-함수로)
6. [발사체와 광역 피해: 추적과 명중](#6-발사체와-광역-피해-추적과-명중)
7. [경제: 누적 투자라는 하나의 장부](#7-경제-누적-투자라는-하나의-장부)
8. [시간: 누적기와 죽음의 나선 차단](#8-시간-누적기와-죽음의-나선-차단)
9. [결정성은 나중에 얻는 성질이 아니다](#9-결정성은-나중에-얻는-성질이-아니다)
10. [실행과 관측](#10-실행과-관측)

---

## 1. 자유 좌표를 버리고 격자를 택했다

처음 만든 `GameWorld` 는 격자가 없었다. `Init(corePos, spawnPos, waves)` 로 두 점만 받고, 적은 코어를 향해 직선으로 걸었다. 타워는 아무 데나 놓을 수 있었다.

그 형태는 규칙을 검증하기에는 충분했지만 **게임이 아니었다**. 타워 디펜스의 재미 전체가 "적이 반드시 지나가는 길"과 "그 길을 볼 수 있는 자리"의 관계에서 나오는데, 직선 이동에는 그 관계가 없다. 최적 배치가 코어 바로 앞 한 점으로 수렴하고, 그 뒤로는 아무 선택도 남지 않는다.

그래서 `DefenseMap` 을 넣었다. 격자는 다섯 종류의 타일을 갖는다.

| 타일 | 문자 | 의미 |
|---|---|---|
| `Buildable` | `.` | 타워를 놓을 수 있다 |
| `Path` | `P` | 적이 지난다. 건설 불가 |
| `Spawn` | `S` | 경로의 시작 |
| `Core` | `C` | 경로의 끝. 여기 닿으면 코어가 피해를 입는다 |
| `Blocked` | `#` | 장식·지형. 건설도 통행도 불가 |

두 `Init` 오버로드를 **둘 다 남겼다.** 격자 없는 쪽은 죽은 코드가 아니라 시험 장치다 — 경로 추종과 무관하게 사격·경제·승패만 재고 싶은 테스트가 그쪽을 쓴다. 그 구분은 `mapEnabled_` 불린 하나로 표현되고, 이동 시스템이 그 값으로 갈린다.

격자가 들어오면서 배치 규칙도 격자의 것이 됐다.

<!-- snippet: include/game/game.hpp#CanPlaceTower -->
```cpp
bool CanPlaceTower(const math::Vec2& pos) const {
    if (towers_.size() >= kMaxTowers) return false;
    if (!mapEnabled_) return true;
    if (!map_.IsBuildable(pos)) return false;
    math::Vec2 snapped = map_.Snap(pos);
    float minDistance = map_.CellSize() * 0.75f;
    float minDistanceSq = minDistance * minDistance;
    for (const Tower& tower : towers_)
        if (math::LengthSq(tower.pos - snapped) < minDistanceSq) return false;
    // ...
    return true;
}
```

거리 검사가 격자 검사와 **별도로** 있는 이유가 있다. 타일은 `IsBuildable` 로 걸러지지만, 그것만으로는 같은 셀에 두 타워가 겹쳐 서는 것을 막지 못한다 — `Snap` 이 둘을 같은 중심으로 보내기 때문이다. "셀 하나에 타워 하나"를 셀 점유 배열로 표현할 수도 있었지만, 그러면 격자 없는 모드에 그 배열이 존재하지 않아 규칙이 두 갈래가 된다. 거리 비교는 두 모드에서 같은 코드로 성립한다.

`kMaxTowers = 16` 은 **밸런스 상한**이다. 뒤에서 전송 포맷에도 16이라는 상한이 나오는데, 두 값이 같은 것은 우연이고 이유가 다르다. 그 구분은 4편에서 다룬다.

## 2. FromAscii: 한 번의 걷기로 네 가지를 거부한다

맵이 텍스트 파일에서 오는 순간, 그 파일은 **신뢰 경계 밖 입력**이 된다. 사용자가 편집하라고 만든 파일이므로 "잘못 쓴 맵"이 정상적으로 도착한다.

거부해야 할 것이 넷이다.

1. **끊긴 경로** — `S` 에서 출발해 `C` 에 닿지 못한다.
2. **분기** — 한 칸에서 갈 수 있는 다음 경로 칸이 둘 이상이다.
3. **순환** — 경로가 자기 자신으로 돌아온다.
4. **미사용 조각** — 주 경로와 이어지지 않은 `P` 덩어리가 따로 떠 있다.

넷을 각각 검사하면 검사기가 넷이 되고, 넷 중 하나가 다른 셋과 어긋나는 순간 통과해서는 안 될 맵이 통과한다. 대신 **한 번의 걷기**로 넷을 동시에 만족시켰다.

<!-- snippet: include/game/defense_map.hpp#FromAscii -->
```cpp
static bool FromAscii(const std::vector<std::string>& rows, int cellSize,
                      int originY, DefenseMap& out) {
    if (rows.empty() || rows.size() > 64 || rows.front().empty() || rows.front().size() > 64)
        return false;
    size_t width = rows.front().size();
    for (const std::string& row : rows) if (row.size() != width) return false;
    // ... 문자 → 타일 변환. 'S' 와 'C' 는 각각 한 번만 나올 수 있고,
    //     정의되지 않은 문자는 그 자리에서 거부한다. pathTileCount 를 센다.
    if (spawn.x < 0 || core.x < 0 || pathTileCount < 2) return false;

    std::vector<uint8_t> visited(static_cast<size_t>(map.columns_ * map.rows_), 0);
    std::vector<Cell> cells;
    Cell current = spawn;
    cells.push_back(current);
    visited[static_cast<size_t>(current.y * map.columns_ + current.x)] = 1;
    const int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!(current.x == core.x && current.y == core.y)) {
        std::array<Cell, 4> candidates{};
        int candidateCount = 0;
        for (const auto& direction : directions) {
            int nx = current.x + direction[0];
            int ny = current.y + direction[1];
            if (!map.InBounds(nx, ny)) continue;
            TileType tile = map.Tile(nx, ny);
            bool isPath = tile == TileType::Path || tile == TileType::Spawn || tile == TileType::Core;
            size_t index = static_cast<size_t>(ny * map.columns_ + nx);
            if (isPath && !visited[index]) candidates[static_cast<size_t>(candidateCount++)] = {nx, ny};
        }
        if (candidateCount != 1) return false;
        current = candidates[0];
        visited[static_cast<size_t>(current.y * map.columns_ + current.x)] = 1;
        cells.push_back(current);
    }
    if (static_cast<int>(cells.size()) != pathTileCount) return false;
    // ...
    map.pathCells_.AssignFrom(cells.data(), cells.size());
    out = std::move(map);
    return true;
}
```

두 줄이 넷을 전부 잡는다.

**`if (candidateCount != 1) return false;`** — 방문하지 않은 이웃 경로 칸이 정확히 하나여야 한다.
- 0개면 막다른 길이다(끊긴 경로, 또는 순환해 돌아와 갈 곳이 없어진 경우).
- 2개 이상이면 분기다. 순환도 여기서 잡힌다 — 고리가 있으면 고리에 진입하는 칸에서 후보가 둘이 되기 때문이다.

**`if (cells.size() != pathTileCount) return false;`** — 걸으면서 방문한 칸 수가 파일 전체의 경로 칸 수와 같아야 한다. 다르면 어딘가에 걷지 못한 `P` 덩어리가 남아 있다는 뜻이다(미사용 조각).

방문 표시가 필수인 이유는 미묘하다. `visited` 가 없으면 두 번째 칸에서 이미 후보가 둘이 된다 — 앞으로 갈 칸과 방금 온 칸. 즉 이 알고리즘은 "왔던 길로 돌아가지 않는다"를 방문 배열로 표현하고, 그 배열이 동시에 순환 검출기 노릇을 한다.

`while` 이 끝나지 않을 수 있는가? 없다. 매 반복이 방문하지 않은 칸으로만 이동하고 그 칸을 즉시 방문 표시하므로, 반복 수는 격자 칸 수(최대 64×64)로 상한이 잡힌다.

크기 상한 64×64 도 같은 이유다 — 파일이 주장하는 폭·높이를 그대로 믿으면 안 된다. 그리고 모든 행의 길이가 같아야 한다는 검사가 먼저 오므로, 그 뒤의 인덱싱은 범위 검사 없이 안전하다.

`pathTileCount < 2` 는 `S` 와 `C` 가 같은 칸이거나 인접하지 않은 퇴화 맵을 거른다. 경로가 한 칸이면 `Path()` 가 원소 하나인 벡터를 돌려주고, 이동 시스템이 "다음 목표"를 찾지 못한다.

## 3. 숫자는 표에 있고 코드에 없다

타워 넷과 적 다섯으로 시작한 두 표는 지금 여섯과 여섯이다. 종류의 차이는 그때나 지금이나 전부 숫자다 — 그 숫자를 어디에 둘지가 이 절의 전부고, 표가 둘 늘어나는 동안 그 자리가 바뀌지 않았다는 것이 그 선택의 값이다.

- **타워마다 클래스**: 가상 함수로 사격 동작을 다르게. 기각 — 넷이 전부 "사거리 안 하나를 골라 발사체를 낸다"로 같아서, 다형성이 표현할 차이가 없다.
- **적마다 절대 스탯**: 웨이브 정의에 종류별 완전한 스탯을 적기. 기각 — 웨이브 파일이 종류 수만큼 길어지고, "탱크는 느리다"는 규칙이 파일마다 반복돼 어긋날 수 있다.
- **배수 표**: 웨이브가 기본값을 주고 종류가 배수를 곱한다. (선택)

<!-- snippet: include/game/game.hpp#TowerSpecFor -->
```cpp
inline TowerSpec TowerSpecFor(TowerType t) {
    static constexpr std::array<TowerSpec, 6> specs{{
        {90,  25, 0.80f,  4,  70},   // Cannon: 고데미지·느림
        {75,   6, 0.12f,  2,  60},   // Gatling: 저데미지·연사
        {70,  14, 0.60f, 16,  90},   // Splash: 광역
        {180, 40, 1.40f,  2, 110},   // Sniper: 장거리·고데미지
        {85,   4, 0.90f,  6,  80},   // Frost: 피해는 명목뿐, 값은 재우는 것
        {200,  0, 1.00f,  0,  45},   // Radar: 쓰지 않는다. 은신을 드러낸다
    }};
    size_t index = static_cast<size_t>(t);
    return index < specs.size() ? specs[index] : TowerSpec{80, 10, 0.5f, 4, 60};
}
```

<!-- snippet: include/game/entity_store.hpp#EnemyModFor -->
```cpp
inline EnemyMod EnemyModFor(EnemyType t) {
    static constexpr EnemyMod mods[6] = {
        {1.0f, 1.0f, 1.0f, 1},
        {0.6f, 1.8f, 1.0f, 1},    // Fast: 빠르고 약함
        {3.0f, 0.6f, 2.0f, 3},    // Tank: 튼튼·느림·강타·고보상
        {0.4f, 1.2f, 0.5f, 1},    // Swarm: 다수 약체
        {12.0f, 0.4f, 5.0f, 10},  // Boss: 거대 체력·매우 느림·치명타·대량 보상
        {0.7f, 1.5f, 1.0f, 2},    // Stalker: 은신·빠름. 조준되지 않는 것이 방어력이다
    };
    size_t index = static_cast<size_t>(t);
    return index < 6 ? mods[index] : EnemyMod{1, 1, 1, 1};
}
```

두 함수의 마지막 줄이 같은 모양인 것이 우연이 아니다. **열거값은 와이어를 건너온다.** 원격 클라이언트가 보낸 바이트가 `TowerType` 으로 캐스트되어 여기 도착할 수 있고, 그 값이 범위 밖일 수 있다. 그때 `specs[index]` 를 그대로 읽으면 범위 밖 접근이다. 기본값을 돌려주는 것은 관대함이 아니라 **메모리 안전**이다.

같은 이유로 `TargetMode` 를 포함한 와이어를 건너는 모든 열거형이 `uint8_t` 로 기반 타입을 고정하고 있다. 기반 타입이 없으면 `{Nearest, First, Strongest}` 의 열거 범위는 `{0..3}` 이라 200 을 캐스트한 결과가 unspecified 가 되고, 그 뒤의 어떤 비교도 아무것도 보장하지 못한다.

곱셈 방향도 결정 사항이다. 배수는 웨이브 값을 **곱한다**:

```
e.hp    = w.hp    * mod.hpMul
e.speed = w.speed * mod.speedMul
e.damage = 10.0f  * mod.damageMul
```

코어 피해만 웨이브가 아니라 상수 10 에 곱한다. 웨이브 정의에 "코어 피해" 필드를 두지 않은 것은 그 값이 밸런스에서 거의 조정되지 않고, 필드가 늘면 웨이브 파일의 한 줄이 그만큼 길어지기 때문이다. 대신 종류별 차이는 배수로 남아 있다 — 보스는 5배, 무리는 0.5배다.

## 4. 한 틱의 여섯 시스템과 그 순서

<!-- snippet: include/game/game.hpp@world-step -->
```cpp
void Step(float dt) {
    if (won_ || lost_) return;
    MEGA_PROFILE_SCOPE("sim.step");
    scratch_.Begin();
    SpawnSystem(dt);
    // ...
    EnemyAdvanceSystem(dt);
    TowerFireSystem(dt);
    ProjectileSystem(dt);
    // ...
    WinLoseSystem();
    scratch_.End();
}
```

이 시스템 함수들은 전부 `void(float)` 또는 `void()` 이고, 서로를 부르지 않는다. 통신은 멤버 저장 세 개(`enemies_`, `towers_`, `projs_`)뿐이다. 이 형태를 고른 이유는 **순서를 바꿔 보는 것이 가능해야** 하기 때문이다 — 순서가 함수 호출 그래프에 숨어 있으면 순서에 대해 논증할 수 없다.

순서에는 각각 근거가 있다.

**스폰이 먼저.** 이번 틱에 태어난 적이 이번 틱에 움직이고 이번 틱에 맞을 수 있다. 반대로 스폰을 마지막에 두면 갓 태어난 적이 한 틱 동안 얼어 있고, 스폰 지점 근처의 타워가 그 한 틱을 놓친다.

**진격이 사격보다 먼저.** 타워는 *이동을 마친* 위치를 조준한다. 반대 순서면 타워가 한 틱 전 위치를 쏘고, 빠른 적일수록 그 오차가 커진다. 게임에서 "리드 샷"이 필요해지는데, 이 게임의 발사체는 목표를 추적하므로 그 복잡성을 살 이유가 없다.

**사격이 발사체보다 먼저.** 이번 틱에 발사된 것이 이번 틱에 조금 날아간다. 반대면 모든 발사체가 한 틱을 정지 상태로 보낸다 — 개틀링(0.12초 간격, 약 7틱)에서는 그 한 틱이 눈에 보인다.

**정리가 승패 판정보다 먼저.** `WinLoseSystem` 은 `waveIdx_` 만 보므로 정리 순서에 직접 의존하지는 않지만, 승패가 확정된 틱에도 죽은 엔티티가 벡터에 남아 있으면 그 상태가 그대로 마지막 스냅샷이 된다. 정리를 먼저 하면 승리 화면의 상태가 깨끗하다.

**승패 판정이 마지막.** `Step` 의 첫 줄 `if (won_ || lost_) return;` 과 짝이다. 판정이 선 순간부터 시뮬은 완전히 멈춘다.

<!-- snippet: include/game/game.hpp#SpawnSystem -->
```cpp
void SpawnSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.spawn");
    if (waveIdx_ >= (int)waves_.size()) return;
    const WaveDef& w = waves_[waveIdx_];
    spawnTimer_ -= dt;
    EnemyMod mod = EnemyModFor(w.type);
    while (spawned_ < w.count && spawnTimer_ <= 0.0f) {
        // 상한에 닿으면 이번 틱은 더 넣지 않는다.
        if (enemies_.Full()) break;
        Enemy e;
        e.pos = spawnPos_ + math::Vec2{(Rand01() - 0.5f) * 20.0f, (Rand01() - 0.5f) * 20.0f};
        e.hp = e.maxHp = w.hp * mod.hpMul;
        e.speed  = w.speed * mod.speedMul;
        e.reward = (int)(w.reward * mod.rewardMul);
        e.damage = 10.0f * mod.damageMul;       // 기본 코어 데미지 10 × 배수
        e.type   = w.type;
        e.id     = nextEntityId_++;
        e.pathIndex = (mapEnabled_ && path_.Size() > 1 && !GridNav()) ? 1 : 0;
        // ...
        enemies_.Push(e);
        // ...
        ++spawned_;
        spawnTimer_ += w.interval;
    }
    // 이번 웨이브 다 스폰 + 전멸 → 다음 웨이브.
    if (spawned_ >= w.count && AliveEnemies() == 0) {
        ++waveIdx_; spawned_ = 0; spawnTimer_ = 0;
    }
}
```

`while` 이고 `if` 가 아닌 것, 그리고 `spawnTimer_ = w.interval` 이 아니라 `spawnTimer_ += w.interval` 인 것이 짝이다. 한 틱이 스폰 간격보다 길면(무리 웨이브는 간격 0.14초, 틱은 0.0167초라 정상 상황은 아니지만 프레임이 밀리면 일어난다) 그 틱에 여러 마리가 나와야 하고, 누적 대입은 **소수점 나머지를 보존**해 장기적으로 스폰 리듬이 밀리지 않게 한다.

`pathIndex` 가 1 에서 시작하는 것도 의도다. 0번 경로 칸은 스폰 지점 자신이므로, 0을 목표로 두면 첫 틱에 적이 자기가 서 있는 곳으로 걸어간다. 다만 스폰 위치에 ±10픽셀 흔들림이 있어 실제로는 스폰 칸 중심으로 되돌아가는 짧은 움직임이 생기는데, 그것을 피하려고 1부터 겨눈다.

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

`EnemyAdvanceSystem` 은 개체마다 이 둘 중 하나를 부른다. 산술이 시스템 안이 아니라 자유 함수에 있는 것은 같은 규칙을 여러 저장 배치가 공유해야 하기 때문이고, 코어 체력을 직접 깎지 않고 `damageOut` 으로 돌려주는 것은 그 호출이 프레임 스레드가 아닌 곳에서 일어날 수 있기 때문이다.

경로 추종의 핵심은 **이동 예산을 다 쓸 때까지 도는 안쪽 루프**다. 한 틱의 이동량이 남은 구간보다 길면 코너를 돌아 다음 구간으로 이어져야 한다. `if` 하나로 짜면 적이 코너마다 한 틱씩 멈추고, 빠른 적이 느린 적보다 코너에서 더 손해를 본다.

`+ 1e-4f` 는 부동소수 여유다. 없으면 `dist` 와 `remaining` 이 수치적으로 같을 때 `else` 로 빠져 `remaining` 만 0이 되고 `pathIndex` 는 그대로다 — 적이 코너에 붙어 한 틱을 낭비한다.

`pathIndex >= path_.size()` 가 곧 "코어 도달"이다. 좌표 거리로 판정하지 않는 이유는 마지막 경로 칸이 곧 코어 칸이라 인덱스가 더 정확하고, 무엇보다 **판정에 임계값이 없다**. 격자 없는 모드는 임계값 6.0f 을 쓰는데, 그 숫자가 필요하다는 사실 자체가 격자 모드의 인덱스 판정이 더 나은 이유다.

## 5. 타게팅: 세 모드를 하나의 점수 함수로

타워가 사거리 안 여러 적 중 하나를 고른다. 모드는 셋이다 — 가장 가까운 적, 코어에 가장 가까운 적, 체력이 가장 높은 적.

세 개의 루프를 쓰는 대신 **점수 하나를 최소화**하는 형태로 통일했다.

<!-- snippet: include/game/game.hpp#SelectTarget -->
```cpp
inline int SelectTarget(const math::Vec2& towerPos, const math::Vec2& corePos,
                        const std::vector<Enemy>& enemies, float range, TargetMode mode) {
    int best = -1;
    float bestScore = 0;
    float r2 = range * range;
    for (int i = 0; i < (int)enemies.size(); ++i) {
        if (!enemies[i].alive) continue;
        float d2 = math::LengthSq(enemies[i].pos - towerPos);
        if (d2 > r2) continue;
        float score;
        switch (mode) {
            case TargetMode::Nearest:   score = d2; break;
            case TargetMode::First:
                score = enemies[i].pathIndex > 0
                    ? -static_cast<float>(enemies[i].pathIndex) * 1000000.0f
                    : math::LengthSq(enemies[i].pos - corePos);
                break;
            case TargetMode::Strongest: score = -enemies[i].hp; break;
            default:                    score = d2; break;
        }
        if (best < 0 || score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}
```

세 점수 함수는 전부 "작을수록 좋다"로 뒤집혀 있다. `Strongest` 가 `-hp` 인 것이 그 변환이다.

`First` 가 가장 미묘하다. "코어에 가장 가까운 적"의 자연스러운 척도는 코어까지의 직선 거리인데, **경로가 굽어 있으면 직선 거리가 경로 진행도를 배신한다.** 이 맵에서 (12,3) 은 코어(8,14)까지 직선으로 멀지만, 경로상으로는 (4,10) 보다 앞서 있지 않다. 실제로는 경로 인덱스가 진행도의 정확한 척도다.

그래서 `pathIndex` 가 있으면 그것을 쓰고, `-index * 1000000.0f` 로 부호를 뒤집는다. 100만 배는 인덱스 차이 하나가 어떤 거리 값보다도 크게 만들기 위한 것이다 — 두 척도를 한 `float` 에 섞을 때 우선순위를 보장하는 가장 단순한 방법이다. 격자 없는 모드는 `pathIndex` 가 0 이므로 직선 거리로 되돌아간다.

거리 비교가 전부 `LengthSq` 인 것은 제곱근을 피하려는 것이다. 정렬 순서가 같으므로 결과가 바뀌지 않고, 사거리 검사도 `r2` 와 비교해 같은 규칙을 지킨다.

## 6. 발사체와 광역 피해: 추적과 명중

<!-- snippet: include/game/game.hpp@projectile-system -->
```cpp
void ProjectileSystem(float dt) {
    MEGA_PROFILE_SCOPE("sim.projectile");
    const size_t enemyCount = enemies_.Size();
    uint32_t* candidates = enemyCount ? scratch_.Alloc<uint32_t>(enemyCount) : nullptr;

    projs_.ForEach([&](Projectile& p) {
        int tracked = enemies_.FindRow(p.targetId);
        if (tracked >= 0 && !enemies_.Alive(static_cast<size_t>(tracked))) tracked = -1;
        if (tracked >= 0) p.target = enemies_.Pos(static_cast<size_t>(tracked));

        math::Vec2 d = p.target - p.pos;
        float dist = math::Length(d);
        float step = p.speed * dt;
        if (dist > step + 1e-3f) p.pos += d * (step / dist);
        else p.pos = p.target;

        const bool hitTracked =
            tracked >= 0 && physics::CollideCircleCircle(
                p.pos, 2.0f, enemies_.Pos(static_cast<size_t>(tracked)),
                EnemyCollisionRadius(enemies_.Type(static_cast<size_t>(tracked)))).hit;
        if (!hitTracked && dist > step + 1e-3f) return;

        projs_.Kill(p);
        const math::Vec2 impact = tracked >= 0
            ? enemies_.Pos(static_cast<size_t>(tracked)) : p.target;
        const float splash = p.splash;
        const float damage = p.damage;
        const float stun = p.stun;

        size_t hits = 0;
        for (size_t i = 0; i < enemyCount; ++i) {
            if (!enemies_.Alive(i)) continue;
            if (!physics::CollideCircleCircle(impact, splash, enemies_.Pos(i),
                                              EnemyCollisionRadius(enemies_.Type(i))).hit)
                continue;
            candidates[hits++] = static_cast<uint32_t>(i);
        }
        for (size_t k = 0; k < hits; ++k) {
            const size_t i = candidates[k];
            // ...
            const float hp = enemies_.Hp(i) - damage;
            enemies_.SetHp(i, hp);
            if (hp <= 0.0f) {
                enemies_.Kill(i);
                gold_ += enemies_.Reward(i);
                // ...
            }
        }
    });
}
```

발사체는 **id 로 목표를 추적한다.** 인덱스나 포인터로 잡지 않는 이유는 `CleanupSystem` 이 죽은 행을 걷어내며 저장을 압축하기 때문이다 — 그 순간 모든 인덱스가 밀리고 모든 포인터가 무효가 된다. id 는 `nextEntityId_` 로 부여되는 단조 증가 정수라 재사용되지 않는다.

목표가 죽었으면 `tracked` 는 널이 되고 `p.target` 은 **마지막으로 본 위치에 그대로 남는다.** 발사체가 사라지지 않고 그 자리까지 날아가 터진다. 대안은 목표 상실 시 발사체를 즉시 없애는 것이었는데, 그러면 광역 타워가 밀집한 무리를 상대로 손해를 본다 — 첫 마리가 죽는 순간 뒤따르는 발사체가 전부 증발한다.

명중 판정이 둘이다.
- `hitTracked` — 추적 중인 목표와 실제로 겹쳤다. 목표가 발사체 쪽으로 다가오는 경우 도착 전에 맞는다.
- `dist <= step + 1e-3f` — 이번 틱에 목표 지점에 도달했다. 목표를 잃은 발사체가 끝나는 경로다.

폭발 중심 `impact` 는 목표가 살아 있으면 목표 위치, 아니면 마지막 목표 지점이다. 발사체의 현재 위치가 아니다 — 그래야 광역 원이 적을 중심으로 퍼져 대칭이 된다.

충돌 판정이 `physics::CollideCircleCircle` 인 것은 이 게임이 물리 계층을 실제로 쓴다는 뜻이다. 두 원의 겹침은 두 줄이면 직접 쓸 수 있지만, 직접 쓰는 순간 "이 저장소의 narrow phase 는 아무도 안 쓴다"가 되고 그 코드는 테스트만 보는 코드가 된다.

<!-- snippet: include/game/game.hpp#CleanupSystem -->
```cpp
void CleanupSystem() {
    MEGA_PROFILE_SCOPE("sim.cleanup");
    projs_.EndStep();       // 미뤄 둔 발사체 블록이 여기서만 실제로 반납된다
    // ...
    enemies_.CompactDead();
}
```

두 압축 모두 **상대 순서를 보존한다.** 그것이 이 게임의 결정성 요구와 직결된다(9절). 더 빠른 swap-remove 를 쓰면 순서가 삭제 패턴에 의존하게 되고, 같은 입력에서 같은 결과가 나온다는 성질이 사라진다.

## 7. 경제: 누적 투자라는 하나의 장부

타워를 사고, 강화하고, 판다. 판매 환불을 얼마로 할 것인가가 문제다.

- **구매가의 70%** — 강화에 쓴 돈이 증발한다. 3티어 타워를 팔면 손해가 커서 아무도 팔지 않는다.
- **현재 티어의 정가 70%** — 정가 표를 티어별로 따로 관리해야 한다.
- **누적 투자의 70%** — 타워가 자기 장부를 들고 다닌다. (선택)

<!-- snippet: include/game/game.hpp#UpgradeTower -->
```cpp
bool UpgradeTower(int idx) {
    if (idx < 0 || idx >= (int)towers_.size() || won_ || lost_) return false;
    Tower& t = towers_[idx];
    if (t.tier >= 3) return false;
    int cost = UpgradeCost(idx);
    if (gold_ < cost) return false;
    gold_ -= cost;
    ++t.tier;
    t.damage *= 1.6f;
    t.range  *= 1.12f;
    // ...
    t.invested += cost;
    return true;
}
```

<!-- snippet: include/game/game.hpp#SellTower -->
```cpp
// 타워 판매: 누적 투자의 70% 환불 후 제거. 성공 시 환불 골드 반환(실패 0).
int SellTower(int idx) {
    if (idx < 0 || idx >= (int)towers_.size() || won_ || lost_) return 0;
    int refund = (towers_[idx].invested * 7) / 10;
    // ...
    gold_ += refund;
    towers_[idx] = towers_.back();   // swap-remove
    towers_.pop_back();
    // ...
    return refund;
}
```

`invested` 는 배치에서 `cost` 로 시작해 강화마다 더해진다. 그래서 환불 계산에 티어도, 종류도, 정가표도 필요 없다 — **정수 하나에 이력 전체가 접혀 있다.**

강화 비용은 `60 * tier` 다. 1→2 가 60, 2→3 이 120. 효과는 곱셈(데미지 1.6배, 사거리 1.12배)이라 비용은 선형인데 효과는 지수다. 상한 3티어가 그 발산을 막는다.

`SellTower` 만 swap-remove 를 쓴다. `CleanupSystem` 과 규칙이 다른 것이 눈에 걸리지만 이유가 있다 — 타워는 시뮬 갱신 경로에서 순회 순서가 결과에 영향을 주지 않는다. 타워는 서로 상호작용하지 않고, 각자 독립적으로 목표를 골라 쏘기 때문이다. 반면 적 벡터의 순서는 `SelectTarget` 의 동점 처리에 직접 영향을 준다.

판매·강화가 승패 후에 거부되는 것(`won_ || lost_`)은 결과가 확정된 뒤에도 골드가 움직이면 최종 점수가 흔들리기 때문이다.

## 8. 시간: 누적기와 죽음의 나선 차단

시뮬레이션은 고정 `dt` 로 돈다. 프레임 시간은 고정이 아니다. 그 둘을 잇는 것이 누적기다.

<!-- snippet: include/game/defense_app.hpp@fixed-step -->
```cpp
if (options_.mode != RunMode::Client) {
    if ((world_.Won() || world_.Lost()) && input_.WasPressed("restart")) Restart();
    int catchUpSteps = 0;
    if (paused_) accumulator_ = 0.0;
    while (!paused_ && accumulator_ >= kDefenseFixedDt && catchUpSteps < 5) {
        if (!world_.Won() && !world_.Lost()) world_.Step(kDefenseFixedDt);
        // ...
        ++hostTick_;
        accumulator_ -= kDefenseFixedDt;
        ++catchUpSteps;
        if (options_.mode == RunMode::Host && network_.PeerKnown()) {
            // ...
            if (hostTick_ % 3 == 0) {
                SendSnapshot();
                MaybeSendTerrain();
            }
        }
    }
    if (catchUpSteps == 5 && accumulator_ >= kDefenseFixedDt) accumulator_ = 0.0;
    view_ = CaptureView(world_, hostTick_);
} else {
    // ...
}
```

`kDefenseFixedDt` 는 `1.0f / 60.0f` 이고, 이 값이 시뮬의 유일한 시간 단위다. 가변 `dt` 로 적분하면 같은 입력이 프레임률에 따라 다른 결과를 내고, 그 순간 리플레이·네트워크 동기화·결정성이 전부 불가능해진다.

세 개의 방어가 겹쳐 있다.

**입력 클램프.** `Step` 진입부에서 `frameSeconds` 를 0.25초로 자른다. 창을 드래그하거나 디버거에서 멈췄다 재개하면 프레임 시간이 수 초로 튀는데, 그것을 그대로 누적하면 한 프레임에 수백 틱을 돌려 게임이 순간이동한다.

**보충 상한 5틱.** 한 프레임에 최대 5번만 시뮬한다. 이것이 죽음의 나선(spiral of death) 차단이다 — 시뮬이 프레임 예산보다 오래 걸리기 시작하면 다음 프레임의 `frameSeconds` 가 늘고, 그래서 더 많은 틱을 돌아야 하고, 그래서 더 오래 걸린다. 상한이 없으면 이 고리가 발산해 프로그램이 응답을 멈춘다.

**초과분 폐기.** `if (catchUpSteps == 5 && accumulator_ >= kDefenseFixedDt) accumulator_ = 0.0;` 상한에 걸렸는데도 누적기에 시간이 남았다면 그것은 갚을 수 없는 빚이다. 남겨 두면 다음 프레임에도 5틱을 꽉 채우고, 나선이 형태만 바뀌어 계속된다. 게임은 그만큼 느려지지만(slow motion) **응답은 유지된다** — 시뮬 시간의 정확성보다 살아 있는 프로그램을 택한 것이다.

일시정지는 `accumulator_ = 0.0` 으로 표현한다. 일시정지 중 흐른 실시간을 누적기에 남기면 재개 순간 5틱이 몰아친다.

`view_` 를 이 블록 뒤에서 다시 캡처하는 것도 시간 문제다. 이 블록 앞에서 이미 한 번 캡처했지만, 그것은 입력 처리(어느 타워를 클릭했는가)가 **이번 틱 시뮬 이전 상태**를 봐야 하기 때문이다. 그리기는 시뮬 이후 상태를 봐야 하므로 다시 캡처한다.

## 9. 결정성은 나중에 얻는 성질이 아니다

이 시뮬은 **같은 입력에서 같은 결과**를 낸다. 그 성질을 뒤에서 얻으려 하면 이미 늦기 때문에 여기서 세 가지를 지켰다.

**난수는 고정 시드 xorshift 하나.**

```cpp
float Rand01() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ >> 8) * (1.0f / 16777216.0f); }
```

`Init` 이 `rng_ = 0x1234567u` 로 되돌린다. `std::mt19937` + `random_device` 를 쓰면 실행마다 다른 게임이 되고, 표준 라이브러리 분포 객체는 구현마다 다른 값을 내므로 플랫폼 간에도 갈린다. `>> 8` 후 24비트만 쓰는 것은 `float` 의 가수가 24비트라 그 이상은 반올림으로 사라지기 때문이다.

**순회 순서가 안정적인 자료구조만 시뮬 갱신 경로에 둔다.** 엔티티는 전부 `std::vector` 이고 삭제는 순서 보존 `remove_if` 다. 해시 컨테이너를 여기 넣으면 순회 순서가 삽입 이력과 구현에 의존하게 되고, 그러면 동점 상황에서 다른 적이 선택된다.

**부동소수 연산 순서를 바꾸지 않는다.** 위 시스템들은 전부 순차 루프다. 병렬화하면 누적 순서가 바뀌어 결과가 갈린다.

이 성질이 지금 무엇을 사 주는가. 지금은 테스트가 "3600틱 뒤 승리"를 단언할 수 있다는 것뿐이다. 그러나 호스트와 클라이언트가 같은 규칙을 돌리는 구조에서는 이것이 전제 조건이 된다.

## 10. 실행과 관측

게임 규칙에는 두 개의 검사가 붙어 있다. 둘 다 창도 소켓도 요구하지 않고, 각자 `GameWorld` 를 직접 구성해 3600틱(60초 분량)까지 돌린다.

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release -R "test_game"
```

```text
    Start 23: test_game
1/2 Test #23: test_game ........................   Passed    0.03 sec
    Start 24: test_game_expansion
2/2 Test #24: test_game_expansion ..............   Passed    0.03 sec

100% tests passed out of 2

Total Test time (real) =   0.08 sec
```

```powershell
.\build\bin\Release\test_game.exe
.\build\bin\Release\test_game_expansion.exe
```

```text
[test] game
[test] game: 24 checks, 0 failures
[test] game_expansion
[test] game_expansion: 71 checks, 0 failures
```

두 파일이 무엇을 묻는지가 이 편의 요약이다.

`test_game` 은 **규칙의 양 끝**을 본다. 강한 타워 둘을 경로에 두면 이기고(`Won()`, `CoreHp() > 0`, 보상 누적으로 `Gold() > 100`), 타워가 없으면 진다(`Lost()`, `CoreHp() <= 0`). 골드 60으로 50짜리 타워를 사면 10이 남고 두 번째 구매가 실패한다. 웨이브 1에서 시작해 2에 도달했다가 결국 클리어한다. 그리고 `FromAscii` 의 불변식을 직접 친다 — 분기 맵 `{".P.", "SPC", "..."}` 과 미사용 조각 맵 `{"SPC", "##P"}` 이 둘 다 거부되고, 유효한 4×4 맵에서는 경로 칸 `(2,1)` 에 배치가 실패하고 건설 가능 칸 `(0,0)` 에 성공하며 같은 칸 두 번째 배치가 실패한다.

`test_game_expansion` 은 **표의 관계**를 본다. 캐논이 개틀링보다 데미지가 높고, 개틀링이 캐논보다 발사 간격이 짧고, 스나이퍼가 캐논보다 사거리가 길다. 탱크는 배수 3.0 이라 체력 20짜리 웨이브에서 60으로 나오고, 보스는 12.0 이라 50짜리 웨이브에서 600으로 나온다. 강화는 1→2 에 60, 2→3 에 120 이 들고 3에서 막힌다. 스플래시 타워 둘이 20마리 무리 웨이브를 정리한다.

제품 전체를 돌린 결과는 이렇다.

```powershell
.\build\bin\Release\mega_defense.exe --selftest
```

```text
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

이 한 줄에서 이 편에 해당하는 것.

- `ticks=1377` — 60Hz 기준 22.95초 만에 한 판이 끝났다. 매 틱이 `kDefenseFixedDt` 정확히 하나이므로 이 수는 **시뮬 시간 그 자체**다.
- `wave=6` — 웨이브 파일에 정의된 것은 5개다. `Wave()` 가 `waveIdx_ + 1` 이므로 6은 인덱스가 목록 끝을 넘었다는 뜻이고, 그것이 `WinLoseSystem` 의 승리 조건이다.
- `towers=11` — 16기 상한 안에서 11기가 섰다. 셀프테스트 스크립트가 계획한 자리는 12곳인데 11에서 멈춘 것은 마지막 자리를 살 골드가 모이기 전에 판이 끝났기 때문이다.
- `result=CLEAR` — 코어가 살아남았다.

같은 명령을 여러 번 돌려도 이 수들은 바뀌지 않는다. 그것이 9절의 성질이다.

기본 맵의 형태도 관측 가능하다. `games/defense/assets/level.txt` 는 16×15 격자에 경로 칸 32개(`S` 1 + `P` 30 + `C` 1)를 담고 있고, 이 32칸이 (7,0)에서 (8,14)까지 **끊김도 분기도 없는 한 줄**이다. `FromAscii` 가 그 사실을 검증하고, 검증에 실패하면 코드 안의 기본 맵으로 되돌아간다. 두 맵이 같은 모양인 것은 의도다 — 폴백이 *다른 게임*이 되면 폴백이 일어났다는 사실이 밸런스 변화로 위장된다.

---

## 결론

이 단계에서 세운 것.

1. **격자와 경로** — 자유 좌표를 버렸다. 재미가 "반드시 지나가는 길"에서 나오기 때문이다. 배치 규칙은 타일 검사 + 거리 검사 두 겹이고, 후자가 두 모드에서 같은 코드로 성립한다.
2. **`FromAscii` 의 단일 경로 불변식** — 끊김·분기·순환·미사용 조각 넷을 검사기 넷이 아니라 **한 번의 flood walk** 로 거부한다. `candidateCount != 1` 과 `pathCells_.size() != pathTileCount` 두 줄이 전부다.
3. **스펙 표와 배수 표** — 타워는 절대값, 적은 배수. 범위 밖 열거값이 와이어로 도착할 수 있으므로 두 조회 함수 모두 기본값으로 떨어진다.
4. **여섯 시스템의 고정 순서** — 스폰 → 진격 → 사격 → 발사체 → 정리 → 승패. 각 인접 쌍마다 뒤집었을 때 생기는 구체적 결함이 있다.
5. **타게팅 점수 함수** — 세 모드를 "작을수록 좋다" 하나로 통일. `First` 는 굽은 경로에서 직선 거리가 진행도를 배신하므로 경로 인덱스에 100만 배를 곱해 우선순위를 보장한다.
6. **누적 투자 회계** — `invested` 정수 하나가 구매·강화 이력을 접고 있어, 환불 계산에 티어도 정가표도 필요 없다.
7. **누적기 + 3중 방어** — 입력 클램프 0.25초, 보충 상한 5틱, 초과분 폐기. 정확한 시뮬 시간보다 응답하는 프로그램을 택했다.
8. **결정성** — 고정 시드 xorshift, 순서 보존 삭제, 순차 루프.

현재 상태의 한계. 이 규칙에 **입력이 닿지 않는다.** `PlaceTowerType` 을 부를 사람이 없고, 웨이브 목록은 호출자가 손으로 만들어 넘겨야 한다. 마우스 클릭 하나가 "어느 셀에 어느 종류를 세운다"로 변환되는 경로도, 그 변환이 지나야 하는 문지기도 아직 없다.
