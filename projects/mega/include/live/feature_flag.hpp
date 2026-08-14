// feature_flag — 피처 플래그·점진 롤아웃·A/B. 코드 재배포 없이 기능을 켠다.
//
// 단순 on/off 플래그와, 사용자 일부에게만 켜는 점진 롤아웃(percent)을 둔다.
// 롤아웃은 (사용자 id, 플래그 이름) 해시를 100으로 나눈 나머지로 결정해, 같은
// 사용자는 항상 같은 그룹에 든다(결정적·sticky). A/B 도 같은 해시 버킷팅이다
// (live_service 피처 플래그/원격 설정 챕터).

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mega {
namespace live {

class FeatureFlags {
public:
    void Set(const std::string& name, bool enabled) { flags_[name] = enabled; }
    bool IsEnabled(const std::string& name) const {
        auto it = flags_.find(name);
        return it != flags_.end() && it->second;
    }

    // 점진 롤아웃 비율(0~100). 설정 시 해당 비율의 사용자에게만 켜진다.
    void SetRollout(const std::string& name, int percent) {
        if (percent < 0) percent = 0; if (percent > 100) percent = 100;
        rollout_[name] = percent;
    }

    // 이 사용자에게 켜졌는가(sticky·결정적). rollout 미설정이면 Set 값을 따른다.
    bool IsEnabledForUser(const std::string& name, uint32_t userId) const {
        auto rit = rollout_.find(name);
        if (rit == rollout_.end()) return IsEnabled(name);
        uint32_t bucket = Bucket(name, userId) % 100;        // 0..99
        return (int)bucket < rit->second;
    }

    // A/B 변형 인덱스(0..variants-1). 같은 사용자는 항상 같은 변형.
    int Variant(const std::string& name, uint32_t userId, int variants) const {
        if (variants <= 1) return 0;
        return (int)(Bucket(name, userId) % (uint32_t)variants);
    }

private:
    // (이름, 사용자) → 결정적 해시.
    static uint32_t Bucket(const std::string& name, uint32_t userId) {
        uint32_t h = 2166136261u;
        for (char c : name) { h ^= (uint8_t)c; h *= 16777619u; }
        h ^= userId; h *= 16777619u;
        h ^= h >> 16;
        return h;
    }

    std::unordered_map<std::string, bool> flags_;
    std::unordered_map<std::string, int>  rollout_;
};

}  // namespace live
}  // namespace mega
