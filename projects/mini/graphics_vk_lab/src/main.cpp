// graphics_vk_lab — Vulkan 측정 lab.

#include "app/app.h"

#include <cstdio>
#include <exception>

int main() {
    try {
        vklab::App app;
        return app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[vklab fatal] %s\n", e.what());
        return 1;
    }
}
