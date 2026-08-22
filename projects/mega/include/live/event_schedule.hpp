// event_schedule — 기간 한정 이벤트. 반복되는 매치에 처음으로 시간 축이 생긴다.
//
// 이 게임은 한 판이 5웨이브고, 끝나면 처음부터 다시 시작한다. 그 구조에는
// "언제 플레이했는가"라는 축이 없다 — 오늘의 한 판과 다음 주의 한 판이 완전히
// 같다. 이벤트는 그 축을 만든다. 시각 t 를 물으면 활성 이벤트 집합이 나오고,
// 그 이벤트들이 밸런스에 곱해질 수정자를 들고 있다.
//
// 시각은 **바깥에서 주입**한다. 여기서 시계를 읽으면 같은 리플레이가 실행
// 시각에 따라 다른 게임이 되고, 호스트와 서버가 서로 다른 이벤트를 볼 수 있다.
//
// 경계 규칙은 [start, end) — 시작 포함, 종료 제외다. 반씩 열린 구간이어야
// 연속된 두 이벤트(end == 다음 start)가 겹치지 않는다. `<=` 로 바꾸면 그 한
// 초 동안 두 이벤트가 동시에 활성이 되고, 수정자가 두 번 곱해진다.
//
// 이 헤더는 파일을 읽지 않는다. 텍스트를 받는 파서만 두고, 디스크는 io/ 를
// 쓸 수 있는 상위 계층이 만진다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mega {
namespace live {

// ── 수정자 ─────────────────────────────────────────────────────────
// 활성 이벤트가 밸런스에 얹는 것. 곱하는 값과 더하는 값이 섞여 있으므로
// 합성 순서를 명시적으로 고정한다(우선순위 내림차순, 동률은 id 오름차순).
struct EventModifiers {
    float scrapMultiplier   = 1.0f;   // 매치 종료 정산의 메타 통화 배율
    float rewardMultiplier  = 1.0f;   // 매치 안 골드 보상 배율
    float enemyHpMultiplier = 1.0f;
    int   bonusStartGold    = 0;
    int   blastCostDelta    = 0;      // 음수면 발파가 싸진다
    bool  bossRush          = false;

    bool IsIdentity() const {
        return scrapMultiplier == 1.0f && rewardMultiplier == 1.0f &&
               enemyHpMultiplier == 1.0f && bonusStartGold == 0 &&
               blastCostDelta == 0 && !bossRush;
    }

    // snippet-begin: event-compose
    void Compose(const EventModifiers& next) {
        scrapMultiplier   *= next.scrapMultiplier;
        rewardMultiplier  *= next.rewardMultiplier;
        enemyHpMultiplier *= next.enemyHpMultiplier;
        bonusStartGold    += next.bonusStartGold;
        blastCostDelta    += next.blastCostDelta;
        bossRush           = bossRush || next.bossRush;
    }
    // snippet-end: event-compose

    // 여러 이벤트가 겹쳐도 게임이 무너지지 않게 최종 범위를 자른다. 개별
    // 이벤트의 값은 파서가 이미 검증했지만, 곱은 그 범위 밖으로 나갈 수 있다.
    EventModifiers Clamped() const {
        EventModifiers out = *this;
        out.scrapMultiplier   = std::clamp(out.scrapMultiplier, 0.25f, 8.0f);
        out.rewardMultiplier  = std::clamp(out.rewardMultiplier, 0.25f, 8.0f);
        out.enemyHpMultiplier = std::clamp(out.enemyHpMultiplier, 0.25f, 4.0f);
        out.bonusStartGold    = std::clamp(out.bonusStartGold, -1000, 4000);
        out.blastCostDelta    = std::clamp(out.blastCostDelta, -500, 2000);
        return out;
    }
};

// 필드 순서는 와이어가 아니라 **기존 호출부**가 정한다. `{id, start, end,
// period, priority}` 형태의 집합 초기화가 이미 쓰이고 있어, 새 필드를 그 앞이나
// 사이에 끼우면 조용히 다른 값이 다른 자리에 들어간다. 그래서 새 필드는 뒤에만
// 붙인다.
struct Event {
    uint32_t       id       = 0;
    int64_t        start    = 0;       // 활성 시작(포함)
    int64_t        end      = 0;       // 활성 종료(미포함)
    int64_t        period   = 0;       // 0 = 1회성, >0 = 이 주기마다 반복
    int            priority = 0;       // 높을수록 먼저 합성된다
    std::string    name;
    EventModifiers mods;

    // snippet-begin: event-active
    bool ActiveAt(int64_t t) const {
        if (t < start) return false;
        const int64_t duration = end - start;
        if (duration <= 0) return false;
        if (period <= 0) return t < end;                 // 1회성
        const int64_t phase = (t - start) % period;      // 주기 안에서의 위상
        return phase < duration;
    }
    // snippet-end: event-active
};

inline constexpr size_t kMaxScheduledEvents = 64;

class EventSchedule {
public:
    bool Add(const Event& e) {
        if (events_.size() >= kMaxScheduledEvents) return false;
        for (const Event& existing : events_)
            if (existing.id == e.id) return false;        // id 는 유일하다
        events_.push_back(e);
        return true;
    }
    void Clear() { events_.clear(); }

    // 시각 t 에 활성인 이벤트 id 들(우선순위 내림차순, 동률은 id 오름차순).
    std::vector<uint32_t> ActiveAt(int64_t t) const {
        std::vector<uint32_t> ids;
        for (const Event* e : ActiveOrdered(t)) ids.push_back(e->id);
        return ids;
    }

    // 활성 이벤트의 수정자를 정의된 순서로 합성한 결과.
    // snippet-begin: event-modifiers
    EventModifiers ModifiersAt(int64_t t) const {
        EventModifiers out;
        for (const Event* e : ActiveOrdered(t)) out.Compose(e->mods);
        return out.Clamped();
    }
    // snippet-end: event-modifiers

    bool IsActive(uint32_t id, int64_t t) const {
        for (const Event& e : events_) if (e.id == id) return e.ActiveAt(t);
        return false;
    }
    const Event* Find(uint32_t id) const {
        for (const Event& e : events_) if (e.id == id) return &e;
        return nullptr;
    }
    size_t Count() const { return events_.size(); }
    const std::vector<Event>& All() const { return events_; }

private:
    std::vector<const Event*> ActiveOrdered(int64_t t) const {
        std::vector<const Event*> active;
        for (const Event& e : events_) if (e.ActiveAt(t)) active.push_back(&e);
        std::stable_sort(active.begin(), active.end(),
                         [](const Event* a, const Event* b) {
                             if (a->priority != b->priority) return a->priority > b->priority;
                             return a->id < b->id;
                         });
        return active;
    }

    std::vector<Event> events_;
};

// ── 운영자 파일 ────────────────────────────────────────────────────
//
//     event <id> <이름> <start> <end> <period> <priority> [key=value]...
//
// 키: scrap · reward · enemy_hp · start_gold · blast_cost · boss_rush.
// 원자적 폴백 — 한 줄이라도 거부되면 파일 전체를 버리고 이벤트 없이 돈다.
// 이벤트가 없는 상태는 결함이 아니라 **정상 경로**이므로, 절반만 켜진 시즌보다
// 아무 시즌도 없는 쪽이 언제나 낫다.
inline constexpr size_t kEventFileMaxLines = 512;

inline bool ParseEventSchedule(std::string_view text, EventSchedule& out,
                               std::string* error = nullptr) {
    EventSchedule parsed;
    size_t lineNumber = 0, begin = 0;
    auto fail = [&](const char* why) {
        if (error) *error = "events:" + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    auto number = [](std::string_view token, double& value) {
        if (token.empty() || token.size() > 24) return false;
        size_t cursor = 0;
        double sign = 1.0;
        if (token[0] == '-') { sign = -1.0; cursor = 1; }
        double whole = 0.0, fraction = 0.0, scale = 1.0;
        bool digits = false, dot = false;
        for (; cursor < token.size(); ++cursor) {
            const char c = token[cursor];
            if (c == '.') {
                if (dot) return false;
                dot = true;
                continue;
            }
            if (c < '0' || c > '9') return false;
            digits = true;
            if (dot) { scale *= 10.0; fraction += (c - '0') / scale; }
            else whole = whole * 10.0 + (c - '0');
        }
        if (!digits) return false;
        value = sign * (whole + fraction);
        return true;
    };

    while (begin <= text.size()) {
        size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(begin, end - begin);
        begin = end + 1;
        if (++lineNumber > kEventFileMaxLines) return fail("줄 수가 상한을 넘는다");
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::vector<std::string_view> word;
        size_t cursor = 0;
        while (cursor < line.size()) {
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
            const size_t wordBegin = cursor;
            while (cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t') ++cursor;
            if (cursor > wordBegin) word.push_back(line.substr(wordBegin, cursor - wordBegin));
        }
        if (word.empty() || word[0][0] == '#') continue;
        if (word[0] != "event") return fail("첫 낱말은 event 다");
        if (word.size() < 7) return fail("event <id> <이름> <start> <end> <period> <priority>");

        Event e;
        double value = 0.0;
        if (!number(word[1], value) || value < 1.0 || value > 65535.0) return fail("id 는 1..65535");
        e.id = static_cast<uint32_t>(value);
        if (word[2].size() > 32) return fail("이름이 너무 길다");
        e.name.assign(word[2]);
        if (!number(word[3], value) || value < 0.0) return fail("start 는 0 이상");
        e.start = static_cast<int64_t>(value);
        if (!number(word[4], value) || value < 0.0) return fail("end 는 0 이상");
        e.end = static_cast<int64_t>(value);
        if (!number(word[5], value) || value < 0.0) return fail("period 는 0 이상");
        e.period = static_cast<int64_t>(value);
        if (!number(word[6], value) || value < -100.0 || value > 100.0)
            return fail("priority 는 -100..100");
        e.priority = static_cast<int>(value);

        if (e.end <= e.start) return fail("end 는 start 보다 커야 한다");
        if (e.period > 0 && e.period < e.end - e.start)
            return fail("period 는 지속 시간보다 짧을 수 없다");

        for (size_t i = 7; i < word.size(); ++i) {
            const size_t equals = word[i].find('=');
            if (equals == std::string_view::npos) return fail("수정자는 key=value 다");
            const std::string_view key = word[i].substr(0, equals);
            if (!number(word[i].substr(equals + 1), value)) return fail("수정자 값이 수가 아니다");
            if (key == "scrap") {
                if (value < 0.25 || value > 4.0) return fail("scrap 은 0.25..4");
                e.mods.scrapMultiplier = static_cast<float>(value);
            } else if (key == "reward") {
                if (value < 0.25 || value > 4.0) return fail("reward 는 0.25..4");
                e.mods.rewardMultiplier = static_cast<float>(value);
            } else if (key == "enemy_hp") {
                if (value < 0.5 || value > 2.0) return fail("enemy_hp 는 0.5..2");
                e.mods.enemyHpMultiplier = static_cast<float>(value);
            } else if (key == "start_gold") {
                if (value < -500.0 || value > 2000.0) return fail("start_gold 는 -500..2000");
                e.mods.bonusStartGold = static_cast<int>(value);
            } else if (key == "blast_cost") {
                if (value < -500.0 || value > 1000.0) return fail("blast_cost 는 -500..1000");
                e.mods.blastCostDelta = static_cast<int>(value);
            } else if (key == "boss_rush") {
                e.mods.bossRush = value != 0.0;
            } else {
                return fail("알 수 없는 수정자 키");
            }
        }
        if (!parsed.Add(e)) return fail("id 중복이거나 이벤트 수가 상한을 넘는다");
    }
    out = std::move(parsed);
    if (error) error->clear();
    return true;
}

}  // namespace live
}  // namespace mega
