// patch — 콘텐츠의 정체성. 버전·해시·롤백.
//
// 지금까지 이 게임의 콘텐츠에는 이름이 없었다. `balance.script` 를 고친
// 클라이언트도, 스프라이트를 바꾼 클라이언트도 서버에 조용히 붙을 수 있었다 —
// 서버가 "네가 들고 있는 콘텐츠가 무엇인가"를 물을 어휘 자체가 없었기 때문이다.
//
// 매니페스트가 그 어휘다. 쿡 파이프라인(tools/asset_pipeline.hpp)이 낸 산출
// 해시를 버전과 함께 발행하면, 그 (버전, 해시) 쌍이 콘텐츠의 이름이 된다.
// 서버는 입장 승인에 그 이름을 실어 보내고, 해시가 다른 클라이언트를 거른다.
//
// 두 가지를 의도적으로 구분한다.
//
//   * **미발행(version 0)** — 아직 아무것도 발행하지 않은 상태. 이때는 무엇도
//     거르지 않는다. 강제할 기준이 없는데 거절부터 하면, 매니페스트를 세우는
//     그 순간 모든 클라이언트가 못 붙는다.
//   * **불일치** — 기준이 있는데 다르다. 이때만 거절이다.
//
// Publish 는 단조 증가만 받는다. 그 검사가 없으면 과거 버전을 다시 발행하는
// 것으로 현재가 뒤로 갈 수 있고, 그러면 Rollback 이 "직전"을 가리키지 못한다.

#pragma once

#include <cstdint>
#include <vector>

namespace mega {
namespace live {

struct PatchEntry {
    uint32_t version     = 0;
    uint64_t contentHash = 0;
    uint32_t prevVersion = 0;
};

// 입장 검사 결과. bool 로 줄이지 않는 이유는 "통과"의 두 가지 이유(강제할
// 기준이 없다 / 해시가 같다)를 로그에서 구분할 수 있어야 하기 때문이다.
enum class ContentVerdict : uint8_t { Accept, Unpublished, Mismatch };

inline const char* ContentVerdictName(ContentVerdict v) {
    switch (v) {
        case ContentVerdict::Accept:      return "accept";
        case ContentVerdict::Unpublished: return "unpublished";
        case ContentVerdict::Mismatch:    return "mismatch";
        default:                          return "unknown";
    }
}

class PatchManifest {
public:
    // snippet-begin: patch-publish
    // 새 패치 배포. version 은 **발행 이력 전체에 대해** 단조 증가여야 한다 —
    // 롤백 중이어도 기준은 마지막 발행분이지 현재 가리키는 것이 아니다.
    bool Publish(uint32_t version, uint64_t contentHash) {
        if (version == 0) return false;                          // 0 은 "미발행"의 이름이다
        if (!history_.empty() && version <= history_.back().version) return false;
        const uint32_t previous = history_.empty() ? 0 : history_.back().version;
        history_.push_back({version, contentHash, previous});
        currentIdx_ = static_cast<int>(history_.size()) - 1;
        return true;
    }
    // snippet-end: patch-publish

    uint32_t Current()     const { return Valid() ? history_[currentIdx_].version : 0; }
    uint64_t CurrentHash() const { return Valid() ? history_[currentIdx_].contentHash : 0; }
    // 가장 최근에 발행된 버전. 롤백 중이면 Current() 와 다르다.
    uint32_t Latest()      const { return history_.empty() ? 0 : history_.back().version; }
    bool     RolledBack()  const { return Valid() && currentIdx_ + 1 < static_cast<int>(history_.size()); }
    bool     Published()   const { return Valid(); }

    const PatchEntry* Entry(uint32_t version) const {
        for (const PatchEntry& entry : history_)
            if (entry.version == version) return &entry;
        return nullptr;
    }

    // 직전 버전으로 되돌린다. 더 되돌릴 게 없으면 false.
    bool Rollback() {
        if (currentIdx_ <= 0) return false;
        --currentIdx_;
        return true;
    }
    // 롤백했던 것을 다시 앞으로(가장 최근 publish 까지).
    bool RollForward() {
        if (currentIdx_ + 1 >= static_cast<int>(history_.size())) return false;
        ++currentIdx_;
        return true;
    }

    // ── 입장 검사 ──────────────────────────────────────────────────
    // snippet-begin: patch-verdict
    ContentVerdict Check(uint64_t clientHash) const {
        if (!Valid()) return ContentVerdict::Unpublished;   // 강제할 기준이 없다
        return clientHash == history_[currentIdx_].contentHash ? ContentVerdict::Accept
                                                               : ContentVerdict::Mismatch;
    }
    // snippet-end: patch-verdict
    bool Accepts(uint64_t clientHash) const { return Check(clientHash) != ContentVerdict::Mismatch; }

    size_t VersionCount() const { return history_.size(); }
    void   Clear() { history_.clear(); currentIdx_ = -1; }

private:
    bool Valid() const { return currentIdx_ >= 0 && currentIdx_ < static_cast<int>(history_.size()); }

    std::vector<PatchEntry> history_;
    int                     currentIdx_ = -1;
};

}  // namespace live
}  // namespace mega
