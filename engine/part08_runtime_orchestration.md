## 들어가며

게임 엔진은 렌더러, 물리, 애니메이션, 오디오, 입력, 네트워크, 리소스 로더를 한 프로세스에 모아 둔 이름이 아니다. 중요한 것은 이들이 *언제 실행되고, 누가 상태를 쓰며, 어느 시점의 결과를 서로에게 보여 주는가* 다. 개별 시스템이 정확해도 프레임 경계가 불명확하면 입력은 한 틱 늦고, 물리는 렌더 중간 상태를 읽으며, 오디오는 파괴된 객체를 참조하고, 워커는 다음 프레임 데이터를 너무 일찍 덮어쓴다.

런타임 통합의 핵심은 호출 목록이 아니라 시간과 소유권의 계약이다.

1. 하나의 상태에는 한 단계에서 한 명의 논리적 writer만 둔다.
2. 단계 사이 데이터는 명시적인 command, event, snapshot으로 전달한다.
3. 시뮬레이션 시간과 표시 시간을 구분한다.
4. 파괴와 전환은 참조가 남지 않는 commit 지점에서 수행한다.
5. 병렬 작업은 의존성 그래프로 표현하고 프레임 전체를 막는 barrier를 최소화한다.

이 글은 게임 루프를 상태 기계와 작업 그래프로 모델링하고, 고정 틱·보간·수명·스냅샷·스레드 도메인·오류 복구·계측을 하나의 런타임 계약으로 묶는다.

---

## 목차

1. [런타임은 상태 기계다](#1-런타임은-상태-기계다)
2. [시간 도메인과 고정 틱](#2-시간-도메인과-고정-틱)
3. [프레임 단계와 의존성](#3-프레임-단계와-의존성)
4. [상태 소유권과 지연 파괴](#4-상태-소유권과-지연-파괴)
5. [작업 그래프와 스레드 도메인](#5-작업-그래프와-스레드-도메인)
6. [스냅샷과 메시지 경계](#6-스냅샷과-메시지-경계)
7. [초기화·종료·전환의 트랜잭션](#7-초기화종료전환의-트랜잭션)
8. [결정론적 기록과 검증](#8-결정론적-기록과-검증)
9. [프레임 예산과 계측](#9-프레임-예산과-계측)
10. [통합 설계 예시](#10-통합-설계-예시)

---

## 1. 런타임은 상태 기계다

게임 실행 상태를 단순한 `while (running)`으로만 보면 로딩, 일시정지, 월드 교체, 장치 손실, 종료의 중간 상태가 숨는다. 최소한 다음 상태는 구분해야 한다.

| 상태 | 허용되는 작업 | 금지되는 작업 |
|---|---|---|
| Boot | 설정·플랫폼·로그 초기화 | 게임 월드 update |
| Loading | 비동기 읽기·역직렬화·GPU 업로드 예약 | 활성 월드 규칙 실행 |
| Running | 입력·고정 틱·렌더·오디오 | 파괴된 월드 접근 |
| Paused | UI·플랫폼 이벤트·선택적 렌더 | 게임 시간 진행 |
| Transition | 새 월드 준비·이전 월드 drain | 두 월드의 권위 상태 동시 쓰기 |
| Recovering | 장치·세션 재생성 | 정상 프레임으로 결과 발표 |
| ShuttingDown | 작업 취소·flush·역순 종료 | 새 비동기 요청 |

상태 전이는 요청과 commit을 분리한다. 예를 들어 메뉴에서 월드 교체를 요청한 순간 현재 월드를 즉시 파괴하지 않는다. 요청은 프레임 명령 큐에 들어가고, 현재 단계의 모든 reader가 끝난 commit 지점에서만 활성 월드 포인터를 교체한다.

```cpp
enum class RuntimeState {
    Boot,
    Loading,
    Running,
    Paused,
    Transition,
    Recovering,
    ShuttingDown,
};

struct TransitionRequest {
    WorldId destination;
    TransitionReason reason;
};
```

상태 기계가 없으면 `isLoading`, `isPaused`, `deviceLost`, `quitRequested` 같은 boolean 조합이 암묵적인 상태를 만든다. boolean이 네 개면 이론상 16개 조합이 생기고, 그중 대부분은 의미가 없거나 위험하다. 명시적 상태와 허용 전이를 사용하면 불가능한 조합을 표현하지 않을 수 있다.

### 전이 불변식

- `Running -> Transition`은 한 프레임에 한 번만 commit한다.
- 새 월드는 `Ready`가 되기 전 활성 포인터에 게시되지 않는다.
- 종료 요청 이후 새 작업을 제출하지 않는다.
- `Recovering` 중 만든 렌더 결과는 정상 프레임 통계에 섞지 않는다.
- 전이 실패 시 이전 활성 월드는 계속 유효하거나 안전한 오류 월드로 이동한다.

## 2. 시간 도메인과 고정 틱

게임에는 하나의 시간이 아니라 여러 시간 도메인이 있다.

| 시간 | 의미 | 대표 사용자 |
|---|---|---|
| monotonic real time | 역행하지 않는 실제 경과 | 프레임 계측, timeout |
| simulation time | 고정 간격으로 진행하는 권위 시간 | 물리, 게임 규칙 |
| presentation time | 보간된 표시 시각 | 렌더, 카메라 |
| audio device time | 샘플 클록 | 믹서, 음악 동기 |
| network remote time | 원격 tick·timestamp | 보간, 동기화 |
| UI time | 일시정지와 독립할 수 있는 시간 | 메뉴 애니메이션 |

벽시계는 사용자가 시간을 바꾸거나 NTP 보정이 일어나 역행할 수 있으므로 프레임 delta에 쓰지 않는다. monotonic clock에서 측정한 실제 delta를 누산하고, 시뮬레이션은 고정 간격 $h$만큼 진행한다.

$$
A_{n+1}=\min(A_n+\Delta t_{\text{real}}, A_{\max})
$$

$$
\text{while } A \ge h:
\quad S_{k+1}=F(S_k, I_k),\quad A\leftarrow A-h
$$

$A_{\max}$는 긴 정지 뒤 수백 틱을 한 프레임에 몰아서 실행하는 spiral of death를 막는다. 이 clamp는 시간을 보존하지 않는다. 따라서 `realDelta`가 clamp에서 잘린 시간(`droppedRealClamp`)과 catch-up 상한에 걸린 잔여 시간(`droppedCatchUpTime`)을 구분해 계측하고, 프레임 계측에는 clamp하지 않은 monotonic delta를 따로 보존한다.

```cpp
constexpr double kTick = 1.0 / 60.0;
constexpr double kMaxFrameDelta = 0.25;
constexpr int kMaxCatchUpSteps = 4;

const double clampedDelta = std::min(realDelta, kMaxFrameDelta);
droppedRealClamp += realDelta - clampedDelta;   // 첫 clamp에서 버린 시간도 기록
accumulator += clampedDelta;
int steps = 0;
while (accumulator >= kTick && steps < kMaxCatchUpSteps) {
    FixedUpdate(kTick);
    accumulator -= kTick;
    ++steps;
}
if (accumulator >= kTick) {
    droppedCatchUpTime += accumulator - std::fmod(accumulator, kTick);
    accumulator = std::fmod(accumulator, kTick);
}
const double alpha = accumulator / kTick;
```

렌더는 마지막 두 권위 상태 $S_{k-1}$과 $S_k$ 사이를 $\alpha$로 보간한다.

$$
S_{\text{render}}=(1-\alpha)S_{k-1}+\alpha S_k
$$

이 보간은 현재 real time까지 예측한 위치가 아니라 $(k-1+\alpha)h$ 시각의 표시 상태다. 따라서 $\text{presentationTime}=\text{simulationTime}-h+\text{accumulator}$이며, 이 1틱 지연은 입력 지연 분석과 snapshot age 계측에 포함한다. 렌더만 local prediction을 쓰는 경우에는 권위 보간과 별도로 표시한다.

회전은 정규화된 quaternion 보간을 사용하고, 순간 이동·리스폰·월드 원점 이동에는 보간을 끊는 discontinuity flag가 필요하다. 그렇지 않으면 존재하지 않은 중간 경로가 화면에 나타난다.

입력은 프레임마다 수집하되 시뮬레이션 tick에 소비되는 command로 양자화한다. “키가 눌려 있는가”와 “이번 tick에 눌렸는가”를 분리하지 않으면 한 렌더 프레임에 여러 고정 틱이 실행될 때 edge 입력이 중복 소비된다.

## 3. 프레임 단계와 의존성

프레임 단계는 관습적 함수 순서가 아니라 데이터 의존성이다. 한 가지 가능한 공통 경로는 다음과 같다.

```text
Platform events
  -> Raw input snapshot
  -> Network receive / command decode
  -> Fixed simulation ticks
       -> gameplay commands
       -> AI decisions
       -> animation intent
       -> physics integrate/solve
       -> post-physics events
  -> Transform finalization
  -> Presentation snapshot extraction
  -> Render graph build/submit
  -> Audio command publish
  -> Deferred destruction
  -> Telemetry commit
```

이 순서가 유일한 정답은 아니지만, 선택한 순서의 의미는 고정해야 한다.

### 물리 전과 후

게임플레이가 힘과 목표 속도를 쓰고 물리가 위치를 결정한다면, 최종 transform은 물리 이후에만 읽어야 한다. 애니메이션 root motion이 물리에 들어간다면 root delta를 먼저 계산하고 character controller가 소비한 뒤 최종 pose를 평가한다.

### 이벤트 발생과 소비

충돌 이벤트를 solver 중간에 즉시 게임플레이로 보내면 callback이 물리 배열을 수정해 iterator와 island 불변식을 깨뜨릴 수 있다. solver는 event record만 만들고, 물리 단계가 끝난 뒤 읽기 전용 이벤트 배열을 게시한다.

```cpp
struct ContactEvent {
    EntityHandle a;
    EntityHandle b;
    ContactKind kind;
    Vec3 point;
    Vec3 normal;
};
```

### 렌더 추출

렌더 스레드가 live ECS를 임의 시점에 읽게 두면 시뮬레이션 writer와 경쟁한다. 시뮬레이션 단계 끝에서 필요한 데이터만 render snapshot으로 추출한다. 추출 비용은 들지만 렌더러는 일관된 한 시점의 상태를 받는다.

### 부분 순서

모든 시스템을 직렬화할 필요는 없다. 다음처럼 독립적인 작업은 같은 단계 안에서 병렬로 실행할 수 있다.

```text
AI perception ----> AI decision ----\
Animation sample --------------------+--> transform finalize
Independent particles --------------/
Physics broad phase -> narrow phase -> solve
```

핵심은 “update 순서 목록”을 “읽는 데이터, 쓰는 데이터, 게시 지점”으로 바꾸는 것이다.

## 4. 상태 소유권과 지연 파괴

raw pointer는 객체가 이동·파괴될 수 있는 런타임 경계에 적합하지 않다. 안정적 handle은 slot index와 generation을 함께 저장한다.

```cpp
struct EntityHandle {
    uint32_t index;
    uint32_t generation;
};

bool IsAlive(EntityHandle handle) const {
    return handle.index < slots.size() &&
           slots[handle.index].generation == handle.generation &&
           slots[handle.index].denseIndex != kInvalid;
}
```

slot이 재사용될 때 generation을 증가시키면 오래된 handle이 같은 index의 새 객체를 가리키는 ABA 형태의 오류를 막는다. generation overflow는 이론적으로 가능하므로 비트 수와 재사용 빈도를 정량화해야 한다. 32비트 generation에서 같은 slot을 초당 천 번 재사용하면 wrap까지 약 49.7일이다.

$$
\frac{2^{32}}{1000\ \text{reuse/s}}
\approx 4.29\times10^6\text{s}\approx49.7\text{days}
$$

따라서 극단적으로 빠른 재사용이 가능한 풀에서는 64비트 generation, quarantine, epoch를 고려한다. “32비트면 영원하다”는 가정은 안전하지 않다.

파괴 요청도 즉시 메모리 회수와 같지 않다.

1. 객체를 `PendingDestroy`로 표시한다.
2. 새 query와 command 대상에서 제외한다.
3. 현재 단계가 가진 snapshot과 작업을 완료한다.
4. event와 render/audio command가 안정적 ID만 갖는지 확인한다.
5. commit 지점에서 dense storage에서 제거하고 generation을 증가시킨다.

이 방식을 쓰면 충돌 callback 안에서 엔티티를 파괴해도 현재 solver 배열은 변하지 않는다.

수명은 객체 단위만이 아니다. 월드, scene, subsystem에도 부모-자식 관계가 있다. 부모가 자식을 소유하고 종료는 생성의 역순으로 수행한다.

```text
Platform
  -> Job system
  -> File system
  -> Renderer / Audio
  -> Resource registry
  -> World
  -> Gameplay systems
```

실제 의존성이 이 순서와 다르면 위상 정렬 가능한 dependency graph로 표현해야 한다. 전역 singleton의 정적 파괴 순서에 맡기면 다른 translation unit의 종료 순서를 보장할 수 없다.

## 5. 작업 그래프와 스레드 도메인

작업 시스템의 목적은 모든 함수를 워커로 보내는 것이 아니다. 큰 작업을 의존성이 있는 작은 단위로 나누되 scheduling overhead, cache locality, 동기화 비용보다 충분히 큰 grain을 유지하는 것이다.

작업 $i$의 실행 비용을 $C_i$, 제출·큐·wake 비용을 $S$라 할 때 $C_i \ll S$인 작업을 무수히 만들면 병렬화가 느려진다. 실무에서는 마이크로초 단위의 작은 루프를 묶고, profiler에서 ready queue 대기와 worker utilization을 함께 본다.

### 스레드 도메인

| 도메인 | 주 책임 | 금지 사항 |
|---|---|---|
| platform/main | 창, OS 이벤트, 수명 commit | 긴 파일 I/O |
| simulation | 권위 월드 상태 | 렌더 API 호출 |
| render | command recording, GPU submit | live gameplay 수정 |
| audio callback | 고정 시간 mix | lock, allocation, blocking I/O |
| I/O | 파일 읽기, 압축 해제 준비 | GPU 객체 직접 게시 |
| worker | 순수 계산 작업 | 암묵적 thread-local 전역 의존 |

오디오 callback은 deadline을 놓치면 즉시 crackle로 드러난다. 일반 mutex에서 priority inversion이 일어날 수 있고, page fault나 allocator lock도 위험하다. 게임 스레드는 lock-free 또는 bounded queue에 작은 command를 게시하고 callback은 이미 준비된 데이터만 소비해야 한다.

### 의존성 카운터

```cpp
struct JobNode {
    JobFunction run;
    std::atomic<uint32_t> unfinishedDependencies;
    SmallVector<JobId, 4> dependents;
};
```

작업 완료 시 dependent의 카운터를 감소시키고 0이 된 작업만 ready queue에 넣는다. 이 모델은 전역 barrier보다 필요한 간선만 기다린다.

### 프레임을 넘는 작업

에셋 I/O나 shader compile은 여러 프레임에 걸친다. 프레임 arena의 주소, live ECS pointer, stack reference를 캡처하면 완료 시점에 수명이 끝났을 수 있다. 장기 작업은 안정적 ID와 자체 소유 입력을 가져야 하며 결과 게시 전에 generation과 요청 token을 다시 검증한다.

```cpp
struct LoadTicket {
    AssetId asset;
    uint32_t generation;
    CancellationToken cancellation;
};
```

취소는 작업을 반드시 즉시 멈춘다는 뜻이 아니다. 결과를 게시하지 않도록 만드는 논리적 취소와 I/O 자체의 취소 가능성을 구분한다.

## 6. 스냅샷과 메시지 경계

스레드 사이에서 가장 단순한 계약은 immutable snapshot이다. writer가 back buffer를 완성한 뒤 원자적으로 index를 교체하고 reader는 한 프레임 동안 같은 buffer만 읽는다.

```cpp
template <class T>
class DoubleSnapshot {
public:
    T& BeginWrite() { return buffers_[1u - published_.load(std::memory_order_relaxed)]; }

    void Publish() {
        const unsigned next = 1u - published_.load(std::memory_order_relaxed);
        published_.store(next, std::memory_order_release);
    }

    const T& Read() const {
        return buffers_[published_.load(std::memory_order_acquire)];
    }

private:
    T buffers_[2];
    std::atomic<unsigned> published_{0};
};
```

이 예시는 reader가 이전 buffer를 다 읽기 전에 writer가 다시 덮어쓰지 않는다는 추가 보장이 필요하다. release/acquire는 가시성만 보장할 뿐 재사용 허가가 아니므로, reader completion/fence/lease로 반환된 슬롯만 `BeginWrite`가 가져갈 수 있도록 API로 표현한다. render가 simulation보다 두 프레임 늦을 수 있다면 triple buffer, frame fence, reference-counted snapshot 중 하나를 사용하되, reader 지연에 시간 상한을 두고 필요한 버퍼 수와 backpressure를 함께 정한다.

### Command와 event

- **Command**: 아직 실행되지 않은 의도. writer가 검증하고 상태를 바꾼다.
- **Event**: 이미 일어난 사실. 소비자가 원인을 되돌릴 수 없다.
- **Snapshot**: 특정 시점 상태의 읽기 전용 표현.

`DamageCommand(target, amount)`와 `EntityDiedEvent(entity)`를 구분하지 않으면 여러 소비자가 같은 상태를 중복 변경할 수 있다.

메시지에는 raw pointer 대신 안정적 ID를 넣는다. 지연 전달 시 target이 사라질 수 있으므로 소비 시 유효성을 다시 검사한다. 유효하지 않은 command를 drop할지 오류로 볼지는 command 의미에 따라 정한다.

### 큐의 용량

bounded queue가 가득 찼을 때 정책은 데이터 종류에 따라 다르다.

| 데이터 | overflow 정책 예 |
|---|---|
| 입력 command | drop 금지, backpressure 또는 세션 오류 |
| 반복 footstep audio | 낮은 우선순위 drop 가능 |
| transform snapshot | 오래된 중간 값 교체 가능 |
| 경제 transaction | drop 금지, 영속 큐 필요 |
| debug telemetry | 표본화·drop 가능 |

“lock-free queue를 썼다”보다 overflow 의미를 정한 것이 더 중요하다.

## 7. 초기화·종료·전환의 트랜잭션

하위 시스템 초기화는 중간 실패를 전제로 해야 한다. renderer가 성공하고 audio가 실패했을 때 이미 만든 창과 GPU 객체를 누가 정리하는지 명확해야 한다.

```cpp
bool Runtime::Initialize() {
    if (!platform.Initialize()) return false;
    if (!jobs.Initialize()) return false;
    if (!files.Initialize()) return false;
    if (!renderer.Initialize()) return false;
    if (!audio.Initialize()) return false;
    initialized_ = true;
    return true;
}
```

이 코드만으로는 실패한 지점 이전의 자원을 되돌리지 않는다. RAII를 사용하거나 성공한 단계 stack을 기록해 역순 rollback을 수행한다.

```cpp
ScopeRollback rollback;   // 가드 자체는 할당하지 않는다
if (!platform.Initialize()) return false;   // 실패한 단계는 자기 부분 자원을 스스로 정리한다
rollback.Push([&] { platform.Shutdown(); });
if (!jobs.Initialize()) return false;
rollback.Push([&] { jobs.Shutdown(); });
// ...
rollback.Commit();   // 모든 단계가 성공한 뒤에만 commit
```

각 단계의 `bool` 반환을 확인한 뒤 성공한 단계만 rollback에 등록하고, 실패하면 즉시 탈출해 `Commit`하지 않는다. 선행 성공 단계의 역순 해제는 rollback이 맡고, 부분 초기화에 실패한 컴포넌트 자신의 자원은 그 컴포넌트가 정리한다. `Shutdown`은 `noexcept`이며 callback/job을 drain한 뒤 호출한다.

종료 순서도 역순만 외우면 충분하지 않다.

1. 새 요청을 차단한다.
2. 외부 callback과 네트워크 입력을 끊는다.
3. 장기 작업을 취소하고 완료를 join한다.
4. render·audio가 참조 중인 snapshot을 drain한다.
5. 월드와 리소스 handle을 해제한다.
6. GPU·audio device 객체를 해제한다.
7. job system과 platform을 종료한다.

device lost 복구는 전역 재시작의 축소판이다. CPU 권위 상태와 복구 가능한 리소스 description을 남기고 device 객체만 재생성해야 한다. GPU handle 자체를 gameplay에 노출하면 이 경계가 무너진다.

월드 전환 역시 준비와 게시를 분리한다.

```text
Request -> Load candidate -> Validate -> Quiesce old world
        -> Atomic publish -> Drain old snapshots -> Destroy old world
```

검증 실패 전에 이전 월드를 파괴하지 않으면 전환 실패를 안전하게 처리할 수 있다.

## 8. 결정론적 기록과 검증

런타임 통합 버그는 단위 함수보다 단계 사이에서 발생한다. 재현을 위해 권위 시뮬레이션에 들어간 입력을 tick 번호와 함께 기록한다.

```cpp
struct TickRecord {
    uint64_t tick;
    uint64_t randomSeed;
    std::vector<PlayerCommand> commands;
    uint64_t stateHash;
};
```

재생에서는 real clock, OS 입력, 네트워크 도착 시각을 직접 읽지 않고 record를 공급한다. 렌더·오디오 같은 비권위 출력은 hash에서 제외하되, 이들이 시뮬레이션을 수정하지 않는지 별도로 검사한다.

입력과 seed만으로는 비동기 활성화 순서가 고정되지 않는다. asset/cell 활성화가 gameplay 결과에 영향을 주면 권위적 외부 완료를 tick event로 기록하거나 활성화 시점을 결정론적으로 고정한다. 병렬 RNG는 stream ownership을 정해 호출 순서에 의존하지 않게 하고, canonical state hash가 어떤 상태를 포함하는지 범위를 선언한다.

### 검증할 불변식

- 같은 초기 snapshot과 command log는 같은 권위 state hash를 만든다.
- `FixedUpdate` 외부에서 권위 component가 바뀌지 않는다.
- 파괴된 handle은 어느 queue에서도 성공적으로 resolve되지 않는다.
- render snapshot의 tick은 프레임 도중 바뀌지 않는다.
- pause 중 simulation tick은 증가하지 않지만 platform event는 처리된다.
- catch-up 상한을 넘겨도 한 프레임의 작업량이 제한된다.

### 스케줄 무작위화

경쟁 조건은 항상 같은 worker 순서에서 숨을 수 있다. 테스트 빌드에서는 ready job 순서를 seed 기반으로 섞고, 작은 yield와 지연을 주입해 더 많은 interleaving을 만든다. 실패 시 seed와 job trace를 저장한다.

### 실패 주입

- 특정 초기화 단계 실패
- I/O 완료가 월드 전환 뒤 도착
- render가 두 프레임 지연
- queue 용량 초과
- device lost
- 종료 중 callback 도착

성공 경로만 테스트하면 수명 계약을 검증하지 못한다.

## 9. 프레임 예산과 계측

60Hz 프레임의 16.67ms를 모든 시스템에 고정 비율로 나누는 것만으로는 부족하다. CPU와 GPU는 겹쳐 실행되고, render thread가 이전 프레임을 제출하며, audio는 별도 deadline을 가진다.

기록해야 할 기본 채널은 다음과 같다.

| 채널 | 의미 |
|---|---|
| wall frame ms | 사용자에게 보이는 프레임 간격 |
| simulation steps/frame | catch-up과 부하 |
| dropped simulation time | 고정 틱 보존 실패 |
| main/render/worker busy ms | 스레드별 사용 시간 |
| ready queue wait | 병렬성 부족 또는 worker 부족 |
| job critical path | 프레임 완료를 실제로 막은 경로 |
| snapshot age | 표시 상태가 권위 상태보다 늦은 정도 |
| deferred destroy count | 수명 commit backlog |
| queue occupancy/drop | 메시지 경계 압력 |
| GPU frame latency | CPU-GPU 파이프라인 깊이 |

총 작업량 $W$, 무한 worker에서도 줄일 수 없는 critical path $L$, worker 수 $P$라 하면 이상적인 하한은 다음과 같다.

$$
T_P \ge \max\left(\frac{W}{P}, L\right)
$$

worker를 늘려도 $L$이 길면 프레임은 줄지 않는다. 평균 utilization만 보고 작업 시스템이 효율적이라고 판단하면 안 되는 이유다.

trace event에는 `frame`, `tick`, `job`, `entity/world generation`을 넣어 서로 다른 시간축을 연결한다. 로그 문자열만으로는 프레임 단계 간 인과를 복원하기 어렵다.

## 10. 통합 설계 예시

다음은 계약을 드러내기 위한 축약된 런타임 구조다.

```cpp
void Runtime::RunFrame() {
    const double now = clock.NowMonotonic();
    const double realDelta = ClampDelta(now - lastFrameTime_);
    lastFrameTime_ = now;

    platform.PumpEvents();
    RawInput raw = input.CaptureSnapshot();
    network.DrainIncoming(networkCommands_);
    commandQueue_.Enqueue(input.Quantize(raw, simulationTick_));   // held 상태 + 이번 tick edge
    commandQueue_.Enqueue(networkCommands_);                        // 미래 tick 명령은 보존

    fixedAccumulator_ += realDelta;
    int steps = 0;
    while (fixedAccumulator_ >= fixedStep_ && steps < maxCatchUpSteps_) {
        TickContext tick{
            .index = simulationTick_,
            .dt = fixedStep_,
            .commands = commandQueue_.Take(simulationTick_),   // tick당 1회, 단조 증가
        };
        world.BeginTick(tick);
        jobs.Run(simulationGraph);
        world.CommitTick();

        ++simulationTick_;
        fixedAccumulator_ -= fixedStep_;
        ++steps;
    }

    if (fixedAccumulator_ >= fixedStep_) {   // catch-up 상한에 걸린 잔여 시간은 버린다
        droppedSimulationTime_ += fixedAccumulator_ - std::fmod(fixedAccumulator_, fixedStep_);
        fixedAccumulator_ = std::fmod(fixedAccumulator_, fixedStep_);
    }

    const float alpha = (float)(fixedAccumulator_ / fixedStep_);
    RenderSnapshot& back = renderSnapshots.BeginWrite();
    world.ExtractPresentation(back, alpha);
    renderSnapshots.Publish();

    renderer.Submit(renderSnapshots.Read());
    audio.Publish(world.ConsumeAudioCommands());
    world.CommitDeferredDestruction();
    telemetry.CommitFrame();
}
```

이 코드는 세부 API보다 다음 경계를 보여 준다.

- platform event와 권위 tick의 분리
- command를 통한 입력 전달
- 고정 틱 commit
- live world 대신 presentation snapshot
- 렌더·오디오 게시 이후 지연 파괴
- 프레임 끝 계측 commit

`RunFrame`은 `Running` 상태 전용이다. `Paused`/`Loading`에서는 `platform.PumpEvents()`와 UI·플랫폼 이벤트만 처리하고 시뮬레이션·렌더 제출 구간은 상태 predicate로 감싸 건너뛴다. pause 진입/해제 시 accumulator를 유지할지 초기화할지, pause 중 도착한 edge 입력을 보존할지 폐기할지도 함께 정한다.

`commands`에는 매 tick 지속되는 held 상태와 한 tick에만 소비되는 pressed/released edge를 구분해 담는다. edge 명령은 지정된 tick에서 정확히 한 번 `Take`되고 다시 나타나지 않는다. 고정 틱이 하나도 실행되지 않은 프레임의 입력은 다음 tick으로 보존한다. 지정 tick보다 늦게 도착한 네트워크 명령은 일반 `Take` 경로에 넣지 않고 별도의 rollback/지연 명령 경로로 보낸다.

`jobs.Run`은 완료 barrier인 `RunAndWait`로 두거나 완료 continuation에서 `CommitTick`을 수행하게 한다. `renderer.Submit`이 snapshot lease를 이전하는지 복사하는지, `world.CommitDeferredDestruction`이 어느 reader 완료와 GPU fence를 기다리는지 명시하고 Part05의 에셋 수명 계약과 통일한다. 안정 ID만 저장해도 GPU buffer 수명이 자동 보장되지는 않는다.

실제 엔진에서는 render와 simulation이 다른 프레임을 처리할 수 있으므로 snapshot slot과 fence를 추가해야 한다. 네트워크 rollback을 사용하면 command history와 state history도 tick 단위로 보존해야 한다. 중요한 것은 기능을 한 루프에 나열하는 것이 아니라 각 데이터의 시점과 owner를 설명할 수 있는 구조다.

---

## 결론

게임 런타임 통합의 핵심은 “모든 시스템을 매 프레임 호출한다”가 아니라 시간, 소유권, 게시 지점을 계약으로 만드는 것이다. 고정 시뮬레이션 틱과 표시 시간, command·event·snapshot, 안정적 handle과 지연 파괴, 작업 그래프와 스레드 도메인을 분리하면 하위 시스템이 같은 상태를 제각기 해석하는 문제를 줄일 수 있다.

좋은 런타임은 평균 프레임이 빠른 런타임만이 아니다. 긴 정지, 비동기 완료 지연, 월드 전환 실패, 장치 손실, 종료 중 callback에서도 불변식을 유지하고 그 실패를 재현할 수 있어야 한다.

## 더 읽을거리

- Jason Gregory, *Game Engine Architecture*, 3rd ed. — 게임 루프, 엔진 하위 시스템, 메모리와 런타임 구조
- Glenn Fiedler, [Fix Your Timestep!](https://gafferongames.com/post/fix_your_timestep/) — 고정 시간 스텝과 누산기
- Robert Nystrom, *Game Programming Patterns* — game loop, update method, event queue
- Anthony Williams, *C++ Concurrency in Action*, 2nd ed. — 메모리 모델, 작업 큐, 동기화
- ISO/IEC 14882, C++ 표준 — 객체 수명과 원자 연산의 happens-before 계약
- Microsoft, “Game Timing and Multicore Processors” — 역사적 배경. 확인한 문서에서 저자(Bruce Dawson) 귀속은 확인되지 않았고, single-core affinity 같은 과거 multicore 권고는 현재의 보편 규칙이 아니다(현재 QPC 지침은 별도). monotonic timing과 프레임 계측의 배경으로만 인용한다.
