// repository — 직렬화 가능한 레코드를 KvStore 에 id 로 저장/조회하는 제네릭 계층.
//
// KvStore(영속·트랜잭션) + 바이너리 직렬화(레코드↔바이트)를 이어 붙인다. 레코드
// 종류마다 키 접두사를 달리해(예: "acc:", "match:") 한 스토어에 공존시킨다. 여러
// 레포지토리에 걸친 변경도 KvStore 의 트랜잭션으로 원자적으로 커밋한다.

#pragma once

#include "db/kv_store.hpp"
#include "serialize/archive.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace db {

template <class T>
class Repository {
public:
    Repository(KvStore& store, std::string prefix) : store_(store), prefix_(std::move(prefix)) {}

    void Save(uint32_t id, T& rec) {
        std::vector<uint8_t> bytes = SaveBinary(rec);          // 레코드 → 바이트
        store_.Put(Key(id), std::string(bytes.begin(), bytes.end()));
    }
    bool Load(uint32_t id, T& out) const {
        std::string s;
        if (!store_.Get(Key(id), s)) return false;
        return LoadBinary(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
    bool Exists(uint32_t id) const { return store_.Has(Key(id)); }
    void Remove(uint32_t id) { store_.Delete(Key(id)); }

    std::string Key(uint32_t id) const { return prefix_ + std::to_string(id); }

private:
    KvStore&    store_;
    std::string prefix_;
};

}  // namespace db
}  // namespace mega
