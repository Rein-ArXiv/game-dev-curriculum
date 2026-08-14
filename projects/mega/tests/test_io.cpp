// 비동기 IO 검증: 라운드트립·동시 positioned 읽기·완료 통지·에러 처리.
#include "io/async_io.h"
#include "io/file.h"
#include "test_util.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace mega;
using namespace mega::io;

static std::string TempPath(const char* tag) {
    // 작업 디렉토리에 테스트 파일을 둔다(CI 친화: 별도 임시폴더 권한 불필요).
    return std::string("megaio_") + tag + ".tmp";
}

static void RunTests() {
    Scheduler sched;
    sched.Start();
    IoSystem io(sched);

    // --- 동기 File 기본: 위치 지정 쓰기/읽기 라운드트립 ---
    {
        std::string path = TempPath("basic");
        std::vector<uint8_t> data(4096);
        for (size_t i = 0; i < data.size(); ++i) data[i] = (uint8_t)(i * 31 + 7);

        File f;
        CHECK(f.Open(path.c_str(), FileMode::Write));
        IoResult w = f.WriteAt(0, data.data(), data.size());
        CHECK(w.ok);
        CHECK_EQ(w.bytes, data.size());
        f.Close();

        File r;
        CHECK(r.Open(path.c_str(), FileMode::Read));
        CHECK_EQ(r.Size(), (int64_t)data.size());
        std::vector<uint8_t> back(data.size(), 0);
        IoResult rr = r.ReadAt(0, back.data(), back.size());
        CHECK(rr.ok);
        CHECK_EQ(rr.bytes, data.size());
        CHECK(back == data);
        r.Close();
        std::remove(path.c_str());
    }

    // --- 비동기 쓰기 → 비동기 읽기 라운드트립 ---
    {
        std::string path = TempPath("async");
        const size_t N = 64 * 1024;
        std::vector<uint8_t> src(N);
        for (size_t i = 0; i < N; ++i) src[i] = (uint8_t)(i ^ (i >> 8));

        File wf;
        CHECK(wf.Open(path.c_str(), FileMode::Write));
        IoRequest wreq = io.WriteAt(wf, 0, src.data(), N);
        IoResult wres = wreq.Wait();           // 완료까지 블로킹
        CHECK(wres.ok);
        CHECK_EQ(wres.bytes, N);
        wf.Close();

        File rf;
        CHECK(rf.Open(path.c_str(), FileMode::Read));
        std::vector<uint8_t> dst(N, 0);
        IoRequest rreq = io.ReadAt(rf, 0, dst.data(), N);
        IoResult rres = rreq.Wait();
        CHECK(rres.ok);
        CHECK_EQ(rres.bytes, N);
        CHECK(dst == src);
        rf.Close();
        std::remove(path.c_str());
    }

    // --- 동시 positioned 읽기: 한 파일을 여러 청크로 병렬 읽어 재조립 ---
    {
        std::string path = TempPath("concurrent");
        const int    chunks    = 64;
        const size_t chunkSize = 8192;
        const size_t total     = chunks * chunkSize;
        std::vector<uint8_t> src(total);
        for (size_t i = 0; i < total; ++i) src[i] = (uint8_t)(i * 2654435761u >> 13);

        File wf;
        CHECK(wf.Open(path.c_str(), FileMode::Write));
        CHECK(wf.WriteAt(0, src.data(), total).ok);
        wf.Close();

        File rf;
        CHECK(rf.Open(path.c_str(), FileMode::Read));
        std::vector<uint8_t> dst(total, 0);
        std::vector<IoRequest> reqs;
        reqs.reserve(chunks);
        for (int c = 0; c < chunks; ++c)       // 모든 청크를 한꺼번에 제출
            reqs.push_back(io.ReadAt(rf, (uint64_t)c * chunkSize, dst.data() + c * chunkSize, chunkSize));
        bool allOk = true;
        for (auto& rq : reqs) {                 // 완료 수거
            IoResult r = rq.Wait();
            if (!r.ok || r.bytes != chunkSize) allOk = false;
        }
        CHECK(allOk);
        CHECK(dst == src);                      // 동시 읽기가 데이터를 안 섞었다
        rf.Close();
        std::remove(path.c_str());
    }

    // --- 에러 처리: 없는 파일 열기 실패, 무효 핸들 IO 실패 ---
    {
        File f;
        CHECK(!f.Open("no_such_dir_xyz/does_not_exist.bin", FileMode::Read));
        CHECK(!f.IsOpen());
        uint8_t b = 0;
        IoResult r = f.ReadAt(0, &b, 1);
        CHECK(!r.ok);                           // 무효 핸들 읽기 실패
    }

    sched.Stop();
}

TEST_MAIN("io")
