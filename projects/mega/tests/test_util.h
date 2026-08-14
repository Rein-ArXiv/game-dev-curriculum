// 의존성 없는 미니 테스트 하니스.

#pragma once

#include <cstdio>

namespace megatest {
inline int g_failures = 0;
inline int g_checks   = 0;
}

#define CHECK(cond)                                                          \
    do {                                                                    \
        ++megatest::g_checks;                                               \
        if (!(cond)) {                                                      \
            ++megatest::g_failures;                                         \
            std::printf("  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                      \
    do {                                                                    \
        ++megatest::g_checks;                                               \
        if (!((a) == (b))) {                                                \
            ++megatest::g_failures;                                         \
            std::printf("  FAIL %s:%d  CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, #a, #b); \
        }                                                                   \
    } while (0)

#define TEST_MAIN(name)                                                     \
    int main() {                                                            \
        std::printf("[test] %s\n", name);                                   \
        RunTests();                                                         \
        std::printf("[test] %s: %d checks, %d failures\n",                  \
                    name, megatest::g_checks, megatest::g_failures);        \
        return megatest::g_failures == 0 ? 0 : 1;                           \
    }
