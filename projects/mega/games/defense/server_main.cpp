// Headless authoritative Mega Defense server -- entry point only.
//
// 서버 루프는 mega_core 의 game::DefenseServer 가 갖는다. 소켓은 net::UdpServer,
// 종료 신호는 platform::InstallShutdownSignals, 에셋 경로·환경 변수는
// platform:: 이 담당한다 — games/ 는 OS API 를 직접 부르지 않는다(계층 규칙 A3).

#include "game/defense_server.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    mega::game::DefenseServerOptions options;
    if (!mega::game::ParseDefenseServerOptions(argc, argv, options)) {
        std::fprintf(stderr, "%s", mega::game::DefenseServerUsage());
        return 2;
    }
    return mega::game::RunDefenseServer(options);
}
