#include "fault/fault_inject.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <limits>
#include <vector>

namespace skl {

namespace {

struct Rule {
    std::string op;
    std::size_t skip       = 0;
    int         err        = EIO;
    std::size_t times      = 0;  // 0 = 무제한
    std::size_t calls      = 0;
    std::size_t injections = 0;
};

std::mutex& RulesMutex() {
    static std::mutex m;
    return m;
}

std::vector<Rule>& Rules() {
    static std::vector<Rule> rules;
    return rules;
}

struct ErrnoName {
    const char* name;
    int         value;
};

// 이 lab 의 오류 경로에서 실제로 의미가 있는 것들만.
constexpr ErrnoName kErrnoNames[] = {
    {"EACCES",    EACCES},    {"EAGAIN",  EAGAIN},  {"EBADF",   EBADF},
    {"ECANCELED", ECANCELED}, {"EFAULT",  EFAULT},  {"EINTR",   EINTR},
    {"EINVAL",    EINVAL},    {"EIO",     EIO},     {"EMFILE",  EMFILE},
    {"ENFILE",    ENFILE},    {"ENOENT",  ENOENT},  {"ENOMEM",  ENOMEM},
    {"ENOSPC",    ENOSPC},    {"ENOSYS",  ENOSYS},  {"EOVERFLOW", EOVERFLOW},
    {"EPERM",     EPERM},
};

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

bool ParseUnsigned(const std::string& token, std::size_t* out) {
    if (token.empty()) return false;
    std::size_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::size_t>(c - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10)
            return false;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    *out = value;
    return true;
}

bool ParseErrno(const std::string& token, int* out) {
    if (token.empty()) return false;
    for (const ErrnoName& e : kErrnoNames) {
        if (token == e.name) {
            *out = e.value;
            return true;
        }
    }
    std::size_t numeric = 0;
    if (ParseUnsigned(token, &numeric) && numeric > 0 && numeric < 4096) {
        *out = static_cast<int>(numeric);
        return true;
    }
    return false;
}

bool ParseSpec(const std::string& spec, std::vector<Rule>* out) {
    for (const std::string& entry : Split(spec, ',')) {
        if (entry.empty()) continue;  // 후행 콤마 허용
        const std::vector<std::string> field = Split(entry, ':');
        if (field.empty() || field[0].empty() || field.size() > 4) return false;

        Rule rule;
        rule.op = field[0];
        if (field.size() > 1 && !ParseUnsigned(field[1], &rule.skip)) return false;
        if (field.size() > 2 && !ParseErrno(field[2], &rule.err))     return false;
        if (field.size() > 3 && !ParseUnsigned(field[3], &rule.times)) return false;
        out->push_back(std::move(rule));
    }
    return true;
}

}  // namespace

namespace detail {

// constant-initialized — 어떤 동적 초기화보다 먼저 준비된다.
std::atomic<bool> g_faultArmed{false};

int FaultCheckSlow(const char* op) {
    std::lock_guard<std::mutex> lock(RulesMutex());
    for (Rule& rule : Rules()) {
        if (rule.op != op) continue;  // 같은 op 가 여러 개면 첫 규칙만 쓴다.
        ++rule.calls;
        if (rule.calls <= rule.skip) return 0;
        if (rule.times != 0 && rule.injections >= rule.times) return 0;
        ++rule.injections;
        return rule.err;
    }
    return 0;
}

}  // namespace detail

bool SetFaultSpec(const std::string& spec) {
    std::vector<Rule> parsed;
    const bool ok = ParseSpec(spec, &parsed);

    std::lock_guard<std::mutex> lock(RulesMutex());
    if (!ok) {
        Rules().clear();
        detail::g_faultArmed.store(false, std::memory_order_release);
        return false;
    }
    Rules() = std::move(parsed);
    detail::g_faultArmed.store(!Rules().empty(), std::memory_order_release);
    return true;
}

void ClearFaults() {
    std::lock_guard<std::mutex> lock(RulesMutex());
    Rules().clear();
    detail::g_faultArmed.store(false, std::memory_order_release);
}

bool FaultsArmed() {
    return detail::g_faultArmed.load(std::memory_order_acquire);
}

std::size_t FaultCallCount(const char* op) {
    std::lock_guard<std::mutex> lock(RulesMutex());
    for (const Rule& rule : Rules()) {
        if (rule.op == op) return rule.calls;
    }
    return 0;
}

std::size_t FaultInjectionCount(const char* op) {
    std::lock_guard<std::mutex> lock(RulesMutex());
    for (const Rule& rule : Rules()) {
        if (rule.op == op) return rule.injections;
    }
    return 0;
}

namespace {

// SKL_FAULT 환경변수 seam. 프로세스 시작 시 한 번 읽는다 — 빌드를 바꾸지
// 않고 도구/테스트 바이너리에 그대로 주입할 수 있다.
struct EnvLoader {
    EnvLoader() {
        const char* spec = std::getenv("SKL_FAULT");
        if (!spec || spec[0] == '\0') return;
        if (!SetFaultSpec(spec)) {
            std::fprintf(stderr,
                "skl: malformed SKL_FAULT='%s' — fault injection disabled\n",
                spec);
            return;
        }
        std::fprintf(stderr, "skl: fault injection armed: %s\n", spec);
    }
};

const EnvLoader g_envLoader;

}  // namespace

}  // namespace skl
