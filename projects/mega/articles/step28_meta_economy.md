# 28. 사라지는 돈에는 인플레이션이 없다

## 들어가며

이 게임의 골드는 매치 안에서만 산다. 적을 잡으면 들어오고, 타워를 세우면 나가고, 한 판이 끝나면 전부 사라진다. 그 구조에서는 경제에 관해 물을 수 있는 질문이 거의 없다 — "통화가 쌓인다"는 문장이 성립하지 않으므로 인플레이션이라는 개념 자체가 존재하지 않고, "발행보다 소각이 적다"는 관측도 5분 뒤에 무의미해진다.

`live/economy.hpp` 는 2편의 등재부에 이렇게 적혀 있었다.

> 지상 기지 루프의 통화 source/sink 와 총 통화량 보존 불변식. 현 디펜스의 골드는 매치 안에서만 살고 매치가 끝나면 사라져 인플레이션이라는 개념이 성립하지 않는다.

등재를 지우려면 **골드를 고치는 것이 아니라 그 위에 층을 하나 얹어야 한다.** 매치 종료 정산에서 나오고, 매치 밖 영구 강화로 나가고, 프로필에 남는 통화. 이름은 스크랩이다.

골드는 그대로 둔다. 매치 안 경제는 이미 그 자체로 완결돼 있고, 그것을 영속시키면 한 판을 오래 버틴 사람이 다음 판을 시작부터 이기는 구조가 된다. 층을 나누는 것이 요점이다.

| 헤더 | 하는 일 |
|---|---|
| `live/economy.hpp` | source/sink 태그 · 보존 불변식 세 개 · 영구 강화 · 지갑 왕복 |

도달점의 수치.

- 100매치 x 3명 시뮬 뒤 **sourced=32677 sunk=32576 supply=101**, 인플레율 **0.003**, 잔액 부족으로 거부된 지출 **119건**.
- 세 불변식(A/B/C)을 **매 거래 뒤에** 확인한다. 300매치 분량 606회.
- 잔액 검사를 지운 변이에서 **A 와 B 는 끝까지 참**이고 C 만 깨진다 — 실패 615건 전부 "잔액이 음수".
- 지갑 왕복: `balance=1150 sourced=2400 sunk=1250 tier=1 unlocks=1` 이 WAL 재생 뒤 비트 그대로 돌아온다.

---

## 목차
1. [층을 나눈다](#1-층을-나눈다)
2. [source 와 sink 에 이름을 붙이는 이유](#2-source-와-sink-에-이름을-붙이는-이유)
3. [불변식 셋 — 그리고 진짜 방어선은 C 다](#3-불변식-셋--그리고-진짜-방어선은-c-다)
4. [계정 순회를 정렬해 두는 이유](#4-계정-순회를-정렬해-두는-이유)
5. [영구 강화가 다음 매치의 시작 조건을 바꾼다](#5-영구-강화가-다음-매치의-시작-조건을-바꾼다)
6. [가격이 가팔라야 하는 이유](#6-가격이-가팔라야-하는-이유)
7. [지갑 — live 가 db 를 인클루드하지 않고 저장되는 법](#7-지갑--live-가-db-를-인클루드하지-않고-저장되는-법)
8. [저장 파일도 신뢰 경계 밖이다](#8-저장-파일도-신뢰-경계-밖이다)
9. [영구 강화가 밸런스 검증을 바꾼다](#9-영구-강화가-밸런스-검증을-바꾼다)
10. [변이 하나](#10-변이-하나)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 층을 나눈다

두 통화의 수명이 다르다.

| | 골드 | 스크랩 |
|---|---|---|
| 수명 | 한 매치 | 프로필 |
| 발행 | 적 처치 보상 | 매치 종료 정산 |
| 소각 | 타워 건설·강화 | 영구 해금·시작 골드 상향 |
| 저장 | 없음 | `db::KvStore` WAL |
| 권위 | `GameWorld` | 서버(또는 솔로 호스트) |

골드를 영속시키지 않는 이유는 밸런스가 아니라 **관측 가능성**이다. 매치 안 골드가 다음 매치로 넘어가면 그 매치의 난이도 곡선이 이전 매치의 결과에 좌우되고, 그러면 "이 웨이브 표가 적절한가"라는 질문에 답할 수 없다. 층을 나누면 매치 안 경제는 지금 그대로 검증되고, 그 위의 메타 경제만 새로 검증하면 된다.

## 2. source 와 sink 에 이름을 붙이는 이유

발행처와 소각처를 열거로 고정한다.

```cpp
enum class Source : uint8_t { MatchReward = 0, ClearBonus = 1, EventBonus = 2, Refund = 3, Count = 4 };
enum class Sink   : uint8_t { TowerUnlock = 0, StartGold = 1, BlastCharge = 2, Respec = 3, Count = 4 };
```

총량만 세면 "인플레이션이 0.4다"까지는 말할 수 있지만 **왜** 그런지는 말할 수 없다. 발행이 늘었는지 소각이 줄었는지, 늘어난 발행이 매치 보상인지 이벤트 보너스인지가 같은 수에 뭉쳐 있다. 태그가 그 수를 나눈다.

100매치 시뮬의 내역이 이렇게 나온다.

```
  source 내역: match_reward=31177 clear_bonus=1500 | sink 내역: blast_charge=32576
```

그리고 테스트가 그 합이 총량과 같은지 확인한다 — 태그가 붙지 않은 경로로 통화가 드나들면 이 단언이 먼저 깨진다.

## 3. 불변식 셋 — 그리고 진짜 방어선은 C 다

경제가 지켜야 하는 것은 셋이다.

```
A. Supply() == Sourced() - Sunk()      발행과 소각 외의 경로가 없다
B. Supply() == Σ 모든 계정의 잔액       통화가 계정 밖에 있을 수 없다
C. 모든 잔액 >= 0                       없는 돈을 쓸 수 없다
```

A 와 B 는 산수다. 세 카운터를 같이 갱신하는 한 어지간해서는 깨지지 않는다. **C 가 진짜 방어선이고**, 그 자물쇠는 한 줄이다.

<!-- snippet: include/live/economy.hpp@economy-spend -->
```cpp
bool Spend(uint32_t account, int64_t amount, Sink sink) {
    if (amount <= 0 || amount > kMaxTransaction) return false;
    Entry* entry = Find(account);
    if (!entry || entry->balance < amount) return false;   // ← 불변식 C 의 자물쇠
    entry->balance -= amount;
    entry->sunk    += amount;
    supply_ -= amount;
    sunk_   += amount;
    bySink_[static_cast<size_t>(sink)] += amount;
    return true;
}
```

그 줄을 지우면 어떻게 되는지가 이 편에서 가장 중요한 관측이다. **A 도 B 도 그대로 참이다.** `supply_ -= amount` 와 `sunk_ += amount` 가 짝을 이루므로 `supply == sourced - sunk` 는 유지되고, 계정 잔액도 같은 만큼 줄었으니 합도 맞는다. `InflationRate()` 는 계속 그럴듯한 소수를 낸다. 깨지는 것은 잔액이 음수라는 사실 하나뿐이고, 그것을 묻지 않으면 아무도 모른다.

그래서 검사는 셋을 함께 본다.

<!-- snippet: include/live/economy.hpp@economy-conserved -->
```cpp
bool Conserved(std::string* violation = nullptr) const {
    if (supply_ != sourced_ - sunk_) {
        if (violation) *violation = "A: supply != sourced - sunk";
        return false;
    }
    int64_t total = 0;
    for (const Entry& entry : accounts_) {
        if (entry.balance < 0) {
            if (violation)
                *violation = "C: 계정 " + std::to_string(entry.account) + " 의 잔액이 음수";
            return false;
        }
        if (entry.balance != entry.sourced - entry.sunk) {
            if (violation)
                *violation = "A(계정): " + std::to_string(entry.account);
            return false;
        }
        total += entry.balance;
    }
    if (total != supply_) {
        if (violation) *violation = "B: 잔액 합 != supply";
        return false;
    }
    return true;
}
```

`violation` 문자열이 있는 이유는 진단이다. `Conserved()` 가 `false` 만 돌려주면 "경제가 깨졌다"까지만 알 수 있고, 셋 중 어느 것이 깨졌는지는 다시 재현해야 알 수 있다.

## 4. 계정 순회를 정렬해 두는 이유

`accounts_` 는 `std::unordered_map` 이 아니라 계정 id 오름차순 벡터다. 이유는 두 가지고, 둘 다 8편의 결정성 규약에서 나온다.

**총합의 순서.** `total += entry.balance` 는 지금 정수라 순서와 무관하지만, 인플레율이나 계정별 가중 평균 같은 지표가 부동소수점으로 들어오는 순간 순회 순서가 결과에 새어 들어온다. 그 지표가 대시보드에 실리면 같은 데이터가 실행마다 다른 수를 낸다.

**진단의 재현.** `Conserved()` 가 어느 계정을 먼저 지적하는지가 실행마다 달라지면, 같은 손상 상태를 두 번 조사할 때 다른 계정 번호가 나온다.

`lower_bound` 삽입이라 계정 수에 로그다. 이 게임의 계정 수는 매치당 둘이므로 성능 논의가 아니다 — 순서를 자료구조가 보장하게 만드는 것이 요점이다.

## 5. 영구 강화가 다음 매치의 시작 조건을 바꾼다

스크랩이 사는 이유가 있어야 한다. 소각처가 없는 통화는 발행만 하는 카운터이고, 그것은 경제가 아니다.

두 종류를 둔다.

**시작 골드 티어.** 티어마다 다음 매치의 시작 골드가 40씩 오른다. 상한은 5티어(+200골드). 기본 시작 골드가 420이므로 만렙에서 약 48% 증가다.

**타워 해금.** 타워 종류를 영구히 연다. 한 번 사면 다시 사지지 않는다.

```cpp
static int64_t StartGoldTierCost(uint32_t tier) {
    return 200 + static_cast<int64_t>(tier) * 250;
}
static int StartGoldBonus(uint32_t tier) {
    return static_cast<int>(std::min<uint32_t>(tier, kMaxStartGoldTier)) * 40;
}
static int64_t TowerUnlockCost(uint32_t towerType) {
    return 300 + static_cast<int64_t>(towerType) * 150;
}
```

구매는 `Spend` 를 지나므로 잔액 검사와 태그가 자동으로 붙는다. 잔액이 모자라면 티어가 오르지 않는다 — 테스트가 그 짝을 확인한다(잔액 10인 계정이 `BuyStartGoldTier` 에 실패하고 `StartGoldTier` 가 0으로 남는다).

**배선의 자리는 두 곳이다.** 시작 골드에 티어 보너스를 더하는 곳과, 매치가 끝날 때 스크랩을 발행하는 곳.

```cpp
        balance_ = defense::ApplyEventModifiers(balance_, eventMods_);
        balance_.startGold += live::Economy::StartGoldBonus(wallet_.startGoldTier);
```

이 한 줄이 `world_.Init(map_, waves_, balance_.startGold, ...)` 보다 **먼저** 와야 한다는 것이 배선에서 배운 것이다. 지갑은 진행도 저장소에서 오고, 그 저장소를 여는 코드는 원래 `Init` 의 끝부분에 있었다. 그대로 두면 첫 판은 언제나 티어 0으로 돈다 — 지갑을 읽기 전에 시작 골드가 정해지기 때문이다. 저장소 열기를 `Init` 의 맨 앞으로 옮겼다.

발행 자리는 정확히 하나다.

<!-- snippet: include/game/defense_app.hpp@settle-scrap -->
```cpp
            const int64_t base = static_cast<int64_t>(std::max(score, 0)) / 10 +
                                 (world_.Won() ? 50 : 0);
            const double multiplier =
                scrapEventEnabled_ ? static_cast<double>(eventMods_.scrapMultiplier) : 1.0;
            const int64_t scrap = static_cast<int64_t>(static_cast<double>(base) * multiplier);
            if (scrap > 0 && economy_.Grant(kMetaAccount, scrap,
                                            world_.Won() ? live::Source::ClearBonus
                                                         : live::Source::MatchReward)) {
                scrapEarned_ += scrap;
                wallet_ = economy_.Export(kMetaAccount);
            }
```

발행 자리가 둘이면 `supply == sourced - sunk` 를 어디서 깨뜨렸는지 말할 수 없다. 그리고 이벤트 배율 위에 운영 토글(`scrap_weekend`)이 한 겹 더 있다 — 일정 파일을 고치지 않고도 시즌 보너스를 끌 수 있어야 운영이 성립한다.

**실측.** 새 프로필로 한 판:

```
> mega_defense --selftest --profile ./tierdemo.wal
[meta] scrap=0 tier=0 start_gold=420+0
[meta] settle scrap=+416 balance=416 supply=416 sourced=416 sunk=0 tier=0
[selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181
```

`start_gold=420+0` 의 두 수가 밸런스와 강화다. 티어를 사고 나면 뒤쪽 수가 오른다.

```
> mega_defense --selftest --profile ./tierdemo.wal --buy-tier 5     (스크랩이 모인 뒤)
[meta] buy start_gold_tier=5 cost=1200 balance=174
[meta] scrap=174 tier=5 start_gold=420+200
[meta] settle scrap=+416 balance=590 supply=590 sourced=4090 sunk=3500 tier=5
```

`sourced=4090 sunk=3500 supply=590` — 보존 불변식이 제품 안에서 성립한다. 그리고 `420+200` 이 그 3500스크랩이 산 것의 전부다.

## 6. 가격이 가팔라야 하는 이유

`StartGoldTierCost` 가 티어마다 250씩 오르고 `UpgradeCost` 는 티어마다 2배다. 선형이 아니라 가파른 이유는 sink 의 크기가 발행을 따라가야 하기 때문이다.

매치당 발행이 대략 일정한데 소각처의 가격이 고정이면, 계정이 모든 강화를 다 산 뒤부터 잔액이 단조 증가한다. 그때 인플레율이 1로 수렴하고, 스크랩은 아무 의미 없는 숫자가 된다. 가격이 가팔라지면 후반 계정도 계속 살 것이 남는다.

100매치 시뮬에서 인플레율이 0.003 으로 나온 것은 그 설계가 아니라 시뮬의 소비 성향(매 매치마다 30~430 을 쓰려고 시도) 때문이다. 이 수는 "가격표가 옳다"의 증거가 아니라 **보존 불변식이 유지된 채로 그 수를 잴 수 있게 됐다**는 사실의 증거다. 그 차이가 이 편의 전부다.

## 7. 지갑 — live 가 db 를 인클루드하지 않고 저장되는 법

잔액은 프로필에 남아야 한다. 그런데 `live/economy.hpp` 가 `db/repository.hpp` 를 인클루드하면 라이브 서비스 층이 저장소 층에 묶인다.

묶지 않는 방법이 이미 저장소 안에 있다. `serialize/archive.h` 의 계약은 "필드를 한 번 방문하는 템플릿 함수"뿐이고, `db::Repository<T>` 는 그 계약만 요구한다.

```cpp
struct Wallet {
    uint32_t              schemaVersion = 1;
    uint32_t              account       = 0;
    int64_t               balance       = 0;
    int64_t               sourced       = 0;
    int64_t               sunk          = 0;
    uint32_t              startGoldTier = 0;
    std::vector<uint32_t> unlocks;      // 영구 해금된 타워 종류(오름차순)

    template <class Archive>
    void Serialize(Archive& archive) {
        archive(schemaVersion, account, balance, sourced, sunk, startGoldTier, unlocks);
    }
};
```

`Wallet` 은 `live` 네임스페이스에 있고 어떤 db 헤더도 인클루드하지 않는다. `db::Repository<live::Wallet>` 이 그대로 저장한다.

**기존 `Profile` 에 필드를 더하지 않은 이유.** 6편의 `defense::Profile` 은 이미 저장된 레코드가 있는 구조체다. 필드를 더하면 옛 바이트를 읽을 때 스키마가 어긋나고, 그 실패는 "진행도가 사라졌다"로 나타난다. 별도 키 접두사(`wallet:`)로 새 레코드를 두면 옛 프로필은 손대지 않은 채 새 층만 생긴다.

## 8. 저장 파일도 신뢰 경계 밖이다

`profile.wal` 은 사용자 디스크에 있는 파일이다. 손으로 고칠 수 있고, 디스크 오류로 손상될 수 있다. 그래서 `Import` 는 지갑을 받기 전에 검증한다.

거부하는 것들.

| 지갑 | 거부 사유 |
|---|---|
| `balance=-10` | 음수 필드 |
| `balance=100, sourced=100, sunk=50` | 잔액이 발행-소각과 다르다 |
| `startGoldTier=6` | 시작 골드 티어가 상한 초과 |
| `unlocks={3,3}` | 해금 목록이 오름차순이 아니다 |
| `schemaVersion=99` | 모르는 스키마 버전 |

거부되면 **아무것도 반영하지 않는다.** 손상된 잔액을 절반만 받아들이면 그 뒤의 모든 보존 단언이 거짓말이 된다 — 불변식은 "지금까지 들어온 모든 것이 검증됐다"를 전제로만 의미가 있다.

`unlocks` 의 오름차순 검사가 형식적으로 보이지만 그렇지 않다. `HasUnlock` 이 `binary_search` 라, 정렬돼 있지 않은 목록을 받으면 이미 산 타워를 "안 샀다"고 답하고 두 번 팔린다.

## 9. 영구 강화가 밸런스 검증을 바꾼다

여기가 이 편에서 가장 조심해야 하는 자리다.

지금까지 밸런스 검증은 고정 시작 조건 위에서 이뤄졌다. `start_gold = 420`, `core_hp = 260`, 5웨이브. `mega_defense_selftest` 가 CLEAR 를 내는 것은 그 조건에서다. 영구 강화가 시작 골드를 620까지 올리면 **같은 웨이브 표가 두 개의 다른 난이도**를 갖게 된다.

두 가지를 지킨다.

**셀프테스트는 티어 0에서 돈다.** 게이트가 재는 것은 "이 콘텐츠로 한 판이 끝까지 도는가"이고, 그 판정은 강화 상태에 좌우되면 안 된다. 강화를 반영한 판정은 별도 검사여야 한다.

**강화는 시작 조건만 바꾸고 규칙은 바꾸지 않는다.** 시작 골드와 해금 타워 집합까지다. 적 체력이나 웨이브 구성을 바꾸는 강화는 넣지 않는다 — 그것을 허용하면 권위 시뮬이 계정 상태에 의존하게 되고, 30편이 세우는 "티어가 달라도 권위 해시는 같다"와 같은 종류의 경계가 여기서도 무너진다.

**실측.** 같은 웨이브 표, 같은 콘텐츠, 다른 것은 시작 골드뿐이다.

```
(a) 티어 0 · start_gold=420
[selftest] result=CLEAR ticks=1377 towers=10 wave=6 packet=181 protocol=OK render=OK content=external

(b) 티어 5 · start_gold=620
[selftest] result=CLEAR ticks=1377 towers=12 wave=6 packet=203 protocol=OK render=OK content=external
```

**타워 두 개**가 차이다. 도달 웨이브(6)도 소요 틱(1377)도 같다 — 이 콘텐츠는 티어 0에서도 클리어되고, 강화는 여유를 늘릴 뿐 판정을 뒤집지 않는다. 그것이 §9 가 요구한 성질이다: 게이트가 재는 것은 "이 콘텐츠로 한 판이 끝까지 도는가"이고, 그 판정이 강화 상태에 좌우되면 안 된다.

`packet=181 → 203` 은 타워 두 개가 스냅샷에 실린 결과다(타워 하나가 11바이트).

ctest 에는 두 항목이 나란히 있다. `mega_defense_selftest` 는 티어를 사지 않고, `mega_defense_selftest_meta` 는 `--buy-tier 5` 로 소비 경로까지 밟는다. 후자가 없으면 `Spend` 는 제품에서 한 번도 돌지 않는 코드로 남는다 — 발행만 있고 소각이 없는 경제는 보존 불변식을 검사할 수 없다.

## 10. 변이 하나

**Spend 의 잔액 검사를 지운다.**

```cpp
Entry* entry = Find(account);
if (!entry) entry = &Slot(account);   // ← 잔액 검사를 지운 변이
entry->balance -= amount;
```

```
[test] meta_economy
  FAIL spend: 보존 불변식 위반 — C: 계정 11 의 잔액이 음수
  FAIL grant: 보존 불변식 위반 — C: 계정 11 의 잔액이 음수
  FAIL spend: 보존 불변식 위반 — C: 계정 11 의 잔액이 음수
  ...
  FAIL poor: 보존 불변식 위반 — C: 계정 9 의 잔액이 음수
  왕복: balance=1150 sourced=2400 sunk=1250 tier=1 unlocks=1
[test] meta_economy: 1040 checks, 615 failures
```

615건이 전부 **C** 다. A 도 B 도 한 번도 실패하지 않았다. 그리고 마지막 줄을 보면 지갑 왕복은 여전히 통과한다 — 손상된 값이 그대로 저장되고 그대로 돌아왔기 때문이다.

이것이 "산수가 맞는 것"과 "경제가 맞는 것"이 다른 질문이라는 증거다. 복원 뒤 1040 검사에 실패 0.

## 11. 실행과 관측

```
> build\bin\Release\test_meta_economy.exe
[test] meta_economy
  100매치 x 3명: sourced=32677 sunk=32576 supply=101 inflation=0.003 거부된 지출=119
  source 내역: match_reward=31177 clear_bonus=1500 | sink 내역: blast_charge=32576
  왕복: balance=1150 sourced=2400 sunk=1250 tier=1 unlocks=1
[test] meta_economy: 1040 checks, 0 failures
```

읽을 것 넷.

**`supply=101` 이 `32677 - 32576` 과 정확히 같다.** 불변식 A 를 총량으로 다시 확인한 것이고, 매 거래 뒤 606회 확인한 것과 같은 값이다.

**거부된 지출 119건.** 시뮬이 매번 잔액을 넘는 금액을 시도하도록 짜여 있다. 거부 경로가 한 번도 안 돌면 잔액 검사가 있는지 없는지 이 테스트가 구분하지 못하므로, `CHECK(rejectedSpends > 0)` 가 그 상태를 막는다.

**source 두 갈래의 합이 총 발행과 같다.** `31177 + 1500 = 32677`. 태그 없는 경로로 통화가 들어오면 이 단언이 먼저 깨진다.

**왕복이 `sourced` 와 `sunk` 까지 보존한다.** 잔액만 저장하면 다음 실행에서 불변식 A 를 확인할 수 없다 — 재오픈 뒤 `Import` 가 세 값의 관계를 다시 검증하는 것이 가능한 이유가 셋을 다 적어 두기 때문이다.

**db 왕복은 실제 WAL 을 지난다.**

```cpp
mega::db::KvStore store;
CHECK(store.Open(kWalletPath));
mega::db::Repository<Wallet> wallets(store, "wallet:");
store.Begin();
wallets.Save(saved.account, saved);
CHECK(store.Commit());
store.Close();
```

`Commit()` 뒤 `Close()` 하고 다시 `Open()` 한다. 6편의 WAL 재생 경로를 그대로 지나므로, 프로세스가 죽었다 살아난 것과 같은 조건이다.

## 12. 제품 실측

**(1) 잔액이 판을 넘어 이어진다.**

```
> mega_defense --selftest                 (첫 판)
[meta] scrap=0 tier=0 start_gold=420+0
[meta] settle scrap=+416 balance=416 supply=416 sourced=416 sunk=0 tier=0

> mega_defense --selftest                 (둘째 판, 같은 프로필)
[meta] scrap=416 tier=0 start_gold=420+0
[meta] settle scrap=+416 balance=832 supply=832 sourced=832 sunk=0 tier=0
```

둘째 판의 시작 잔액이 첫 판의 정산 결과다. 그 사이에 프로세스가 죽었다 살아났고, 지갑은 `wallet:1` 레코드로 WAL 에 남아 있었다.

원자성이 여기서 값을 한다. 지갑 저장이 프로필·매치 기록과 **같은 트랜잭션**에 들어간다.

<!-- snippet: include/game/defense_progression.hpp#Record -->
```cpp
        store_.Begin();
        db::Repository<Profile> profiles(store_, "profile:");
        db::Repository<db::MatchResult> matches(store_, "match:");
        profiles.Save(1, profile);
        matches.Save(match.id, match);
        if (wallet) {
            // ...
            live::Wallet copy = *wallet;
            db::Repository<live::Wallet> wallets(store_, "wallet:");
            wallets.Save(1, copy);
        }
        if (!store_.Commit()) return false;
```

따로 쓰면 매치가 끝난 직후 프로세스가 죽었을 때 "판은 세어졌는데 스크랩은 안 들어온" 상태가 남고, 그 상태는 어느 쪽도 복구할 수 없다.

**(2) 전용 서버는 정산하지 않는다.**

이 편을 배선하면서 뒤집힌 진술이 하나 있다. 전용 서버도 매치 종료에 스크랩을 발행하게 만들었다가 **지웠다.** 이유는 셋이다.

- 발행 자리가 둘이 되면 `supply == sourced - sunk` 를 어디서 깨뜨렸는지 말할 수 없다.
- 전용 서버는 플레이어 저장소를 소유하지 않는다. 발행해도 `wallet:` 레코드를 쓸 곳이 없으니 그 잔액은 프로세스가 끝나면 사라진다.
- 무엇보다 그 경로를 실행하는 검사가 없었다. 한 판이 끝나기까지 실시간 20초 이상이 걸리므로 3초짜리 프로세스 검사가 그 자리를 밟지 못하고, 밟지 않는 코드는 무력화 변이가 죽이지 못한다.

그래서 정산 자리는 **프로필을 가진 프로세스 하나**다. 서버가 매치 종료에 하는 일은 이름을 장부에서 지우는 것뿐이다.

```cpp
    if ((world_.Won() || world_.Lost()) && !activeMatch_.empty()) {
        std::printf("[match] end id=%s result=%s version=%u\n", activeMatch_.c_str(),
                    world_.Won() ? "win" : "loss", patch_.Current());
        // ...
        matches_.Forget(activeMatch_);
        activeMatch_.clear();
    }
```

**(3) 합성 부하에서의 보존.**

```
> test_meta_economy.exe
  100매치 x 3명: sourced=32677 sunk=32576 supply=101 inflation=0.003 거부된 지출=119
  source 내역: match_reward=31177 clear_bonus=1500 | sink 내역: blast_charge=32576
  왕복: balance=1150 sourced=2400 sunk=1250 tier=1 unlocks=1
[test] meta_economy: 1040 checks, 0 failures
```

`거부된 지출 119` 가 잔액 검사가 실제로 돌았다는 증거다. 그 수가 0이면 부하가 너무 관대해서 검사를 자극하지 못했다는 뜻이고, 그때 이 파일은 "음수 잔액이 생기지 않는다"를 증명하지 못한다.

CTest 는 111개 대상을 전수 통과한다.

---

## 결론

등재부의 문장 — "매치가 끝나면 사라져 인플레이션이라는 개념이 성립하지 않는다" — 은 이 헤더가 게을러서 도달하지 못한다는 뜻이 아니었다. **그 헤더가 답하는 질문이 이 게임에 없었다**는 뜻이다. 등재를 지우는 방법이 헤더를 억지로 호출하는 것이 아니라 게임에 그 질문을 만드는 것이었던 이유가 그것이다.

측정에서 나온 것 셋.

**보존 불변식은 세 개를 함께 봐야 한다.** 하나만 보면 잔액이 음수인 경제가 통과한다. 그리고 그 상태에서 인플레율은 계속 그럴듯한 수를 낸다 — 지표가 정상이라는 것이 시스템이 정상이라는 뜻이 아니다.

**거부 경로가 실제로 도는지를 단언해야 한다.** `rejectedSpends > 0` 이 없으면 이 테스트는 "잔액이 넉넉한 경우"만 재고, 잔액 검사의 존재 여부에 무감각해진다.

**저장은 잔액만으로 부족하다.** `sourced` 와 `sunk` 를 함께 적어야 재오픈 뒤에도 불변식을 다시 검증할 수 있다.

현재 상태의 한계 넷.

**계정별 sourced/sunk 가 전역 카운터와 이중으로 유지된다.** `Import` 가 그 차이를 전역에 반영하므로 둘이 어긋날 수 없게 돼 있지만, 그 정합을 유지하는 것은 코드이지 자료구조가 아니다.

**`EventBonus` 와 `Refund` source, `Respec` sink 에 아직 호출자가 없다.** 열거에 자리만 있고 27편의 이벤트 수정자가 스크랩 배율을 곱하는 자리는 아직 배선되지 않았다.

**동시 접속 두 명의 지갑이 같은 WAL 을 쓴다.** 전용 서버가 두 플레이어의 정산을 한 트랜잭션에 넣어야 하는데, 지금 `ProgressionStore` 는 계정 1번 하나만 다룬다.

**인플레율이 계정 수에 무감각하다.** `(sourced - sunk) / sourced` 는 전체 발행 대비 잔존이라 계정이 늘어도 같은 수가 나온다. 계정당 평균 잔액이나 상위 백분위 잔액이 실제로 필요한 지표인데 아직 없다.
