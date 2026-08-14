#include "language.hpp"

#include <chrono>
#include <cstdio>

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kSource =
    "let x = 1; let sum = 0;"
    "while (x <= 100) { sum = sum + x; x = x + 1; }";

template <class Function>
double Milliseconds(Function&& function) {
    const auto begin = Clock::now();
    function();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

}  // namespace

int main() {
    constexpr int iterations = 10000;
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
