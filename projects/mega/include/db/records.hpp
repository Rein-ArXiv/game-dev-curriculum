// records — 영속 대상 도메인 레코드. 바이너리 직렬화로 바이트화해 KV 에 넣는다.
//
// 계정·매치 결과·영구 강화는 구조체다. 각자 Serialize 로 필드를 한 번 기술하면,
// 같은 함수가 저장(쓰기)·복원(읽기)을 겸한다. schemaVersion 을 넣어 필드가 늘어도
// 옛 레코드를 읽을 여지를 둔다(db 시리즈 스키마 진화).

#pragma once

#include "serialize/archive.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mega {
namespace db {

struct Account {
    uint32_t    schemaVersion = 1;
    uint32_t    id = 0;
    std::string name;
    uint32_t    rating = 1000;
    uint64_t    createdAt = 0;     // 호출자가 채움(결정성 위해 외부 주입)

    template <class Ar>
    void Serialize(Ar& ar) { ar(schemaVersion, id, name, rating, createdAt); }
};

struct MatchResult {
    uint32_t schemaVersion = 1;
    uint32_t id = 0;
    uint32_t hostAccount = 0;
    uint32_t waveReached = 0;
    uint32_t score = 0;
    uint8_t  cleared = 0;          // 0/1
    uint64_t endedAt = 0;

    template <class Ar>
    void Serialize(Ar& ar) { ar(schemaVersion, id, hostAccount, waveReached, score, cleared, endedAt); }
};

struct Upgrade {
    uint32_t schemaVersion = 1;
    uint32_t accountId = 0;
    uint32_t towerType = 0;
    uint32_t tier = 0;             // 강화 티어

    template <class Ar>
    void Serialize(Ar& ar) { ar(schemaVersion, accountId, towerType, tier); }
};

}  // namespace db
}  // namespace mega
