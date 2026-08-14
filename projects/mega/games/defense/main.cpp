// mega_defense -- final playable product entry point.
//
//   mega_defense                         solo
//   mega_defense --host [port]           authoritative host
//   mega_defense --join <ipv4> [port]    remote cooperative client
//   mega_defense --join <ipv4> [port] --token <game-token>
//                                          dedicated-server client
//   mega_defense --selftest [ticks]      headless run of the real frame loop
//
// 이 파일에는 게임 로직이 한 줄도 없고, OS API 호출도 없다(계층 규칙 A3).
// 프레임 루프는 mega_core 의 game::DefenseApp::Step() 이고, 창·소켓·오디오·
// 파일은 각각 platform/ · net/ · io/ 가 소유한다. 그래서 헤드리스 셀프테스트가
// 창 없이 **제품의 진짜 프레임 루프**를 그대로 구동할 수 있다.
//
// 진입점이 얇아야 하는 이유는 미학이 아니다. 여기가 두꺼우면 여기서만 도달
// 가능한 코드가 생기고, 그 코드를 검증하는 테스트는 존재할 수 없게 된다.

#include "game/defense_app.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    mega::game::DefenseOptions options;
    if (!mega::game::ParseDefenseOptions(argc, argv, options)) {
        std::fprintf(stderr, "%s", mega::game::DefenseUsage());
        return 2;
    }
    return mega::game::RunDefense(options);
}
