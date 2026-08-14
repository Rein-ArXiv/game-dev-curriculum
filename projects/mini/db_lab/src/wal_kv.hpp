// wal_kv.hpp — WAL 기반 트랜잭셔널 키-값 스토어 (db_lab)
//
// 측정 목적의 최소 구현이다. 설계 결정:
//   - 진실은 append-only 로그(WAL), 조회는 인메모리 인덱스.
//   - 트랜잭션 = [op 프레임들...][COMMIT 프레임]. COMMIT 프레임이 디스크에
//     닿기 전의 변경은 복구 시 통째로 버려진다 (원자성).
//   - fsync 토글로 내구성-지연 트레이드오프를 측정한다.
//   - 프레임마다 CRC32 — 찢긴 쓰기(torn write)를 복구 시점에 검출한다.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
  #include <io.h>
  #define WALKV_FSYNC(f) _commit(_fileno(f))
#else
  #include <unistd.h>
  #define WALKV_FSYNC(f) fsync(fileno(f))
#endif

namespace walkv {

// ---------- CRC32 (IEEE, 테이블 방식) ----------
inline uint32_t Crc32(const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---------- 프레임 ----------
// 디스크 형식: [u32 len][u32 crc(payload)][payload: u8 type + body]
enum class Op : uint8_t { Put = 1, Del = 2, Commit = 3 };

struct Frame {
    Op op;
    std::string key;    // Put/Del
    std::string value;  // Put
};

inline void AppendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
inline uint32_t ReadU32Le(const uint8_t* p) {
    return uint32_t(p[0]) |
           (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
inline void AppendStr(std::vector<uint8_t>& b, const std::string& s) {
    AppendU32(b, uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

inline std::vector<uint8_t> EncodePayload(const Frame& f) {
    std::vector<uint8_t> p;
    p.push_back(uint8_t(f.op));
    if (f.op == Op::Put) { AppendStr(p, f.key); AppendStr(p, f.value); }
    else if (f.op == Op::Del) { AppendStr(p, f.key); }
    return p;
}

// ---------- 스토어 ----------
class WalKv {
public:
    struct Options {
        bool fsync_on_commit = true;   // 내구성 다이얼 — bench 의 주 변수
    };

    // 한 프레임 payload 의 상한. 쓰레기 길이 필드를 '잘림'으로 취급하는 경계이며,
    // 테스트가 그 경계를 정확히 겨냥할 수 있도록 계약으로 노출한다.
    static constexpr uint32_t kMaxFrame = 1u << 20;

    bool Open(const std::string& path, Options opt) {
        opt_ = opt;
        path_ = path;
        RecoverStats rs;
        if (!Replay(path, index_, &rs)) return false;
        recovered_txns_ = rs.committed_txns;
        discarded_tail_bytes_ = rs.discarded_bytes;
        if (rs.discarded_bytes > 0) {
            std::error_code ec;
            std::filesystem::resize_file(path, rs.valid_bytes, ec);
            if (ec) return false;
        }
        f_ = std::fopen(path.c_str(), "ab");
        return f_ != nullptr;
    }

    void Close() { if (f_) { std::fclose(f_); f_ = nullptr; } }

    // ----- 트랜잭션 API -----
    void TxnPut(const std::string& k, const std::string& v) { staged_.push_back({Op::Put, k, v}); }
    void TxnDel(const std::string& k)                       { staged_.push_back({Op::Del, k, {}}); }

    // 커밋: op 프레임 전부 + COMMIT 프레임을 로그에 적고 (옵션) fsync 후 인덱스에 적용한다.
    bool Commit() {
        for (const Frame& fr : staged_) if (!WriteFrame(fr)) return false;
        if (!WriteFrame({Op::Commit, {}, {}})) return false;
        if (std::fflush(f_) != 0) return false;
        if (opt_.fsync_on_commit && WALKV_FSYNC(f_) != 0) return false;
        for (const Frame& fr : staged_) ApplyToIndex(index_, fr);
        staged_.clear();
        return true;
    }

    void Rollback() { staged_.clear(); }

    std::optional<std::string> Get(const std::string& k) const {
        auto it = index_.find(k);
        if (it == index_.end()) return std::nullopt;
        return it->second;
    }

    size_t KeyCount() const { return index_.size(); }
    uint64_t RecoveredTxns() const { return recovered_txns_; }
    uint64_t DiscardedTailBytes() const { return discarded_tail_bytes_; }

    // ----- 복구 (정적: 측정 도구가 직접 호출할 수 있게) -----
    struct RecoverStats {
        uint64_t committed_txns = 0;
        uint64_t valid_bytes = 0;
        uint64_t discarded_bytes = 0;
    };

    static bool Replay(const std::string& path,
                       std::unordered_map<std::string, std::string>& out,
                       RecoverStats* stats) {
        out.clear();
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return true;  // 파일 없음 = 빈 스토어 (정상)
        std::vector<Frame> pending;          // 현재 트랜잭션의 미확정 op 들
        uint64_t committed = 0, good_end = 0;
        for (;;) {
            uint8_t header[8]{};
            const size_t header_bytes = std::fread(header, 1, sizeof(header), f);
            if (header_bytes == 0) break;                           // EOF — 정상 종료
            if (header_bytes != sizeof(header)) break;              // 헤더 도중 잘림
            const uint32_t len = ReadU32Le(header);
            const uint32_t crc = ReadU32Le(header + 4);
            if (len == 0 || len > kMaxFrame) break;                 // 쓰레기 길이 — 잘림 취급
            std::vector<uint8_t> payload(len);
            if (std::fread(payload.data(), 1, len, f) != len) break;  // 본문 잘림
            if (Crc32(payload.data(), len) != crc) break;           // 찢긴 쓰기 검출
            Frame fr;
            if (!DecodePayload(payload, fr)) break;
            if (fr.op == Op::Commit) {
                for (const Frame& p : pending) ApplyToIndex(out, p);
                pending.clear();
                ++committed;
                good_end = uint64_t(std::ftell(f));
            } else {
                pending.push_back(std::move(fr));
            }
        }
        // pending 에 남은 op 들 = COMMIT 마커가 없는 미완 트랜잭션 — 버린다 (원자성).
        std::fseek(f, 0, SEEK_END);
        uint64_t file_size = uint64_t(std::ftell(f));
        std::fclose(f);
        if (stats) {
            stats->committed_txns = committed;
            stats->valid_bytes = good_end;
            stats->discarded_bytes = file_size - good_end;
        }
        return true;
    }

private:
    static void ApplyToIndex(std::unordered_map<std::string, std::string>& idx, const Frame& fr) {
        if (fr.op == Op::Put) idx[fr.key] = fr.value;
        else if (fr.op == Op::Del) idx.erase(fr.key);
    }

    static bool DecodePayload(const std::vector<uint8_t>& p, Frame& out) {
        size_t off = 0;
        auto readU32 = [&](uint32_t& v) {
            if (off + 4 > p.size()) return false;
            v = ReadU32Le(p.data() + off);
            off += 4;
            return true;
        };
        auto readStr = [&](std::string& s) {
            uint32_t n; if (!readU32(n)) return false;
            if (off + n > p.size()) return false;
            s.assign(reinterpret_cast<const char*>(p.data() + off), n); off += n; return true;
        };
        if (p.empty()) return false;
        out.op = Op(p[0]); off = 1;
        switch (out.op) {
            case Op::Put:    return readStr(out.key) && readStr(out.value);
            case Op::Del:    return readStr(out.key);
            case Op::Commit: return true;
            default:         return false;
        }
    }

    bool WriteFrame(const Frame& fr) {
        std::vector<uint8_t> payload = EncodePayload(fr);
        std::vector<uint8_t> header;
        header.reserve(8);
        AppendU32(header, uint32_t(payload.size()));
        AppendU32(header, Crc32(payload.data(), payload.size()));
        return std::fwrite(header.data(), 1, header.size(), f_) == header.size() &&
               std::fwrite(payload.data(), 1, payload.size(), f_) == payload.size();
    }

    Options opt_;
    std::string path_;
    FILE* f_ = nullptr;
    std::vector<Frame> staged_;
    std::unordered_map<std::string, std::string> index_;
    uint64_t recovered_txns_ = 0;
    uint64_t discarded_tail_bytes_ = 0;
};

}  // namespace walkv
