// paths.hpp 의 플랫폼별 구현. 시스템 콜은 전부 여기서 끝난다.

#include "platform/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mega {
namespace platform {

std::string ExecutableDirectory() {
#if defined(_WIN32)
    // MAX_PATH 를 넘는 경로가 실재하므로 버퍼를 늘려 가며 재시도한다.
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 6; ++attempt) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return ".";
        if (length < buffer.size()) {
            buffer.resize(length);
            std::error_code error;
            const std::filesystem::path parent =
                std::filesystem::path(buffer).parent_path();
            (void)error;
            const std::string result = parent.string();
            return result.empty() ? std::string(".") : result;
        }
        buffer.resize(buffer.size() * 2);
    }
    return ".";
#else
    std::error_code error;
    const std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return ".";
    const std::string result = self.parent_path().string();
    return result.empty() ? std::string(".") : result;
#endif
}

bool EnvironmentVariable(const char* name, std::string& out) {
    if (!name) return false;
    const char* value = std::getenv(name);
    if (!value || *value == '\0') return false;
    out = value;
    return true;
}

bool EnsureDirectory(const std::string& path) {
    if (path.empty()) return false;
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) return true;
    std::filesystem::create_directories(path, error);
    return !error;
}

std::string UserDataDirectory(const std::string& application) {
    if (application.empty()) return std::string();
    std::filesystem::path directory;
    std::string root;
#if defined(_WIN32)
    if (EnvironmentVariable("LOCALAPPDATA", root)) directory = root;
    else directory = ExecutableDirectory();
    directory /= application;
#else
    if (EnvironmentVariable("HOME", root)) directory = root;
    else directory = ".";
    directory /= ".local/share";
    directory /= application;
#endif
    const std::string result = directory.string();
    if (!EnsureDirectory(result)) return std::string();
    return result;
}

}  // namespace platform
}  // namespace mega
