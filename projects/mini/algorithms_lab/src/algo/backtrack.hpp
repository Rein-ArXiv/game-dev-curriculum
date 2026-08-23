// backtrack.hpp — 되돌리는 탐색.
//
// 이 헤더가 소유하는 계약은 하나다. **Apply 와 Undo 는 서로의 역이다.**
// 상태를 복사해 넘기면 이 계약은 자명하지만 비싸다. 변경 로그로 되돌리면 싸지만,
// 로그가 실제 변경을 빠짐없이 담고 있다는 것을 *검사가* 지켜야 한다.
// 그래서 모든 탐색 단계 전후로 상태 해시를 비교한다.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "algo/greedy.hpp"
#include "measure/counters.hpp"

namespace algo {

// snippet-begin: backtrack-domain
// 유한 도메인 CSP. 변수마다 값 후보를 비트마스크로 든다.
// 가지치기가 하는 일은 전부 "도메인에서 값을 지우는 것" 이고, 되돌리기는
// "지운 값을 되살리는 것" 이다 — 그래서 변경 로그의 단위가 (변수, 지운 비트) 다.
struct CspProblem {
    size_t variables = 0;
    uint32_t full_domain = 0;                       // 초기 도메인 비트
    std::vector<std::pair<uint32_t, uint32_t>> not_equal;   // 서로 달라야 하는 쌍
};

struct CspOptions {
    bool forward_check   = false;
    bool arc_consistency = false;
    bool mrv             = false;   // 남은 값이 가장 적은 변수를 먼저
    bool symmetry_break  = false;   // 첫 변수의 값을 오름차순 canonical 로 제한
};

struct CspStats {
    uint64_t nodes = 0;
    uint64_t leaves = 0;
    uint64_t pruned_conflict = 0;
    uint64_t pruned_forward = 0;
    uint64_t pruned_arc = 0;
    uint64_t pruned_symmetry = 0;
    uint64_t undo_entries = 0;
};
// snippet-end: backtrack-domain

// snippet-begin: backtrack-solver
class CspSolver {
public:
    CspSolver(const CspProblem& problem, const CspOptions& opt)
        : p_(problem), opt_(opt) {
        domain_.assign(p_.variables, p_.full_domain);
        value_.assign(p_.variables, -1);
        neighbor_.assign(p_.variables, std::vector<uint32_t>());
        for (const auto& e : p_.not_equal) {
            neighbor_[e.first].push_back(e.second);
            neighbor_[e.second].push_back(e.first);
        }
    }

    // 모든 해를 센다. 대칭 깨기를 켠 실행의 해 집합은 끈 실행의 canonical 집합과
    // 같아야 한다 — 그것이 "대칭 깨기가 해를 잃지 않았다" 의 정의다.
    size_t CountSolutions(std::vector<std::vector<int32_t>>* out = nullptr) {
        stats_ = CspStats{};
        out_ = out;
        if (out_) out_->clear();
        const uint64_t before = StateHash();
        const size_t found = Search(0);
        hash_stable_ = (StateHash() == before);   // 로그가 완전한가
        return found;
    }

    const CspStats& Stats() const { return stats_; }
    bool HashStable() const { return hash_stable_; }

private:
    struct Undo { uint32_t var; uint32_t removed; };

    size_t Search(size_t depth) {
        ++stats_.nodes;
        if (depth == p_.variables) {
            ++stats_.leaves;
            if (out_) out_->push_back(value_);
            return 1;
        }
        const size_t var = PickVariable();
        if (var == p_.variables) return 0;

        size_t total = 0;
        uint32_t candidates = domain_[var];
        // 대칭 깨기: 값이 완전히 교환 가능하면, 첫 등장 순서를 오름차순으로 고정해도
        // 해를 잃지 않는다. 잃는 것은 "이름만 다른 같은 해" 뿐이다.
        if (opt_.symmetry_break) {
            const uint32_t allowed = CanonicalMask(var);
            if ((candidates & ~allowed) != 0) ++stats_.pruned_symmetry;
            candidates &= allowed;
        }

        for (uint32_t bit = 0; bit < 32; ++bit) {
            const uint32_t mask = 1u << bit;
            if ((candidates & mask) == 0) continue;

            // 기본 후향 검사. 이미 배정된 이웃과 충돌하면 그 값은 버린다.
            // 이것이 *없으면* 아래의 전방 검사·AC-3 는 최적화가 아니라 정확성의
            // 유일한 근거가 되어 버린다 — 가지치기를 끈 팔이 기준이 되려면
            // 기준 자체가 제약을 지켜야 한다.
            bool conflict = false;
            for (uint32_t w : neighbor_[var]) {
                if (value_[w] == int32_t(bit)) { conflict = true; break; }
            }
            if (conflict) { ++stats_.pruned_conflict; continue; }

            const size_t log_mark = log_.size();
            value_[var] = int32_t(bit);
            Remove(uint32_t(var), domain_[var] & ~mask);   // 이 변수는 이제 이 값 하나다

            bool ok = true;
            if (opt_.forward_check) ok = ForwardCheck(var, mask);
            if (ok && opt_.arc_consistency) ok = Ac3();
            if (ok) total += Search(depth + 1);

            Rewind(log_mark);
            value_[var] = -1;
        }
        return total;
    }

    // 변경 로그. 되돌리기는 이 로그를 역순으로 되감는 것뿐이다.
    void Remove(uint32_t var, uint32_t bits) {
        if (bits == 0) return;
        domain_[var] &= ~bits;
        log_.push_back(Undo{var, bits});
        ++stats_.undo_entries;
    }
    void Rewind(size_t mark) {
        while (log_.size() > mark) {
            const Undo u = log_.back();
            log_.pop_back();
            domain_[u.var] |= u.removed;    // ← Apply 의 정확한 역
        }
    }

    bool ForwardCheck(size_t var, uint32_t mask) {
        for (uint32_t w : neighbor_[var]) {
            if (value_[w] != -1) continue;
            if ((domain_[w] & mask) == 0) continue;
            Remove(w, mask);
            if (domain_[w] == 0) { ++stats_.pruned_forward; return false; }
        }
        return true;
    }

    // AC-3. 큐가 빌 때까지 (x, y) 호를 다듬는다. not-equal 제약에서는
    // "y 의 도메인이 값 하나뿐이면 x 에서 그 값을 지운다" 로 좁혀진다.
    bool Ac3() {
        std::deque<std::pair<uint32_t, uint32_t>> queue;
        for (const auto& e : p_.not_equal) {
            queue.push_back({e.first, e.second});
            queue.push_back({e.second, e.first});
        }
        while (!queue.empty()) {
            const auto arc = queue.front();
            queue.pop_front();
            const uint32_t x = arc.first, y = arc.second;
            if (Popcount(domain_[y]) != 1) continue;
            if ((domain_[x] & domain_[y]) == 0) continue;
            Remove(x, domain_[y]);
            if (domain_[x] == 0) { ++stats_.pruned_arc; return false; }
            for (uint32_t z : neighbor_[x]) if (z != y) queue.push_back({z, x});
        }
        return true;
    }

    size_t PickVariable() const {
        size_t best = p_.variables;
        size_t best_count = 33;
        for (size_t v = 0; v < p_.variables; ++v) {
            if (value_[v] != -1) continue;
            if (!opt_.mrv) return v;               // 순서대로
            const size_t count = Popcount(domain_[v]);
            if (count < best_count) { best_count = count; best = v; }
        }
        return best;
    }

    // 값 대칭 깨기 마스크. not-equal 제약만 있으면 값 이름은 완전히 교환 가능하다 —
    // 그러면 "이미 쓴 값 중 가장 큰 것 + 1" 을 넘는 값을 새로 쓸 이유가 없다.
    // 잘려 나가는 것은 이름만 다른 같은 해뿐이다. 변수 선택 순서에 무관하도록
    // *배정된 모든* 변수를 본다 — 인덱스가 아니라 배정 여부가 기준이다.
    uint32_t CanonicalMask(size_t var) const {
        int32_t highest = -1;
        for (size_t v = 0; v < p_.variables; ++v) {
            if (v == var) continue;
            if (value_[v] != -1) highest = std::max(highest, value_[v]);
        }
        const uint32_t limit = uint32_t(highest + 1);
        if (limit >= 31) return ~0u;
        return (1u << (limit + 1)) - 1u;
    }

    static size_t Popcount(uint32_t x) {
        size_t n = 0;
        while (x) { x &= x - 1; ++n; }
        return n;
    }

    // 상태 해시. 도메인과 배정을 함께 섞는다. Undo 가 하나라도 빠지면 달라진다.
    uint64_t StateHash() const {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t d : domain_) { h ^= d; h *= 1099511628211ull; }
        for (int32_t v : value_)   { h ^= uint64_t(uint32_t(v)); h *= 1099511628211ull; }
        return h;
    }

    CspProblem p_;
    CspOptions opt_;
    std::vector<uint32_t> domain_;
    std::vector<int32_t>  value_;
    std::vector<std::vector<uint32_t>> neighbor_;
    std::vector<Undo> log_;
    CspStats stats_;
    bool hash_stable_ = false;
    std::vector<std::vector<int32_t>>* out_ = nullptr;
};
// snippet-end: backtrack-solver

// ------------------------------------------------------------ branch & bound

// snippet-begin: backtrack-branch-bound
// 0/1 배낭의 분기 한정. 완화(relaxation)로 상계를 만들고, 그 상계가 지금까지의
// 최선(incumbent)보다 나쁘면 그 가지를 통째로 버린다.
//   완화: 분할 가능 배낭 — 남은 물건을 밀도 순으로 담되 마지막을 쪼갠다.
// 완화가 *진짜 상계* 여야 한다. 상계가 실제 최적보다 낮으면 최적해를 잘라 낸다.
struct BranchBoundResult {
    int64_t best = 0;
    uint64_t nodes = 0;
    uint64_t pruned_bound = 0;
    uint64_t pruned_capacity = 0;
    double  first_gap = 0.0;   // 첫 feasible 해가 최종 최적과 벌린 간격
};

inline double FractionalBound(const std::vector<Item>& sorted, size_t from,
                              int64_t room, int64_t taken) {
    double bound = double(taken);
    for (size_t i = from; i < sorted.size() && room > 0; ++i) {
        if (sorted[i].weight <= room) {
            room -= sorted[i].weight;
            bound += double(sorted[i].value);
        } else {
            bound += double(sorted[i].value) * double(room) / double(sorted[i].weight);
            room = 0;
        }
    }
    return bound;
}

inline BranchBoundResult BranchAndBoundKnapsack(std::vector<Item> items,
                                                int64_t capacity) {
    // 완화가 유효하려면 밀도 내림차순 정렬이 전제다.
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        const double da = double(a.value) / double(a.weight);
        const double db = double(b.value) / double(b.weight);
        if (da != db) return da > db;
        return a.weight < b.weight;
    });
    BranchBoundResult r;
    int64_t first_found = -1;

    struct Frame { size_t at; int64_t room; int64_t value; };
    std::vector<Frame> stack{Frame{0, capacity, 0}};
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        ++r.nodes;
        if (f.value > r.best) {
            r.best = f.value;
            if (first_found < 0) first_found = f.value;
        }
        if (f.at == items.size()) continue;
        if (FractionalBound(items, f.at, f.room, f.value) <= double(r.best)) {
            ++r.pruned_bound;                     // 이 가지는 최선을 넘을 수 없다
            continue;
        }
        stack.push_back(Frame{f.at + 1, f.room, f.value});          // 안 담는다
        if (items[f.at].weight <= f.room) {
            stack.push_back(Frame{f.at + 1, f.room - items[f.at].weight,
                                  f.value + items[f.at].value});    // 담는다
        } else {
            ++r.pruned_capacity;
        }
    }
    if (first_found >= 0 && r.best > 0)
        r.first_gap = double(r.best - first_found) / double(r.best);
    return r;
}
// snippet-end: backtrack-branch-bound

}  // namespace algo
