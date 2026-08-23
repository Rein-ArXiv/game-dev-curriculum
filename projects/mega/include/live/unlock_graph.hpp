// unlock_graph — 영구 해금의 선행 관계. 저작 파일 한 장이 상점의 순서를 정한다.
//
// 지금까지 영구 강화는 가격표 하나짜리 목록이었다. 값만 있으면 아무 순서로나
// 해금됐고, "이것을 먼저 열어야 저것이 열린다"는 개념이 없었다. 그 상태에서
// 상점은 그냥 나열이고, 변조된 프로필이 잎 노드만 들고 있어도 통과한다.
//
// `assets/unlocks.txt` 가 그 관계를 저작한다.
//
//     # 주석과 빈 줄은 무시한다
//     unlock 0
//     unlock 1 requires=0
//     unlock 3 requires=1,2
//
// ── 파일이 통째로 거부되는 경우 ───────────────────────────────────
// 이 저장소의 다른 저작 파일(이벤트 창·기능 플래그)과 같은 규율이다: 한 줄이라도
// 거부되면 파일 전체를 폐기하고 기본 동작(선행 없음)으로 돈다. 절반만 적용된
// 선행 그래프는 상점에 영원히 회색인 항목을 남기고, 그것은 이벤트가 절반만 켜진
// 시즌과 같은 종류의 상태다.
//
// 거부 사유는 다섯이다 — 문법, 범위 밖 종류, 중복 선언, 선언되지 않은 선행,
// 그리고 **순환**. 순환은 "순환이 있다"로 끝내지 않는다. 어느 항목들이 서로를
// 물고 있는지를 강결합 요소로 지목해야 저작자가 고칠 수 있다.
//
// ── 순서가 결정적이어야 하는 이유 ─────────────────────────────────
// 진입 차수가 동시에 0 이 되는 항목이 여럿일 때 위상 정렬은 아무 것이나 먼저
// 꺼내도 옳다. 옳지만 재현되지 않는다. 파일의 줄 순서로 꺼내면 줄 하나를 옮겨
// 적은 사람이 상점 순서를 바꾸게 되고, 스크린샷도 검사도 그 순간 갈린다.
// 그래서 꺼내는 자리를 종류 번호 오름차순 최소 힙으로 고정한다.

#pragma once

#include "algo/topo_sort.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace live {

// 종류 번호의 상한. 파일이 신뢰 경계 밖이므로 정점 수에 예산을 건다.
inline constexpr uint32_t kMaxUnlockType = 63;
inline constexpr size_t   kMaxUnlockLines = 256;

class UnlockGraph {
public:
    struct Requirement {
        uint32_t              type = 0;
        std::vector<uint32_t> prerequisites;   // 오름차순
    };

    bool Empty() const { return nodes_.empty(); }
    size_t Size() const { return nodes_.size(); }
    // 상점 표시 순서. 선행이 언제나 앞에 온다.
    const std::vector<uint32_t>& ShopOrder() const { return order_; }
    bool Declared(uint32_t type) const {
        return std::binary_search(declared_.begin(), declared_.end(), type);
    }
    const std::vector<uint32_t>& Prerequisites(uint32_t type) const {
        for (const Requirement& node : nodes_)
            if (node.type == type) return node.prerequisites;
        return empty_;
    }

    // snippet-begin: unlock-satisfied
    // 이 종류를 지금 살 수 있는가. 선행이 전부 이미 해금돼 있어야 한다.
    // 그래프가 비어 있으면(파일 없음) 언제나 참 — 선행이라는 개념이 없던
    // 시절의 동작이 그대로 기본값이다.
    bool Satisfied(uint32_t type, const std::vector<uint32_t>& unlocks) const {
        if (nodes_.empty()) return true;
        if (!Declared(type)) return false;   // 저작되지 않은 종류는 상점에 없다
        for (uint32_t need : Prerequisites(type))
            if (!std::binary_search(unlocks.begin(), unlocks.end(), need)) return false;
        return true;
    }

    // 해금 집합이 **선행 폐포**인가. 저장 파일에서 온 지갑을 받을 때 묻는다 —
    // 잎 노드만 들고 있는 지갑은 이 게임에서 도달할 수 없는 상태이므로 손상이거나
    // 변조다. offender 에는 폐포를 깨는 첫 종류가 담긴다.
    bool ClosedUnder(const std::vector<uint32_t>& unlocks, uint32_t* offender = nullptr) const {
        if (nodes_.empty()) return true;
        for (uint32_t owned : unlocks) {
            if (!Declared(owned)) {
                if (offender) *offender = owned;
                return false;
            }
            for (uint32_t need : Prerequisites(owned)) {
                if (!std::binary_search(unlocks.begin(), unlocks.end(), need)) {
                    if (offender) *offender = owned;
                    return false;
                }
            }
        }
        return true;
    }
    // snippet-end: unlock-satisfied

    // snippet-begin: unlock-parse
    // 파일 전체를 받거나 통째로 버린다. 실패하면 이 객체는 비어 있는 상태로
    // 남으므로, 호출자가 반환값을 무시해도 "선행 없음"이라는 안전한 기본으로 돈다.
    bool Parse(const std::string& text, std::string* error = nullptr) {
        nodes_.clear();
        declared_.clear();
        order_.clear();
        auto fail = [&](const std::string& why) {
            nodes_.clear();
            declared_.clear();
            order_.clear();
            if (error) *error = why;
            return false;
        };
        if (error) error->clear();

        std::vector<Requirement> parsed;
        std::vector<std::vector<uint32_t>> rawNeeds;
        size_t lineNumber = 0, lines = 0;
        size_t begin = 0;
        while (begin <= text.size()) {
            size_t end = text.find('\n', begin);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(begin, end - begin);
            begin = end + 1;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                if (end == text.size()) break;
                continue;
            }
            if (++lines > kMaxUnlockLines) return fail("unlocks: 줄 수가 상한을 넘는다");

            std::vector<std::string> fields = Split(trimmed);
            if (fields.size() < 2 || fields.size() > 3 || fields[0] != "unlock")
                return fail("unlocks:" + std::to_string(lineNumber) +
                            ": 형식은 `unlock <종류> [requires=a,b]` 다");
            uint32_t type = 0;
            if (!ParseType(fields[1], type))
                return fail("unlocks:" + std::to_string(lineNumber) + ": 종류 번호가 범위 밖이다");
            for (const Requirement& already : parsed)
                if (already.type == type)
                    return fail("unlocks:" + std::to_string(lineNumber) +
                                ": 종류 " + std::to_string(type) + " 가 두 번 선언됐다");

            std::vector<uint32_t> needs;
            if (fields.size() == 3) {
                const std::string& tail = fields[2];
                if (tail.rfind("requires=", 0) != 0)
                    return fail("unlocks:" + std::to_string(lineNumber) +
                                ": 세 번째 필드는 requires= 여야 한다");
                std::string list = tail.substr(9);
                if (list.empty())
                    return fail("unlocks:" + std::to_string(lineNumber) + ": requires= 가 비었다");
                size_t at = 0;
                while (at <= list.size()) {
                    size_t comma = list.find(',', at);
                    if (comma == std::string::npos) comma = list.size();
                    uint32_t need = 0;
                    if (!ParseType(list.substr(at, comma - at), need))
                        return fail("unlocks:" + std::to_string(lineNumber) +
                                    ": 선행 번호가 범위 밖이다");
                    if (std::find(needs.begin(), needs.end(), need) != needs.end())
                        return fail("unlocks:" + std::to_string(lineNumber) +
                                    ": 같은 선행이 두 번 적혔다");
                    needs.push_back(need);
                    if (comma == list.size()) break;
                    at = comma + 1;
                }
                std::sort(needs.begin(), needs.end());
            }
            Requirement node;
            node.type = type;
            parsed.push_back(node);
            rawNeeds.push_back(std::move(needs));
        }
        if (parsed.empty()) {
            if (error) *error = "unlocks: 선언이 하나도 없다";
            return false;
        }

        // 조밀 색인. 정점 번호를 종류 번호 오름차순으로 다시 매기면 위상 정렬의
        // 최소 힙 동점 처리가 그대로 "종류 번호 오름차순"이 된다.
        std::vector<uint32_t> ids;
        ids.reserve(parsed.size());
        for (const Requirement& node : parsed) ids.push_back(node.type);
        std::sort(ids.begin(), ids.end());
        auto indexOf = [&ids](uint32_t type) -> size_t {
            return static_cast<size_t>(
                std::lower_bound(ids.begin(), ids.end(), type) - ids.begin());
        };

        std::vector<algo::Edge> edges;
        for (size_t i = 0; i < parsed.size(); ++i) {
            for (uint32_t need : rawNeeds[i]) {
                if (!std::binary_search(ids.begin(), ids.end(), need))
                    return fail("unlocks: 선행 " + std::to_string(need) +
                                " 가 선언되지 않았다");
                edges.push_back({static_cast<uint32_t>(indexOf(need)),
                                 static_cast<uint32_t>(indexOf(parsed[i].type))});
            }
        }

        const algo::TopoResult topo = algo::TopologicalOrderWithDiagnosis(ids.size(), edges);
        if (!topo.ok) {
            std::string why = "unlocks: 선행 관계에 순환이 있다 [";
            for (size_t i = 0; i < topo.cycle.size(); ++i) {
                if (i) why += ",";
                why += std::to_string(ids[topo.cycle[i]]);
            }
            why += "]";
            return fail(why);
        }

        nodes_.resize(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) nodes_[i].type = ids[i];
        for (size_t i = 0; i < parsed.size(); ++i)
            nodes_[indexOf(parsed[i].type)].prerequisites = rawNeeds[i];
        declared_ = ids;
        order_.reserve(topo.order.size());
        for (uint32_t index : topo.order) order_.push_back(ids[index]);
        return true;
    }
    // snippet-end: unlock-parse

private:
    static std::string Trim(const std::string& text) {
        size_t begin = 0, end = text.size();
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
        return text.substr(begin, end - begin);
    }
    static std::vector<std::string> Split(const std::string& line) {
        std::vector<std::string> fields;
        size_t at = 0;
        while (at < line.size()) {
            while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
            size_t start = at;
            while (at < line.size() && line[at] != ' ' && line[at] != '\t') ++at;
            if (at > start) fields.push_back(line.substr(start, at - start));
        }
        return fields;
    }
    static bool ParseType(const std::string& text, uint32_t& out) {
        if (text.empty() || text.size() > 2) return false;
        uint32_t value = 0;
        for (char c : text) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + static_cast<uint32_t>(c - '0');
        }
        if (value > kMaxUnlockType) return false;
        out = value;
        return true;
    }

    std::vector<Requirement> nodes_;      // 종류 번호 오름차순
    std::vector<uint32_t>    declared_;   // 오름차순
    std::vector<uint32_t>    order_;      // 위상 순서
    std::vector<uint32_t>    empty_;
};

}  // namespace live
}  // namespace mega
