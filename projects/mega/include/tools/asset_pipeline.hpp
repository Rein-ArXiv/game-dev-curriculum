// asset_pipeline — 증분 쿡. 바뀐 것과 그 의존자만 다시 굽는다.
//
// 에셋이 PPM 열 장이던 시절에는 굽는 단계가 없었다. 원본을 그대로 실행 파일
// 옆으로 복사하면 끝이고, "무엇을 다시 만들어야 하는가"라는 질문 자체가 없었다.
// 종류가 늘고(레벨·웨이브·밸런스·포즈·행동 트리·셰이더) 그 사이에 의존 관계가
// 생기면서 — 포즈는 본 정의를 참조하고, 행동 트리는 행동 이름 목록을 참조한다 —
// 그 질문이 처음으로 실재한다.
//
// 답은 **유효 해시**다. 어떤 에셋의 유효 해시는 자기 콘텐츠 해시와 모든 의존
// 에셋의 유효 해시를 합친 값이다. 마지막으로 구운 유효 해시와 다르면 다시
// 굽는다. 의존이 바뀌면 그 값이 위로 전파되므로, 의존자는 자기 콘텐츠가 그대로
// 여도 자동으로 더티가 된다.
//
// 두 가지가 이 헤더의 계약이다.
//
//   1. **결정적 순서** — 등록 순서나 해시 버킷이 아니라 이름 오름차순으로만
//      순회한다. 쿡 결과 목록과 콘텐츠 버전 해시가 실행마다 같아야, 그 해시를
//      입장 검사의 기준으로 쓸 수 있다(live/patch.hpp).
//   2. **순환은 거부** — 의존 그래프에 순환이 있으면 유효 해시가 정의되지
//      않는다. 재귀로 계산하면 그 상태가 스택 오버플로로 나타나므로, 명시적
//      스택으로 색칠하며 순회해 순환을 **값으로** 돌려준다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mega {
namespace tools {

enum class CookStatus : uint8_t { Ok, Cycle, MissingDependency, Overflow, BadName };

inline const char* CookStatusName(CookStatus s) {
    switch (s) {
        case CookStatus::Ok:                return "ok";
        case CookStatus::Cycle:             return "cycle";
        case CookStatus::MissingDependency: return "missing_dependency";
        case CookStatus::Overflow:          return "overflow";
        case CookStatus::BadName:           return "bad_name";
        default:                            return "unknown";
    }
}

class AssetPipeline {
public:
    static constexpr size_t kMaxAssets = 512;
    static constexpr size_t kMaxDependencies = 16;
    static constexpr size_t kMaxNameLength = 64;

    static bool ValidAssetName(std::string_view name) {
        if (name.empty() || name.size() > kMaxNameLength) return false;
        for (char c : name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '/';
            if (!ok) return false;
        }
        return true;
    }

    // 에셋 등록/갱신: 콘텐츠 해시 + 의존 에셋 이름들. 의존이 아직 등록되지
    // 않아도 받는다 — 등록 순서로 그래프를 세울 수 있어야 하고, 실재 여부는
    // Cook 이 판정한다.
    bool Register(std::string_view name, uint64_t contentHash,
                  const std::vector<std::string>& deps = {}) {
        if (!ValidAssetName(name)) return false;
        if (deps.size() > kMaxDependencies) return false;
        for (const std::string& dep : deps) {
            if (!ValidAssetName(dep)) return false;
            if (dep == name) return false;              // 자기 참조는 가장 짧은 순환이다
        }
        // 의존은 **집합**이지 열이 아니다. 그런데 유효 해시는 접어 만드는 값이라
        // 나열 순서에 좌우된다 — 같은 그래프를 다른 순서로 적은 두 빌드가 다른
        // 콘텐츠 버전을 내면 그 해시는 정체성이 될 수 없다. 그래서 여기서 한 번
        // 정렬하고, 중복은 실수로 보고 거부한다.
        std::vector<std::string> sorted = deps;
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 1; i < sorted.size(); ++i)
            if (sorted[i] == sorted[i - 1]) return false;
        Entry* existing = FindMutable(name);
        if (!existing && assets_.size() >= kMaxAssets) return false;
        Entry& entry = existing ? *existing : Insert(name);
        entry.contentHash = contentHash;
        entry.deps = std::move(sorted);
        return true;
    }

    // 소스 변경: 콘텐츠 해시만 갱신(다음 Cook 에서 더티로 잡힌다).
    bool UpdateSource(std::string_view name, uint64_t newHash) {
        Entry* entry = FindMutable(name);
        if (!entry) return false;
        entry->contentHash = newHash;
        return true;
    }

    // ── 증분 쿡 ────────────────────────────────────────────────────
    // 더티인 에셋만 굽고, 구운 이름을 오름차순으로 돌려준다.
    // snippet-begin: cook-incremental
    CookStatus Cook(int* rebuilt, std::vector<std::string>* names = nullptr,
                    std::string* error = nullptr) {
        std::vector<uint64_t> effective;
        const CookStatus status = Evaluate(effective, error);
        if (status != CookStatus::Ok) return status;

        int count = 0;
        if (names) names->clear();
        for (size_t i = 0; i < assets_.size(); ++i) {
            Entry& entry = assets_[i];
            if (entry.cooked && entry.cookedHash == effective[i]) continue;
            entry.cookedHash = effective[i];
            entry.cooked = true;
            ++count;
            if (names) names->push_back(entry.name);
        }
        if (rebuilt) *rebuilt = count;
        if (error) error->clear();
        return CookStatus::Ok;
    }
    // snippet-end: cook-incremental

    // 개수만 묻는 짧은 형태. 순환이나 없는 의존이면 -1 — "0개 구웠다"와
    // 구분돼야 하므로 0 을 돌려주지 않는다.
    int Cook() {
        int count = 0;
        return Cook(&count) == CookStatus::Ok ? count : -1;
    }

    // 구운 산출물 전체의 이름. 이 값이 live::PatchManifest 로 발행되고, 서버가
    // 입장 승인에 실어 클라이언트의 것과 대조한다.
    // snippet-begin: content-version
    uint64_t ContentVersionHash() const {
        uint64_t h = 0xcbf29ce484222325ull;
        for (const Entry& entry : assets_) {                 // 이름 오름차순
            if (!entry.cooked) continue;
            for (char c : entry.name) h = Combine(h, static_cast<uint64_t>(static_cast<uint8_t>(c)));
            h = Combine(h, entry.cookedHash);
        }
        return h;
    }
    // snippet-end: content-version

    bool EffectiveHash(std::string_view name, uint64_t& out) const {
        std::vector<uint64_t> effective;
        if (Evaluate(effective, nullptr) != CookStatus::Ok) return false;
        const size_t index = IndexOf(name);
        if (index == kNone) return false;
        out = effective[index];
        return true;
    }

    bool IsCooked(std::string_view name) const {
        const Entry* entry = Find(name);
        return entry && entry->cooked;
    }
    uint64_t CookedHash(std::string_view name) const {
        const Entry* entry = Find(name);
        return entry ? entry->cookedHash : 0;
    }
    size_t Count() const { return assets_.size(); }
    void   Clear() { assets_.clear(); }
    const std::vector<std::string>& Dependencies(std::string_view name) const {
        static const std::vector<std::string> empty;
        const Entry* entry = Find(name);
        return entry ? entry->deps : empty;
    }

private:
    struct Entry {
        std::string              name;
        uint64_t                 contentHash = 0;
        std::vector<std::string> deps;
        uint64_t                 cookedHash = 0;
        bool                     cooked = false;
    };

    static constexpr size_t kNone = static_cast<size_t>(-1);

    static uint64_t Combine(uint64_t a, uint64_t b) {
        a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
        return a;
    }

    // 명시적 스택으로 후위 순회하며 유효 해시를 아래에서 위로 접는다.
    // 회색(스택 위) 노드를 다시 만나면 그것이 순환이다.
    // snippet-begin: cook-evaluate
    CookStatus Evaluate(std::vector<uint64_t>& effective, std::string* error) const {
        const size_t total = assets_.size();
        effective.assign(total, 0);
        std::vector<uint8_t> color(total, 0);          // 0 흰, 1 회(방문 중), 2 검(완료)
        std::vector<std::pair<size_t, size_t>> stack;  // (에셋 인덱스, 다음 의존 커서)

        for (size_t root = 0; root < total; ++root) {
            if (color[root] != 0) continue;
            color[root] = 1;
            stack.push_back({root, 0});
            while (!stack.empty()) {
                const size_t index = stack.back().first;
                const Entry& entry = assets_[index];
                if (stack.back().second < entry.deps.size()) {
                    const std::string& depName = entry.deps[stack.back().second];
                    ++stack.back().second;
                    const size_t dep = IndexOf(depName);
                    if (dep == kNone) {
                        if (error) *error = entry.name + " → " + depName + ": 없는 의존";
                        return CookStatus::MissingDependency;
                    }
                    if (color[dep] == 1) {
                        if (error) *error = entry.name + " → " + depName + ": 순환 의존";
                        return CookStatus::Cycle;
                    }
                    if (color[dep] == 0) {
                        color[dep] = 1;
                        stack.push_back({dep, 0});
                    }
                    continue;
                }
                uint64_t hash = entry.contentHash;
                for (const std::string& dep : entry.deps)
                    hash = Combine(hash, effective[IndexOf(dep)]);
                effective[index] = hash;
                color[index] = 2;
                stack.pop_back();
            }
        }
        return CookStatus::Ok;
    }
    // snippet-end: cook-evaluate

    size_t IndexOf(std::string_view name) const {
        auto slot = std::lower_bound(
            assets_.begin(), assets_.end(), name,
            [](const Entry& entry, std::string_view key) { return entry.name < key; });
        if (slot == assets_.end() || slot->name != name) return kNone;
        return static_cast<size_t>(slot - assets_.begin());
    }
    const Entry* Find(std::string_view name) const {
        const size_t index = IndexOf(name);
        return index == kNone ? nullptr : &assets_[index];
    }
    Entry* FindMutable(std::string_view name) {
        const size_t index = IndexOf(name);
        return index == kNone ? nullptr : &assets_[index];
    }
    Entry& Insert(std::string_view name) {
        auto slot = std::lower_bound(
            assets_.begin(), assets_.end(), name,
            [](const Entry& entry, std::string_view key) { return entry.name < key; });
        Entry fresh;
        fresh.name.assign(name);
        return *assets_.insert(slot, std::move(fresh));
    }

    std::vector<Entry> assets_;   // 이름 오름차순
};

}  // namespace tools
}  // namespace mega
