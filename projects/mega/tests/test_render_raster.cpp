// 소프트웨어 래스터라이저 검증: 프레임버퍼·프리미티브를 픽셀 단위로.
#include "render/raster2d.hpp"
#include "test_util.h"

using namespace mega::render;

static bool Same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static void TestClearAndSet() {
    Framebuffer fb;
    fb.Init(16, 16);
    Color bg{10, 20, 30, 255};
    fb.Clear(bg);
    CHECK(Same(fb.Get(0, 0), bg));
    CHECK(Same(fb.Get(15, 15), bg));

    Color red{255, 0, 0, 255};
    fb.Set(5, 5, red);
    CHECK(Same(fb.Get(5, 5), red));
    CHECK(Same(fb.Get(5, 6), bg));         // 이웃은 그대로

    // 경계 밖 set 은 무시(크래시 없음).
    fb.Set(-1, -1, red);
    fb.Set(100, 100, red);
    CHECK(true);
}

static void TestFillRect() {
    Framebuffer fb;
    fb.Init(20, 20);
    fb.Clear({0, 0, 0, 255});
    Color g{0, 255, 0, 255};
    FillRect(fb, 5, 5, 6, 4, g);
    CHECK(Same(fb.Get(5, 5), g));          // 좌상 모서리
    CHECK(Same(fb.Get(10, 8), g));         // 우하 모서리(5+6-1, 5+4-1)
    CHECK(Same(fb.Get(11, 8), Color{0, 0, 0, 255}));   // 바로 밖
    CHECK(Same(fb.Get(4, 5), Color{0, 0, 0, 255}));
}

static void TestFillRectBevel() {
    Framebuffer fb;
    fb.Init(20, 20);
    fb.Clear({0, 0, 0, 255});
    Color base{100, 100, 100, 255};
    FillRectBevel(fb, 4, 4, 8, 6, base, 50);
    Color tl = fb.Get(4, 4);          // 위/왼 모서리 = 밝음
    Color br = fb.Get(11, 9);         // 아래/오른 모서리(4+8-1, 4+6-1) = 어두움
    Color mid = fb.Get(7, 6);         // 내부 = 밑색
    CHECK(tl.r > base.r);             // 밝아짐(150)
    CHECK(br.r < base.r);             // 어두워짐(50)
    CHECK(Same(mid, base));           // 내부는 밑색 그대로
}

static void TestFillCircle() {
    Framebuffer fb;
    fb.Init(40, 40);
    fb.Clear({0, 0, 0, 255});
    Color w{255, 255, 255, 255};
    FillCircle(fb, 20, 20, 8, w);
    CHECK(Same(fb.Get(20, 20), w));        // 중심
    CHECK(Same(fb.Get(27, 20), w));        // 반지름 7 < 8 안
    CHECK(Same(fb.Get(20, 12), w));        // 위쪽 가장자리(거리 8)
    CHECK(Same(fb.Get(30, 30), Color{0, 0, 0, 255}));   // 밖(거리>8)
}

static void TestDrawLine() {
    Framebuffer fb;
    fb.Init(20, 20);
    fb.Clear({0, 0, 0, 255});
    Color c{255, 255, 0, 255};
    DrawLine(fb, 2, 2, 17, 2, c);          // 수평선
    CHECK(Same(fb.Get(2, 2), c));
    CHECK(Same(fb.Get(10, 2), c));
    CHECK(Same(fb.Get(17, 2), c));
    CHECK(Same(fb.Get(10, 3), Color{0, 0, 0, 255}));

    DrawLine(fb, 0, 0, 9, 9, c);           // 대각선
    CHECK(Same(fb.Get(0, 0), c));
    CHECK(Same(fb.Get(5, 5), c));
    CHECK(Same(fb.Get(9, 9), c));
}

static void TestFillTriangle() {
    Framebuffer fb;
    fb.Init(30, 30);
    fb.Clear({0, 0, 0, 255});
    Color b{0, 0, 255, 255};
    FillTriangle(fb, 5, 5, 25, 5, 5, 25, b);   // 직각 삼각형
    CHECK(Same(fb.Get(6, 6), b));              // 내부 근처
    CHECK(Same(fb.Get(8, 8), b));              // 내부(빗변 안)
    CHECK(Same(fb.Get(20, 20), Color{0, 0, 0, 255}));   // 빗변 밖
    CHECK(Same(fb.Get(5, 5), b));              // 꼭짓점
}

static void TestBlend() {
    Framebuffer fb;
    fb.Init(8, 8);
    fb.Clear({0, 0, 0, 255});
    fb.Blend(0, 0, Color{255, 255, 255, 128});  // 50% 흰색 over 검정
    Color got = fb.Get(0, 0);
    CHECK(got.r > 100 && got.r < 160);          // ~127
    // 완전 투명은 변화 없음.
    fb.Blend(1, 1, Color{255, 0, 0, 0});
    CHECK(Same(fb.Get(1, 1), Color{0, 0, 0, 255}));
}

static void TestPPM() {
    Framebuffer fb;
    fb.Init(4, 4);
    fb.Clear({1, 2, 3, 255});
    CHECK(fb.SavePPM("mega_raster_test.ppm"));   // 쓰기 성공
    std::remove("mega_raster_test.ppm");
}

static void RunTests() {
    TestClearAndSet();
    TestFillRect();
    TestFillRectBevel();
    TestFillCircle();
    TestDrawLine();
    TestFillTriangle();
    TestBlend();
    TestPPM();
}

TEST_MAIN("render_raster")
