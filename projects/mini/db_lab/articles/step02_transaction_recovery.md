## 들어가며

두 번째 단계는 프레임을 트랜잭션으로 묶고 복구 규칙을 만든다. 핵심 불변식은 단순하다. 복구 결과에는 Commit 프레임으로 끝난 트랜잭션만 보인다.

---

## 목차
1. [Staged 변경](#1-staged-변경)
2. [Commit 순서](#2-commit-순서)
3. [Replay 규칙](#3-replay-규칙)
4. [내구성과 가시성](#4-내구성과-가시성)
5. [검증 기준](#5-검증-기준)

---

## 1. Staged 변경

`TxnPut`과 `TxnDel`은 메모리의 `staged_`에만 쌓인다. 커밋 전에는 `Get` 결과에 보이지 않는다. 이 단순한 격리가 트랜잭션의 가시성 경계를 만든다.

스토어의 공개 계약과 상태는 다음과 같다.

<!-- snippet: src/wal_kv.hpp -->
```cpp
class WalKv {
public:
    struct Options {
        bool fsync_on_commit = true;   // 내구성 다이얼 — bench 의 주 변수
    };

    // 한 프레임 payload 의 상한. 쓰레기 길이 필드를 '잘림'으로 취급하는 경계이며,
    // 테스트가 그 경계를 정확히 겨냥할 수 있도록 계약으로 노출한다.
    static constexpr uint32_t kMaxFrame = 1u << 20;

    // ... Open — 3절

    void Close() { if (f_) { std::fclose(f_); f_ = nullptr; } }

    // ----- 트랜잭션 API -----
    void TxnPut(const std::string& k, const std::string& v) { staged_.push_back({Op::Put, k, v}); }
    void TxnDel(const std::string& k)                       { staged_.push_back({Op::Del, k, {}}); }

    // ... Commit — 2절

    void Rollback() { staged_.clear(); }

    std::optional<std::string> Get(const std::string& k) const {
        auto it = index_.find(k);
        if (it == index_.end()) return std::nullopt;
        return it->second;
    }

    size_t KeyCount() const { return index_.size(); }
    uint64_t RecoveredTxns() const { return recovered_txns_; }
    uint64_t DiscardedTailBytes() const { return discarded_tail_bytes_; }

    // ... RecoverStats, Replay — 3절

private:
    // ... ApplyToIndex, DecodePayload, WriteFrame

    Options opt_;
    std::string path_;
    FILE* f_ = nullptr;
    std::vector<Frame> staged_;
    std::unordered_map<std::string, std::string> index_;
    uint64_t recovered_txns_ = 0;
    uint64_t discarded_tail_bytes_ = 0;
};
```

## 2. Commit 순서

커밋은 다음 순서를 지킨다.

1. staged op 프레임을 모두 WAL에 기록
2. Commit 프레임 기록
3. 필요하면 flush와 fsync
4. 메모리 인덱스에 staged op 적용
5. staged 비우기

WAL보다 인덱스를 먼저 바꾸면 프로세스가 죽었을 때 메모리에서 보였던 값이 복구 후 사라질 수 있다.

<!-- snippet: src/wal_kv.hpp#Commit -->
```cpp
bool Commit() {
    for (const Frame& fr : staged_) if (!WriteFrame(fr)) return false;
    if (!WriteFrame({Op::Commit, {}, {}})) return false;
    if (std::fflush(f_) != 0) return false;
    if (opt_.fsync_on_commit && WALKV_FSYNC(f_) != 0) return false;
    for (const Frame& fr : staged_) ApplyToIndex(index_, fr);
    staged_.clear();
    return true;
}
```

프레임 하나를 쓰는 일과 인덱스에 적용하는 일은 각각 한 함수로 떨어진다. `fsync`는 플랫폼 분기다.

<!-- snippet: src/wal_kv.hpp -->
```cpp
#if defined(_WIN32)
  #include <io.h>
  #define WALKV_FSYNC(f) _commit(_fileno(f))
#else
  #include <unistd.h>
  #define WALKV_FSYNC(f) fsync(fileno(f))
#endif

// ...

static void ApplyToIndex(std::unordered_map<std::string, std::string>& idx, const Frame& fr) {
    if (fr.op == Op::Put) idx[fr.key] = fr.value;
    else if (fr.op == Op::Del) idx.erase(fr.key);
}

// ...

bool WriteFrame(const Frame& fr) {
    std::vector<uint8_t> payload = EncodePayload(fr);
    std::vector<uint8_t> header;
    header.reserve(8);
    AppendU32(header, uint32_t(payload.size()));
    AppendU32(header, Crc32(payload.data(), payload.size()));
    return std::fwrite(header.data(), 1, header.size(), f_) == header.size() &&
           std::fwrite(payload.data(), 1, payload.size(), f_) == payload.size();
}
```

`Commit()`이 실패하면 결과가 항상 “미커밋”인 것은 아니다. Commit 프레임을 쓴 뒤 `fsync`에서 실패했다면 호출자는 결과를 알 수 없는 상태로 취급해야 한다. 이 때문에 경제 연산 재시도에는 트랜잭션 ID와 멱등성 정책이 필요하다.

## 3. Replay 규칙

Replay는 op 프레임을 임시 pending 목록에 모으고, Commit을 만났을 때만 인덱스에 적용한다. 파일 끝이나 손상을 만나면 pending은 버리고, 다시 쓰기 전에 로그를 마지막 유효 Commit 끝으로 절단한다. 꼬리를 그대로 둔 채 뒤에 새 커밋을 붙이면 다음 Replay가 이전 손상에서 멈춰 새 커밋까지 잃기 때문이다.

따라서 어느 바이트에서 로그가 잘려도 복구 결과는 *완전히 커밋된 트랜잭션들의 접두사* 다.

<!-- snippet: src/wal_kv.hpp -->
```cpp
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
```

`Open`은 Replay 결과를 스토어 상태로 옮기고, 버린 꼬리만큼 파일을 잘라낸 뒤 append 모드로 연다.

<!-- snippet: src/wal_kv.hpp#Open -->
```cpp
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
```

## 4. 내구성과 가시성

fsync OFF에서 Commit이 성공해도 OS 페이지 캐시에만 있을 수 있다. 프로세스 재시작에는 남지만 전원 손실에는 사라질 수 있다. fsync ON은 저장 장치에 내구성 경계를 요청하는 대신 지연을 지불한다.

반환값의 의미도 명확해야 한다. fsync 실패를 반환했다면 호출자는 커밋 결과가 불확실할 수 있음을 알아야 하며, 같은 트랜잭션을 무작정 재시도하면 중복 적용 위험이 생긴다.

## 5. 검증 기준

1. 커밋 전 Put이 보이지 않는다.
2. Put 후 Del을 같은 트랜잭션에서 커밋하면 키가 없다.
3. Rollback한 변경은 로그와 인덱스에 없다.
4. Commit 없는 정상 프레임 꼬리는 복구에서 버려진다.
5. 재시작 후 커밋된 값만 복원된다.
6. 미완 꼬리를 복구한 뒤 새로 커밋한 값도 다음 재시작에서 복원된다.

1·2번은 `commit_visibility`(`TestBasic`), 3·5번은 `reopen_keeps_committed`(`TestReopen`), 4·6번은 `uncommitted_tail_discarded`(`TestUncommittedTailDiscarded`)가 확인한다. 여기에 `midlog_corruption_drops_tail`·`del_survives_restart`·`fsync_on_commit_durable`을 더한 여섯이 이 step의 실행 단위 `db_tests_step02`다. 셋 다 이 step이 추가한 것 — Replay의 정지 규칙, Del 프레임의 왕복, fsync 반환값 — 을 겨냥한다.

4·6번의 단정은 이렇게 생겼다. 미완 꼬리를 버리는 것만으로는 부족하고, *그 자리에 새로 커밋한 값* 까지 다음 재시작에서 살아 있어야 한다. 꼬리를 잘라내지 않고 뒤에 이어 붙이는 구현은 두 번째 재시작에서 죽는다.

<!-- snippet: tests/db_tests.cpp#TestUncommittedTailDiscarded -->
```cpp
static void TestUncommittedTailDiscarded() {
    Reset();
    // ... 커밋 하나를 남긴 뒤, COMMIT 프레임 없이 PUT 프레임만 손으로 덧붙인다
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.Get("k").value_or("") == "committed");   // torn 값이 아니어야 한다
        CHECK(db.DiscardedTailBytes() > 0);
        db.TxnPut("after-recovery", "visible");
        CHECK(db.Commit());
        db.Close();
    }
    {
        walkv::WalKv db;
        CHECK(db.Open(kPath, {.fsync_on_commit = false}));
        CHECK(db.Get("k").value_or("") == "committed");
        CHECK(db.Get("after-recovery").value_or("") == "visible");
        db.Close();
    }
}
```

```bash
cmake --build build --config Release
./build/Release/db_tests --step=02
```

관측 결과 — step01의 형식 검사는 빠지고, 이 step이 추가한 여섯만 이름과 함께 돈다.

```text
  ok   commit_visibility
  ok   reopen_keeps_committed
  ok   uncommitted_tail_discarded
  ok   midlog_corruption_drops_tail
  ok   del_survives_restart
  ok   fsync_on_commit_durable
db_tests[step02]: 6/6 passed
```

ctest 이름은 `db_tests_step02`이고, `./build/Release/db_tests`는 여전히 두 step을 합쳐 전부 돌린다.

---

## 결론

변경을 메모리에 stage한 뒤 데이터 프레임과 Commit 경계를 기록하고, 복구 시 완전히 커밋된 트랜잭션의 접두사만 적용하도록 만들었다. 로그가 어느 바이트에서 잘려도 미완 트랜잭션이 가시 상태에 섞이지 않는 것이 핵심 불변식이다.

현재 저장소는 단일 프로세스와 단일 writer를 전제로 하며 동시 트랜잭션 격리, 체크포인트, 로그 압축을 제공하지 않는다. `fsync` 성공의 실제 내구성도 운영체제와 저장 장치의 쓰기 보장 범위 안에서 해석해야 한다.
