# system / network 시리즈 검수 보고서

- 대상: `network/` 15편 + README (10,179줄), `system/` 22편 (27,650줄)
- 기준: **확정 가능한 사실오류·산술오류·컴파일 불가 코드·자기모순·서지오류**만. 문체·구성 취향은 대상 아님.
- 방법: 모든 수치 재계산, 서지의 저자/연도/게재지/장절 대조, 코드 컴파일 가능성·논리 검증, 문서 내부 및 문서 간 교차 대조.
- **검수 완료: `network/` 15편 + README, `system/` part01~part22 — 총 37편 37,829줄 전수 검수 완료.**

---

## 요약

| 구분 | 건수 |
|---|---:|
| 확정 사실오류 | 24 |
| 산술·계산 오류 | 5 |
| 컴파일 불가 / 코드 논리 오류 | 11 |
| 서지 오류 | 10 |
| 개정 잔재(존재하지 않는 것을 가리키는 참조) | 2 |
| 문서 내부·문서 간 모순, 최신성 이탈 | 24 |
| 구조 결함(절 교차참조 깨짐) | **26곳** |
| 서지 중복 | 9종 |

문서별 오류 밀도는 고르지 않다. `system/part21_security.md`(확정 4 + 서지 2)와 `system/part20_container.md`(확정 3)가 마지막 7편 중 가장 무겁고, **`part17_windows_iocp.md` · `part18_driver.md` · `part22_kernel_observability.md` 세 편은 확정 오류가 0건**으로 시리즈 전체에서 가장 깨끗하다.

---

## 1. 확정 사실오류

### 1-1. `system/part03_virtual_memory.md` L646-651 — 페이지 폴트 error code 비트가 한 칸씩 밀림

문서:

```
- Bit 3 (I/D): 1 = instruction fetch
- Bit 4 (PK): 1 = protection key violation
```

실제 x86-64 / Linux `X86_PF_*`:

```
bit 3 = RSVD (reserved bit violation)
bit 4 = I/D  (instruction fetch)
bit 5 = PK   (protection key)
```

**RSVD가 누락되면서 I/D와 PK가 각각 한 비트씩 당겨졌다.** bit 0~2(P / W-R / U-S)는 정확하다.

### 1-2. `system/part02_process.md` L878-883 — "4단계"라 쓰고 5개를 나열

> "x86-64는 **4단계** 페이지 테이블을 사용한다: PGD / P4D / PUD / PMD / PTE"

x86-64 기본은 4단계(PGD·PUD·PMD·PTE)이고 **P4D는 5단계 페이징(LA57)용으로 도입된 레벨로 4단계 구성에서는 folding된다.** 같은 시리즈 `part03` L222는 이 관계를 정확히 설명하고 있어 대비된다.

### 1-3. `system/part08_scheduler.md` L705-713 — 스케줄링 "클래스" 우선순위가 클래스와 정책을 혼동

```
1. SCHED_DEADLINE  2. SCHED_FIFO  3. SCHED_RR  4. SCHED_NORMAL  5. SCHED_BATCH  6. SCHED_IDLE
```

**SCHED_FIFO와 SCHED_RR은 같은 클래스(`rt_sched_class`)** 이며 서로의 우열은 목록 순서가 아니라 **rt_priority(1~99) 숫자**로 정해진다(RR prio 90이 FIFO prio 10을 선점). **SCHED_NORMAL/BATCH/IDLE도 모두 `fair_sched_class` 안의 정책**이다. 실제 클래스 서열은 `stop > deadline > rt > fair > idle`.

같은 글 L886은 "두 정책이 하나의 숫자 우선순위 공간을 공유한다는 뜻이 아니다"라고 정확히 쓰고 있어, 이 목록만 어긋난다.

### 1-4. `system/part05_syscall.md` L1779-1787 — 명령이 그 출력을 낼 수 없음

```bash
objdump -T /lib64/libc.so.6 | grep LINUX
```

출력으로 `__vdso_gettimeofday` 등을 제시했으나, **`__vdso_*` 심볼은 libc가 아니라 vDSO(`linux-vdso.so.1`)의 동적 심볼 테이블에 `LINUX_2.6` 버전으로 들어 있다.** libc.so.6에는 없다.

### 1-5. `system/part05_syscall.md` L1314-1319 — "레지스터 부족" 산수가 성립하지 않음

> "x86-64는 범용 레지스터가 **16개**지만 … RAX / RCX·R11 / RSP·RBP … **나머지 6개**: 인자 전달"

16 − 5 = **11**이지 6이 아니다. RBX·R12~R15는 "예약"된 것이 아니라 callee-saved라 쓰지 않는 것이고, 6개라는 결론은 ABI 관례에서 나오지 이 뺄셈에서 나오지 않는다.

### 1-6. `system/part03_virtual_memory.md` L842-844 — 실행 불가능한 예시

```bash
$ cat /proc/1234/status | grep Fault
VmPeak:    12345 kB
VmSize:    12345 kB
```

`grep Fault`는 "Fault"를 포함하지 않는 VmPeak/VmSize 줄을 출력할 수 없다. 게다가 **`/proc/[pid]/status`에는 페이지 폴트 카운터가 없다**(min_flt/maj_flt는 `/proc/[pid]/stat`의 10·12번째 필드).

### 1-7. `network/part10_rollback.md` L512 — 크로스플레이 범위

> "크로스 플랫폼(Street Fighter 6, Tekken 8): PC ↔ 콘솔 ↔ **모바일** 교차 매치가 현대 격투의 표준"

SF6·철권8의 크로스플레이는 **PC / PS5 / Xbox Series**이고 **모바일 교차 매치는 없다.**

### 1-8. `network/part15_anticheat_trust.md` L122 — Vanguard 적용 범위

> "Vanguard (Riot) … 자체 (**Valorant 전용**)"

Vanguard는 **2024년 5월부터 리그 오브 레전드(및 TFT)에도 필수 적용**되었다. "Valorant 전용"은 현재 시점에서 사실과 다르다. (L302·L810의 "부팅 시 상주 논란" 서술 자체는 정확)

### 1-9. `network/part05_services_security.md` L131 — 429를 "재시도 무의미"로 분류

> "4xx *클라이언트 잘못* (재시도 무의미 — 400 …, **429 과요청**)"

같은 글 L196("429 + Retry-After … 백오프"), L226("과부하를 뜻하는 `429`·`503`과 영구 실패를 구분")과 정면 충돌한다. **429는 4xx이지만 재시도 대상**이다.

### 1-10. `network/part08_authority_prediction.md` L888 — 문서가 스스로 금지한 이중계산

§12(L374)는 이렇게 못 박는다.

> "내가 보는 상대의 snapshot time은 현재 추정 server time보다 interpolation delay만큼 과거다. **`RTT/2`를 여기에 다시 더하면 client 표시 age를 중복 계산한다.**"

그런데 §37 타임라인(RTT 80ms → 편도 40ms, 보간지연 100ms)의 `t=0ms` 항목은

> "적은 보간된 과거(**서버 기준 약 -140ms**)의 모습"

이라고 적었다. 공유 시간축의 t=0에서 클라가 보는 상대는 `추정 서버시간 − 보간지연 = **−100ms**`다. −140ms는 100 + 40을 더한 값으로, 바로 그 이중계산이다. (L892의 "-140ms"는 *수신 시각 t=40 기준 되감기 양*이라 맞다 — 두 −140의 기준점이 달라 한쪽만 옳다.)

### 1-11. `system/part12_filesystem.md` L216-221 — 디렉토리 엔트리 예시의 `..` inode가 불가능

```
inode=2     name="."
inode=1     name=".."
```

ext 계열에서 **inode 1은 bad blocks inode**로 예약되어 있어 어떤 디렉토리의 `..`도 1을 가리킬 수 없다. 게다가 `.`이 inode **2(루트)** 라면 루트의 `..`도 **2**여야 한다(루트의 부모는 자기 자신). 같은 글 L229가 "ext 계열 root inode 번호는 2"라고 명시하고 있어 자기모순이기도 하다. 섹션 제목이 "ext4 디렉토리 엔트리"이고 구조체도 `ext4_dir_entry_2`라 ext4 문맥이 확정적이다.

### 1-12. `system/part13_block_io.md` L1030 — `nomerges` 값 1의 의미

> "`0: 모두 병합, **1: 쓰기만**, 2: 비활성`"

커널 문서(`Documentation/block/queue-sysfs.rst`) 기준 **1은 "simple one-hit merge만 시도"**(복잡한 tree/hash lookup 비활성)이며 읽기/쓰기 구분과 무관하다.

### 1-13. `system/part14_io_uring.md` L1757 — CVE-2020-29373 수정 버전

문서는 "**5.10**에서 … 수정"이라 하지만, 이 CVE는 "Linux kernel **before 5.6**"에 영향을 주고 `io_uring: grab ->fs as part of async offload`(ff002b30181d) 커밋으로 **v5.6**에 수정되었다. (바로 다음 항목 CVE-2021-41073의 "5.14.7에서 수정"은 정확하다.)

### 1-14. `network/part09_state_replication.md` — 본문과 서지가 반대로 말함

- 본문 L262: "DRR의 fairness는 … **byte service**에 관한 것이다. entity update **frequency** 자체를 보장하려면 target interval·deadline scheduler를 별도로 둔다."
- 본문 L201: "`priority=3`이 `priority=1`보다 정확히 세 배 자주 전송된다는 보장은 없다."
- 서지 L619: "Shreedhar & Varghese DRR … 가중치 비례 throughput 정리가 **빈도 비례 법칙의 이론적 근거**"

서지 주석이 본문 논지를 정반대로 요약한다.

### 1-15. `system/part05_syscall.md` L1614 ↔ `system/part03` L1951 — 문서 간 정면 충돌

- part05: "`/proc/<pid>/stack`에서 [스택 사용량] 확인 가능."
- part03: "`/proc/<pid>/stack`은 **call stack 관측 인터페이스이지 크기 계약을 조회하는 API가 아니다.**"

part03이 옳다. `/proc/<pid>/stack`은 커널 콜 트레이스이며 스택 사용량과 무관하다.

### 1-16. `system/part16_network_stack.md` L117-119 — `struct sk_buff`에 없는 멤버 2개

```c
struct sk_buff {
    ...
    /* 8. 분산 데이터 (선택적) */
    skb_frag_t          frags[MAX_SKB_FRAGS];
    struct skb_shared_info *shinfo;
};
```

**둘 다 `sk_buff`의 멤버가 아니다.** `skb_shared_info`는 sk_buff 구조체가 아니라 **데이터 버퍼 뒤(`skb_end_pointer(skb)`)** 에 놓이며 `skb_shinfo(skb)` 매크로로 접근한다. `frags[]`는 그 `skb_shared_info` 안에 있다. 같은 글 L181-185가 `frags`를 `struct skb_shared_info` 안에 정확히 넣어 설명하고 있어 **문서 내부에서 충돌**한다.

### 1-17. `system/part19_virtualization.md` L588-592 — 중첩 가상화 레벨이 표준·본문과 한 칸씩 어긋남

```text
L0: 물리 하드웨어
  └─ L1: KVM (호스트)
       └─ L2: KVM (게스트 안의 KVM)
            └─ L3: 게스트 OS
```

커널 문서(`Documentation/virt/kvm/nested-vmx.rst`)의 표준은 **L0 = KVM을 돌리는 bare-metal 호스트, L1 = guest hypervisor, L2 = nested guest**이며 L3는 없다. 문서 자신도 바로 아래에서 표준 용법을 쓴다 — L602 "**L1이 VMCS를 수정하면 L0이 intercept**", L604 "GVA → **GPA_L2 → GPA_L1** → HPA". 위 다이어그램대로 읽으면 "물리 하드웨어가 호스트 KVM의 VMCS 쓰기를 intercept한다"가 되어 성립하지 않는다.

### 1-18. `system/part20_container.md` L93-104 — `struct nsproxy`에 `user_ns`가 들어 있음

```c
// kernel/nsproxy.c
struct nsproxy {
    ...
    struct user_namespace *user_ns;     // UID/GID 매핑, capability
    // 총 8종: MNT, PID, NET, IPC, UTS, USER, CGROUP, TIME
};
```

**user namespace는 nsproxy의 멤버가 아니다.** 커널에서 user ns는 `struct cred`에 달려 있고(`task->cred->user_ns`), 그래서 `unshare(CLONE_NEWUSER)`는 nsproxy가 아니라 cred를 교체한다. `create_new_namespaces()`가 다루는 것은 uts/ipc/mnt/pid/cgroup/net/time **7종**뿐이므로, "총 8종"을 한 구조체 안에 넣은 주석까지 함께 틀렸다.

부가 오류 3개: (a) 정의 위치는 `kernel/nsproxy.c`가 아니라 **`include/linux/nsproxy.h`**, (b) net 멤버 타입은 `struct net_namespace *`가 아니라 **`struct net *net_ns`**, (c) pid 멤버 이름은 **`pid_ns_for_children`**(현재 pid ns는 `task_active_pid_ns()`로 얻는다).

### 1-19. `system/part20_container.md` L753-759 — devicemapper의 CoW를 "No"로 표기

```
| devicemapper | LVM | No | |
```

devicemapper storage driver는 **device-mapper thin provisioning(dm-thinp)의 CoW 스냅샷**으로 이미지 레이어를 구현한다. 파일 단위가 아니라 **블록 단위 CoW**일 뿐 CoW가 없는 것이 아니다(Docker 공식 문서: "leverages the thin provisioning and snapshotting capabilities"). 같은 표의 overlay2/aufs/btrfs/zfs = Yes는 정확하다.

### 1-20. `system/part20_container.md` L184-189 — `struct mnt_namespace`의 파일 표기

`kernel/nsproxy.c`로 적혀 있으나 실제 정의는 **`fs/mount.h`**다. 바로 위 nsproxy 코드와 같은 파일로 묶여 있다.

### 1-21. `system/part21_security.md` L369-374 — "SELinux + AppArmor 동시 사용 가능" (중요)

```text
Linux 5.1+ 부터 stacking 지원:
- SELinux + AppArmor 동시 사용 가능
- 둘 다 허용해야만 접근 가능 (AND 조건)
```

SELinux·AppArmor·Smack·TOMOYO는 모두 **`LSM_FLAG_EXCLUSIVE`** 로 등록되며, `security/security.c`의 `lsm_allowed()`가 "이미 exclusive LSM이 초기화됐으면 skip"으로 막는다. **현재 메인라인에서도 이 둘을 동시에 켤 수 없다**(major 모듈 full stacking은 미병합). 4.2/5.1에서 가능해진 것은 **minor LSM**(capability, Yama, LoadPin, SafeSetID, Landlock, lockdown)을 major 1개와 함께 쌓는 것이다.

바로 앞 L361-362가 제시한 실제 출력 `lockdown,capability,landlock,yama,apparmor`도 major는 apparmor **하나뿐**임을 보여주고 있어 문서 내부에서도 어긋난다.

### 1-22. `system/part21_security.md` L889-899 — `capable()`이 `current_user_ns()`를 본다고 서술 (중요)

```c
// kernel/capability.c
bool capable(int cap) {
    struct user_namespace *ns = current_user_ns();
    if (!security_capable(current_cred(), ns, cap))
        return true;
```

실제 커널은 `bool capable(int cap) { return ns_capable(&init_user_ns, cap); }` — **`current_user_ns()`가 아니라 `init_user_ns` 기준**이다. 이 한 줄이 바로 위 L883-886이 선언한 규칙("네임스페이스 내부에서 CAP_SYS_ADMIN을 가져도 외부 객체에는 영향 없다")을 성립시키는 장치인데, 문서 코드대로면 컨테이너 안 root가 `capable()`을 통과해 규칙이 무너진다. `current_user_ns()`를 보는 것은 `ns_capable(current_user_ns(), cap)` 쪽이다.

### 1-23. `system/part21_security.md` L901-909 — 존재하지 않는 커널 코드

```c
static int inode_permission(struct inode *inode, int mask) {
    struct user_namespace *inode_userns = inode->i_sb->s_user_ns;
    if (!in_userns(current_user_ns(), inode_userns))
        return -EACCES;
}
```

`fs/namei.c`의 `inode_permission()`에는 **이런 검사가 없다**(sb 읽기전용 검사 → `do_inode_permission()` → `devcgroup_inode_permission()` → `security_inode_permission()`). `s_user_ns`는 실재하지만 쓰이는 곳은 `privileged_wrt_inode_uidgid()` / `capable_wrt_inode_uidgid()`와 마운트 시점 검사다. `in_userns()`도 `kernel/user_namespace.c`의 static 함수라 namei.c에서 호출할 수 없다.

### 1-24. `system/part21_security.md` L77-88 — setuid 처리 코드의 파일·필드가 모두 틀림

```c
// kernel/sys.c
SYSCALL_DEFINE3(execve, ...) {
    if (bprm->file->f_mode & S_ISUID) {
```

(a) `execve`는 `kernel/sys.c`가 아니라 **`fs/exec.c`**에 있다. (b) `f_mode`는 파일 **개방 모드**(FMODE_READ/FMODE_WRITE)이지 퍼미션 비트가 아니다. setuid 판정은 **`inode->i_mode & S_ISUID`**(`fs/exec.c`의 `bprm_fill_uid()`)로 한다. 바로 다음 줄에서는 `bprm->file->f_inode->i_uid`로 inode를 제대로 타고 들어가고 있어 세 줄 안에서 앞뒤가 맞지 않는다.

---

## 2. 산술·계산 오류

### 2-1. `system/part03_virtual_memory.md` L263 — PDPT 인덱스

`0x00007F8A12345678`의 하위 48비트:

```
0111 1111 1000 1010 0001 0010 0011 0100 0101 0110 0111 1000
```

bits 38..30 = `0 0 0 1 0 1 0 0 0` = **0x028 (=40)**. 문서는 **0x14 (=20)** — 정확히 절반이므로 39비트 시프트로 계산한 off-by-one으로 보인다.

### 2-2. `system/part03_virtual_memory.md` L275 — PT 인덱스가 애초에 불가능

`(V >> 12) & 0x1FF`의 결과 범위는 0~0x1FF인데 문서는 **0x345**를 적었다(0x345 > 0x1FF). 실제 bits 20..12 = `1 0 1 0 0 0 1 0 1` = **0x145**.

※ 같은 walkthrough의 PML4=0xFF, PD=0x91, 최종 `(0x77777<<12)|0x678 = 0x77777678`, "총 5회 메모리 접근"은 모두 정확하므로 이 두 줄만 고치면 된다.

### 2-3. `system/part10_elf_linker.md` L244-246 — `size` 출력의 hex 열이 dec 열과 다름

```
   text    data     bss     dec     hex filename
   1605     612 4000020 4002237  3d0e9d test_bss
```

dec 열은 자기정합적이다(1605 + 612 + 4,000,020 = **4,002,237** ✓). 그런데 **`0x3d0e9d = 4,001,437`** 로 800 차이가 난다. 4,002,237의 16진 표현은 **`0x3d11bd`**.

### 2-4. `system/part11_dynamic_linking.md` L255-256 — `.got.plt` 주소가 같은 글의 PLT 덤프와 어긋남

```
  [21] .got.plt   0000000000404018  00004018   size 0000000000000020
```

그러나 L330-331의 PLT0 덤프는

```
401010:  ff 35 f2 2f 00 00   push 0x2ff2(%rip)   # 404008 <_GLOBAL_OFFSET_TABLE_+0x8>
401016:  ff 25 f4 2f 00 00   jmp  *0x2ff4(%rip)  # 404010 <_GLOBAL_OFFSET_TABLE_+0x10>
```

이므로 **`_GLOBAL_OFFSET_TABLE_` = 0x404000**이다. 크기 0x20 = 4엔트리(예약 3 + printf 1)이고 printf 슬롯이 0x404018(= GOT+0x18)이므로 **`.got.plt`의 주소/오프셋은 0x404000 / 0x4000이어야 한다.** 0x404018로 두면 `.got`(0x403fc0 + 0x40 = 0x404000까지) 뒤에 어느 섹션도 덮지 않는 0x18 공백이 생기고, PLT0이 참조하는 0x404008·0x404010이 어떤 섹션에도 속하지 않게 된다.

### 2-5. `network/part04_socket_server_runtime.md` L451 — KiB / KB

> "순간 20 Mbit/s burst … 100 ms stall … **약 250 KiB**"

20×10⁶ bit/s × 0.1 s = 2×10⁶ bit = **250,000 B = 약 250 KB = 244 KiB**.

---

## 3. 컴파일 불가 / 코드 논리 오류

### 3-1. `network/part10_rollback.md` L282-298 — "예측 적중" 분기가 절대 안 걸림

```c
if (f.confirmed[playerId] && f.inputs[playerId] == input) return;  // 예측 적중, 무비용
```

`confirmed[]`는 주석(L207)대로 **"진짜 입력인가, 예측인가"** 플래그다. 원격 입력을 **예측**한 프레임은 `confirmed == false`이므로, **예측이 맞아떨어져도 이 분기를 못 타고 무조건 롤백 + 재시뮬**을 돈다. 실제로 걸러지는 건 "이미 확정된 입력의 중복 패킷"뿐이다.

같은 글 L71("예측이 맞으면 rewind가 필요 없다")·L244("예측이 맞으면 롤백 비용 0")·해당 줄 주석과 모두 충돌한다.

올바른 형태: `if (f.inputs[playerId] == input) { f.confirmed[playerId] = true; return; }`

### 3-2 · 3-3. `system/part07_signal.md` L1131, L1519 — 미선언 변수 `tid` (2곳)

```c
int main() {
    pthread_t signal_tid;
    pthread_t workers[4];
    pthread_create(&tid, NULL, thread_func, NULL);   // 'tid' 미선언
```

그리고 L1493-1521 `main()`의 마지막 줄에도 `pthread_join(tid, NULL);`. 두 예제 모두 그대로는 컴파일되지 않는다.

### 3-4. `system/part05_syscall.md` L1204-1207 — glibc 래퍼의 오류 검사

```c
if (ret == -1) { errno = -ret; return -1; }
```

커널은 **음수 errno**(`-EBADF = -9` 등)를 반환한다. `ret == -1`은 `-EPERM` 한 경우만 잡고, **`-9`가 오면 오류로 인식하지 못한 채 −9를 바이트 수로 반환**한다. 실제 glibc는 `(unsigned long)ret >= -4095UL` 범위 검사를 쓴다.

### 3-5. `system/part05_syscall.md` L1348-1356 — `clone3` 예시가 커널 스택 오버플로

```c
if (copy_from_user(&args, uargs, size))   // size는 사용자 제공값, args는 고정 크기 지역변수
```

실제 커널은 `copy_clone_args_from_user()`에서 `size`를 `sizeof(struct clone_args)`와 대조한다. §7이 "사용자 포인터를 직접 역참조하지 말라"고 강조하는 글에서 나온 예시라 특히 눈에 띈다. (`_do_fork`도 5.10에서 `kernel_clone`으로 개명)

---

### 3-6. `system/part13_block_io.md` L897 — 삼항 조건이 상수라 항상 write

```c
cmd.rw.opcode = nvme_cmd_write ? nvme_cmd_write : nvme_cmd_read;
```

`nvme_cmd_write`는 `enum nvme_opcode`의 상수(0x01)이므로 조건이 **항상 참**이다. 읽기 요청도 write opcode로 나간다. 의도는 `rq_data_dir(req) == WRITE ? nvme_cmd_write : nvme_cmd_read`.

### 3-7. `system/part14_io_uring.md` L1866 — `int fds[16] = {-1};`

C 초기화 규칙상 **fds[0]만 −1이고 fds[1..15]는 0**이다. `io_uring_register_files`에서 −1은 "빈 슬롯"이므로, 의도(전부 빈 슬롯)와 달리 **슬롯 1~15에 fd 0(stdin)이 등록**된다.

### 3-8. `system/part15_ipc.md` L1775 — 포맷 지정자 불일치(UB)

```c
printf("Subscriber 1: %s (v%d)\n", ps->data, ps->version);
```

`version`은 **`uint64_t`**(L1755)인데 `%d`로 받는다. `%" PRIu64 "` 또는 `%llu` + 캐스트가 필요하다.

### 3-9. `system/part15_ipc.md` L2049-2090 — 전방 선언 없이 호출

`main()`이 `worker_main()`(L2052)을, `worker_main()`이 `handle_request()`(L2079)를 **정의보다 먼저 호출**하는데 프로토타입이 없다. C99 이후 암시적 함수 선언은 제거되어 **컴파일 에러**다. 헤더도 `fcntl.h`(O_CREAT/O_RDWR), `unistd.h`(ftruncate/fork/read/write/close/sleep), `stdio.h`, `stdint.h`, `string.h`, `stdlib.h`가 전부 빠져 있다(포함된 것은 socket.h/un.h/mman.h/pthread.h 넷뿐).

### 3-10. `system/part15_ipc.md` L1882-1904 — `ipcperms()`가 어떤 권한도 통과시키지 못함

커널 호출자(`ipc/shm.c` 등)는 `flag`로 **`S_IRUGO`(0444) / `S_IWUGO`(0222)** 를 넘기므로 실제 코드는 먼저 `requested_mode = (flag >> 6) | (flag >> 3) | flag;` 로 3비트로 접는다. 문서 버전은 이 정규화 없이 `mode`(0~7)와 `flag`(0444 등)를 `(mode & flag) == flag`로 비교하므로 **결과가 항상 거짓 → 항상 `-EACCES`** 다.

부수적으로 실제 함수에 있는 **`CAP_IPC_OWNER` 우회**와 **`security_ipc_permission()` LSM 훅**이 빠졌고, 반환값도 `-EACCES`가 아니라 `-1`이며, 그룹 검사 함수는 `in_egroup_p()`가 아니라 **`in_group_p()`** 다.

### 3-11. `system/part20_container.md` L203-225 — `pivot_root`가 EINVAL로 실패하는 순서

`setup_rootfs()`의 bind mount → chdir → pivot_root → `umount2(MNT_DETACH)` 순서 자체는 정확하지만, 현대 배포판은 `/`가 shared propagation이라 **`mount(NULL, "/", NULL, MS_REC|MS_PRIVATE, NULL)`을 먼저 호출하지 않으면 `pivot_root`가 EINVAL로 실패**한다(runc가 실제로 하는 일).

---

## 4. 서지 오류

### 4-0-a. `system/part14_io_uring.md` L2091 — 한 이름이 서로 다른 두 자료를 가리킴

```
- Lord of the io_uring(CVE 분석): https://googleprojectzero.blogspot.com/2023/06/io_uring-new-code-new-bugs.html
```

**"Lord of the io_uring"은 L2102의 `unixism.net/loti/` 튜토리얼 이름**이고, Google Project Zero의 보안 분석과는 전혀 다른 자료다. 같은 목록 안에서 한 이름이 두 자료에 붙어 있다.

### 4-0-b. `system/part14_io_uring.md` L2090 — 제목과 URL 불일치

"ScyllaDB, *io_uring and Seastar: The Way Forward* (2020)"인데 링크는 `.../how-io_uring-and-ebpf-will-revolutionize-programming-in-linux/`로, 그 URL의 실제 글 제목은 "How io_uring and eBPF Will Revolutionize Programming in Linux"다.

### 4-1. `system/part09_sync.md` L936 — 매체·문서·URL이 모두 어긋남

```
- LWN: ["Futexes are tricky"](https://www.kernel.org/doc/Documentation/futex-requeue-pi.txt)
```

- **"Futexes Are Tricky"는 Ulrich Drepper의 논문**이고 LWN 기사가 아니다.
- 링크가 가리키는 `Documentation/futex-requeue-pi.txt`는 **Darren Hart의 requeue-PI 문서**로 전혀 다른 글이다.

바로 위 L935의 LWN "The design of preemptible read-copy-update"(lwn.net/Articles/253651/)는 정확하다.

### 4-2. `network/part08_authority_prediction.md` L989 — RFC 5905 절 번호

> "RFC 5905 **§8** — … 시계 규율(점프 대신 슬루)과 **최솟값 필터**의 표준 정의"

RFC 5905에서 **§8은 On-Wire Protocol**(오프셋/지연 공식)이다. **최솟값 필터는 §10 Clock Filter Algorithm**, **슬루 규율은 §12 Clock Discipline Algorithm**. 비대칭 편향 근거로 §8을 든 것(L515)은 타당하다.

### 4-3. `system/part06_interrupt.md` L1508 — 4.4BSD 장 번호

> "McKusick et al., *The Design and Implementation of the 4.4BSD Operating System*, **Chapter 5**"

4.4BSD 책의 **5장은 Memory Management**이고, 인터럽트·트랩은 **3장 Kernel Services**다. 같은 목록의 Love **7장**(Interrupts and Interrupt Handlers)과 Bovet & Cesati **4장**(Interrupts and Exceptions)은 정확해서 대비된다.

### 4-4. `system/part02_process.md` L219-224 — `fork_init`의 usercopy whitelist

`offsetof(struct task_struct, saved_state)` / `sizeof_field(..., saved_state)`로 되어 있으나, 실제 커널은 `task_struct_whitelist()`가 아키텍처의 `arch_thread_struct_whitelist()`(FPU 상태)를 받아 **`offsetof(struct task_struct, thread)` 기준**으로 계산한다. `saved_state`는 PREEMPT_RT용 필드로 무관하다.

### 4-5. `system/part21_security.md` L1241 — SubDomain 논문 연도

> "Cowan, C., et al. (**1999**). *SubDomain: Parsimonious Server Security*. USENIX LISA."

이 논문(AppArmor의 전신)은 **USENIX LISA 2000**(14th Systems Administration Conference, 2000년 12월, New Orleans) 발표작이다. 같은 목록의 Smalley et al.(2001) NAI Labs와 Wright et al. USENIX Security 2002는 정확하다.

### 4-6. `system/part21_security.md` L1253 — 발행 주체 오귀속

> "NSA (2020). *SELinux User's and Administrator's Guide*."

이 제목의 문서는 **Red Hat의 RHEL 공식 문서**다. NSA가 낸 것은 SELinux 원본 릴리스와 관련 기술 문서이지 이 가이드가 아니다.

### 4-7. `system/part21_security.md` L1242 — LWN 기사 제목 (확신도 중)

> "Edge, J. (2015). *Seccomp and the v2 API*. LWN.net. https://lwn.net/Articles/656307/"

저자·연도·매체는 맞으나 이 URL의 실제 기사 제목은 **"A seccomp overview"**(Jake Edge, 2015-09-02)로 알고 있다. 발행 전에 링크를 한 번 열어 확인할 것.

### 4-8. `system/part15_ipc.md` L2324 — 존재하지 않는 장을 IPC 참고로 지목

> "Robert Love, *Linux System Programming*, 2nd Edition **7~8장**"

LSP 2판의 **7장은 Threading, 8장은 File and Directory Management**이며, 이 책에는 **IPC 전용 장이 아예 없다**(2판 구성: 1 Intro / 2 File I/O / 3 Buffered I/O / 4 Advanced File I/O / 5 Process Management / 6 Advanced Process Management / 7 Threading / 8 File and Directory Management / 9 Memory Management / 10 Signals / 11 Time). IPC 참고문헌 목록에 7~8장을 지목한 것은 성립하지 않는다.

같은 줄 위의 TLPI 장 범위도 좁다 — "43~46장(… System V IPC)"이라 했으나 System V IPC는 **45~48장**(46 메시지 큐, 47 세마포어, 48 공유 메모리)까지이고, "52~54장(POSIX IPC)"은 **51장(Introduction to POSIX IPC)** 부터여야 한다. 이 글은 SysV 세마포어·공유 메모리와 POSIX IPC 개요를 모두 다룬다.

### 4-9. `system/part16_network_stack.md` L1260 — BBR 논문 연도가 자매 문서와 어긋남

> "Cardwell et al. (**2017**). BBR … ACM Queue"

BBR의 **ACM Queue 판은 2016년**(vol.14 no.5, Sep-Oct 2016)이고, `network/part06` L452는 이미 "ACM Queue **2016**"이라 정확히 적고 있다. 같은 시리즈 안에서 같은 논문의 연도가 갈린다. (2017은 *Communications of the ACM* 재게재판 연도다.)

### 4-10. `system/part22_kernel_observability.md` L540 — 저자명 오기와 순서

> "**Gregg KH**, Jonathan Corbet, Alessandro Rubini, *Linux Device Drivers*"

(a) 이름은 **Greg KH**(Greg Kroah-Hartman)이며, 'Gregg'는 두 항목 위의 **Brendan Gregg**와 섞인 오기로 보인다. (b) LDD3의 저자 표기 순서는 **Corbet, Rubini, Kroah-Hartman**으로 Greg KH가 셋째다.

---

## 5. 개정 잔재 — 존재하지 않는 것을 가리키는 참조

### 5-1. `network/part13_replay_spectating.md` L350

> 위 "**약 0.1°**"가 어디서 나오고 최악일 때 얼마인지는 세 단계로 닫힌다.

**문서 어디에도 "약 0.1°"가 없다.** 앞 절(smallest-three, L315-346)은 각오차 수치를 전혀 제시하지 않는다(`network/` 전체 grep으로 확인: 이 줄이 유일). 뒤따르는 유도의 결론은 최악 **0.27°**이고 단일 성분 오차만 볼 때가 약 0.079° ≈ 0.1°이므로, 앞 문장을 복원하거나 이 도입부를 고쳐야 한다.

### 5-2. `network/part14_voice.md` L465

> 위 적응 코드의 **`2.0f * jitterMs`**·`lateRatio` 임계는 …

위 코드(L453)는 `p.baseMarginMs + p.jitterGain * rfcJitterMs`이고, `2.0f * jitterMs`라는 표현은 문서 어디에도 없다(grep 확인). 리터럴을 정책 상수로 바꾸면서 이 문장만 남았다. 5-1과 동일 유형.

---

## 6. 그 밖의 자기모순 · 최신성 이탈

| 문서 | 위치 | 내용 |
|---|---|---|
| system/part02 | L113-119 | `EXIT_DEAD`·`EXIT_ZOMBIE`를 "`state` 필드 상수" 블록에 나열 — 실제로는 **`exit_state`** 값(본문 L380·L1547은 올바르게 씀) |
| system/part02 | L1798 | `clone` 인자 순서가 같은 글 L1743의 선언과 4·5번째가 뒤바뀜(CLONE_BACKWARDS 규약) |
| system/part02 | L1694 ↔ L1990 | "`32768`은 현대 기본값이 아니다"라고 못 박고, 예시는 `pid_max` → `32768`을 단서 없이 제시 |
| system/part02 | L129 vs L76 | "Linux 5.15 기준"이라 명시했으나 `volatile long state`는 5.14 이전 형태(현재 `unsigned int __state`) |
| system/part01 | L383 / L434 | `kmem_cache_init()`이 `start_kernel`과 `mm_init` 양쪽에 등장 — 실제로는 `mm_init`만 호출 |
| system/part01 | L570 | "PID 1은 SIGKILL/SIGSTOP을 **무시할 수 있음**" — 커널이 전역 init에 기본동작 시그널을 전달하지 않는 것이지 init이 무시하는 게 아님 |
| system/part01 | L1112 | `[Mon Dec 23 … 2025]` — 2025-12-23은 **화요일** |
| system/part06 | L241 ↔ L345 | "CPL이 DPL보다 **높은 권한**"(strict) vs "**CPL ≤ gate.DPL**" — 후자가 정확(int3는 CPL=DPL=3) |
| system/part07 | L824 vs L1147 | `wants_signal()`이 서로 다른 본문으로 두 번 등장(뒤쪽이 실제 커널과 일치) |
| system/part07 | L513-519 | `sa_handler`/`sa_sigaction`을 별개 멤버로 나열 — 실제로는 **union** |
| system/part07 | L868 vs L1760 | 본문은 POSIX.1-**2024**, 서지는 POSIX.1-**2017** |
| system/part03 | L349 | 스택 범위 끝 주소 `0x7FFFFFFFFF`에 F가 2개 부족(시작보다 작아짐) |
| system/part03 | L351 | "PD **엔트리** 하나가 1GB" — 엔트리는 2MB, **테이블**(또는 PDPT 엔트리)이 1GB |
| system/part03 | L1315 vs L2128 | 본문은 "명시적 사용자 버퍼 복사 없는 I/O"로 신중히 고쳤는데 결론은 다시 "제로 카피 I/O" |
| system/part04 | L489 / L526 | zswap을 **frontswap** 기준으로 서술 — frontswap은 **6.6에서 제거**됨 |
| system/part04 | L589 | `page->mem_cgroup` — **5.11에서 `page->memcg_data`로 대체** |
| system/part09 | L648-655 | `smp_read_barrier_depends()` — 4.15에서 `READ_ONCE()`로 흡수, **5.9에서 제거** |
| network/part06 ↔ part12 | L428-429 / L98 | part12가 "TBF `burst`는 **바이트** 단위이고 `32kbit`는 약 4KiB로 해석된다"고 명시하는데, part06은 정확히 `burst 32kbit`와 `parent 1:`을 사용 |
| network/part14, part15 | L176, L104 | 표 헤더 2열 vs 구분자 3열(마크다운 문법 불일치) |
| network/part10 | L540 | 서지 제목 "Tony Cannon, **GGPO: Breaking the Latency Barrier**" — 널리 알려진 글은 "Fight the Lag! …"(Gamasutra 2012). 확신도 낮음 |
| system/part12 | L824 | 결론 "ext4는 안정성, XFS는 대용량 성능, Btrfs는 고급 기능" — 본문 L600·L613·L632가 "이름만으로 최고를 정하는 표는 재현 가능한 지식이 아니다"라며 피한 단정을 결론에서 되풀이 |
| system/part13 | L985-992 | `blktrace -d /dev/nvme0n1` 인데 `blkparse` 출력의 장치는 `8,0`(= `/dev/sda`) |
| system/part13 | L367, L207 | `address_space.nrexceptional`(5.9 무렵 제거), `bio_for_each_segment_all`의 3번째 인자(5.1부터 `bvec_iter_all`) |
| system/part14 | L620 / L1100 | 본문 L101·L105·L1045는 "링 공유 ≠ payload zero-copy", "등록 버퍼가 무조건 무복사는 아니다"라고 못 박는데, 기능 비교표는 "제로카피 = 지원", 불릿은 "제로카피: 커널 버퍼가 불필요하다"로 단정 |
| system/part15 | L629-648 | `mq_notify` 시그널 핸들러에서 **`printf`** 호출(part07 §6이 금지) + `struct sigaction sa;`에 `sigemptyset(&sa.sa_mask)` 누락 |
| system/part15 | L1158-1164 | abstract socket `bind`에 `sizeof(addr)` 사용 — 이름에 107바이트 트레일링 NUL이 포함된다. `offsetof(sun_path) + 1 + strlen(name)`을 써야 함 |
| system/part15 | L1346-1363 | glibc `sem_wait` 발췌: 값은 하위 비트이므로 감소는 `- 1`(시프트 아님)이고, glibc는 CAS로 **감소 전에** 0을 검사한다 |
| system/part15 | L2062/L2064, L2117 | `uint64_t`에 `%lu`, `off_t`에 `%ld` — LP64에서만 맞음(`PRIu64` 권장) |
| system/part15 | L2327 | `Documentation/filesystems/tmpfs.**txt**` — 문서가 기준으로 삼은 v5.10(L2320)에서는 이미 **`tmpfs.rst`**(5.8에서 reST 전환) |
| system/part17 | L333 | "50,000개 연결 × 2개 4KB buffer … 약 **390.6MB**" — 50,000×2×4096 = 409,600,000 B = **409.6 MB = 390.6 MiB**. 값은 맞고 단위만 MB/MiB 혼용 |
| system/part18 | L316, L121/L993, L1002 | 스스로 "Linux 5.15 기준"(L316 부근)이라 하면서 **`ioremap_nocache`**(5.6에서 제거), `poll` 반환형 `unsigned int`(→`__poll_t`), `POLLIN`/`POLLOUT`(→`EPOLLIN`/`EPOLLOUT`) 사용 |
| system/part18 | L602-603 | `writel(dma_handle >> 32, ...)` — `dma_addr_t`가 32비트인 빌드에서 **폭 이상 시프트(UB)**. `upper_32_bits()`를 써야 함 |
| system/part18 | L72 / L1157 | "문자 디바이스는 순차 접근만"(실제로는 `llseek` 구현 가능), "단 6줄의 코드"(바로 위 예제는 그보다 김) |
| system/part19 | L562-563 | "처리량 **100Kpps → 수 Mpps**" — 같은 글 L463-465가 "line rate는 packet size·batching·offload·host CPU 예산에 달렸다"고 못 박은 것과 어긋나는 단정 수치 |
| system/part20 | L694-713 | `ovl_copy_up_one` 예시가 read/write에 **같은 `pos` 변수**를 사용 — 그대로면 데이터가 어긋난다(실제 커널은 별도 오프셋) |
| system/part20 | L534-545 | `oom_kill_process()` 안에서 희생자를 고르는 것처럼 그렸으나, 실제로는 **`select_bad_process()`/`oom_evaluate_task()`가 고르고** `oom_kill_process()`는 정해진 희생자를 받는다 |
| system/part20 | L600-607 | "Cgroup 자체는 네트워크 제어가 없다" — cgroup **v1에는 net_cls/net_prio 컨트롤러가 있다**(대역폭 제한은 아니고 분류/우선순위). 문서가 v1·v2를 모두 다루므로 단정이 과함 |
| system/part20 | L730-738 | "각 명령어가 새 레이어 생성" — 레이어를 만드는 것은 `RUN`/`COPY`/`ADD`뿐이고 `ENV`/`CMD`/`LABEL`은 메타데이터만 바꾼다. `FROM`은 베이스 레이어를 가져올 뿐 "Layer 1"을 새로 만들지 않는다 |
| system/part20 | L753-759 | devicemapper storage driver는 **Docker Engine v25에서 제거**됨(§1-19의 CoW 오기와 별개로 최신성도 이탈) |
| system/part20 | L1107-1110 | `clone()` **뒤에** `setup_cgroup()`을 호출 — 자식이 cgroup에 들어가기 전에 `execve`할 수 있는 경합 |
| system/part21 | L744-751 | libseccomp 예시가 `write`를 **무조건 ALLOW**로 먼저 넣은 뒤 조건부 ALLOW를 추가 — 앞 규칙이 전부 허용해 "stdout만 허용"이라는 주석과 실제 효과가 다름 |
| system/part21 | L803-807 | Docker seccomp `"value": 2114060288` 주석이 "CLONE_NEWUSER 제외" — 0x7E020000은 **CLONE_NEWNS/NEWCGROUP/NEWUTS/NEWIPC/NEWUSER/NEWPID/NEWNET 전부**를 마스킹해 0을 요구한다 |
| system/part21 | L1165-1168 | `podman run --userns=auto \` 다음 줄이 **백슬래시 뒤에 공백+주석** — 줄 연결이 깨져 그대로 붙여넣으면 실행 불가. 게다가 `--userns=auto`와 별도 `--uidmap`은 배타적 |
| system/part21 | L771-772, L1249 | `SECCOMP_RET_ERRNO`의 `?: EPERM` 폴백은 커널에 없음(data를 MAX_ERRNO로 clamp해 그대로 사용) / "`security/capability.c` - Capability system"은 **`security/commoncap.c`** + **`kernel/capability.c`**여야 함(문서 자신이 L890에서 후자로 표기) |
| system/part22 | L180-192 | "함수 sample 수 **X**는 Binomial(n,p)이고 **표준 오차는 √(p(1−p)/n)**" — 이 식은 **비율 추정량 p̂ = X/n의 표준오차**이고 개수 X의 표준편차는 √(np(1−p)). 식은 맞고 붙는 대상이 어긋남 |

---

## 7. 구조 결함 — "N절" 교차참조 깨짐 (총 26곳)

여러 편의 글을 한 파일로 병합하면서 **원본 글의 절 번호가 그대로 남았다.** `system/`은 이 문제가 **0건**이고 `network/`에만 집중되어 있다.

| 문서 | 개수 | 사례 |
|---|---:|---|
| part01_internetworking | 4 | L145 "traceroute(**8절**)" → **15절**, L321 "symmetric(**3절**)" → **18절**, L340 "NAT64(**7절**)" → **22절**, L374 "**3절** 분류" → **18절** |
| part02_transport | 3 | L52 "TCP 스트림(**6절**)" → **3절**, L162 "HoL(**7절**)" → **14절**, L261 "**4절**의 윈도/BDP" → **11절** |
| part05_services_security | 6 | L70 "자체 API(**4절**)" → **6절**, L131 "재시도 분기(**6절**)" → **15절**, L166 "연결 재사용(**2~3절**)" → **9절**, L167 "멱등성(**6절**)" → **13/15절**, L196 "백오프(**6절**)" → **15절**, L197 "GET 캐시(**1절**)" → **8절** |
| part06_game_transport_protocol | 2 | L330 "무선 손실(**6절**)" → **15절**, L333 "정책(**4절**)" → **13절** |
| part08_authority_prediction | **13** | L284 "**4절**"→**12절**, L383 "**8절**"→**16절**, L400 "**7절**"→**15절**, L501 "**7절**"→**23절**, L603·L620 "**6절**"→**22절**, L710 "**5절**"→**31절**, L716·L724 "**4절**"→**28절**, L724 "**7절**"→**31절**, L766 "**5절**"→**29절**, L967 "**7절**"→**39절**, L967 "**8절**"→**40절** |
| part09_state_replication | 1 | L438 "**4절**의 dormancy" → **13절** |
| part15_anticheat_trust | 1 | L602 "**§5**에서 정규분포로 계산한 임계" → **§4** |

---

## 8. 서지 중복 (같은 항목이 한 목록에 2~4회)

| 문서 | 중복 항목 |
|---|---|
| part08_authority_prediction | Bernier GDC 2001 **3회**, Overwatch(Ford GDC 2017) **4회**, Gambetta 2회, Valve Source Networking 2회, Halo: Reach 2회 — 39항목 중 11항목 |
| part09_state_replication | TRIBES Networking Model **3회**, Halo: Reach 2회, Fiedler *Snapshot Compression* 2회 |
| part01_internetworking | Riot "Fixing the Internet…" 2회, Kurose & Ross 3항목 |
| part06_game_transport_protocol | Valve GameNetworkingSockets 2회 |

---

## 9. 권고 (우선순위 순)

0. **먼저 §1-21, §1-22, §1-18을 고친다.** 이 셋은 독자가 잘못된 보안 모델을 갖게 만드는 유형이다. "SELinux와 AppArmor를 같이 켤 수 있다"(1-21)는 실제 환경에서 시도하면 반드시 실패하고, `capable()`이 `current_user_ns()`를 본다는 서술(1-22)은 컨테이너 격리가 왜 성립하는지를 정반대로 가르치며, nsproxy에 user_ns가 있다는 서술(1-18)은 `unshare(CLONE_NEWUSER)`의 동작을 오해하게 만든다.
1. **§2의 산술 오류 3건**과 **§1-1 비트 번호**를 고친다. 독자가 그대로 따라 계산하면 틀린 결과가 나오는 유형이다.
2. **§3의 코드 11건**. 특히 3-1은 롤백 넷코드의 핵심 최적화가 동작하지 않는 코드이고, 3-4·3-5·3-9·3-10은 잘못 따라 쓰면 실제 버그가 되거나 아예 빌드되지 않는 패턴이다.
3. **§5의 개정 잔재 2건**은 문장이 가리키는 대상이 아예 없으므로 독자가 혼란을 겪는다. 같은 유형이 두 파일에서 나왔으므로 개정 시 "앞 문단을 지우고 받는 문단만 남기지 않았는지" 점검 절차를 권한다.
4. **§7의 교차참조 26곳**. 자동화가 가능하다 — 각 파일의 H2 번호를 뽑아 본문의 `N절` 표현과 대조하는 스크립트로 일괄 검출·수정할 수 있다. `system/`은 이미 앵커링크 방식으로 이 문제가 없으므로, `network/`도 앵커링크로 통일하는 편이 재발을 막는다.
5. **§8의 서지 중복**은 병합 흔적이다. part08은 39항목 중 11항목이 중복이라 목록의 신뢰도를 떨어뜨린다.
6. **커널 코드 발췌의 파일 경로를 일괄 점검한다.** 마지막 7편에서만 `struct nsproxy`(→`include/linux/nsproxy.h`), `struct mnt_namespace`(→`fs/mount.h`), `execve`(→`fs/exec.c`), `security/capability.c`(→`commoncap.c`) 네 건이 나왔다. 코드 자체는 맞는데 경로만 틀린 유형이라 독자가 원본을 찾으러 갔다가 헤맨다.
7. **`system/part17` · `part18` · `part22`는 사실상 손댈 것이 없다.** 세 편 모두 확정 오류 0건이며, 특히 part22는 수치를 단정하지 않고 "무엇을 함께 기록해야 해석이 가능한지"로 서술해 이 시리즈에서 가장 견고하다. 다른 편을 고칠 때 이 세 편의 서술 방식을 기준으로 삼는 것을 권한다.

---

## 10. 검증 통과한 것들 (참고)

본문의 정량 서술은 전반적으로 매우 정확했다. 재계산으로 **일치**를 확인한 대표 항목:

- **network/part01** — 캡슐화 14+20+8+12+28+4=86B, preamble+IFG 포함 106B, UDP 예산 v4 1472 / v6 1452, `f=8⌊(M−20)/8⌋`=1480, D=3000→(1480,1480,40), offset 상한 65528B, `L_frag=1−(1−p)ⁿ`=2.97%, RFC 8200 §5 → 1232 → 1200, IPv4 ID 랩 6.55s / 65.5ms
- **network/part02** — RFC 6298 SRTT/RTTVAR 갱신 순서(RTTVAR가 갱신 전 SRTT 사용), Jain 지수 `F=1/(1+CV²)`, Chiu-Jain 척도불변 논증
- **network/part03** — RFC 9002 rttvar/smoothedRTT 계수, `PTO = smoothedRTT + max(4·rttvar, kGranularity) + maxAckDelay`, 3× anti-amplification
- **network/part06** — `IsMoreRecent`의 half-range 계산(uint8/16/32 모두), `WasAcked` diff 1..32 ↔ 비트 0..31, shift==32 경계, uint16 seq @60Hz = 18분, `Q_peak=S(1−C/L)`, Kleinrock ICC 1979
- **network/part08** — Cristian 1989 편향 상한 `RTT/2 − d_min`, EWMA 분산 `α/(2−α)σ²`, 200ppm → 12ms/min, 슬루 `t=Δ/r`, `ε_x ≈ v·ε_t`
- **network/part09** — 양자화 비트식 `b ≥ ⌈log₂(W/2ε+1)⌉`, smallest-three 범위 `[−1/√2, 1/√2]`, 200MB/s → 8MB/s, IPv4+UDP 28B / IPv6+UDP 48B
- **network/part12** — Gilbert 모델 `π_B = p/(p+r)`, IEEE754 비결합 반례에서 **10²⁰의 ULP = 2¹⁴**
- **network/part13** — smallest-three 각오차 유도 전 단계(Huynh 2009 측지거리 → `Δφ≈2‖δq‖` → `w≥½` → **2√3Δ = 0.274°**), `ceil(bits/8)` 경고
- **network/part14** — RFC 3550 §6.4.1 지터 EWMA 무게중심 15, `J ≈ 2σ/√π ≈ 1.13σ`, Cantelli `1/(1+k²)`, 100ppm → 20ms 버퍼 200초
- **network/part15** — 베이즈 오즈형 PPV=0.835, `Λ_req≈10⁸`, `α_req≈5.1×10⁻⁹`, SPRT 경계
- **system/part02** — CLONE_* 22개 플래그 값, `/proc/[pid]/stat` fscanf 필드 오프셋(utime=14, stime=15, rss=24), PDP-7 fork 27줄
- **system/part03** — 커널 주소 레이아웃이 `Documentation/x86/x86_64/mm.rst`와 **전수 일치**, PTE 비트 P0/RW1/US2/PWT3/PCD4/A5/D6/PS7/G8/NX63, PCID=Westmere·INVPCID=Haswell·Linux 4.14 정정
- **system/part05** — MSR 번호 전수, syscall/sysret 시맨틱, 32비트 syscall 번호 대조표, `struct pt_regs` 필드 순서와 `POP_REGS` 전개
- **system/part06** — 예외 벡터 표 18행, IDT 16바이트와 `gate_struct` 비트필드, IDTR limit 4095, softirq 10종
- **system/part08** — `sched_prio_to_weight[40]` **40개 값 전수 일치**, PELT half-life 32.768ms, CFS/EEVDF/SCHED_DEADLINE/EAS/NUMA balancing 연도 전수
- **system/part09** — rwsem `count` 비트 레이아웃, Drepper futex lock/unlock 로직
- **system/part17** — IOCP의 completion key vs `OVERLAPPED*` 구분, `GetQueuedCompletionStatus` 반환값·`WSAGetLastError` 처리, worker 동시성 값의 의미
- **system/part18** — `/dev/null` 1,3 / `/dev/ttyS0` 4,64, `MINORBITS 20` → major 4,096 · minor 1,048,576, PC 스피커 PIT 시퀀스(`divisor = 1193180/frequency`, `outb(0xb6, 0x43)`, low→high를 0x42로)
- **system/part19** — **VM exit reason 12개 값 전수 일치**(0/1/2/10/12/14/18/28/30/31/32/48), **게스트 기계어 바이트 전수 일치**(`ba f8 03`=mov dx,0x3f8 / `b0 48`='H' / `ee`=out dx,al / `b0 69`='i' / `f4`=hlt), `rflags = 0x2`, VT-x 2005 / AMD-V 2006 / Xen SOSP 2003, VMPTRLD·VMREAD·VMWRITE·VMCLEAR
- **system/part20** — namespace 8종 목록, Docker 기본 capability 14개, cgroup v1 `cpu.cfs_quota_us`/`cfs_period_us` 50%와 v2 `cpu.max "50000 100000"`, `--cpu-shares` 기본 1024, `io.max`의 `rbps=/wbps=`, **`io.weight` 기본 100·범위 1-10000**, `memory.max`가 `memparse()`로 "512M" 수용, overlayfs **whiteout = character device 0:0**, `write(fd,"536870912",9)`의 길이 9와 512MiB
- **system/part21** — **capability 상수값 전수 일치**(CHOWN 0 / DAC_OVERRIDE 1 / FOWNER 3 / KILL 5 / SETUID 7 / NET_BIND_SERVICE 10 / NET_RAW 13 / SYS_ADMIN 21 / SYS_TIME 25), `CapBnd: 000001ffffffffff` = 41비트 = CAP_LAST_CAP 40, **Docker 기본 `CapEff: 00000000a80425fb` 디코딩 결과가 정확히 14개**(비트 0,1,3,4,5,6,7,8,10,13,18,27,29,31), **seccomp-BPF 필터 점프 오프셋 전수 검증 통과**, `setgroups`에 "deny"를 gid_map보다 먼저, **SMEP 위반 시 #PF error_code=0x11**(P=1+I/D=1), **CR4.SMEP=bit20 / CR4.SMAP=bit21**, **arm64 PTE PXN=bit53 / UXN=bit54**, **CET 불일치 시 #CP = 벡터 21**, Linux 6.6 + `CONFIG_X86_SHADOW_STACK`, `440 − 3 = 437`
- **system/part22** — **f=500,000/s × c=200ns = 10% CPU**, eBPF 레지스터 규약(R0 반환 / R1-R5 인자 / R6-R9 callee-saved / R10 읽기전용 FP), Generic KASAN shadow scale 8:1, dynamic_debug 플래그 `+p`/`+pflmt`, tracepoint 이름 5종 전부 실재, crash 명령 5종 전부 유효, `struct IoEvent` 8+8+4+4+4+2+2 = **32바이트 패딩 없음**
- **system/part15(§11-13)** — POSIX IPC 경로 분리(공유메모리·세마포어 `/dev/shm/`, 메시지 큐 `/dev/mqueue/`), `"Hello, World!"` = **13자** → `Content-Length: 13`, `sendfile(out_fd, in_fd, offset, count)` 인자 순서, **`ipcrm -m <shmid>` vs `-M <key>`**, `/proc/<pid>/maps`의 `rw-s`, "blocking `accept()`는 커널이 exclusive wakeup으로 한 프로세스만 깨운다"

서지 검증도 대부분 정확했다 — Kleinrock ICC 1979, Cristian *Distributed Computing* 3(1989), Huynh *JMIV* 35(2) 2009, Gilbert BSTJ 1960, Wald *Ann. Math. Statist.* 16(2) 1945 / Wald & Wolfowitz 19(3) 1948, Van Trees(1968), Shreedhar & Varghese *IEEE/ACM ToN* 1996, Boulanger NetGames 2006, Bonwick USENIX 1994, Denning CSUR 1970 / CACM 1968, Liu & Layland JACM 1973, NIST SP 800-38D §8.2~8.3의 무작위 IV 2³² 한도, US-CERT TA14-017A, RFC 9605(SFrame)·9143(BUNDLE) 등.

마지막 7편의 서지도 대부분 정확했다 — Neiger et al. *Intel Technology Journal* 2006, Barham et al. SOSP 2003, Russell "virtio…" ACM SIGOPS OSR 2008, Smith & Nair(Morgan Kaufmann, 2005), Kerrisk *TLPI*(No Starch, 2010), Liz Rice *Container Security*(O'Reilly, 2020), Lukša *Kubernetes in Action*(Manning, 2017), LWN "Namespaces in operation" 531114 / part5 532593, Smalley et al.(2001) NAI Labs, Wright et al. USENIX Security 2002, Intel CET Spec 2016, McCanne & Jacobson "The BSD Packet Filter"(USENIX Winter 1993), Høiland-Jørgensen et al. "The eXpress Data Path"(CoNEXT 2018, DOI 10.1145/3281411.3281443), Stevens *UNP Vol.2* 2nd ed, Bach 5장·11장, Ritchie & Thompson CACM 1974.

또한 **오래된 통념을 정정하는 서술이 다수** 확인됐다 — "bz는 bzip2가 아니라 big zImage", "PCID는 Haswell이 아니라 Westmere부터", "vfork는 COW의 한계 때문이 아니라 COW 이전에 나왔다", "BUNDLE은 RTP aggregation이 아니다", "lockstep도 결정론 요구를 없애지 않는다", "tasklet은 신규 코드에 권장되지 않는다", "Ring −1은 비유일 뿐 CPL이 −1인 게 아니다", "EEVDF가 CFS를 삭제한 게 아니라 fair class의 선택기가 바뀐 것", "mdev는 모집단 표준편차이지 게임의 지터 정의와 같지 않다" 등.

마지막 7편에서도 같은 성격의 정정이 계속된다 — **"게스트 OS가 Ring 0인 줄 알면서 사실은 Ring 3"이 아니라 VMX non-root의 진짜 Ring 0에서 돈다**(part19, 가장 흔한 오해 중 하나), **"KVM = Kernel + QEMU"가 아니라 KVM은 커널 가상화 API이고 QEMU는 그 위의 VMM 중 하나**(cloud-hypervisor·Firecracker·crosvm 병기), **VMCS를 "C 구조체 한 페이지"로 추측하지 말 것**, **nested exit 비용을 고정 "3단 EPT walk"로 환산하지 말 것**(part19), **`STAC`이 AC=1로 SMAP을 해제하고 `CLAC`이 AC=0으로 복원한다**(part21 — 흔히 반대로 서술되는 대목을 정확히 잡음), **"eBPF에는 loop가 없다"는 옛 제약이고 현대 verifier는 bounded loop를 허용**, **verifier 통과가 보장하지 않는 6가지**(part22), **예측 불가능한 이름은 보안 경계가 아니라 추측 비용을 올리는 보조 수단일 뿐**, **`unlink` 후에도 이미 열린 핸들은 유효**(part15).
