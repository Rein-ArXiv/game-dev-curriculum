// patch — 버전·패치 매니페스트·롤백. 출시 후 콘텐츠를 안전하게 바꾼다.
//
// 패치마다 버전·콘텐츠 해시·이전 버전을 매니페스트에 적는다. 새 패치를 publish
// 하면 current 가 올라가고, 문제가 생기면 직전 버전으로 rollback 한다. 버전이
// 순서를 매겨 어느 시점의 콘텐츠든 재현할 수 있다(live_service 패치 챕터).

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

class PatchManifest {
public:
    // 새 패치 배포. version 은 단조 증가여야 한다(아니면 거부, false).
    bool Publish(uint32_t version, uint64_t contentHash) {
        if (!history_.empty() && version <= history_.back().version) return false;
        uint32_t prev = history_.empty() ? 0 : history_.back().version;
        history_.push_back({version, contentHash, prev});
        currentIdx_ = (int)history_.size() - 1;
        return true;
    }

    uint32_t Current()     const { return Valid() ? history_[currentIdx_].version : 0; }
    uint64_t CurrentHash() const { return Valid() ? history_[currentIdx_].contentHash : 0; }

    // 직전 버전으로 되돌린다. 더 되돌릴 게 없으면 false.
    bool Rollback() {
        if (currentIdx_ <= 0) return false;
        --currentIdx_;
        return true;
    }
    // 롤백했던 것을 다시 앞으로(가장 최근 publish 까지).
    bool RollForward() {
        if (currentIdx_ + 1 >= (int)history_.size()) return false;
        ++currentIdx_;
        return true;
    }

    size_t VersionCount() const { return history_.size(); }

private:
    bool Valid() const { return currentIdx_ >= 0 && currentIdx_ < (int)history_.size(); }

    std::vector<PatchEntry> history_;
    int                     currentIdx_ = -1;
};

}  // namespace live
}  // namespace mega
