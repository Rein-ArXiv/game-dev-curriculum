// 델타 스냅샷: 기준선은 언제나 클라이언트가 실제로 가진 것이어야 한다.
//
// 이 파일이 지키는 성질은 셋이다.
//   1. 복원이 **비트 동일**이다. 델타는 압축이지 근사가 아니다.
//   2. 호스트가 고른 기준선은 클라이언트가 그 순간 실제로 들고 있는 것이다.
//      손실·재정렬 아래에서도, 시퀀스가 한 바퀴 돈 뒤에도.
//   3. 기준선이 보관 창에서 밀려나면 즉시 풀 스냅샷으로 되돌아간다.
//
// 페이로드는 합성 바이트가 아니라 **제품 프로토콜이 실제로 내는 스냅샷**이다.
// 델타의 이득은 "무엇이 안 바뀌는가"에 달려 있고, 그것은 인코더가 필드를 어떤
// 순서로 늘어놓는가의 함수다. 합성 데이터로 재면 그 사실이 측정에서 사라진다.

#include "game/defense_map.hpp"
#include "game/defense_protocol.hpp"
#include "game/game.hpp"
#include "net/packet.hpp"
#include "net/transport.hpp"
#include "test_util.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

using namespace mega;

// ── 1. varint ──────────────────────────────────────────────────────

static void TestVarint() {
    const uint32_t values[] = {0u, 1u, 127u, 128u, 300u, 16383u, 16384u,
                               0x0FFFFFFFu, 0xFFFFFFFFu};
    for (uint32_t value : values) {
        net::Bytes bytes;
        net::PutVarint(bytes, value);
        const uint8_t* cursor = bytes.data();
        const uint8_t* end = cursor + bytes.size();
        uint32_t decoded = 0;
        CHECK(net::GetVarint(cursor, end, decoded));
        CHECK_EQ(decoded, value);
        CHECK(cursor == end);
    }

    // 잘린 연속 바이트는 거부.
    net::Bytes truncated{0x80};
    const uint8_t* cursor = truncated.data();
    uint32_t decoded = 0;
    CHECK(!net::GetVarint(cursor, truncated.data() + truncated.size(), decoded));

    // 32비트를 넘는 다섯 번째 바이트는 거부.
    net::Bytes overflow{0x80, 0x80, 0x80, 0x80, 0x10};
    cursor = overflow.data();
    CHECK(!net::GetVarint(cursor, overflow.data() + overflow.size(), decoded));
}

// ── 2. 델타 코덱 ───────────────────────────────────────────────────

static net::Bytes RandomBytes(uint32_t& state, size_t count) {
    net::Bytes out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        out.push_back(static_cast<uint8_t>(state & 0xFF));
    }
    return out;
}

static void TestDeltaRoundTrip() {
    uint32_t state = 0x1234567u;
    for (int trial = 0; trial < 200; ++trial) {
        const size_t baseSize = static_cast<size_t>(trial * 3 % 200);
        const size_t currentSize = static_cast<size_t>(trial * 7 % 250);
        net::Bytes base = RandomBytes(state, baseSize);
        net::Bytes current = base;
        current.resize(currentSize);
        // 일부 바이트만 바꾼다 — 실제 스냅샷이 그렇다.
        for (size_t i = 0; i < current.size(); i += 5) current[i] ^= 0x5Au;

        net::Bytes delta = net::EncodeDelta(base, current);
        net::Bytes restored;
        CHECK(net::ApplyDelta(base, delta, restored));
        CHECK(restored == current);            // 비트 동일
    }

    // 완전히 같으면 델타는 길이 + 한 세그먼트뿐이다.
    net::Bytes same = RandomBytes(state, 512);
    net::Bytes identical = net::EncodeDelta(same, same);
    net::Bytes restored;
    CHECK(net::ApplyDelta(same, identical, restored));
    CHECK(restored == same);
    CHECK(identical.size() <= 6);

    // 빈 것끼리, 빈 기준선에서 자라기.
    net::Bytes empty;
    CHECK(net::ApplyDelta(empty, net::EncodeDelta(empty, empty), restored));
    CHECK(restored.empty());
    net::Bytes grown = RandomBytes(state, 64);
    CHECK(net::ApplyDelta(empty, net::EncodeDelta(empty, grown), restored));
    CHECK(restored == grown);
}

static void TestDeltaRejectsMalformed() {
    uint32_t state = 0xABCDEFu;
    net::Bytes base = RandomBytes(state, 128);
    net::Bytes current = RandomBytes(state, 128);
    const net::Bytes delta = net::EncodeDelta(base, current);
    net::Bytes out;
    CHECK(net::ApplyDelta(base, delta, out));

    net::Bytes surplus = delta;
    surplus.push_back(0);
    CHECK(!net::ApplyDelta(base, surplus, out));      // 잉여 바이트

    net::Bytes truncated = delta;
    truncated.pop_back();
    CHECK(!net::ApplyDelta(base, truncated, out));    // 모자란 바이트

    CHECK(!net::ApplyDelta(base, net::Bytes{}, out)); // 길이조차 없다

    // 전진하지 않는 세그먼트(same=0, copy=0)는 무한 루프의 미끼다.
    net::Bytes stuck;
    net::PutVarint(stuck, 8);
    net::PutVarint(stuck, 0);
    net::PutVarint(stuck, 0);
    CHECK(!net::ApplyDelta(base, stuck, out));

    // 선언한 총 길이보다 많이 복사하려는 세그먼트.
    net::Bytes overrun;
    net::PutVarint(overrun, 4);
    net::PutVarint(overrun, 0);
    net::PutVarint(overrun, 9);
    for (int i = 0; i < 9; ++i) overrun.push_back(0x11);
    CHECK(!net::ApplyDelta(base, overrun, out));
}

// ── 3. ack 창과 순환 ───────────────────────────────────────────────

static void TestAckWindow() {
    // wrap 비교 자체. 이 두 줄이 무너지면 아래 배선 검사가 전부 의미를 잃는다.
    CHECK(net::SeqGreater(0, 65535));
    CHECK(!net::SeqGreater(65535, 0));
    CHECK_EQ(net::SeqDiff(0, 65535), 1);
    CHECK_EQ(net::SeqDiff(65535, 0), -1);

    net::AckWindow window;
    CHECK(!window.Started());
    window.OnReceived(10);
    CHECK(window.Started());
    CHECK_EQ(window.Ack(), (uint16_t)10);
    CHECK(window.Seen(10));
    CHECK(!window.Seen(9));

    window.OnReceived(11);
    window.OnReceived(13);            // 12 는 아직 안 왔다
    CHECK_EQ(window.Ack(), (uint16_t)13);
    CHECK(window.Seen(11));
    CHECK(!window.Seen(12));
    window.OnReceived(12);            // 재정렬 도착 → 구멍이 메워진다
    CHECK(window.Seen(12));
    CHECK_EQ(window.Ack(), (uint16_t)13);   // 과거 도착은 최신을 되돌리지 않는다

    // 32칸을 건너뛰면 옛 비트는 창 밖이다.
    window.OnReceived(200);
    CHECK_EQ(window.Ack(), (uint16_t)200);
    CHECK(!window.Seen(13));
    CHECK_EQ(window.AckBits(), 0u);

    // 순환. 65530..65535 다음에 0..5 가 온다.
    net::AckWindow wrapped;
    for (int i = 0; i < 12; ++i) wrapped.OnReceived(static_cast<uint16_t>(65530 + i));
    CHECK_EQ(wrapped.Ack(), (uint16_t)5);
    CHECK(wrapped.Seen(65535));
    CHECK(wrapped.Seen(0));
    CHECK(wrapped.Seen(65530));

    // 헤더가 확인해 주는 범위.
    const net::PacketHeader header = wrapped.Header(77);
    CHECK_EQ(header.seq, (uint16_t)77);
    CHECK(net::HeaderAcknowledges(header, 5));
    CHECK(net::HeaderAcknowledges(header, 65530));
    CHECK(!net::HeaderAcknowledges(header, 6));       // 아직 안 온 미래
    CHECK(!net::HeaderAcknowledges(header, 65000));   // 창보다 오래된 과거
}

static void TestSequenceBufferEvicts() {
    net::SequenceBuffer<int> buffer(8);
    CHECK_EQ(buffer.Capacity(), (size_t)8);
    for (int i = 0; i < 8; ++i) buffer.Insert(static_cast<uint16_t>(i), i);
    CHECK_EQ(buffer.Size(), (size_t)8);
    CHECK_EQ(buffer.Evicted(), (uint64_t)0);

    buffer.Insert(8, 8);                       // 슬롯 0 을 덮는다
    CHECK_EQ(buffer.Evicted(), (uint64_t)1);
    CHECK(buffer.Find(0) == nullptr);          // 밀려났다
    CHECK(buffer.Find(8) != nullptr);
    CHECK_EQ(*buffer.Find(8), 8);
    CHECK_EQ(buffer.Size(), (size_t)8);        // 용량은 고정

    // 순회는 슬롯 번호 오름차순이다(결정적).
    std::vector<uint16_t> order;
    buffer.ForEach([&](uint16_t seq, const int&) { order.push_back(seq); });
    CHECK_EQ(order.size(), (size_t)8);
    for (size_t i = 1; i < order.size(); ++i)
        CHECK((order[i] & 7) > (order[i - 1] & 7));
}

// ── 4. 호스트/클라이언트 왕복 ──────────────────────────────────────
//
// 제품이 하게 될 것과 같은 모양의 최소 하네스다. 호스트는 매 스냅샷마다
// 시퀀스를 하나 쓰고, 기준선이 있으면 차분을, 없으면 전체를 싣는다. 클라이언트는
// 받은 seq 를 ack 로 접어 돌려보내고, 자기가 복원한 스냅샷을 seq 로 보관한다.

static constexpr uint8_t kFull = 0;
static constexpr uint8_t kDelta = 1;

struct Link {          // 결정적 손실·재정렬 파이프
    struct Item { net::Bytes bytes; int deliverTick; };
    std::vector<Item> pipe;
    std::vector<net::Bytes> inbox;
    float loss = 0.0f;
    float reorder = 0.0f;
    int latency = 1;
    uint32_t rng = 0x9E3779B9u;

    float Rand01() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float>(rng >> 8) * (1.0f / 16777216.0f);
    }
    void Send(int tick, const net::Bytes& bytes) {
        if (loss > 0.0f && Rand01() < loss) return;
        int extra = 0;
        if (reorder > 0.0f && Rand01() < reorder) extra = 1 + static_cast<int>(Rand01() * 3.0f);
        pipe.push_back({bytes, tick + latency + extra});
    }
    void Pump(int tick) {
        std::vector<Item> remain;
        for (Item& item : pipe) {
            if (item.deliverTick <= tick) inbox.push_back(std::move(item.bytes));
            else remain.push_back(std::move(item));
        }
        pipe.swap(remain);
    }
    bool Recv(net::Bytes& out) {
        if (inbox.empty()) return false;
        out = std::move(inbox.front());
        inbox.erase(inbox.begin());
        return true;
    }
};

struct Report {
    int sent = 0;
    int full = 0;
    int delta = 0;
    int received = 0;
    int mismatched = 0;         // 복원 결과가 원본과 다르다 (0 이어야 한다)
    int unreconstructable = 0;  // 호스트가 클라가 없는 기준선을 가리켰다 (0 이어야 한다)
    int deltaAfterWrap = 0;
    size_t fullBytes = 0;
    size_t wireBytes = 0;
    uint64_t baselineEvictions = 0;
};

static void PutU16(net::Bytes& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
static uint16_t GetU16(const net::Bytes& bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

static void RunReplication(Report& report, float lossHostToClient, float lossClientToHost,
                           float reorder, int ticks, uint16_t firstSeq,
                           size_t baselineCapacity, size_t clientCapacity) {
    game::DefenseMap map = game::DefenseMap::CreateDefault();
    game::GameWorld world;
    world.Init(map, {game::WaveDef{6, 12, 22, 6, 0.12f}}, 400, 100);
    world.PlaceTowerType(map.CellCenter(2, 2), game::TowerType::Gatling);
    world.PlaceTowerType(map.CellCenter(4, 4), game::TowerType::Cannon);
    world.PlaceTowerType(map.CellCenter(6, 6), game::TowerType::Splash);

    net::PacketSequencer hostSeq;
    hostSeq.Reset(firstSeq);
    net::AckWindow hostWindow;
    net::BaselineStore baselines(baselineCapacity);

    net::PacketSequencer clientSeq;
    net::AckWindow clientWindow;
    net::SequenceBuffer<net::Bytes> clientHas(clientCapacity);

    Link downstream, upstream;
    downstream.loss = lossHostToClient;
    downstream.reorder = reorder;
    downstream.rng = 0x1B873593u;
    upstream.loss = lossClientToHost;
    upstream.reorder = reorder;
    upstream.rng = 0xCC9E2D51u;

    std::map<uint16_t, net::Bytes> truth;   // 호스트가 그 seq 에 실은 전체 스냅샷
    bool wrapped = false;

    for (int tick = 0; tick < ticks; ++tick) {
        world.Step(1.0f / 60.0f);

        // ── 호스트 송신 ──
        const net::Bytes full = game::wire::WriteSnapshot(world, static_cast<uint32_t>(tick), 0);
        CHECK(!full.empty());
        uint16_t baselineSeq = 0;
        const net::BaselineStore::Entry* baseline = baselines.Baseline(&baselineSeq);

        net::Bytes body;
        if (baseline) {
            body.push_back(kDelta);
            PutU16(body, baselineSeq);
            const net::Bytes delta = net::EncodeDelta(baseline->bytes, full);
            body.insert(body.end(), delta.begin(), delta.end());
            ++report.delta;
            if (wrapped) ++report.deltaAfterWrap;
        } else {
            body.push_back(kFull);
            body.insert(body.end(), full.begin(), full.end());
            ++report.full;
        }
        const uint16_t seq = hostSeq.Next();
        if (seq < firstSeq) wrapped = true;
        truth[seq] = full;
        baselines.Sent(seq, static_cast<uint32_t>(tick), full);
        const net::Bytes packet = net::WritePacket(hostWindow.Header(seq), body);
        downstream.Send(tick, packet);
        ++report.sent;
        report.fullBytes += full.size();
        report.wireBytes += packet.size();

        downstream.Pump(tick);
        upstream.Pump(tick);

        // ── 클라이언트 수신 ──
        net::Bytes raw;
        while (downstream.Recv(raw)) {
            net::PacketHeader header;
            net::Bytes payload;
            CHECK(net::ReadPacket(raw, header, payload));
            clientWindow.OnReceived(header.seq);
            CHECK(!payload.empty());
            net::Bytes restored;
            if (payload[0] == kFull) {
                restored.assign(payload.begin() + 1, payload.end());
            } else {
                CHECK(payload.size() >= 3);
                const uint16_t baseSeq = GetU16(payload, 1);
                const net::Bytes* held = clientHas.Find(baseSeq);
                if (!held) { ++report.unreconstructable; continue; }
                const net::Bytes delta(payload.begin() + 3, payload.end());
                if (!net::ApplyDelta(*held, delta, restored)) { ++report.mismatched; continue; }
            }
            auto expected = truth.find(header.seq);
            CHECK(expected != truth.end());
            if (restored != expected->second) ++report.mismatched;
            clientHas.Insert(header.seq, restored);
            ++report.received;
        }

        // ── 클라이언트 송신(빈 페이로드. 헤더의 ack 만이 화물이다) ──
        upstream.Send(tick, net::WritePacket(clientWindow.Header(clientSeq.Next()), {}));

        // ── 호스트 수신 ──
        while (upstream.Recv(raw)) {
            net::PacketHeader header;
            net::Bytes payload;
            CHECK(net::ReadPacket(raw, header, payload));
            hostWindow.OnReceived(header.seq);
            baselines.Acknowledge(header);
        }
    }
    report.baselineEvictions = baselines.Evicted();
}

static void TestLossyReplication() {
    Report report;
    RunReplication(report, 0.35f, 0.35f, 0.25f, 400, 0, 32, 64);
    std::printf("  손실 35%%: 전송 %d (full %d / delta %d) 수신 %d "
                "재구성불가 %d 불일치 %d\n",
                report.sent, report.full, report.delta, report.received,
                report.unreconstructable, report.mismatched);
    std::printf("  스냅샷 원본 %zu B → 와이어 %zu B (%.1f%%)\n",
                report.fullBytes, report.wireBytes,
                100.0 * static_cast<double>(report.wireBytes) /
                    static_cast<double>(report.fullBytes));

    CHECK_EQ(report.unreconstructable, 0);   // 기준선은 언제나 클라가 가진 것
    CHECK_EQ(report.mismatched, 0);          // 복원은 비트 동일
    CHECK(report.received > 200);            // 35% 손실이라도 대부분 도착한다
    CHECK(report.delta > report.full);       // 델타가 주 경로다
    CHECK(report.full > 0);                  // 초반과 손실 구간은 전체를 보낸다
    CHECK(report.wireBytes < report.fullBytes);
}

static void TestWrapAroundKeepsDeltas() {
    Report report;
    // 시퀀스가 반드시 한 바퀴 돌게 시작점을 끝 가까이 둔다.
    RunReplication(report, 0.10f, 0.10f, 0.15f, 300, 65450, 32, 64);
    std::printf("  순환 후 델타 %d / 전체 델타 %d / 재구성불가 %d\n",
                report.deltaAfterWrap, report.delta, report.unreconstructable);
    CHECK_EQ(report.unreconstructable, 0);
    CHECK_EQ(report.mismatched, 0);
    // 순환 지점(65536) 이후로도 델타가 계속 쓰여야 한다. 순환 비교가 깨지면
    // 기준선이 65535 근처에서 굳어 밀려나고, 그 뒤는 전부 풀 스냅샷이 된다.
    CHECK(report.deltaAfterWrap > 150);
}

static void TestEvictedBaselineFallsBackToFull() {
    // 기준선 보관을 4개로 줄이고 상행(ack) 경로를 심하게 잃게 한다. 확인된
    // 기준선이 살아남기 전에 밀려나므로 호스트는 전체 스냅샷으로 되돌아가야 한다.
    Report report;
    RunReplication(report, 0.0f, 0.85f, 0.0f, 300, 0, 4, 64);
    std::printf("  기준선 4칸 · ack 손실 85%%: full %d / delta %d / 밀려남 %llu\n",
                report.full, report.delta,
                static_cast<unsigned long long>(report.baselineEvictions));
    CHECK_EQ(report.unreconstructable, 0);
    CHECK_EQ(report.mismatched, 0);
    CHECK(report.baselineEvictions > 0);
    CHECK(report.full > 1);        // 첫 장 말고도 전체를 다시 보낸 적이 있다
}

// ── 5. 실제 스냅샷에서 델타가 얼마나 줄이는가 ──────────────────────

static void TestRealSnapshotSavings() {
    game::DefenseMap map = game::DefenseMap::CreateDefault();
    game::GameWorld world;
    world.Init(map, {game::WaveDef{8, 14, 24, 6, 0.10f}}, 500, 100);
    for (int i = 2; i < 8; ++i) world.PlaceTowerType(map.CellCenter(i, i), game::TowerType::Gatling);

    net::Bytes previous;
    size_t fullTotal = 0, deltaTotal = 0, frames = 0, maxEnemies = 0;
    for (int tick = 0; tick < 600; ++tick) {
        world.Step(1.0f / 60.0f);
        if (tick % 3 != 0) continue;
        const net::Bytes full = game::wire::WriteSnapshot(world, static_cast<uint32_t>(tick), 0);
        if (full.empty()) continue;
        game::wire::Snapshot decoded;
        CHECK(game::wire::ReadSnapshot(full, decoded));
        maxEnemies = decoded.enemies.size() > maxEnemies ? decoded.enemies.size() : maxEnemies;
        if (!previous.empty()) {
            const net::Bytes delta = net::EncodeDelta(previous, full);
            net::Bytes restored;
            CHECK(net::ApplyDelta(previous, delta, restored));
            CHECK(restored == full);
            fullTotal += full.size();
            deltaTotal += delta.size();
            ++frames;
        }
        previous = full;
    }
    CHECK(frames > 100);
    std::printf("  연속 프레임 델타: %zu장 · 원본 평균 %.1f B → 델타 평균 %.1f B (%.1f%%) "
                "· 최대 적 %zu\n",
                frames, static_cast<double>(fullTotal) / static_cast<double>(frames),
                static_cast<double>(deltaTotal) / static_cast<double>(frames),
                100.0 * static_cast<double>(deltaTotal) / static_cast<double>(fullTotal),
                maxEnemies);
    CHECK(deltaTotal < fullTotal);
}

static void RunTests() {
    TestVarint();
    TestDeltaRoundTrip();
    TestDeltaRejectsMalformed();
    TestAckWindow();
    TestSequenceBufferEvicts();
    TestLossyReplication();
    TestWrapAroundKeepsDeltas();
    TestEvictedBaselineFallsBackToFull();
    TestRealSnapshotSavings();
}

TEST_MAIN("delta_snapshot")
