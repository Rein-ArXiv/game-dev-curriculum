// event_schedule — 시간 윈도우 기반 한정 이벤트. 운영자가 데이터로 켠다.
//
// 각 이벤트는 [start, end) 구간과 선택적 반복 주기를 갖는다. 질의 시각 t 에서
// 활성 이벤트 집합을 우선순위 순으로 돌려준다. 코드 재배포 없이 시각만 바꿔
// 시즌·주말 이벤트를 여닫는다(live_service 이벤트 챕터). 시각은 외부 주입이라
// 결정적이다(유닉스 초 또는 게임 틱).

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mega {
namespace live {

struct Event {
    uint32_t id       = 0;
    int64_t  start    = 0;       // 활성 시작(포함)
    int64_t  end      = 0;       // 활성 종료(미포함)
    int64_t  period   = 0;       // 0 = 1회성, >0 = 이 주기마다 반복
    int      priority = 0;       // 높을수록 먼저

    // 시각 t 에 활성인가(반복 고려).
    bool ActiveAt(int64_t t) const {
        if (t < start) return false;
        int64_t dur = end - start;
        if (dur <= 0) return false;
        if (period <= 0) return t < end;                 // 1회성
        int64_t phase = (t - start) % period;            // 주기 내 위상
        return phase < dur;                              // 윈도우 안
    }
};

class EventSchedule {
public:
    void Add(const Event& e) { events_.push_back(e); }

    // 시각 t 에 활성인 이벤트 id 들(우선순위 내림차순, 동률은 id 오름차순).
    std::vector<uint32_t> ActiveAt(int64_t t) const {
        std::vector<const Event*> active;
        for (const Event& e : events_) if (e.ActiveAt(t)) active.push_back(&e);
        std::sort(active.begin(), active.end(), [](const Event* a, const Event* b) {
            if (a->priority != b->priority) return a->priority > b->priority;
            return a->id < b->id;
        });
        std::vector<uint32_t> ids;
        ids.reserve(active.size());
        for (const Event* e : active) ids.push_back(e->id);
        return ids;
    }

    bool IsActive(uint32_t id, int64_t t) const {
        for (const Event& e : events_) if (e.id == id) return e.ActiveAt(t);
        return false;
    }
    size_t Count() const { return events_.size(); }

private:
    std::vector<Event> events_;
};

}  // namespace live
}  // namespace mega
