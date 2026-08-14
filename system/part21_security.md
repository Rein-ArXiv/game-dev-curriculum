## 들어가며

> **핵심 질문**: "리눅스는 어떻게 권한을 제어하는가?"


보안은 편의성과의 영원한 트레이드오프다. 리눅스는 1970년대 UNIX의 단순한 사용자/그룹/기타 권한 모델에서 출발해, 다층 방어(defense-in-depth) 체계로 진화했다.

이 글은 리눅스가 악의적 코드와 실수로부터 시스템을 보호하는 메커니즘을 다룬다. DAC의 한계, Capabilities로 root를 쪼개는 방법, LSM 위에 구현된 SELinux와 AppArmor의 MAC, Seccomp 시스템 콜 필터링, namespace 격리, 그리고 KASLR/SMAP/SMEP/CET 같은 하드웨어 보안 기능까지 각 계층이 어떻게 협력하는지 살펴본다.


---

## 목차
1. [DAC: Discretionary Access Control](#1-dac-discretionary-access-control)
2. [Capabilities: root 권한 분할](#2-capabilities-root-권한-분할)
3. [LSM: Linux Security Modules](#3-lsm-linux-security-modules)
4. [SELinux: Security-Enhanced Linux](#4-selinux-security-enhanced-linux)
5. [AppArmor: 경로 기반 MAC](#5-apparmor-경로-기반-mac)
6. [Seccomp: 시스템 콜 필터링](#6-seccomp-시스템-콜-필터링)
7. [Namespaces와 권한 격리](#7-namespaces와-권한-격리)
8. [하드웨어 보안 기능](#8-하드웨어-보안-기능)
9. [실전 보안 강화](#9-실전-보안-강화)


---

## 1. DAC: Discretionary Access Control

### 전통적 UNIX 권한

**파일 권한 비트:**
```bash
$ ls -l /bin/bash
-rwxr-xr-x 1 root root 1183448 Feb  18  2024 /bin/bash
 ││││││││
 │││││││└─ other: execute
 ││││││└── other: read
 │││││└─── other: write (없음)
 ││││└──── group: execute
 │││└───── group: read
 ││└────── group: write (없음)
 │└─────── owner: execute
 └──────── owner: read/write
```

**3개의 ID로 판단:**
```c
// fs/namei.c
static int acl_permission_check(struct inode *inode, int mask) {
    unsigned int mode = inode->i_mode;
    kuid_t uid = current_fsuid();  // 현재 프로세스의 UID

    if (uid_eq(uid, inode->i_uid))         // owner?
        mode >>= 6;
    else if (in_group_p(inode->i_gid))     // group?
        mode >>= 3;
    // else: other 권한 사용

    if ((mask & ~mode & (MAY_READ|MAY_WRITE|MAY_EXEC)) == 0)
        return 0;  // 허용

    return -EACCES;
}
```

### 특수 비트: setuid, setgid, sticky

**setuid (Set User ID):**
```bash
$ ls -l /usr/bin/passwd
-rwsr-xr-x 1 root root 68208 Feb  2  2024 /usr/bin/passwd
   ^
   └─ 's' = setuid bit
```

**작동 원리:**
```c
// fs/exec.c의 bprm_fill_uid()를 단순화한 개념 코드
static void apply_setid_bits(struct linux_binprm *bprm) {
    struct inode *inode = file_inode(bprm->file);
    umode_t mode = inode->i_mode;

    // 실제 커널은 nosuid mount, idmapped mount, ptrace 등도 검사한다.
    if (mode & S_ISUID)
        bprm->cred->euid = inode->i_uid;
}
```

**실제 동작:**
```bash
# 일반 사용자가 passwd 실행
$ id
uid=1000(user) gid=1000(user)

$ /usr/bin/passwd
# 프로세스의 euid=0 (root)로 실행됨
# → /etc/shadow 수정 가능

# 하지만 실제 uid는 여전히 1000
# → 다른 사용자 파일 접근 불가
```

**setgid + sticky bit:**
```bash
# setgid on directory: 생성된 파일이 디렉터리 그룹 상속
$ chmod g+s /shared
$ ls -ld /shared
drwxrwsr-x 2 root staff 4096 Dec 24 10:00 /shared
      ^
      └─ 's' = setgid

# sticky bit: 소유자만 삭제 가능
$ chmod +t /tmp
$ ls -ld /tmp
drwxrwxrwt 20 root root 4096 Dec 24 10:00 /tmp
        ^
        └─ 't' = sticky bit
```

### DAC의 근본적 한계

**문제 1: root의 절대 권력**
```c
// fs/namei.c
if (capable(CAP_DAC_OVERRIDE))  // root는 이 권한 보유
    return 0;  // 모든 권한 검사 우회
```

**문제 2: 소유자의 재량**
```bash
# 사용자가 자신의 파일을 누구나 읽을 수 있게 설정
$ chmod 777 ~/sensitive_data.txt
# → DAC는 막을 방법이 없음
```

**문제 3: 프로그램별 제어 불가**
```text
nginx와 apache가 같은 UID로 실행되면
서로의 설정 파일을 읽을 수 있다
→ 프로세스 단위 격리 불가능
```

## 2. Capabilities: root 권한 분할

### 문제 인식

**전통적 이분법:**
```text
UID == 0  →  모든 권한
UID != 0  →  제한된 권한
```

**실제 필요:**
```text
프로그램 A: 네트워크 바인딩만 필요 (포트 80)
프로그램 B: 시간 설정만 필요
→ 굳이 root로 실행할 필요 없음
```

### Capability 목록

```c
// include/uapi/linux/capability.h
#define CAP_CHOWN            0   // 파일 소유자 변경
#define CAP_DAC_OVERRIDE     1   // DAC 권한 우회
#define CAP_FOWNER           3   // 파일 권한 무시
#define CAP_KILL             5   // 시그널 전송
#define CAP_SETUID           7   // UID 변경
#define CAP_NET_BIND_SERVICE 10  // 1024 이하 포트 바인딩
#define CAP_NET_RAW          13  // RAW 소켓 생성
#define CAP_SYS_TIME         25  // 시스템 시간 설정
#define CAP_SYS_ADMIN        21  // 거의 모든 관리 작업
// ... 총 40+개
```

### 프로세스 Capability 구조

```c
// include/linux/cred.h
struct cred {
    kuid_t uid;         // 실제 UID
    kuid_t euid;        // 유효 UID

    kernel_cap_t cap_effective;   // 현재 사용 가능
    kernel_cap_t cap_permitted;   // 가질 수 있는 최대
    kernel_cap_t cap_inheritable; // execve 시 상속 가능
    kernel_cap_t cap_bset;        // bounding set (상한선)
    kernel_cap_t cap_ambient;     // 조건을 만족한 비특권 execve에서 보존 가능한 집합
};
```

**Capability 계산 공식:**
```text
P'(effective)   = F(effective) ? P'(permitted) : P'(ambient)
P'(permitted)   = (P(inheritable) & F(inheritable)) |
                  (F(permitted) & P(bset)) | P'(ambient)
P'(inheritable) = P(inheritable)
P'(ambient)     = (file is privileged) ? 0 : P(ambient)

P  = 실행 전 프로세스
P' = 실행 후 프로세스
F  = 실행 파일
```

### 실제 사용 예시

**파일에 Capability 부여:**
```bash
# nginx에 포트 80 바인딩 권한만 부여
$ sudo setcap cap_net_bind_service=+ep /usr/sbin/nginx

# 확인
$ getcap /usr/sbin/nginx
/usr/sbin/nginx = cap_net_bind_service+ep

# 이제 일반 유저로 실행 가능
$ /usr/sbin/nginx  # UID=1000이지만 포트 80 바인딩 가능
```

**프로세스 Capability 확인:**
```bash
$ cat /proc/self/status | grep Cap
CapInh: 0000000000000000  # inheritable
CapPrm: 0000000000000000  # permitted
CapEff: 0000000000000000  # effective
CapBnd: 000001ffffffffff  # bounding set
CapAmb: 0000000000000000  # ambient

# 초기 user namespace에서 root(uid=0)로 실행되는 단순한 프로세스
CapEff: 000001ffffffffff  # 모든 비트 1
```

이 "root는 모든 capability 비트가 1"이라는 모델은 **초기(init) user namespace 안에서, bounding set과 securebits 같은 제한이 걸리지 않은 단순한 root 프로세스**에 한해 성립한다. 다음 경우에는 root여도 effective capability가 모든 비트가 켜진 상태가 아니다.

- 컨테이너 안의 root: 컨테이너 런타임이 보통 `CAP_SYS_ADMIN`, `CAP_NET_ADMIN`, `CAP_SYS_MODULE` 같은 위험한 capability를 bounding set에서 제거하고 시작한다. 결과적으로 컨테이너 안의 root는 host root와 같은 권한이 아니다.
- user namespace 안의 root(rootless): user namespace 내부에서는 "root"이지만, 그 user namespace 밖의 자원에 대해서는 capability가 자동으로 권한을 주지 않는다.
- securebits/file capability/ambient capability/no_new_privs: 이들 메커니즘이 켜지면 exec 경계에서 capability가 손실되거나 추가될 수 있다.

따라서 보안 모델을 설명할 때는 "초기 root 모델에서는 그렇다"라는 전제를 명시하는 편이 정확하다.

**프로그래밍 인터페이스:**
```c
#include <sys/capability.h>

// Capability 획득 시도
cap_t caps = cap_get_proc();

// CAP_NET_BIND_SERVICE 확인
cap_flag_value_t value;
cap_get_flag(caps, CAP_NET_BIND_SERVICE, CAP_EFFECTIVE, &value);

if (value == CAP_SET) {
    // 1024 이하 포트 바인딩 가능
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
}
```

### Capability 제한 (Bounding Set)

**자식 프로세스 제한:**
```c
#include <sys/prctl.h>

// CAP_SYS_ADMIN을 bounding set에서 제거
// → 이후 execve로 실행되는 모든 프로그램에서 사용 불가
prctl(PR_CAPBSET_DROP, CAP_SYS_ADMIN);

// 이제 root로 실행해도 CAP_SYS_ADMIN 없음
execve("/bin/bash", ...);
```

**컨테이너 격리:**
```bash
# Docker는 기본적으로 14개 capability만 부여
$ docker run --rm -it alpine sh -c 'cat /proc/1/status | grep CapEff'
CapEff: 00000000a80425fb
# CAP_SYS_ADMIN, CAP_SYS_MODULE 등은 제거됨
```

## 3. LSM: Linux Security Modules

### 설계 철학

**문제:**
```text
NSA는 SELinux를 원함
SUSE는 AppArmor를 원함
→ 커널에 둘 다 하드코딩?
```

**해결책: 플러그인 아키텍처**
```c
// include/linux/lsm_hooks.h
struct security_hook_list {
    struct hlist_node       list;
    struct hlist_head       *head;
    union security_list_options hook;
    char                    *lsm;
};

// 각 시스템 콜 지점에 훅 설치
security_hook_heads {
    struct hlist_head inode_permission;
    struct hlist_head file_open;
    struct hlist_head task_kill;
    // ... 200+ 훅
};
```

### LSM Hook 설치

**SELinux 등록 예시:**
```c
// security/selinux/hooks.c
static struct security_hook_list selinux_hooks[] = {
    LSM_HOOK_INIT(inode_permission, selinux_inode_permission),
    LSM_HOOK_INIT(file_open, selinux_file_open),
    LSM_HOOK_INIT(task_kill, selinux_task_kill),
    // ...
};

static __init int selinux_init(void) {
    security_add_hooks(selinux_hooks, ARRAY_SIZE(selinux_hooks), "selinux");
    return 0;
}
```

**Hook 호출 지점:**
```c
// fs/namei.c
int inode_permission(struct inode *inode, int mask) {
    int retval;

    // 1. DAC 검사
    retval = do_inode_permission(inode, mask);
    if (retval)
        return retval;

    // 2. LSM 훅 호출 (SELinux/AppArmor 등)
    return security_inode_permission(inode, mask);
}

// security/security.c
int security_inode_permission(struct inode *inode, int mask) {
    // 등록된 모든 LSM 순회
    hlist_for_each_entry(hp, &security_hook_heads.inode_permission, list) {
        rc = hp->hook.inode_permission(inode, mask);
        if (rc != 0)
            return rc;  // 하나라도 거부하면 차단
    }
    return 0;
}
```

### 주요 LSM 모듈

**활성화된 LSM 확인:**
```bash
$ cat /sys/kernel/security/lsm
lockdown,capability,landlock,yama,apparmor

# 또는
$ cat /proc/cmdline | grep security
security=apparmor
```

**복수 LSM 동시 사용:**
```text
현대 mainline Linux의 LSM stacking:
- capability와 여러 minor LSM(Yama, Landlock, LoadPin 등)은 함께 활성화 가능
- SELinux, AppArmor, Smack, TOMOYO 같은 major LSM은 동시에 최대 하나
- 따라서 SELinux + AppArmor의 full stacking은 현재 일반 계약이 아님
```

활성화된 훅의 판정은 누적되므로 어느 LSM 하나가 거부하면 접근은 실패한다. 다만 이를 근거로 두 major LSM을 동시에 켤 수 있다고 추론하면 안 된다. 커널 공식 문서도 capability, 여러 minor module, 최대 한 major module로 설명한다.

## 4. SELinux: Security-Enhanced Linux

### MAC (Mandatory Access Control)

**DAC와의 차이:**
```text
DAC (소유자 재량):
  소유자가 chmod 777 하면 누구나 접근 가능

MAC (강제 접근 통제):
  관리자가 정의한 정책에 따라 결정
  소유자도 마음대로 변경 불가
```

### Type Enforcement (TE)

**핵심 개념: 모든 객체에 레이블 부여**
```bash
$ ls -Z /etc/passwd
-rw-r--r--. root root system_u:object_r:passwd_file_t:s0 /etc/passwd
                         └─────────┬─────────┘
                                   └─ SELinux context
```

**Context 구조:**
```text
user:role:type:level
  │    │    │     └─ MLS level (군사용)
  │    │    └─ type (가장 중요)
  │    └─ role (RBAC)
  └─ SELinux user
```

**정책 예시:**
```c
// policy/modules/system/userdomain.te (의사 코드)

// httpd 프로세스는 httpd_t 타입
type httpd_t;

// httpd_t는 httpd_sys_content_t 파일을 읽을 수 있다
allow httpd_t httpd_sys_content_t:file { read open };

// 하지만 passwd_file_t는 읽을 수 없다
// (명시적 allow가 없으면 거부)
```

### Domain Transition

**문제:**
```bash
# 일반 유저가 passwd 실행 시
$ id -Z
unconfined_u:unconfined_r:unconfined_t:s0

# passwd 바이너리는
$ ls -Z /usr/bin/passwd
system_u:object_r:passwd_exec_t:s0
```

**자동 전환 규칙:**
```c
// policy/modules/system/userdomain.te

// unconfined_t에서 passwd_exec_t 실행 시
// → passwd_t 도메인으로 전환
domain_auto_trans(unconfined_t, passwd_exec_t, passwd_t)

// passwd_t는 shadow_t 파일을 쓸 수 있다
allow passwd_t shadow_t:file { read write };
```

**실행 후:**
```bash
$ ps -eZ | grep passwd
unconfined_u:unconfined_r:passwd_t:s0 12345 pts/0 passwd
                          └─ 도메인 전환됨
```

### 커널 구현

```c
// security/selinux/hooks.c
static int selinux_inode_permission(struct inode *inode, int mask) {
    struct inode_security_struct *isec = inode->i_security;
    u32 sid = current_sid();  // 현재 프로세스의 Security ID
    u32 isid = isec->sid;     // inode의 Security ID

    // AVC (Access Vector Cache) 확인
    return avc_has_perm(sid, isid, SECCLASS_FILE,
                        mask_to_av(mask), NULL);
}

// security/selinux/avc.c
int avc_has_perm(u32 ssid, u32 tsid, u16 tclass, u32 requested) {
    // 1. 캐시 확인 (빠른 경로)
    node = avc_lookup(ssid, tsid, tclass);
    if (node && node->ae.avd.allowed & requested)
        return 0;  // 캐시 히트

    // 2. 정책 서버 질의 (느린 경로)
    rc = security_compute_av(ssid, tsid, tclass, requested, &avd);

    // 3. 캐시에 저장
    avc_insert(ssid, tsid, tclass, &avd);

    return (avd.allowed & requested) ? 0 : -EACCES;
}
```

### 실전 사용

**상태 확인:**
```bash
$ getenforce
Enforcing  # Permissive (로그만), Disabled

$ sestatus
SELinux status:                 enabled
Current mode:                   enforcing
Policy version:                 33
```

**문제 해결:**
```bash
# 1. 거부 로그 확인
$ ausearch -m avc -ts recent
type=AVC msg=audit(1703414400.123:456): avc: denied { read } for
  pid=1234 comm="nginx" name="index.html" scontext=system_u:system_r:httpd_t:s0
  tcontext=unconfined_u:object_r:user_home_t:s0 tclass=file

# 2. 파일 context 수정
$ chcon -t httpd_sys_content_t /var/www/html/index.html

# 3. 또는 정책 추가
$ ausearch -m avc -ts recent | audit2allow -M mypolicy
$ semodule -i mypolicy.pp

# 4. Boolean 토글 (미리 정의된 정책)
$ getsebool httpd_read_user_content
httpd_read_user_content --> off

$ setsebool -P httpd_read_user_content on  # -P: 영구 적용
```

## 5. AppArmor: 경로 기반 MAC

### SELinux와의 차이

| 특징 | SELinux | AppArmor |
|------|---------|----------|
| 식별자 | inode 레이블 | 파일 경로 |
| 정책 언어 | Type Enforcement (복잡) | 경로 규칙 (직관적) |
| 학습 곡선 | 가파름 | 완만함 |
| 유연성 | 매우 높음 | 중간 |
| 주 사용처 | RHEL, Fedora, Android | Ubuntu, SUSE |

### 프로필 예시

```bash
# /etc/apparmor.d/usr.bin.tcpdump
#include <tunables/global>

/usr/bin/tcpdump {
  #include <abstractions/base>
  #include <abstractions/nameservice>

  capability net_raw,           # Capability 부여
  capability setuid,

  network raw,                  # 네트워크 접근
  network packet,

  /usr/bin/tcpdump mr,          # 바이너리 읽기/mmap
  /etc/protocols r,             # 설정 파일 읽기

  /var/log/tcpdump/* w,         # 로그 쓰기 허용

  deny /home/** rw,             # 홈 디렉터리 접근 차단
  deny /root/** rw,

  /proc/*/net/dev r,            # proc 일부 읽기
}
```

### 커널 구현

```c
// security/apparmor/lsm.c
static int apparmor_file_permission(struct file *file, int mask) {
    struct aa_profile *profile = aa_current_profile();
    struct path_cond cond = {
        .uid = file_inode(file)->i_uid,
        .mode = file_inode(file)->i_mode
    };

    // 파일 경로 추출
    char *name = aa_get_buffer();
    int error = aa_path_name(&file->f_path, name);

    // 프로필과 비교
    return aa_file_perm(mask, profile, name, &cond);
}

// security/apparmor/file.c
int aa_file_perm(u32 request, struct aa_profile *profile,
                 const char *name, struct path_cond *cond) {
    // DFA (Deterministic Finite Automaton)로 경로 매칭
    struct aa_dfa *dfa = profile->file.dfa;
    unsigned int state = dfa_match(dfa, DFA_START, name);

    // 해당 상태의 허용 권한 확인
    u16 allowed = dfa_user_allow(dfa, state);

    if ((allowed & request) == request)
        return 0;

    return -EACCES;
}
```

### 프로필 생성 (학습 모드)

```bash
# 1. complain 모드로 시작 (로그만 남김)
$ aa-complain /usr/bin/firefox

# 2. 프로그램 정상 사용

# 3. 로그 분석해서 프로필 생성
$ aa-logprof
Reading log entries from /var/log/syslog.

Profile: /usr/bin/firefox
Execute: /usr/lib/firefox/firefox
Severity: unknown

(A)llow / [(D)eny] / (I)gnore / (G)lob / ...

# 4. enforce 모드로 전환
$ aa-enforce /usr/bin/firefox

# 5. 확인
$ aa-status
apparmor module is loaded.
44 profiles are loaded.
44 profiles are in enforce mode.
   /usr/bin/firefox
```

### 경로 기반의 한계

**심볼릭 링크:**
```bash
$ ln -s /etc/shadow /tmp/mylink

# AppArmor 프로필
/usr/bin/myapp {
  /tmp/** r,       # /tmp는 읽기 허용
  deny /etc/** r,  # /etc는 차단
}

# 하지만
$ myapp /tmp/mylink  # 성공? 실패?
# → 커널 버전에 따라 다름 (link_path_walk 처리)
```

**Rename 공격:**
```bash
# 허용된 경로
/var/www/html/safe.txt r,

# 공격자가
$ mv /sensitive/data.txt /var/www/html/safe.txt
# → 이름이 바뀌면 접근 가능해짐
```

**해결책:**
- AppArmor 2.x부터 `owner` 조건 추가
- `audit` 플래그로 의심스러운 접근 로깅

## 6. Seccomp: 시스템 콜 필터링

### 문제 인식

**거대한 공격 표면:**
```bash
$ ausyscall --dump | wc -l
440  # x86_64에서 440개 시스템 콜
```

**실제 필요:**
```c
// 간단한 계산 프로그램
int main() {
    int a, b;
    scanf("%d %d", &a, &b);
    printf("%d\n", a + b);
}

// 필요한 시스템 콜: read, write, exit (3개)
// → 나머지 437개는 공격에만 사용됨
```

### Seccomp-BPF

**Berkeley Packet Filter를 시스템 콜 필터링에 응용:**

x86_64는 같은 프로세스에서 64-bit syscall과 32-bit (`int 0x80`/`x32`) ABI를 모두 호출할 수 있어, syscall 번호가 ABI마다 다르다. 따라서 필터의 첫 단계는 항상 `seccomp_data.arch`를 검사해, 알지 못하는 ABI에서 호출되면 거부해야 한다. 그렇지 않으면 32-bit ABI로 같은 번호를 가진 다른 syscall을 호출해 우회할 수 있다.

또한 `PR_SET_NO_NEW_PRIVS`(NNP)는 root가 아닌 사용자가 seccomp 필터를 적용하기 위해 반드시 먼저 호출되어야 한다. NNP가 켜지면 setuid 비트로 권한이 올라가는 일이 차단되어 필터가 안전하게 동작한다.

```c
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>

// read, write, exit만 허용 (x86_64 ABI에서만)
struct sock_filter filter[] = {
    // 1. 아키텍처 검사: AUDIT_ARCH_X86_64 가 아니면 즉시 kill
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
             offsetof(struct seccomp_data, arch)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

    // 2. syscall 번호 로드
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
             offsetof(struct seccomp_data, nr)),

    // read
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_read, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

    // write
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_write, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

    // exit_group
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

    // 나머지는 모두 kill
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
};

struct sock_fprog prog = {
    .len = sizeof(filter) / sizeof(filter[0]),
    .filter = filter,
};

// 적용
// NNP가 켜지지 않으면 비특권 사용자는 PR_SET_SECCOMP가 EPERM으로 실패한다.
if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) perror("nnp");
if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) perror("seccomp");

// 이제 open() 호출 시 프로세스 즉시 종료
int fd = open("/etc/passwd", O_RDONLY);  // killed
```

### libseccomp 라이브러리

**더 간편한 인터페이스:**
```c
#include <seccomp.h>

scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);  // 기본 거부

// 허용할 시스템 콜 추가
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);

// 조건부 허용: write(1, ...) 만 허용 (stdout만)
seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1,
                 SCMP_A0(SCMP_CMP_EQ, 1));

seccomp_load(ctx);
```

### 커널 구현

```c
// kernel/seccomp.c
static long __seccomp_filter(int this_syscall, const struct seccomp_data *sd) {
    struct seccomp_filter *f = current->seccomp.filter;
    u32 action;

    // BPF 프로그램 실행
    action = bpf_prog_run_pin_on_cpu(f->prog, sd);

    switch (action & SECCOMP_RET_ACTION_FULL) {
    case SECCOMP_RET_ALLOW:
        return 0;  // 통과

    case SECCOMP_RET_ERRNO:
        return -min_t(int, action & SECCOMP_RET_DATA, MAX_ERRNO);

    case SECCOMP_RET_TRAP:
        // SIGSYS 시그널 전송
        force_sig_seccomp(this_syscall, action & SECCOMP_RET_DATA);
        return -1;

    case SECCOMP_RET_KILL_PROCESS:
        do_exit(SIGSYS);  // 프로세스 종료
    }
}
```

### 실전: Docker 보안

**Docker의 기본 seccomp 프로필:**
```json
{
  "defaultAction": "SCMP_ACT_ERRNO",
  "syscalls": [
    {
      "names": ["read", "write", "open", "close", ...],
      "action": "SCMP_ACT_ALLOW"
    },
    {
      "names": ["reboot", "swapon", "swapoff"],
      "action": "SCMP_ACT_ERRNO"
    },
    {
      "names": ["clone"],
      "action": "SCMP_ACT_ALLOW",
      "args": [
        {
          "index": 0,
          "value": 2114060288,  // 검사할 CLONE_NEW* namespace flag mask
          "valueTwo": 0,        // mask 안의 flag가 하나도 없을 때만 일치
          "op": "SCMP_CMP_MASKED_EQ"
        }
      ]
    }
  ]
}
```

**확인:**
```bash
$ docker run --rm -it alpine sh
/ # reboot
reboot: Operation not permitted  # seccomp이 차단

$ docker run --security-opt seccomp=unconfined alpine reboot
# 동작함 (위험!)
```

## 7. Namespaces와 권한 격리

### User Namespace

**목표: 네임스페이스 내부에서만 root**
```bash
# 일반 유저(UID 1000)가
$ unshare --user --map-root-user /bin/bash

# 네임스페이스 내부에서
$ id
uid=0(root) gid=0(root)  # 내부에서는 root!

$ cat /proc/self/uid_map
         0       1000          1
         ^         ^           ^
         └─ 내부  └─ 외부    └─ 개수

# 하지만 실제 파일 시스템에서는
$ ls -l /etc/shadow
-rw-r----- 1 nobody nogroup 1234 Dec 24 10:00 /etc/shadow
# → 여전히 접근 불가 (외부 UID 1000으로 매핑됨)
```

### UID 매핑 구조

```c
// kernel/user_namespace.c
struct uid_gid_map {
    u32 nr_extents;  // 매핑 개수
    struct uid_gid_extent {
        u32 first;       // 네임스페이스 내부 시작
        u32 lower_first; // 외부 UID 시작
        u32 count;       // 개수
    } extent[UID_GID_MAP_MAX_EXTENTS];
};

// UID 변환
kuid_t make_kuid(struct user_namespace *ns, uid_t uid) {
    struct uid_gid_map *map = &ns->uid_map;

    for (idx = 0; idx < map->nr_extents; idx++) {
        if (uid >= map->extent[idx].first &&
            uid < map->extent[idx].first + map->extent[idx].count) {

            // 내부 UID → 외부 UID 변환
            uid = (uid - map->extent[idx].first) +
                  map->extent[idx].lower_first;
            return KUIDT_INIT(uid);
        }
    }
    return INVALID_UID;
}
```

### Capability와 Namespace 상호작용

**규칙:**
```text
네임스페이스 내부에서 CAP_SYS_ADMIN을 가져도
외부 객체에는 영향을 줄 수 없다
```

**검사 로직:**
```c
// include/linux/capability.h 계열의 핵심 계약을 단순화
bool capable(int cap) {
    return ns_capable(&init_user_ns, cap);
}

bool ns_capable(struct user_namespace *target_ns, int cap) {
    return security_capable(current_cred(), target_ns, cap,
                            CAP_OPT_NONE) == 0;
}
```

`capable()`은 초기 user namespace에 대한 권한을 묻는다. 특정 namespace가 소유하는 자원은 해당 자원의 user namespace를 `ns_capable(target_ns, cap)` 같은 helper에 명시적으로 전달한다. 일반 VFS의 `inode_permission()`에 “현재 namespace가 아니면 즉시 거부”라는 단일 검사는 없으며, DAC·idmapped mount·filesystem user namespace·LSM 훅과 `capable_wrt_inode_uidgid()` 계열 helper가 각 연산의 계약에 맞게 결합된다.

### 컨테이너 격리 구현

**Docker/Podman의 내부:**
```c
// 1. User namespace 생성
unshare(CLONE_NEWUSER);

// 2. UID 매핑
write_file("/proc/self/uid_map", "0 1000 1");
write_file("/proc/self/setgroups", "deny");  // 보안상 필수
write_file("/proc/self/gid_map", "0 1000 1");

// 3. 다른 namespace 생성 (이제 권한 있음)
unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET);

// 4. Capability 제한
cap_t caps = cap_init();
cap_set_proc(caps);

// 5. Seccomp 적용
prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);

// 6. 루트 변경
chroot("/var/lib/container/rootfs");
chdir("/");

// 7. 컨테이너 프로세스 실행
execve("/bin/sh", ...);
```

## 8. 하드웨어 보안 기능

### KASLR (Kernel Address Space Layout Randomization)

**목표: 커널 메모리 주소 예측 불가능하게**

**전통적 커널:**
```text
커널 베이스: 0xffffffff81000000 (고정)
→ 공격자가 ROP 가젯 주소를 미리 계산 가능
```

**KASLR 적용:**
```c
// arch/x86/boot/compressed/kaslr.c
void choose_random_location(unsigned long input,
                           unsigned long output,
                           unsigned long *virt_addr) {
    unsigned long random = get_boot_seed();  // 부팅 시 엔트로피
    unsigned long offset = random % (512 * 1024 * 1024);  // 최대 512MB

    *virt_addr = KERNEL_IMAGE_START + offset;
    // 예: 0xffffffff81000000 + 0x12345000
}
```

**확인:**
```bash
$ sudo cat /proc/kallsyms | grep _text
ffffffff82a00000 T _text  # 부팅마다 변경됨

# 우회 방어
$ cat /proc/sys/kernel/kptr_restrict
2  # root도 커널 주소 보기 불가
```

### SMAP (Supervisor Mode Access Prevention)

**문제: 커널이 사용자 메모리를 직접 읽기**
```c
// 취약한 코드
void vulnerable_syscall(char __user *user_ptr) {
    char buf[64];
    strcpy(buf, user_ptr);  // 커널이 user_ptr을 직접 참조
    // 공격자가 user_ptr에 커널 주소를 넣으면?
}
```

**SMAP 보호:**
```text
CPU가 CPL=0 (커널 모드)에서 U=1 (사용자 페이지) 접근 시
→ Page Fault 발생
```

**올바른 접근:**

Intel x86 정의에 따르면 `STAC`은 EFLAGS의 AC(Alignment Check) 비트를 **set**(1)하여 SMAP 검사를 일시적으로 비활성화하고, `CLAC`은 AC를 **clear**(0)하여 SMAP을 다시 활성화한다. 즉 SMAP을 우회해 사용자 메모리에 접근하는 짧은 구간 사이를 `STAC... CLAC`으로 감싸는 것이 정상 패턴이다.

```c
// include/linux/uaccess.h (x86)
long copy_from_user(void *to, const void __user *from, unsigned long n) {
    stac();          // AC = 1 → SMAP 우회 허용 구간 시작
    n = rep_movs(to, from, n);   // 복사하지 못한 바이트 수가 남는다
    clac();          // AC = 0 → SMAP 다시 적용
    return n;        // 성공 시 0, 실패 시 복사하지 못한 바이트 수
}
```

**커널 컴파일러 강제:**
```c
// __user 어노테이션 없이 역참조 시도 시
char c = *user_ptr;  // sparse 경고 발생
// warning: dereference of noderef expression
```

### SMEP (Supervisor Mode Execution Prevention)

**문제: 커널에서 사용자 코드 실행**
```c
// 공격 시나리오
// 1. 사용자 공간에 셸코드 배치 (0x400000)
// 2. 커널 취약점으로 RIP를 0x400000으로 변경
// 3. 커널 권한으로 셸코드 실행 → root 탈취
```

**SMEP 보호:**
```text
CPU가 CPL=0에서 U=1 페이지의 명령어 fetch 시도 시
→ Page Fault (#PF, error_code=0x11)
```

**CR4 레지스터 비트:**
```c
// arch/x86/include/asm/processor.h
#define X86_CR4_SMEP  _BITUL(X86_CR4_SMEP_BIT)  // bit 20
#define X86_CR4_SMAP  _BITUL(X86_CR4_SMAP_BIT)  // bit 21

// 부팅 시 활성화
write_cr4(read_cr4() | X86_CR4_SMEP | X86_CR4_SMAP);
```

**확인:**
```bash
$ cat /proc/cpuinfo | grep -E 'smep|smap'
flags: ... smep smap ...

# 또는
$ dmesg | grep -E 'SMEP|SMAP'
[    0.000000] x86/cpu: User Mode Instruction Prevention (UMIP) activated
[    0.000000] x86/cpu: SMEP enabled
[    0.000000] x86/cpu: SMAP enabled
```

### PXN (Privileged Execute Never) - ARM

**ARM 버전의 SMEP:**
```c
// arch/arm64/include/asm/pgtable-prot.h
#define PAGE_KERNEL  __pgprot(_PAGE_DEFAULT | PTE_PXN | PTE_UXN)
                                            ^
                                            └─ Privileged eXecute Never
```

**페이지 테이블 비트:**
```text
PTE bit 53: PXN
  0: EL1에서 실행 가능
  1: EL1에서 실행 불가

PTE bit 54: UXN
  0: EL0에서 실행 가능
  1: EL0에서 실행 불가
```

### CET (Control-flow Enforcement Technology) - Intel

**Shadow Stack (최신 CPU):**
```c
// 함수 호출 시
call function
  → 일반 스택에 return address 푸시
  → Shadow Stack에도 복사 (하드웨어)

// 함수 반환 시
ret
  → Shadow Stack의 주소와 비교 (하드웨어)
  → 불일치 시 #CP(21) 예외 발생
```

**ROP 공격 무력화:**
```c
// 공격자가 스택 오버플로우로 return address 변조해도
// Shadow Stack과 불일치 → 즉시 탐지
```

**커널 지원:**
```bash
$ cat /proc/cpuinfo | grep shstk
flags: ... shstk ...  # Shadow Stack 지원

# Linux 6.6+ 커널에서 지원
CONFIG_X86_SHADOW_STACK=y
```

## 9. 실전 보안 강화

### 사례 1: 웹 서버 보안

**최소 권한 원칙 적용:**
```bash
# 1. 전용 사용자 생성
$ useradd -r -s /bin/false nginx

# 2. Capability로 포트 바인딩
$ setcap cap_net_bind_service=+ep /usr/sbin/nginx

# 3. AppArmor 프로필
$ cat /etc/apparmor.d/usr.sbin.nginx
/usr/sbin/nginx {
  capability net_bind_service,
  capability setuid,
  capability setgid,

  /var/www/html/** r,
  /var/log/nginx/** w,

  deny /etc/shadow r,
  deny /proc/** w,
  deny @{HOME}/** r,
}

$ aa-enforce /usr/sbin/nginx

# 4. Seccomp 필터
$ cat nginx-seccomp.json
{
  "defaultAction": "SCMP_ACT_ERRNO",
  "syscalls": [
    {"names": ["read", "write", "open", "close", "stat", "fstat",
               "poll", "lseek", "mmap", "mprotect", "munmap",
               "accept", "socket", "bind", "listen", "sendfile",
               "epoll_create", "epoll_wait"], "action": "SCMP_ACT_ALLOW"},
    {"names": ["execve", "fork", "ptrace"], "action": "SCMP_ACT_ERRNO"}
  ]
}

# 5. Systemd 하드닝
$ cat /etc/systemd/system/nginx.service
[Service]
User=nginx
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
NoNewPrivileges=yes
ReadWritePaths=/var/log/nginx /var/cache/nginx
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
```

### 사례 2: 컨테이너 보안

**다층 방어:**
```bash
# 1. User namespace로 root 격리: 자동 할당
$ podman run --userns=auto alpine sh

# 또는 명시적 mapping을 별도 실행으로 선택
$ podman run --uidmap 0:100000:65536 alpine sh

# 2. Read-only root filesystem
$ docker run --read-only \
  --tmpfs /tmp \
  --tmpfs /var/run \
  myapp

# 3. Capability 제거
$ docker run --cap-drop=ALL \
  --cap-add=NET_BIND_SERVICE \
  myapp

# 4. Seccomp 강화
$ docker run --security-opt seccomp=strict.json myapp

# 5. SELinux 라벨
$ docker run --security-opt label=type:svirt_apache_t myapp
```

### 사례 3: 커널 파라미터 튜닝

```bash
# /etc/sysctl.d/99-security.conf

# ASLR 최대 강도
kernel.randomize_va_space=2

# dmesg 접근 제한
kernel.dmesg_restrict=1

# 커널 주소 숨김
kernel.kptr_restrict=2

# ptrace 제한 (프로세스는 자식만 추적 가능)
kernel.yama.ptrace_scope=1

# 하드/심볼릭 링크 보호
fs.protected_hardlinks=1
fs.protected_symlinks=1

# 코어덤프 비활성화
kernel.core_pattern=|/bin/false

# SysRq 키 비활성화
kernel.sysrq=0

# 적용
$ sysctl -p /etc/sysctl.d/99-security.conf
```


---

## 결론

리눅스 보안은 다층 방어(defense-in-depth) 철학을 따른다.

1. **DAC**: 기본 권한 차단(1차)
2. **Capabilities**: root 권한 분할(2차)
3. **LSM(SELinux/AppArmor)**: 강제 접근 통제(3차)
4. **Seccomp**: 시스템 콜 필터링(4차)
5. **Namespaces**: 리소스 격리(5차)
6. **하드웨어**: KASLR, SMAP, SMEP, CET(6차)

각 계층은 독립적으로 동작하며, 하나가 뚫려도 다른 계층이 방어한다. 완벽한 보안은 없지만 공격 비용을 높이는 것이 목표다.

현대 보안은 "모든 것을 차단하고 필요한 것만 허용"하는 최소 권한 원칙(Principle of Least Privilege)으로 수렴한다. 컨테이너, seccomp, LSM 모두 이 원칙을 구현하는 도구다.

## 더 읽을거리

- Smalley, S., et al. (2001). *Implementing SELinux as a Linux Security Module*. NAI Labs.
- Wright, C., et al. (2002). *Linux Security Modules: General Security Support for the Linux Kernel*. USENIX Security Symposium.
- Cowan, C., et al. (2000). *SubDomain: Parsimonious Server Security*. USENIX LISA.
- Edge, J. (2015). *A seccomp overview*. LWN.net. https://lwn.net/Articles/656307/
- Kerrisk, M. (2013). *Namespaces in operation, part 5: User namespaces*. LWN.net. https://lwn.net/Articles/532593/
- Intel Corporation. (2016). *Control-flow Enforcement Technology Specification*.
- ARM Ltd. (2021). *Armv8-A Architecture Reference Manual*.
- Linux kernel source tree: `security/` directory
 - `security/selinux/` - SELinux implementation
 - `security/apparmor/` - AppArmor implementation
 - `security/commoncap.c`, `kernel/capability.c` - Capability 검사와 syscall
 - `kernel/seccomp.c` - Seccomp implementation
 - `kernel/user_namespace.c` - User namespace
- Documentation: `Documentation/security/` in kernel source
- Red Hat, *SELinux User's and Administrator's Guide* — RHEL 공식 문서.
