## 들어가며

시작 상태의 결과물은 read(2) 한 가지로 도는 단일 스레드 baseline + ImGui HUD다. read 호출이 `FileIoWorkload::RunCycle` 안에 *하드코딩* 되어 있어 다른 IO 모델(pread/mmap/aio/io_uring)을 같은 워크로드에 끼우려면 *그 함수를 매번 갈아엎어야* 한다. 같은 워크로드 위에서 *다른 backend* 를 비교하는 게 이 lab의 핵심인데, 비교 대상이 하나밖에 없는 상태다.

이 단계의 목표는 *IO 백엔드 추상* 을 얇게 세우는 것이다. `IIoBackend` 인터페이스가 한 사이클의 IO 동작을 정의하고, `ReadBackend` 가 그 첫 구현이다. `FileIoWorkload` 는 backend를 *외부에서 주입받아* 호출만 한다. 토글 UI는 현재 범위 밖의 확장에서 들어오고, 이 단계에서는 *모듈 경계* 만 정리한다. 결과 동작은 시작 상태와 *완전히 동일* — read baseline. 차이는 *내부 구조* 다.

이 단계의 결과물은 다음 다섯 가지다.

1. `IIoBackend` 추상 인터페이스 (`io/io_backend.h`).
2. `ReadBackend` — read 동기 백엔드 (`io/read_backend.cpp`).
3. `BackendType` enum + `CreateBackend` 팩토리 (`io/io_backend_factory.cpp`).
4. `FileIoWorkload` 가 backend 포인터를 받도록 변경.
5. main에서 backend 인스턴스 생성 후 워크로드에 주입.

토글이 없는데 굳이 인터페이스를 먼저 세우는 이유는 *누적성* 이다. 토글 UI와 두 번째·세 번째 백엔드(pread/mmap)가 동시에 들어오면 변경 범위가 두 곳으로 늘고 인터페이스 결정이 *실측에 쫓겨* 어색해진다. 인터페이스를 먼저 단단히 두면 완성된 구성들이 *구현만 추가* 하는 깔끔한 누적이 된다.

이 글의 코드 스니펫은 *해당 단계의 스냅샷* 이다. 디스크의 통합 코드는 누적되어 더 진행된 상태일 수 있다.

---

## 목차

1. 왜 인터페이스가 먼저인가
2. IIoBackend 인터페이스 설계
3. ReadBackend 구현 — read 로직의 이전
4. BackendType enum과 팩토리
5. FileIoWorkload의 backend 주입
6. main 통합과 빌드 결과

---

## 1. 왜 인터페이스가 먼저인가

이 단계의 *결과 동작은 변하지 않는다*. read backend로 도는 baseline이 그대로다. 그런데 코드는 절반쯤 갈렸다. 왜?

이 lab의 비교 대상은 다섯 가지 IO 모델이다.

- `read(2)` — 동기, 시작 위치는 fd offset
- `pread(2)` — 동기, 시작 위치를 인자로 (offset 인자가 thread-safe)
- `mmap(2) + sampled scan` — 페이지 매핑, 페이지 폴트가 데이터 관찰 시점
- `aio_read` (POSIX AIO) — 비동기 요청 API, Linux에서는 구현 방식이 환경 의존
- `io_uring_submit` — 비동기, 링버퍼 기반

다섯이 *다른 모델* 이다. 동기·비동기 차이가 있고, 시스템 콜 단위가 다르고, 결과 관찰 시점도 다르다. 그렇다면 *공통 인터페이스* 를 어떻게 잡을 것인가가 핵심이다. 잘못 잡으면 한 모델에는 자연스럽고 다른 모델에는 어색한 어댑터가 된다.

### 두 가지 잘못된 시작점

**A. 인터페이스를 *동기 read 모양* 으로 잡는다.**

```cpp
class IIoBackend {
    virtual std::size_t Read(int fd, void* buf, std::size_t bytes) = 0;
};
```

read는 자연스럽지만, mmap은 fd가 아니라 *주소* 로 작업하고, io_uring은 *제출 + 수확* 두 단계라 한 호출에 매핑이 어색하다. async는 *결과가 호출 직후 안 오는* 모델인데 인터페이스가 *값을 반환* 하면 sync 가정이 박힌다.

**B. 인터페이스를 너무 *넓게* 잡는다.**

```cpp
class IIoBackend {
    virtual void OpenAll() = 0;
    virtual void SubmitAll() = 0;
    virtual void Reap() = 0;
    virtual void CloseAll() = 0;
    virtual std::vector<Buffer> Results() = 0;
};
```

io_uring에는 잘 맞지만 read에는 *Submit과 Reap이 같은 호출* 이라 오버헤드만 늘고 의미가 흐려진다. 동기 backend가 *async를 흉내내는* 어색한 형태가 된다.

### 좋은 시작점: 사이클 단위 + 콜백

이 lab의 워크로드는 한 사이클이 *N개 파일을 모두 읽는* 단위다. 그러면 인터페이스도 *사이클 단위* 가 자연스럽다.

```cpp
class IIoBackend {
public:
    using OnFileRead = std::function<void(const unsigned char* data,
                                           std::size_t bytes)>;

    virtual std::uint64_t RunCycle(const std::vector<std::string>& paths,
                                    std::size_t fileBytes,
                                    const OnFileRead& onRead) = 0;
    virtual const char* Name() const = 0;
};
```

세 결정 포인트.

- **사이클 단위** : 한 호출이 *모든 파일* 을 처리한다. 동기 backend는 순차 read, async backend는 batch submit + reap. 둘 다 *같은 시그니처* 안에서 자연스럽다.
- **결과 콜백** : `onRead` 가 *각 파일이 관찰 가능한 시점* 에 호출된다. 동기는 read 직후, async는 reap 시점. 호출자(워크로드)는 *어떤 모델인지 신경 안 쓰고* 결과를 받는다.
- **소유권 단순화** : backend는 자기 임시 버퍼를 직접 들고, 콜백 인자로 *읽은 바이트와 길이만* 넘긴다. 호출자가 버퍼를 미리 잡지 않아도 된다.

이 모델은 *모든 다섯 backend가 자기 자연스러운 형태로* 구현되게 둔다.

| backend | RunCycle 안에서 |
|---|---|
| ReadBackend | for paths: open → read 루프 → close → onRead |
| PreadBackend | for paths: open → pread → close → onRead |
| MmapBackend | for paths: open → mmap → onRead(mapped) → munmap → close |
| AioBackend | `aiocb` 배열 prep → `aio_read` → `aio_suspend`/`aio_error`/`aio_return` → 완료한 것부터 onRead |
| UringBackend | SQE 배열 prep → io_uring_submit → CQE 수확 → 완료 순서대로 onRead |

다섯 모두 *같은 함수 시그니처* 안에서 자기 모델대로 동작한다. 인터페이스가 모델을 *강제하지 않는다* 는 점이 핵심이다.

여기서 `AioBackend`는 POSIX AIO를 뜻한다. Linux native AIO(libaio)의 `iocb`, `io_submit`, `io_getevents`와는 API가 다르다. POSIX AIO는 파일 시스템과 libc에 따라 내부 구현이 달라질 수 있으므로, 결과를 io_uring과 같은 커널 링 버퍼 모델의 특성으로 해석해서는 안 된다.

### 결과 도착 순서가 다르다는 사실 자체가 측정 대상

async backend는 *제출 순서* 와 *결과 도착 순서* 가 다를 수 있다. 디스크 IO 스케줄러가 더 빨리 끝낼 수 있는 요청을 먼저 완료시킨다. 이 lab에서 측정에 영향을 주지는 않는다 (sampled checksum이 *순서 무관* 한 누적이라). 실제 게임 엔진의 에셋 로딩 같은 시나리오에서는 *완료 순서* 가 다음 작업의 시작 시점에 영향을 주는데, 이 lab은 단순 누적이라 그 효과를 측정하지 않는다.

현재 범위 밖의 확장에서 *완료 순서가 frame ms 분포에 어떻게 영향* 을 주는지를 보고 싶으면 onRead 콜백 안에 의존성을 넣는 모드를 추가하면 된다.

## 2. IIoBackend 인터페이스 설계

`src/io/io_backend.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace skl {

class IIoBackend {
public:
    virtual ~IIoBackend() = default;

    using OnFileRead = std::function<void(const unsigned char* data,
                                           std::size_t bytes)>;

    // data 포인터는 onRead 호출 중에만 유효하다.
    // 비동기 구현도 RunCycle 안에서 모든 완료 수확과 onRead 호출을 끝내고 반환한다.
    virtual std::uint64_t RunCycle(const std::vector<std::string>& paths,
                                    std::size_t fileBytes,
                                    const OnFileRead& onRead) = 0;

    virtual const char* Name() const = 0;
};

}  // namespace skl
```

40줄도 안 되는 단순한 인터페이스다. 이게 모든 backend의 공통 약속이다.

### `std::function` 의 비용

`std::function` 은 type erasure가 들어간다. `FileIoWorkload::RunCycle` 이 람다를 넘길 때 작은 type-erased 객체가 만들어지고, 파일별 `onRead` 호출에는 *간접 호출 한 번* 비용이 붙는다. 16개 파일 한 사이클에서 16번 호출이라 측정 노이즈 안에 묻힌다.

대안으로 *템플릿 콜백* 도 가능하다.

```cpp
template <typename F>
std::uint64_t RunCycle(const std::vector<std::string>& paths,
                        std::size_t fileBytes,
                        F&& onRead);
```

템플릿은 *간접 호출 0* 이라 더 빠르지만, *가상 함수와 결합 불가* 다. 가상 + 템플릿 조합을 하려면 *type erasure 어댑터* 를 직접 짜야 하는데, 이미 `std::function` 이 그걸 한다.

이 lab은 *backend 다형성이 핵심* 이라 가상 함수가 우선이고, 콜백 호출 비용이 측정 차이를 만들 만큼 크지 않으니 `std::function` 으로 둔다.

### 구조

```mermaid
flowchart LR
    W[FileIoWorkload<br/>RunCycle] -->|backend->RunCycle paths fileBytes onRead| B[IIoBackend]
    B -->|onRead data bytes| W
    W -->|sampled checksum 누적| C[lastChecksum]
```

워크로드가 backend의 RunCycle을 호출하고, backend는 자기 방식대로 IO를 한 뒤 *각 파일 관찰 시점* 에 onRead로 워크로드에 알린다. 워크로드는 onRead 안에서 sampled checksum을 누적한다.

## 3. ReadBackend 구현 — read 로직의 이전

시작 상태의 `FileIoWorkload::RunCycle` 안에 있던 read 코드를 *그대로* 옮긴다. 변경 사항은 함수 위치와 시그니처뿐이고, *동작은 동일* 하다.

`src/io/read_backend.h`:

```cpp
#pragma once

#include "io/io_backend.h"

#include <vector>

namespace skl {

class ReadBackend final : public IIoBackend {
public:
    std::uint64_t RunCycle(const std::vector<std::string>& paths,
                            std::size_t fileBytes,
                            const OnFileRead& onRead) override;

    const char* Name() const override { return "read"; }

private:
    // 파일당 임시 버퍼. 사이클 간 재사용.
    std::vector<unsigned char> buffer_;
};

}  // namespace skl
```

`final` 키워드는 *다른 클래스가 ReadBackend를 상속하지 않는다* 는 뜻. 컴파일러가 가상 호출을 *직접 호출로 디버추얼라이즈* 할 여지를 준다. 작은 최적화지만 *클래스가 상속 의도가 없음* 을 코드로 표현하는 의미가 더 크다.

`src/io/read_backend.cpp`:

```cpp
#include "io/read_backend.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace skl {

std::uint64_t ReadBackend::RunCycle(const std::vector<std::string>& paths,
                                      std::size_t fileBytes,
                                      const OnFileRead& onRead) {
    if (buffer_.size() < fileBytes) {
        buffer_.resize(fileBytes);
    }

    std::uint64_t totalBytes = 0;

    for (const auto& path : paths) {
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error(std::string("open failed: ") + path
                                     + ": " + std::strerror(errno));
        }

        std::size_t pos = 0;
        while (pos < fileBytes) {
            const ssize_t n = ::read(fd, buffer_.data() + pos,
                                      fileBytes - pos);
            if (n < 0) {
                if (errno == EINTR) continue;
                const std::string msg = std::string("read failed: ") + path
                                      + ": " + std::strerror(errno);
                ::close(fd);
                throw std::runtime_error(msg);
            }
            if (n == 0) break;  // EOF (파일이 fixture보다 작을 때)
            pos += static_cast<std::size_t>(n);
        }
        ::close(fd);

        onRead(buffer_.data(), pos);
        totalBytes += pos;
    }

    return totalBytes;
}

}  // namespace skl
```

세 가지 변경.

1. **buffer가 backend의 멤버**: 시작 상태에서는 워크로드가 들고 있었다. 백엔드별로 *임시 버퍼 모양이 다를 수 있어* (mmap은 별도 버퍼 없이 mapped address를 넘길 수 있고, POSIX AIO는 `aiocb` 배열이 추가된다) backend 측으로 옮긴다.
2. **콜백 호출**: `onRead(buffer_.data(), pos)` 한 줄이 워크로드의 checksum 누적을 트리거한다.
3. **에러 처리는 그대로**: open/read 실패가 throw로 드러난다. fd leak 방지를 위해 close가 throw 전에 호출된다.

### EINTR 재시도가 중요한 이유

`read` 가 -1을 반환하고 `errno == EINTR` 이면 *시그널이 도착해서 read가 끊긴* 것이지 실제 IO 에러가 아니다. 단순히 한 번 더 호출하면 된다. 이 패턴이 빠지면 시그널이 도착할 때마다 throw가 나서 도구가 무작위로 죽는다.

WSL2 환경에서 시그널 도착이 흔하지는 않지만, 현재 범위 밖의 확장에서 SCHED_FIFO 같은 시그널-사용 코드가 들어오면 EINTR 빈도가 올라간다. 미리 처리해 두는 게 안전하다.

### `final` 의 효과

`ReadBackend final : public IIoBackend` 에서 backend의 가상 함수 호출을 디버추얼라이즈할 수 있을까?

컴파일러는 호출 시점에 *정적 타입* 이 `IIoBackend*` 라면 디버추얼라이즈를 못 한다. 그러나 *정적 타입이 ReadBackend*` 라면 `final` 정보로 *상속이 더 없음* 을 알아 직접 호출로 변환한다.

이 도구는 `std::unique_ptr<IIoBackend>` 로 다형 호출을 하므로 디버추얼라이즈가 *대부분 안 일어난다*. `final` 은 상속 의도가 없다는 코드 자체의 의미 표시가 본질이다.

## 4. BackendType enum과 팩토리

토글 UI는 현재 범위 밖의 확장에서 들어온다. 이 단계는 enum과 factory만 둔다.

`src/io/io_backend_factory.h`:

```cpp
#pragma once

#include "io/io_backend.h"

#include <memory>

namespace skl {

enum class BackendType : int {
    Read = 0,
};

constexpr int kBackendTypeCount = 1;

std::unique_ptr<IIoBackend> CreateBackend(BackendType type);

}  // namespace skl
```

이 단계의 enum은 `Read` 한 가지. 현재 범위 밖의 확장에서 `Pread`, `Mmap`, `Aio`, `Uring` 이 추가된다. enum이 확장될 때 `kBackendTypeCount` 도 같이 갱신된다.

### enum의 명시적 정수 값

`Read = 0` 으로 *시작 값을 명시* 한다. raygui나 ImGui의 콤보박스가 *정수 인덱스* 를 in-out으로 받는데, enum 값이 0부터 연속해야 `static_cast<int>` 가 안전하다. 이 lab은 ImGui라 자동이지만 *습관적으로 명시* 해 두면 토글 UI에서 자연스럽다.

### 팩토리 구현

`src/io/io_backend_factory.cpp`:

```cpp
#include "io/io_backend_factory.h"

#include "io/read_backend.h"

namespace skl {

std::unique_ptr<IIoBackend> CreateBackend(BackendType type) {
    switch (type) {
        case BackendType::Read:
            return std::make_unique<ReadBackend>();
    }
    return std::make_unique<ReadBackend>();
}

}  // namespace skl
```

팩토리는 현재 존재하는 `ReadBackend`만 생성한다. 이 구조의 핵심은 열거형에서 구현을 선택하는 책임을 한곳에 모으는 것이다. 사용할 수 없는 백엔드를 조용히 read로 폴백시키면 측정 라벨과 실제 실행 경로가 달라지므로, 지원하지 않는 값은 명시적으로 거부해야 한다.

## 5. FileIoWorkload의 backend 주입

`FileIoWorkload` 가 backend 포인터를 받도록 변경한다. 소유권은 호출자에게 있고, 워크로드는 *참조만* 한다.

### 헤더 변경

```cpp
// src/workload/file_io_workload.h
#pragma once

#include "io/io_backend.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace skl {

class FileIoWorkload {
public:
    void Init(IIoBackend* backend,
              std::size_t fileCount,
              std::size_t fileBytes,
              const std::filesystem::path& fixtureDir = {});

    // backend만 교체. fixture와 워크로드 파라미터는 유지.
    void SetBackend(IIoBackend* backend) { backend_ = backend; }

    std::uint64_t RunCycle();
    void Shutdown();

    std::size_t   FileCount()    const { return paths_.size(); }
    std::size_t   FileBytes()    const { return fileBytes_; }
    std::uint64_t LastChecksum() const { return lastChecksum_; }
    const std::filesystem::path& FixtureDir() const { return fixtureDir_; }
    IIoBackend*   Backend()      const { return backend_; }

private:
    void GenerateFixture(std::size_t fileCount, std::size_t fileBytes);

    IIoBackend*                  backend_   = nullptr;
    std::filesystem::path        fixtureDir_;
    std::vector<std::string>     paths_;
    std::size_t                  fileBytes_ = 0;
    std::uint64_t                lastChecksum_ = 0;
};

}  // namespace skl
```

세 가지 추가.

- **`IIoBackend* backend_` 멤버**: nullptr default. Init에서 받는다.
- **`SetBackend`**: backend 만 교체. 토글 변경 시 *fixture를 재생성하지 않고 backend만* 갈아끼우는 데 쓴다.
- **`Backend()`**: HUD가 backend 이름을 표시하기 위해 노출. const 포인터로 충분하지만 단순화를 위해 non-const.

### RunCycle 변경

```cpp
std::uint64_t FileIoWorkload::RunCycle() {
    if (!backend_) {
        throw std::runtime_error("FileIoWorkload::RunCycle: backend is null");
    }

    std::uint64_t checksum = lastChecksum_;
    auto onRead = [&](const unsigned char* data, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; i += kChecksumStride) {
            checksum += data[i];
        }
        if (bytes > 0 && ((bytes - 1) % kChecksumStride) != 0) {
            checksum += data[bytes - 1];
        }
    };

    const std::uint64_t total = backend_->RunCycle(paths_, fileBytes_, onRead);
    lastChecksum_ = checksum;
    return total;
}
```

시작 상태의 `for path: open → read → close → checksum` 루프가 *backend 호출 한 줄과 onRead 람다* 로 바뀌었다. 핵심이 *명확히 분리* 되어 있다.

- **워크로드의 책임**: 어떤 파일을, 몇 개, 어떤 가공으로.
- **backend의 책임**: 그 파일들을 *어떤 IO 모델로* 읽을지.

이 분리가 이후의 누적을 매끄럽게 한다.

### 람다 capture와 수명

`onRead` 람다는 `checksum` 을 *참조 캡처* (`[&]`) 한다. 람다 객체의 수명이 `RunCycle` 함수 안 (스택)이고 backend의 RunCycle도 그 안에서 끝나므로 dangling 위험이 없다.

backend가 *비동기로 onRead를 *RunCycle 종료 후* 호출* 하는 형태라면 dangling이 생기지만, 우리 인터페이스는 *RunCycle이 모든 onRead가 끝난 뒤에야 반환* 한다는 약속이다. async backend도 RunCycle 안에서 *수확까지 마치고* 반환한다.

## 6. main 통합과 빌드 결과

main에서 backend를 만들고 워크로드에 주입한다.

```cpp
// src/main.cpp 일부

#include "io/io_backend.h"
#include "io/io_backend_factory.h"
#include "measure/frame_timer.h"
#include "ui/hud.h"
#include "workload/file_io_workload.h"

// ... (SDL2/ImGui 초기화)

auto backend = skl::CreateBackend(skl::BackendType::Read);

skl::FileIoWorkload workload;
workload.Init(backend.get(), kFileCount, kFileBytes);
```

두 줄 추가, 한 줄 변경. `unique_ptr<IIoBackend>` 가 main 스택에 살고, 워크로드는 *생포인터* 로 참조한다. main이 종료되면 backend도 자동 정리된다.

### HUD에 backend 이름 추가

```cpp
const IIoBackend* backend = workload.Backend();
const char* backendName = backend ? backend->Name() : "(none)";

char buf[640];
std::snprintf(buf, sizeof(buf),
              "backend     : %s\n"
              "files       : %zu  x  %.2f MB\n"
              ...
```

stats 텍스트의 첫 줄에 `backend : read` 가 표시된다. 토글 변경 시 이 이름이 바뀐다.

### CMakeLists 변경

```cmake
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/io/io_backend_factory.cpp
    src/io/read_backend.cpp
    src/measure/frame_timer.cpp
    src/workload/file_io_workload.cpp
    src/ui/hud.cpp
    ${IMGUI_SOURCES}
)
```

두 cpp 파일이 빌드 대상에 추가됐다. 그 외 변경 없음.

### 빌드와 동작

WSL2에서 재빌드한다.

```bash
cmake --build build -j
./build/system_kernel_lab
```

화면은 시작 상태와 *완전히 동일* 하다. cycle ms, throughput 그래프가 같은 모양으로 그려진다. 차이는 stats 텍스트 첫 줄에 `backend : read` 가 추가된 것뿐.

### 무엇이 달라졌는가

이 단계의 *결과 동작이 변하지 않은* 게 핵심이다. 측정 도구는 *경계가 늘면 동작이 바뀌었는지 즉시 검증할 수 있다*. 이 단계의 검증은 다음과 같이 한다.

1. 시작 상태의 cycle ms 평균을 기록 (예: 3.4 ms).
2. 이 단계로 빌드, 실행.
3. cycle ms 평균이 *같거나 측정 노이즈 안* 인지 확인.

만약 *눈에 띄게 느려졌다면* 인터페이스 도입에 비용이 들어간 신호다. `std::function` 호출 비용, 가상 함수 디스패치 비용이 누적된 결과일 수 있다. 이 lab의 워크로드는 호출이 *파일 수만큼* (16번)이라 그 비용이 측정 노이즈 안에 묻혀야 정상이다.

만약 *빨라졌다면* (드물지만) 컴파일러가 read 코드를 다른 위치에 두면서 inline/캐시 행동이 우연히 좋아진 것. 차이가 의미 있을 정도면 측정 환경의 노이즈를 의심해 본다.

### 검증 체크리스트

- 빌드 시 `IIoBackend` 헤더 못 찾음 → `target_include_directories` 의 `src` 가 빠짐. CMakeLists 확인.
- 링크 오류 *undefined reference to ReadBackend::RunCycle* → `read_backend.cpp` 가 빌드 대상에 빠짐.
- 실행 시 *FileIoWorkload::RunCycle: backend is null* → main에서 `workload.Init(backend.get(), ...)` 의 첫 인자가 null. backend 인스턴스가 만들어졌는지 확인.
- HUD에 backend 이름이 안 보임 → `workload.Backend()` 가 nullptr 반환. main의 Init 호출 시점 확인.

### 측정의 정직성

이 단계는 *측정 결과를 바꾸지 않는 리팩토링* 이다. *코드 분량은 늘고 동작은 같은* 단계라 그래프에는 변화가 없다. 그래도 이 단계가 필요한 이유는 *현재 범위 밖의 확장의 변경 범위* 를 좁히기 위해서다.

토글 UI + pread + mmap 이 들어올 때 *지금* 인터페이스가 없으면 그 단계가 *세 가지 작업을 동시에* 한다 — 인터페이스 설계, UI, 두 backend 추가. 변경 범위가 커지면 *어디서 뭐가 깨졌는지* 추적이 어렵다.

이 단계의 가치는 측정 결과를 바꾸지 않은 채 변경 지점을 좁힌 데 있다. 백엔드 선택, 수명, 워크로드 호출 경계가 분리되었으므로 새 구현을 넣더라도 baseline 경로와 비교 기준을 유지할 수 있다.

---

## 결론

이 단계의 결과물은 read backend의 코드가 `IIoBackend` 추상 뒤로 옮겨가고, FileIoWorkload가 backend를 주입받는 구조다. 약 100줄의 새 코드(`io/` 디렉토리 + 워크로드 시그니처 변경 + main 통합)가 시작 상태 위에 얹혔다. *측정 결과는 변하지 않는다*.

세 가지 결정이 이 단계의 토대를 만들었다.

1. **사이클 단위 + 콜백 인터페이스** — 동기·비동기 다섯 모델을 같은 시그니처에 자연스럽게 담는다. backend는 자기 모델대로 IO를 하고, 결과는 콜백으로 워크로드에 알린다.
2. **인터페이스를 토글 UI보다 먼저** — *변경 범위를 좁히는* 단계. 이후 단계가 UI와 두 backend 추가에만 집중할 수 있게 모듈 경계를 미리 그어 둔다.
3. **소유권은 호출자, 사용은 워크로드** — backend는 main이 들고 있고 워크로드는 *참조만* 한다. 토글 변경 시 *backend만 교체* 가 자연스러운 패턴이 된다.

현재 코드는 backend 한 가지로만 도는 단일 경로다. 따라서 인터페이스 분리가 동작 보존에는 성공했지만, 서로 다른 IO 모델의 비용 차이를 보여 주지는 못한다.

## 더 읽을거리

- Robert Love, *Linux System Programming*, 2nd ed., Ch. 2 "File I/O" — read/pread의 모델 차이
- Brendan Gregg, *Systems Performance*, Ch. 8 "File Systems" — IO 모델별 측정 패턴
- [C++ Core Guidelines: I.25 — Prefer abstract classes as interfaces](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#i25-prefer-abstract-classes-as-interfaces-to-class-hierarchies)
- Sean Parent, "Inheritance Is The Base Class of Evil" (GoingNative 2013) — 타입 erasure 패턴
- [`std::function` 의 type erasure 구현](https://en.cppreference.com/w/cpp/utility/functional/function) — 콜백 인터페이스의 비용
- [Linux man page: read(2)](https://man7.org/linux/man-pages/man2/read.2.html) — EINTR 동작
- Jeff Dean & Sanjay Ghemawat, "MapReduce: Simplified Data Processing on Large Clusters" — 사이클 단위 추상화의 고전적 사례
