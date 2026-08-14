# 시스템 시리즈

운영체제를 Linux 기능 목록이 아니라 자원 가상화, 보호 경계, 스케줄링, I/O, 격리의 모델로 이해하기 위한 시리즈다. 01~16은 Linux를 구체 구현으로 삼는 공통 핵심이고 17~22는 Windows 비동기 I/O, 엔진 플랫폼, 서버 인프라, 커널 진단을 맡을 때 읽는 전문 경로다.

## 공통 핵심

| # | 문서 | 소유하는 모델 |
|---|---|---|
| 01 | [부트](part01_boot.md) | firmware에서 PID 1까지 이어지는 초기화 의존성 |
| 02 | [프로세스](part02_process.md) | 주소 공간·실행 상태·fork/exec·thread 표현 |
| 03 | [가상 메모리](part03_virtual_memory.md) | 주소 변환·fault·mapping·working set 관측 |
| 04 | [커널 할당](part04_kernel_allocator.md) | buddy·slab·NUMA·huge page·memory control |
| 05 | [시스템 콜](part05_syscall.md) | user/kernel 보호 경계와 ABI |
| 06 | [인터럽트](part06_interrupt.md) | 비동기 hardware event와 deferred work |
| 07 | [시그널](part07_signal.md) | process 비동기 통지·job control·PTY |
| 08 | [스케줄러](part08_scheduler.md) | 공정성·deadline·load balancing·latency |
| 09 | [커널 동기화](part09_sync.md) | spin/mutex/futex/RCU/per-CPU 상태 |
| 10 | [ELF와 정적 링킹](part10_elf_linker.md) | section·symbol·relocation·loader image |
| 11 | [동적 링킹](part11_dynamic_linking.md) | PIC·GOT/PLT·symbol resolution·interposition |
| 12 | [파일시스템](part12_filesystem.md) | inode·directory·VFS·journal |
| 13 | [블록 I/O](part13_block_io.md) | page cache·request queue·writeback·NVMe |
| 14 | [비동기 I/O](part14_io_uring.md) | submission/completion·등록 자원·취소·보안 |
| 15 | [IPC](part15_ipc.md) | pipe·shared memory·message·local socket |
| 16 | [네트워크 스택](part16_network_stack.md) | packet 수명·socket queue·NAPI·stack 경로 |

## 전문 경로

| # | 문서 | 필요한 경우 |
|---|---|---|
| 17 | [Windows IOCP](part17_windows_iocp.md) | overlapped completion·worker concurrency·I/O 수명을 설계할 때 |
| 18 | [디바이스 드라이버](part18_driver.md) | MMIO·DMA·IRQ·module 수명과 장치 경계를 다룰 때 |
| 19 | [가상화](part19_virtualization.md) | VM exit·EPT·virtio와 cloud 실행 비용을 다룰 때 |
| 20 | [컨테이너](part20_container.md) | namespace·cgroup·image·runtime 격리를 다룰 때 |
| 21 | [시스템 보안](part21_security.md) | capability·LSM·seccomp·hardware protection을 설계할 때 |
| 22 | [커널 관측](part22_kernel_observability.md) | ftrace·perf·eBPF·sanitizer·dump로 kernel 문제를 진단할 때 |

공통 핵심의 완료 조건은 Linux 명령을 외우는 것이 아니다. 각 자원의 소유자, 상태 전이, 실패 조건, 관측 가능한 비용을 설명할 수 있어야 한다. 전문 경로는 해당 업무를 맡을 때만 추가한다.
