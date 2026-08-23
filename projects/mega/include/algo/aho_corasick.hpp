// aho_corasick — 고정 패턴 집합의 모든 일치를 한 번의 훑기로 전부 모은다.
//
// 이 게임이 이것을 쓰는 자리는 입장 토큰의 JSON 페이로드다. 페이로드는 수백
// 바이트이므로 **속도가 이유가 아니다.** 이유는 다중 패턴 자동자가 "첫 일치"가
// 아니라 "모든 일치"를 내준다는 것이고, 그 차이가 조용한 정책 하나를 명시적
// 거부로 바꾼다 — 같은 필드 이름이 페이로드에 두 번 나오는 상태를, 앞의 것을
// 조용히 채택하는 대신 셀 수 있게 된다.
//
// ── 세 개의 링크 ──────────────────────────────────────────────────
//   goto     현재 상태에서 문자 c 를 읽었을 때 갈 곳. 트라이의 간선이다.
//   failure  goto 가 없을 때 떨어질 곳. **지금까지 읽은 것의 가장 긴 진접미사**
//            중 트라이에 있는 상태다. 전부 뿌리로 만들어 버리면 접미가 겹치는
//            패턴 쌍에서 일치를 놓친다.
//   output   이 상태에서 끝나는 패턴 말고도, failure 를 따라 올라가며 끝나는
//            패턴이 더 있을 수 있다. 그 목록을 미리 이어 두지 않으면 한 위치에서
//            끝나는 두 패턴 중 하나만 보고된다.
//
// 셋 다 없으면 자동자는 "가장 긴 패턴 하나"만 찾는 기계가 된다. 그것으로는 중복을
// 셀 수 없다.
//
// ── 저장 ──────────────────────────────────────────────────────────
// 상태마다 256칸 전이 배열이다. 패턴이 여섯 개, 상태는 수십 개라 전체가 수십 KB
// 이고 한 번만 짓는다. 해시로 줄이면 훑기 안쪽 루프에 분기가 생기고, 그 대가는
// 이 크기에서 값을 하지 못한다.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mega {
namespace algo {

class AhoCorasick {
public:
    struct Match {
        uint32_t id = 0;      // Add 로 준 패턴 번호
        size_t   begin = 0;   // 텍스트에서 패턴이 시작한 자리
        size_t   end = 0;     // 끝 다음 자리
    };

    void Clear() {
        nodes_.clear();
        patternLength_.clear();
        built_ = false;
        NewNode();
    }

    // 빈 패턴은 받지 않는다. 빈 패턴은 모든 위치에서 일치하므로 "모든 일치"라는
    // 계약이 무한이 된다.
    bool Add(std::string_view pattern, uint32_t id) {
        if (pattern.empty()) return false;
        if (nodes_.empty()) NewNode();
        built_ = false;
        uint32_t at = 0;
        for (char raw : pattern) {
            const uint8_t c = static_cast<uint8_t>(raw);
            uint32_t next = nodes_[at].next[c];
            if (next == kNone) {
                next = NewNode();
                nodes_[at].next[c] = next;
            }
            at = next;
        }
        if (nodes_[at].terminal) return false;   // 같은 패턴을 두 번 등록
        nodes_[at].terminal = true;
        nodes_[at].id = id;
        if (patternLength_.size() <= id) patternLength_.resize(id + 1, 0);
        patternLength_[id] = static_cast<uint32_t>(pattern.size());
        return true;
    }

    // snippet-begin: aho-build
    // BFS 한 번에 goto 보완 · failure · output 을 함께 채운다. 뿌리의 자식은
    // failure 가 뿌리이고, 그 아래부터는 부모의 failure 에서 같은 문자로 간 곳이다.
    void Build() {
        if (nodes_.empty()) NewNode();
        std::vector<uint32_t> queue;
        queue.reserve(nodes_.size());
        for (int c = 0; c < 256; ++c) {
            const uint32_t child = nodes_[0].next[c];
            if (child == kNone) {
                nodes_[0].next[c] = 0;    // 뿌리에서 모르는 문자는 제자리
                continue;
            }
            nodes_[child].fail = 0;
            queue.push_back(child);
        }
        for (size_t head = 0; head < queue.size(); ++head) {
            const uint32_t at = queue[head];
            // output 링크. 자기 실패 상태가 종단이면 그 상태를, 아니면 그 상태가
            // 이미 이어 둔 곳을 물려받는다. 이 한 줄이 "한 위치에서 끝나는 두
            // 패턴" 중 짧은 쪽을 살린다.
            const uint32_t fail = nodes_[at].fail;
            nodes_[at].output = nodes_[fail].terminal ? fail : nodes_[fail].output;
            for (int c = 0; c < 256; ++c) {
                const uint32_t child = nodes_[at].next[c];
                if (child == kNone) {
                    nodes_[at].next[c] = nodes_[fail].next[c];
                    continue;
                }
                nodes_[child].fail = nodes_[fail].next[c];
                queue.push_back(child);
            }
        }
        built_ = true;
    }
    // snippet-end: aho-build

    bool Built() const { return built_; }
    size_t StateCount() const { return nodes_.size(); }

    // snippet-begin: aho-scan
    // 텍스트를 한 번 훑어 모든 (패턴, 위치) 를 모은다. 같은 패턴이 여러 번 나오면
    // 여러 번 담긴다 — 그것을 세는 것이 이 자동자를 들인 이유다.
    void Scan(std::string_view text, std::vector<Match>& out) const {
        out.clear();
        if (!built_ || nodes_.empty()) return;
        uint32_t at = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            at = nodes_[at].next[static_cast<uint8_t>(text[i])];
            for (uint32_t node = nodes_[at].terminal ? at : nodes_[at].output;
                 node != kNone; node = nodes_[node].output) {
                const uint32_t id = nodes_[node].id;
                const size_t length = patternLength_[id];
                out.push_back({id, i + 1 - length, i + 1});
            }
        }
    }
    // snippet-end: aho-scan

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    struct Node {
        uint32_t next[256];
        uint32_t fail = 0;
        uint32_t output = kNone;
        uint32_t id = 0;
        bool     terminal = false;
    };

    uint32_t NewNode() {
        Node node;
        for (int c = 0; c < 256; ++c) node.next[c] = kNone;
        nodes_.push_back(node);
        return static_cast<uint32_t>(nodes_.size() - 1);
    }

    std::vector<Node>     nodes_;
    std::vector<uint32_t> patternLength_;
    bool                  built_ = false;
};

}  // namespace algo
}  // namespace mega
