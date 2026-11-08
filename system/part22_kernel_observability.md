## 들어가며

커널 문제는 관찰하기 어렵다. 잘못된 포인터 하나가 프로세스가 아니라 시스템 전체를 손상시키고, 로그를 추가하는 행위 자체가 타이밍을 바꾸어 race를 숨길 수 있다. 재현이 끝난 뒤에는 문제가 발생한 메모리와 실행 상태가 이미 사라져 있을 수도 있다.

관측 도구를 이름으로 외우면 `printk`, ftrace, perf, eBPF, KASAN, KGDB를 언제 써야 하는지 판단하기 어렵다. 도구들은 서로 다른 질문에 답한다.

- 사건이 발생했는가?
- 어느 호출 경로가 시간을 사용했는가?
- 어느 상태 전이가 잘못되었는가?
- 누가 잘못된 메모리를 읽거나 썼는가?
- 시스템이 멈춘 시점의 전체 상태는 무엇인가?

이 글은 **증거의 정확도·범위·비용**을 축으로 커널 관측을 정리한다. 정적 tracepoint, ftrace, PMU sampling, eBPF의 검증된 확장 모델, ring buffer와 event loss, sanitizer, lockdep, KGDB, crash dump를 하나의 진단 절차로 연결한다.


---

## 목차

1. [관측 모델과 Observer Effect](#1-관측-모델과-observer-effect)
2. [로그와 정적 이벤트](#2-로그와-정적-이벤트)
3. [ftrace와 perf](#3-ftrace와-perf)
4. [eBPF 실행과 검증](#4-ebpf-실행과-검증)
5. [Map과 이벤트 전송](#5-map과-이벤트-전송)
6. [메모리·동시성 검증](#6-메모리동시성-검증)
7. [정지·패닉·사후 분석](#7-정지패닉사후-분석)
8. [증거 기반 진단 절차](#8-증거-기반-진단-절차)


---

## 1. 관측 모델과 Observer Effect

### 증거의 층

| 층 | 대표 수단 | 얻는 정보 | 주요 비용·왜곡 |
|---|---|---|---|
| counter | per-CPU counter, PMU | 총량·비율 | 원인 경로 부족 |
| sampling | perf sample | 시간 분포·stack | 짧은 사건 누락 가능 |
| tracing | tracepoint, ftrace, eBPF | 사건별 시간축 | event 양·buffer pressure |
| instrumentation | KASAN, KCSAN, lockdep | 특정 오류의 증거 | 큰 실행·메모리 오버헤드 |
| interactive stop | KGDB | 임의 상태 정밀 검사 | 시스템 시간축 파괴 |
| postmortem | kdump, vmcore | 실패 시점 전체 상태 | dump 예약 메모리·재현 필요 |

정밀도가 높은 도구가 항상 먼저가 아니다. 운영 중 p99 지연 원인을 찾는 데 KGDB로 모든 CPU를 멈추면 문제를 측정할 수 없다. 반대로 재현 가능한 use-after-free를 counter만으로 찾기는 어렵다.

### Observer effect

초당 $f$회 event를 기록하고 event 하나의 평균 관측 비용이 $c$초라면 한 CPU에서 단순한 시간 점유율은

$$
U_{\text{observe}} \approx f c
$$

이다. $f=500{,}000$/s, $c=200$ ns면 약 10% CPU다. 여기에 cache line 이동, ring buffer write, wakeup, formatting이 더해질 수 있다.

비용은 균일하지 않다.

- 전역 atomic counter는 여러 CPU가 같은 cache line을 경쟁한다.
- 문자열 formatting은 binary event 기록보다 비싸다.
- call stack 수집은 unwind 방식과 깊이에 따라 비용이 달라진다.
- 사용자 공간 consumer가 느리면 event buffer가 차고 producer가 drop하거나 block한다.
- serial console 출력은 로그 생성보다 훨씬 느릴 수 있다.

### Heisenbug

로그와 breakpoint는 scheduling, interrupt latency, cache state를 바꾼다. race가 로그 추가 뒤 사라졌다고 수정된 것이 아니다. 관측을 좁히고, timestamp counter·per-CPU binary buffer처럼 간섭이 적은 수단과 sanitizer처럼 오류 조건을 직접 검증하는 수단을 병행한다.

### 시간축

서로 다른 CPU의 event를 합치려면 clock source와 ordering 범위를 알아야 한다. Monotonic timestamp가 있어도 같은 timestamp의 전역 순서를 자동으로 주지는 않는다. Per-CPU buffer는 CPU 안의 순서는 보존하기 쉽지만 CPU 사이에는 동기화 event를 이용해 partial order를 복원해야 한다.

## 2. 로그와 정적 이벤트

### printk의 계약

`printk` 계열은 kernel log buffer에 record를 남긴다.

```c
pr_err("submit failed: queue=%u error=%d\n", qid, ret);
pr_warn_ratelimited("rx ring full: queue=%u\n", qid);
pr_debug("state=%u -> %u\n", old_state, new_state);
```

현대 kernel의 `printk` 구현과 console 출력 경로는 version에 따라 바뀐다. 호출이 허용되는 context와 console에 실제 문자가 나가는 시점을 같은 것으로 보면 안 된다. 특히 NMI·panic·atomic context와 normal context의 보장은 kernel version과 설정을 확인한다.

핫 경로 로그는 아래 원칙을 따른다.

- 문자열보다 stable event ID와 정수 field를 우선한다.
- pointer와 secret을 무심코 노출하지 않는다.
- 반복 오류에는 rate limit을 둔다.
- 로그를 내기 전에 공유 상태를 오래 잠그지 않는다.
- 종료된 객체의 pointer를 formatting 과정에서 역참조하지 않는다.

### Dynamic debug

Dynamic debug는 compile된 `pr_debug` callsite를 file, function, module, line, format 조건으로 켠다. 배포 binary를 다시 만들지 않고 범위를 좁힐 수 있지만 callsite가 존재해야 한다.

```bash
echo 'module my_driver +p' \
  > /sys/kernel/debug/dynamic_debug/control

echo 'file drivers/net/example.c line 200-260 +pflmt' \
  > /sys/kernel/debug/dynamic_debug/control
```

운영에서 전체 module의 verbose log를 한꺼번에 켜기보다 한 queue·PID·function으로 좁힌다.

### Tracepoint

Tracepoint는 kernel source에 안정적인 이름과 구조화된 field를 갖춘 event 지점이다.

```text
sched:sched_switch
block:block_rq_issue
block:block_rq_complete
net:net_dev_queue
syscalls:sys_enter_read
```

정적 tracepoint의 장점은 function 이름과 compiler inlining에 덜 의존하고 field 의미가 명시된다는 점이다. 단점은 미리 배치된 지점만 볼 수 있다는 것이다. Kernel 내부 구현 함수에 임시 probe를 붙이는 것보다 가능하면 tracepoint를 먼저 사용한다.

### Event schema

관측 event도 protocol이다.

```c
struct IoEvent {
    __u64 timestamp_ns;
    __u64 request_id;
    __u32 device;
    __u32 bytes;
    __s32 status;
    __u16 cpu;
    __u16 version;
};
```

Producer와 consumer가 다른 시점에 배포될 수 있으면 version과 size를 두고, padding·endianness·field 추가 규칙을 정의한다. 문자열 command line이나 사용자 pointer를 나중에 읽겠다고 저장하면 해당 메모리가 이미 바뀌거나 해제될 수 있으므로 event 시점에 필요한 값만 bounded copy한다.

## 3. ftrace와 perf

### ftrace

ftrace는 function instrumentation과 tracepoint를 kernel tracing ring buffer에 기록한다.

```bash
cd /sys/kernel/tracing
echo function_graph > current_tracer
echo 'submit_bio' > set_graph_function
echo 1 > tracing_on
# workload
echo 0 > tracing_on
cat trace
```

Function graph는 진입·반환으로 inclusive duration을 보여 주지만, interrupt나 preemption으로 빠져나간 시간이 함수 자체의 CPU 작업인지 대기인지 구분해야 한다. Filter 없이 전체 kernel function을 추적하면 event 폭증과 큰 왜곡을 만들 수 있다.

### PMU counter

perf는 hardware performance monitoring unit과 software event를 이용한다.

```bash
perf stat -e cycles,instructions,cache-misses,branches,branch-misses \
    -- command
```

IPC를

$$
\text{IPC} = \frac{\text{instructions}}{\text{cycles}}
$$

로 계산할 수 있지만 낮은 IPC만으로 memory bound를 증명할 수는 없다. Frequency 변화, frontend stall, dependency도 IPC를 낮춘다. Cache miss, stalled cycle, bandwidth, wall time을 함께 본다.

Counter가 hardware slot보다 많으면 kernel이 multiplex한다. 이때 각 event의 enabled time과 running time으로 scaling한 추정치가 나오므로 짧은 실행에서는 오차가 커질 수 있다.

### Sampling

주기 $T_s$로 program counter를 sample하면 긴 실행 구간은 많이 잡히고 짧은 구간은 적게 잡힌다. 특정 함수가 CPU 시간의 비율 $p$를 사용하고 독립적인 sample $n$개를 얻었다고 단순화하면 함수 sample 수 $X$는

$$
X \sim \operatorname{Binomial}(n,p)
$$

이다. 표본 비율 $\hat p=X/n$의 표준 오차는

$$
\sqrt{\frac{p(1-p)}{n}}
$$

이다. 반면 sample 개수 $X$ 자체의 표준편차는 $\sqrt{np(1-p)}$다. 0 sample은 "절대 실행되지 않았다"가 아니라 현재 sample 수에서 희귀할 수 있다는 뜻이다.

```bash
perf record -F 99 -g -- workload
perf report
```

Sampling frequency를 높이면 시간 해상도는 좋아지지만 interrupt·stack unwind 비용이 커진다. Period sampling과 frequency mode, call graph 방식(frame pointer, DWARF, LBR)을 기록한다.

### On-CPU와 off-CPU

CPU profile은 runnable하지 않은 대기 시간을 보여 주지 못한다. Mutex, I/O, scheduler delay가 문제라면 block 시점과 wakeup 시점을 request/thread ID로 연결한다.

```text
enqueue -> block -> wakeup -> scheduled -> completion
```

각 구간의 timestamp 차이를 분리하면 queue delay, sleep duration, run-queue delay, service time을 구분할 수 있다.

## 4. eBPF 실행과 검증

### 제한된 kernel extension

eBPF program은 bytecode로 load되어 verifier를 통과한 뒤 interpreter 또는 JIT-compiled native code로 실행된다. Program type과 attach point가 context type, 사용 가능한 helper, return value 의미를 결정한다.

```text
userspace loader
  -> BPF_PROG_LOAD
  -> verifier abstract interpretation
  -> optional JIT
  -> attach to tracepoint/kprobe/LSM/cgroup/XDP
```

eBPF를 "안전한 임의 kernel code"로 이해하면 안 된다. Verifier가 허용한 제한된 instruction, pointer, helper, bounded resource 안에서 실행하는 extension이다.

### Register와 stack

eBPF instruction은 64비트 register machine을 사용한다.

```text
R0      return value
R1-R5   arguments
R6-R9   callee-saved
R10     read-only frame pointer
```

Stack 크기와 tail call 깊이, program instruction complexity에는 kernel version별 제한이 있다. 상수 하나를 외워 설계하지 말고 목표 kernel의 verifier log와 documentation을 확인한다.

### Verifier의 abstract state

Verifier는 control-flow path를 탐색하며 register마다 scalar range와 pointer provenance를 추적한다.

```text
R1: PTR_TO_CTX
R2: PTR_TO_MAP_VALUE, offset=[0, 64]
R3: SCALAR_VALUE, unsigned range=[0, 255]
```

아래 조건을 증명하지 못하면 program을 거부한다.

- 모든 path가 유효한 종료에 도달한다.
- stack read 전에 초기화되었다.
- pointer arithmetic이 객체 범위를 벗어나지 않는다.
- helper 인자 type과 lifetime이 맞다.
- map value와 packet 접근이 bounds 안이다.
- loop와 path exploration이 verifier complexity budget 안이다.

현대 eBPF는 verifier가 bounded임을 증명할 수 있는 loop를 허용한다. "eBPF에는 loop가 없다"는 설명은 오래된 제약이다. 반대로 source의 작은 loop도 range가 넓으면 verifier state가 폭증할 수 있다.

### 검증이 보장하지 않는 것

Verifier 통과는 아래를 보장하지 않는다.

- 관측 field의 의미가 올바르다.
- event를 빠뜨리지 않는다.
- map key cardinality가 운영 예산 안이다.
- security policy가 의도한 사용자를 막는다.
- tracing overhead가 latency SLO를 지킨다.
- 여러 CPU event의 전역 순서가 정확하다.

eBPF program도 logic test, load test, privilege boundary review가 필요하다.

### Hook 선택

| Hook | 안정성 | 용도 |
|---|---|---|
| tracepoint | 비교적 안정된 event schema | scheduler, syscall, block, network 관측 |
| kprobe/kretprobe | kernel function 구현에 의존 | 임시 내부 진단 |
| uprobe/uretprobe | user binary symbol·offset 의존 | allocator, runtime 함수 관측 |
| fentry/fexit | BTF 기반 낮은 overhead | kernel function 진입·반환 |
| LSM | security decision | 접근 정책·audit |
| XDP/TC | packet data path | 필터, redirect, load balancing |

Kprobe는 compiler가 inline하거나 제거한 function, probe 금지 지점, version별 symbol 변화에 영향을 받는다. 장기 도구는 tracepoint나 BTF 기반 typed attach를 우선한다.

### BTF와 CO-RE

BTF(BPF Type Format)는 kernel type 정보를 제공한다. CO-RE(Compile Once - Run Everywhere)는 field 존재·offset 같은 relocation을 load 시 목표 kernel BTF에 맞춘다. 이것은 모든 kernel에서 의미가 동일함을 보장하지 않는다. Type layout 호환을 개선하는 도구이며 hook 의미와 feature availability는 별도로 검사한다.

## 5. Map과 이벤트 전송

### Map은 상태와 통신 채널이다

eBPF program은 map을 통해 호출 사이 상태를 유지하고 사용자 공간과 데이터를 공유한다.

| Map | 적합한 상태 | 주의점 |
|---|---|---|
| array | bounded index counter/config | 최대 entry를 사전 할당 |
| hash | sparse key state | cardinality·allocation·eviction |
| LRU hash | bounded 최근 key | eviction이 의미를 바꿀 수 있음 |
| per-CPU array/hash | hot counter | 합산 시점과 CPU hotplug |
| ring buffer | ordered event stream | capacity와 loss |
| stack trace map | stack ID dedup | collision·용량 |

전역 hash counter를 모든 CPU가 갱신하면 map bucket과 value cache line에서 경합할 수 있다. Per-CPU map은 update 경합을 줄이지만 consumer가 CPU별 값을 합산해야 한다.

### Cardinality

PID, IP, path, stack 같은 무제한 key를 map에 넣으면 workload cardinality만큼 메모리가 증가한다. Map의 `max_entries`는 단순 설정이 아니라 admission policy다.

고갈 시 정책을 정한다.

- 새 key drop
- LRU eviction
- key를 prefix·bucket으로 aggregate
- sampling
- top-K sketch

어떤 정책이든 drop/eviction counter를 별도로 내야 한다. 관측 도구가 조용히 데이터를 잃으면 정상 상태로 오판할 수 있다.

### Ring buffer

Event producer가 consumer보다 빠르면

$$
\frac{dQ}{dt} = \lambda - \mu
$$

이고 $\lambda>\mu$인 동안 유한 buffer는 결국 찬다. Buffer를 크게 만드는 것은 burst를 늦출 뿐 지속 과부하를 해결하지 않는다.

Event에 길이와 type을 넣고 reserve/submit 또는 discard protocol을 지킨다. Variable-size record를 쓰다가 producer가 중단되어도 consumer가 다음 record 경계를 찾을 수 있어야 한다.

### Ordering

단일 shared ring은 reserve 순서와 commit 순서가 다를 수 있고, per-CPU ring은 CPU 사이 total order가 없다. 동일 request를 연결하려면 timestamp만 믿기보다 request ID와 state transition sequence를 기록한다.

### Lost event

Event loss는 관측 결과의 일부다.

```text
produced
consumed
dropped_by_kernel
dropped_by_userspace
parse_error
```

이 수가 없으면 latency histogram의 꼬리가 실제로 짧은지, 느린 event가 buffer 압박에서 선택적으로 사라졌는지 알 수 없다.

## 6. 메모리·동시성 검증

### KASAN

KASAN은 memory 접근에 shadow check를 넣어 out-of-bounds와 use-after-free를 탐지한다. Generic KASAN의 대표 shadow scale은 8바이트당 1바이트지만 mode와 architecture에 따라 구현이 다르다. Tag 기반 mode도 있다.

KASAN report의 핵심은 fault address뿐 아니라 allocation stack, free stack, access stack을 연결하는 것이다. Report 뒤 kernel state가 손상되었을 수 있으므로 첫 오류를 우선 분석한다.

### KMSAN

KMSAN은 byte의 초기화 여부와 origin을 추적하여 초기화되지 않은 값이 branch, address, copy-to-user 등에 사용될 때 보고한다. 모든 architecture·configuration에서 사용할 수 있는 것은 아니고 큰 overhead가 있으므로 전용 검증 build로 운영한다.

### KCSAN

KCSAN은 sampling과 watchpoint를 이용해 동시 접근을 겹치게 만들어 data race를 탐지한다. 한 번도 보고되지 않았다고 race-free가 증명되는 것은 아니다. 확률적 탐지이므로 stress, 반복, 의도적 delay와 함께 사용한다.

`READ_ONCE`/`WRITE_ONCE`는 compiler가 접근을 쪼개거나 합치는 것을 제한하지만 atomic read-modify-write나 ordering을 자동으로 제공하지 않는다.

```c
// lost update 가능
WRITE_ONCE(counter, READ_ONCE(counter) + 1);

// 다중 writer 증가
atomic_inc(&atomic_counter);
```

### Lockdep

Lockdep은 실행 중 관측한 lock acquisition 순서로 dependency graph를 만들고 cycle 가능성을 탐지한다.

```text
path 1: A -> B
path 2: B -> A
=> potential deadlock
```

실제 deadlock이 아직 발생하지 않아도 순서 cycle을 보고할 수 있다. 모든 실행 path를 자동으로 탐색하지는 않으므로 test가 lock order를 충분히 exercise해야 한다.

### UBSAN과 fault injection

Kernel UBSAN은 선택한 undefined behavior를 runtime check한다. Allocation failure, I/O error, timer delay, workqueue race를 주입하면 정상 성공 경로에서는 보이지 않는 unwind와 shutdown bug를 찾을 수 있다.

Sanitizer를 켠 build는 timing과 memory layout이 달라진다. Sanitizer에서 찾은 오류는 수정해야 하지만 sanitizer build에서 재현되지 않는다는 이유로 production race를 부정할 수 없다.

## 7. 정지·패닉·사후 분석

### KGDB

KGDB는 serial transport(kgdboc)를 통해 GDB가 kernel을 멈추고 register, memory, breakpoint를 검사하게 한다. 정밀하지만 CPU를 정지시키므로 timer, lock timeout, distributed timing을 깨뜨린다.

KGDB에 적합한 문제:

- 재현 가능한 deterministic control-flow bug
- 특정 instruction에서 잘못된 state
- boot 또는 driver bring-up

적합하지 않은 문제:

- breakpoint로 사라지는 race
- 운영 latency 분석
- 여러 node의 시간 상관관계

### Oops와 panic

Oops는 kernel fault를 보고하고 가능하면 현재 task를 종료하지만, kernel이 일관된 상태로 회복되었다는 보장이 아니다. Faulting context가 lock, interrupt state, reference를 손상시켰다면 이후 오류가 연쇄될 수 있다. 운영 정책은 `panic_on_oops`와 dump 확보 여부를 위험 모델에 맞춰 정한다.

Panic은 kernel이 계속 실행하기 어렵다고 판단한 상태다. 첫 panic message와 taint flags, call trace, register, fault address, loaded module을 보존한다.

### kdump와 vmcore

Kdump는 crashkernel용으로 예약한 메모리에 작은 capture kernel을 올려 실패한 kernel의 physical memory를 `vmcore`로 저장한다.

```text
running kernel panic
  -> kexec capture kernel
  -> write vmcore
  -> reboot
  -> crash/drgn analysis
```

예약 메모리가 너무 작거나 dump target이 같은 실패 장치에 의존하면 dump를 잃을 수 있다. 실제 panic injection으로 end-to-end 수집을 검증한다.

### Dump 분석

Debug symbol과 정확한 kernel build가 필요하다.

```bash
crash vmlinux vmcore

crash> bt
crash> ps
crash> log
crash> files <pid>
crash> struct task_struct <address>
```

분석 순서는 마지막 줄부터 무작정 읽는 것이 아니다.

1. 첫 Oops/panic과 fault type을 찾는다.
2. Faulting instruction과 register로 memory access를 재구성한다.
3. Pointer가 어떤 object type과 lifecycle에 속하는지 확인한다.
4. Allocation/free stack 또는 slab metadata와 연결한다.
5. 다른 CPU stack에서 lock owner와 concurrent path를 찾는다.
6. 이후의 secondary fault는 구분한다.

## 8. 증거 기반 진단 절차

### 질문을 수치로 바꾼다

```text
나쁜 질문:
  서버가 가끔 느리다.

검증 가능한 질문:
  64-player match에서 tick p99가 16 ms를 넘는 프레임은
  block I/O completion 뒤 run-queue delay와 상관되는가?
```

필요한 event와 counter가 이 질문에서 나온다.

### 넓게 시작하고 좁힌다

1. CPU, run queue, fault, I/O, network, memory pressure counter로 자원 축을 정한다.
2. perf sampling으로 hot path와 CPU 분포를 찾는다.
3. stable tracepoint로 queue와 latency 구간을 연결한다.
4. 필요한 내부 경계만 ftrace/eBPF probe로 좁힌다.
5. memory/race 의심이면 sanitizer·lockdep build로 재현한다.
6. 정지 상태가 필요하면 KGDB, 사후 상태가 필요하면 vmcore를 사용한다.

### 실험의 대조군

관측을 켠 실행과 끈 실행을 같은 workload로 비교한다.

- throughput과 p50/p99 latency
- CPU utilization
- event produced/consumed/dropped
- ring occupancy
- context switch와 wakeup
- cache miss와 memory bandwidth

Probe를 켠 뒤 문제가 사라지거나 새 병목이 생기면 관측 비용을 줄이거나 sample 방식으로 바꾼다.

### 수명 문제

Use-after-free 진단에는 object ID와 generation, allocation/free event, reference 변화, callback enqueue/execute/cancel을 같은 time line에 둔다.

```text
alloc obj=42 gen=7
queue_work obj=42 gen=7
free obj=42 gen=7
alloc obj=42 gen=8
run_work obj=42 gen=7  <- stale callback
```

Pointer 값만 기록하면 같은 주소가 재사용되었을 때 다른 객체를 하나로 오인한다.

### 종료 조건

진단은 그럴듯한 stack을 찾았다고 끝나지 않는다.

- 원인을 켜고 끌 수 있는 최소 재현이 있다.
- 고장 불변식이 코드나 test로 표현되었다.
- 수정 전 test가 실패하고 수정 뒤 통과한다.
- 관측 도구의 lost event와 overhead가 허용 범위다.
- 유사한 수명·ordering 경로를 함께 검사했다.


---

## 결론

커널 관측은 도구 수집이 아니라 **질문에 맞는 증거와 간섭 수준을 선택하는 일**이다. Counter와 sampling으로 병목 축을 좁히고, tracepoint와 ftrace로 시간 구간을 연결하며, eBPF는 verifier가 허용한 제한 안에서 동적 상태와 event를 수집한다. Map cardinality와 ring buffer loss를 기록하지 않으면 관측 시스템 자체가 결과를 왜곡할 수 있다.

Memory corruption과 race는 KASAN·KMSAN·KCSAN·lockdep처럼 오류 조건을 직접 검사하는 build가 필요하다. 정밀한 상태 검사는 KGDB가, 실패 시점의 전체 상태는 kdump와 vmcore가 담당한다. 각 도구는 정보량을 얻는 대신 timing, memory, CPU를 바꾸므로 대조군과 observer overhead 측정이 필수다.

좋은 진단은 마지막에 재현 가능한 불변식으로 남는다. 어떤 객체가 어느 세대에 해제되었고, 어느 callback이 어떤 완료 조건 없이 접근했는지 설명할 수 있어야 한다. "perf에서 이 함수가 보였다"나 "eBPF로 이벤트를 잡았다"는 증거 수집의 시작이지 원인 증명의 끝이 아니다.

## 더 읽을거리

- Brendan Gregg, *Systems Performance* and *BPF Performance Tools*
- Linux kernel, [Tracing documentation](https://docs.kernel.org/trace/index.html)
- Linux kernel, [BPF documentation](https://docs.kernel.org/bpf/)
- Linux kernel, [KASAN](https://docs.kernel.org/dev-tools/kasan.html), [KCSAN](https://docs.kernel.org/dev-tools/kcsan.html), [KMSAN](https://docs.kernel.org/dev-tools/kmsan.html)
- Linux kernel, [Lockdep design](https://docs.kernel.org/locking/lockdep-design.html)
- Linux kernel, [Using kgdb, kdb and the kernel debugger internals](https://docs.kernel.org/process/debugging/kgdb.html)
- Linux kernel, [Kdump](https://docs.kernel.org/admin-guide/kdump/kdump.html)
- Steven McCanne, Van Jacobson, ["The BSD Packet Filter"](https://www.tcpdump.org/papers/bpf-usenix93.pdf) — classic BPF 역사적 배경; 현대 eBPF verifier/CO-RE의 직접 근거는 아님
- Toke Høiland-Jørgensen et al., ["The eXpress Data Path"](https://dl.acm.org/doi/10.1145/3281411.3281443) — 본문 XDP 문장의 추가 읽을거리
- Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman, *Linux Device Drivers*, 3rd ed. — 고전 드라이버 디버깅 추가 읽을거리; 현대 eBPF/BTF/CO-RE 규약의 직접 근거는 아님
