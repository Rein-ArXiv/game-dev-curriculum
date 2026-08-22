// reliable_channel — 비신뢰 데이터그램 위의 신뢰·순서 보장 메시지 채널.
//
// 손실·재정렬되는 transport 위에서 "모든 메시지를 보낸 순서대로 정확히 한 번"
// 전달한다. 각 메시지에 단조 시퀀스를 붙이고(누적 ack), 받은 쪽은 연속 수신
// 경계(nextExpected)를 ack 로 알린다. 보낸 쪽은 ack 미만을 지우고 나머지를 재전송
// 한다 — TCP 의 누적 ack 와 같은 원리지만 게임에 맞게 데이터그램 위에 직접 쌓는다.
//
// 위치 스냅샷처럼 최신값만 의미 있는 데이터는 이 채널을 거치지 않고 비신뢰로 보낸다
// (snapshot.hpp). 순서가 꼭 필요한 이벤트(타워 건설, 적 처치 확정)만 여기로.
//
// 주의: 이 구현은 매 Update 마다 미확인 메시지를 전부 재전송한다(단순·정확 우선).
// 실전은 RTT 기반 타임아웃으로 선택 재전송한다 — rtt.hpp 가 그 추정을 제공한다.
//
// ── 구조: 신뢰성 로직과 프레이밍의 분리 ──────────────────────────────
// 신뢰성 자체(시퀀스 부여·미확인 재전송 목록·누적 ack·순서 복원 버퍼)는
// ReliableOrderedSender / ReliableOrderedReceiver 가 갖고, 바이트를 어떻게 싸는지는
// 모른다. ReliableOrderedChannel 은 그 둘에 자체 DATA/ACK 프레이밍을 씌운 것뿐이다.
//
// 이미 자기 패킷 형식을 가진 프로토콜(game/defense_protocol.hpp 의 명령·스냅샷은
// 시퀀스를 명령 패킷에, 누적 ack 를 스냅샷의 acknowledgedCommand 에 싣는다)은
// 프레이밍 없이 코어 둘만 직접 쓴다. 이 분리가 없으면 제품은 프레이밍이 안 맞는다는
// 이유로 신뢰성 로직을 자기 자리에 다시 구현하게 된다 — 실제로 그랬다.

#pragma once

#include "net/sequence.hpp"
#include "net/transport.hpp"

#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <utility>

namespace mega {
namespace net {

// 보내는 쪽 코어: 시퀀스 부여 + 미확인 보관 + 누적 ack 로 정리.
// 전송은 하지 않는다 — 호출자가 ForEachUnacked 로 자기 형식에 맞게 내보낸다.
// snippet-begin: reliable-sender
template <class Payload = Bytes>
class ReliableOrderedSender {
public:
    explicit ReliableOrderedSender(uint32_t initialSequence = 0)
        : nextSend_(initialSequence) {}

    // 메시지를 큐잉하고 부여된 시퀀스를 돌려준다.
    uint32_t Queue(Payload message) {
        const uint32_t sequence = nextSend_++;
        unacked_.emplace(sequence, std::move(message));
        return sequence;
    }

    // 누적 ack(배타 경계): 상대가 boundary 미만을 전부 받았다.
    void AckBefore(uint32_t boundary) {
        for (auto it = unacked_.begin(); it != unacked_.end();)
            it = SequenceMoreRecent(boundary, it->first) ? unacked_.erase(it)
                                                        : std::next(it);
    }

    // 누적 ack(포함): 상대가 sequence 까지 받았다. 상태 패킷에 "마지막으로 반영한
    // 명령 번호"를 실어 보내는 프로토콜이 쓰는 형태.
    void AckThrough(uint32_t sequence) {
        for (auto it = unacked_.begin(); it != unacked_.end();)
            it = SequenceAtOrBefore(it->first, sequence) ? unacked_.erase(it)
                                                        : std::next(it);
    }

    // fn(sequence, payload) 를 미확인 메시지에 시퀀스 오름차순으로 적용.
    template <class Fn>
    void ForEachUnacked(Fn&& fn) const {
        for (const auto& entry : unacked_) fn(entry.first, entry.second);
    }

    bool Empty() const { return unacked_.empty(); }
    size_t UnackedCount() const { return unacked_.size(); }
    uint32_t NextSequence() const { return nextSend_; }

private:
    uint32_t                    nextSend_ = 0;   // 다음 부여할 시퀀스
    std::map<uint32_t, Payload> unacked_;        // 미확인(재전송 대상)
};
// snippet-end: reliable-sender

// 받는 쪽 코어: 순서 복원 + 중복 제거. 역시 프레이밍을 모른다.
template <class Payload = Bytes>
class ReliableOrderedReceiver {
public:
    static constexpr uint32_t kReceiveWindow = 1024;

    explicit ReliableOrderedReceiver(uint32_t initialSequence = 0)
        : nextExpected_(initialSequence) {}

    // 처음 보는 메시지면 true(즉시 전달 대기열에 넣었거나 순서 복원 버퍼에 넣었다).
    // 이미 받은 과거·창 밖의 과도한 미래는 false.
    bool Accept(uint32_t sequence, Payload payload) {
        if (sequence == nextExpected_) {          // 기대한 다음 → 전달 + 버퍼 드레인
            delivered_.push_back(std::move(payload));
            ++nextExpected_;
            auto it = buffered_.find(nextExpected_);
            while (it != buffered_.end()) {
                delivered_.push_back(std::move(it->second));
                buffered_.erase(it);
                ++nextExpected_;
                it = buffered_.find(nextExpected_);
            }
            return true;
        }
        if (SequenceMoreRecent(sequence, nextExpected_) &&
            static_cast<uint32_t>(sequence - nextExpected_) <=
                kReceiveWindow)                   // 제한된 앞당겨 도착 → 버퍼링
            return buffered_.emplace(sequence, std::move(payload)).second;
        return false;                             // 과거·과도한 미래: 무시
    }

    // 앱이 순서대로 받은 메시지를 꺼낸다.
    bool Poll(Payload& out) {
        if (delivered_.empty()) return false;
        out = std::move(delivered_.front());
        delivered_.pop_front();
        return true;
    }

    uint32_t Delivered() const { return nextExpected_; }          // 연속 전달 경계(배타)
    uint32_t LastDelivered() const { return nextExpected_ - 1; }  // 마지막 전달(포함)
    size_t BufferedCount() const { return buffered_.size(); }
    size_t PendingCount() const { return delivered_.size(); }

private:
    uint32_t                    nextExpected_ = 0;  // 다음 기대 수신 seq
    std::map<uint32_t, Payload> buffered_;          // 비순서 도착 버퍼
    std::deque<Payload>         delivered_;         // 앱 전달 대기(순서대로)
};

// 위 두 코어에 자체 DATA/ACK 프레이밍을 씌운 완결형 채널.
class ReliableOrderedChannel {
public:
    explicit ReliableOrderedChannel(ITransport& t, uint32_t initialSequence = 0)
        : t_(t), sender_(initialSequence), receiver_(initialSequence) {}

    // 신뢰·순서 전송할 메시지 큐잉.
    void Send(const Bytes& msg) { sender_.Queue(msg); }

    // 매 틱 호출: 수신 처리 → 재전송 → ack 전송.
    void Update() {
        Bytes raw;
        while (t_.Receive(raw)) Handle(raw);
        sender_.ForEachUnacked([this](uint32_t seq, const Bytes& payload) {
            SendData(seq, payload);                                           // 미확인 재전송
        });
        SendAck(receiver_.Delivered());                                       // 연속 수신 경계 통지
    }

    // 앱이 순서대로 받은 메시지를 꺼낸다.
    bool Poll(Bytes& out) { return receiver_.Poll(out); }

    size_t UnackedCount() const { return sender_.UnackedCount(); }
    uint32_t Delivered() const { return receiver_.Delivered(); }   // 연속 전달 수

private:
    static void PutU32(Bytes& b, uint32_t v) {
        b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v >> 8) & 0xFF));
        b.push_back((uint8_t)((v >> 16) & 0xFF)); b.push_back((uint8_t)((v >> 24) & 0xFF));
    }
    static uint32_t GetU32(const uint8_t* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    void SendData(uint32_t seq, const Bytes& payload) {
        Bytes pkt;
        pkt.push_back(0x00);              // DATA
        PutU32(pkt, seq);
        pkt.insert(pkt.end(), payload.begin(), payload.end());
        t_.Send(pkt);
    }
    void SendAck(uint32_t ackSeq) {
        Bytes pkt;
        pkt.push_back(0x01);             // ACK(연속 수신 경계: ackSeq 미만 전부 수신)
        PutU32(pkt, ackSeq);
        t_.Send(pkt);
    }

    void Handle(const Bytes& raw) {
        if (raw.empty()) return;
        if (raw[0] == 0x00) {                            // DATA
            if (raw.size() < 5) return;
            receiver_.Accept(GetU32(&raw[1]), Bytes(raw.begin() + 5, raw.end()));
        } else if (raw[0] == 0x01) {                     // ACK
            if (raw.size() < 5) return;
            // ackSeq 미만은 상대가 전부 받음 → 미확인에서 제거.
            sender_.AckBefore(GetU32(&raw[1]));
        }
    }

    ITransport&                     t_;
    ReliableOrderedSender<Bytes>    sender_;
    ReliableOrderedReceiver<Bytes>  receiver_;
};

}  // namespace net
}  // namespace mega
