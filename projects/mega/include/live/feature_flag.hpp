// feature_flag — A/B 팔과 점진 롤아웃의 출처. 코드 재배포 없이 팔을 고른다.
//
// 이 저장소의 A/B 스위치(entity_store · pathing · steering …)는 처음에 CLI 인자와
// 환경 변수에서 왔다. 그것으로 충분했던 이유는 팔을 고르는 사람이 곧 프로세스를
// 띄우는 사람이었기 때문이다. 서버가 매치를 만들고 클라이언트가 붙는 구조에서는
// 그 둘이 다른 사람이 되고, 그러면 "누가 어느 팔로 도는가"를 정할 자리가 없다.
//
// 여기가 그 자리다. 규칙은 셋이다.
//
//   1. **안정 버킷** — 같은 (플래그 이름, 플레이어 id)는 언제나 같은 팔이다.
//      난수도 시각도 쓰지 않는다. 매치마다 팔이 바뀌면 그 사용자가 본 것이
//      무엇이었는지 사후에 말할 수 없고, 그러면 A/B 는 측정이 아니라 소문이 된다.
//   2. **플래그마다 독립** — 버킷 해시에 플래그 이름이 섞인다. 섞지 않으면 모든
//      플래그가 같은 사용자 집합을 켜서, 두 실험이 서로의 결과를 오염시킨다.
//   3. **모르는 값은 기본 팔** — 팔 목록의 **첫 번째**가 기본 팔이고, 그것은
//      정의상 "스위치가 생기기 전의 동작"이다(game/switches.hpp 의 규약 2와 같다).
//      운영자가 오타 난 팔 이름을 내려보내도 결과는 그 기본 팔이지 크래시가 아니다.
//
// 이 헤더는 파일을 읽지 않는다. 텍스트를 받아 파싱하는 함수만 두고, 그 텍스트를
// 디스크에서 가져오는 일은 io/ 를 쓸 수 있는 상위 계층이 한다.

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mega {
namespace live {

// ── 안정 버킷 ──────────────────────────────────────────────────────
// (플래그 이름, 사용자 id, salt) → 32비트. salt 는 같은 플래그 안에서 서로
// 독립인 두 질문("롤아웃에 드는가"와 "어느 팔인가")을 가르는 데 쓴다.
// snippet-begin: flag-bucket
inline uint32_t FlagBucket(std::string_view name, uint32_t userId, uint32_t salt) {
    uint32_t h = 2166136261u;
    for (char c : name) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
    h ^= salt;   h *= 16777619u;
    h ^= userId; h *= 16777619u;
    h ^= h >> 16;
    return h;
}
// snippet-end: flag-bucket

// ── 이름과 팔의 문법 ───────────────────────────────────────────────
// 이 문자열들은 서버가 클라이언트로 내려보내고, 운영자가 파일로 적는다. 둘 다
// 신뢰 경계 밖이므로 문법이 곧 방어선이다. 소문자 스네이크와 숫자만 받는다 —
// 구분자(`=` · `;`)가 값 안에 들어갈 수 없으므로 인코딩이 애매해질 여지가 없다.
inline constexpr size_t kFlagNameMax = 24;
inline constexpr size_t kFlagArmMax  = 16;
inline constexpr size_t kFlagSetMax  = 32;    // 한 플레이어에게 내려가는 항목 수
inline constexpr size_t kFlagArmsMax = 8;     // 한 플래그의 팔 개수
inline constexpr size_t kFlagWireMax = 512;   // 인코딩된 바이트 상한

inline bool ValidFlagToken(std::string_view text, size_t maxLength) {
    if (text.empty() || text.size() > maxLength) return false;
    for (char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// ── 확정된 배정 ────────────────────────────────────────────────────
// 서버가 한 플레이어에 대해 판정을 끝낸 결과. 클라이언트는 이것을 받아서
// 그대로 쓰고, 다시 버킷을 계산하지 않는다 — 같은 해시를 두 곳에서 돌리면
// 한쪽만 바뀌었을 때 두 프로세스가 다른 게임을 돌게 된다.
struct FlagAssignment {
    std::string name;
    std::string arm;
};

class FlagSet {
public:
    // 이름 오름차순을 항상 유지한다. 인코딩이 삽입 순서에 좌우되면 같은 배정이
    // 실행마다 다른 바이트가 되고, 골든 패킷 대조가 성립하지 않는다.
    bool Assign(std::string_view name, std::string_view arm) {
        if (!ValidFlagToken(name, kFlagNameMax)) return false;
        if (!ValidFlagToken(arm, kFlagArmMax)) return false;
        auto slot = std::lower_bound(
            items_.begin(), items_.end(), name,
            [](const FlagAssignment& item, std::string_view key) { return item.name < key; });
        if (slot != items_.end() && slot->name == name) {
            slot->arm.assign(arm);
            return true;
        }
        if (items_.size() >= kFlagSetMax) return false;
        items_.insert(slot, FlagAssignment{std::string(name), std::string(arm)});
        return true;
    }

    const std::string* Find(std::string_view name) const {
        auto slot = std::lower_bound(
            items_.begin(), items_.end(), name,
            [](const FlagAssignment& item, std::string_view key) { return item.name < key; });
        if (slot == items_.end() || slot->name != name) return nullptr;
        return &slot->arm;
    }
    // 없으면 호출자가 준 기본값. "모르는 플래그"와 "기본 팔"이 같은 결과를 내는
    // 것이 의도다 — 배정이 도착하지 않아도 게임은 스위치 이전 동작으로 돈다.
    std::string Get(std::string_view name, std::string_view fallback) const {
        const std::string* arm = Find(name);
        return arm ? *arm : std::string(fallback);
    }

    bool   Empty() const { return items_.empty(); }
    size_t Count() const { return items_.size(); }
    void   Clear() { items_.clear(); }
    const std::vector<FlagAssignment>& All() const { return items_; }

    // snippet-begin: flag-wire
    std::string Encode() const {
        std::string out;
        for (const FlagAssignment& item : items_) {
            if (!out.empty()) out += ';';
            out += item.name;
            out += '=';
            out += item.arm;
        }
        return out;
    }

    // 실패하면 out 을 건드리지 않는다 — 절반만 적용된 배정은 어느 팔로도
    // 설명되지 않는 세 번째 상태다.
    static bool Decode(std::string_view text, FlagSet& out, std::string* error = nullptr) {
        auto fail = [&](const char* why) {
            if (error) *error = why;
            return false;
        };
        if (text.size() > kFlagWireMax) return fail("배정 문자열이 상한을 넘는다");
        FlagSet parsed;
        size_t begin = 0;
        while (begin < text.size()) {
            size_t end = text.find(';', begin);
            if (end == std::string_view::npos) end = text.size();
            const std::string_view item = text.substr(begin, end - begin);
            const size_t equals = item.find('=');
            if (equals == std::string_view::npos) return fail("항목 형식은 name=arm 이다");
            const std::string_view name = item.substr(0, equals);
            const std::string_view arm = item.substr(equals + 1);
            if (parsed.Find(name)) return fail("같은 플래그가 두 번 배정됐다");
            if (!parsed.Assign(name, arm)) return fail("플래그 이름 또는 팔이 문법에 맞지 않는다");
            begin = end + 1;
        }
        out = std::move(parsed);
        return true;
    }
    // snippet-end: flag-wire

private:
    std::vector<FlagAssignment> items_;   // 이름 오름차순
};

// ── 정의 ───────────────────────────────────────────────────────────
// 팔이 둘 이상인 플래그. arms[0] 이 기본 팔이다.
struct VariantSpec {
    std::string              name;
    std::vector<std::string> arms;
    int                      rollout = 0;   // 기본 팔이 아닌 팔로 가는 사용자 비율(0..100)
    std::string              forced;        // 비지 않으면 전원 이 팔(운영자 강제)
};

// 켜고 끄는 플래그. rollout 이 음수면 미설정(Set 값 그대로).
struct ToggleSpec {
    std::string name;
    bool        enabled = false;
    int         rollout = -1;
};

class FeatureFlags {
public:
    // ── on/off 플래그 ──────────────────────────────────────────────
    void Set(std::string_view name, bool enabled) { ToggleSlot(name).enabled = enabled; }
    void SetRollout(std::string_view name, int percent) {
        ToggleSlot(name).rollout = std::clamp(percent, 0, 100);
    }
    bool IsEnabled(std::string_view name) const {
        const ToggleSpec* spec = FindToggle(name);
        return spec && spec->enabled;
    }
    bool IsEnabledForUser(std::string_view name, uint32_t userId) const {
        const ToggleSpec* spec = FindToggle(name);
        if (!spec) return false;
        if (spec->rollout < 0) return spec->enabled;
        return static_cast<int>(FlagBucket(name, userId, 0u) % 100u) < spec->rollout;
    }

    // 팔 인덱스. 팔 이름이 없던 시절의 API 라 그대로 둔다 — 같은 사용자는 언제나
    // 같은 인덱스다.
    int Variant(std::string_view name, uint32_t userId, int variants) const {
        if (variants <= 1) return 0;
        return static_cast<int>(FlagBucket(name, userId, 0u) % static_cast<uint32_t>(variants));
    }

    // ── 팔 이름이 있는 플래그 ──────────────────────────────────────
    bool Define(std::string_view name, const std::vector<std::string>& arms,
                int rollout = 100, std::string* error = nullptr) {
        auto fail = [&](const char* why) {
            if (error) *error = std::string(name) + ": " + why;
            return false;
        };
        if (!ValidFlagToken(name, kFlagNameMax)) return fail("플래그 이름 문법 위반");
        if (arms.empty() || arms.size() > kFlagArmsMax) return fail("팔은 1개 이상 8개 이하");
        for (size_t i = 0; i < arms.size(); ++i) {
            if (!ValidFlagToken(arms[i], kFlagArmMax)) return fail("팔 이름 문법 위반");
            for (size_t j = 0; j < i; ++j)
                if (arms[i] == arms[j]) return fail("같은 팔이 두 번 나온다");
        }
        VariantSpec& spec = VariantSlot(name);
        spec.arms = arms;
        spec.rollout = std::clamp(rollout, 0, 100);
        spec.forced.clear();
        return true;
    }

    // 운영자 강제. 팔 이름이 정의에 없어도 **저장은 한다** — 그 값이 어디서
    // 왔는지 말할 수 있어야 하기 때문이다. 대신 판정은 기본 팔로 떨어지고,
    // 반환값 false 가 호출자에게 그 사실을 알린다.
    bool Force(std::string_view name, std::string_view arm, std::string* error = nullptr) {
        VariantSpec* spec = FindVariantMutable(name);
        if (!spec) {
            if (error) *error = std::string(name) + ": 정의되지 않은 플래그";
            return false;
        }
        if (!ValidFlagToken(arm, kFlagArmMax)) {
            if (error) *error = std::string(name) + ": 팔 이름 문법 위반";
            return false;
        }
        spec->forced.assign(arm);
        for (const std::string& known : spec->arms)
            if (known == arm) return true;
        if (error) *error = std::string(name) + "=" + std::string(arm) + ": 알 수 없는 팔, 기본 팔로 폴백";
        return false;
    }
    void ClearForce(std::string_view name) {
        if (VariantSpec* spec = FindVariantMutable(name)) spec->forced.clear();
    }

    bool Defined(std::string_view name) const { return FindVariant(name) != nullptr; }
    const VariantSpec* Definition(std::string_view name) const { return FindVariant(name); }
    size_t DefinitionCount() const { return variants_.size(); }
    size_t ToggleCount() const { return toggles_.size(); }
    const std::vector<VariantSpec>& Definitions() const { return variants_; }

    // 이 사용자의 팔. 정의가 없으면 빈 문자열 — 호출자는 자기 기본값을 유지한다.
    // snippet-begin: flag-resolve
    std::string ArmFor(std::string_view name, uint32_t userId) const {
        const VariantSpec* spec = FindVariant(name);
        if (!spec || spec->arms.empty()) return {};
        if (!spec->forced.empty()) {
            for (const std::string& arm : spec->arms)
                if (arm == spec->forced) return arm;      // 운영자가 고른 팔
            return spec->arms.front();                    // 알 수 없는 팔 → 기본 팔
        }
        if (spec->arms.size() == 1) return spec->arms.front();
        if (static_cast<int>(FlagBucket(name, userId, 0u) % 100u) >= spec->rollout)
            return spec->arms.front();                    // 롤아웃 밖 → 기본 팔
        const uint32_t alternatives = static_cast<uint32_t>(spec->arms.size() - 1);
        return spec->arms[1 + FlagBucket(name, userId, 0x9E3779B9u) % alternatives];
    }
    // snippet-end: flag-resolve

    // 이 사용자에게 확정된 전량. 이름 오름차순이라 인코딩이 결정적이다.
    FlagSet Resolve(uint32_t userId) const {
        FlagSet out;
        for (const VariantSpec& spec : variants_)
            out.Assign(spec.name, ArmFor(spec.name, userId));
        for (const ToggleSpec& spec : toggles_)
            out.Assign(spec.name, IsEnabledForUser(spec.name, userId) ? "on" : "off");
        return out;
    }

private:
    ToggleSpec& ToggleSlot(std::string_view name) {
        auto slot = std::lower_bound(
            toggles_.begin(), toggles_.end(), name,
            [](const ToggleSpec& item, std::string_view key) { return item.name < key; });
        if (slot != toggles_.end() && slot->name == name) return *slot;
        return *toggles_.insert(slot, ToggleSpec{std::string(name), false, -1});
    }
    const ToggleSpec* FindToggle(std::string_view name) const {
        auto slot = std::lower_bound(
            toggles_.begin(), toggles_.end(), name,
            [](const ToggleSpec& item, std::string_view key) { return item.name < key; });
        if (slot == toggles_.end() || slot->name != name) return nullptr;
        return &*slot;
    }
    VariantSpec& VariantSlot(std::string_view name) {
        auto slot = std::lower_bound(
            variants_.begin(), variants_.end(), name,
            [](const VariantSpec& item, std::string_view key) { return item.name < key; });
        if (slot != variants_.end() && slot->name == name) return *slot;
        return *variants_.insert(slot, VariantSpec{std::string(name), {}, 0, {}});
    }
    const VariantSpec* FindVariant(std::string_view name) const {
        auto slot = std::lower_bound(
            variants_.begin(), variants_.end(), name,
            [](const VariantSpec& item, std::string_view key) { return item.name < key; });
        if (slot == variants_.end() || slot->name != name) return nullptr;
        return &*slot;
    }
    VariantSpec* FindVariantMutable(std::string_view name) {
        return const_cast<VariantSpec*>(FindVariant(name));
    }

    std::vector<ToggleSpec>  toggles_;    // 이름 오름차순
    std::vector<VariantSpec> variants_;   // 이름 오름차순
};

// ── 운영자 파일 ────────────────────────────────────────────────────
// 한 줄이 한 정의다.
//
//     variant entity_store aos soa ecs rollout=50 force=soa
//     toggle  scrap_weekend on
//     toggle  beta off rollout=25
//
// 원자적 폴백: 한 줄이라도 거부되면 파일 전체를 버린다. 절반만 적용된 플래그
// 표는 "운영자가 의도한 구성"도 "기본 구성"도 아닌 세 번째 상태다.
inline constexpr size_t kFlagFileMaxLines = 256;
inline constexpr size_t kFlagFileMaxDefinitions = 64;

inline bool ParseFlagConfig(std::string_view text, FeatureFlags& out,
                            std::string* error = nullptr) {
    FeatureFlags parsed;
    size_t lineNumber = 0, definitions = 0;
    size_t begin = 0;
    auto fail = [&](const char* why) {
        if (error) *error = "flags:" + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    while (begin <= text.size()) {
        size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(begin, end - begin);
        begin = end + 1;
        ++lineNumber;
        if (lineNumber > kFlagFileMaxLines) return fail("줄 수가 상한을 넘는다");
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::vector<std::string_view> word;
        size_t cursor = 0;
        while (cursor < line.size()) {
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) ++cursor;
            const size_t wordBegin = cursor;
            while (cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t') ++cursor;
            if (cursor > wordBegin) word.push_back(line.substr(wordBegin, cursor - wordBegin));
        }
        if (word.empty() || word[0][0] == '#') continue;
        if (++definitions > kFlagFileMaxDefinitions) return fail("정의 수가 상한을 넘는다");

        if (word[0] == "variant") {
            if (word.size() < 3) return fail("variant <이름> <팔>... 형식이 아니다");
            std::vector<std::string> arms;
            int rollout = 100;
            std::string forced;
            for (size_t i = 2; i < word.size(); ++i) {
                const size_t equals = word[i].find('=');
                if (equals == std::string_view::npos) { arms.emplace_back(word[i]); continue; }
                const std::string_view key = word[i].substr(0, equals);
                const std::string_view value = word[i].substr(equals + 1);
                if (key == "rollout") {
                    int percent = 0;
                    if (value.empty() || value.size() > 3) return fail("rollout 은 0..100");
                    for (char c : value) {
                        if (c < '0' || c > '9') return fail("rollout 은 0..100");
                        percent = percent * 10 + (c - '0');
                    }
                    if (percent > 100) return fail("rollout 은 0..100");
                    rollout = percent;
                } else if (key == "force") {
                    forced.assign(value);
                } else {
                    return fail("알 수 없는 키");
                }
            }
            std::string reason;
            if (!parsed.Define(word[1], arms, rollout, &reason)) return fail("정의 거부");
            // 알 수 없는 강제 팔은 여기서 거부한다. 런타임 폴백은 **서버가
            // 내려보낸 값**을 위한 것이지, 운영자가 오타를 눈치채지 못하게
            // 하려는 것이 아니다.
            if (!forced.empty() && !parsed.Force(word[1], forced, &reason))
                return fail("force 값이 팔 목록에 없다");
        } else if (word[0] == "toggle") {
            if (word.size() < 3) return fail("toggle <이름> on|off 형식이 아니다");
            if (!ValidFlagToken(word[1], kFlagNameMax)) return fail("플래그 이름 문법 위반");
            if (word[2] == "on") parsed.Set(word[1], true);
            else if (word[2] == "off") parsed.Set(word[1], false);
            else return fail("toggle 값은 on|off");
            for (size_t i = 3; i < word.size(); ++i) {
                const size_t equals = word[i].find('=');
                if (equals == std::string_view::npos || word[i].substr(0, equals) != "rollout")
                    return fail("알 수 없는 키");
                const std::string_view value = word[i].substr(equals + 1);
                int percent = 0;
                if (value.empty() || value.size() > 3) return fail("rollout 은 0..100");
                for (char c : value) {
                    if (c < '0' || c > '9') return fail("rollout 은 0..100");
                    percent = percent * 10 + (c - '0');
                }
                if (percent > 100) return fail("rollout 은 0..100");
                parsed.SetRollout(word[1], percent);
            }
        } else {
            return fail("첫 낱말은 variant 또는 toggle 이다");
        }
        if (end == text.size()) break;
    }
    out = std::move(parsed);
    if (error) error->clear();
    return true;
}

}  // namespace live
}  // namespace mega
