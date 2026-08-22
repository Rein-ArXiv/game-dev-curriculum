// asset_reload — 매치 중 에셋 다시 읽기와 종료 시 트레이스 덤프.
//
// 시작할 때 파일을 한 번 읽는 것은 동기 IO 로 충분하다. 아무도 그 시간을 프레임
// 예산으로 재지 않기 때문이다. 매치 중에 읽는 것은 다르다 — 스프라이트 열 장을
// 프레임 스레드에서 읽으면 그 프레임 하나가 디스크 지연만큼 통째로 멈춘다.
// 60Hz 예산이 16.7ms 인데 콜드 캐시의 파일 열 번은 그것을 쉽게 넘는다.
//
// 그래서 여기서는 같은 리로드를 두 방식으로 실장한다.
//
//   sync   프레임 스레드가 열 장을 다 읽고 디코드까지 끝낸다. 그 프레임이 멈춘다.
//   async  io/async_io.h 의 IoSystem 이 잡 워커에서 읽는다. 프레임 루프는 완료를
//          폴링만 하고, 다 모인 프레임에서 디코드해 반영한다.
//
// 판정은 둘이다. (a) 두 방식의 **최종 스프라이트가 같아야 한다** — 이것이 "읽기가
// 끝나기 전에 버퍼를 쓴다" 부류의 결함을 잡는다. (b) 리로드가 걸린 프레임의 최대
// 프레임 시간. (a) 없이 (b) 만 재면 덜 읽고 빨라진 것을 개선으로 오독한다.

#pragma once

#include "game/defense_view.hpp"
#include "game/switches.hpp"
#include "io/async_io.h"
#include "io/file.h"
#include "io/read_file.hpp"
#include "jobs/scheduler.h"
#include "profiler/trace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace mega {
namespace game {

class AssetReloader {
public:
    void Init(Scheduler* scheduler, AssetIoKind kind, const std::string& root) {
        scheduler_ = scheduler;
        kind_ = kind;
        root_ = root;
        if (scheduler_ && scheduler_->WorkerCount() > 0 && !io_)
            io_.reset(new io::IoSystem(*scheduler_));
        // 워커가 없으면 비동기는 성립하지 않는다 — 제출한 읽기를 아무도 처리하지
        // 않아 리로드가 영영 끝나지 않는다. 조용히 매달리는 대신 동기로 떨어진다.
        if (kind_ == AssetIoKind::Async && !io_) kind_ = AssetIoKind::Sync;
    }

    AssetIoKind Kind() const { return kind_; }
    bool Busy() const { return busy_; }
    uint64_t Reloads() const { return reloads_; }

    // 리로드 시작. 동기 팔은 이 호출 안에서 끝나고, 비동기 팔은 **일 하나를 던지고**
    // 나온다. 파일을 여는 것(CreateFile/open)도 시스템 콜이라 프레임 스레드에
    // 남겨 두면 열 번의 커널 진입이 그대로 프레임 비용이 된다 — 그래서 열기·크기
    // 질의·읽기 제출을 통째로 워커로 옮긴다.
    // snippet-begin: reload-request
    void Request(DefenseSkin& skin) {
        if (busy_) return;
        MEGA_PROFILE_SCOPE("io.reload_request");
        if (kind_ == AssetIoKind::Sync) {
            skin.Load(root_);        // 프레임 스레드가 열 장을 다 읽는다
            ++reloads_;
            return;
        }
        prepared_.store(0, std::memory_order_release);
        busy_ = true;
        scheduler_->Submit([this] { OpenAndSubmitReads(); });
    }
    // snippet-end: reload-request

    // 프레임마다 한 번. 아직 안 끝났으면 false 를 돌려주고 프레임은 계속 간다.
    // snippet-begin: reload-poll
    bool Poll(DefenseSkin& skin) {
        if (!busy_) return false;
        MEGA_PROFILE_SCOPE("io.reload_poll");
        if (prepared_.load(std::memory_order_acquire) == 0) return false;   // 제출 전
        for (const Slot& s : slots_)
            if (s.ok && !s.request.Done()) return false;    // 아직 한 장이라도 안 왔다

        // 열 장이 전부 모인 뒤에야 여기 온다. 그래도 스테이징 → 커밋으로 나누는
        // 이유는 디코드가 실패할 수 있기 때문이다 — 절반만 갈아 끼운 스킨이 한
        // 프레임이라도 그려지면, 화면에서 본 것과 파일의 내용이 다른 상태가 된다.
        skin.BeginReload();
        skin.BuildFallbacks();
        skin.externalAssets = 0;
        for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot) {
            Slot& s = slots_[static_cast<size_t>(slot)];
            if (!s.ok) continue;
            // Done() 이 참이라도 결과는 Wait 로 받는다 — 완료 상태에서 즉시
            // 반환하지만, 이 호출이 바이트에 대한 acquire 경계다. 이것을 건너뛰고
            // 버퍼를 읽으면 워커의 쓰기가 이 스레드에 보인다는 보장이 없다.
            const io::IoResult result = s.request.Wait();
            s.file.Close();
            if (!result.ok || result.bytes != s.bytes.size()) continue;
            render::Sprite decoded;
            const std::string text(reinterpret_cast<const char*>(s.bytes.data()), s.bytes.size());
            if (render::DecodePpmP3(text, decoded)) skin.Apply(slot, std::move(decoded));
        }
        skin.CommitReload();
        busy_ = false;
        prepared_.store(0, std::memory_order_release);
        ++reloads_;
        return true;
    }
    // snippet-end: reload-poll

private:
    struct Slot {
        io::File              file;
        io::IoRequest         request;
        std::vector<uint8_t>  bytes;
        bool                  ok = false;
    };

    // 워커에서 돈다. 파일을 열고 크기를 재고 읽기를 IoSystem 에 제출한다. 이
    // 함수가 끝나야 slots_ 가 읽을 수 있는 상태가 되므로 prepared_ 를 release 로
    // 올리고, Poll 은 acquire 로 그것을 본다.
    void OpenAndSubmitReads() {
        MEGA_PROFILE_SCOPE("io.reload_open");
        for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot) {
            Slot& s = slots_[static_cast<size_t>(slot)];
            s.ok = false;
            s.bytes.clear();
            s.file.Close();
            if (!s.file.Open((root_ + "/" + DefenseSkin::FileName(slot)).c_str(),
                             io::FileMode::Read))
                continue;
            const int64_t size = s.file.Size();
            if (size <= 0 || size > 4 * 1024 * 1024) { s.file.Close(); continue; }
            s.bytes.assign(static_cast<size_t>(size), 0);
            s.request = io_->ReadAt(s.file, 0, s.bytes.data(), s.bytes.size());
            s.ok = true;
        }
        prepared_.store(1, std::memory_order_release);
    }

    std::array<Slot, DefenseSkin::kFileCount> slots_;
    std::unique_ptr<io::IoSystem>             io_;
    Scheduler*   scheduler_ = nullptr;
    std::string  root_;
    std::atomic<int> prepared_{0};
    AssetIoKind  kind_ = AssetIoKind::Sync;
    uint64_t     reloads_ = 0;
    bool         busy_ = false;
};

// 프로파일러 링에 쌓인 이벤트를 이름별로 접어 텍스트로 쓴다.
//
// 이 경로가 io::File::WriteAt 의 첫 제품 호출자다. P0 감사는 io/file.cpp 가
// "링크만 되고 호출 0" 이라고 판정했는데, 정확히는 읽기만 쓰이고 쓰기는 아무도
// 부르지 않는 상태였다 — 진행도 저장은 db/kv_store.hpp 가 자기 FILE* 를 쓴다.
inline std::string FormatTraceReport() {
    const std::vector<profiler::TraceEvent> events = profiler::Profiler::Get().Snapshot();
    std::vector<profiler::AggEntry> aggregate = profiler::AggregateByName(events);
    std::sort(aggregate.begin(), aggregate.end(),
              [](const profiler::AggEntry& a, const profiler::AggEntry& b) {
                  return a.totalMs > b.totalMs;
              });
    std::string out = "scope\tcount\ttotal_ms\tmax_ms\tmean_ms\n";
    char line[256];
    for (const profiler::AggEntry& entry : aggregate) {
        const double mean = entry.count ? entry.totalMs / static_cast<double>(entry.count) : 0.0;
        std::snprintf(line, sizeof(line), "%s\t%llu\t%.3f\t%.3f\t%.4f\n",
                      entry.name.c_str(),
                      static_cast<unsigned long long>(entry.count),
                      entry.totalMs, entry.maxMs, mean);
        out += line;
    }
    return out;
}

// 동기 팔은 프레임 스레드가 쓰고, 비동기 팔은 워커가 쓰는 동안 호출자가 정리
// 작업(지표 출력 등)을 계속한다. 반환 시점에는 두 팔 모두 쓰기가 끝나 있다.
inline bool WriteTraceReport(const std::string& path, AssetIoKind kind,
                             Scheduler* scheduler) {
    const std::string text = FormatTraceReport();
    io::File file;
    if (!file.Open(path.c_str(), io::FileMode::Write)) return false;
    if (kind == AssetIoKind::Async && scheduler && scheduler->WorkerCount() > 0) {
        io::IoSystem system(*scheduler);
        io::IoRequest request = system.WriteAt(file, 0, text.data(), text.size());
        return request.Wait().ok;
    }
    return file.WriteAt(0, text.data(), text.size()).ok;
}

}  // namespace game
}  // namespace mega
