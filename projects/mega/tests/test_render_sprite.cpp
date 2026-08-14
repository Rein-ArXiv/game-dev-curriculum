// 스프라이트 블릿 검증: ASCII→스프라이트, 알파 투명, 정수 스케일, 좌우반전, 클리핑.
#include "render/sprite.hpp"
#include "test_util.h"

#include <cstdio>
#include <fstream>

using namespace mega::render;

static bool Same(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static void TestMakeSprite() {
    // 3x2, 가운데만 빨강. 나머지는 투명.
    Sprite s = MakeSprite({"...", ".R."}, {{'R', {255, 0, 0, 255}}});
    CHECK_EQ(s.w, 3);
    CHECK_EQ(s.h, 2);
    CHECK_EQ((int)s.At(1, 1).a, 255);          // 'R' 불투명
    CHECK_EQ((int)s.At(0, 0).a, 0);            // '.' 투명
    CHECK(Same(s.At(1, 1), Color{255, 0, 0, 255}));
    // 팔레트에 없는 문자는 투명.
    Sprite u = MakeSprite({"X"}, {{'R', {1, 2, 3, 255}}});
    CHECK_EQ((int)u.At(0, 0).a, 0);
}

static void TestBlitBasic() {
    Framebuffer fb;
    fb.Init(8, 8);
    Color bg{0, 0, 0, 255};
    fb.Clear(bg);
    Sprite s = MakeSprite({".R.", "RRR", ".R."}, {{'R', {0, 200, 0, 255}}});  // + 모양
    BlitSprite(fb, s, 2, 2);
    CHECK(Same(fb.Get(3, 2), Color{0, 200, 0, 255}));   // 위 팔
    CHECK(Same(fb.Get(2, 3), Color{0, 200, 0, 255}));   // 왼 팔
    CHECK(Same(fb.Get(3, 3), Color{0, 200, 0, 255}));   // 중앙
    CHECK(Same(fb.Get(2, 2), bg));                      // 모서리(투명) → 배경 유지
    CHECK(Same(fb.Get(4, 4), bg));                      // 모서리(투명)
}

static void TestAlphaTransparency() {
    Framebuffer fb;
    fb.Init(4, 4);
    Color bg{50, 60, 70, 255};
    fb.Clear(bg);
    // 전부 투명인 스프라이트는 아무 것도 바꾸지 않는다.
    Sprite empty = MakeSprite({"..", ".."}, {{'R', {255, 0, 0, 255}}});
    BlitSprite(fb, empty, 0, 0);
    CHECK(Same(fb.Get(0, 0), bg));
    CHECK(Same(fb.Get(1, 1), bg));
}

static void TestScale() {
    Framebuffer fb;
    fb.Init(16, 16);
    fb.Clear({0, 0, 0, 255});
    Sprite dot = MakeSprite({"R"}, {{'R', {255, 255, 0, 255}}});  // 1x1
    BlitSprite(fb, dot, 3, 3, 4);                                 // 4배 → 4x4 블록
    Color y{255, 255, 0, 255};
    CHECK(Same(fb.Get(3, 3), y));      // 블록 좌상
    CHECK(Same(fb.Get(6, 6), y));      // 블록 우하(3+4-1)
    CHECK(Same(fb.Get(7, 7), Color{0, 0, 0, 255}));   // 블록 밖
    CHECK(Same(fb.Get(2, 2), Color{0, 0, 0, 255}));   // 블록 앞
}

static void TestFlipX() {
    Framebuffer fb;
    fb.Init(8, 4);
    fb.Clear({0, 0, 0, 255});
    // 왼쪽 열만 칠한 3x1. 반전하면 오른쪽 열만 찍혀야.
    Sprite s = MakeSprite({"R.."}, {{'R', {10, 20, 30, 255}}});
    BlitSpriteFlipX(fb, s, 0, 0);
    CHECK(Same(fb.Get(2, 0), Color{10, 20, 30, 255}));   // 반전 후 x=2 에 픽셀
    CHECK(Same(fb.Get(0, 0), Color{0, 0, 0, 255}));      // x=0 은 비어야
}

static void TestClipping() {
    Framebuffer fb;
    fb.Init(6, 6);
    Color bg{0, 0, 0, 255};
    fb.Clear(bg);
    Sprite s = MakeSprite({"RR", "RR"}, {{'R', {99, 0, 0, 255}}});
    BlitSprite(fb, s, -1, -1);          // 좌상 모서리 밖으로 → 일부만 보임(크래시 없음)
    CHECK(Same(fb.Get(0, 0), Color{99, 0, 0, 255}));   // (-1,-1)+px(1,1) = (0,0)
    BlitSprite(fb, s, 5, 5);            // 우하 모서리 밖 → (5,5) 한 픽셀만
    CHECK(Same(fb.Get(5, 5), Color{99, 0, 0, 255}));
    CHECK(true);                        // 경계 밖 접근에도 무사
}

static void TestBuiltins() {
    Sprite h = HeartSprite();
    CHECK_EQ(h.w, 7);
    CHECK_EQ(h.h, 6);
    CHECK_EQ((int)h.At(3, 5).a, 255);   // 하트 맨 아래 꼭짓점(가운데)
    Sprite c = CoinSprite();
    CHECK_EQ(c.w, 6);
    CHECK_EQ(c.h, 6);
    // 모서리는 투명, 가장자리는 노랑.
    CHECK_EQ((int)c.At(0, 0).a, 0);
    CHECK_EQ((int)c.At(1, 0).a, 255);
}

static void TestPpmAssetLoading() {
    const char* path = "test_sprite.ppm";
    {
        std::ofstream out(path);
        out << "P3\n# two pixels\n2 1\n255\n255 0 255   10 20 30\n";
    }
    Sprite sprite;
    CHECK(LoadPpmP3(path, sprite));
    CHECK_EQ(sprite.w, 2);
    CHECK_EQ(sprite.h, 1);
    CHECK_EQ((int)sprite.At(0, 0).a, 0);
    CHECK(Same(sprite.At(1, 0), Color{10, 20, 30, 255}));
    std::remove(path);

    {
        std::ofstream out(path);
        out << "P3\n999 1\n255\n";
    }
    CHECK(!LoadPpmP3(path, sprite));
    std::remove(path);
}

static void RunTests() {
    TestMakeSprite();
    TestBlitBasic();
    TestAlphaTransparency();
    TestScale();
    TestFlipX();
    TestClipping();
    TestBuiltins();
    TestPpmAssetLoading();
}

TEST_MAIN("render_sprite")
