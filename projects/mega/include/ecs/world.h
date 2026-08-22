// Archetype ECS — 컴포넌트 조합이 같은 엔티티를 같은 저장소(archetype)에 모은다.
//
// sparse set 방식이 컴포넌트별로 흩어진 배열을 두는 반면, archetype 방식은
// "같은 컴포넌트 집합"을 한 덩어리로 모아 컬럼(SoA)으로 저장한다. 쿼리가
// "이 컴포넌트들을 가진 모든 엔티티"일 때, 해당 archetype 들의 컬럼을 통째로
// 순회하면 되어 캐시 친화적이고 SIMD 적용이 쉽다. 컴포넌트 추가/제거가 드물고
// 쿼리가 잦은 게임 시뮬에서 유리하다(parts 시리즈의 ECS 비교 결론).
//
// 컴포넌트는 trivially copyable 만 허용한다 — archetype 간 이동이 memcpy 이고
// 소멸자를 호출하지 않기 때문이다(게임 컴포넌트는 대부분 POD).
//
// 순회 순서에 대하여. `archetypes_` 는 **어느 조합이 먼저 나타났는가** 순으로
// 늘어난다. 같은 엔티티 집합이라도 도달 경로가 다르면(스폰 순서가 다르면)
// 배열 순서가 달라지므로, 그 순서로 부동소수를 누적하면 결과가 실행마다 갈린다.
// 그래서 시뮬 갱신 경로는 `ForEachChunk`(생성 순서)가 아니라
// `ForEachChunkOrdered`(조합 마스크 오름차순)만 쓴다. 마스크는 컴포넌트 집합의
// 순수 함수라 도달 경로에 의존하지 않는다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace mega {

struct EntityId {
    uint32_t index      = 0xFFFFFFFFu;
    uint32_t generation = 0;
    bool Valid() const { return index != 0xFFFFFFFFu; }
    bool operator==(const EntityId& o) const { return index == o.index && generation == o.generation; }
};

namespace detail {
inline int& ComponentCounter() { static int c = 0; return c; }
}

// 컴포넌트 타입마다 0..63 의 고유 id 를 부여(프로세스 전역).
template <class T>
int ComponentTypeId() {
    static int id = detail::ComponentCounter()++;
    return id;
}

class World {
public:
    static constexpr int kMaxComponents = 64;

    World() {
        infos_.resize(kMaxComponents);
        // 인덱스 0 = 빈 archetype(컴포넌트 없음).
        archetypes_.push_back(Archetype{0});
        archByMask_[0] = 0;
    }

    EntityId Create() {
        uint32_t idx;
        if (!freeList_.empty()) { idx = freeList_.back(); freeList_.pop_back(); }
        else { idx = (uint32_t)records_.size(); records_.push_back({}); }
        Record& r = records_[idx];
        r.alive = true;
        r.archetype = 0;
        r.row = (uint32_t)archetypes_[0].entities.size();
        EntityId e{idx, r.generation};
        archetypes_[0].entities.push_back(e);
        return e;
    }

    bool Alive(EntityId e) const {
        return e.index < records_.size() && records_[e.index].alive &&
               records_[e.index].generation == e.generation;
    }

    void Destroy(EntityId e) {
        if (!Alive(e)) return;
        Record& r = records_[e.index];
        RemoveRow(archetypes_[r.archetype], r.row);
        r.alive = false;
        ++r.generation;             // 핸들 무효화(재사용 시 generation 불일치)
        freeList_.push_back(e.index);
    }

    template <class T>
    void Add(EntityId e, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "컴포넌트는 trivially copyable 이어야 한다");
        if (!Alive(e)) return;
        int cid = ComponentTypeId<T>();
        EnsureInfo<T>(cid);
        Record& r = records_[e.index];
        Archetype& src = archetypes_[r.archetype];
        if (src.mask & Bit(cid)) {              // 이미 있으면 덮어쓰기
            *GetPtr<T>(src, r.row, cid) = value;
            return;
        }
        uint64_t newMask = src.mask | Bit(cid);
        uint32_t tgtIdx  = GetOrCreateArchetype(newMask);
        MoveEntity(e, tgtIdx);
        // 새 컴포넌트 값 기록.
        Record& r2 = records_[e.index];
        *GetPtr<T>(archetypes_[r2.archetype], r2.row, cid) = value;
    }

    template <class T>
    void Remove(EntityId e) {
        if (!Alive(e)) return;
        int cid = ComponentTypeId<T>();
        Record& r = records_[e.index];
        if (!(archetypes_[r.archetype].mask & Bit(cid))) return;
        uint64_t newMask = archetypes_[r.archetype].mask & ~Bit(cid);
        uint32_t tgtIdx  = GetOrCreateArchetype(newMask);
        MoveEntity(e, tgtIdx);
    }

    template <class T>
    bool Has(EntityId e) const {
        if (!Alive(e)) return false;
        return archetypes_[records_[e.index].archetype].mask & Bit(ComponentTypeId<T>());
    }

    template <class T>
    T* Get(EntityId e) {
        if (!Alive(e)) return nullptr;
        int cid = ComponentTypeId<T>();
        Record& r = records_[e.index];
        Archetype& a = archetypes_[r.archetype];
        if (!(a.mask & Bit(cid))) return nullptr;
        return GetPtr<T>(a, r.row, cid);
    }

    // 청크 쿼리: 컴포넌트 Ts 를 모두 가진 archetype 마다 컬럼 포인터를 넘긴다.
    // fn(size_t count, Ts*...) — SoA 핫 패스.
    template <class... Ts, class Fn>
    void ForEachChunk(Fn&& fn) {
        uint64_t qmask = (Bit(ComponentTypeId<Ts>()) | ...);
        for (Archetype& a : archetypes_) {
            if ((a.mask & qmask) != qmask) continue;
            size_t n = a.entities.size();
            if (n == 0) continue;
            fn(n, ColPtr<Ts>(a)...);
        }
    }

    // 순서를 고정한 청크 쿼리. 시뮬 갱신 경로가 쓰는 쪽이다.
    //
    // 위 ForEachChunk 와 다른 것은 방문 순서 하나뿐이다. archetype 을 조합
    // 마스크 오름차순으로 방문하므로, 같은 엔티티 집합이면 그 집합에 어떤
    // 순서로 도달했든 같은 순서로 돌아온다. 정렬 대상은 archetype 개수(수십)라
    // 청크 하나의 원소 수(수천)에 비하면 무시할 만하다.
    // snippet-begin: archetype-order
    template <class... Ts, class Fn>
    void ForEachChunkOrdered(Fn&& fn) {
        uint64_t qmask = (Bit(ComponentTypeId<Ts>()) | ...);
        visit_.clear();
        for (uint32_t i = 0; i < (uint32_t)archetypes_.size(); ++i) {
            const Archetype& a = archetypes_[i];
            if ((a.mask & qmask) != qmask || a.entities.empty()) continue;
            visit_.push_back(i);
        }
        std::sort(visit_.begin(), visit_.end(), [this](uint32_t l, uint32_t r) {
            return archetypes_[l].mask < archetypes_[r].mask;
        });
        for (uint32_t i : visit_) {
            Archetype& a = archetypes_[i];
            fn(a.entities.size(), ColPtr<Ts>(a)...);
        }
    }
    // snippet-end: archetype-order

    // 엔티티 단위 쿼리: fn(EntityId, Ts&...).
    template <class... Ts, class Fn>
    void ForEach(Fn&& fn) {
        uint64_t qmask = (Bit(ComponentTypeId<Ts>()) | ...);
        for (Archetype& a : archetypes_) {
            if ((a.mask & qmask) != qmask) continue;
            size_t n = a.entities.size();
            for (size_t i = 0; i < n; ++i)
                fn(a.entities[i], ColPtr<Ts>(a)[i]...);
        }
    }

    size_t EntityCount() const {
        size_t total = 0;
        for (const auto& a : archetypes_) total += a.entities.size();
        return total;
    }
    size_t ArchetypeCount() const { return archetypes_.size(); }

private:
    struct ComponentInfo { uint32_t size = 0; uint32_t align = 0; bool registered = false; };
    struct Record { uint32_t archetype = 0; uint32_t row = 0; uint32_t generation = 0; bool alive = false; };

    struct Archetype {
        uint64_t mask = 0;
        std::vector<int>                  typeList;   // 정렬된 컴포넌트 id
        std::vector<std::vector<uint8_t>> columns;    // typeList 와 평행, 원시 바이트
        std::vector<EntityId>             entities;    // row → entity
        int ColumnOf(int cid) const {
            for (size_t i = 0; i < typeList.size(); ++i) if (typeList[i] == cid) return (int)i;
            return -1;
        }
    };

    static uint64_t Bit(int cid) { return uint64_t(1) << cid; }

    template <class T>
    void EnsureInfo(int cid) {
        if (!infos_[cid].registered)
            infos_[cid] = {(uint32_t)sizeof(T), (uint32_t)alignof(T), true};
    }

    template <class T>
    T* GetPtr(Archetype& a, uint32_t row, int cid) {
        int col = a.ColumnOf(cid);
        return reinterpret_cast<T*>(a.columns[col].data() + (size_t)row * sizeof(T));
    }
    template <class T>
    T* ColPtr(Archetype& a) {
        int col = a.ColumnOf(ComponentTypeId<T>());
        return reinterpret_cast<T*>(a.columns[col].data());
    }

    uint32_t GetOrCreateArchetype(uint64_t mask) {
        auto it = archByMask_.find(mask);
        if (it != archByMask_.end()) return it->second;
        Archetype a;
        a.mask = mask;
        for (int cid = 0; cid < kMaxComponents; ++cid)
            if (mask & Bit(cid)) { a.typeList.push_back(cid); a.columns.emplace_back(); }
        uint32_t idx = (uint32_t)archetypes_.size();
        archetypes_.push_back(std::move(a));
        archByMask_[mask] = idx;
        return idx;
    }

    // 엔티티를 src archetype 에서 tgt archetype 으로 이동(공통 컴포넌트는 memcpy).
    void MoveEntity(EntityId e, uint32_t tgtIdx) {
        Record& r = records_[e.index];
        uint32_t srcIdx = r.archetype;
        if (srcIdx == tgtIdx) return;
        Archetype& src = archetypes_[srcIdx];
        Archetype& tgt = archetypes_[tgtIdx];
        uint32_t srcRow = r.row;
        uint32_t newRow = (uint32_t)tgt.entities.size();

        // tgt 의 각 컬럼에 한 행을 추가: src 에 있으면 복사, 없으면 0으로.
        for (size_t c = 0; c < tgt.typeList.size(); ++c) {
            int cid = tgt.typeList[c];
            uint32_t sz = infos_[cid].size;
            std::vector<uint8_t>& col = tgt.columns[c];
            size_t oldBytes = col.size();
            col.resize(oldBytes + sz);
            int srcCol = src.ColumnOf(cid);
            if (srcCol >= 0)
                std::memcpy(col.data() + oldBytes, src.columns[srcCol].data() + (size_t)srcRow * sz, sz);
            else
                std::memset(col.data() + oldBytes, 0, sz);   // 새 컴포넌트는 0 초기화
        }
        tgt.entities.push_back(e);

        RemoveRow(src, srcRow);     // src 에서 swap-remove
        r.archetype = tgtIdx;
        r.row       = newRow;
    }

    // archetype 에서 한 행 제거(마지막 행을 swap, 이동된 엔티티 record 갱신).
    void RemoveRow(Archetype& a, uint32_t row) {
        uint32_t last = (uint32_t)a.entities.size() - 1;
        if (row != last) {
            for (size_t c = 0; c < a.typeList.size(); ++c) {
                uint32_t sz = infos_[a.typeList[c]].size;
                std::vector<uint8_t>& col = a.columns[c];
                std::memcpy(col.data() + (size_t)row * sz, col.data() + (size_t)last * sz, sz);
            }
            a.entities[row] = a.entities[last];
            records_[a.entities[row].index].row = row;    // 이동된 엔티티 record 수정
        }
        for (size_t c = 0; c < a.typeList.size(); ++c) {
            uint32_t sz = infos_[a.typeList[c]].size;
            a.columns[c].resize(a.columns[c].size() - sz);
        }
        a.entities.pop_back();
    }

    std::vector<ComponentInfo>            infos_;
    std::vector<Archetype>                archetypes_;
    std::vector<uint32_t>                 visit_;   // 정렬된 방문 목록(재사용)
    std::unordered_map<uint64_t, uint32_t> archByMask_;
    std::vector<Record>                   records_;
    std::vector<uint32_t>                 freeList_;
};

}  // namespace mega
