// progression -- transactional local profile and match history.
//
// games/defense/progression.hpp 에서 옮겨 왔다. OS API 를 직접 부르지는 않지만
// (db/ 가 대신 부른다) 제품과 테스트가 같은 타깃에서 같은 코드를 보게 하려면
// mega_core 안에 있어야 한다. games/ 를 인클루드 루트로 열어 두면 "테스트만
// 보는 코드"가 다시 생길 수 있고, 그것이 감사가 지적한 결함의 형태였다.
// 저장 경로 결정(%LOCALAPPDATA% 등)은 platform::UserDataDirectory 의 몫이다.

#pragma once

#include "db/records.hpp"
#include "db/repository.hpp"
#include "live/economy.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace mega {
namespace defense {

struct Profile {
    uint32_t schemaVersion = 1;
    uint32_t totalMatches = 0;
    uint32_t clears = 0;
    uint32_t bestWave = 0;
    uint32_t bestScore = 0;

    template <class Archive>
    void Serialize(Archive& archive) {
        archive(schemaVersion, totalMatches, clears, bestWave, bestScore);
    }
};

class ProgressionStore {
public:
    bool Open(const std::string& path) {
        Close();
        open_ = store_.Open(path.c_str());
        return open_;
    }

    void Close() {
        if (open_) store_.Close();
        open_ = false;
    }

    bool Load(Profile& profile) {
        if (!open_) return false;
        db::Repository<Profile> profiles(store_, "profile:");
        return profiles.Load(1, profile);
    }

    // 메타 통화 지갑. 프로필과 **같은 저장소**에 있고 아래에서 **같은 트랜잭션**에
    // 커밋된다. 따로 쓰면 매치가 끝난 직후 프로세스가 죽었을 때 "판은 세어졌는데
    // 스크랩은 안 들어온" 상태가 남고, 그 상태는 어느 쪽도 복구할 수 없다.
    bool LoadWallet(live::Wallet& wallet) {
        if (!open_) return false;
        db::Repository<live::Wallet> wallets(store_, "wallet:");
        return wallets.Load(1, wallet);
    }

    bool Record(uint32_t wave, uint32_t score, bool cleared, uint64_t endedAt,
                Profile& updated, const live::Wallet* wallet = nullptr) {
        if (!open_) return false;
        Profile profile;
        Load(profile);
        ++profile.totalMatches;
        if (cleared) ++profile.clears;
        profile.bestWave = std::max(profile.bestWave, wave);
        profile.bestScore = std::max(profile.bestScore, score);

        db::MatchResult match;
        match.id = profile.totalMatches;
        match.hostAccount = 1;
        match.waveReached = wave;
        match.score = score;
        match.cleared = cleared ? 1 : 0;
        match.endedAt = endedAt;

        store_.Begin();
        db::Repository<Profile> profiles(store_, "profile:");
        db::Repository<db::MatchResult> matches(store_, "match:");
        profiles.Save(1, profile);
        matches.Save(match.id, match);
        if (wallet) {
            // Repository::Save 는 방문자를 쓰므로 비const 참조를 요구한다.
            // 사본을 뜨는 것이 호출자의 지갑을 건드리지 않는 유일한 방법이다.
            live::Wallet copy = *wallet;
            db::Repository<live::Wallet> wallets(store_, "wallet:");
            wallets.Save(1, copy);
        }
        if (!store_.Commit()) return false;
        updated = profile;
        return true;
    }

    ~ProgressionStore() { Close(); }

private:
    db::KvStore store_;
    bool open_ = false;
};

}  // namespace defense
}  // namespace mega
