## 들어가며

2편의 도달점은 "규칙이 스스로 돈다"였다. `GameWorld::Step` 을 3600번 부르면 한 판이 끝난다. 그러나 그 판에는 사람이 없다 — 타워를 세우는 것은 테스트 코드가 손으로 부른 `PlaceTowerType` 이고, 웨이브 목록은 호출자가 만들어 넘긴 벡터다.

이 편의 도달점은 **바깥에서 안으로 들어오는 두 갈래를 세우는 것**이다.

- **입력** — 키와 마우스가 게임 규칙에 닿는 경로. 물리 키 → 액션 → 명령 → 월드.
- **콘텐츠** — 디스크의 텍스트 파일이 게임 구성이 되는 경로. 레벨·웨이브·밸런스.

두 갈래에 공통된 문제가 하나 있다. **둘 다 우리가 쓰지 않은 데이터를 받는다.** 사용자가 클릭한 좌표도, 사용자가 편집한 파일도, 우리가 고른 값이 아니다. 그래서 이 편의 절반은 "받아들이는 법"이 아니라 **"거부하는 법"** 이다.

이 편이 다루지 않는 것: 명령이 바이트로 직렬화되어 네트워크를 건너는 형태(4편), 프레임 루프가 창과 헤드리스에서 같은 코드를 도는 이음매(5편).

---

## 목차
1. [키를 액션으로 바꾸는 한 겹](#1-키를-액션으로-바꾸는-한-겹)
2. [엣지는 프레임 시작에 비운다](#2-엣지는-프레임-시작에-비운다)
3. [클릭 하나가 두 갈래로 갈린다](#3-클릭-하나가-두-갈래로-갈린다)
4. [명령: 로컬 입력이라는 두 번째 입구](#4-명령-로컬-입력이라는-두-번째-입구)
5. [ApplyCommand 가 그 입구의 문지기다](#5-applycommand-가-그-입구의-문지기다)
6. [콘텐츠 셋, 신뢰 경계 셋](#6-콘텐츠-셋-신뢰-경계-셋)
7. [폴백은 왜 파일 전체인가](#7-폴백은-왜-파일-전체인가)
8. [스크립트를 돌리는 것은 임의 프로그램을 돌리는 것이다](#8-스크립트를-돌리는-것은-임의-프로그램을-돌리는-것이다)
9. [실행 성공과 값의 유효는 다른 조건이다](#9-실행-성공과-값의-유효는-다른-조건이다)
10. [실행과 관측](#10-실행과-관측)

---

## 1. 키를 액션으로 바꾸는 한 겹

가장 짧은 코드는 `if (window.Key('1')) selectedType_ = TowerType::Cannon;` 이다. 한 겹을 더 두는 이유가 있어야 한다.

`engine::InputMap` 은 물리 키를 문자열 액션에 묶고, 게임은 액션만 묻는다.

<!-- snippet: include/engine/input.hpp#OnKey -->
```cpp
// 물리 키 상태 갱신. 바인딩된 액션의 활성 여부가 따라 바뀐다.
void OnKey(int key, bool down) {
    auto it = bindings_.find(key);
    if (it == bindings_.end()) return;
    const bool wasDown = active_.count(it->second) != 0;
    if (down) {
        active_.insert(it->second);
        if (!wasDown) pressed_.insert(it->second);
    } else {
        active_.erase(it->second);
        if (wasDown) released_.insert(it->second);
    }
}
```

이 게임이 그 한 겹에서 실제로 얻는 것은 셋이다.

**엣지 계산이 한 곳에 있다.** `pressed_` 는 "이번 프레임에 새로 눌림"이다. 이 계산을 창이 하면 헤드리스 경로가 같은 계산을 다시 만들어야 하고, 두 계산이 어긋나는 순간 테스트와 제품이 다른 입력을 본다. `InputMap` 은 창을 모르므로 양쪽이 같은 코드를 지난다.

**바인딩이 데이터다.** 키 목록은 배열 하나에 있고, 그 배열이 창 폴링·헤드리스 스크립트·바인딩 등록 세 곳에서 같은 순서로 쓰인다.

```cpp
inline constexpr std::array<int, 9> kDefenseActionKeys{
    '1', '2', '3', '4', 'P', 'U', 'X', 'T', 'R'};
```

<!-- snippet: include/game/defense_app.hpp#ActionForKey -->
```cpp
static const char* ActionForKey(int key) {
    switch (key) {
        case '1': return "tower_cannon";
        case '2': return "tower_gatling";
        case '3': return "tower_splash";
        case '4': return "tower_sniper";
        case 'P': return "pause";
        case 'U': return "upgrade";
        case 'X': return "sell";
        case 'T': return "target_mode";
        case 'L': return "reload";
        // ...
        default:  return "restart";
    }
}
```

**액션 이름이 곧 문서다.** `input_.WasPressed("target_mode")` 는 `window.KeyPressed('T')` 보다 읽는 사람에게 더 많은 것을 말한다.

값이 아닌 것도 적어 둔다. `InputMap` 은 `std::unordered_set<std::string>` 셋을 쓴다 — 액션 하나 조회에 해시와 문자열 비교가 든다. 프레임당 아홉 번이라 비용은 무시할 수 있지만, **이 자료구조를 시뮬 갱신 경로에 넣지는 않는다.** 순회 순서가 불안정하기 때문이다. 여기서는 순회하지 않고 조회만 하므로 결정성에 영향이 없다.

`ApplyDeadzone` 은 이 게임이 부르지 않는다. 아날로그 축이 없기 때문이다. 죽은 코드로 보이지만 남긴 이유는 이 클래스가 게임 전용이 아니라 엔진 계층의 것이고, 데드존은 아날로그 입력의 정의에 속하는 부분이기 때문이다.

## 2. 엣지는 프레임 시작에 비운다

<!-- snippet: include/engine/input.hpp#BeginFrame -->
```cpp
// 프레임 시작에서 edge 상태만 비운다. held 상태는 유지한다.
void BeginFrame() {
    pressed_.clear();
    released_.clear();
}
```

세 줄짜리 함수지만 계약 하나를 정의한다. **`pressed_`/`released_` 는 한 프레임짜리 사실이고, `active_` 는 지속되는 사실이다.**

이 계약이 없으면 두 가지 중 하나가 일어난다. 엣지를 지우지 않으면 `WasPressed("upgrade")` 가 키를 누르고 있는 내내 참이라 타워가 프레임마다 강화된다. 반대로 `active_` 까지 지우면 "누르고 있는 동안" 을 물을 수 없다.

프레임 루프에서의 사용은 이렇다.

```cpp
input_.BeginFrame();
for (size_t i = 0; i < kDefenseActionKeys.size(); ++i)
    input_.OnKey(kDefenseActionKeys[i], frame.keyDown[i]);
```

`FrameInput::keyDown` 은 **엣지가 아니라 현재 눌림 상태**다. 엣지는 `OnKey` 안에서 이전 상태와 비교해 만든다. 이 방향을 고른 이유는 입력 소스가 엣지를 만들 필요가 없어지기 때문이다 — 헤드리스 스크립트는 "이 프레임에 U 를 누르고 있다"만 적으면 되고, 그것이 자동으로 한 번의 엣지가 된다.

마우스는 이 경로를 지나지 않는다. `FrameInput::mouseClicked` 가 이미 엣지다. 마우스 버튼은 액션에 바인딩되지 않으므로 `InputMap` 이 관여할 것이 없고, 좌표는 액션 개념과 무관하다.

## 3. 클릭 하나가 두 갈래로 갈린다

마우스 왼쪽 버튼 하나에 두 가지 의미가 실려 있다. 타워가 있는 자리를 누르면 **선택**, 빈 자리를 누르면 **배치**다.

<!-- snippet: include/game/defense_app.hpp@click-routing -->
```cpp
if (frame.mouseClicked && onGround) {
    if (const auto* tower = TowerAt(view_, mouse, map_.CellSize() * 0.45f)) {
        selectedTower_ = tower->id;
    } else {
        selectedTower_ = 0;
        Submit(wire::CommandType::Place, mouse, static_cast<uint8_t>(selectedType_));
    }
}

if (const auto* selected = wire::FindById(view_.towers, selectedTower_)) {
    if (input_.WasPressed("upgrade"))
        Submit(wire::CommandType::Upgrade, selected->pos, 0);
    if (input_.WasPressed("sell")) {
        Submit(wire::CommandType::Sell, selected->pos, 0);
        selectedTower_ = 0;
    }
    if (input_.WasPressed("target_mode")) {
        uint8_t next = (static_cast<uint8_t>(selected->targetMode) + 1) % 3;
        Submit(wire::CommandType::Target, selected->pos, next);
    }
} else {
    selectedTower_ = 0;
}
```

네 가지 결정이 이 블록에 있다.

**`frame.mouseY >= map_.OriginY()`** — HUD 영역(위 30픽셀)의 클릭은 게임 입력이 아니다. 이 한 줄이 없으면 HUD 위를 누를 때마다 y 좌표가 음수 셀로 변환되어 배치 명령이 나가고, 그 명령은 월드에서 거부되지만 **네트워크로는 나간다**.

**판정 반경 `CellSize() * 0.45f`** — 셀 크기의 45%, 즉 13.5픽셀이다. 셀 반폭(15픽셀)보다 약간 작다. 정확히 셀 경계로 잡으면 인접 셀의 모서리를 눌렀을 때 어느 타워가 잡힐지가 부동소수 오차로 갈린다. 안쪽으로 줄이면 그 회색 지대가 "아무것도 선택되지 않음"이 되어 결과가 예측 가능해진다.

**선택 대상은 `view_` 이지 `world_` 가 아니다.** `view_` 는 이번 프레임의 스냅샷이다. 클라이언트로 접속했을 때는 권위 월드가 아예 없으므로 `world_` 를 보면 항상 비어 있고, 아무것도 선택되지 않는다. 스냅샷을 보면 솔로·호스트·클라이언트 세 모드가 **같은 코드로** 선택을 처리한다.

**선택은 id 로 유지한다.** `selectedTower_` 는 `uint32_t` 다. 포인터나 인덱스로 들고 있으면 다음 스냅샷에서 무효가 된다 — 클라이언트에게 스냅샷은 매번 새로 디코드된 벡터다. 그리고 매 프레임 `FindById` 로 다시 찾다가 못 찾으면 선택을 해제한다(`else selectedTower_ = 0;`). 그래서 **다른 플레이어가 판 타워를 내가 계속 선택하고 있는 상태**가 존재할 수 없다.

`Target` 명령의 `(mode + 1) % 3` 은 순환이다. 세 모드 중 하나를 직접 고르는 UI 대신 토글 하나로 처리한 것은, 그 선택이 명령의 `argument` 한 바이트에 그대로 실려 나가야 하기 때문이다 — 값이 항상 0..2 로 유지되므로 디코더의 범위 검사가 절대 걸리지 않는다.

## 4. 명령: 로컬 입력이라는 두 번째 입구

여기가 이 편에서 가장 중요한 자리다.

플레이어의 의도는 `wire::Command` 라는 구조체 하나로 표현된다.

```cpp
struct Command {
    uint32_t sequence = 0;
    CommandType type = CommandType::Place;
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t argument = 0;
};
```

이 구조체가 월드에 도달하는 경로가 **둘**이다.

```
[클라이언트]  입력 → Submit → 큐 → WriteCommand → UDP → ReadCommand → ApplyCommand → World
[솔로·호스트] 입력 → Submit ────────────────────────────────────────→ ApplyCommand → World
```

위쪽 경로에는 디코더가 있다. `ReadCommand` 가 바이트를 파싱하면서 타입 범위·인자 범위·잉여 바이트를 전부 검사한다. **아래쪽 경로에는 디코더가 없다.** 로컬 입력은 `Submit` 에서 구조체를 손으로 만들어 곧장 `ApplyCommand` 로 간다.

<!-- snippet: include/game/defense_app.hpp#Submit -->
```cpp
void Submit(wire::CommandType type, const math::Vec2& position, uint8_t argument) {
    wire::Command command;
    command.type = type;
    command.x = wire::EncodePosition(position.x);
    command.y = wire::EncodePosition(position.y);
    command.argument = argument;
    if (options_.mode == RunMode::Client) {
        localCommands_.Queue(command);        // 시퀀스는 채널이 부여한다
        ++commandsQueued_;
    } else if (wire::ApplyCommand(world_, command)) {
        sound_.Play(platform::Sfx::Pickup);
    }
}
```

좌표가 **로컬 경로에서도 양자화를 지난다**는 점이 설계다. `EncodePosition` 은 `float` 을 1/8픽셀 단위 `uint16_t` 로 바꾸고, `ApplyCommand` 가 다시 `float` 으로 되돌린다. 로컬 플레이라면 이 왕복을 생략하고 원래 `float` 을 쓸 수 있다. 그러지 않은 이유는 하나다 — **그러면 솔로와 협동이 다른 게임이 된다.** 셀 경계에 정확히 걸친 클릭이 솔로에서는 A 셀, 협동에서는 B 셀로 갈 수 있다. 같은 코드를 지나게 하면 그 차이가 존재할 수 없다.

`sequence` 를 `Submit` 이 채우지 않는 것도 의도다. 시퀀스는 신뢰 채널이 큐잉할 때 부여한다. 여기서 손으로 매기면 재전송·중복 제거의 근거가 두 곳에서 관리된다.

## 5. ApplyCommand 가 그 입구의 문지기다

디코더를 지나지 않는 입구가 있다면, 그 입구에도 검사가 있어야 한다.

<!-- snippet: include/game/defense_protocol.hpp#ApplyCommand -->
```cpp
inline bool ApplyCommand(GameWorld& world, const Command& command) {
    math::Vec2 position{DecodePosition(command.x), DecodePosition(command.y)};
    switch (command.type) {
        case CommandType::Place:
            if (command.argument >= static_cast<uint8_t>(TowerType::Count)) return false;
            return world.PlaceTowerType(position, static_cast<TowerType>(command.argument));
        case CommandType::Upgrade: {
            int index = world.FindTowerAt(position);
            return index >= 0 && world.UpgradeTower(index);
        }
        case CommandType::Sell: {
            int index = world.FindTowerAt(position);
            return index >= 0 && world.SellTower(index) > 0;
        }
        case CommandType::Target: {
            int index = world.FindTowerAt(position);
            if (index < 0 || command.argument > static_cast<uint8_t>(TargetMode::Strongest)) return false;
            return world.SetTowerTarget(index, static_cast<TargetMode>(command.argument));
        }
        // ...
    }
    return false;
}
```

이 함수의 인자 검사는 디코더의 검사와 **중복이 아니다.** 디코더는 위쪽 경로에만 있고, 이 함수는 두 경로 모두의 마지막 관문이다. 검사를 디코더에만 두면 로컬 입력이 무검사로 들어오고, 이 함수에만 두면 디코더가 통과시킨 쓰레기 시퀀스가 신뢰 채널의 자리를 태운다(그 두 번째 문제는 4편에서 다룬다).

검사의 종류가 두 함수에서 다르다는 점도 설계다.

| 질문 | 답할 수 있는 곳 |
|---|---|
| `argument` 가 `TowerType` 집합 안인가 | 와이어 포맷이 안다 |
| `Upgrade` 가 `argument` 를 안 읽으니 0이어야 하는가 | 와이어 포맷이 안다 |
| 그 칸이 건설 가능한가 | **월드만 안다** |
| 골드가 있는가 | **월드만 안다** |
| 거기 타워가 있는가 | **월드만 안다** |

앞의 둘은 디코더의 몫이고, 뒤의 셋은 여기 남는다. 이 함수가 `argument` 를 다시 보는 것은 로컬 입구 때문이지, 월드 질문이라서가 아니다.

`FindTowerAt` 이 좌표로 타워를 찾는 것도 결정이다. 명령에 타워 id 를 실을 수도 있었다. 그러지 않은 이유는 **클라이언트가 아는 id 가 호스트의 id 와 같다는 보장이 스냅샷 도달에 의존하기 때문**이다. 좌표는 스냅샷 없이도 클라이언트가 스스로 안다(자기가 클릭한 자리다). 대신 좌표 기반 조회는 반경 판정이 필요하고, 그 반경이 `map_.CellSize() * 0.45f` 로 UI 쪽 판정과 같은 값이라 클릭한 것과 명령이 닿는 것이 일치한다.

반환값 `bool` 은 호스트에서 효과음을 낼지 정하는 데 쓰인다. 거부된 명령은 소리를 내지 않는다 — 골드가 부족해 타워가 안 서는데 배치 효과음이 나면 플레이어는 무엇이 잘못됐는지 알 수 없다.

## 6. 콘텐츠 셋, 신뢰 경계 셋

게임 구성은 실행 파일 옆 `assets/` 의 세 파일에서 온다.

| 파일 | 형식 | 파서 | 실패 시 |
|---|---|---|---|
| `level.txt` | 문자 격자 | `DefenseMap::FromAscii` | 코드 안 기본 맵 |
| `waves.txt` | 공백 구분 숫자 + 종류 이름 | 수동 파싱 + 범위 검사 | 코드 안 기본 웨이브 5개 |
| `balance.script` | 미니 DSL | `tools::Script` 인터프리터 | 기본 `BalanceConfig` |

읽기는 전부 `io::ReadTextFile` 을 지난다. 여기서 `<fstream>` 을 직접 여는 것이 금지된 이유는 7편의 계층 규칙에 있다. 지금 중요한 것은 **줄 끝 처리**다.

<!-- snippet: include/game/defense_content.hpp#SplitLines -->
```cpp
inline std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        if (current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }
    return lines;
}
```

파일은 바이트 그대로 읽고, CRLF 는 **여기서 한 번만** 벗긴다. 텍스트 모드 스트림의 암묵적 변환에 기대면 Windows 에서 만든 맵 파일이 Linux 에서 각 행 끝에 `\r` 을 달고 나타나고, `FromAscii` 는 그것을 "정의되지 않은 문자"로 거부한다 — 같은 파일이 플랫폼에 따라 유효했다 무효였다 한다.

맵 로더는 이렇게 생겼다.

<!-- snippet: include/game/defense_content.hpp#LoadMapFile -->
```cpp
inline game::DefenseMap LoadMapFile(const std::string& path,
                                    bool* loadedExternal = nullptr) {
    std::string text;
    std::vector<std::string> rows;
    if (io::ReadTextFile(path, text, 256 * 1024)) {
        for (std::string& row : SplitLines(text))
            if (!row.empty()) rows.push_back(std::move(row));
    }

    game::DefenseMap loaded;
    const bool valid = game::DefenseMap::FromAscii(rows, 30, 30, loaded);
    if (loadedExternal) *loadedExternal = valid;
    return valid ? loaded : game::DefenseMap::CreateDefault();
}
```

파일이 없을 때도 `rows` 가 빈 채로 `FromAscii` 에 들어간다. `FromAscii` 의 첫 줄이 빈 입력을 거부하므로 "파일 없음"과 "파일이 잘못됨"이 **같은 한 갈래**로 흡수된다. 분기가 하나 줄고, 그 결과 `loadedExternal` 의 의미가 정확히 "외부 파일이 유효했다"가 된다.

`30, 30` 은 셀 크기와 HUD 높이다. 1편에서 화면 크기를 결정한 그 숫자들이고, 여기서 맵에 전달된다.

## 7. 폴백은 왜 파일 전체인가

1편의 스프라이트는 **개별 폴백**이었다 — 10개 중 3개가 깨지면 나머지 7개는 파일에서 온다. 콘텐츠는 정반대다.

<!-- snippet: include/game/defense_content.hpp#LoadWaveFile -->
```cpp
inline std::vector<game::WaveDef> LoadWaveFile(
        const std::string& path,
        const std::vector<game::WaveDef>& fallback,
        bool* loadedExternal = nullptr) {
    std::string text;
    std::vector<game::WaveDef> waves;
    bool valid = io::ReadTextFile(path, text, 256 * 1024);

    if (valid) {
        for (std::string line : SplitLines(text)) {
            const size_t comment = line.find('#');
            if (comment != std::string::npos) line.erase(comment);
            if (line.find_first_not_of(" \t") == std::string::npos) continue;

            game::WaveDef wave;
            std::string typeName;
            std::string trailing;
            std::istringstream parser(line);
            if (!(parser >> wave.count >> wave.hp >> wave.speed >> wave.reward
                         >> wave.interval >> typeName) ||
                (parser >> trailing) ||
                !ParseEnemyType(typeName, wave.type) ||
                wave.count < 1 || wave.count > 64 ||
                // ... hp · speed · reward · interval 도 각각 유한성과 범위를 본다
                wave.interval > 10.0f) {
                valid = false;
                break;
            }
            waves.push_back(wave);
            if (waves.size() > 100) { valid = false; break; }
        }
    }

    valid = valid && !waves.empty();
    if (loadedExternal) *loadedExternal = valid;
    return valid ? waves : fallback;
}
```

`valid = false; break;` 가 요점이다. **한 줄이라도 틀리면 이미 파싱한 앞줄들까지 전부 버린다.**

차이의 근거는 데이터의 성질이다.

- **스프라이트는 독립적이고 장식적이다.** `enemy_tank.ppm` 이 깨져도 탱크가 내장 그림으로 나올 뿐, 게임 규칙은 그대로다. 그리고 에셋을 하나씩 그려 넣는 도중에는 항상 "일부만 있는" 상태를 지나므로 개별 폴백이 작업 흐름에 맞는다.
- **웨이브는 상호 의존적이고 규칙 그 자체다.** 5개 웨이브가 난이도 곡선을 이룬다. 3번 줄이 틀렸을 때 앞의 두 줄만 살리면 **2웨이브짜리 게임**이 되고, 그것은 사용자가 의도한 어떤 것도 아니다. 게다가 그 상태가 "쉬워졌다"로 보이지 "파일이 틀렸다"로 보이지 않는다.

같은 이유로 `waves.empty()` 도 실패다. 주석과 빈 줄만 있는 파일은 문법적으로는 오류가 없지만 웨이브가 0개인 게임은 시작하자마자 이기는 게임이다.

`(parser >> trailing)` 이 참이면 실패라는 것도 같은 정신이다. 여섯 필드 뒤에 무엇인가 더 있다면 그 줄은 우리가 아는 형식이 아니다. 잉여를 무시하면 오타 한 글자가 조용히 지나간다.

`> 100` 상한과 필드별 범위 검사(`count` 1..64, `hp` 1..1000, `speed` 1..300, `interval` 0.03..10)는 신뢰 경계 밖 입력에 대한 방어다. `!std::isfinite` 검사가 함께 있는 이유는 `inf` 와 `nan` 이 텍스트로 파싱 가능하기 때문이다 — `hp` 가 `nan` 이면 모든 비교가 거짓이 되어 적이 죽지 않는다.

폴백 웨이브 자체도 한 곳에만 있다.

<!-- snippet: include/game/defense_content.hpp#DefaultWaves -->
```cpp
inline std::vector<game::WaveDef> DefaultWaves() {
    return {
        {10, 12, 30, 5, 0.35f, game::EnemyType::Normal},
        {16, 14, 34, 5, 0.22f, game::EnemyType::Fast},
        { 6, 40, 30, 8, 0.45f, game::EnemyType::Tank},
        {26, 10, 36, 4, 0.14f, game::EnemyType::Swarm},
        { 1, 60, 30, 50, 1.0f, game::EnemyType::Boss},
    };
}
```

이 표는 클라이언트 진입점과 전용 서버가 각자 사본을 갖고 있던 것을 합친 것이다. 두 사본이 갈라지면 **두 프로세스가 다른 게임을 돌린다** — 폴백 상황에서만, 그리고 조용히.

## 8. 스크립트를 돌리는 것은 임의 프로그램을 돌리는 것이다

`balance.script` 는 나머지 둘과 급이 다르다. 데이터가 아니라 **프로그램**이기 때문이다.

```text
start_gold = 420
core_hp = 260
enemy_hp_multiplier = 1.0
enemy_speed_multiplier = 1.0
reward_multiplier = 1.0
```

지금 파일은 대입 다섯 줄이지만, `tools::Script` 는 변수·산술·비교·`if/else`·`while` 을 지원한다. 그리고 이 파일은 "재빌드 없이 교체할 수 있다"고 광고된 파일이다. 즉 **끝나지 않는 프로그램도 정상적인 입력**이다.

예전 방어는 `While` 노드마다 지역 카운터를 두는 것이었다.

```cpp
case Node::While: { int guard = 0; while (...) { ...; if (++guard > 1000000) break; } }
```

이 방어에는 구멍이 셋 있었다.

1. **지역 카운터는 곱해진다.** `While` 노드에 진입할 때마다 0으로 되돌아가므로 두 겹이면 상한이 10^12 회다.
2. **파서가 전진하지 않을 수 있었다.** 모르는 토큰을 소비하지 않고 돌아오면 최상위 문 루프가 같은 토큰을 영원히 다시 본다 — `}` 한 글자짜리 파일이 노드를 무한 할당했다.
3. **재귀 하강에 깊이 제한이 없었다.** 괄호를 충분히 겹치면 스택이 넘친다.

세 구멍은 전부 같은 결과를 낸다. 로더의 `if (!script.Run(source)) return fallback;` 은 **`Run` 이 돌아온다는 전제** 위에 있고, 돌아오지 않으면 "전체 폴백"이라는 약속에 도달하지조차 못한다. 콘텐츠 로드 지점에서 두 실행 파일이 그대로 선다.

그래서 방어를 `While` 노드가 아니라 **인터프리터 실행 전체**에 하나씩 걸었다.

<!-- snippet: include/tools/script.hpp#Eval -->
```cpp
double Eval(const Node* n) {
    // 예산 초과·실행 오류 뒤에는 한 노드도 더 방문하지 않는다.
    if (!ok_) return 0;
    if (++steps_ > budget_) { FailBudget(n); return 0; }
    switch (n->kind) {
        case Node::Num: return n->num;
        // ...
        case Node::While: { while (ok_ && Eval(n->kids[0].get()) != 0) Eval(n->kids[1].get()); return 0; }
        // ...
    }
}
```

`steps_` 는 `Run` 마다 0에서 시작하는 **전역 카운터**다. 중첩과 무관하게 합산되므로 곱해지지 않는다. 상한은 `kDefaultStepBudget = 1000000` 이고, 밸런스 DSL 의 실제 사용량은 수십 스텝이라 이 값은 순전히 여유분이다.

두 줄의 순서가 중요하다. `if (!ok_) return 0;` 이 먼저이므로, 예산이 터진 뒤에는 `While` 의 조건식도 0으로 읽혀 루프가 즉시 끝난다. 즉 **예산 초과가 결과가 아니라 오류**이고, 그 오류가 모든 상위 프레임을 되감는다.

<!-- snippet: include/tools/script.hpp#Depth -->
```cpp
class Depth {
public:
    explicit Depth(Script* s) : script_(s) {
        if (++script_->depth_ > kMaxNestingDepth)
            script_->Fail(script_->CurrentLine(),
                          "중첩 깊이가 상한 " + std::to_string(kMaxNestingDepth) + " 을 넘었다");
    }
    ~Depth() { --script_->depth_; }
    Depth(const Depth&) = delete;
    Depth& operator=(const Depth&) = delete;
private:
    Script* script_;
};
```

RAII 가드가 `ParseStatement` 와 `ParseUnary` 두 곳에 있다. 표현식의 모든 재귀 경로(단항 `-` 사슬, 괄호 중첩)가 `ParseUnary` 를 지나고, 문의 모든 중첩이 `ParseStatement` 를 지나기 때문이다. 상한 64는 파서 재귀 깊이인 동시에 `Eval` 재귀 깊이다 — AST 깊이가 파서 깊이와 같으므로, 파싱을 통과한 트리는 실행 스택도 넘기지 않는다.

세 번째 구멍은 파서가 반드시 토큰을 먹게 해서 막았다.

<!-- snippet: include/tools/script.hpp#ParsePrimary -->
```cpp
std::unique_ptr<Node> ParsePrimary() {
    if (Peek().type == T::Num) { auto n = Make(Node::Num); n->num = Next().num; return n; }
    if (Peek().type == T::Ident) { auto n = Make(Node::Var); n->name = Next().text; return n; }
    if (Peek().type == T::LParen) {
        // ...
    }
    const int line = CurrentLine();
    const std::string what = Peek().type == T::End
        ? std::string("표현식이 끝나기 전에 소스가 끝났다")
        : std::string("표현식 자리에 올 수 없는 토큰 '") + Peek().text + "'";
    if (Peek().type != T::End) ++pos_;
    Fail(line, what);
    return Make(Node::Num);
}
```

`if (Peek().type != T::End) ++pos_;` 한 줄이 전진 보장이다. `End` 토큰만 소비하지 않는데, 그것은 `End` 를 넘어가면 토큰 벡터 밖을 읽기 때문이고, `Fail` 이 `ok_` 를 내리므로 상위 루프가 `while (ok_ && ...)` 에서 어차피 멈춘다.

셋 다 조용한 실패가 아니라 진단 문자열을 남긴다. 형식은 `<이름>:<줄>: <무엇이>` 이고, 이름은 `SetName()` 으로 준 파일 경로다. 폴백은 정상 동작이지만 **"왜 폴백인가"까지 조용하면 파일을 교체한 사람은 자기 값이 적용된 줄 안다.**

## 9. 실행 성공과 값의 유효는 다른 조건이다

`Run()` 이 `true` 를 돌려줬다고 해서 결과를 써도 되는 것은 아니다.

<!-- snippet: include/game/defense_content.hpp#LoadBalanceScript -->
```cpp
inline BalanceConfig LoadBalanceScript(const std::string& path,
                                       bool* loadedExternal = nullptr,
                                       std::string* diagnostic = nullptr) {
    BalanceConfig fallback;
    if (diagnostic) diagnostic->clear();
    std::string source;
    if (!io::ReadTextFile(path, source, 64 * 1024)) {
        if (loadedExternal) *loadedExternal = false;
        return fallback;
    }

    tools::Script script;
    script.SetName(path);            // 진단 문자열이 어느 파일인지 말하게 한다
    if (!script.Run(source)) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic) *diagnostic = script.Error();
        return fallback;
    }

    const double startGold = script.GetOr("start_gold", fallback.startGold);
    const double coreHp = script.GetOr("core_hp", fallback.coreHp);
    const double hp = script.GetOr("enemy_hp_multiplier", 1.0);
    const double speed = script.GetOr("enemy_speed_multiplier", 1.0);
    const double reward = script.GetOr("reward_multiplier", 1.0);
    const bool valid =
        InRange(startGold, 0.0, 5000.0) &&
        InRange(coreHp, 1.0, 10000.0) &&
        InRange(hp, 0.5, 1.25) &&
        InRange(speed, 0.5, 1.25) &&
        InRange(reward, 0.5, 2.0);
    if (!valid) {
        if (loadedExternal) *loadedExternal = false;
        if (diagnostic)
            *diagnostic = path + ": 계산된 밸런스 값이 허용 범위 밖이다";
        return fallback;
    }
    // ... 검증을 통과한 값만 BalanceConfig 로 옮긴다
}
```

세 갈래의 실패가 하나의 폴백으로 흡수된다.

1. **파일이 없다** — 진단 없음. 기본 구성으로 도는 정상 상태이지 거부가 아니다.
2. **실행에 실패했다** — 구문 오류·예산 초과·깊이 초과. 진단은 인터프리터가 만든 `<파일>:<줄>: <이유>`.
3. **값이 범위 밖이다** — 실행은 성공했다. 진단은 로더가 만든다.

세 번째가 이 절의 제목이다. `start_gold = 99999` 는 문법적으로 완벽하고 실행도 성공하지만, 시작 골드 10만은 게임이 아니다. `InRange` 는 `std::isfinite` 를 함께 보므로 `0 / 0` 같은 식이 만든 `nan` 도 여기서 걸린다.

배수의 범위가 좁은 것(0.5~1.25)은 의도다. 밸런스 스크립트는 **조정 장치이지 재설계 장치가 아니다.** 적 체력을 10배로 만들 수 있다면 그것은 다른 게임이고, 그 다른 게임에서 나온 버그 보고는 원본 게임에 대한 정보가 아니다.

`GetOr` 의 기본값이 `fallback` 의 필드인 것도 작은 결정이다. 스크립트가 `core_hp` 를 정의하지 않았다면 그 항목만 기본값을 쓰되, 나머지 검사는 그대로 돈다. 파일이 다섯 값을 전부 정의할 의무는 없다.

## 10. 실행과 관측

콘텐츠 계층과 인터프리터에 각각 검사가 붙어 있다.

```powershell
cmake --build build --config Release -j
ctest --test-dir build -C Release -R "defense_content|tools_script"
```

```text
    Start 12: test_defense_content
1/2 Test #12: test_defense_content .............   Passed    0.05 sec
    Start 53: test_tools_script
2/2 Test #53: test_tools_script ................   Passed    0.06 sec

100% tests passed out of 2

Total Test time (real) =   0.12 sec
```

```powershell
.\build\bin\Release\test_defense_content.exe
.\build\bin\Release\test_tools_script.exe
```

```text
[test] defense_content
[test] defense_content: 18 checks, 0 failures
[test] tools_script
[test] tools_script: 64 checks, 0 failures
```

두 테스트에는 **30초 타임아웃**이 걸려 있다. 다른 테스트와 달리 이 둘은 "끝나지 않는 스크립트"를 실제로 인터프리터에 먹이기 때문이다. 8절의 방어가 무너지면 이 테스트는 실패하는 게 아니라 **끝나지 않는다** — 그 상태를 실패로 바꿔 주는 것은 타임아웃뿐이다. 방어가 살아 있으면 둘 다 0.1초 안에 끝난다.

폴백 경로는 제품에 실제 파일을 먹여 관측할 수 있다. 실행 파일 옆 `assets/balance.script` 를 끝나지 않는 루프로 바꿔 보면 이렇게 된다.

```text
start_gold = 420
core_hp = 260
x = 1
while x > 0 { x = x + 1 }
```

```powershell
.\build\bin\Release\mega_defense.exe --selftest 200
```

```text
[content] balance.script 거부, 기본 구성으로 진행: C:\...\assets/balance.script:4: 실행 스텝 예산 1000000 을 넘겨 중단했다(끝나지 않는 반복으로 보인다)
[assets] loaded 10/10 external sprites from C:\...\build\bin\Release/assets
[content] map=external waves=external (5) balance=fallback
[selftest] result=TIMEOUT ticks=200 towers=6 wave=1 packet=141 protocol=OK render=OK assets=10/10 content=fallback
```

읽을 것이 넷이다. 프로세스가 **멈추지 않았다.** 예산이 터진 줄 번호(`:4`)가 나온다. `balance=fallback` 이 그 사실을 상태로 남긴다. 그리고 게임은 기본 구성으로 계속 돌았다.

실행은 성공하지만 값이 범위 밖인 경우는 다른 진단이 나온다.

```text
start_gold = 99999
core_hp = 260
```

```text
[content] balance.script 거부, 기본 구성으로 진행: C:\...\assets/balance.script: 계산된 밸런스 값이 허용 범위 밖이다
[content] map=external waves=external (5) balance=fallback
```

줄 번호가 없다. 인터프리터는 아무 오류도 만나지 않았고, 거부한 것은 로더이기 때문이다. 9절의 구분이 진단 문자열의 형태로 드러난다.

웨이브 파일의 전체 폴백도 같은 방식으로 볼 수 있다. 세 줄짜리 파일의 마지막 줄에만 존재하지 않는 종류(`dragon`)를 적으면,

```text
[content] map=external waves=fallback (5) balance=fallback
```

`waves=fallback (5)` 다. 앞의 두 줄이 완벽히 유효했음에도 웨이브 수는 파일의 3이 아니라 내장 표의 **5**다. 7절의 "한 줄이라도 틀리면 전부 버린다"가 이 숫자다.

맵도 같다. `level.txt` 를 `FromAscii` 가 거부하는 내용(주 경로와 이어지지 않은 조각이 있는 맵)으로 바꾸면 `map=fallback` 이 된다.

파일을 원래대로 되돌리면 다시 이렇게 된다.

```text
[assets] loaded 10/10 external sprites from C:\...\build\bin\Release/assets
[content] map=external waves=external (5) balance=script
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

셀프테스트의 통과 조건에 `content=external` 이 들어 있다는 점이 중요하다. **폴백으로 돌면 이 검사는 실패한다.** 폴백은 사용자에게는 정상 동작이지만, 저장소 자신의 검사에서는 "에셋이 제대로 배포되지 않았다"는 신호여야 하기 때문이다.

입력 경로가 실제로 명령을 만드는지는 셀프테스트가 `towers=11` 로 답한다. 그 11기는 전부 `FrameInput` → `InputMap` → 클릭 분기 → `Submit` → `ApplyCommand` 를 지나 섰다 — `PlaceTowerType` 을 직접 부른 곳은 한 군데도 없다.

---

## 결론

이 단계에서 세운 것.

1. **액션 한 겹** — 물리 키를 문자열 액션에 묶어 엣지 계산을 한 곳에 모았다. 키 배열 하나가 창 폴링·헤드리스 스크립트·바인딩 등록 세 곳에서 같은 순서로 쓰인다.
2. **엣지 계약** — `pressed_`/`released_` 는 한 프레임, `active_` 는 지속. 입력 소스는 현재 눌림 상태만 주면 되고 엣지는 `OnKey` 가 만든다.
3. **클릭 분기** — 타워가 있으면 선택, 없으면 배치. 판정은 스냅샷 기준, 선택은 id 로 유지, 매 프레임 재확인.
4. **두 번째 입구** — 로컬 입력은 디코더를 지나지 않는다. 그래서 좌표를 굳이 1/8픽셀로 왕복시켜 솔로와 협동이 같은 양자화를 지나게 했다.
5. **`ApplyCommand` 의 문지기 역할** — 와이어 포맷이 답할 수 있는 질문과 월드만 답할 수 있는 질문을 나누고, 전자를 두 입구 모두에서 다시 본다.
6. **콘텐츠 셋의 원자적 폴백** — 스프라이트와 반대로 파일 전체를 버린다. 웨이브는 서로 의존하는 규칙이고, 부분 적용은 "쉬워졌다"로 위장되기 때문이다.
7. **인터프리터의 세 방어** — 전역 스텝 예산 10^6, 중첩 깊이 64, 파서 전진 보장. 셋 다 진단 문자열을 남긴다.
8. **실행 성공 ≠ 값 유효** — 문법이 맞고 실행이 끝나도 도메인 범위 밖이면 거부한다. 두 거부의 진단 형태가 다르므로 어느 쪽인지 밖에서 읽힌다.

현재 상태의 한계. 명령은 구조체로 만들어졌지만 **바이트가 된 적이 없다.** `Submit` 의 클라이언트 갈래는 큐에 넣기만 하고, 그 큐를 비우는 코드도 그 바이트를 받는 코드도 아직 없다. `EncodePosition` 이 1/8픽셀이라는 사실만 등장했지 왜 그 정밀도인지, 스냅샷은 몇 바이트인지, 그 상한을 무엇이 지키는지가 비어 있다. 그 포맷이 다음 편의 주제다.
