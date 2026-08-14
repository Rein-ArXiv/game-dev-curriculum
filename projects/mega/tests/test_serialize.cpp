// BinaryArchive 검증: 라운드트립 항등성과 손상 입력 방어.
#include "serialize/archive.h"
#include "test_util.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace mega;

namespace {

struct Vec3 { float x, y, z; };   // trivially copyable → raw 경로

// 멤버 Serialize 를 가진 복합 타입(string/vector 포함).
struct SaveGame {
    uint32_t            version = 0;
    std::string         player;
    std::vector<int32_t> inventory;
    std::vector<Vec3>   waypoints;
    Vec3                spawn{};

    template <class Ar>
    void Serialize(Ar& ar) { ar(version, player, inventory, waypoints, spawn); }

    bool operator==(const SaveGame& o) const {
        if (version != o.version || player != o.player) return false;
        if (inventory != o.inventory) return false;
        if (waypoints.size() != o.waypoints.size()) return false;
        for (size_t i = 0; i < waypoints.size(); ++i) {
            const Vec3& a = waypoints[i]; const Vec3& b = o.waypoints[i];
            if (a.x != b.x || a.y != b.y || a.z != b.z) return false;
        }
        return spawn.x == o.spawn.x && spawn.y == o.spawn.y && spawn.z == o.spawn.z;
    }
};

// 중첩: Serialize 를 가진 타입을 멤버로.
struct World {
    std::string             name;
    std::vector<SaveGame>   slots;
    template <class Ar> void Serialize(Ar& ar) { ar(name, slots); }
};

}  // namespace

static void RunTests() {
    // --- POD 라운드트립 ---
    {
        Vec3 v{1.5f, -2.0f, 3.25f};
        auto bytes = SaveBinary(v);
        CHECK_EQ(bytes.size(), sizeof(Vec3));   // raw 경로: 정확히 12바이트
        Vec3 out{};
        CHECK(LoadBinary(out, bytes.data(), bytes.size()));
        CHECK_EQ(out.x, 1.5f);
        CHECK_EQ(out.y, -2.0f);
        CHECK_EQ(out.z, 3.25f);
    }

    // --- 복합 타입 라운드트립 ---
    {
        SaveGame g;
        g.version = 7;
        g.player  = "lunacat";
        g.inventory = {10, 20, 30, 40};
        g.waypoints = {{0,0,0}, {1,2,3}, {-4,-5,-6}};
        g.spawn = {100, 0, 100};

        auto bytes = SaveBinary(g);
        SaveGame loaded;
        CHECK(LoadBinary(loaded, bytes.data(), bytes.size()));
        CHECK(g == loaded);
    }

    // --- 빈 컨테이너/빈 문자열 ---
    {
        SaveGame g;             // 전부 기본값(빈 문자열, 빈 벡터)
        auto bytes = SaveBinary(g);
        SaveGame loaded;
        loaded.player = "garbage";       // 로드가 덮어써야 함
        loaded.inventory = {99};
        CHECK(LoadBinary(loaded, bytes.data(), bytes.size()));
        CHECK(g == loaded);
        CHECK(loaded.player.empty());
        CHECK(loaded.inventory.empty());
    }

    // --- 중첩(vector<Serialize 타입>) ---
    {
        World w;
        w.name = "campaign";
        SaveGame a; a.version = 1; a.player = "p1"; a.inventory = {1,2};
        SaveGame b; b.version = 2; b.player = "p2"; b.waypoints = {{9,9,9}};
        w.slots = {a, b};

        auto bytes = SaveBinary(w);
        World loaded;
        CHECK(LoadBinary(loaded, bytes.data(), bytes.size()));
        CHECK_EQ(loaded.name, std::string("campaign"));
        CHECK_EQ(loaded.slots.size(), (size_t)2);
        CHECK(loaded.slots[0] == a);
        CHECK(loaded.slots[1] == b);
    }

    // --- 손상/잘림 입력 방어: 끝을 잘라도 크래시 없이 Error ---
    {
        SaveGame g;
        g.version = 5; g.player = "abcdef"; g.inventory = {1,2,3};
        auto bytes = SaveBinary(g);
        // 절반만 넘기면 읽기가 경계를 넘어 Error 가 떠야 한다(UB·크래시 없이).
        SaveGame loaded;
        bool ok = LoadBinary(loaded, bytes.data(), bytes.size() / 2);
        CHECK(!ok);              // 잘린 입력은 실패로 보고
    }

    // --- 길이 헤더가 거대한 손상 입력: resize 폭주 없이 Error ---
    {
        // version(4) + player 길이=0xFFFFFFFF 만 담고 본문 없음.
        std::vector<uint8_t> bad;
        uint32_t version = 1, hugeLen = 0xFFFFFFFFu;
        const uint8_t* vp = reinterpret_cast<const uint8_t*>(&version);
        const uint8_t* hp = reinterpret_cast<const uint8_t*>(&hugeLen);
        bad.insert(bad.end(), vp, vp + 4);
        bad.insert(bad.end(), hp, hp + 4);
        SaveGame loaded;
        bool ok = LoadBinary(loaded, bad.data(), bad.size());
        CHECK(!ok);              // 본문 부족 → Error, 거대 resize 안 함
    }
}

TEST_MAIN("serialize")
