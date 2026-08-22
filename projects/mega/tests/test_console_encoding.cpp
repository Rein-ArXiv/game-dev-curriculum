// 진단이 읽히는가 — 소스 인코딩·콘솔 코드페이지·실제 진단 문자열의 바이트.
//
// 이 저장소의 실패 메시지는 전부 한국어다. 그 줄이 깨져 나오면 사람은 그것을
// 읽지 않고, 읽히지 않는 진단은 없는 진단이다. 그래서 인코딩은 표시 취향이
// 아니라 **진단의 도달성**이고, 검사 대상이다.
//
// 세 층을 따로 확인한다.
//   1. 컴파일러 — 소스의 한국어 리터럴이 UTF-8 바이트로 실행 파일에 들어갔는가.
//   2. 런타임   — EnableUtf8Console 이 콘솔 출력 코드페이지를 65001 로 만드는가.
//   3. 내용     — 제품 코드가 실제로 만들어 내는 진단 문자열이 유효한 UTF-8 인가.
#include "live/economy.hpp"
#include "live/event_schedule.hpp"
#include "live/feature_flag.hpp"
#include "platform/console.hpp"
#include "test_util.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mega::platform;

// UTF-8 로 유효한 바이트 열인가. 여기서 직접 세는 이유는, 이 판정을 표준
// 라이브러리에 맡기면 로케일에 따라 답이 달라지기 때문이다.
static bool ValidUtf8(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        size_t extra = 0;
        if (lead < 0x80) extra = 0;
        else if ((lead & 0xE0) == 0xC0) extra = 1;
        else if ((lead & 0xF0) == 0xE0) extra = 2;
        else if ((lead & 0xF8) == 0xF0) extra = 3;
        else return false;
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= text.size()) return false;
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

// ── 1. 컴파일러 ────────────────────────────────────────────────────
static void TestSourceLiteralsAreUtf8() {
    // "거부" 의 UTF-8 바이트는 EA B1 B0 EB B6 80 이다. CP949 로 좁혀졌다면
    // 네 바이트(B0 C5 BA CE)가 되어 이 비교가 깨진다.
    const std::string reject = "거부";
    CHECK_EQ(reject.size(), (size_t)6);
    CHECK_EQ(reject, std::string("\xEA\xB1\xB0\xEB\xB6\x80"));
    CHECK(ValidUtf8(reject));

    // 검사기 자체의 반례 — 아무 바이트나 통과시키면 위 단언이 의미가 없다.
    CHECK(!ValidUtf8(std::string("\xB0\xC5", 2)));       // CP949 조각
    CHECK(!ValidUtf8(std::string("\xEA\xB1", 2)));       // 잘린 3바이트 문자
    CHECK(ValidUtf8("ascii only"));
    CHECK(ValidUtf8(""));
}

// ── 2. 런타임 ──────────────────────────────────────────────────────
static void TestConsoleCodePage() {
    const unsigned before = ConsoleOutputCodePage();
#ifdef _WIN32
    // 깨지는 상태를 실제로 만들어 놓고 부른다. 이미 65001 인 콘솔에서만 재면
    // "아무것도 안 하는 구현"과 구분되지 않는다. 437 은 어느 Windows 에나 있다.
    if (before != 0u) {
        SetConsoleOutputCP(437u);
        CHECK_EQ(ConsoleOutputCodePage(), 437u);
    }
#endif
    CHECK(EnableUtf8Console());
    const unsigned after = ConsoleOutputCodePage();
    std::printf("  console cp: %u -> (437) -> %u\n", before, after);

    if (before == 0u) {
        // 콘솔이 없는 프로세스. 설정할 대상이 없고, 그 상태는 실패가 아니다.
        CHECK_EQ(after, 0u);
    } else {
        CHECK_EQ(after, kUtf8CodePage);
        // 멱등 — 두 번 불러도 같다(진입점이 여러 경로에서 부를 수 있다).
        CHECK(EnableUtf8Console());
        CHECK_EQ(ConsoleOutputCodePage(), kUtf8CodePage);
    }
}

// 표준 출력으로 나가는 바이트가 실제로 UTF-8 인가. 자기 stdout 을 읽을 수는
// 없으므로 같은 경로(stdio)로 파일에 적고 그 바이트를 되읽는다.
static void TestDiagnosticBytesOnTheWire() {
    const char* path = "mega_console_encoding_test.txt";
    const std::string line = "[content] balance.script 거부, 기본 구성으로 진행";
    std::FILE* out = std::fopen(path, "wb");
    CHECK(out != nullptr);
    if (!out) return;
    std::fprintf(out, "%s\n", line.c_str());
    std::fclose(out);

    std::FILE* in = std::fopen(path, "rb");
    CHECK(in != nullptr);
    if (!in) return;
    std::string read;
    char buffer[256];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), in)) > 0) read.append(buffer, got);
    std::fclose(in);
    std::remove(path);

    CHECK_EQ(read, line + "\n");
    CHECK(ValidUtf8(read));
}

// ── 3. 내용 ────────────────────────────────────────────────────────
// 제품 코드가 만들어 내는 진단이 전부 유효한 UTF-8 인가. 문자열 리터럴은 1번이
// 이미 확인했지만, 진단은 리터럴과 런타임 값(파일명·줄번호·계정 id)을 이어
// 붙여 만들어진다 — 그 이음매에서 잘린 바이트가 섞일 수 있다.
static void TestProducedDiagnosticsAreUtf8() {
    std::vector<std::string> diagnostics;
    std::string error;

    mega::live::EventSchedule schedule;
    const char* badSchedules[] = {
        "event 1 x 200 100 0 0\n",
        "event 1 x 100 200 0 0 scrap=9\n",
        "season 1 x 100 200 0 0\n",
    };
    for (const char* text : badSchedules) {
        CHECK(!mega::live::ParseEventSchedule(text, schedule, &error));
        diagnostics.push_back(error);
    }

    mega::live::FeatureFlags flags;
    CHECK(!mega::live::ParseFlagConfig("variant pathing poly astar rollout=250\n", flags, &error));
    diagnostics.push_back(error);
    CHECK(flags.Define("pathing", {"poly", "astar"}));
    CHECK(!flags.Force("pathing", "flowfeild", &error));
    diagnostics.push_back(error);
    CHECK(!flags.Define("나쁜이름", {"a"}, 100, &error));
    diagnostics.push_back(error);

    mega::live::Economy economy;
    mega::live::Wallet corrupt;
    corrupt.balance = 5; corrupt.sourced = 0; corrupt.sunk = 0;
    CHECK(!economy.Import(corrupt, &error));
    diagnostics.push_back(error);
    economy.Grant(1, 10);
    CHECK(economy.Spend(1, 20) == false);
    std::string violation;
    CHECK(economy.Conserved(&violation));

    for (const std::string& text : diagnostics) {
        CHECK(!text.empty());
        CHECK(ValidUtf8(text));
    }
    std::printf("  진단 %zu건 전부 UTF-8 유효. 예: %s\n", diagnostics.size(),
                diagnostics.front().c_str());
}

static void RunTests() {
    TestSourceLiteralsAreUtf8();
    TestConsoleCodePage();
    TestDiagnosticBytesOnTheWire();
    TestProducedDiagnosticsAreUtf8();
}

TEST_MAIN("console_encoding")
