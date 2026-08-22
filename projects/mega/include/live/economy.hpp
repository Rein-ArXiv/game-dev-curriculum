// economy — 매치를 넘어 사는 통화 '스크랩'. 보존 불변식이 관측 가능해진다.
//
// 이 게임의 골드는 매치 안에서만 산다. 한 판이 끝나면 전부 사라지므로 "통화가
// 쌓인다"는 문장 자체가 성립하지 않고, 그래서 인플레이션도 경제 균형도 관측할
// 대상이 없었다. 스크랩은 그 위에 얹는 두 번째 층이다 — 매치 종료 정산이
// 발행(source)이고, 매치 밖 영구 강화가 소각(sink)이며, 잔액은 프로필에 남는다.
//
// 이 헤더가 지키는 세 불변식.
//
//   A. `Supply() == Sourced() - Sunk()`      — 발행과 소각 외의 경로가 없다.
//   B. `Supply() == Σ 모든 계정의 잔액`      — 통화가 계정 밖에 있을 수 없다.
//   C. 모든 잔액 ≥ 0                          — 없는 돈을 쓸 수 없다.
//
// A 와 B 는 산수라 어지간해서는 깨지지 않는다. **C 가 진짜 방어선**이다.
// Spend 의 잔액 검사를 지우면 A 도 B 도 그대로 참인 채로 잔액만 음수가 되고,
// 그 상태에서 InflationRate 는 계속 그럴듯한 수를 낸다 — 셋을 함께 단언해야
// 하는 이유가 그것이다.
//
// 계정 순회는 id 오름차순이다. unordered_map 순회로 총합을 내면 부동소수점이
// 섞이는 순간 실행마다 다른 값이 나온다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace live {

// 발행처와 소각처는 열거로 고정한다. "어디서 들어와 어디로 나갔는가"를 못 적는
// 경제 지표는 총량만 알려 주고 원인을 알려 주지 못한다.
enum class Source : uint8_t { MatchReward = 0, ClearBonus = 1, EventBonus = 2, Refund = 3, Count = 4 };
enum class Sink   : uint8_t { TowerUnlock = 0, StartGold = 1, BlastCharge = 2, Respec = 3, Count = 4 };

inline const char* SourceName(Source s) {
    switch (s) {
        case Source::MatchReward: return "match_reward";
        case Source::ClearBonus:  return "clear_bonus";
        case Source::EventBonus:  return "event_bonus";
        case Source::Refund:      return "refund";
        default:                  return "unknown";
    }
}
inline const char* SinkName(Sink s) {
    switch (s) {
        case Sink::TowerUnlock: return "tower_unlock";
        case Sink::StartGold:   return "start_gold";
        case Sink::BlastCharge: return "blast_charge";
        case Sink::Respec:      return "respec";
        default:                return "unknown";
    }
}

// 한 번의 거래 상한. 상한이 없으면 오버플로가 불변식을 조용히 깬다.
inline constexpr int64_t kMaxTransaction = 1'000'000'000ll;
inline constexpr uint32_t kMaxStartGoldTier = 5;

// 계정 하나의 영속 상태. db::Repository<Wallet> 이 그대로 저장한다 — live/ 가
// db/ 를 인클루드하지 않고도 왕복이 되는 것은 직렬화가 필드 방문 하나뿐이기
// 때문이다(serialize/archive.h).
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

class Economy {
public:
    // ── 발행 ───────────────────────────────────────────────────────
    bool Grant(uint32_t account, int64_t amount, Source source) {
        if (amount <= 0 || amount > kMaxTransaction) return false;
        Entry& entry = Slot(account);
        if (entry.balance > kMaxTransaction * 1000 - amount) return false;
        entry.balance += amount;
        entry.sourced += amount;
        supply_  += amount;
        sourced_ += amount;
        bySource_[static_cast<size_t>(source)] += amount;
        return true;
    }
    // 출처를 적지 않은 옛 호출 형태. 매치 보상으로 친다.
    bool Grant(uint32_t account, int64_t amount) {
        return Grant(account, amount, Source::MatchReward);
    }

    // ── 소각 ───────────────────────────────────────────────────────
    // snippet-begin: economy-spend
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
    // snippet-end: economy-spend
    bool Spend(uint32_t account, int64_t amount) {
        return Spend(account, amount, Sink::TowerUnlock);
    }

    // ── 조회 ───────────────────────────────────────────────────────
    int64_t Balance(uint32_t account) const {
        const Entry* entry = Find(account);
        return entry ? entry->balance : 0;
    }
    int64_t Supply()  const { return supply_; }
    int64_t Sourced() const { return sourced_; }
    int64_t Sunk()    const { return sunk_; }
    int64_t SourcedBy(Source s) const { return bySource_[static_cast<size_t>(s)]; }
    int64_t SunkBy(Sink s)      const { return bySink_[static_cast<size_t>(s)]; }
    size_t  AccountCount() const { return accounts_.size(); }

    // 발행분 중 회수되지 않고 쌓인 비율. 0 = 완전 흡수, 1 = 전혀 회수 안 됨.
    double InflationRate() const {
        if (sourced_ == 0) return 0.0;
        return static_cast<double>(sourced_ - sunk_) / static_cast<double>(sourced_);
    }

    // ── 보존 불변식 ────────────────────────────────────────────────
    // snippet-begin: economy-conserved
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
    // snippet-end: economy-conserved

    // ── 영구 강화 ──────────────────────────────────────────────────
    // 다음 매치의 시작 조건을 바꾸는 두 종류. 가격은 티어마다 가팔라져 sink 를
    // 키운다 — 그러지 않으면 후반 계정의 잔액이 단조 증가만 한다.
    static int64_t UpgradeCost(int tier) {           // 옛 이름(매치 안 강화 가격표)
        int64_t cost = 50;
        for (int i = 0; i < tier; ++i) cost *= 2;
        return cost;
    }
    static int64_t StartGoldTierCost(uint32_t tier) {
        return 200 + static_cast<int64_t>(tier) * 250;
    }
    static int StartGoldBonus(uint32_t tier) {
        return static_cast<int>(std::min<uint32_t>(tier, kMaxStartGoldTier)) * 40;
    }
    static int64_t TowerUnlockCost(uint32_t towerType) {
        return 300 + static_cast<int64_t>(towerType) * 150;
    }

    bool BuyStartGoldTier(uint32_t account) {
        Entry* entry = Find(account);
        if (!entry || entry->startGoldTier >= kMaxStartGoldTier) return false;
        if (!Spend(account, StartGoldTierCost(entry->startGoldTier), Sink::StartGold)) return false;
        ++Slot(account).startGoldTier;
        return true;
    }
    bool UnlockTower(uint32_t account, uint32_t towerType) {
        if (HasUnlock(account, towerType)) return false;
        if (!Spend(account, TowerUnlockCost(towerType), Sink::TowerUnlock)) return false;
        Entry& entry = Slot(account);
        entry.unlocks.insert(
            std::lower_bound(entry.unlocks.begin(), entry.unlocks.end(), towerType), towerType);
        return true;
    }
    bool HasUnlock(uint32_t account, uint32_t towerType) const {
        const Entry* entry = Find(account);
        if (!entry) return false;
        return std::binary_search(entry->unlocks.begin(), entry->unlocks.end(), towerType);
    }
    uint32_t StartGoldTier(uint32_t account) const {
        const Entry* entry = Find(account);
        return entry ? entry->startGoldTier : 0;
    }

    // ── 영속 왕복 ──────────────────────────────────────────────────
    Wallet Export(uint32_t account) const {
        Wallet wallet;
        wallet.account = account;
        if (const Entry* entry = Find(account)) {
            wallet.balance = entry->balance;
            wallet.sourced = entry->sourced;
            wallet.sunk = entry->sunk;
            wallet.startGoldTier = entry->startGoldTier;
            wallet.unlocks = entry->unlocks;
        }
        return wallet;
    }

    // 저장소에서 돌아온 지갑을 받는다. 저장 파일은 신뢰 경계 밖이므로 불변식을
    // 어기는 지갑은 통째로 거부한다 — 손상된 잔액을 절반만 받아들이면 그 뒤의
    // 모든 보존 단언이 거짓말이 된다.
    bool Import(const Wallet& wallet, std::string* error = nullptr) {
        auto fail = [&](const char* why) {
            if (error) *error = std::string("wallet: ") + why;
            return false;
        };
        if (wallet.schemaVersion != 1) return fail("모르는 스키마 버전");
        if (wallet.balance < 0 || wallet.sourced < 0 || wallet.sunk < 0) return fail("음수 필드");
        if (wallet.balance != wallet.sourced - wallet.sunk) return fail("잔액이 발행-소각과 다르다");
        if (wallet.startGoldTier > kMaxStartGoldTier) return fail("시작 골드 티어가 상한 초과");
        for (size_t i = 1; i < wallet.unlocks.size(); ++i)
            if (wallet.unlocks[i] <= wallet.unlocks[i - 1]) return fail("해금 목록이 오름차순이 아니다");
        Entry& entry = Slot(wallet.account);
        supply_  += wallet.balance - entry.balance;
        sourced_ += wallet.sourced - entry.sourced;
        sunk_    += wallet.sunk - entry.sunk;
        entry.balance = wallet.balance;
        entry.sourced = wallet.sourced;
        entry.sunk = wallet.sunk;
        entry.startGoldTier = wallet.startGoldTier;
        entry.unlocks = wallet.unlocks;
        if (error) error->clear();
        return true;
    }

private:
    struct Entry {
        uint32_t              account = 0;
        int64_t               balance = 0;
        int64_t               sourced = 0;
        int64_t               sunk    = 0;
        uint32_t              startGoldTier = 0;
        std::vector<uint32_t> unlocks;
    };

    Entry& Slot(uint32_t account) {
        auto slot = std::lower_bound(
            accounts_.begin(), accounts_.end(), account,
            [](const Entry& entry, uint32_t key) { return entry.account < key; });
        if (slot != accounts_.end() && slot->account == account) return *slot;
        Entry fresh;
        fresh.account = account;
        return *accounts_.insert(slot, std::move(fresh));
    }
    const Entry* Find(uint32_t account) const {
        auto slot = std::lower_bound(
            accounts_.begin(), accounts_.end(), account,
            [](const Entry& entry, uint32_t key) { return entry.account < key; });
        if (slot == accounts_.end() || slot->account != account) return nullptr;
        return &*slot;
    }
    Entry* Find(uint32_t account) {
        return const_cast<Entry*>(static_cast<const Economy*>(this)->Find(account));
    }

    std::vector<Entry> accounts_;      // 계정 id 오름차순
    int64_t supply_  = 0;
    int64_t sourced_ = 0;
    int64_t sunk_    = 0;
    int64_t bySource_[static_cast<size_t>(Source::Count)]{};
    int64_t bySink_[static_cast<size_t>(Sink::Count)]{};
};

}  // namespace live
}  // namespace mega
