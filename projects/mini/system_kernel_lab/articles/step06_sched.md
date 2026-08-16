## 들어가며

현재 도구는 backend·sync·worker count 토글로 *워커 풀의 IO·동기화 비용* 을 비교한다. 워커 스레드가 *어느 코어에 어떻게 할당되는지* 는 OS 스케줄러에 맡긴 상태고, *스케줄링 정책 자체* 가 측정 변수에 들어오지 않았다. nice / SCHED_FIFO / cgroup cpu.weight 같은 커널 정책이 그대로 남아 있다.

이 단계의 목표는 *스케줄링 정책 토글* 을 추가하는 것이다. 워커 스레드에 nice 값을 변경하거나(SCHED_OTHER 안에서 양보 정도 조절), SCHED_FIFO 실시간 정책을 적용하거나(다른 nice 스레드를 선점), 현재 cgroup의 cpu.weight를 늘려 같은 시스템의 다른 작업 대비 *우선 받기* 를 시도한다. 측정 결과로 *백그라운드에서 다른 작업이 도는 환경* 에서 워커가 어떻게 영향을 받거나 받지 않는지가 그래프에 드러난다.

스케줄링은 *권한 의존* 이 크다. SCHED_FIFO는 보통 CAP_SYS_NICE 또는 root, cgroup cpu.weight 쓰기는 cgroup v2 환경 + 적절한 권한이 필요하다. 권한 부족 시 *stderr에 안내 + 기본 정책으로 폴백* 하는 best-effort 패턴으로 도구가 죽지 않게 한다.

이 단계의 결과물은 다음 다섯 가지다.

1. `SchedType` enum + `kSchedTypeLabels` (`sched/sched_policy.h`).
2. `ApplySchedToCurrentThread` / `ApplySchedProcessSetup` 두 진입점.
3. `WorkerLoop` 시작 시 정책 적용 + Init/Reconfigure에서 프로세스 단위 셋업.
4. `Toggles::sched` + Reconfigure 시그니처에 SchedType 추가.
5. ImGui HUD에 sched 콤보 + stats 라인 추가.

코드 스니펫은 이 단계에서 추가되는 핵심 경로만 보인다. 오류 처리와 보조 함수는 디스크의 통합 코드에 더 자세히 들어 있다.

---

## 목차

1. 네 가지 스케줄링 정책의 의미
2. 워커별 적용 vs 프로세스 적용의 분리
3. ApplySchedToCurrentThread 구현
4. cgroup v2 cpu.weight 쓰기
5. 워커 풀 통합과 권한 폴백
6. HUD 콤보와 사용 시나리오
7. 빌드와 측정 결과
8. 검증 — 권한이 없을 때의 동작이 계약이다

---

## 1. 네 가지 스케줄링 정책의 의미

이 lab은 네 가지 정책을 토글한다.

| 토글 값 | 정책 | 의미 |
|---|---|---|
| `Default` | `SCHED_OTHER`, nice 0 | 시스템 기본. CFS 안에서 다른 nice 0 스레드와 동등한 가중치 |
| `Nice` | `SCHED_OTHER`, nice +10 | CFS 안에서 *양보 많이*. 백그라운드 작업이 있을 때 우선순위가 낮아진다 |
| `Fifo` | `SCHED_FIFO`, prio 50 | 실시간 정책. 같은 코어의 nice 스레드를 *선점*. 권한 필요 |
| `Cgroup` | cgroup v2 cpu.weight 200 | 사용자가 명시 승인한 현재 cgroup의 가중치를 200으로 설정 |

### CFS와 nice

`SCHED_OTHER` 는 Linux의 *Completely Fair Scheduler* (CFS) 안에서 도는 정책이다. CFS는 모든 스레드에게 *공정하게* CPU 시간을 분배하는데, *공정* 의 가중치를 nice 값으로 조절한다.

- `nice 0`: 기본. 다른 nice 0 스레드와 *같은 분량* CPU.
- `nice +10`: 가중치가 약 1/10 수준 (정확히는 1.25^10 비율). CPU가 부족할 때 *덜 받는다*.
- `nice -10`: 가중치가 약 10배. *더 받는다*. 다만 음의 nice는 root 권한이 필요.

이 lab의 워크로드는 *CPU 한정* 이 아니라 *IO 대기 + 짧은 가공* 이라 nice의 효과가 *측정에 강하게 드러나지 않을 수 있다*. 효과를 보려면 *백그라운드에 CPU를 쓰는 다른 프로세스* 가 있어야 한다.

### SCHED_FIFO

`SCHED_FIFO` 는 *실시간 정책*. CFS의 nice 스레드를 모두 *선점* 해 같은 코어의 CFS 작업이 멈춘다. 적용된 스레드는 자기 우선순위 내에서 *FIFO 순서* (먼저 진입 먼저 실행) 로 동작한다.

이 정책의 위험: *쓸데없이 CPU를 점유* 해 시스템을 굶게 만들 수 있다. 그래서 권한이 제한되어 있다.

- 일반 사용자: `ulimit -r` (`RLIMIT_RTPRIO`) 으로 허용된 우선순위까지만.
- root 또는 `CAP_SYS_NICE`: 자유.

이 lab의 워커는 idle 상태에서 짧게 sleep하도록 처리한다. SCHED_FIFO 워커가 계속 runnable 상태로 polling하면 CFS 메인/UI 스레드를 굶길 수 있기 때문이다. 그래도 실시간 정책은 측정 도구로서는 *주의 영역* 이다. 권한을 부여한 뒤에는 워커 수를 낮게 두고 관찰한다.

### cgroup v2 cpu.weight

cgroup은 *프로세스 그룹별* 자원 격리. cgroup v2의 cpu.weight는 *그룹 단위 가중치* — 같은 cgroup 안의 모든 프로세스 합이 *다른 cgroup 대비* 어느 정도 비율로 CPU를 받는지 결정.

- 흔한 기본값은 100이지만 실제 원래 값은 읽어서 확인해야 한다.
- 200은 weight가 100인 sibling에 대해 두 배 비율이다. 모든 sibling의 값이 다르면 절대적인 “2배 CPU”를 뜻하지 않는다.
- 50으로 내리면 *절반*.

이 lab은 `SKL_CGROUP_DIR`이 `/proc/self/cgroup`에서 검출한 현재 경로와 정확히 일치할 때만 `cpu.weight`를 200으로 변경한다. 공유 session cgroup을 실수로 바꾸지 않기 위한 opt-in 계약이다. 다른 cgroup의 runnable 작업이 없으면 weight 변경만으로 처리량이 늘지 않는다.

cgroup v2 환경에서만 동작. systemd 기반 데스크톱 Linux는 거의 v2이지만 WSL2는 *v2 단독* 인지 확인이 필요할 수 있다 (`/sys/fs/cgroup/cgroup.controllers` 존재 확인).

## 2. 워커별 적용 vs 프로세스 적용의 분리

스케줄링 정책의 *적용 시점* 이 정책마다 다르다.

| 정책 | 적용 단위 | 호출 시점 |
|---|---|---|
| Default | 스레드 | 워커 시작 시 |
| Nice | 스레드 | 워커 시작 시 (`setpriority(PRIO_PROCESS, tid, ...)` 로 Linux TID에 적용) |
| Fifo | 스레드 | 워커 시작 시 (`pthread_setschedparam`) |
| Cgroup | 프로세스 | Init/Reconfigure에서 한 번 (`cpu.weight` 파일 쓰기) |

이 차이를 인터페이스로 분리한다.

<!-- snippet: src/sched/sched_policy.h -->
```cpp
// src/sched/sched_policy.h

bool ApplySchedToCurrentThread(SchedType type);
bool ApplySchedProcessSetup(SchedType type);
```

- `ApplySchedToCurrentThread`: 워커 스레드 *자기 자신* 에 적용. WorkerLoop 진입 직후 호출.
- `ApplySchedProcessSetup`: 프로세스 단위 셋업. Init / Reconfigure 에서 한 번 호출. cgroup 토글에서는 `cpu.weight` 를 쓰고, 다른 토글로 돌아오면 기존 값을 복구한다.

cgroup의 cpu.weight 쓰기는 *프로세스 한 번* 이면 충분 (그 cgroup의 모든 스레드가 그 가중치 안에서 도므로). nice / FIFO 는 *스레드별* 이라 워커마다 적용해야 한다. worker count가 1이면 별도 워커 스레드가 없으므로 nice / FIFO 토글은 측정에 직접 적용되지 않는다.

### 왜 함수 두 개로 분리하나

한 함수로 통합할 수도 있다.

```cpp
// 통합 버전
void ApplySchedAt(SchedType type, bool isProcessSetup);
```

다만 이 형태는 *호출 측이 isProcessSetup 인자를 의식해야* 한다. 함수 두 개로 나누면 *호출 위치만 보고도* 어떤 적용인지 알 수 있다.

```cpp
// Init/Reconfigure에서:
ApplySchedProcessSetup(schedType_);

// WorkerLoop 진입에서:
ApplySchedToCurrentThread(schedType_);
```

위치가 의도를 표현. 이 lab의 코드 양식과 일관.

## 3. ApplySchedToCurrentThread 구현

```cpp
// src/sched/sched_policy.cpp 핵심부

bool ApplySchedToCurrentThread(SchedType type) {
    switch (type) {
        case SchedType::Default:
            // 명시적 reset. 현재 스레드를 기본 정책으로.
            ::setpriority(PRIO_PROCESS, SklGettid(), 0);
            {
                sched_param p{};
                p.sched_priority = 0;
                ::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &p);
            }
            return true;

        case SchedType::Nice:
            return ApplyNice(10);

        case SchedType::Fifo:
            return ApplyFifo(50);

        case SchedType::Cgroup:
            // cgroup은 프로세스 단위라 워커별 추가 작업 없음.
            return true;
    }
    return true;
}
```

### ApplyNice

```cpp
bool ApplyNice(int niceValue) {
    const pid_t tid = SklGettid();
    if (::setpriority(PRIO_PROCESS, tid, niceValue) != 0) {
        std::fprintf(stderr,
            "skl: setpriority(nice=%d) failed: %s — keeping default\n",
            niceValue, std::strerror(errno));
        return false;
    }
    return true;
}
```

Linux는 nice 값을 task 단위로 들고 있으므로 `SYS_gettid` 로 현재 워커의 TID를 얻어 `setpriority(PRIO_PROCESS, tid, ...)` 에 넘긴다. POSIX 문맥의 "process priority" 와 달리 Linux/NPTL 환경에서는 스레드별 nice가 가능하다. 이 lab의 워커 스레드 단위 nice 적용에 정확히 맞는다.

### ApplyFifo

```cpp
bool ApplyFifo(int rtPriority) {
    sched_param param{};
    param.sched_priority = rtPriority;
    const int rc = ::pthread_setschedparam(::pthread_self(),
                                            SCHED_FIFO, &param);
    if (rc != 0) {
        std::fprintf(stderr,
            "skl: pthread_setschedparam(SCHED_FIFO, prio=%d) failed: %s — "
            "keeping default. Check ulimit -r and CAP_SYS_NICE.\n",
            rtPriority, std::strerror(rc));
        return false;
    }
    return true;
}
```

`pthread_setschedparam` 은 호출자의 *POSIX thread* 에 정책 적용. `SCHED_FIFO` + 우선순위 50은 *실시간 중간 정도*. 일반 사용자가 `ulimit -r 50` 이상이거나 `setcap cap_sys_nice+ep` 가 도구에 적용되어 있어야 성공한다.

실패 시 *stderr 메시지* 로 사용자에게 셋업 안내. 도구는 그대로 동작 (기본 정책 유지).

### 권한 셋업 안내

WSL2 일반 사용자에서 SCHED_FIFO를 켜려면 다음 중 하나.

```bash
# 옵션 1: ulimit 영구 변경 (/etc/security/limits.conf)
@users  -  rtprio  50

# 옵션 2: 빌드된 바이너리에 capability 부여
sudo setcap cap_sys_nice+ep ./build/system_kernel_lab

# 옵션 3: sudo로 실행 (개발용)
sudo ./build/system_kernel_lab
```

옵션 2가 가장 깔끔 — 도구 자체에만 권한이 붙고, 일반 셸에서 실행 가능.

## 4. cgroup v2 cpu.weight 쓰기

cgroup의 cpu.weight 쓰기는 *현재 프로세스의 cgroup 경로를 알아내는* 게 첫 단계다.

### 현재 cgroup 경로 검출

<!-- snippet: src/sched/sched_policy.cpp#CurrentCgroupV2Path -->
```cpp
// /proc/self/cgroup 첫 줄의 v2 cgroup 경로를 얻는다.
//   "0::/user.slice/.../skl.scope" 형태. /sys/fs/cgroup 와 합쳐 절대 경로.
//   cgroup v2가 아니면 빈 문자열.
std::string CurrentCgroupV2Path() {
    std::ifstream f("/proc/self/cgroup");
    if (!f) return {};
    std::string line;
    while (std::getline(f, line)) {
        // v2 single hierarchy 라인은 "0::..."
        if (line.size() >= 4 && line[0] == '0' && line[1] == ':'
            && line[2] == ':') {
            const std::string rel = line.substr(3);
            const std::string detected =
                (rel.empty() || rel == "/")
                ? std::string("/sys/fs/cgroup")
                : std::string("/sys/fs/cgroup") + rel;
            // 검출만으로는 쓰지 않는다. SKL_CGROUP_DIR 이 검출 경로와
            // *정확히 일치* 할 때만 승인된 것으로 본다.
            const char* approved = std::getenv("SKL_CGROUP_DIR");
            if (!approved || detected != approved) {
                std::fprintf(stderr,
                    "skl: refusing to modify the current cgroup. Set "
                    "SKL_CGROUP_DIR exactly to '%s' only for a delegated, "
                    "disposable benchmark cgroup.\n", detected.c_str());
                return {};
            }
            return detected;
        }
    }
    return {};
}
```

`/proc/self/cgroup` 가 *프로세스의 cgroup 멤버십* 을 보여 준다. v2는 *single unified hierarchy* 라 "0::" 로 시작하는 한 줄만 있다. v1 hybrid라면 여러 줄에 *각 controller 별* cgroup이 보인다.

승인 게이트가 이 단계의 핵심 결정이다. 검출된 경로는 보통 *현재 shell이나 사용자 세션이 공유하는* cgroup이라, 검출한 값을 그대로 쓰면 도구가 자기 것이 아닌 자원 제어를 말없이 바꾼다. 그래서 검출 경로를 반환하는 대신 `SKL_CGROUP_DIR` 과 대조해 일치할 때만 쓰기를 허용한다.

### cpu.weight 쓰기

<!-- snippet: src/sched/sched_policy.cpp#ApplyCgroupCpuWeight -->
```cpp
bool ApplyCgroupCpuWeight(int weight) {
    const std::string dir = CurrentCgroupV2Path();
    if (dir.empty()) {
        std::fprintf(stderr,
            "skl: cgroup v2 not detected — keeping default\n");
        return false;
    }
    CaptureOriginalCgroupWeight(dir);
    if (CgroupState().originalWeight.empty()) {
        // 원래 값을 못 읽으면 복구가 불가능하다. 쓰지 않는다.
        std::fprintf(stderr,
            "skl: reading %s/cpu.weight failed — keeping default\n",
            dir.c_str());
        return false;
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d\n", weight);
    int writeErr = 0;
    if (!WriteCgroupFile(dir, "cpu.weight", buf, &writeErr)) {
        std::fprintf(stderr,
            "skl: writing %s/cpu.weight failed: %s — keeping default\n",
            dir.c_str(), std::strerror(writeErr));
        return false;
    }
    CgroupState().modified = true;
    return true;
}
```

`WriteCgroupFile` 의 네 번째 인자는 실패 지점의 errno를 그대로 돌려받기 위한 것이다. `fopen`/`fwrite`/`fclose` 사이에서 다른 호출이 errno를 덮어쓸 수 있어 호출자가 `errno` 를 직접 읽으면 엉뚱한 메시지가 나온다.

`/sys/fs/cgroup/.../cpu.weight` 파일에 *정수 한 줄* 쓰면 된다. 일반 사용자는 자기 cgroup의 *일부 파일* 에 쓰기 권한이 있을 수도 있고 (delegation 받은 경우) 없을 수도 있다. 도구는 cgroup 토글을 켜기 전에 기존 `cpu.weight` 값을 읽어 두고, 다른 sched 토글로 돌아가면 원래 값으로 복구를 시도한다.

### cgroup delegation의 환경 의존성

systemd 환경에서 *유저 세션* 마다 cgroup이 만들어지고, 일부 파일에 쓰기 권한이 위임된다. cpu.weight는 *delegated* 되어 있으면 일반 사용자가 쓸 수 있다.

```bash
# 자기 cgroup 권한 확인
cat /proc/self/cgroup
# 예: 0::/user.slice/user-1000.slice/session-3.scope
ls -la /sys/fs/cgroup/user.slice/user-1000.slice/session-3.scope/cpu.weight
```

쓰기 권한이 없으면 `chmod` 로 줄 수 없다 (cgroup 파일은 가상 파일시스템). systemd의 cgroup delegation 설정이 필요하다. 이 lab은 *환경에 따라 동작 여부가 갈리는* 토글이라 *실패 시 안내 + 폴백* 패턴이 본질.

### systemd-run으로 깨끗한 cgroup에서 실행

깨끗한 환경에서 cgroup을 테스트하려면 다음과 같이 실행한다.

```bash
systemd-run --user --scope --slice=skl.slice ./build/system_kernel_lab
```

`--user --scope` 는 *유저 세션 안에서 새 scope* 를 만들고 도구를 그 안에서 실행한다. 도구는 *자기만의 cgroup* 을 갖게 되어 cpu.weight 쓰기에 영향이 분리된다.

## 5. 워커 풀 통합과 권한 폴백

### Init / Reconfigure에서 프로세스 셋업

```cpp
// src/workload/file_io_workload.cpp 일부

void FileIoWorkload::Init(BackendType backendType,
                          SyncType    syncType,
                          SchedType   schedType,
                          std::size_t workerCount,
                          std::size_t fileCount,
                          std::size_t fileBytes,
                          const std::filesystem::path& fixtureDir) {
    Shutdown();

    backendType_ = backendType;
    syncType_    = syncType;
    schedType_   = schedType;
    workerCount_ = (workerCount == 0) ? 1 : workerCount;
    fileBytes_   = fileBytes;
    fixtureDir_  = ...;

    GenerateFixture(fileCount, fileBytes);

    // 프로세스 단위 스케줄링 셋업 (cgroup cpu.weight).
    ApplySchedProcessSetup(schedType_);

    if (workerCount_ <= 1) {
        singleBackend_ = CreateBackend(backendType_);
    } else {
        StartWorkers();
    }
}
```

cgroup의 cpu.weight 쓰기가 워커 시작 *전에* 일어난다. 워커가 띄워지면 *이미 새 가중치가 적용된 cgroup* 안에서 동작한다.

### 워커 스레드 진입 시 적용

```cpp
void FileIoWorkload::WorkerLoop(std::size_t workerIdx) {
    auto* backend = workerBackends_[workerIdx].get();

    // 워커 스레드 자체에 정책 적용.
    ApplySchedToCurrentThread(schedType_);

    std::size_t lastSeen = 0;

    while (running_.load(std::memory_order_acquire)) {
        const std::size_t curSeq = cycleSeq_.load(std::memory_order_acquire);
        if (curSeq == lastSeen) {
            if (schedType_ == SchedType::Fifo) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            } else {
                std::this_thread::yield();
            }
            continue;
        }
        lastSeen = curSeq;
        // 작업 큐 처리...
    }
}
```

워커 스레드가 *자기 자신을 가장 잘 안다* 는 원칙. 메인이 `pthread_setschedparam` 으로 *다른 스레드* 의 정책을 바꿀 수도 있지만, *워커 자기가 자기 자신* 을 설정하는 게 코드 흐름이 명확하다.

SCHED_FIFO일 때 idle polling을 `yield` 만으로 처리하지 않는 점이 중요하다. 실시간 스레드가 계속 runnable이면 CFS 메인 스레드가 실행 기회를 못 받을 수 있다. 그래서 새 사이클을 기다리는 구간에서는 짧은 `sleep_for` 로 실제 block 상태를 만든다.

### 권한 폴백 시나리오

여러 권한 시나리오가 있다.

| 시나리오 | nice +10 | SCHED_FIFO | cgroup |
|---|---|---|---|
| 일반 사용자 (default 셋업) | OK | 실패 → 폴백 | 환경 의존 |
| `ulimit -r 50` 이상 | OK | OK | 환경 의존 |
| `setcap cap_sys_nice+ep` 부여 | OK | OK | 환경 의존 |
| sudo (root) | OK | OK | OK |

각 정책 적용에서 실패하면 *stderr에 안내 메시지* 가 한 번씩 찍힌다.

```text
skl: pthread_setschedparam(SCHED_FIFO, prio=50) failed: Operation not permitted —
    keeping default. Check ulimit -r and CAP_SYS_NICE.
```

도구는 *기본 정책으로 그대로 동작* 한다. 그래프에는 정책이 적용되지 않은 *Default* 와 같은 결과가 그려지고, HUD는 *요청한 값* 과 *실제 적용 여부* 를 두 줄로 나눠 표시한다.

### 토글 변경 시 처리

```cpp
void FileIoWorkload::Reconfigure(BackendType backendType,
                                  SyncType    syncType,
                                  SchedType   schedType,
                                  std::size_t workerCount) {
    StopWorkers();
    singleBackend_.reset();

    backendType_ = backendType;
    syncType_    = syncType;
    schedType_   = schedType;
    workerCount_ = (workerCount == 0) ? 1 : workerCount;

    ApplySchedProcessSetup(schedType_);

    if (workerCount_ <= 1) {
        singleBackend_ = CreateBackend(backendType_);
    } else {
        StartWorkers();
    }
}
```

스케줄링 토글 변경 시 워커 풀이 *재시작* 된다. 새 워커 스레드가 *새 정책* 으로 시작되는 모델. 기존 워커에 *동적으로 적용* 하지 않는 이유는 *코드 흐름의 단순성* — 워커 스레드 자체가 자기 정책 적용을 책임지니, 새 워커에 새 정책이 자연스럽다.

## 6. HUD 콤보와 사용 시나리오

<!-- snippet: src/ui/hud.cpp#Hud::Draw -->
```cpp
// src/ui/hud.cpp 일부

// sched 콤보.
const int curSchedIdx = static_cast<int>(toggles.sched);
const char* schedPrev =
    (curSchedIdx >= 0 && curSchedIdx < kSchedTypeCount)
    ? kSchedTypeLabels[curSchedIdx] : "(none)";
if (ImGui::BeginCombo("sched", schedPrev)) {
    for (int n = 0; n < kSchedTypeCount; ++n) {
        const bool selected = (curSchedIdx == n);
        if (ImGui::Selectable(kSchedTypeLabels[n], selected)) {
            toggles.sched = static_cast<SchedType>(n);
        }
        if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
}
```

backend / sync / sched / workers 네 콤보가 중앙 패널에 들어간다.

### stats 라인

```text
backend     : read
sync        : mutex
sched req   : SCHED_FIFO
sched apply : NO (see stderr / N/A)
workers     : 4
files       : 16  x  1.05 MB
cycle ms    :   5.421
throughput  :  3093.9 MB/s
total cycles: 482
total MB    : 8079.7
```

sched는 두 줄이다. `sched req` 는 *현재 토글 값*, `sched apply` 는 *그 정책이 실제로 적용됐는지* 를 보여준다. 실패 원인은 stats에 담기지 않고 *stderr 메시지* 로 안내하므로, 도구를 터미널에서 실행하면 그 메시지를 함께 볼 수 있다.

적용 여부를 별도 채널로 두는 이유는 *실패가 조용하기 때문* 이다. `schedApplied_` 는 `ApplySchedProcessSetup` 과 각 워커의 `ApplySchedToCurrentThread` 결과를 모아 두는 플래그이고, 다음 두 경우에 false가 된다.

- 권한 부족으로 `setpriority`/`pthread_setschedparam` 이 실패했을 때.
- `workers <= 1` + nice/FIFO 조합일 때. 단일 모드는 워커 스레드를 만들지 않고 GUI/main 스레드에서 직접 `RunCycle` 을 호출하는데, 이 lab은 UI 스레드의 정책을 바꾸지 않는다. 따라서 *워커 전용 정책* 은 이 조합에서 적용될 자리가 없다.

두 번째가 특히 헷갈리는 지점이다. 권한이 충분해도 `workers=1` 에서는 nice/FIFO 토글이 측정에 반영되지 않는데, `sched apply` 가 없으면 사용자는 "정책을 켰는데 그래프가 그대로"인 결과를 정책 효과 없음으로 잘못 읽는다.

### 측정 시나리오

스케줄링 효과를 측정에서 보려면 *백그라운드 부하* 가 있어야 한다.

```bash
# 별도 터미널에서 부하 생성
stress-ng --cpu $(nproc) --timeout 60s &

# 도구 실행
./build/system_kernel_lab
```

부하 상태에서 sched 토글을 default → nice +10 → SCHED_FIFO 로 바꾸면 cycle ms 변화가 보인다.

- **default + 부하**: cycle ms가 *부하 없을 때보다 큼*. CFS가 워커와 stress-ng를 *공정 분배* 하느라.
- **nice +10 + 부하**: cycle ms가 *더 큼*. 워커가 *덜 받기* 시도.
- **SCHED_FIFO + 부하**: cycle ms가 줄어들 수 있다. 워커가 stress-ng 같은 CFS 작업보다 먼저 실행된다.
- **cgroup +200 + 부하**: 다른 cgroup에서 부하가 있는 경우 효과. 같은 cgroup 부하면 영향 없음.

### 부하 없는 상태에서

부하가 없으면 sched 토글의 영향이 *측정에 거의 안 드러난다*. 워커가 *원하는 만큼 CPU를 받기* 때문. nice + 10이라도 비교 대상 (다른 nice 0 스레드) 가 없으면 *덜 받지 않는다*.

이게 *옳은 결과* 다. 스케줄링 정책의 가치는 *경합 환경* 에서 드러난다. 도구가 *부하가 없을 때 sched 토글이 효과 없음* 을 보여주는 것 자체가 측정 정보다.

## 7. 빌드와 측정 결과

```bash
cmake --build build -j

# 일반 사용자: SCHED_FIFO 안 됨, nice/cgroup만 동작 시도
./build/system_kernel_lab

# capability 부여하면 SCHED_FIFO 가능
sudo setcap cap_sys_nice+ep ./build/system_kernel_lab
./build/system_kernel_lab
```

### 측정 시점에서 보이는 경향

부하 없이 16 파일 × 1MB 워크로드 + 4 worker 시나리오에서:

- **default**: baseline.
- **nice +10**: 차이 거의 없음. 비교 대상 부재.
- **SCHED_FIFO** (권한 OK): 차이 거의 없음. 우선순위는 높지만 다른 작업이 없어 의미 없음. 권한 부족 시 stderr 메시지 + default와 같은 결과.
- **cgroup x2** (delegation OK): 차이 거의 없음. 다른 cgroup이 안 도는 환경.

부하를 별도 터미널에서 가하면 (stress-ng) 차이가 *측정에 들어온다*.

### 검증 체크리스트

- SCHED_FIFO 선택 시 stderr에 *Operation not permitted* → 권한 부족. `setcap` 또는 sudo로 실행.
- cgroup 선택 시 stderr에 *cgroup v2 not detected* → v1 또는 hybrid cgroup. systemd 기반 최신 Linux는 보통 v2.
- cgroup 선택 시 stderr에 *writing ... failed: Permission denied* → cgroup delegation 부재. systemd-run --user --scope 로 새 cgroup 안에서 실행.
- 부하 없이 토글 변경에 *효과 없음* → 정상. stress-ng 등으로 부하 추가.
- SCHED_FIFO 선택 시 *시스템 전체가 멈춰 보임* → 실시간 워커가 너무 많이 CPU 점유. 워커 수를 줄이거나, 권한을 제거하고 default 정책으로 되돌린다.

### 측정의 정직성

이 단계의 결과를 *어떤 정책이 절대 빠르다* 로 일반화하지 않는다.

- **부하 없는 환경에서는 모든 정책이 같음** — 정상. 정책의 의미가 *경합 시* 에 있어서.
- **SCHED_FIFO가 항상 빠르다** — 거짓. 다른 작업 없는 환경에서는 default와 같다. *우선순위는 비교 대상이 있을 때만 의미*.
- **cgroup이 자주 효과 없음** — 단일 cgroup 환경이거나 권한 부족. 측정 *환경 셋업이 결과의 절반*.

도구의 정직성은 *환경 의존 결과를 환경 의존이라고 분명히 말하는* 데 있다. sched 토글이 *효과 없는 시나리오* 도 측정 정보다.

## 8. 검증 — 권한이 없을 때의 동작이 계약이다

스케줄링 정책의 *성공* 경로는 자동으로 확인할 수 없다. `SCHED_FIFO` 는 `CAP_SYS_NICE` 가 있어야 붙고, cgroup 쓰기는 위임된 cgroup이 있어야 한다. 두 조건 다 검사 프로세스가 스스로 만들 수 없고, 만들려 들면 실제 시스템을 건드리게 된다.

그런데 이 모듈이 실제로 약속한 것은 성공이 아니다. *권한이 없으면 `false` 를 돌려주고, 스레드는 기본 정책으로 계속 돈다* 가 약속이다. 도구가 죽거나 조용히 성공한 척하면 측정 메타데이터가 거짓이 된다. 그쪽은 seam 주입으로 결정적으로 밟을 수 있다.

<!-- snippet: tests/system_fault_tests.cpp#TestSchedThreadPolicyFallback -->
```cpp
// tests/system_fault_tests.cpp

void TestSchedThreadPolicyFallback() {
    // 이 스레드의 nice 를 바꾸므로 메인 스레드가 아니라 전용 스레드에서.
    std::thread worker([] {
        CHECK(skl::ApplySchedToCurrentThread(skl::SchedType::Default));

        {
            ScopedFault fault("setpriority:0:EACCES");
            CHECK(!skl::ApplySchedToCurrentThread(skl::SchedType::Nice));
            CHECK(skl::FaultInjectionCount("setpriority") == 1);
        }
        // 무주입: nice 를 *올리는* 것은 권한이 필요 없다 → 성공 경로 대조군.
        CHECK(skl::ApplySchedToCurrentThread(skl::SchedType::Nice));

        {
            ScopedFault fault("pthread_setschedparam:0:EPERM");
            CHECK(!skl::ApplySchedToCurrentThread(skl::SchedType::Fifo));
            CHECK(skl::FaultInjectionCount("pthread_setschedparam") == 1);
        }
        // ...
        // 실패했든 성공했든 스레드는 살아 있고 기본으로 돌아갈 수 있어야 한다.
        CHECK(skl::ApplySchedToCurrentThread(skl::SchedType::Default));
    });
    worker.join();
}
```

`무주입: nice 를 올리는 것은 권한이 필요 없다` 줄이 대조군이다. 실패만 확인하면 *항상 `false` 를 돌려주는* 구현도 통과한다. `nice` 를 올리는 쪽은 일반 사용자도 되므로, 같은 함수가 조건에 따라 두 답을 낸다는 것을 여기서 보인다.

cgroup 쪽은 검사가 실제 cgroup을 바꿀 위험이 있어 순서를 뒤집는다. 먼저 승인 환경변수 없이 부르면 거부하는지, 승인 경로가 현재 cgroup과 다르면 거부하는지를 확인하고, `cpu.weight` 에 쓰기 권한이 *없음을 확인한 뒤에만* 읽기·쓰기 실패 경로로 들어간다. 쓰기가 가능한 환경에서는 그 검사를 건너뛴다고 stdout에 남긴다 — 조용히 건너뛰면 "검사했다"가 거짓이 된다.

```bash
cmake --build build -j
ctest --test-dir build -R skl_step06 --output-on-failure
```

```text
    Start 21: skl_step06_fault
1/1 Test #21: skl_step06_fault .................   Passed    0.00 sec

100% tests passed, 0 tests failed out of 1
```

```bash
./build/system_fault_tests --step=06
```

```text
[ run  ] step06  sched thread policy fallback
skl: setpriority(nice=10) failed: Permission denied — keeping default
skl: pthread_setschedparam(SCHED_FIFO, prio=50) failed: Operation not permitted — keeping default. Check ulimit -r and CAP_SYS_NICE.
skl: pthread_setschedparam(SCHED_FIFO, prio=50) failed: Operation not permitted — keeping default. Check ulimit -r and CAP_SYS_NICE.
  SCHED_FIFO applied without injection: no (expected without CAP_SYS_NICE)
[  ok  ] step06  sched thread policy fallback
[ run  ] step06  sched cgroup fallback
skl: refusing to modify the current cgroup. Set SKL_CGROUP_DIR exactly to '/sys/fs/cgroup/init.scope' only for a delegated, disposable benchmark cgroup.
skl: cgroup v2 not detected — keeping default
skl: refusing to modify the current cgroup. Set SKL_CGROUP_DIR exactly to '/sys/fs/cgroup/init.scope' only for a delegated, disposable benchmark cgroup.
skl: cgroup v2 not detected — keeping default
skl: reading /sys/fs/cgroup/init.scope/cpu.weight failed — keeping default
skl: reading /sys/fs/cgroup/init.scope/cpu.weight failed — keeping default
  cgroup write seam reached: no (cpu.weight unreadable here)
[  ok  ] step06  sched cgroup fallback
system_fault_tests --step=6: 2/17 check(s) run, 0 failed
```

stderr에 실패 안내가 잔뜩 나오는데도 케이스가 통과한다는 것이 이 절의 요점이다. 안내는 *기대된 출력* 이고, 그 뒤에도 스레드가 살아 있고 기본 정책으로 돌아갈 수 있다는 것이 검사 대상이다. `SCHED_FIFO applied without injection: no` 와 `cgroup write seam reached: no` 두 줄은 이 머신의 권한 상태를 그대로 적은 것 — 권한이 있는 머신에서는 `yes` 로 바뀌고 그때는 쓰기 seam까지 검사된다.

---

## 결론

이 단계의 결과물은 워커 스레드의 스케줄링 정책을 4종(default / nice +10 / SCHED_FIFO / cgroup x2) 으로 토글할 수 있는 도구다. 약 200줄의 새 코드(sched policy 모듈 + 워크로드 통합 + Toggles 확장 + HUD 콤보)가 워커 풀 구조 위에 얹혔다.

세 가지 결정이 이 단계의 토대를 만들었다.

1. **워커별 적용 vs 프로세스 적용 분리** — `ApplySchedToCurrentThread` 와 `ApplySchedProcessSetup` 두 진입점. 정책 단위에 맞춰 호출 위치가 갈린다.
2. **best-effort 폴백** — 권한 부족 시 stderr 안내 + 기본 정책으로 도구가 그대로 동작. 환경 차이로 도구가 죽는 사고를 막는다.
3. **부하 없으면 효과 없음** 을 측정 정보로 — sched 토글의 의미는 *경합 시* 에만. 단일 작업 환경에서 효과 없음이 *옳은 결과*.

여기까지의 코드는 *워커가 스레드* 라는 가정 위에 있다. 한 워커당 하나의 OS 스레드, 한 작업이 끝나야 다음 작업으로. 워커를 *코루틴* 으로 바꾸면 *컨텍스트 스위치 비용* 이 측정 변수가 된다. 스레드는 커널 스케줄러를 거치고, 코루틴은 유저 공간에서 전환된다.

## 더 읽을거리

- [Linux man page: sched(7)](https://man7.org/linux/man-pages/man7/sched.7.html) — 정책 전체 개관
- [Linux man page: setpriority(2)](https://man7.org/linux/man-pages/man2/setpriority.2.html) — nice 시스템 콜
- [Linux man page: pthread_setschedparam(3)](https://man7.org/linux/man-pages/man3/pthread_setschedparam.3.html)
- [Linux man page: cgroups(7)](https://man7.org/linux/man-pages/man7/cgroups.7.html) — v1/v2 차이
- [cgroup v2 인터페이스 문서](https://www.kernel.org/doc/Documentation/admin-guide/cgroup-v2.rst)
- Brendan Gregg, *Systems Performance*, Ch. 6 "CPUs" — 스케줄러 분석 도구 (`/proc/sched_debug` 등)
- Robert Love, *Linux Kernel Development*, Ch. 4 "Process Scheduling" — CFS와 실시간 정책 내부
- [systemd cgroup delegation](https://systemd.io/CGROUP_DELEGATION/) — 사용자 cgroup 권한 위임
