// dict.hpp — 사전. 해시 표와 확률적 필터.
//
// 이 헤더가 소유하는 계약 둘.
//   1. equal(a, b) => hash(a) == hash(b) 만 요구된다. 역은 아니다 —
//      해시가 같은데 키가 다른 것은 결함이 아니라 *정상* 이고, 표는 그것을
//      감당하도록 설계돼야 한다.
//   2. open addressing 의 조회 종료 불변식 —
//      키가 표에 있으면, 홈 슬롯부터 그 키의 실제 위치까지 *한 번도 쓰이지 않은*
//      슬롯이 없다. 삭제가 슬롯을 EMPTY 로 되돌리면 이 불변식이 깨진다.
//      tombstone 은 그 불변식을 지키기 위한 최소 장치다.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "measure/counters.hpp"

namespace algo {

// snippet-begin: dict-hash
// seed 를 물린 FNV-1a. seed 가 있으면 같은 키 집합이라도 프로세스마다 다른
// 자리에 앉는다 — 충돌을 노린 입력이 매번 통하지는 않게 된다(완전한 방어는
// 아니다. seed 를 알아내는 부채널이 있으면 다시 뚫린다).
inline uint64_t HashBytes(const void* data, size_t n, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ull ^ seed;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    // 최종 확산. FNV 는 하위 비트가 약해서, 표 크기가 2의 거듭제곱이면
    // 마스킹 후 분포가 뭉친다.
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

inline uint64_t HashKey(const std::string& s, uint64_t seed) {
    return HashBytes(s.data(), s.size(), seed);
}
// snippet-end: dict-hash

// ------------------------------------------------------------------ chaining

// snippet-begin: dict-chaining
// 분리 연쇄. 충돌은 버킷 안 리스트로 흡수된다 — 삭제가 자명하고(노드를 떼면 끝)
// load factor 1 을 넘겨도 동작하지만, 노드마다 포인터 추적이 붙는다.
class ChainMap {
public:
    explicit ChainMap(size_t buckets = 16, uint64_t seed = 0x9e3779b9ull)
        : seed_(seed) { bucket_.assign(buckets ? buckets : 1, kNil); }

    void Put(const std::string& key, int64_t value, measure::Counters* c = nullptr) {
        if (double(size_ + 1) > 1.0 * double(bucket_.size())) Rehash(bucket_.size() * 2);
        const size_t b = Bucket(key);
        for (size_t i = bucket_[b]; i != kNil; i = node_[i].next) {
            if (c) ++c->probes;
            if (node_[i].key == key) { node_[i].value = value; return; }
        }
        node_.push_back(Node{key, value, bucket_[b], false});
        bucket_[b] = node_.size() - 1;
        ++size_;
    }

    bool Get(const std::string& key, int64_t* out, measure::Counters* c = nullptr) const {
        const size_t b = Bucket(key);
        for (size_t i = bucket_[b]; i != kNil; i = node_[i].next) {
            if (c) ++c->probes;
            if (node_[i].key == key) { if (out) *out = node_[i].value; return true; }
        }
        return false;
    }

    bool Erase(const std::string& key, measure::Counters* c = nullptr) {
        const size_t b = Bucket(key);
        size_t prev = kNil;
        for (size_t i = bucket_[b]; i != kNil; i = node_[i].next) {
            if (c) ++c->probes;
            if (node_[i].key != key) { prev = i; continue; }
            if (prev == kNil) bucket_[b] = node_[i].next;
            else              node_[prev].next = node_[i].next;
            node_[i].dead = true;   // 노드 풀은 그대로 두고 표시만 한다
            --size_;
            return true;
        }
        return false;
    }

    size_t Size() const { return size_; }
    size_t Buckets() const { return bucket_.size(); }
    size_t LongestChain() const {
        size_t worst = 0;
        for (size_t head : bucket_) {
            size_t len = 0;
            for (size_t i = head; i != kNil; i = node_[i].next) ++len;
            if (len > worst) worst = len;
        }
        return worst;
    }

private:
    struct Node { std::string key; int64_t value; size_t next; bool dead; };
    static const size_t kNil = size_t(-1);

    size_t Bucket(const std::string& key) const {
        return size_t(HashKey(key, seed_) & uint64_t(bucket_.size() - 1));
    }
    void Rehash(size_t buckets) {
        std::vector<Node> old;
        old.swap(node_);
        bucket_.assign(buckets ? buckets : 1, kNil);
        size_ = 0;
        for (const Node& n : old) {
            if (n.dead) continue;
            Put(n.key, n.value);
        }
    }

    std::vector<size_t> bucket_;
    std::vector<Node>   node_;
    size_t   size_ = 0;
    uint64_t seed_;
};
// snippet-end: dict-chaining

// -------------------------------------------------------- open addressing

// snippet-begin: dict-open-policy
// 삭제를 어떻게 처리하는가가 세 팔을 가른다.
//   Tombstone     지운 자리에 묘비를 남긴다. 조회는 묘비를 지나쳐 계속 간다.
//                 간단하지만 묘비가 쌓이면 조회가 실패해도 길어진다.
//   BackwardShift 지운 자리로 뒤쪽 항목을 당겨 온다. 묘비가 없어 조회가 깨끗하지만
//                 삭제가 클러스터 길이만큼 일한다.
//   RobinHood     넣을 때 "자기 홈에서 더 멀리 밀린 쪽" 에게 자리를 양보한다.
//                 probe 길이의 *분산* 이 줄어 P99 가 좋아진다.
enum class DeletePolicy { Tombstone, BackwardShift, RobinHood };
// snippet-end: dict-open-policy

// snippet-begin: dict-open-map
// 선형 탐사 open addressing. 슬롯 상태는 셋이다 — Empty(한 번도 안 씀) ·
// Used · Dead(묘비). 조회는 Empty 에서만 멈춘다.
class OpenMap {
public:
    explicit OpenMap(DeletePolicy policy, size_t capacity = 16,
                     uint64_t seed = 0x9e3779b9ull)
        : policy_(policy), seed_(seed) {
        size_t cap = 1;
        while (cap < capacity) cap *= 2;
        slot_.assign(cap, Slot{});
    }

    void Put(const std::string& key, int64_t value, measure::Counters* c = nullptr) {
        // 묘비도 자리를 차지한다 — used + dead 로 재해시를 판단해야 표가 꽉 차서
        // 조회가 무한 루프에 빠지는 일이 없다.
        if (double(used_ + dead_ + 1) > max_load_ * double(slot_.size()))
            Grow(slot_.size() * 2);
        if (policy_ == DeletePolicy::RobinHood) PutRobinHood(key, value, c);
        else                                    PutLinear(key, value, c);
    }

    bool Get(const std::string& key, int64_t* out, measure::Counters* c = nullptr) const {
        const uint64_t h = HashKey(key, seed_);
        size_t i = size_t(h & uint64_t(slot_.size() - 1));
        size_t dist = 0;
        for (;;) {
            if (c) ++c->probes;
            const Slot& s = slot_[i];
            if (s.state == State::Empty) return false;       // ← 종료 불변식이 여기 산다
            if (s.state == State::Used && s.hash == h && s.key == key) {
                if (out) *out = s.value;
                return true;
            }
            // Robin Hood: 내 밀린 거리가 그 자리 주인보다 크면 나는 없는 것이다.
            if (policy_ == DeletePolicy::RobinHood && s.state == State::Used &&
                Displacement(i) < dist) {
                return false;
            }
            i = (i + 1) & (slot_.size() - 1);
            if (++dist >= slot_.size()) return false;
        }
    }

    bool Erase(const std::string& key, measure::Counters* c = nullptr) {
        const uint64_t h = HashKey(key, seed_);
        size_t i = size_t(h & uint64_t(slot_.size() - 1));
        for (size_t dist = 0; dist < slot_.size(); ++dist) {
            if (c) ++c->probes;
            Slot& s = slot_[i];
            if (s.state == State::Empty) return false;
            if (s.state == State::Used && s.hash == h && s.key == key) {
                if (policy_ == DeletePolicy::Tombstone) {
                    s.state = State::Dead;
                    s.key.clear();
                    ++dead_;
                } else {
                    BackwardShift(i);
                }
                --used_;
                return true;
            }
            i = (i + 1) & (slot_.size() - 1);
        }
        return false;
    }

    size_t Size() const { return used_; }
    size_t Capacity() const { return slot_.size(); }
    size_t Tombstones() const { return dead_; }
    void SetMaxLoad(double v) { max_load_ = v; }

    // 조회 종료 불변식의 직접 검사. 살아 있는 모든 키에 대해, 홈부터 실제 위치까지
    // Empty 슬롯이 없어야 한다. 이 검사가 tombstone 을 EMPTY 로 되돌리는 결함을 잡는다.
    bool ProbeChainIntact() const {
        for (size_t at = 0; at < slot_.size(); ++at) {
            if (slot_[at].state != State::Used) continue;
            size_t i = size_t(slot_[at].hash & uint64_t(slot_.size() - 1));
            while (i != at) {
                if (slot_[i].state == State::Empty) return false;
                i = (i + 1) & (slot_.size() - 1);
            }
        }
        return true;
    }

    size_t LongestCluster() const {
        size_t worst = 0, run = 0;
        // 원형이므로 두 바퀴를 돌되 한 바퀴 분량만 센다.
        for (size_t k = 0; k < slot_.size() * 2; ++k) {
            const size_t i = k & (slot_.size() - 1);
            if (slot_[i].state == State::Empty) run = 0;
            else { ++run; if (run > worst) worst = run; }
        }
        if (worst > slot_.size()) worst = slot_.size();
        return worst;
    }

private:
    enum class State : uint8_t { Empty, Used, Dead };
    struct Slot {
        State       state = State::Empty;
        uint64_t    hash  = 0;
        std::string key;
        int64_t     value = 0;
    };

    size_t Displacement(size_t at) const {
        const size_t home = size_t(slot_[at].hash & uint64_t(slot_.size() - 1));
        return (at + slot_.size() - home) & (slot_.size() - 1);
    }

    void PutLinear(const std::string& key, int64_t value, measure::Counters* c) {
        const uint64_t h = HashKey(key, seed_);
        size_t i = size_t(h & uint64_t(slot_.size() - 1));
        size_t first_dead = size_t(-1);
        for (size_t dist = 0; dist < slot_.size(); ++dist) {
            if (c) ++c->probes;
            Slot& s = slot_[i];
            if (s.state == State::Dead && first_dead == size_t(-1)) first_dead = i;
            if (s.state == State::Used && s.hash == h && s.key == key) {
                s.value = value;
                return;
            }
            if (s.state == State::Empty) {
                // 묘비를 봤으면 거기에 앉는다 — 클러스터가 짧아진다.
                const size_t at = (first_dead != size_t(-1)) ? first_dead : i;
                if (slot_[at].state == State::Dead) --dead_;
                slot_[at] = Slot{State::Used, h, key, value};
                ++used_;
                return;
            }
            i = (i + 1) & (slot_.size() - 1);
        }
    }

    void PutRobinHood(const std::string& key, int64_t value, measure::Counters* c) {
        Slot incoming{State::Used, HashKey(key, seed_), key, value};
        size_t i = size_t(incoming.hash & uint64_t(slot_.size() - 1));
        size_t dist = 0;
        for (size_t step = 0; step < slot_.size(); ++step) {
            if (c) ++c->probes;
            Slot& s = slot_[i];
            if (s.state != State::Used) {
                if (s.state == State::Dead) --dead_;
                s = incoming;
                ++used_;
                return;
            }
            if (s.hash == incoming.hash && s.key == incoming.key) {
                s.value = incoming.value;
                return;
            }
            const size_t theirs = Displacement(i);
            if (theirs < dist) {          // 부자에게서 뺏어 가난한 쪽에 준다
                Slot tmp = s;
                s = incoming;
                incoming = tmp;
                dist = theirs;
            }
            i = (i + 1) & (slot_.size() - 1);
            ++dist;
        }
    }

    // 지운 자리로 뒤쪽 항목을 당겨 온다.
    //
    // 당길 수 있는 항목은 "홈에서 볼 때 구멍이 지금 자리보다 가까운" 것뿐이다.
    // 홈이 (구멍, 현재자리] 구간 안에 있으면 그 항목은 내려올 수 없다 — 내려오면
    // 자기 홈보다 앞에 앉게 되어 자기 자신의 조회가 끊긴다.
    //
    // 중요한 것은 그런 항목을 만났을 때 **거기서 멈추지 않는다** 는 점이다.
    // 멈추고 구멍을 비우면, 그 항목보다 뒤에 있으면서 홈이 구멍보다 앞인 항목들이
    // 전부 조회 불가가 된다. 그래서 j 는 Empty 를 만날 때까지 계속 전진하고,
    // 옮길 수 있는 것을 만날 때만 구멍이 따라 이동한다.
    void BackwardShift(size_t hole) {
        const size_t m = slot_.size();
        size_t j = hole;
        for (size_t guard = 0; guard < m; ++guard) {
            j = (j + 1) & (m - 1);
            if (slot_[j].state != State::Used) break;   // 군집의 끝
            const size_t home = size_t(slot_[j].hash & uint64_t(m - 1));
            const size_t to_hole = (hole + m - home) & (m - 1);
            const size_t to_j    = (j + m - home) & (m - 1);
            if (to_hole > to_j) continue;               // 못 내려온다. 그러나 계속 훑는다.
            slot_[hole] = slot_[j];
            hole = j;
        }
        slot_[hole] = Slot{};
    }

    void Grow(size_t capacity) {
        std::vector<Slot> old;
        old.swap(slot_);
        size_t cap = 1;
        while (cap < capacity) cap *= 2;
        slot_.assign(cap, Slot{});
        used_ = 0;
        dead_ = 0;
        for (const Slot& s : old) {
            if (s.state == State::Used) Put(s.key, s.value);
        }
    }

    DeletePolicy policy_;
    uint64_t     seed_;
    std::vector<Slot> slot_;
    size_t used_ = 0;
    size_t dead_ = 0;
    double max_load_ = 0.7;
};
// snippet-end: dict-open-map

// --------------------------------------------------------------- Bloom filter

// snippet-begin: dict-bloom
// Bloom filter. 없다고 말하면 확실히 없고, 있다고 말하면 아닐 수도 있다.
// n 개를 넣은 뒤 예측 거짓양성률은 (1 - e^(-k n / m))^k 다.
// k 개의 독립 해시 대신 두 해시의 선형 결합(g_i = h1 + i*h2)을 쓴다 — 이론적
// FP 율을 거의 그대로 유지하면서 해시 계산이 둘로 준다.
class BloomFilter {
public:
    BloomFilter(size_t bits, int hashes, uint64_t seed = 0x51ED5EEDull)
        : m_(bits ? bits : 1), k_(hashes < 1 ? 1 : hashes), seed_(seed),
          word_((m_ + 63) / 64, 0) {}

    void Add(const std::string& key) {
        const uint64_t h1 = HashKey(key, seed_);
        const uint64_t h2 = HashKey(key, seed_ ^ 0xA5A5A5A5A5A5A5A5ull) | 1ull;
        for (int i = 0; i < k_; ++i) {
            const size_t bit = size_t((h1 + uint64_t(i) * h2) % uint64_t(m_));
            word_[bit / 64] |= (1ull << (bit % 64));
        }
        ++inserted_;
    }
    bool MightContain(const std::string& key, measure::Counters* c = nullptr) const {
        const uint64_t h1 = HashKey(key, seed_);
        const uint64_t h2 = HashKey(key, seed_ ^ 0xA5A5A5A5A5A5A5A5ull) | 1ull;
        for (int i = 0; i < k_; ++i) {
            if (c) ++c->probes;
            const size_t bit = size_t((h1 + uint64_t(i) * h2) % uint64_t(m_));
            if ((word_[bit / 64] & (1ull << (bit % 64))) == 0) return false;
        }
        return true;
    }
    // 예측 FP 율. 실측과 이 값을 나란히 두는 것이 이 자료구조의 학습 지점이다.
    double PredictedFalsePositiveRate() const {
        const double m = double(m_), n = double(inserted_), k = double(k_);
        const double filled = 1.0 - std::exp(-k * n / m);
        double r = 1.0;
        for (int i = 0; i < k_; ++i) r *= filled;
        return r;
    }
    size_t Bits() const { return m_; }
    int Hashes() const { return k_; }

private:
    size_t   m_;
    int      k_;
    uint64_t seed_;
    std::vector<uint64_t> word_;
    size_t   inserted_ = 0;
};
// snippet-end: dict-bloom

}  // namespace algo
