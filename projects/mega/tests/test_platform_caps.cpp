// 능력 플래그·타깃 프로파일 검증.
#include "platform/capability.hpp"
#include "platform/detect.hpp"
#include "test_util.h"

#include <string>

using namespace mega::platform;

static void TestPcProfile() {
    PlatformProfile pc = profiles::PC();
    CHECK(pc.Has(Capability::Keyboard));
    CHECK(pc.Has(Capability::Mouse));
    CHECK(pc.Has(Capability::Filesystem));
    CHECK(pc.Has(Capability::Threads));
    CHECK(!pc.Has(Capability::Touch));        // PC 는 터치 없음
    CHECK(pc.memoryBudgetMB > 0);
}

static void TestMobileProfile() {
    PlatformProfile m = profiles::Mobile();
    CHECK(m.Has(Capability::Touch));          // 모바일은 터치
    CHECK(!m.Has(Capability::Mouse));         // 마우스 없음
    CHECK(m.Has(Capability::Gamepad));
    CHECK(m.memoryBudgetMB < profiles::PC().memoryBudgetMB);   // 예산 작음
}

static void TestWebProfile() {
    PlatformProfile w = profiles::Web();
    CHECK(!w.Has(Capability::Filesystem));    // 웹은 임의 파일 접근 제한
    CHECK(!w.Has(Capability::Threads));       // 자유 스레드 제한
    CHECK(w.Has(Capability::Network));
    CHECK(w.Has(Capability::GPU));
}

static void TestConsoleProfile() {
    PlatformProfile c = profiles::Console();
    CHECK(c.Has(Capability::Gamepad));
    CHECK(!c.Has(Capability::Keyboard));      // 콘솔은 키보드 기본 아님
    CHECK(c.memoryBudgetMB >= profiles::PC().memoryBudgetMB);   // 콘솔 메모리 넉넉
}

static void TestProfileForOS() {
    CHECK(std::string(ProfileFor(OS::Windows).name) == "PC");
    CHECK(std::string(ProfileFor(OS::Android).name) == "Mobile");
    CHECK(std::string(ProfileFor(OS::iOS).name) == "Mobile");
    CHECK(std::string(ProfileFor(OS::Web).name) == "Web");

    // 현재 플랫폼 프로파일은 유효(이 머신=PC).
    PlatformProfile here = ProfileFor(CurrentOS());
    CHECK(here.maxThreads >= 1);
    CHECK(here.memoryBudgetMB > 0);
}

static void RunTests() {
    TestPcProfile();
    TestMobileProfile();
    TestWebProfile();
    TestConsoleProfile();
    TestProfileForOS();
}

TEST_MAIN("platform_caps")
