#include "bench_program.hpp"
#include "language.hpp"

#include <chrono>
#include <cstdio>

namespace {

using Clock = std::chrono::steady_clock;

// 측정 대상 소스는 `bench_program.hpp` 가 소유한다. 정확성 테스트가 같은 상수를
// 읽고 sum·명령 수를 못 박는다.
using lab::lang::bench::kSource;

template <class Function>
double Milliseconds(Function&& function) {
    const auto begin = Clock::now();
    function();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

}  // namespace

int main() {
    constexpr int iterations = lab::lang::bench::kIterations;
    lab::lang::Compiler compiler;
    const auto compiled = compiler.Compile(kSource);
    if (!compiled.program) return 1;

    uint64_t checksum = 0;
    const double compileEveryTime = Milliseconds([&] {
        for (int i = 0; i < iterations; ++i) {
            auto program = compiler.Compile(kSource);
            lab::lang::VirtualMachine vm;
            const auto result = vm.Run(*program.program);
            checksum += result.instructions;
        }
    });

    const double reuseBytecode = Milliseconds([&] {
        for (int i = 0; i < iterations; ++i) {
            lab::lang::VirtualMachine vm;
            const auto result = vm.Run(*compiled.program);
            checksum += result.instructions;
        }
    });

    std::printf("iterations=%d\n", iterations);
    std::printf("compile+run_ms=%.3f\n", compileEveryTime);
    std::printf("run_cached_bytecode_ms=%.3f\n", reuseBytecode);
    std::printf("speedup=%.2fx checksum=%llu\n",
                compileEveryTime / reuseBytecode,
                static_cast<unsigned long long>(checksum));
    return 0;
}
