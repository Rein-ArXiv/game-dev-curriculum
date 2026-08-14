// kv_store — WAL 기반 트랜잭셔널 키-값 스토어. 외부 의존 없음(파일 1개).
//
// 모든 변경을 먼저 append-only 로그(WAL)에 적고 fsync 한 뒤 인메모리 맵에 적용한다.
// 트랜잭션의 모든 레코드를 적고 마지막에 COMMIT 마커를 적은 뒤에야 적용하므로,
// 마커 전 크래시는 그 트랜잭션을 통째로 버린다(원자성). 재시작 시 로그를 재생해
// 맵을 복원하고, 토막난(torn) 마지막 레코드는 안전하게 무시한다(내구성·복구).
// 버린 꼬리는 무시하는 데서 그치지 않고 파일에서 잘라낸다 — 남겨 두면 재기동 뒤
// 첫 트랜잭션의 COMMIT 마커가 그 꼬리까지 커밋한다(Open 주석 참조).
//
// 인덱스는 빠른 조회용 in-memory 해시맵, 영속 진실은 WAL 이다.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <io.h>      // _commit
#else
#include <unistd.h>  // fsync
#endif

namespace mega {
namespace db {

class KvStore {
public:
    ~KvStore() { Close(); }

    // 파일을 열고 기존 WAL 을 재생해 맵을 복원. 없으면 새로 만든다.
    bool Open(const char* path) {
        Close();
        path_ = path;
        long committedEnd = 0, total = 0;
        if (std::FILE* rf = std::fopen(path, "rb")) {   // 기존 로그 재생
            Replay(rf, committedEnd, total);
            std::fclose(rf);
        }
        // 크래시가 남긴 미커밋 꼬리를 파일에 그대로 두면 안 된다. 로그는 append-only 고
        // COMMIT 마커는 "그 앞에 쌓인 pending 전부"에 걸리므로, 재기동 뒤 첫 트랜잭션이
        // 적는 마커 하나가 그 꼬리까지 커밋해 버린다 — 원자성이 재기동을 못 넘긴다.
        // 재생이 인정한 마지막 커밋 지점까지로 잘라 그 경로를 없앤다.
        if (total > committedEnd) Truncate(path, committedEnd);
        wal_ = std::fopen(path, "ab");                  // 이후 추가 기록용
        return wal_ != nullptr;
    }

    void Close() {
        if (wal_) { std::fflush(wal_); std::fclose(wal_); wal_ = nullptr; }
        map_.clear(); txnBuf_.clear(); inTxn_ = false;
    }

    // --- 자동 커밋 또는 트랜잭션 내 버퍼링 ---
    bool Put(const std::string& key, const std::string& val) {
        if (!inTxn_) txnBuf_.clear();
        txnBuf_.push_back({kPut, key, val});
        return inTxn_ ? true : CommitBuffer();
    }
    bool Delete(const std::string& key) {
        if (!inTxn_) txnBuf_.clear();
        txnBuf_.push_back({kDel, key, {}});
        return inTxn_ ? true : CommitBuffer();
    }
    bool Get(const std::string& key, std::string& out) const {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        out = it->second;
        return true;
    }
    bool Has(const std::string& key) const { return map_.count(key) != 0; }
    size_t Size() const { return map_.size(); }

    // --- 명시 트랜잭션 ---
    void Begin() { inTxn_ = true; txnBuf_.clear(); }
    bool Commit() {
        if (!inTxn_) return false;
        bool ok = CommitBuffer();
        inTxn_ = false;
        return ok;
    }
    void Rollback() { inTxn_ = false; txnBuf_.clear(); }   // 버퍼만 버림(로그 미기록)

private:
    enum RecType : uint8_t { kPut = 1, kDel = 2, kCommit = 3 };
    struct Op { RecType type; std::string key, val; };

    static void PutU32(std::FILE* f, uint32_t v) {
        uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                        (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF)};
        std::fwrite(b, 1, 4, f);
    }

    // 트랜잭션 버퍼를 WAL 에 적고(+COMMIT), fsync, 그 다음 맵에 적용.
    bool CommitBuffer() {
        if (!wal_) return false;
        for (const Op& op : txnBuf_) {
            uint8_t t = op.type;
            std::fwrite(&t, 1, 1, wal_);
            PutU32(wal_, (uint32_t)op.key.size());
            std::fwrite(op.key.data(), 1, op.key.size(), wal_);
            if (op.type == kPut) {
                PutU32(wal_, (uint32_t)op.val.size());
                std::fwrite(op.val.data(), 1, op.val.size(), wal_);
            }
        }
        uint8_t commit = kCommit;
        std::fwrite(&commit, 1, 1, wal_);
        std::fflush(wal_);
        Fsync(wal_);                                  // 내구성 보장 지점
        for (const Op& op : txnBuf_) Apply(op);
        txnBuf_.clear();
        return true;
    }

    void Apply(const Op& op) {
        if (op.type == kPut) map_[op.key] = op.val;
        else if (op.type == kDel) map_.erase(op.key);
    }

    static void Fsync(std::FILE* f) {
#if defined(_WIN32)
        _commit(_fileno(f));
#else
        fsync(fileno(f));
#endif
    }

    // 로그를 size 바이트로 자른다. 자르지 못해도 재생 결과는 바뀌지 않으므로
    // 실패는 조용히 넘긴다 — 다음 Open 이 같은 지점을 다시 시도한다.
    static void Truncate(const char* path, long size) {
        std::FILE* f = std::fopen(path, "r+b");
        if (!f) return;
#if defined(_WIN32)
        (void)_chsize_s(_fileno(f), (__int64)size);
#else
        if (ftruncate(fileno(f), (off_t)size) != 0) { /* 무시 */ }
#endif
        std::fclose(f);
    }

    // 파일 전체를 읽어 재생. 커밋된 트랜잭션만 적용, 토막난 꼬리는 무시.
    // committedEnd 에는 마지막 COMMIT 마커 바로 뒤의 오프셋을, total 에는 읽어낸
    // 파일 크기를 돌려준다(둘이 다르면 그 사이가 버려진 미커밋 꼬리다).
    void Replay(std::FILE* rf, long& committedEnd, long& total) {
        committedEnd = 0;
        total = 0;
        std::fseek(rf, 0, SEEK_END);
        long sz = std::ftell(rf);
        std::fseek(rf, 0, SEEK_SET);
        if (sz <= 0) return;
        std::vector<uint8_t> buf((size_t)sz);
        // 읽지 못하면 total 을 0 으로 남긴다 — 내용을 못 본 파일을 자르지 않기 위해서다.
        if (std::fread(buf.data(), 1, (size_t)sz, rf) != (size_t)sz) return;
        total = sz;

        size_t pos = 0;
        std::vector<Op> pending;
        auto u32 = [&](size_t p) {
            return (uint32_t)buf[p] | ((uint32_t)buf[p + 1] << 8) | ((uint32_t)buf[p + 2] << 16) | ((uint32_t)buf[p + 3] << 24);
        };
        while (pos < buf.size()) {
            uint8_t type = buf[pos++];
            if (type == kCommit) {
                for (const Op& op : pending) Apply(op);   // 커밋 → 적용
                pending.clear();
                committedEnd = (long)pos;                 // 여기까지는 영속 확정분
                continue;
            }
            if (type != kPut && type != kDel) break;      // 손상 → 중단
            if (pos + 4 > buf.size()) break;              // 토막난 레코드
            uint32_t kl = u32(pos); pos += 4;
            if (pos + kl > buf.size()) break;
            std::string key((const char*)&buf[pos], kl); pos += kl;
            std::string val;
            if (type == kPut) {
                if (pos + 4 > buf.size()) break;
                uint32_t vl = u32(pos); pos += 4;
                if (pos + vl > buf.size()) break;
                val.assign((const char*)&buf[pos], vl); pos += vl;
            }
            pending.push_back({(RecType)type, key, val});
        }
        // pending 에 남은(미커밋) 트랜잭션은 버린다 — 원자성.
        // 그 바이트 범위는 [committedEnd, total) 이고, Open 이 그만큼을 잘라낸다.
    }

    std::string                                  path_;
    std::FILE*                                   wal_ = nullptr;
    std::unordered_map<std::string, std::string> map_;
    std::vector<Op>                              txnBuf_;
    bool                                         inTxn_ = false;
};

}  // namespace db
}  // namespace mega
