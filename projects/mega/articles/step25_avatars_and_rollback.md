# 25. 플레이어가 맵 위에 선다

## 들어가며

지금까지 플레이어는 맵 위의 커서였다. 클릭하면 타워가 서고, 키를 누르면 강화된다. 플레이어의 몸은 어디에도 없다.

그래서 앞선 편들이 만든 것 중 일부가 플레이어에게 도달하지 않는다. 절차 생성 맵의 분기와 막다른 길은 적에게만 의미가 있고, 시야 규칙은 타워에게만 의미가 있다. 미로 안을 걸어 다니는 주체가 없으니 미로가 플레이어의 문제가 아니다.

이 편은 그 주체를 만든다. 맵 위를 도는 **엔지니어 아바타**가 생기고, 건설은 아바타가 그 칸 근처에 있을 때만 가능해진다. 그 순간 지형이 플레이어의 제약이 된다 — 어느 길로 가서 어디에 짓느냐가 선택이 된다.

기술적으로 그것은 세 가지를 부른다.

| 헤더 | 하는 일 |
|---|---|
| `mp/input.hpp` | 프레임 단위 입력 레코드와 그 와이어 형식 |
| `mp/det_sim.hpp` | 아바타만의 정수 결정적 서브시뮬 |
| `mp/rollback.hpp` | 예측 · 되감기 · 재시뮬, 그리고 그 두 팔 |

도달점의 수치.

- 입력 레코드는 **3바이트**. `moveX=+1, moveY=-1, build` 의 바이트는 `01 FF 02` 다.
- 유효 조합 **36가지**(3 × 3 × 4) 전수 왕복. 범위 밖 축·정의되지 않은 버튼 비트는 전부 거부.
- 35% 손실 링크에서 패킷 **230개**를 잃고도 확정 구간 592프레임 중 못 받은 프레임 **0**.
- `avatar_predict=1`: 300프레임에 되감기 **283회**, 최대 깊이 **4**, 멈춤 0, Tick 시도 **300**.
- `avatar_predict=0`: 되감기 **0회**, 멈춤 **4**, Tick 시도 **304**.
- 두 팔의 최종 체크섬이 **지상 진실과 같다** — 처음부터 확정 입력만으로 굴린 상태와 비트 동일.
- 3000프레임을 굴려도 보관은 스냅샷 **8** · 입력 슬롯 **8**.
- 갈렸다가 같은 벽에 눌린 두 실행: 좌표는 동일(100000), 이력은 `29fd7dce` vs `c3606543`.

---

## 목차
1. [커서에서 아바타로](#1-커서에서-아바타로)
2. [명령과 프레임 입력은 성질이 반대다](#2-명령과-프레임-입력은-성질이-반대다)
3. [3바이트 — 바이트가 계약이다](#3-3바이트--바이트가-계약이다)
4. [중복 전송이 재전송을 대신한다](#4-중복-전송이-재전송을-대신한다)
5. [왜 아바타만 정수인가](#5-왜-아바타만-정수인가)
6. [벽이 숨기는 갈라짐](#6-벽이-숨기는-갈라짐)
7. [예측하고, 틀리면 되감는다](#7-예측하고-틀리면-되감는다)
8. [두 팔 — 되감기와 멈춤](#8-두-팔--되감기와-멈춤)
9. [되감을 수 없는 정정](#9-되감을-수-없는-정정)
10. [보관 창을 접는다](#10-보관-창을-접는다)
11. [설계에서 조정한 것](#11-설계에서-조정한-것)
12. [변이 셋과 살아남은 하나](#12-변이-셋과-살아남은-하나)
13. [실행과 관측](#13-실행과-관측)

---

## 1. 커서에서 아바타로

협동 디펜스에 아바타를 넣는 것은 장르의 관용구다. 그런데 이 게임에서 그것이 지금 들어오는 이유는 관용구여서가 아니다.

앞선 편들이 만든 것을 세어 보면 이렇다. 절차 생성 맵은 분기와 막다른 길을 갖고, 발파는 그 격자를 런타임에 바꾸고, 잔해는 통행 격자를 막고, 시야 원뿔은 은신한 적을 드러낸다. 이 네 가지 전부 **적과 타워 사이의 관계**다. 플레이어는 그 관계를 위에서 내려다보며 조정할 뿐 그 안에 들어가지 않는다.

아바타가 들어오면 같은 지형이 플레이어의 제약이 된다. 건설 사거리를 아바타 주변으로 묶으면 "어디에 지을까"가 "어디까지 갈 수 있는가"를 포함하게 되고, 그때 비로소 미로의 모양이 플레이어의 결정에 들어온다.

그 규칙은 A/B 스위치 뒤에 선다. 규약이 "기본값은 언제나 스위치가 생기기 전의 동작"을 요구하므로, 사거리 제약을 켠 것이 기본이 되려면 그것이 더 낫다는 판정이 먼저 있어야 한다. 이 편이 만드는 것은 그 판정을 할 수 있는 상태까지다.

와이어 쪽에서 생기는 것은 하나다. **프레임 단위 입력 스트림.** 명령은 그대로 남고 그 옆에 새 흐름이 하나 생긴다.

## 2. 명령과 프레임 입력은 성질이 반대다

두 흐름이 같은 채널을 쓸 수 없다는 것은 성질을 나란히 적어 보면 분명하다.

| | 명령(Place/Upgrade/Sell/Target/Blast) | 프레임 입력(moveX/moveY/buttons) |
|---|---|---|
| 빈도 | 드물다(플레이어가 누를 때만) | 매 프레임 |
| 손실 허용 | 안 된다 | 최신이 오면 그만이다 |
| 순서 | 의미가 있다 | 프레임 번호가 순서다 |
| 늦게 도착하면 | 그래도 반영한다 | 무의미하다 |
| 그래서 | 신뢰·순서 채널 | 비신뢰 + 중복 전송 |

프레임 입력을 신뢰 채널에 실으면 어떻게 되는지가 이 표의 핵심이다. 손실 한 번이 그 뒤의 모든 입력을 막는다(head-of-line blocking). 재전송이 도착할 때까지 뒤에 쌓인 입력이 앱에 전달되지 않으므로, 화면에서는 **아바타가 그 자리에 굳었다가 갑자기 순간이동한다**. 손실률 35%에서는 그 일이 초당 몇 번씩 일어난다.

대신 최근 몇 프레임분을 매번 통째로 다시 실어 보낸다. 중복은 재전송보다 싸고, 무엇보다 **지연이 0이다** — 잃어버린 프레임을 메우기 위해 왕복을 기다리지 않는다.

## 3. 3바이트 — 바이트가 계약이다

<!-- snippet: include/mp/input.hpp#Input mode=partial -->
```cpp
struct Input {
    int8_t moveX = 0;     // -1, 0, +1
    int8_t moveY = 0;
    uint8_t buttons = 0;  // 비트 플래그(kButtonFire 등)
    // ...
};
```

이동은 8방향이라 축마다 -1/0/+1 뿐이다. 아날로그가 아닌 것이 와이어 계약의 일부이고, 그 사실이 `kMoveAxisMax` 로 적혀 있다. 범위 밖 값을 통과시키면 그것이 정수 시뮬의 클램프를 우회하는 통로가 된다.

버튼도 마찬가지다. 정의된 비트의 합집합 `kButtonMask` 밖의 비트가 켜져 있으면 디코더가 거부한다. 읽지 않는 비트를 통과시키면 같은 조작을 뜻하는 입력이 여러 가지가 되고, 다음 버전이 그 비트에 의미를 주는 순간 옛 클라이언트의 쓰레기가 조작이 된다. 명령 디코더의 `argument == 0` 규칙과 같은 논리다.

인코딩은 손으로 쓴 세 줄이다.

<!-- snippet: include/mp/input.hpp@input-codec mode=partial -->
```cpp
inline void WriteInput(const Input& input, std::vector<uint8_t>& out) {
    out.push_back(static_cast<uint8_t>(input.moveX));   // 2의 보수 그대로
    out.push_back(static_cast<uint8_t>(input.moveY));
    out.push_back(input.buttons);
}
```

이 세 줄에 대해 검사가 묻는 것은 "왕복이 되는가"가 아니다. 왕복은 필드 순서를 바꿔도 성립한다. 묻는 것은 **바이트가 그 값인가**다.

```cpp
input.moveX = 1;
input.moveY = -1;
input.SetBuild(true);
WriteInput(input, bytes);
CHECK_EQ(bytes[0], (uint8_t)0x01);   // moveX = +1
CHECK_EQ(bytes[1], (uint8_t)0xFF);   // moveY = -1 (2의 보수)
CHECK_EQ(bytes[2], kButtonBuild);    // buttons
```

세 필드가 서로 다른 값이어야 순서 변경이 관측된다. `(0,0,0)` 이나 `(1,1,1)` 같은 대칭인 값으로는 이 검사가 아무것도 지키지 못한다.

## 4. 중복 전송이 재전송을 대신한다

한 패킷에 최근 8프레임분을 싣는다. 60Hz 에서 8프레임은 133 ms 이므로, 그보다 짧은 연속 손실은 다음 패킷 하나가 통째로 메운다.

상한이 8인 이유는 예산이다. 이 수를 올리면 매 패킷이 3바이트씩 커지고, 그 비용은 손실이 하나도 없을 때도 낸다. 8프레임이면 헤더 5 + 본문 24 = 29바이트로, 스냅샷 한 장(185.8 B)의 6분의 1 이하다.

효과는 재는 것이 낫다.

```
35% 손실 · 8프레임 중복: 패킷 230개 유실 · 끝내 못 받은 프레임 0/592
```

600프레임 중 패킷 230개를 잃었는데 **입력은 하나도 잃지 않았다.** 한 프레임이 사라지려면 그것을 실은 패킷 여덟 개가 연속으로 사라져야 하고, 0.35의 여덟 제곱은 2.25e-4 다.

마지막 8프레임을 세지 않는 이유도 적어 둔다 — 그 구간은 아직 중복될 기회를 다 쓰지 못했으므로, 거기서의 손실은 이 규칙의 결함이 아니라 아직 오지 않은 패킷이다. 그 구분을 하지 않으면 검사가 마지막 프레임의 35% 확률에 흔들린다.

스트림 디코더의 규율은 다른 디코더와 같다. 선언한 개수가 상한을 넘으면 거부, 바이트가 남거나 모자라면 거부, 안의 입력 하나가 범위 밖이면 스트림 전체가 무효다.

```cpp
    if (bytes.size() != kInputStreamHeaderBytes + static_cast<size_t>(count) * kInputBytes)
        return false;   // 잉여·부족 바이트 거부. 다른 디코더와 같은 규율이다.
```

배선은 얇다. `PacketType::Input = 6` 을 더하고, 본문은 `mp::WriteInputStream` 을 그대로 감싼다 — 입력 레코드의 바이트 계약은 `mp/` 하나가 소유한 채로 남는다.

```cpp
static constexpr size_t kInputPacketMaxBytes =
    16 + mp::kInputStreamHeaderBytes + mp::kMaxInputFrames * mp::kInputBytes;   // 45
static_assert(kInputPacketMaxBytes <= kMaxPacketBytes,
              "아바타 입력 패킷이 UDP payload 예산을 넘는다");
```

`kVersion` 은 3에서 4로 올랐다. 같은 편에 두 단계를 올리지 않는 것이 규약이다 — 프레이밍 · 델타 · 입력을 한 커밋에 묶으면 어느 변경이 협동을 깼는지 이분할 수 없다.

**보내는 자리가 두 곳인 것은 대칭 때문이다.** 클라이언트만 보내면 클라이언트 쪽 `player 0`(호스트의 아바타)은 언제나 "직전 반복"으로 예측되고, 호스트가 실제로 움직이는 순간 두 사본이 갈라진다. 그래서 호스트도 매 고정 스텝마다 자기 창을 되보낸다.

그리고 창에 넣는 시점이 규칙 하나를 만든다.

<!-- snippet: include/game/defense_app.hpp@avatar-input -->
```cpp
    void StepAvatars(const FrameInput& frame) {
        // ...
        avatars_.SetInput(avatarFrame_, localAvatar_, input, true);
        // ...
        if (avatarFrame_ == windowEnd_) {
            // ...
            ++windowEnd_;
        }
        if (!avatars_.Tick()) return;   // 비예측 팔: 원격 확정이 없으면 멈춘다
        ++avatarFrame_;
        // ...
    }
```

`Tick()` 앞에서 창에 넣는다. 뒤로 미루면 비예측 팔에서 양쪽이 서로를 기다리며 영원히 굳는다 — 내 입력을 안 보내니 상대가 프레임을 못 열고, 상대가 못 여니 내 입력이 확정되지 않는다. 이 교착은 배선 전에는 존재할 수 없던 상태였고, 두 제품 프로세스를 실제로 붙여 보고 나서야 드러났다.

**실측.** 루프백 한 쌍(호스트 8000틱 · 클라이언트 8000틱)에서:

```
  (클라이언트) input_packets=4 input_bytes=102        → 평균 25.5B
  (호스트)     input_packets=145 input_bytes=4335     → 평균 29.9B
```

창이 아직 8프레임을 채우지 못한 구간이 섞여 있어 45바이트 상한보다 작다. 창이 가득 찬 뒤의 한 장은 정확히 `16 + 5 + 8×3 = 45`바이트다.

같은 링크를 900회 돌린 검사에서는 이렇다.

```
> test_avatar_wiring.exe
  아바타 스트림(predict=1): client_sent=899 host_recv=898 client_recv=899 대조=299 desync=0 frame=900
```

세 수가 전부 0이 아니어야 한다는 것이 배선 검사의 전부다. `client_sent` 가 0이면 클라이언트가 벙어리이고, `host_recv` 가 0이면 호스트가 귀머거리이며, `client_recv` 가 0이면 위에 적은 대칭이 깨져 상시 desync 가 된다.

## 5. 왜 아바타만 정수인가

이 게임의 권위 시뮬 — 적·타워·발사체·잔해 강체 — 은 float 이고 앞으로도 float 다. 그런데 아바타는 정수다. 두 규칙이 한 제품 안에 공존하는 것이 이상해 보이므로 근거를 분명히 적는다.

**정수화의 비용을 내는 이유는 예측 때문이다.** 클라이언트가 자기 아바타를 즉시 움직이고 나중에 호스트의 확정과 대조하려면, "같은 입력에서 같은 결과"가 두 머신에서 성립해야 한다. float 는 그것을 보장하지 않는다 — 같은 소스라도 컴파일러가 곱셈-덧셈을 하나로 접느냐 마느냐에 따라 마지막 비트가 갈린다.

**그 비용을 낼 필요가 없는 곳도 분명하다.** 클라이언트는 적과 타워를 예측하지 않는다. 호스트의 스냅샷을 보간해 그릴 뿐이라 재현할 필요가 없고, 재현할 필요가 없으면 비트 동일성도 필요 없다. 강체 적분과 접촉 솔버를 정수로 옮기는 것은 이 게임이 요구한 적 없는 작업이다.

그래서 경계는 "무엇을 예측하는가"와 정확히 같은 자리에 그어진다. 예측하는 부분만 정수다.

<!-- snippet: include/mp/det_sim.hpp@det-step mode=partial -->
```cpp
    void Step(const std::vector<Input>& inputs) {
        uint32_t r = rng;
        auto next = [&] { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
        int n = (int)px.size();
        for (int i = 0; i < n; ++i) {
            const Input& in = (i < (int)inputs.size()) ? inputs[i] : Input{};
            px[i] += (int32_t)in.moveX * kSpeed;
            py[i] += (int32_t)in.moveY * kSpeed;
            // ...
            if (px[i] < 0) px[i] = 0; if (px[i] > kWorld) px[i] = kWorld;
            if (py[i] < 0) py[i] = 0; if (py[i] > kWorld) py[i] = kWorld;
            const int32_t cell = CellIndex(px[i], py[i]);
            trail[i] = (trail[i] ^ (uint32_t)cell) * 16777619u;
            buildCell[i] = in.Build() ? cell : kNoBuild;
            // ...
            if (in.Fire()) score[i] += (int32_t)(next() % 10);
        }
        rng = r;
        ++frame;
    }
```

`buildCell` 이 건설 규칙과 만나는 자리다. 이 프레임에 Build 를 눌렀으면 아바타가 서 있는 칸 번호가, 아니면 `kNoBuild` 가 상태에 남는다. 요청이 **상태의 일부**인 것이 중요하다 — 되감기가 일어나면 그 요청도 함께 되감겨야 하고, 상태 밖의 부수 효과로 두면 그 되감기가 불가능해진다.

칸 격자를 16 × 16 으로 둔 것은 제품 맵과 같은 수를 쓰기 위해서다. 아바타가 "어느 칸에 서 있는가"가 건설 승인의 입력이므로, 두 격자가 다르면 규칙이 성립하지 않는다.

승인 판정 자체는 한 함수다.

```cpp
    static bool CellsWithinReach(int32_t a, int32_t b, int32_t reach) {
        if (a < 0 || b < 0) return false;
        const int32_t dx = (a % kCells) - (b % kCells);
        const int32_t dy = (a / kCells) - (b / kCells);
        ...
    }
```

1차원 칸 번호를 2차원으로 되돌려 비교하는 이유가 검사에 적혀 있다. 행 끝(15)과 다음 행 첫 칸(16)은 번호 차이가 1이라 그냥 빼면 인접해 보이지만, 격자에서는 반대편 끝이다.

호스트와 클라이언트가 **같은 함수**를 불러야 한다는 것도 계약의 일부다. 둘이 갈라지면 눌리는데 안 서는 칸이 생긴다.

배선의 자리는 **명령이 월드에 닿기 직전**이다. `GameWorld::PlaceTowerType` 은 한 줄도 건드리지 않았다 — 그 함수를 직접 부르는 기존 검사(`test_defense_protocol` · `test_game` · `test_game_expansion`)는 아바타를 모르고, 알 필요도 없다.

그리고 문은 하나여야 한다. 로컬 제출만 검사하면 사거리 규칙이 자기 자신에게만 적용되고, 클라이언트는 맵 어디에나 지을 수 있다.

<!-- snippet: include/game/defense_app.hpp@avatar-gate -->
```cpp
    bool BuildAllowed(const wire::Command& command, int player) {
        if (!options_.switches.avatarGate) return true;
        if (command.type != wire::CommandType::Place) return true;
        const math::Vec2 position{wire::DecodePosition(command.x),
                                  wire::DecodePosition(command.y)};
        if (wire::BuildWithinReach(map_, avatars_.State().Cell(player), position))
            return true;
        ++buildRejectedOutOfReach_;
        return false;
    }
```

`Submit` 이 `localAvatar_` 로, `PumpHost` 의 적용 루프가 `remoteAvatar_` 로, 전용 서버가 그 클라이언트의 슬롯으로 같은 함수를 부른다. HUD 커서도 같은 `wire::BuildWithinReach` 로 칠한다 — 규칙과 표시가 다른 사본을 쓰면 초록으로 칠해 놓고 안 서는 칸이 생긴다. 사거리 밖이지만 규칙상 지을 수 있는 칸은 셋째 색(노랑)을 얻었다. 초록/빨강 둘만 두면 "지금 닿지 않는다"가 "여기는 못 짓는다"로 보인다.

**두 팔.** 같은 칸, 같은 클릭이다.

```
> test_avatar_wiring.exe
  avatar_gate: off towers=1 rejected=0 / on towers=0 rejected=1 walk=63틱
```

팔 0은 아바타에서 가장 먼 건설 가능 칸에 그대로 짓는다. 팔 1은 짓지 않고 거부를 정확히 한 번 센다. 그리고 63틱을 걸어가면 **선다** — "영원히 못 짓는다"는 게이트가 아니라 결함이므로, 그 뒷문장까지가 검사다.

제품 셀프테스트에서도 같은 차이가 난다.

```
> mega_defense --selftest                       (avatar_gate=0)
[selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181
[selftest] avatar frame=1377 cell=136 walk_ticks=0   gate_rejected=0

> mega_defense --selftest --ab avatar_gate=1
[selftest] result=CLEAR ticks=1339 towers=12 wave=6 packet=195
[selftest] avatar frame=1339 cell=199 walk_ticks=192 gate_rejected=0
```

`walk_ticks` 0 → 192 가 스위치가 바꾼 것의 전부다. 스크립트가 목표 칸까지 걸어간 뒤에만 클릭하므로 `gate_rejected` 는 0으로 남는다 — 거부가 0인 것이 게이트가 죽었다는 뜻이 아니라, 게이트를 아는 클라이언트가 애초에 사거리 밖을 누르지 않는다는 뜻이다. 게이트가 실제로 거절하는 것은 위 검사가 센다.

## 6. 벽이 숨기는 갈라짐

체크섬에 `trail` 을 넣은 이유는 검사가 만든 것이다.

두 실행이 갈렸다가 양쪽 다 같은 벽에 눌리면 좌표가 도로 같아진다. 위치만 비교하는 체크섬은 그 순간 다시 일치하고, "desync 가 없었다"가 된다.

```cpp
// 갈림: 한쪽만 처음 두 프레임에 왼쪽으로 갔다가 돌아온다.
detoured.Step(left);
detoured.Step(right);
straight.Step(right);
straight.Step(right);
CHECK(straight.px[0] != detoured.px[0]);          // 실제로 갈렸다

for (int f = 0; f < push; ++f) { straight.Step(right); detoured.Step(right); }

CHECK_EQ(straight.px[0], DetState::kWorld);       // 둘 다 벽에 눌렸다
CHECK_EQ(detoured.px[0], DetState::kWorld);
CHECK(straight.trail[0] != detoured.trail[0]);    // 이력은 갈린 채로 남는다
```

`trail` 은 밟은 칸 번호를 FNV 로 접는다. 한 번 갈리면 되돌아오지 않는다.

이것이 desync 탐지의 감도를 올리는 값싼 방법이다 — 상태에 4바이트를 더하고, 그 대가로 "갈라졌다가 우연히 다시 붙은" 경우를 놓치지 않는다. 벽은 그런 우연을 만드는 가장 흔한 장치이고, 아바타는 벽에 자주 붙는다.

## 7. 예측하고, 틀리면 되감는다

rollback 의 정확성 조건은 하나다. **되감은 결과가 "처음부터 확정 입력을 알았을 경우"와 비트 동일할 것.** 한 비트라도 다르면 그 차이는 프레임마다 쌓여 결국 desync 가 된다.

<!-- snippet: include/mp/rollback.hpp@rollback-set-input mode=partial -->
```cpp
    void SetInput(uint32_t frame, int player, const Input& in, bool confirmed) {
        if (player < 0 || player >= n_) return;
        if (confirmed && frame_ > (uint32_t)maxRollback_ &&
            frame < frame_ - (uint32_t)maxRollback_) {
            // 되감을 상태가 이미 버려졌다. 여기서 조용히 반환하면 이 클라이언트는
            // 호스트와 다른 상태를 들고 아무 일 없다는 듯 계속 돈다.
            ++lateConfirmations_;
            return;
        }
        // ...
        if (confirmed && frame < frame_) {
            auto u = used_.find(frame);
            if (u != used_.end() && u->second[player] != in) {
                Resimulate(frame);                 // 예측 빗나감 → 되감기
            }
        }
    }
```

되감기는 `used_` 와의 비교로만 촉발된다. "그 프레임에 실제로 사용한 입력"과 "지금 도착한 확정 입력"이 다를 때만 되감고, 예측이 맞았으면 아무 일도 하지 않는다. 이 구분이 없으면 확정이 도착할 때마다 되감으므로 예측의 이득이 사라진다.

실측에서 300프레임 · 지연 4프레임에 되감기가 **283회** 일어난다. 거의 매 프레임이다 — 원격 플레이어의 입력이 무작위로 바뀌므로 "직전 입력 반복"이라는 예측은 자주 틀린다. 되감기가 흔한 것이 결함이 아니라, 그것이 rollback 이 하는 일이다. 중요한 것은 되감은 뒤의 상태가 맞다는 것이고, 깊이가 상한(8) 안에 머무는 것이다. 실측 최대 깊이는 **4**로 지연과 같다.

## 8. 두 팔 — 되감기와 멈춤

`avatar_predict=0` 은 예측을 끈다. 모든 플레이어의 입력이 확정될 때까지 `Tick` 이 전진을 거부하므로 되감기가 아예 일어나지 않고, 대신 로컬 조작이 왕복 시간만큼 늦게 반영된다.

두 팔의 판정은 A/B 규약대로 둘이 함께 온다.

**결과가 같다.** 두 팔의 최종 체크섬이 지상 진실 — 처음부터 확정 입력만으로 굴린 상태 — 과 같다. 예측하든 안 하든 도착하는 곳은 하나다.

**비용이 다르다.**

```
avatar_predict=1: 프레임 300 · 되감기 283회 · 최대 깊이 4 · 멈춤 0 · Tick 시도 300
avatar_predict=0: 프레임 300 · 되감기 0회 · 최대 깊이 0 · 멈춤 4 · Tick 시도 304
```

`Tick 시도` 300 vs 304 가 그 차이다. 비예측 팔은 300프레임을 굴리는 데 304번 두드려야 했고, 그 4번이 정확히 원격 지연(4프레임)이다. 화면에서 그것은 **키를 누르고 4프레임 뒤에 아바타가 움직인다**로 보인다. 60Hz 에서 67 ms 이고, 링크가 멀어질수록 이 수가 그대로 커진다.

예측 팔은 그 지연이 0이다. 대가는 되감기 283회의 CPU 이고, 되감기 하나는 최대 4프레임 재시뮬이다.

**기본 팔은 무엇인가.** 지금은 `avatar_predict=0` 이다. A/B 규약이 "기본값은 언제나 스위치 이전의 동작"을 요구하고, 스위치 이전에는 클라이언트가 예측하지 않았기 때문이다. 예측 팔이 이기려면 제품 경로에서 재시뮬 비용이 프레임 예산 안이라는 측정이 있어야 하고, 그 측정은 아바타가 실제로 배선된 뒤에 나온다.

`avatars_.SetPrediction(options_.switches.avatarPredict)` 한 줄이 배선의 전부이고, 나머지는 무엇을 재느냐다.

**솔로에서는 두 팔이 같다.** 아바타가 하나뿐이면 기다릴 원격이 없으므로 `Tick()` 이 언제나 성공한다.

```
> mega_defense --selftest --ab avatar_predict=0
[selftest] avatar frame=1377 cell=136 checksum=0xC07BEE8E stalls=0 rollbacks=0
[profile] ... sim.avatar=5.57/0.067ms(n=1377) sim.step=4.34/0.095ms(n=1377) ...

> mega_defense --selftest --ab avatar_predict=1
[selftest] avatar frame=1377 cell=136 checksum=0xC07BEE8E stalls=0 rollbacks=0
[profile] ... sim.step=3.63/0.065ms(n=1377) sim.avatar=3.21/0.022ms(n=1377) ...
```

체크섬이 같은 것은 당연하고(같은 입력 열), `sim.avatar` 가 1377프레임 합계 3~6 ms 라는 것이 여기서 얻는 유일한 수다 — 프레임당 0.004 ms, `render.draw`(1015 ms)의 0.5% 다. 이 정수 서브시뮬은 프레임 예산에서 보이지 않는다.

**차이는 두 프로세스가 붙었을 때 나온다.**

```
> test_avatar_wiring.exe          (호스트 · 클라이언트 모두 제품 DefenseApp, 900회 스텝)
  avatar_predict: 1 → stalls=0   rollbacks=1 frame=900 대조=299
                  0 → stalls=450 rollbacks=0 frame=450 대조=300
```

(a) 결과가 같다: 두 팔 모두 `desync=0` 이고, 호스트가 스냅샷에 실은 (프레임, 체크섬)과 클라이언트 자신의 것이 299·300회 대조에서 전부 일치한다.
(b) 비용이 다르다: 예측 팔은 900프레임을 굴리는 동안 한 번도 멈추지 않고 되감기를 한 번 했다. 비예측 팔은 450번 멈췄고 그만큼 프레임이 뒤처졌다 — 같은 900회 스텝에서 450프레임밖에 못 갔다.

되감기가 **한 번**뿐인 것도 읽을 거리다. 예측 규칙이 "직전 확정 입력 반복"이고 이 검사의 상대는 계속 같은 키를 누르므로, 예측이 빗나가는 것은 첫 프레임 한 번뿐이다. 실제 플레이에서 이 수는 상대가 방향을 바꾸는 빈도에 비례한다.

**기본 팔은 여전히 `avatar_predict=0` 이다.** 규약이 요구하는 것("기본값은 스위치 이전의 동작")이 하나이고, 위 수치가 예측 팔의 손을 들어 주는 것은 이 링크(루프백, 지연 1프레임)에서다. 지연이 큰 링크에서 되감기 깊이가 어떻게 자라는지는 아직 재지 않았다.

## 9. 되감을 수 없는 정정

`maxRollback` 보다 오래된 확정이 도착하면 되감을 상태가 없다. 원래 이 경우는 `Resimulate` 가 조용히 반환했다.

조용한 것이 문제다. 그 순간 이 피어는 호스트와 다른 세계를 들고 **아무 일 없다는 듯 계속 돈다.** 화면은 멀쩡하고 로그도 조용하다. 다음에 무언가가 어긋날 때 그 원인을 여기까지 되짚을 방법이 없다.

그래서 센다.

```cpp
Input late = inputs[20][1];
late.moveX = static_cast<int8_t>(-late.moveX);
rollback.SetInput(20, 1, late, true);          // 92프레임 전 — 창 밖이다
CHECK_EQ(rollback.LateConfirmations(), (uint64_t)1);
CHECK(rollback.DesyncSuspected());
CHECK_EQ(rollback.Frame(), (uint32_t)100);     // 상태는 건드리지 않는다
```

`DesyncSuspected()` 가 켜지면 재동기화가 필요하다는 뜻이다. 되감기로는 회복되지 않는다 — 이것은 "고칠 수 있는 예측 실패"가 아니라 "고칠 수 없는 상태 분기"이고, 그 둘을 같은 침묵으로 다루면 안 된다.

상태를 건드리지 않는 것도 규칙이다. 늦은 확정을 반쯤 반영하면 어느 프레임의 상태도 아닌 것이 만들어진다.

## 10. 보관 창을 접는다

원래 구현에는 조용한 누수가 있었다. 스냅샷(`snap_`)만 창 밖을 버렸고, 입력 표(`slots_`)와 사용 기록(`used_`)은 매치 내내 자랐다. 그리고 예측 한 번이 `slots_` 를 처음부터 훑는다.

```cpp
    Input PredictedInput(uint32_t frame, int p) const {
        auto it = slots_.find(frame);
        if (it != slots_.end() && it->second.conf[p]) return it->second.in[p];
        Input last = floor_[p];
        for (const auto& [f, s] : slots_) {
            if (f > frame) break;
            ...
        }
        return last;
    }
```

프레임당 비용이 **경기 시간에 비례**한다는 뜻이다. 300프레임짜리 검사에서는 보이지 않고, 5분짜리 매치에서 보인다.

그냥 버리면 예측 규칙이 바뀐다. 예측은 "이 프레임 이전의 마지막 확정 입력"이고, 창 밖에 그 마지막이 있을 수 있다. 오래 침묵한 플레이어의 예측이 "입력 없음"으로 되돌아가면 그 아바타가 갑자기 멈춘다.

그래서 버리기 전에 **접는다.** 창 밖으로 나가는 프레임의 확정 입력을 플레이어별 바닥값 `floor_` 에 덮어쓰고, 예측은 바닥값에서 시작한다. `std::map` 이 오름차순이므로 접는 순서가 프레임 순서와 같아 결과가 정확히 같다.

```
3000프레임 후 보관: 스냅샷 8 · 입력 슬롯 8 (상한 8)
```

접기가 규칙을 보존하는지는 별도 검사가 짚는다. 프레임 0에만 원격 확정이 오고 그 뒤 40프레임을 예측으로 채우면, 원격 아바타는 40프레임 내내 오른쪽으로 가야 한다. 접기가 없으면 그 아바타는 창을 벗어나는 순간 멈춘다.

## 11. 설계에서 조정한 것

**아바타를 float 권위 시뮬에 넣는 안을 기각했다.** 그렇게 하면 정수화 비용이 없어지지만 예측이 불가능해진다. 예측 없는 아바타는 링크가 멀수록 조작이 무거워지고, 그것은 "맵 위를 돌아다닌다"는 이 편의 목적과 정면으로 부딪힌다.

**`buildCell` 을 상태에 넣었다.** 부수 효과(콜백·이벤트 큐)로 두는 쪽이 짧지만, 되감기가 일어나면 그 효과도 되감아야 한다. 상태 안에 두면 되감기가 공짜다.

**클램프 순서에 대한 주장을 축소했다.** `Step` 안에서 좌표 클램프를 칸 유도보다 먼저 두었고 그것이 옳지만, `CellOf` 가 자기 몫의 클램프를 따로 하고 있어서 **순서를 바꿔도 관측되는 차이가 없다.** 변이로 확인했다(12절). 두 겹인 것을 결함으로 보지 않는 이유는 `CellOf` 의 클램프가 이 함수만을 위한 것이 아니기 때문이다 — 호스트가 클라이언트에게 받은 칸 번호를 검증할 때도 같은 함수를 부른다.

**입력 중복을 8프레임으로 고정했다.** RTT 에 따라 늘리는 안이 있었지만, 그러면 손실이 없을 때도 링크가 먼 것만으로 대역폭을 쓴다. 손실률에 맞춰 늘리는 쪽이 옳고, 그것은 손실률을 재는 장치가 생긴 뒤의 일이다.

## 12. 변이 셋과 살아남은 하나

**하나. `WriteInput` 의 필드 순서를 바꿨다** (moveY 를 먼저).

```
> test_avatar_input.exe
  FAIL test_avatar_input.cpp:181  CHECK(decoded[i] == timeline[frame])
  ... (같은 줄 다수)
[test] avatar_input: 3462 checks, 1976 failures
```

1976개가 죽는다. 골든 바이트 세 줄이 먼저 죽고, 그다음 36가지 전수 왕복과 600프레임 중복 전송 검사가 연쇄로 죽는다. 인코더와 디코더가 **같은 파일 안에서 짝을 이루고 있으므로 왕복 자체는 성립한다** — 순서를 바꿔도 `Write` 다음 `Read` 는 원래 값을 돌려준다. 죽는 것은 바이트를 직접 본 단언들이고, 그것이 이 검사가 왕복이 아니라 바이트를 묻는 이유다.

**둘. `Step` 의 이력 접기를 지웠다.**

```
> test_desync_detect.exe
  FAIL test_desync_detect.cpp:110  CHECK(straight.trail[0] != detoured.trail[0])
  FAIL test_desync_detect.cpp:111  CHECK(straight.Checksum() != detoured.Checksum())
  벽에 눌린 두 실행: 좌표 동일(100000) · 이력 811c9dc5 vs 811c9dc5
[test] desync_detect: 38 checks, 2 failures
```

두 이력이 초기값 `811c9dc5` 그대로다. 갈라졌던 두 실행의 체크섬이 다시 일치하고, desync 탐지가 그것을 놓친다.

**셋. `Resimulate` 에서 스냅샷 복원을 지웠다** (`cur_ = sn->second;`).

```
> test_avatar_predict.exe
  FAIL test_avatar_predict.cpp:122  CHECK_EQ(predicted.checksum, truth)
  FAIL test_avatar_predict.cpp:140  CHECK_EQ(report.checksum, GroundTruth(inputs, 2, frames))
  ... (지연 1 · 4 · 8 셋 다)
[test] avatar_predict: 3174 checks, 4 failures
```

프레임 번호만 되돌리고 상태는 그대로 두면 재시뮬이 **이미 진행된 상태 위에** 과거의 입력을 다시 먹인다. 되감기 횟수(283)와 깊이(4)는 그대로이므로, 되감기가 "일어났는지"만 세는 단언으로는 잡히지 않는다. 결과를 지상 진실과 대조하는 단언만이 이것을 잡는다.

**살아남은 하나. `Step` 의 클램프 두 줄을 칸 유도 아래로 옮겼다.**

```
> test_desync_detect.exe
[test] desync_detect: 38 checks, 0 failures
```

아무것도 죽지 않는다. `CellOf` 가 `cell >= kCells` 를 스스로 잘라내므로, 클램프 전 좌표로 나눗셈을 해도 같은 칸 번호가 나온다. 이것은 검사의 구멍이 아니라 **두 방어가 겹쳐 있다는 사실의 반영**이다. 겹침을 없애 순서를 유일한 방어로 만드는 선택도 가능하지만, `CellOf` 는 아바타 시뮬 밖에서도 불리므로 자기 방어를 갖고 있어야 한다. 그래서 겹침을 남기고, 순서가 관측되지 않는다는 사실을 소스 주석과 검사 주석에 적어 두었다.

## 13. 실행과 관측

**입력 레코드.**

```
> test_avatar_input.exe
[test] avatar_input
  35% 손실 · 8프레임 중복: 패킷 230개 유실 · 끝내 못 받은 프레임 0/592
[test] avatar_input: 3462 checks, 0 failures
```

**결정성과 탐지.**

```
> test_desync_detect.exe
[test] desync_detect
  입력 1개 차이: 첫 갈림 프레임 10 · 이후 다시 붙은 횟수 0
  벽에 눌린 두 실행: 좌표 동일(100000) · 이력 29fd7dce vs c3606543
[test] desync_detect: 38 checks, 0 failures
```

첫 줄을 읽는 법: 500프레임 동안 입력 하나(프레임 10, 플레이어 1의 moveX)만 다른 두 실행이 프레임 10에서 갈리고, 그 뒤 490프레임 동안 체크섬이 **한 번도** 다시 일치하지 않는다. 체크섬이 갈라짐을 유지하는 것은 자명하지 않다 — 정수 상태가 클램프에 눌리면 실제로 다시 붙는다.

**예측과 되감기.**

```
> test_avatar_predict.exe
[test] avatar_predict
  avatar_predict=1: 프레임 300 · 되감기 283회 · 최대 깊이 4 · 멈춤 0 · Tick 시도 300
  avatar_predict=0: 프레임 300 · 되감기 0회 · 최대 깊이 0 · 멈춤 4 · Tick 시도 304
  3000프레임 후 보관: 스냅샷 8 · 입력 슬롯 8 (상한 8)
[test] avatar_predict: 3174 checks, 0 failures
```

지연 1 · 4 · 8 세 값에서 전부 최종 체크섬이 지상 진실과 같고, 최대 깊이가 상한 이하다.

## 11. 제품 실측

```
> mega_defense --selftest
[selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181 protocol=OK render=OK assets=10/10 content=external stomps=2
[selftest] avatar frame=1377 cell=136 checksum=0xC07BEE8E stalls=0 rollbacks=0 walk_ticks=0 gate_rejected=0 input_packets=0 input_bytes=0 desync=0/0
```

솔로는 입력 패킷을 하나도 보내지 않는다(`input_packets=0`) — 보낼 상대가 없기 때문이다. 아바타 서브시뮬은 그래도 돈다(`frame=1377`). 그것이 옳은 이유는 아바타가 네트워크 기능이 아니라 게임플레이이기 때문이다.

협동은 두 프로세스에서:

```
> mega_defense --host 27103 --selftest 8000   (백그라운드)
> mega_defense --join 127.0.0.1 27103 --selftest 8000

  (클라이언트) [selftest] result=LINKED ticks=5 snapshots=2 commands_sent=1 ack=1 towers=9 wave=5
               [selftest] avatar frame=2 cell=136 checksum=0x165D7FC6 stalls=3 input_packets=4 desync=0/0
  (호스트)     [selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181
               [selftest] avatar frame=2 cell=136 checksum=0x165D7FC6 stalls=1375 input_packets=145 desync=0/0
```

두 **프로세스**의 체크섬이 `0x165D7FC6` 로 같다. 이 편이 세우려 한 것이 정확히 그 한 줄이다 — 서로 다른 실행 파일 인스턴스가 서로의 입력만 교환해서 비트 동일한 상태에 도달했다.

호스트의 `stalls=1375` 는 결함이 아니라 기본 팔의 정의다. 호스트도 `avatar_predict=0` 이라 클라이언트의 프레임 입력이 도착해야 전진하고, 클라이언트가 왕복을 증명하고 5틱 만에 빠져나가므로 그 뒤로는 기다릴 상대가 없다.

CTest 는 111개 대상을 전수 통과한다. 이 편이 들여온 것은 `test_avatar_input`(입력 레코드의 골든 바이트) · `test_desync_detect`(체크섬이 갈라짐을 놓치지 않는가) · `test_avatar_predict`(rollback 두 팔) · `test_avatar_wiring`(제품 배선 셋) 이고, `test_mp_rollback` 에는 비예측 팔 케이스가 하나 늘었다.

---

## 결론

이 편이 실제로 한 일은 "아바타를 넣었다"보다 좁다. **예측하는 부분과 예측하지 않는 부분 사이에 선을 그었다.**

그 선을 긋기 전에는 "결정성"이 프로젝트 전체에 걸린 막연한 요구였다. 강체를 정수로 옮겨야 하나? 부동소수점 모드를 고정해야 하나? 두 질문 다 답이 "경우에 따라"였고, 그런 답은 지킬 수 없다.

선을 긋고 나면 답이 좁아진다. 클라이언트가 재현해야 하는 것만 비트 동일해야 하고, 그것은 아바타 서브시뮬 하나다. 나머지는 float 로 남고 스냅샷 양자화가 오차를 흡수한다. 정수화의 비용을 필요한 곳에만 낸다.

측정에서 나온 것 셋.

**되감기는 흔한 일이다.** 300프레임에 283회. 예측이 자주 맞을 것이라는 직관이 틀렸고, rollback 의 가치는 "예측이 잘 맞는다"가 아니라 "틀려도 흔적이 남지 않는다"에 있다.

**두 팔의 차이는 4프레임이다.** 그 4는 원격 지연과 정확히 같고, 어떤 최적화로도 줄지 않는다. 예측만이 그것을 0으로 만든다.

**보관 창을 접지 않으면 프레임 비용이 경기 시간에 비례한다.** 이 결함은 300프레임짜리 검사에서는 보이지 않았다. 3000프레임을 굴리고 보관 개수를 세는 단언을 넣고 나서야 드러났다.

현재 상태의 한계 넷.

**아바타는 지형을 모른다.** `DetState` 의 클램프는 월드 경계뿐이고 벽·타워·잔해를 통과한다. 통행 격자를 정수 서브시뮬에 넣으려면 그 격자가 양쪽에서 비트 동일해야 하고, 그것은 발파와 잔해가 그 격자를 바꾸는 지금 구조에서 간단하지 않다.

**두 아바타가 서로를 통과한다.** 아바타끼리의 충돌은 정수 시뮬에 넣을 수 있지만, 그러면 한쪽의 예측 실패가 다른 쪽의 위치를 흔들어 되감기가 전파된다.

**`kSpeed` 가 고정이다.** 프레임당 500 유닛은 60Hz 를 전제한 값이고, 시뮬 주파수가 바뀌면 아바타의 속도가 바뀐다. 정수 시뮬이라 dt 를 곱할 수 없다는 것이 그 이유이며, 주파수를 상수로 고정하는 것 말고 다른 방어는 없다.

**되감기 비용이 측정되지 않았다.** 283회의 재시뮬이 프레임 예산에서 얼마를 차지하는지는 아바타가 제품 경로에 배선되고 프로파일러 구간이 그 위에 놓인 뒤에야 잴 수 있다. 그때까지 `avatar_predict` 의 기본 팔은 스위치 이전의 동작으로 남는다.
