// matchmaking — 큐·레이팅·매치 형성. 비슷한 실력끼리 묶어 매치를 만든다.
//
// 큐에 들어온 플레이어를 레이팅 순으로 정렬하고, 레이팅 차이가 window 이내인
// teamSize 명을 한 매치로 묶는다. 매치가 차면 host 를 정해(여기선 가장 낮은 id —
// 실전은 RTT 가 낮은 피어) 세션을 시작한다. 정교한 스킬 모델과 운영 정책은 이
// 최소 세션 형성기의 범위 밖이다.
//
// ── 매치에 이름이 생긴다 ───────────────────────────────────────────
// 지금까지 전용 서버의 `activeMatch_` 는 **처음 도착한 입장 토큰이 말한 matchId**
// 였다. 매치를 만드는 주체가 없으니 그럴 수밖에 없었고, 그 상태에서는 "큐"라는
// 개념이 성립하지 않는다 — 서버는 자기가 어떤 매치를 여는지 모르고, 두 번째
// 토큰이 같은 matchId 를 말하는지만 확인할 수 있었다.
//
// FormMatches 가 그 주체가 된다. 형성된 매치는 결정적인 이름(MatchId)과 정원과
// 명단을 갖고, MatchRegistry 가 그것을 기억한다. 입장 승인은 이제 "먼저 온 쪽이
// 정한 이름"이 아니라 **형성된 매치의 명단**에 대고 물을 수 있다.
//
// ── 정렬은 전순서여야 한다 ─────────────────────────────────────────
// `a.rating < b.rating` 하나만으로 비교하면 레이팅이 같은 플레이어들 사이의
// 순서가 정해지지 않는다. std::sort 는 안정 정렬이 아니므로 같은 큐가 실행마다
// 다른 짝을 낼 수 있고, 그러면 "합성 500명에서 레이팅 폭이 window 이하"라는
// 단언은 통과해도 **같은 입력이 같은 매치를 낸다**는 성질은 성립하지 않는다.
// id 를 두 번째 키로 두어 전순서를 만든다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mega {
namespace mp {

struct Player {
    uint32_t id     = 0;
    int      rating = 1000;
    uint64_t enqueuedAt = 0;   // 큐에 들어온 시각(호출자 단위. 대기 길이 측정용)
};

struct Match {
    std::string           id;              // 입장 토큰의 audience 가 되는 이름
    std::vector<uint32_t> players;
    uint32_t              hostId = 0;
    int                   minRating = 0, maxRating = 0;
    uint64_t              maxWait = 0;     // 이 매치 멤버 중 가장 오래 기다린 시간
};

// 결정적 매치 이름. 같은 명단 + 같은 일련번호는 언제나 같은 문자열을 낸다 —
// 이름이 실행마다 달라지면 토큰과 서버가 맞춰 볼 기준이 사라진다.
inline std::string FormatMatchId(uint32_t serial, const std::vector<uint32_t>& players) {
    uint32_t hash = 2166136261u;
    for (uint32_t id : players) {
        for (int shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((id >> shift) & 0xFFu)) * 16777619u;
    }
    static const char* kHex = "0123456789abcdef";
    std::string out = "m-";
    for (int shift = 12; shift >= 0; shift -= 4) out += kHex[(serial >> shift) & 0xF];
    out += '-';
    for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(hash >> shift) & 0xF];
    return out;
}

class Matchmaker {
public:
    void Init(int teamSize, int ratingWindow) {
        teamSize_ = teamSize < 1 ? 1 : teamSize;
        window_   = ratingWindow;
        queue_.clear();
        serial_ = 0;
    }

    // 이미 큐에 있는 id 는 거절한다. 같은 사람이 두 자리를 차지하면 두 매치에
    // 배정될 수 있고, 그 순간 정원과 명단이 동시에 거짓이 된다.
    bool Enqueue(const Player& player, uint64_t now = 0) {
        for (const Player& queued : queue_)
            if (queued.id == player.id) return false;
        Player copy = player;
        if (copy.enqueuedAt == 0) copy.enqueuedAt = now;
        queue_.push_back(copy);
        return true;
    }

    size_t QueueSize() const { return queue_.size(); }
    int    TeamSize() const { return teamSize_; }
    int    Window() const { return window_; }

    // 큐에서 가장 오래 기다린 사람의 대기 길이. 매칭 품질의 다른 한 축이다 —
    // window 를 좁히면 레이팅 폭은 줄지만 이 값이 자란다.
    uint64_t LongestWait(uint64_t now) const {
        uint64_t longest = 0;
        for (const Player& queued : queue_)
            if (now > queued.enqueuedAt) longest = std::max(longest, now - queued.enqueuedAt);
        return longest;
    }

    // 형성 가능한 매치를 모두 만든다. 매칭된 플레이어는 큐에서 제거.
    // snippet-begin: form-matches
    std::vector<Match> FormMatches(uint64_t now = 0) {
        std::vector<Match> out;
        std::sort(queue_.begin(), queue_.end(), [](const Player& a, const Player& b) {
            if (a.rating != b.rating) return a.rating < b.rating;
            return a.id < b.id;                     // 전순서: 동점의 순서를 고정한다
        });

        std::vector<Player> remain;
        size_t i = 0;
        while (i < queue_.size()) {
            // i 부터 teamSize 명이 window 안에 드는지 확인.
            if (i + teamSize_ <= queue_.size() &&
                (queue_[i + teamSize_ - 1].rating - queue_[i].rating) <= window_) {
                Match m;
                m.minRating = queue_[i].rating;
                m.maxRating = queue_[i + teamSize_ - 1].rating;
                m.hostId = queue_[i].id;
                for (int k = 0; k < teamSize_; ++k) {
                    const Player& member = queue_[i + k];
                    m.players.push_back(member.id);
                    if (member.id < m.hostId) m.hostId = member.id;
                    if (now > member.enqueuedAt) m.maxWait = std::max(m.maxWait, now - member.enqueuedAt);
                }
                m.id = FormatMatchId(++serial_, m.players);
                out.push_back(std::move(m));
                i += teamSize_;
            } else {
                remain.push_back(queue_[i]);              // 못 묶인 플레이어는 큐 유지
                ++i;
            }
        }
        queue_.swap(remain);
        return out;
    }
    // snippet-end: form-matches

private:
    int                 teamSize_ = 2;
    int                 window_   = 200;
    uint32_t            serial_   = 0;
    std::vector<Player> queue_;
};

// ── 형성된 매치의 장부 ─────────────────────────────────────────────
// 전용 서버가 입장 토큰을 검증한 뒤 "이 matchId 가 실제로 형성된 매치인가,
// 이 playerId 가 그 명단에 있는가"를 물을 자리. 이 장부가 없으면 서명만 유효한
// 토큰이 아무 이름의 매치나 열 수 있다.
//
// 정렬 컨테이너를 쓴다. 순회 순서가 삽입 순서에 좌우되면 같은 장부가 실행마다
// 다른 순서로 읽히고, 그 위에서 세는 어떤 수치도 재현되지 않는다.
class MatchRegistry {
public:
    bool Register(const Match& match) {
        if (match.id.empty() || match.players.empty()) return false;
        if (matches_.count(match.id)) return false;
        for (uint32_t player : match.players)
            if (assigned_.count(player)) return false;   // 이미 다른 매치의 멤버다
        matches_[match.id] = match.players;
        for (uint32_t player : match.players) assigned_[player] = match.id;
        return true;
    }

    bool Known(std::string_view matchId) const {
        return matches_.find(std::string(matchId)) != matches_.end();
    }
    // 이 매치의 명단에 이 플레이어가 있는가. 둘 중 하나라도 틀리면 거부다.
    bool Allows(std::string_view matchId, uint32_t playerId) const {
        auto it = matches_.find(std::string(matchId));
        if (it == matches_.end()) return false;
        return std::find(it->second.begin(), it->second.end(), playerId) != it->second.end();
    }
    size_t Capacity(std::string_view matchId) const {
        auto it = matches_.find(std::string(matchId));
        return it == matches_.end() ? 0 : it->second.size();
    }
    size_t Count() const { return matches_.size(); }
    size_t AssignedPlayers() const { return assigned_.size(); }

    void Forget(std::string_view matchId) {
        auto it = matches_.find(std::string(matchId));
        if (it == matches_.end()) return;
        for (uint32_t player : it->second) assigned_.erase(player);
        matches_.erase(it);
    }

private:
    std::map<std::string, std::vector<uint32_t>> matches_;
    std::map<uint32_t, std::string>              assigned_;
};

// 레이팅은 프로필의 최고 점수에서 나온다. 별도의 레이팅 시스템을 두지 않는 이유는
// 그것을 움직일 신호가 아직 없기 때문이다 — 협동 PvE 라 승패가 상대적이지 않고,
// 지금 저장되는 유일한 실력 대리 지표가 bestScore 다. 그 사실을 이름으로 적어 둔다.
//
// 로그 스케일로 접는 이유: 점수는 웨이브 수에 대해 대략 지수로 자라므로 선형
// 그대로 쓰면 상위 몇 명이 창을 통째로 차지한다. 계단이 배로 넓어지는 대신
// 계단 값은 100씩 고르게 오른다.
inline int RatingFromBestScore(uint32_t bestScore) {
    int      rating    = 1000;   // 아직 한 판도 끝내지 못한 사람의 자리
    uint32_t threshold = 100;
    while (bestScore >= threshold && rating < 3000) {
        rating += 100;
        threshold *= 2;
    }
    return rating;
}

}  // namespace mp
}  // namespace mega
