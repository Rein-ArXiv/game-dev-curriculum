#include "language.hpp"

#include <algorithm>
#include <cstdio>

int main() {
    constexpr const char* source = R"(
        let wave = 1;
        let hp = 100;
        while (wave <= 5) {
            hp = hp * 1.22;
            wave = wave + 1;
        }
        let clamped_hp = clamp(hp);
    )";

    lab::lang::Compiler compiler;
    auto compiled = compiler.Compile(source);
    if (!compiled.program) {
        for (const auto& diagnostic : compiled.diagnostics)
            std::fprintf(stderr, "%s\n", lab::lang::FormatDiagnostic(diagnostic).c_str());
        return 1;
    }

    lab::lang::VirtualMachine vm;
    vm.Bind("clamp", [](double value) { return std::clamp(value, 1.0, 1000.0); });
    const auto result = vm.Run(*compiled.program);
    if (!result.ok) {
        std::fprintf(stderr, "%s\n",
                     lab::lang::FormatDiagnostic(*result.diagnostic).c_str());
        return 1;
    }

    std::printf("instructions=%llu hp=%.3f\n",
                static_cast<unsigned long long>(result.instructions),
                vm.Global(*compiled.program, "clamped_hp").value_or(-1.0));
    return 0;
}
