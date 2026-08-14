#include "network_types.h"
#include "server_runtime.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

std::atomic<bool> gRunning{true};

void OnSignal(int) {
    gRunning.store(false);
}

std::uint32_t ParseUnsigned(const char* text, std::uint32_t fallback) {
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) return fallback;
    return static_cast<std::uint32_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    gnl::ServerConfig config;
    std::uint32_t durationSeconds = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view option = argv[i];
        std::uint32_t value = ParseUnsigned(argv[i + 1], 0);
        if (option == "--tcp-port" && value <= 65535) {
            config.tcpPort = static_cast<std::uint16_t>(value);
        } else if (option == "--udp-port" && value <= 65535) {
            config.udpPort = static_cast<std::uint16_t>(value);
        } else if (option == "--workers") {
            config.workerCount = value;
        } else if (option == "--duration") {
            durationSeconds = value;
        }
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    auto backend = gnl::CreatePlatformServer();
    std::string error;
    if (!backend->Start(config, error)) {
        std::cerr << "game_server: " << error << '\n';
        return 1;
    }

    std::cout << "game_server: TCP " << config.tcpPort
              << ", UDP " << config.udpPort << '\n';
    gnl::GameServerRuntime runtime(*backend);
    auto started = std::chrono::steady_clock::now();
    auto nextReport = started + std::chrono::seconds(1);

    while (gRunning.load()) {
        std::vector<gnl::NetworkEvent> events;
        backend->Poll(events, std::chrono::milliseconds(2));
        for (const auto& event : events) runtime.OnNetworkEvent(event);

        auto now = std::chrono::steady_clock::now();
        runtime.Advance(now);
        if (now >= nextReport) {
            const auto& metrics = runtime.Metrics();
            std::cout << "clients=" << runtime.ClientCount()
                      << " ticks=" << metrics.ticks
                      << " control=" << metrics.controlFrames
                      << " datagrams=" << metrics.datagrams
                      << " snapshots=" << metrics.snapshots
                      << " rejected=" << metrics.rejectedPackets << '\n';
            nextReport = now + std::chrono::seconds(1);
        }
        if (durationSeconds > 0 &&
            now - started >= std::chrono::seconds(durationSeconds)) break;
    }

    backend->Stop();
    return 0;
}

