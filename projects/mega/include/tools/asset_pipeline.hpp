// asset_pipeline — 증분 에셋 빌드. 변경분만 다시 굽는다.
//
// 각 에셋의 콘텐츠 해시와 의존성을 추적한다. 에셋의 "유효 해시" 는 자기 콘텐츠
// 해시 + 모든 의존성의 유효 해시를 합친 값이다. 마지막으로 구운 유효 해시와 다르면
// (콘텐츠가 바뀌었거나, 의존하는 에셋이 바뀌었으면) 다시 굽는다. 의존성이 바뀌면
// 그것을 쓰는 에셋도 자동으로 더티가 된다 — 풀 리빌드 없이 최소 재작업(tools 에셋
// 파이프라인 챕터).

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mega {
namespace tools {

class AssetPipeline {
public:
    // 에셋 등록/갱신: 콘텐츠 해시 + 의존 에셋 이름들.
    void Register(const std::string& name, uint64_t contentHash, const std::vector<std::string>& deps = {}) {
        Entry& e = assets_[name];
        e.contentHash = contentHash;
        e.deps = deps;
    }
    // 소스 변경: 콘텐츠 해시만 갱신(다음 Cook 에서 더티로 잡힘).
    void UpdateSource(const std::string& name, uint64_t newHash) {
        auto it = assets_.find(name);
        if (it != assets_.end()) it->second.contentHash = newHash;
    }

    // 더티 에셋(유효 해시 변경)만 굽는다. 구운 개수 반환.
    int Cook() {
        int count = 0;
        for (auto& [name, e] : assets_) {
            uint64_t eff = EffectiveHash(name);
            if (e.cookedHash != eff || !e.cooked) {
                e.cookedHash = eff;
                e.cooked = true;
                ++count;
            }
        }
        return count;
    }

    bool IsCooked(const std::string& name) const {
        auto it = assets_.find(name);
        return it != assets_.end() && it->second.cooked;
    }
    size_t Count() const { return assets_.size(); }

private:
    struct Entry {
        uint64_t                 contentHash = 0;
        std::vector<std::string> deps;
        uint64_t                 cookedHash = 0;
        bool                     cooked = false;
    };

    static uint64_t Combine(uint64_t a, uint64_t b) {
        a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
        return a;
    }

    // 유효 해시: 콘텐츠 해시 + 모든 의존성의 유효 해시(소스 기준, 구운 상태 무관).
    uint64_t EffectiveHash(const std::string& name) const {
        auto it = assets_.find(name);
        if (it == assets_.end()) return 0;
        uint64_t h = it->second.contentHash;
        for (const std::string& d : it->second.deps) h = Combine(h, EffectiveHash(d));
        return h;
    }

    std::unordered_map<std::string, Entry> assets_;
};

}  // namespace tools
}  // namespace mega
