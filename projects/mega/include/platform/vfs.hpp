// vfs — 가상 파일시스템. 논리 경로를 마운트 규칙으로 물리 위치에 매핑한다.
//
// 코드는 "assets/maps/x.txt" 같은 논리 경로를 쓰고, VFS 가 마운트로 물리 위치
// (PC 디렉토리, 모바일 번들, 웹 패키지)로 해석한다. 경로는 `.`/`..`/중복 구분자를
// 정규화해 일관성과 보안을 지킨다 — `..` 가 루트를 벗어나지 못하게 막아 디렉토리
// 탈출을 차단한다(platform VFS 챕터). 검증을 위해 인메모리 파일 저장을 함께 둔다.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace mega {
namespace platform {

class Vfs {
public:
    // 경로 정규화: 구분자 통일(/), `.` 제거, `..` 상위 이동(루트 탈출 차단), 중복 슬래시 제거.
    static std::string Normalize(const std::string& path) {
        std::vector<std::string> parts;
        std::string cur;
        auto flush = [&] {
            if (cur.empty() || cur == ".") { cur.clear(); return; }
            if (cur == "..") { if (!parts.empty()) parts.pop_back(); cur.clear(); return; }  // 루트면 무시(탈출 차단)
            parts.push_back(cur);
            cur.clear();
        };
        for (char c : path) {
            if (c == '/' || c == '\\') flush();
            else cur += c;
        }
        flush();
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) { if (i) out += '/'; out += parts[i]; }
        return out;
    }

    // 마운트: 논리 접두사 → 물리 루트.
    void Mount(const std::string& logicalPrefix, const std::string& physicalRoot) {
        mounts_.push_back({Normalize(logicalPrefix), physicalRoot});
    }

    // 논리 경로 → 물리 경로. 가장 긴 일치 마운트를 적용.
    std::string Resolve(const std::string& logical) const {
        std::string norm = Normalize(logical);
        const MountEntry* best = nullptr;
        size_t bestLen = 0;
        for (const MountEntry& m : mounts_) {
            if (IsPrefix(m.logical, norm) && m.logical.size() >= bestLen) { best = &m; bestLen = m.logical.size(); }
        }
        if (!best) return norm;
        std::string rest = norm.substr(best->logical.size());
        if (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
        if (rest.empty()) return best->physical;
        return best->physical + "/" + rest;
    }

    // --- 인메모리 파일(검증용) ---
    void AddFile(const std::string& physicalPath, const std::string& content) { files_[physicalPath] = content; }
    bool Exists(const std::string& logical) const { return files_.count(Resolve(logical)) != 0; }
    bool Read(const std::string& logical, std::string& out) const {
        auto it = files_.find(Resolve(logical));
        if (it == files_.end()) return false;
        out = it->second;
        return true;
    }

    size_t MountCount() const { return mounts_.size(); }

private:
    struct MountEntry { std::string logical, physical; };

    // a 가 b 의 경로 접두사인가(세그먼트 경계 기준).
    static bool IsPrefix(const std::string& a, const std::string& b) {
        if (a.empty()) return true;
        if (b.size() < a.size()) return false;
        if (b.compare(0, a.size(), a) != 0) return false;
        return b.size() == a.size() || b[a.size()] == '/';
    }

    std::vector<MountEntry>                            mounts_;
    std::unordered_map<std::string, std::string>  files_;
};

}  // namespace platform
}  // namespace mega
