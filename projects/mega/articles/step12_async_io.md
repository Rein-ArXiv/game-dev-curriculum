## 들어가며

이 게임은 디스크를 딱 두 번 만진다. 시작할 때 에셋 열 장과 콘텐츠 파일 셋을 읽고, 매치가 끝날 때 진행도를 커밋한다. 둘 다 프레임 스레드가 직접 한다.

**시작할 때 한 번 읽는 것은 동기 IO 로 충분하다.** 아무도 그 시간을 프레임 예산으로 재지 않는다. 창이 뜨기 전에 100ms 를 쓰든 300ms 를 쓰든 그것은 로딩이지 히치가 아니다.

매치 중에 읽는 것은 다르다. 60Hz 예산은 16.7ms 이고, 콜드 캐시의 파일 열기·읽기 열 번은 그것을 쉽게 넘는다. 그리고 매치 중에 읽을 이유가 생겼다 — **에셋 핫 리로드**. 스프라이트를 고치고 게임을 다시 띄우지 않고 결과를 보는 것은 콘텐츠 작업의 반복 속도를 결정한다.

이 편은 그 리로드를 두 방식으로 실장한다.

- `asset_io=sync` — 프레임 스레드가 열 장을 다 읽고 디코드까지 끝낸다. 그 프레임이 멈춘다.
- `asset_io=async` — `io/async_io.h` 의 `IoSystem` 이 잡 워커에서 읽는다. 프레임 루프는 완료를 폴링만 한다.

판정에는 순서가 있다. **(a) 두 방식의 최종 픽셀이 같아야 한다.** 그것이 "읽기가 끝나기 전에 버퍼를 쓴다" 부류의 결함을 잡는다. (b) 그다음에 프레임 스레드가 붙잡히는 시간을 잰다. (a) 없이 (b)만 재면 덜 읽고 빨라진 것을 개선으로 오독한다.

도달점은 이렇다. `L` 키가 리로드를 걸고, 비동기 팔에서 프레임 스레드가 붙잡히는 시간이 **0.7ms 에서 0.005ms** 로 줄며, 두 팔이 그린 480×480 프레임의 픽셀 230,400개가 하나도 다르지 않다.

---

## 목차
1. [동기 IO 가 문제가 되는 조건](#1-동기-io-가-문제가-되는-조건)
2. [리로드를 거는 입구](#2-리로드를-거는-입구)
3. [읽기와 디코드를 가른다](#3-읽기와-디코드를-가른다)
4. [파일 여는 것도 시스템 콜이다](#4-파일-여는-것도-시스템-콜이다)
5. [폴링: 완료 확인과 acquire 경계](#5-폴링-완료-확인과-acquire-경계)
6. [워커가 없으면 비동기는 성립하지 않는다](#6-워커가-없으면-비동기는-성립하지-않는다)
7. [판정 (a): 픽셀이 같은가](#7-판정-a-픽셀이-같은가)
8. [판정 (b): 시간이 아니라 상태를 단언한다](#8-판정-b-시간이-아니라-상태를-단언한다)
9. [트레이스 덤프 — 쓰기 경로의 첫 호출자](#9-트레이스-덤프--쓰기-경로의-첫-호출자)
10. [변이: 읽지 않는 읽기](#10-변이-읽지-않는-읽기)
11. [실행과 관측](#11-실행과-관측)

---

## 1. 동기 IO 가 문제가 되는 조건

"동기 IO 는 나쁘다"는 문장은 틀렸다. 조건이 빠져 있다.

동기 IO 가 문제가 되는 조건은 정확히 하나다 — **그 호출이 프레임 예산 안에 있을 때.**

| 자리 | 언제 | 동기로 충분한가 |
|---|---|---|
| 에셋 초기 로드 | 창 뜨기 전 | 예 |
| 콘텐츠 파일(맵·웨이브·밸런스) | 창 뜨기 전 | 예 |
| 진행도 커밋 | 매치 종료 후 | 예 (프레임이 이미 끝났다) |
| **에셋 핫 리로드** | **매치 중** | **아니오** |
| 트레이스 덤프 | 프로세스 종료 시 | 예 |

넷은 그대로 두고 하나만 바꾼다. 그것이 이 편의 전부다.

바꿔야 하는 이유를 수로 적으면: 스프라이트 열 장을 프레임 스레드에서 읽고 디코드하는 데 이 머신·따뜻한 페이지 캐시에서 **0.7~1.1ms** 가 든다. 16.7ms 예산의 5~7%다. 콜드 캐시나 네트워크 드라이브에서는 자릿수가 달라진다.

## 2. 리로드를 거는 입구

입력은 이미 액션 이름으로 추상화돼 있다(3편). 키 하나를 더한다.

```cpp
inline constexpr std::array<int, 10> kDefenseActionKeys{
    '1', '2', '3', '4', 'P', 'U', 'X', 'T', 'R', 'L'};
```

`R` 은 이미 재시작이라 `L`(reload)을 골랐다. 프레임 루프에서 두 줄이다.

```cpp
// 에셋 리로드. 'L' 이 요청을 걸고, 완료 확인은 매 프레임 폴링이다 —
// 동기 팔에서는 Request 안에서 이미 끝나 있고 Poll 이 할 일이 없다.
if (input_.WasPressed("reload")) reloader_.Request(skin_);
reloader_.Poll(skin_);
```

`Request` 와 `Poll` 이 나뉜 것이 비동기의 전부다. 동기 팔에서도 같은 두 줄이 돌고, `Poll` 이 즉시 `false` 를 돌려준다 — **두 팔이 같은 코드 경로를 지난다.** 프레임 루프에 `if (async)` 분기가 없다.

창 없는 호출자를 위해 같은 입구가 하나 더 있다.

```cpp
// 창 없는 호출자(테스트·셀프테스트)가 리로드를 거는 입구. 창 실행에서는
// 'L' 키가 같은 자리로 들어간다.
void RequestAssetReload() { reloader_.Request(skin_); }
```

## 3. 읽기와 디코드를 가른다

`render::LoadPpmP3(path, out)` 은 파일 경로를 받아 스프라이트를 낸다. 그 안에서 읽기와 파싱이 붙어 있다.

비동기에서는 그 둘이 다른 스레드에 있어야 한다. 바이트는 워커가 가져오고, 스프라이트로 바꾸는 것은 프레임 스레드가 한다(`render::Sprite` 는 렌더러가 소유하는 자료구조다).

그래서 디코더를 떼어냈다.

<!-- snippet: include/render/sprite.hpp#DecodePpmP3 -->
```cpp
inline bool DecodePpmP3(const std::string& text, Sprite& out) {
    std::istringstream input(text);
    std::string token;
    if (!ReadPpmToken(input, token) || token != "P3") return false;
    // ...
    if (ReadPpmToken(input, token)) return false;
    out = std::move(decoded);
    return true;
}
```

`LoadPpmP3` 는 그 위의 얇은 껍질이 된다.

```cpp
inline bool LoadPpmP3(const std::string& path, Sprite& out) {
    std::string text;
    if (!io::ReadTextFile(path, text, 4 * 1024 * 1024)) return false;
    return DecodePpmP3(text, out);
}
```

스킨 쪽에도 같은 분리가 필요했다. 원래 `DefenseSkin::Load` 는 파일 이름 목록과 슬롯 대응을 함수 안에 갖고 있었다. 비동기 팔이 같은 목록을 따로 들면 한쪽만 파일이 늘어나 조용히 다른 스킨이 된다.

<!-- snippet: include/game/defense_view.hpp#FileName -->
```cpp
static const char* FileName(int slot) {
    static const char* names[kFileCount] = {
        "tower_cannon.ppm", "tower_gatling.ppm", "tower_splash.ppm", "tower_sniper.ppm",
        "enemy_normal.ppm", "enemy_fast.ppm", "enemy_tank.ppm", "enemy_swarm.ppm",
        "enemy_boss.ppm", "core.ppm",
    };
    return (slot >= 0 && slot < kFileCount) ? names[slot] : "";
}
```

슬롯 번호 → 파일 이름, 슬롯 번호 → 배열 자리(`Apply`)가 각각 한 곳에만 있다. 동기 로드와 비동기 리로드가 둘 다 그것을 쓴다.

## 4. 파일 여는 것도 시스템 콜이다

첫 구현은 이랬다. `Request` 가 프레임 스레드에서 파일 열 개를 열고, 크기를 재고, 버퍼를 잡고, `IoSystem::ReadAt` 으로 읽기를 제출한다. 읽기만 워커가 한다.

재 보니 이랬다.

```
[asset_io] sync  request=0.597ms
[asset_io] async request=0.492ms
```

18% 밖에 줄지 않았다. 이유는 명확하다 — 따뜻한 페이지 캐시에서 실제 읽기는 사실상 memcpy 이고, 비용의 대부분이 **`CreateFile` 열 번과 `GetFileSizeEx` 열 번**이었다. 그것이 여전히 프레임 스레드에 있었다.

그래서 여는 것까지 워커로 옮겼다. `Request` 는 일 하나를 던지고 나온다.

<!-- snippet: include/game/asset_reload.hpp@reload-request -->
```cpp
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
```

<!-- snippet: include/game/asset_reload.hpp#OpenAndSubmitReads -->
```cpp
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
```

이 함수가 워커에서 돌면서 다시 `IoSystem::ReadAt` 을 부른다 — 워커가 워커에 일을 던진다. 그래도 되는 이유는 `Submit` 이 큐에 넣고 깨우는 것이 전부이기 때문이다.

```cpp
IoRequest ReadAt(const File& f, uint64_t offset, void* buf, size_t n) {
    IoRequest req;
    req.st_ = std::make_shared<IoRequest::State>();
    auto st = req.st_;
    const File* fp = &f;
    sched_.Submit([st, fp, offset, buf, n] {
        st->result = fp->ReadAt(offset, buf, n);
        st->done.store(1, std::memory_order_release);
        sync::FutexWakeAll(&st->done);
    });
    return req;
}
```

`io/async_io.h` 의 수명 규약이 여기서 걸린다 — "제출한 버퍼와 File 은 Wait 가 끝날 때까지 살아 있어야 한다." `Slot` 이 `std::array` 멤버라 주소가 움직이지 않고, `file` 과 `bytes` 가 그 안에 있다. `IoRequest` 만 `shared_ptr` 로 완료 상태를 공유한다.

`File::ReadAt` 이 위치 지정 IO 라는 점이 이 구조를 가능하게 한다. `seek` + `read` 였다면 같은 핸들을 워커가 만지는 순간 위치가 꼬인다.

## 5. 폴링: 완료 확인과 acquire 경계

프레임 루프는 매 프레임 `Poll` 을 한 번 부른다. 아직 안 끝났으면 `false` 를 돌려주고 프레임은 계속 간다.

<!-- snippet: include/game/asset_reload.hpp@reload-poll -->
```cpp
bool Poll(DefenseSkin& skin) {
    if (!busy_) return false;
    MEGA_PROFILE_SCOPE("io.reload_poll");
    if (prepared_.load(std::memory_order_acquire) == 0) return false;   // 제출 전
    for (const Slot& s : slots_)
        if (s.ok && !s.request.Done()) return false;    // 아직 한 장이라도 안 왔다

    // ...
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
    // ...
    busy_ = false;
    prepared_.store(0, std::memory_order_release);
    ++reloads_;
    return true;
}
```

동기화가 두 겹이다.

**`prepared_`** 는 "워커가 `slots_` 를 다 채웠는가"를 말한다. release/acquire 쌍이라 그 값이 1로 보이는 순간 `slots_` 의 모든 쓰기도 보인다. 이것이 없으면 프레임 스레드가 아직 채워지지 않은 `s.request` 를 읽는다.

**`Wait()`** 는 각 읽기의 완료 경계다. `Done()` 이 이미 참인데 왜 `Wait` 을 또 부르는가 — `Done()` 은 원자 로드 하나고, 그것으로도 acquire 의미는 얻는다. 하지만 결과 구조체(`IoResult`)를 읽으려면 `Wait` 의 반환값이 필요하고, 무엇보다 **읽기가 실제로 성공했는지**를 그것으로만 안다. 부분 읽기(`result.bytes != s.bytes.size()`)를 거르는 것이 그 자리다.

`s.file.Close()` 를 Poll 에서 하는 것도 의도다. 열어 둔 채로 두면 아티스트가 그 파일을 다시 저장할 수 없다(Windows 의 공유 모드). 리로드가 끝나는 즉시 놓는다.

## 6. 워커가 없으면 비동기는 성립하지 않는다

`jobs=0` 인데 `asset_io=async` 를 켜면 어떻게 되는가.

`Submit` 은 큐에 넣는다. 워커가 없으면 아무도 그것을 집지 않는다. `prepared_` 는 영원히 0이고, `Poll` 은 영원히 `false` 를 돌려주고, `busy_` 는 영원히 참이다. **리로드가 매달린다.**

조용히 매달리는 것보다 조용히 동기로 떨어지는 것이 낫다.

```cpp
if (scheduler_ && scheduler_->WorkerCount() > 0 && !io_)
    io_.reset(new io::IoSystem(*scheduler_));
// 워커가 없으면 비동기는 성립하지 않는다 — 제출한 읽기를 아무도 처리하지
// 않아 리로드가 영영 끝나지 않는다. 조용히 매달리는 대신 동기로 떨어진다.
if (kind_ == AssetIoKind::Async && !io_) kind_ = AssetIoKind::Sync;
```

`Kind()` 가 실제 팔을 돌려주므로, 폴백이 일어났는지는 밖에서 관측된다. 테스트가 그것을 단언한다.

여기에 `Scheduler::Submit` 의 함정이 하나 더 있다. 워커가 0이면 `queues_` 가 비어 있고, `Push(1 % queues_.size(), ...)` 는 0으로 나눈다. `WorkerCount() > 0` 검사가 그것도 막는다.

## 7. 판정 (a): 픽셀이 같은가

두 팔이 같은 스프라이트를 만들었는지를 어떻게 묻는가.

스프라이트 배열을 직접 비교할 수도 있다. 하지만 그것은 "디코드 결과가 같다"까지만 답한다. 물어야 할 것은 **반영이 화면까지 도달했는가**다.

그래서 한 프레임을 실제로 그려 픽셀을 비교한다.

```cpp
HudState hud;
DrawGame(framebuffer, map, view, skin, hud, true, nullptr);

const size_t count = static_cast<size_t>(framebuffer.Width()) * framebuffer.Height();
return std::vector<uint32_t>(framebuffer.Data(), framebuffer.Data() + count);
```

480×480 = 230,400 픽셀이 하나도 다르지 않아야 한다.

리로드가 정말 새 바이트를 반영했는지도 함께 확인해야 한다. 원본을 그대로 다시 읽으면 "아무것도 안 했다"와 구분되지 않기 때문이다. 그래서 테스트는 실행 파일 옆의 에셋을 사적인 디렉터리로 복사한 뒤, 그중 두 장을 확실히 다른 1×1 단색 PPM 으로 바꾼다.

```cpp
CHECK(WriteWholeFile(staged + "/" + DefenseSkin::FileName(0), SolidPpm(17, 200, 90)));
CHECK(WriteWholeFile(staged + "/" + DefenseSkin::FileName(9), SolidPpm(240, 30, 60)));
```

원본을 건드리지 않는 것도 의도다. 실행 파일 옆의 `assets/` 는 다른 테스트도 읽는다. 그것을 바꾸면 이 테스트가 다른 테스트의 전제를 흔든다.

## 8. 판정 (b): 시간이 아니라 상태를 단언한다

비동기가 빠르다는 것을 어떻게 단언하는가.

시간으로 단언하면 안 된다. `async.requestMs < sync.requestMs` 는 머신 부하에 따라 뒤집힐 수 있고, 뒤집히는 게이트는 곧 무시당한다.

대신 **구조적 상태**를 단언한다.

```cpp
// (b) 구조적 차이: 동기 팔은 Request 안에서 전부 끝나고, 비동기 팔은 제출만
//     하고 나온다. 시간이 아니라 이 상태 차이를 단언한다 — 타이밍은 머신
//     부하에 흔들리지만 "프레임 스레드가 붙잡혔는가"는 흔들리지 않는다.
CHECK(!sync.busyAfterRequest);
CHECK(async.busyAfterRequest);
CHECK_EQ(sync.pollFrames, 0);
CHECK(async.pollFrames >= 1);
// 프레임 스레드가 Request 에 붙잡힌 시간은 비동기 팔이 더 짧아야 한다.
CHECK(async.requestMs <= sync.requestMs);
```

`busyAfterRequest` 가 핵심이다. 동기 팔은 `Request` 가 돌아온 시점에 이미 끝나 있고, 비동기 팔은 그 시점에 아직 진행 중이다. 그 차이는 부하와 무관하다.

시간 단언(`async.requestMs <= sync.requestMs`)도 남겨 뒀지만, 그것이 안전한 것은 §4 의 재설계 덕이다. 두 수의 비는 100배 이상이라 흔들림이 뒤집을 여지가 없다. 첫 구현(18% 차이)이었다면 이 줄은 플래키가 됐을 것이다.

## 9. 트레이스 덤프 — 쓰기 경로의 첫 호출자

같은 `IoSystem` 을 쓰는 두 번째 소비자가 있다. 8편의 `--trace-out` 이다.

<!-- snippet: include/game/asset_reload.hpp#WriteTraceReport -->
```cpp
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
```

이 경로의 값어치는 속도가 아니다. 프로세스가 곧 끝나므로 비동기의 이득이 사실상 없다. 값어치는 **`io::File::WriteAt` 에 제품 호출자가 생겼다**는 것이다.

P0 감사는 `io/file.cpp` 를 "링크만 되고 호출 0"으로 판정했는데, 정확히는 읽기만 쓰이고 쓰기는 아무도 부르지 않는 상태였다. 진행도 저장은 `db/kv_store.hpp` 가 자기 `FILE*` 를 쓴다. 이 한 줄이 그 공백을 메운다.

## 10. 변이: 읽지 않는 읽기

**변이 1 — 완료를 기다리지 않고 버퍼를 읽는다.** `Poll` 의 `Done()` 확인과 `Wait()` 을 둘 다 지웠다.

테스트가 **통과했다.**

```
[asset_io] sync  request=0.775ms total=0.775ms poll_frames=0 assets=10
[asset_io] async request=0.005ms total=0.749ms poll_frames=8868 assets=10
[test] async_reload: 24 checks, 0 failures
```

이 결과를 숨기지 않는다. 데이터 경쟁은 **신뢰성 있게 실패하지 않는다.** 테스트의 폴링 루프가 `prepared_` 를 기다리며 수천 번 도는 동안 워커가 이미 읽기를 다 끝냈기 때문에, 경계를 지워도 버퍼에는 올바른 바이트가 있었다.

이것은 이 검사의 한계다. "읽기 전에 쓴다"를 결정적으로 잡으려면 읽기를 인위적으로 지연시켜야 하고, 그러려면 `IoSystem` 에 테스트 전용 훅을 내야 한다. 그 훅은 제품 코드에 테스트 전용 분기를 넣는 것이라 넣지 않았다.

**변이 2 — 읽기 자체를 무력화한다.** 대신 배선을 죽이는 변이로 증명한다. `IoSystem::ReadAt` 이 제출은 하되 실제 읽기를 하지 않게 만든다.

```cpp
sched_.Submit([st, fp, offset, buf, n] {
    (void)fp; (void)offset; (void)buf;      // [MUTATION] 실제 읽기 제거
    st->result = IoResult{true, n};
    st->done.store(1, std::memory_order_release);
    sync::FutexWakeAll(&st->done);
});
```

```
[test] async_reload
[asset_io] sync  request=0.780ms total=0.780ms poll_frames=0 assets=10
[asset_io] async request=0.006ms total=0.940ms poll_frames=8245 assets=0
  FAIL ...\tests\test_async_reload.cpp:165  CHECK(sync.pixels == async.pixels)
  FAIL ...\tests\test_async_reload.cpp:167  CHECK_EQ(async.externalAssets, DefenseSkin::kFileCount)
```

`assets=0` 이 그대로 나온다. 버퍼가 0으로 남아 `DecodePpmP3` 가 열 장 모두 거부했고, 스킨은 절차 생성 폴백으로 그려졌으며, 그 차이가 픽셀 비교에 잡혔다.

이 변이가 증명하는 것은 정확히 "비동기 팔이 진짜로 `io/async_io.h` 를 지나 디스크를 읽는다"이다. 판정 (a)가 없었다면 — 시간만 쟀다면 — 읽지 않는 구현이 **더 빠른** 결과를 내며 통과했을 것이다.

복원 후 재통과했다(§11).

## 11. 실행과 관측

### 두 팔의 리로드

```powershell
cd C:/Users/REIN/Documents/VeLog/projects/mega
cmake --build build --config Release
.\build\bin\Release\test_async_reload.exe
```

세 번 연속 실행:

```
[asset_io] sync  request=0.800ms total=0.801ms poll_frames=0 assets=10
[asset_io] async request=0.004ms total=0.920ms poll_frames=9495 assets=10
[asset_io] sync  request=0.698ms total=0.698ms poll_frames=0 assets=10
[asset_io] async request=0.006ms total=0.764ms poll_frames=10951 assets=10
[asset_io] sync  request=1.135ms total=1.136ms poll_frames=0 assets=10
[asset_io] async request=0.006ms total=1.576ms poll_frames=22959 assets=10
```

| 회차 | sync request | async request | 비 |
|---|---|---|---|
| 1 | 0.800ms | 0.004ms | 200배 |
| 2 | 0.698ms | 0.006ms | 116배 |
| 3 | 1.135ms | 0.006ms | 189배 |

`request` 가 프레임 스레드가 붙잡힌 시간이다. `total` 은 요청부터 반영까지의 벽시계 시간인데, 비동기 팔의 `total` 이 sync 보다 **크다** — 당연하다. 같은 일을 하고 스레드 간 왕복이 더 있다. 줄어든 것은 총 시간이 아니라 **프레임 스레드가 멈춘 시간**이다.

`poll_frames` 가 만 단위인 것은 테스트가 프레임 루프 대신 타이트 루프로 폴링하기 때문이다. 실제 프레임 루프(16.7ms 주기)에서는 1~2프레임이다.

`assets=10` 이 두 팔 모두에 있고, 그 열 장 중 둘은 테스트가 바꿔치기한 1×1 단색 PPM 이다.

### 픽셀 비교

`24 checks` 안에 480×480 프레임의 전체 비교가 들어 있다(`CHECK(sync.pixels == async.pixels)` 한 줄이 230,400 픽셀을 본다).

### 워커 없는 비동기

```
CHECK(reloader.Kind() == AssetIoKind::Sync);   // 워커 없으면 폴백
CHECK(!reloader.Busy());
CHECK_EQ(skin.externalAssets, DefenseSkin::kFileCount);
```

### 트레이스 덤프 두 팔

```powershell
.\build\bin\Release\mega_defense.exe --selftest 200 --trace-out trace_demo.tsv
```

```
[trace] wrote trace_demo.tsv
```

```
scope   count   total_ms        max_ms  mean_ms
frame   200     123.537 1.178   0.6177
render.draw     200     122.690 1.171   0.6135
sim.step        200     0.223   0.005   0.0011
...
```

### 리로드를 켠 제품

```powershell
.\build\bin\Release\mega_defense.exe --ab jobs=2 --ab asset_io=async --selftest
```

```
[switches] entity_store=aos id_map=linear frame_alloc=malloc blob_alloc=malloc body_alloc=vector worker_lock=std asset_io=async scale=small jobs=2 affinity=0 workers=2 lock=std
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
```

### 변이 복원 후

```powershell
.\build\bin\Release\test_async_reload.exe
```

```
[test] async_reload: 24 checks, 0 failures
```

---

## 결론

매치 중 에셋 리로드가 프레임 스레드에서 떨어졌다. 붙잡히는 시간이 0.7~1.1ms 에서 0.005ms 로, 100배 이상 줄었다.

그 100배는 첫 구현에서 얻은 것이 아니다. 첫 구현은 18% 였고, 그 수를 보고서야 **파일을 여는 것도 시스템 콜**이라는 것이 비용의 대부분임을 알았다. 읽기만 옮기는 것으로는 부족했다.

판정 순서가 값을 했다. 픽셀 비교(a)가 없었다면 "읽지 않는 읽기" 변이가 더 빠른 결과를 내며 통과했을 것이다.

그리고 `io::File::WriteAt` 이 제품 호출자를 얻었다. 트레이스 덤프가 그 경로다.

현재 상태의 한계.

**디코드는 여전히 프레임 스레드에 있다.** 워커가 가져오는 것은 바이트이고, PPM 파싱은 `Poll` 안에서 일어난다. 그 파싱이 `std::istringstream` 기반이라 결코 싸지 않다 — 리로드가 완료되는 프레임 하나는 여전히 디코드만큼 무겁다. 디코드까지 옮기려면 워커가 `render::Sprite` 를 만들어야 하고, 그러면 `io/async_io.h` 의 인터페이스(바이트 읽기)를 넘어서 잡 스케줄러를 직접 쓰는 형태가 된다.

**"읽기 전에 쓴다"를 결정적으로 잡지 못한다.** 데이터 경쟁을 재현하려면 읽기를 인위적으로 지연시켜야 하고, 그 훅은 제품 코드에 테스트 전용 분기를 넣는 일이라 넣지 않았다. 대신 배선을 죽이는 변이로 "정말 읽는가"만 증명했다.

**리로드 대상이 스프라이트뿐이다.** 맵·웨이브·밸런스는 다시 읽지 않는다. 매치 중에 맵이 바뀌면 경로 인덱스를 들고 있는 모든 적이 무효가 되고, 웨이브가 바뀌면 진행 중인 웨이브 인덱스의 의미가 달라진다. 그 재구성은 리로드가 아니라 재시작이다.

**부분 실패가 전체 폴백이 된다.** 열 장 중 한 장이 깨져 있으면 그 슬롯만 절차 생성 폴백으로 남는 것이 아니라, `Poll` 이 `BuildFallbacks()` 로 전부 초기화한 뒤 성공한 것만 덮어쓴다. 결과는 같지만 비용이 다르다 — 성공한 아홉 장도 다시 디코드된다.

**동기 팔은 여전히 프레임을 멈춘다.** 기본값이 `sync` 이므로, 스위치를 켜지 않은 실행에서 `L` 을 누르면 그 프레임은 1ms 를 잃는다. 기본값을 `async` 로 바꾸지 않은 이유는 규약이다 — 기본값은 언제나 스위치 이전의 동작이다.
