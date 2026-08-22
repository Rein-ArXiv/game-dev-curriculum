# 32. 아무것도 못 찾은 도구를 어떻게 믿는가

## 들어가며

감사가 남긴 마지막 항목은 이렇게 적혀 있었다 — 계획표에는 sanitizer 가 있는데, 저장소 전체에 `sanitiz|fsanitize|asan|ubsan` 이 **0건**이다.

문자열 하나 넣는 일로 보인다. 실제로 `-DMEGA_SANITIZE=address` 를 붙이고 CI 잡을 하나 늘리면 문자열은 0이 아니게 된다. 그런데 그렇게 하면 결과가 두 가지 중 하나로 나온다.

**결함을 찾는다.** 그러면 고치면 되고, 도구가 일한다는 것이 증명된다.
**아무것도 못 찾는다.** 그러면 무엇이 증명됐는가?

이 편의 실제 결과는 뒤쪽이다. 117개 검사를 AddressSanitizer 로 다시 돌려 **한 건도 나오지 않았다.** 그래서 이 편의 절반은 "그 green 이 무엇을 뜻하고 무엇을 뜻하지 않는가"를 실측으로 정하는 일이 된다.

| 대상 | 하는 일 |
|---|---|
| `MEGA_SANITIZE` (CMake 캐시 변수) | 별도 빌드 디렉터리에서 계측을 켠다. 기본값은 꺼짐 |
| ASan 런타임 복사 | 실행 파일 옆에 `clang_rt.asan_dynamic-x86_64.dll` 를 둔다 |
| `.github/workflows/ci.yml` 의 `asan-windows` | 같은 테스트 목록을 다른 계측으로 다시 돌린다 |

도달점의 수치.

- ASan 빌드에서 **117/117 전수 통과, 540.49초**. 계측 없는 같은 목록은 64.83초 — **8.3배**(실행마다 466~540초 사이에서 흔들린다).
- 검출 **0건.** 그 0이 무엇인지 확인하기 위해 결함을 하나 심었고, 64바이트 힙 영역 한 칸 밖 읽기가 **파일·줄·심볼까지 붙은 보고서**로 나왔다.
- 같은 심기를 921639바이트 영역에 하면 **한 칸 밖은 보고되지 않는다.** 1024칸 밖에서야 잡힌다. ASan 의 정밀도가 할당 크기에 따라 다르다는 관측이다.
- 느려지는 정도가 검사마다 다르다. GPU present 1.5배, 협동 프로세스 왕복 2.5배, 절차 지형 발파 **90배**.
- ASan 으로 다시 빌드한 실행 파일이 계측 없는 빌드가 기록한 리플레이를 재생해 체인 `0xecfa0daff65fe392` 를 그대로 낸다. 결정성이 **계측 경계**를 넘는다.
- 처음 실행하면 117개 검사가 **전부** 종료 코드 0xC0000135 로 죽는다. 원인은 코드가 아니라 DLL 이었다.

---

## 목차
1. [옵션이 아니라 별도 트리인 이유](#1-옵션이-아니라-별도-트리인-이유)
2. [MSVC 가 가진 것과 없는 것](#2-msvc-가-가진-것과-없는-것)
3. [전부 죽은 117개](#3-전부-죽은-117개)
4. [같은 목록을 다시 돌린다](#4-같은-목록을-다시-돌린다)
5. [0건이라는 결과](#5-0건이라는-결과)
6. [심어 본 결함 — 그리고 잡히지 않은 것](#6-심어-본-결함--그리고-잡히지-않은-것)
7. [ASan 이 보지 않는 것](#7-asan-이-보지-않는-것)
8. [계측이 결과를 바꾸지 않았는가](#8-계측이-결과를-바꾸지-않았는가)
9. [CI 잡과 로컬 재현](#9-ci-잡과-로컬-재현)
10. [실행과 관측](#10-실행과-관측)

---

## 1. 옵션이 아니라 별도 트리인 이유

새니타이저는 컴파일 옵션이면서 동시에 런타임이다. `/fsanitize=address` 로 컴파일된 번역 단위는 힙 접근마다 그림자 메모리를 조회하고, 그 그림자를 관리하는 것은 링크된 런타임이다.

그래서 **섞이면 안 된다.** 같은 트리에서 옵션을 켰다 껐다 하면 계측된 TU 와 안 된 TU 가 한 실행 파일에 들어가고, 그 상태에서는 없는 결함이 보고되기도 하고 있는 결함이 안 보이기도 한다. 어느 쪽이든 그 보고서는 읽을 가치가 없다.

캐시 변수 + 별도 디렉터리로 그 혼합을 구조적으로 불가능하게 만든다.

<!-- snippet: CMakeLists.txt@sanitize-option -->
```cmake
set(MEGA_SANITIZE "" CACHE STRING
    "address | undefined | address,undefined (빈 값 = 끔). MSVC 는 address 만.")
if(MEGA_SANITIZE)
    if(MSVC)
        if(NOT MEGA_SANITIZE STREQUAL "address")
            message(FATAL_ERROR
                "MSVC 가 지원하는 새니타이저는 address 하나다(요청: ${MEGA_SANITIZE}).\n"
                "UndefinedBehaviorSanitizer 가 필요하면 clang-cl 또는 GCC/Clang 으로 빌드하라.")
        endif()
        # /Zi 는 보고서에 파일·줄을 넣는다. 주소만 나오는 ASan 보고서는 읽는
        # 데 드는 비용이 커서 무시당하고, 무시당하는 진단은 없는 진단이다.
        add_compile_options(/fsanitize=address /Zi)
        # ASan 은 증분 링크와 함께 쓸 수 없다. CMake 의 MSVC 기본 Debug 구성은
        # /INCREMENTAL 을 켜므로 여기서 꺼 둔다.
        add_link_options(/INCREMENTAL:NO /DEBUG)
    else()
        add_compile_options(-fsanitize=${MEGA_SANITIZE} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${MEGA_SANITIZE})
    endif()
    add_compile_definitions(MEGA_SANITIZER_BUILD=1)
    message(STATUS "새니타이저 빌드: ${MEGA_SANITIZE}")
```

**기본값이 꺼짐인 것도 판단이다.** 이 저장소는 계층 규칙 검사와 도달성 게이트를 "끌 수 있는 옵션이 아니다 — 끌 수 있는 게이트는 게이트가 아니다"라고 적어 두었다. 새니타이저는 다르게 다룬다. 아래에서 재는 8.3배 때문이다. 늘 켜 두면 개발 루프가 여덟 배 느려지고, 느려진 루프에서 사람은 검사를 덜 돌린다. 그러면 켜 둔 이유가 사라진다.

그 대신 **CI 에서는 끌 수 없다.** 잡이 하나 늘어 있고 그 잡의 실패는 다른 잡의 실패와 같은 급이다.

`/Zi` 를 함께 켜는 것도 같은 종류의 판단이다. 주소만 찍힌 ASan 보고서는 읽는 데 드는 비용이 커서 무시당하고, 무시당하는 진단은 없는 진단이다. 아래 6절의 보고서에 파일·줄·심볼이 다 붙어 있는 것은 이 한 줄 덕분이다.

## 2. MSVC 가 가진 것과 없는 것

`-DMEGA_SANITIZE=undefined` 를 주면 구성 단계에서 실패한다.

MSVC 에는 UndefinedBehaviorSanitizer 가 없다. 그 사실을 조용히 무시하고 옵션만 삼키면, 그 사람은 **자기가 검사하지 않은 것을 검사했다고 믿게 된다.** 정수 오버플로도 정렬 위반도 널 역참조도 보지 않은 채 green 을 받는다. 그래서 요청받은 것을 줄 수 없으면 만들지 않는다.

지금 이 저장소가 **가진** 관측 도구를 정리하면 이렇다.

| 무엇을 보는가 | 도구 | 이 저장소에 있는가 |
|---|---|---|
| 힙 경계 밖 읽기·쓰기, 해제 후 사용, 스택 경계 | ASan | **있다** |
| 정의되지 않은 동작(오버플로·정렬·널) | UBSan | 없다 — MSVC 에 없다 |
| 초기화되지 않은 읽기 | MSan | 없다 — MSVC 에 없다 |
| 데이터 경합 | TSan | 없다 — MSVC 에 없다 |
| 결정성 회귀 | 골든 해시 체인 리플레이 | **있다** |
| 구조 위반 | 계층 규칙 · 도달성 게이트 | **있다** |

없는 셋 중 둘은 이 제품에 실제 표적이 있다. 잡 스케줄러가 워커 스레드를 띄우고 잔해 물리가 그 위에서 돌므로 TSan 이 볼 것이 있고, `platform::RuntimeBudget` 처럼 필드가 많은 구조체는 MSan 이 볼 것이 있다. **그 둘을 지금 검사하지 않는다는 사실을 적어 두는 것이 지금 할 수 있는 최선이다.**

## 3. 전부 죽은 117개

빌드는 깨끗하게 끝났다. 첫 실행에서 117개 검사가 **전부** 실패했다.

```
exit=-1073741515
```

`0xC0000135` — DLL not found. 코드는 한 줄도 실행되지 않았다.

MSVC 의 ASan 은 정적 라이브러리가 아니라 `clang_rt.asan_dynamic-x86_64.dll` 이고, 그 DLL 은 컴파일러 옆(`VC\Tools\MSVC\<버전>\bin\Hostx64\x64`)에 있다. 개발자 프롬프트에서는 그 디렉터리가 PATH 에 있어 돌지만, 평범한 셸이나 CI 러너나 CTest 에서는 없다.

이 실패 모양이 나쁜 이유는 실패한다는 것이 아니라 **거짓말한다**는 것이다. `ctest` 는 "117개 중 117개 실패"라고 보고하고, 그 보고서만 보면 새니타이저가 결함을 117개 찾은 것처럼 읽힌다. 실제로는 한 줄도 돌지 않았다.

구성 단계에서 DLL 을 실행 파일 옆으로 복사해 그 상태를 없앤다.

<!-- snippet: CMakeLists.txt@asan-runtime -->
```cmake
    if(MSVC)
        get_filename_component(MEGA_MSVC_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        find_file(MEGA_ASAN_RUNTIME_DLL
                  NAMES clang_rt.asan_dynamic-x86_64.dll clang_rt.asan_dynamic-i386.dll
                  HINTS ${MEGA_MSVC_BIN_DIR}
                  NO_DEFAULT_PATH)
        if(MEGA_ASAN_RUNTIME_DLL)
            file(COPY ${MEGA_ASAN_RUNTIME_DLL}
                 DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
            if(CMAKE_CONFIGURATION_TYPES)
                foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
                    file(COPY ${MEGA_ASAN_RUNTIME_DLL}
                         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${cfg})
                endforeach()
            endif()
            message(STATUS "ASan 런타임 복사: ${MEGA_ASAN_RUNTIME_DLL}")
```

`NO_DEFAULT_PATH` 를 붙인 것은 **다른 툴체인의 DLL 을 집어 오지 않기 위해서**다. Clang 이나 다른 버전의 MSVC 가 설치된 머신에서 이름만 보고 아무 것이나 복사하면, 계측한 컴파일러와 런타임의 버전이 어긋나 실행 시에 조용히 이상해진다.

찾지 못하면 경고를 내고 그 경고에 해결 방법을 적는다. 여기서 실패시키지 않는 이유는 개발자 프롬프트에서는 DLL 없이도 잘 돌기 때문이다 — 되는 구성을 막는 게이트는 게이트가 아니라 방해다.

## 4. 같은 목록을 다시 돌린다

ASan 잡이 도는 것은 **일반 잡과 정확히 같은 117개**다. 새니타이저 전용 테스트를 따로 쓰지 않는다.

이유는 표적의 위치다. 새로 쓴 테스트는 새로 쓴 코드를 돌리는데, 메모리 결함은 오래 도는 코드에 산다. 이 저장소에서 오래 도는 것은 25000틱 셀프테스트, 두 프로세스가 실제 UDP 로 오가는 협동 검사, 반복 매치 soak 다. 그 셋을 계측하지 않으면 계측한 것이 없다.

같은 목록을 돌리는 데는 대가가 있다. 시간이다.

| 검사 | 계측 없음 | ASan | 배수 |
|---|---|---|---|
| `mega_defense_gpu_selftest` | 1.58s | 2.43s | 1.5 |
| `test_defense_coop_process` | 1.12s | 2.85s | 2.5 |
| `test_replay_golden` | 1.70s | 6.07s | 3.6 |
| `mega_defense_selftest` | 0.81s | 2.99s | 3.7 |
| `mega_defense_selftest_ai` | 3.99s | 27.25s | 6.8 |
| `test_terrain_blast` | 0.39s | 35.07s | **90** |
| **전체 117개** | **64.83s** | **540.49s** | **8.3** |

표의 두 열은 각각 한 실행의 값이다. 같은 목록을 다시 돌리면 전체가 466~540초 사이에서 흔들리므로 배수도 7~8배 사이에서 흔들린다 — **그 흔들림이 이 수치의 정밀도다.** 소수점 둘째 자리까지 적혀 있다고 해서 그만큼 재현되는 값이 아니다.

배수가 검사마다 다른 것이 정보다. GPU present 는 1.5배다 — 시간을 드라이버와 스왑체인에서 쓰므로 계측할 것이 별로 없다. 협동 프로세스 왕복은 2.5배다 — 소켓 대기가 대부분이다.

`test_terrain_blast` 의 90배가 이 표에서 가장 많은 것을 말한다. 그 검사는 절차 지형의 밀도장을 반복해서 깎고 그때마다 정적 기하와 BVH 를 다시 짓는다. 즉 **거의 전부가 할당과 메모리 접근**이고, 그것이 정확히 ASan 이 계측하는 것이다. 배수는 코드가 무엇을 하는지의 지문이다.

## 5. 0건이라는 결과

```
100% tests passed out of 117
Total Test time (real) = 540.49 sec
```

검출 0건이다. 이 결과를 어떻게 다뤄야 하는지가 이 편의 핵심이다.

먼저, **이 0은 "메모리 결함이 없다"가 아니다.** ASan 은 정적 분석이 아니라 런타임 계측이라, 실행된 접근만 본다. 돌지 않은 팔은 계측되지 않는다. 이 저장소에는 27개 A/B 스위치가 있고 ctest 가 도는 조합은 그중 아홉 가지다.

그다음, **이 0이 "도구가 꺼져 있었다"인지 확인해야 한다.** 3절의 DLL 사고가 정확히 그 모양이었다 — 그때는 시끄럽게 실패했지만, 조용히 계측이 빠진 상태는 green 과 구분되지 않는다.

그래서 결함을 하나 심는다. 이 저장소가 매 그룹 끝에서 하는 것과 같은 절차다.

## 6. 심어 본 결함 — 그리고 잡히지 않은 것

셀프테스트의 마지막에는 "렌더가 실제로 일어났는가"를 묻는 픽셀 훑기가 있다. 그 바로 앞에 한 칸 밖 읽기를 넣었다.

```cpp
    {   // ASAN-MUTATION
        std::vector<uint32_t> small;
        small.assign(16, 1u);
        volatile uint32_t probeSmall = small.data()[small.size()];
```

`volatile` 이 붙은 이유가 있다. 처음에는 그냥 읽었는데 **아무 일도 일어나지 않았다.** 읽은 값을 아무도 쓰지 않아 최적화가 그 적재를 통째로 지웠기 때문이다. ASan 은 실행된 접근만 보고, 지워진 접근은 실행되지 않는다. 이것이 첫 번째 관측이다 — **`/O2` 로 계측하면 "코드에 있는 결함"과 "실행되는 결함"이 다르다.**

`volatile` 을 붙이자 이렇게 나온다.

```
[asan-probe] small size=16 cap=16 fb=230400
==27168==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x11a2380b9e60
READ of size 4 at 0x11a2380b9e60 thread T0
    #0 ... in mega::game::RunDefenseHeadless ...\include\game\defense_app.hpp:1912
    #1 ... in mega::game::RunDefenseSelftest ...\include\game\defense_app.hpp:1945
0x11a2380b9e60 is located 0 bytes after 64-byte region [0x11a2380b9e20,0x11a2380b9e60)
    #0 ... in operator new ... asan_win_new_scalar_thunk.cpp:40
    #1 ... in std::_Allocate<16,std::_Default_allocate_traits> ... xmemory:256
```

**계측은 살아 있다.** 제품 프레임 루프 안의 접근이고, 보고서에 읽은 크기·할당 지점·읽은 지점이 파일과 줄까지 붙어 있다.

그런데 여기서 **예상하지 못한 것**이 나왔다. 같은 한 칸 밖 읽기를 프레임버퍼에 하면 잡히지 않는다.

```
[asan-probe] fb=230400 big+1 ok 0
==51080==ERROR: AddressSanitizer: heap-buffer-overflow ...
0x023101563820 is located 4089 bytes after 921639-byte region [0x023101481800,0x023101562827)
```

첫 줄이 요점이다. 480×480 프레임버퍼(230400개, 921600바이트) 바로 다음 칸을 읽었는데 **아무 보고 없이 0을 돌려주었다.** 1024칸(4089바이트) 밖으로 나가서야 잡힌다.

보고서가 이유를 말해 준다 — 할당된 영역이 **921639바이트**다. 요청한 921600보다 39바이트가 크다. ASan 이 지키는 경계는 `std::vector::size()` 가 아니라 **할당자가 실제로 받아 온 영역의 끝**이고, 그 사이의 패딩은 ASan 이 보기에 정당한 메모리다.

이것을 좁히려면 컨테이너 주석(container annotation)이 필요하다. `std::vector` 가 자기 `size()` 와 `capacity()` 의 차이를 ASan 그림자에 표시해 주는 기능인데, 이 구성에서는 켜져 있지 않다. **켜지 않은 것이 아니라 켜져 있지 않다는 것을 이 실험으로 알았다.**

그래서 정확한 문장은 이렇게 된다.

> 이 ASan 잡은 **할당 경계**를 넘는 접근을 잡는다. **컨테이너 경계**만 넘는 접근은 할당자의 패딩 안에 들어가면 잡지 못한다.

심은 결함을 지우고 다시 돌리면 `[selftest] result=CLEAR ticks=1377` 로 돌아온다.

## 7. ASan 이 보지 않는 것

6절의 관측을 포함해, 이 잡의 green 이 **덮지 않는** 것을 나열한다.

**돌지 않은 코드.** 27개 스위치 중 ctest 가 실제로 도는 조합은 아홉이다. 나머지 팔에 결함이 있어도 이 잡은 조용하다.

**최적화가 지운 접근.** 6절에서 본 것이다. `/O2` 에서 결과를 쓰지 않는 적재는 사라지고, 사라진 접근은 계측되지 않는다.

**할당자 패딩 안의 초과.** 6절의 두 번째 관측. 컨테이너 주석이 없는 한 `size()` 와 할당 끝 사이는 회색 지대다.

**초기화되지 않은 읽기.** MSan 의 영역이고 MSVC 에는 없다. 이 제품에는 필드가 많은 구조체가 여럿이라 실제 표적이 있다.

**데이터 경합.** TSan 의 영역이고 MSVC 에는 없다. 잡 스케줄러가 최대 여덟 워커를 띄우고 잔해 물리가 그 위에서 도는데, 그 경로에 경합이 있어도 ASan 은 보지 않는다. 지금 그 자리를 지키는 것은 `jobs=0|1|2|4|8` 다섯 팔이 **같은 상태 해시를 낸다**는 단언뿐이다 — 경합을 직접 보는 것이 아니라 경합이 결과를 바꿨다면 해시가 갈린다는 간접 관측이다.

**정의되지 않은 동작.** UBSan 의 영역이고 MSVC 에는 없다.

이 다섯을 적어 두는 것이 "sanitizer 잡을 추가했다"보다 정확한 상태 보고다.

## 8. 계측이 결과를 바꾸지 않았는가

새니타이저 빌드에는 특유의 위험이 하나 있다. 계측이 프로그램의 동작을 바꾸면, 그 빌드에서 나온 green 은 제품의 green 이 아니다.

이 저장소에는 그것을 물을 도구가 마침 있다. 결정성 리플레이다.

계측 **없는** 빌드가 기록한 파일을, 계측 **있는** 실행 파일로 재생한다.

```
> build_asan\bin\Release\mega_defense.exe --verify-replay build\replays\solo.mgrp
[replay] result=MATCH ticks=1377/1377 frames=1377
         chain=0xecfa0daff65fe392/0xecfa0daff65fe392 stride=1
[replay] lockstep=OK frames=1377 stalls=0 fault=none tick=0
```

1377틱의 체인이 비트 동일하다. 아바타 팔도 마찬가지다(`0xd6a4788b65889b9a`).

**계측은 시간을 여덟 배로 늘렸지만 결과는 한 비트도 바꾸지 않았다.** 이 문장은 두 방향으로 쓰인다. ASan 빌드의 green 이 제품의 green 과 같은 것을 말한다는 쪽으로도 쓰이고, 이 시뮬의 결정성이 프로세스 경계뿐 아니라 **계측 경계**까지 넘는다는 쪽으로도 쓰인다.

다만 넘지 못한 경계도 분명히 해 둔다. 두 빌드는 같은 컴파일러·같은 최적화 수준이다. 다른 컴파일러에서 같은 체인이 나오는지는 이 저장소가 돌려 본 적이 없다.

## 9. CI 잡과 로컬 재현

CI 파일에 잡이 하나 늘었다.

<!-- snippet: .github/workflows/ci.yml@asan-job -->
```yaml
  asan-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Configure (CMake, ASan)
        run: cmake -S . -B build_asan -DMEGA_SANITIZE=address

      - name: Build (ASan)
        run: cmake --build build_asan --config Release

      - name: Test (ctest, ASan)
        run: ctest --test-dir build_asan -C Release --output-on-failure --timeout 900
```

`--timeout 900` 이 필요한 이유는 4절의 표다. `CMakeLists.txt` 는 테스트별 타임아웃을 계측 없는 실행 기준으로 걸어 두었고, ASan 은 그것을 최대 90배까지 늘린다. 전역 하한을 올려 두지 않으면 느려진 것이 실패로 보고된다 — 그리고 **타임아웃 실패는 새니타이저 보고서와 전혀 다른 것인데 로그에서는 둘 다 그냥 빨간색이다.**

로컬 재현은 CI 파일 자체에 주석으로 적어 두었다. 재현 절차를 다른 문서에 적으면 CI 가 바뀔 때 그 문서가 따라오지 않는다.

```
#   cmake -S . -B build_asan -DMEGA_SANITIZE=address
#   cmake --build build_asan --config Release
#   ctest --test-dir build_asan -C Release --output-on-failure --timeout 900
#
# 하나만 다시 돌릴 때:
#
#   ctest --test-dir build_asan -C Release -R test_replay_golden --output-on-failure
#   build_asan\bin\Release\mega_defense.exe --selftest 4000
```

이 저장소의 CI 파일은 `projects/mega/` 아래에 있고 GitHub Actions 는 저장소 루트의 `.github/workflows/` 만 읽는다. 즉 이 워크플로는 **자동으로 돌지 않는다.** 그 사실은 파일 안에 이미 적혀 있었고, 잡이 하나 더 늘었다고 달라지지 않는다. 지금 이 두 잡은 로컬 재현 절차의 기계 판독 가능한 형태다.

## 10. 실행과 관측

**계측 없는 기준선.**

```
> ctest --test-dir build -C Release
100% tests passed out of 117
Total Test time (real) =  64.83 sec
```

**ASan.**

```
> cmake -S . -B build_asan -DMEGA_SANITIZE=address
-- 새니타이저 빌드: address
-- ASan 런타임 복사: C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/
   MSVC/14.51.36231/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll
> ctest --test-dir build_asan -C Release --output-on-failure --timeout 900
 98/117 Test  #98: test_terrain_blast ..................   Passed   35.07 sec
102/117 Test #102: mega_defense_selftest ...............   Passed    2.99 sec
105/117 Test #105: mega_defense_selftest_ai ............   Passed   27.25 sec
110/117 Test #110: mega_defense_record_solo ............   Passed    2.94 sec
111/117 Test #111: mega_defense_replay_solo ............   Passed    2.92 sec
116/117 Test #116: mega_defense_gpu_selftest ...........   Passed    2.43 sec
117/117 Test #117: reachability_gate ...................   Passed    0.66 sec

100% tests passed out of 117
Total Test time (real) = 540.49 sec
```

116번이 `Passed` 인 것도 읽어야 한다. 이 검사는 GPU 장치와 창이 없으면 종료 코드 77로 빠져 `Skipped` 로 세어지는데, 이 머신에서는 실제로 Vulkan 스왑체인에 다섯 프레임을 표시하고 통과했다. **즉 이 실행에서는 GPU 표시 경로도 ASan 계측 아래를 지났다.**

**심은 결함.**

```
0x11a2380b9e60 is located 0 bytes after 64-byte region   ← 잡힌다
[asan-probe] fb=230400 big+1 ok 0                        ← 잡히지 않는다
0x023101563820 is located 4089 bytes after 921639-byte region  ← 잡힌다
```

**계측 경계를 넘는 결정성.**

```
> build_asan\bin\Release\mega_defense.exe --verify-replay build\replays\solo.mgrp
[replay] result=MATCH chain=0xecfa0daff65fe392/0xecfa0daff65fe392
```

**등재부.** `include/` 114개 헤더 전부가 제품에서 도달한다. 랩 전용 등재는 0행이다.

```
  제품 도달 합계 114개 / 랩 전용 등재 0개 / 근거 없는 미도달 0개
```

---

## 결론

감사 항목은 "sanitizer 문자열이 0건"이었다. 그것을 0이 아니게 만드는 데는 CMake 열 줄이면 됐다. 나머지는 전부 **그 열 줄이 실제로 무엇을 하는지 확인하는 일**이었고, 확인하지 않았다면 이 편의 green 은 3절의 DLL 사고와 구분되지 않았을 것이다.

측정에서 나온 것 셋.

**계측을 켰다는 것과 계측이 돈다는 것은 다르다.** 첫 실행의 117개 전패는 코드가 아니라 DLL 이었고, 그 실패는 "새니타이저가 결함을 117개 찾았다"처럼 읽혔다. 도구를 들이면 그 도구가 무엇을 보고하는지를 먼저 배워야 한다.

**최적화가 결함을 지운다.** 결과를 쓰지 않는 경계 밖 읽기는 `/O2` 가 통째로 지웠고, 지워진 접근은 계측되지 않았다. 코드에 있는 결함과 실행되는 결함이 다르다는 것을 실측으로 보았다.

**정밀도가 균일하지 않다.** 64바이트 할당의 한 칸 밖은 파일·줄까지 붙은 보고서로 나오고, 921639바이트 할당의 한 칸 밖은 아무 소리도 안 난다. "ASan 이 경계 위반을 잡는다"는 문장은 **어느 경계인가**를 붙이지 않으면 틀린 문장이다.

현재 상태의 한계 넷.

**UBSan · MSan · TSan 이 없다.** MSVC 가 제공하지 않는다. 셋 중 TSan 의 표적은 이 제품에 확실히 있다 — 최대 여덟 워커가 잔해 물리를 나눠 돈다. 지금 그 자리를 지키는 것은 워커 수 다섯 팔의 상태 해시가 같다는 간접 단언뿐이다.

**컨테이너 주석이 꺼져 있다.** `std::vector` 의 `size()` 와 할당 끝 사이는 이 잡이 보지 않는 회색 지대다. 그 폭은 할당 크기에 따라 다르고, 921600바이트 요청에서는 39바이트였다.

**계측되는 것은 돌린 팔뿐이다.** 27개 스위치 중 ctest 가 도는 조합은 아홉이다. 나머지는 컴파일만 되고 계측되지 않는다.

**CI 파일이 자동으로 돌지 않는다.** 저장소 루트가 아니라 `projects/mega/` 아래에 있어 GitHub Actions 가 읽지 않는다. 두 잡 다 지금은 로컬 재현 절차의 기계 판독 가능한 형태이고, 그 사실은 잡을 늘려도 달라지지 않는다.
