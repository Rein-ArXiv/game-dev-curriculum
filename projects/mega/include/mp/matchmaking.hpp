// matchmaking — 로비·레이팅·세션 형성. 비슷한 실력끼리 묶어 매치를 만든다.
//
// 큐에 들어온 플레이어를 레이팅 순으로 정렬하고, 레이팅 차이가 window 이내인
// teamSize 명을 한 매치로 묶는다. 매치가 차면 host 를 정해(여기선 가장 낮은 id —
// 실전은 RTT 가 낮은 피어) 세션을 시작한다. 정교한 스킬 매칭·대기시간 완화는
// 정교한 스킬 모델과 운영 정책은 이 최소 세션 형성기의 범위 밖이다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mega {
namespace mp {

struct Player {
    uint32_t id     = 0;
    int      rating = 1000;
};

struct Match {
    std::vector<uint32_t> players;
    uint32_t              hostId = 0;
    int                   minRating = 0, maxRating = 0;
};

class Matchmaker {
public:
    void Init(int teamSize, int ratingWindow) {
        teamSize_ = teamSize < 1 ? 1 : teamSize;
        window_   = ratingWindow;
    }

    void Enqueue(const Player& p) { queue_.push_back(p); }
    size_t QueueSize() const { return queue_.size(); }

    // 형성 가능한 매치를 모두 만든다. 매칭된 플레이어는 큐에서 제거.
    std::vector<Match> FormMatches() {
        std::vector<Match> out;
        std::sort(queue_.begin(), queue_.end(),
                  [](const Player& a, const Player& b) { return a.rating < b.rating; });

        std::vector<Player> remain;
        size_t i = 0;
        while (i < queue_.size()) {
            // i 부터 teamSize 명이 window 안에 드는지 확인.
            if (i + teamSize_ <= queue_.size() &&
                (queue_[i + teamSize_ - 1].rating - queue_[i].rating) <= window_) {
                Match m;
                m.minRating = queue_[i].rating;
                m.maxRating = queue_[i + teamSize_ - 1].rating;
                m.hostId = queue_[i].id;                 // 가장 낮은 레이팅... id 로 host
                for (int k = 0; k < teamSize_; ++k) {
                    m.players.push_back(queue_[i + k].id);
                    if (queue_[i + k].id < m.hostId) m.hostId = queue_[i + k].id;
                }
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

private:
    int                 teamSize_ = 2;
    int                 window_   = 200;
    std::vector<Player> queue_;
};

}  // namespace mp
}  // namespace mega
