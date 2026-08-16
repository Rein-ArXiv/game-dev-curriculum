// Per-step case registry shared by the five test binaries.
//
// Every check in this lab belongs to exactly one step article: the step that
// introduced the behaviour being checked. Tagging the cases and letting each
// binary filter on a tag is what lets CTest carry one target per step, so a
// reader who has just finished step N can run the checks step N added and see
// what they observed instead of a single "all tests passed" line.
//
// argv[1] selects either one case by name or every case of one step. Without
// it every case in the binary runs, which is what the whole-binary CTest
// targets keep doing.

#ifndef GNL_TEST_STEP_CASES_H
#define GNL_TEST_STEP_CASES_H

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace gnl_test {

// The lifecycle and oversized-datagram cases check one backend contract that
// has two implementations, and only one of them is compiled here. The step they
// belong to is therefore the step that added the backend actually being built:
// step03 wrote the IOCP one, step04 wrote the epoll one.
#if defined(_WIN32)
inline constexpr const char* kBackendStep = "step03";
#else
inline constexpr const char* kBackendStep = "step04";
#endif

struct Case {
    const char* step;
    const char* name;
    int (*run)();  // 0 on success.
};

inline std::string& Note() {
    static std::string note;
    return note;
}

// What the case actually observed, printed next to its name. A case that prints
// only "ok" tells the reader it ran, not what it saw.
inline void Observe(std::string text) { Note() = std::move(text); }

// process_tests locates its sibling executables from argv[0].
inline const char*& ProgramPath() {
    static const char* path = "";
    return path;
}

inline int Run(const char* binary, std::initializer_list<Case> cases, int argc,
               char** argv) {
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) ProgramPath() = argv[0];
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    std::size_t width = 0;
    for (const Case& item : cases) {
        const std::size_t length = std::strlen(item.name);
        if (length > width) width = length;
    }

    std::vector<std::pair<std::string, int>> perStep;
    int passed = 0;
    for (const Case& item : cases) {
        // snippet-begin: case-filter
        if (filter != nullptr && std::strcmp(filter, item.step) != 0 &&
            std::strcmp(filter, item.name) != 0) {
            continue;
        }
        // snippet-end: case-filter
        Note().clear();
        std::cout << '[' << item.step << "] " << item.name
                  << std::string(width - std::strlen(item.name), ' ') << "  "
                  << std::flush;
        if (item.run() != 0) {
            std::cout << "FAILED\n";
            std::cerr << binary << ": case " << item.name << " failed\n";
            return 1;
        }
        std::cout << "ok";
        if (!Note().empty()) std::cout << "  " << Note();
        std::cout << '\n';
        ++passed;
        if (perStep.empty() || perStep.back().first != item.step) {
            perStep.emplace_back(item.step, 0);
        }
        ++perStep.back().second;
    }

    if (passed == 0) {
        std::cerr << binary << ": no case matched `" << (filter ? filter : "")
                  << "`\n";
        return 1;
    }

    std::cout << binary << ": " << passed << " case(s) passed [";
    for (std::size_t index = 0; index < perStep.size(); ++index) {
        if (index != 0) std::cout << ", ";
        std::cout << perStep[index].first << ' ' << perStep[index].second;
    }
    std::cout << "]\n";
    return 0;
}

}  // namespace gnl_test

#endif  // GNL_TEST_STEP_CASES_H
