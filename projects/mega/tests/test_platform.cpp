// 플랫폼 검증: 감지·엔디안 정규화·가상 파일시스템.
#include "platform/byteorder.hpp"
#include "platform/detect.hpp"
#include "platform/vfs.hpp"
#include "test_util.h"

#include <cstdint>
#include <string>

using namespace mega::platform;

static void TestDetect() {
    // 이 머신에서 알려진 값(컴파일 타임).
    CHECK(CurrentOS() != OS::Unknown);
    CHECK(CurrentArch() != Arch::Unknown);
    // 이름이 비지 않음.
    CHECK(std::string(OSName(CurrentOS())) != "Unknown");
    // 엔디안 enum 은 둘 중 하나.
    CHECK(CurrentEndian() == Endian::Little || CurrentEndian() == Endian::Big);
}

static void TestByteSwap() {
    CHECK_EQ(Swap16(0x1234), (uint16_t)0x3412);
    CHECK_EQ(Swap32(0x12345678u), 0x78563412u);
    CHECK_EQ(Swap64(0x0102030405060708ull), 0x0807060504030201ull);
    // 두 번 스왑 = 원본.
    CHECK_EQ(Swap32(Swap32(0xDEADBEEFu)), 0xDEADBEEFu);
}

static void TestLittleEndianBytes() {
    // WriteLE/ReadLE 는 플랫폼 엔디안 무관하게 정해진 바이트 순서.
    uint8_t buf[4];
    WriteLE32(buf, 0x12345678u);
    CHECK_EQ(buf[0], (uint8_t)0x78);   // little: 최하위 바이트 먼저
    CHECK_EQ(buf[1], (uint8_t)0x56);
    CHECK_EQ(buf[2], (uint8_t)0x34);
    CHECK_EQ(buf[3], (uint8_t)0x12);
    CHECK_EQ(ReadLE32(buf), 0x12345678u);   // 왕복

    uint8_t b2[8];
    WriteLE64(b2, 0x0102030405060708ull);
    CHECK_EQ(ReadLE64(b2), 0x0102030405060708ull);

    // host↔LE 왕복(네이티브 엔디안 무관 항등).
    CHECK_EQ(LEToHost32(HostToLE32(0xCAFEBABEu)), 0xCAFEBABEu);
}

static void TestNormalize() {
    CHECK_EQ(Vfs::Normalize("a/b/c"), std::string("a/b/c"));
    CHECK_EQ(Vfs::Normalize("a/./b"), std::string("a/b"));        // . 제거
    CHECK_EQ(Vfs::Normalize("a//b"), std::string("a/b"));         // 중복 슬래시
    CHECK_EQ(Vfs::Normalize("a\\b\\c"), std::string("a/b/c"));    // 역슬래시 통일
    CHECK_EQ(Vfs::Normalize("a/b/../c"), std::string("a/c"));     // .. 상위
    CHECK_EQ(Vfs::Normalize("a/b/../../c"), std::string("c"));    // 다중 ..
    CHECK_EQ(Vfs::Normalize("../../x"), std::string("x"));        // 루트 탈출 차단(클램프)
    CHECK_EQ(Vfs::Normalize("/abs/path/"), std::string("abs/path"));
}

static void TestVfsMount() {
    Vfs vfs;
    vfs.Mount("assets", "/game/data");
    vfs.Mount("user", "/home/save");

    CHECK_EQ(vfs.Resolve("assets/maps/x.txt"), std::string("/game/data/maps/x.txt"));
    CHECK_EQ(vfs.Resolve("user/profile.dat"), std::string("/home/save/profile.dat"));
    CHECK_EQ(vfs.Resolve("assets"), std::string("/game/data"));
    // 정규화 후 해석: assets/./maps/../maps/x → assets/maps/x.
    CHECK_EQ(vfs.Resolve("assets/./maps/../maps/x.txt"), std::string("/game/data/maps/x.txt"));
    // 마운트 없는 경로는 정규화만.
    CHECK_EQ(vfs.Resolve("misc/file"), std::string("misc/file"));
}

static void TestVfsFiles() {
    Vfs vfs;
    vfs.Mount("assets", "/game/data");
    vfs.AddFile("/game/data/maps/level1.txt", "S....G");

    CHECK(vfs.Exists("assets/maps/level1.txt"));
    CHECK(!vfs.Exists("assets/maps/missing.txt"));
    std::string content;
    CHECK(vfs.Read("assets/maps/level1.txt", content));
    CHECK_EQ(content, std::string("S....G"));
    // 디렉토리 탈출 시도는 정규화로 차단(.. 가 루트 위로 못 감).
    CHECK(!vfs.Read("assets/../../../etc/passwd", content));
}

static void RunTests() {
    TestDetect();
    TestByteSwap();
    TestLittleEndianBytes();
    TestNormalize();
    TestVfsMount();
    TestVfsFiles();
}

TEST_MAIN("platform")
