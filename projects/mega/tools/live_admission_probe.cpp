// The HTTP/UDP smoke reuses the product codec instead of maintaining a second wire format.
#include "game/defense_content.hpp"
#include "game/defense_protocol.hpp"
#include "net/udp_transport.hpp"
#include "platform/detect.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    using namespace mega;
    namespace wire = game::wire;
    using Clock = std::chrono::steady_clock;
    unsigned port = 0;
    if (argc != 2) return 2;
    const std::string_view portText(argv[1]);
    const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size() ||
        port == 0 || port > 65535) return 2;

    // Credentials travel on stdin, not in command-line arguments or persistent files.
    std::array<std::string, 2> tokens;
    for (auto& token : tokens) {
        if (!std::getline(std::cin, token)) return 2;
        if (!token.empty() && token.back() == '\r') token.pop_back();
    }
    tools::AssetPipeline pipeline;
    std::string error;
    const auto assets = std::filesystem::path(argv[0]).parent_path() / "assets";
    const uint64_t hash = defense::CookAssets(
        assets.string(), pipeline, nullptr, nullptr, &error);
    if (!error.empty() || hash == 0) {
        std::fprintf(stderr, "content cook failed: %s\n", error.c_str());
        return 1;
    }
    std::array<net::UdpTransport, 2> clients;
    std::array<net::Bytes, 2> hellos;
    std::array<bool, 2> grants{}, snapshots{};
    for (size_t i = 0; i < clients.size(); ++i) {
        if (!clients[i].OpenClient("127.0.0.1", static_cast<uint16_t>(port))) return 1;
        hellos[i] = wire::WriteAdmissionHello(tokens[i], {}, hash,
            platform::OSCode(platform::CurrentOS()), platform::ArchCode(platform::CurrentArch()));
        if (hellos[i].empty()) return 2;
    }

    const auto deadline = Clock::now() + std::chrono::seconds(5);
    auto nextHello = Clock::time_point{};
    while (Clock::now() < deadline) {
        if (Clock::now() >= nextHello) {
            for (size_t i = 0; i < clients.size(); ++i) clients[i].Send(hellos[i]);
            nextHello = Clock::now() + std::chrono::milliseconds(50);
        }
        for (size_t i = 0; i < clients.size(); ++i) {
            net::Bytes packet;
            while (clients[i].Receive(packet)) {
                wire::AdmissionGrant grant;
                wire::Snapshot snapshot;
                if (wire::ReadAdmissionGrant(packet, grant)) {
                    if (grant.contentHash != hash || grant.contentVersion == 0) return 1;
                    grants[i] = true;
                } else if (wire::ReadSnapshot(packet, snapshot) && snapshot.tick > 0) {
                    snapshots[i] = true;
                }
            }
        }
        if (grants[0] && grants[1] && snapshots[0] && snapshots[1]) {
            std::printf("live admission: two grants and two advancing snapshots verified\n");
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::fprintf(stderr, "admission timeout: grants=%d,%d snapshots=%d,%d\n",
                 grants[0], grants[1], snapshots[0], snapshots[1]);
    return 1;
}
