## 들어가며

앞의 여섯 편은 게임을 세웠다. 이 편은 게임을 세우지 않는다. **이미 세운 것이 무너지지 않게 하는 장치**를 만든다.

시작 상태는 이렇다. 규칙은 전부 문서에 있었다.

- "진입점은 얇아야 한다."
- "`games/` 는 OS API 를 직접 부르지 않는다."
- "테스트와 제품이 같은 코드를 본다."
- "`include/` 의 라이브러리들은 게임이 실제로 쓴다."

넷 다 참이 아니었다. 그리고 넷 다 **아무 검사도 통과하지 못했다는 신호를 내지 않았다** — 빌드는 초록이었고 테스트는 전부 통과했다.

이 편의 도달점은 그 넷을 **빌드 실패**로 바꾸는 것이다. 끝나면 규칙을 어긴 편집이 커밋되기 전에 멈춘다. 규율은 깨지지만 검사기는 깨지지 않는다.

---

## 목차
1. [감사가 찾은 것](#1-감사가-찾은-것)
2. [문서에 적힌 규칙은 규칙이 아니다](#2-문서에-적힌-규칙은-규칙이-아니다)
3. [계층 규칙 여섯 줄](#3-계층-규칙-여섯-줄)
4. [화이트리스트가 블랙리스트보다 중요하다](#4-화이트리스트가-블랙리스트보다-중요하다)
5. [주석 속 socket( 을 위반으로 세지 않기](#5-주석-속-socket-을-위반으로-세지-않기)
6. [세 지점에서 돈다](#6-세-지점에서-돈다)
7. [도달성은 컴파일러에게 묻는다](#7-도달성은-컴파일러에게-묻는다)
8. [모두 컴파일하되, 도달성은 제품에서만 잰다](#8-모두-컴파일하되-도달성은-제품에서만-잰다)
9. [제3의 상태는 빌드 실패다](#9-제3의-상태는-빌드-실패다)
10. [게이트가 조용히 꺼지는 것을 막는 장치](#10-게이트가-조용히-꺼지는-것을-막는-장치)
11. [리포트는 생성물이다](#11-리포트는-생성물이다)
12. [실패도 부재도 아닌 세 번째 결과](#12-실패도-부재도-아닌-세-번째-결과)
13. [실행과 관측](#13-실행과-관측)

---

## 1. 감사가 찾은 것

이 편의 모든 장치에는 구체적인 발단이 있다.

**도달성.** `include/` 아래 헤더 89개 중 제품 실행 파일에서 도달 가능한 것은 **25개**였다. 나머지 64개, 줄 수로는 전체 9,310줄 중 **5,681줄(61%)** 이 게임에서 한 번도 컴파일되지 않았다. 헤더온리 파일은 아무도 인클루드하지 않으면 코드 생성 자체가 없기 때문이다. 그 상태에서 테스트 51개는 전부 통과했다.

이 상태의 진짜 문제는 "안 쓰는 코드가 있다"가 아니다. **"아직 안 쓴다"와 "죽었다"를 아무도 구분할 수 없다**는 것이다. 디렉터리 구조는 모든 축이 게임에 들어간 것처럼 보이는데 실행 경로는 25개만 지난다.

**재구현.** 제품의 `main.cpp` 는 익명 네임스페이스 안에 `class ReplicaBuffer` 를 갖고 있었다. `net/snapshot.hpp` 의 보간 버퍼와 같은 역할을 하는 73줄짜리 사본이다. 명령 중복 억제도, 재전송+누적 ACK 정리도 같은 파일에 인라인으로 다시 쓰여 있었다. 그리고 `net/` 의 그 헤더들은 **테스트만 링크했다.**

즉 테스트되는 구현과 배포되는 구현이 달랐다. 변이 테스트가 그것을 증명했다 — 제품 쪽 스냅샷 순서 보장을 지워도, 재전송 조건을 `if (false)` 로 바꿔도 **어떤 테스트도 실패하지 않았다.**

**조기 반환.** `--selftest` 는 창 프런트엔드보다 먼저 반환했고, 그 셀프테스트는 26줄이었다. 에셋 로드·밸런스 스크립트·창·입력·렌더·파티클·오디오·UDP 루프·복제·진행도·지표 전부가 ctest 가 한 번도 진입하지 않는 영역이었다.

세 발견은 하나의 원인을 공유한다. **`main.cpp` 가 두꺼웠고, 거기서 `<winsock2.h>` 와 `<windows.h>` 를 손에 쥘 수 있었다.**

원재료가 닿는 곳에 있으면 누구든 그 자리에서 다시 만든다. 재구현은 게으름이 아니라 구조의 결과다.

## 2. 문서에 적힌 규칙은 규칙이 아니다

세 발견에 대응하는 세 가지 처방이 있다.

| 처방 | 형태 | 어기면 |
|---|---|---|
| "진입점을 얇게 유지하자" | 문서 | 아무 일도 없다 |
| 코드 리뷰에서 확인 | 사람 | 놓친다 |
| **빌드가 실패한다** | 검사기 | 커밋 전에 멈춘다 |

앞의 둘을 이미 해 봤고 실패했다. 그래서 셋째다.

검사기를 만들 때의 기준도 이 실패에서 나왔다. **끌 수 있는 게이트는 게이트가 아니다.** 그래서 두 검사기 모두 옵션이 아니다 — Python 인터프리터를 찾지 못하면 구성 자체가 `FATAL_ERROR` 로 멈춘다.

```cmake
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_FOUND)
    message(FATAL_ERROR
        "계층 규칙 검사기(scripts/check_layers.py)를 돌릴 Python 3 인터프리터가 없다.\n"
        "이 검사는 끌 수 있는 옵션이 아니다 — 끌 수 있는 게이트는 게이트가 아니다.\n"
        "Python 3 를 설치하거나 -DPython3_EXECUTABLE=<경로> 로 지정하라.")
endif()
```

선택 의존으로 두면 없는 머신에서 게이트가 조용히 꺼지고, 그 순간 감사가 찾은 상태가 다시 합법이 된다.

## 3. 계층 규칙 여섯 줄

검사기가 강제하는 것은 여섯이다.

1. `games/` 의 어떤 TU 도 OS 헤더를 직접 인클루드할 수 없다.
2. `games/` 가 인클루드할 수 있는 시스템 헤더는 **화이트리스트뿐**이다.
3. `games/` 는 OS 타입 이름을 쓸 수 없다.
4. `games/` 는 OS 함수를 직접 호출할 수 없다.
5. `games/` 의 소스는 **150줄**을 넘을 수 없다.
6. `games/` 아래에는 **헤더 파일을 둘 수 없다**.

3번이 왜 따로 필요한지가 미묘하다. 인클루드를 막는 것만으로는 구멍이 남는다. `games/` 의 TU 는 `game/defense_app.hpp` → `platform/window.hpp` 를 통해 `<windows.h>` 를 **전이적으로** 보므로 `HWND` 같은 이름이 여전히 손에 닿는다. 그래서 이름 사용을 직접 막는다.

6번의 근거는 재구현 사건이다. `games/defense/` 아래에 헤더를 두면 그것은 "게임 폴더 안에만 있는 재사용 코드"이고, 테스트가 그것을 집어 들려면 인클루드 루트를 열어 줘야 한다. 그 통로가 열려 있는 한 "제품만 보는 코드"가 다시 생길 수 있다.

5번의 150줄은 게임 로직이 진입점에 눌러앉는 것을 막는다. 현재 두 진입점은 29줄과 18줄이다.

<!-- snippet: scripts/check_layers.py#check_source -->
```python
def check_source(path: Path, relative: str, limit: int) -> list[Finding]:
    findings: list[Finding] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    without_comments, code_only = blank_out(text)

    total = len(text.splitlines())
    if total > limit:
        findings.append(
            Finding(
                "ERROR",
                relative,
                1,
                f"{total}줄로 상한 {limit}줄을 넘었다. 프레임 루프·게임 로직은 include/ 로 옮겨라",
            )
        )

    include_lines = without_comments.splitlines()
    code_lines = code_only.splitlines()
    for number, line in enumerate(include_lines, 1):
        match = INCLUDE_RE.match(line)
        if not match:
            continue
        bracket, header = match.group(1), match.group(2)
        owner = FORBIDDEN_INCLUDES.get(header)
        if owner is not None:
            # ... 금지 헤더: 그 권한을 가진 계층을 함께 알려 준다
        elif bracket == "<" and header not in ALLOWED_SYSTEM_INCLUDES:
            # ... 화이트리스트 밖 시스템 헤더

    for number, line in enumerate(code_lines, 1):
        for name, pattern in CALL_RE.items():
            if any(is_direct_call(line, m.start()) for m in pattern.finditer(line)):
                # ... OS 함수 직접 호출
        for name, pattern in TYPE_RE.items():
            if pattern.search(line):
                # ... OS 타입 직접 사용
        for name, pattern in NAMESPACE_RE.items():
            if pattern.search(line):
                # ... OS 네임스페이스 직접 사용

    return findings
```

`blank_out` 이 **두 벌**을 돌려주는 것이 설계다. 앞의 것은 주석만 지운 것(`#include "헤더"` 를 읽어야 하므로 문자열을 남긴다), 뒤의 것은 주석과 리터럴을 모두 지운 것이다. 하나로 합칠 수 없다 — 인클루드 검사는 문자열이 필요하고, 호출 검사는 문자열이 방해된다.

## 4. 화이트리스트가 블랙리스트보다 중요하다

금지 목록(규칙 1)은 열려 있다. 아직 적지 않은 OS 헤더 하나면 뚫린다. 그래서 규칙 2가 진짜 문을 닫는다.

```python
ALLOWED_SYSTEM_INCLUDES = {
    "algorithm", "array", "cstddef", "cstdint", "cstdio", "cstring",
    "initializer_list", "limits", "optional", "span", "string",
    "string_view", "tuple", "type_traits", "utility", "vector",
}
```

`games/` 가 쓸 수 있는 시스템 헤더는 이 16개뿐이다. **순수 계산과 진단만 남는다.**

목록에서 빠진 것이 목록에 있는 것보다 많은 것을 말한다.

- `<fstream>`·`<filesystem>` 없음 — 파일은 `io/` 와 `platform/` 소유다.
- `<thread>`·`<chrono>` 없음 — 시간과 스레드는 엔진 소유다. 진입점이 프레임 페이싱을 할 수 없다는 뜻이다.
- `<cstdlib>` 없음 — `getenv`·`system` 이 거기 있다.

`<cstdio>` 만 예외다. 진입점이 사용법을 표준 오류로 찍어야 하기 때문이고, 대신 규칙 4가 같은 헤더의 `fopen`·`freopen` 을 따로 막는다.

이 목록이 유지 가능한 이유는 `games/` 가 실제로 필요로 하는 것이 몇 개뿐이기 때문이다. 목록이 커지기 시작하면 그것 자체가 진입점이 두꺼워지고 있다는 신호다.

금지 목록도 값을 낸다 — **위반 메시지가 대안을 함께 말하기** 때문이다.

```python
FORBIDDEN_INCLUDES = {
    "windows.h": "platform/window.hpp",
    "winsock2.h": "net/udp_transport.hpp",
    "fstream": "io/read_file.hpp",
    "csignal": "platform/process.hpp",
    # ...
}
```

"금지됐다"만 말하는 검사기는 사람을 막다른 길에 세운다. "그 권한은 `net/udp_transport.hpp` 에만 있다"는 다음 행동을 알려 준다.

## 5. 주석 속 socket( 을 위반으로 세지 않기

`socket(` 이라는 문자열이 코드에 나타났다고 전부 OS 호출은 아니다.

<!-- snippet: scripts/check_layers.py#is_direct_call -->
```python
def is_direct_call(line: str, start: int) -> bool:
    """`socket(` 이 정말 OS 호출인지, 남의 멤버·이름공간인지 가른다.

    `session.send(` · `transport->send(` 는 엔진 객체의 메서드다. 반대로
    `std::getenv(` · `::socket(` 는 전역 OS 함수를 부르는 것이므로 잡아야 한다.
    앞이 `X::` 인데 X 가 std 가 아니면 우리 계층의 이름이니 통과시킨다.
    """
    before = line[:start]
    if MEMBER_RE.search(before):
        return False
    qualifier = QUALIFIER_RE.search(before)
    if qualifier is not None:
        return qualifier.group(1) == "std"
    return True
```

세 갈래다. 앞에 `.` 이나 `->` 가 있으면 멤버 호출이므로 통과. 앞에 `X::` 가 있으면 `X` 가 `std` 일 때만 위반. 아무것도 없으면 전역 호출이므로 위반.

이 판정이 필요한 이유는 오탐이 게이트를 죽이기 때문이다. `network_.Send(...)` 를 위반으로 보고하는 검사기는 며칠 안에 무시당하고, 무시당하는 게이트는 게이트가 아니다.

같은 이유로 출력 인코딩에도 결정이 있다.

```python
if __name__ == "__main__":
    # 인코딩을 강제하지 않는다. UTF-8 로 고정하면 cp949 콘솔에서 MSBuild 가 받아
    # 적는 빌드 로그가 통째로 깨져 위반 메시지를 읽을 수 없다.
    sys.stdout.reconfigure(errors="replace")
```

MSBuild 는 자식 프로세스의 표준 출력을 콘솔 코드페이지로 디코딩한다. 파이썬이 UTF-8 로 쓰면 그 지점에서 되돌릴 수 없이 깨진다. **읽을 수 없는 실패 메시지는 무시당한다.**

## 6. 세 지점에서 돈다

<!-- snippet: CMakeLists.txt@layer-gate -->
```cmake
execute_process(COMMAND ${MEGA_LAYER_CHECK_COMMAND}
                RESULT_VARIABLE MEGA_LAYER_CHECK_RESULT
                OUTPUT_VARIABLE MEGA_LAYER_CHECK_OUTPUT
                ERROR_VARIABLE MEGA_LAYER_CHECK_OUTPUT)
if(NOT MEGA_LAYER_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "계층 규칙 위반(재설계안 A3):\n${MEGA_LAYER_CHECK_OUTPUT}")
endif()
message(STATUS "계층 규칙 OK: games/ 에 OS API 직접 호출 없음, 진입점 ≤ ${MEGA_ENTRY_LINE_LIMIT}줄")

add_custom_target(mega_layer_check ALL
    COMMAND ${MEGA_LAYER_CHECK_COMMAND}
    COMMENT "계층 규칙 검사: games/ 에 OS API 직접 호출이 없는가"
    VERBATIM)
add_test(NAME layer_rules COMMAND ${MEGA_LAYER_CHECK_COMMAND})
```

**구성 시 · 매 빌드 · ctest 세 번 돈다.** 각자 잡는 것이 다르다.

- **구성 시** — 즉시 실패. 새로 클론한 저장소가 이미 규칙을 어긴 상태면 빌드를 시작조차 하지 않는다.
- **매 빌드**(`mega_layer_check ALL`) — 이쪽이 본체다. 구성 시에만 돌면 규칙을 어긴 편집이 **다음 재구성 전까지 통과한다.** `add_dependencies(mega_core mega_layer_check)` 가 이것을 코어 빌드보다 먼저 돌게 한다.
- **ctest**(`layer_rules`) — 검사 결과가 테스트 목록에 나타난다. CI 요약에서 다른 검사와 같은 형태로 보인다.

검사기를 CMake 스크립트로 쓰지 않은 이유는 정확성이다. `file(STRINGS)` 의 결과를 `foreach` 로 순회하면 **빈 줄이 사라져** 위반의 줄 번호가 앞의 빈 줄 수만큼 밀린다. 위치를 틀리게 알려 주는 게이트는 무시당한다.

## 7. 도달성은 컴파일러에게 묻는다

"이 헤더가 제품에서 도달 가능한가"를 답하는 방법이 둘이다.

- **텍스트로 `#include` 를 훑는다.** 전처리기 조건부(`#if`)를 무시하므로 결과가 ±몇 개씩 어긋난다. 이 저장소에는 `MEGA_HAS_VULKAN` 과 `_WIN32` 가 실제로 갈리는 자리가 있다.
- **컴파일러가 실제로 연 파일 목록을 받는다.** 정확하다.

후자를 골랐다.

<!-- snippet: CMakeLists.txt#mega_capture_dependencies -->
```cmake
function(mega_capture_dependencies target)
    if(MSVC)
        set(depfile ${MEGA_REACHABILITY_DIR}/$<CONFIG>/${target}.json)
        target_compile_options(${target} PRIVATE
                               "SHELL:/sourceDependencies ${depfile}")
    else()
        set(depfile ${MEGA_REACHABILITY_DIR}/$<CONFIG>/${target}.d)
        target_compile_options(${target} PRIVATE -MD -MF ${depfile})
    endif()
    set_property(GLOBAL APPEND PROPERTY MEGA_PRODUCT_DEPFILES
                 "${target}=${depfile}")
endfunction()
```

MSVC 는 `/sourceDependencies` 로 JSON 을 남기고, GCC/Clang 은 `-MD -MF` 로 Makefile 의존성 규칙을 남긴다. 게이트는 두 형식을 첫 글자로 구분한다.

<!-- snippet: scripts/reachability.py#parse_msvc_source_dependencies -->
```python
def parse_msvc_source_dependencies(text):
    """MSVC /sourceDependencies JSON. Data.Includes 가 절대 경로 목록이다."""
    document = json.loads(text)
    data = document.get("Data", {})
    paths = list(data.get("Includes", []))
    # 헤더 유닛·모듈을 쓰게 되면 그쪽 경로도 도달로 친다(현재는 비어 있다).
    for entry in data.get("ImportedHeaderUnits", []):
        if isinstance(entry, dict):
            paths.append(entry.get("Header", ""))
        else:
            paths.append(entry)
    source = data.get("Source")
    if source:
        paths.append(source)
    return [p for p in paths if p]
```

이 입력을 쓰는 것의 결정적인 성질은 **게이트가 보는 집합과 실행 파일이 실제로 링크하는 코드가 어긋날 수 없다**는 것이다. 같은 컴파일이 만든 부산물이기 때문이다.

경로 비교에도 함정이 있다.

<!-- snippet: scripts/reachability.py#key -->
```python
def key(path):
    """대소문자·구분자·상대경로 차이를 흡수한 비교용 키.

    MSVC 는 도구마다 경로 대소문자를 다르게 남긴다(tlog 는 전부 대문자,
    /sourceDependencies 는 원래 표기). 대소문자 구분 비교를 하면 "도달 0개"라는
    거짓 음성이 조용히 나온다 - 이 게이트에서 가장 위험한 실패 모드다.
    """
    return os.path.normcase(os.path.realpath(path))
```

`realpath` 가 심볼릭 링크와 `..` 을 풀고, `normcase` 가 대소문자와 구분자를 정규화한다. 이것이 없으면 게이트는 "모든 헤더가 미도달"이라고 보고하거나 — 더 나쁘게 — 조용히 통과한다.

## 8. 모두 컴파일하되, 도달성은 제품에서만 잰다

감사가 지적한 상태에는 두 층이 있었다. 헤더가 **제품에서 도달하지 않는다**는 것과, 헤더가 **아예 컴파일되지 않는다**는 것.

둘째는 더 나쁘다. 컴파일되지 않는 헤더는 문법 오류가 있어도 초록이다. 그 상태에서는 "도달 불가"와 "깨진 코드"를 구분할 수 없다.

그래서 헤더 하나당 그 헤더만 인클루드하는 번역 단위를 생성한다.

<!-- snippet: CMakeLists.txt@core-units -->
```cmake
foreach(header ${MEGA_CORE_HEADERS})
    file(RELATIVE_PATH rel ${MEGA_CORE_INCLUDE_DIR} ${header})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" stem "${rel}")
    set(unit ${MEGA_CORE_UNIT_DIR}/unit_${stem}.cpp)
    set(unit_text "...\n#include \"${rel}\"\n#include \"${rel}\"\n")
    set(unit_old "")
    if(EXISTS ${unit})
        file(READ ${unit} unit_old)
    endif()
    if(NOT unit_old STREQUAL unit_text)
        file(WRITE ${unit} "${unit_text}")
    endif()
    list(APPEND MEGA_CORE_UNITS ${unit})
endforeach()
```

생성된 TU 는 같은 헤더를 **두 번** 인클루드한다. 그래서 이 TU 는 두 가지를 강제한다.

1. 헤더가 **자족적으로** 컴파일된다(필요한 것을 스스로 인클루드한다).
2. 인클루드 가드가 **멱등**이다(두 번 넣어도 재정의가 없다).

`CONFIGURE_DEPENDS` 글롭이 붙어 있으므로 헤더를 추가하면 재구성이 걸려 TU 가 자동으로 생긴다. **"컴파일된 적 없는 헤더"는 구조적으로 존재할 수 없다.**

내용이 같으면 다시 쓰지 않는 것(`if(NOT unit_old STREQUAL unit_text)`)은 불필요한 재빌드를 막기 위해서다. 그리고 헤더가 삭제되면 남은 고아 TU 를 지운다 — 지우지 않으면 없어진 헤더를 인클루드하는 TU 가 빌드를 깨뜨린다.

여기서 함정이 하나 생긴다. **`mega_core` 는 모든 헤더를 컴파일하므로, 그 타깃의 의존성 출력으로 도달성을 재면 "전부 도달 가능"이라는 거짓 초록이 나온다.**

그래서 게이트의 입력은 `mega_core` 가 아니라 제품 타깃 둘이다. `mega_defense` 는 `main.cpp` 하나만, `mega_defense_server` 는 `server_main.cpp` 하나만 컴파일하므로 **그 둘의 의존성 출력이 곧 제품 도달 집합**이다.

두 층의 역할이 이렇게 갈린다.

| 타깃 | 컴파일하는 것 | 답하는 질문 |
|---|---|---|
| `mega_core` | 헤더 99개 전부 + 구현 4개 | 이 헤더가 **컴파일되는가** |
| `mega_defense` · `mega_defense_server` | 진입점 TU 하나씩 | 이 헤더가 **제품에 도달하는가** |

## 9. 제3의 상태는 빌드 실패다

규칙 자체는 한 줄이다.

> `include/` 아래 모든 헤더는 제품에서 **도달 가능**하거나 등재부에 **이유와 함께 등재**돼야 한다. 제3의 상태 — 도달 불가인데 근거도 없는 헤더 — 는 빌드 실패다.

등재부는 마크다운 표다. 각 행이 헤더·도입 단계·사유 세 칸이다. 그 행 자체를 검증한다.

<!-- snippet: scripts/reachability.py#validate_entry -->
```python
def validate_entry(entry):
    """등재 항목 자체의 형식 검증. 반환값은 위반 메시지 목록."""
    problems = []
    if entry.stage not in VALID_STAGES:
        # ... 도입 단계가 정해진 집합 밖이다
    if len(entry.reason) < MIN_REASON_LENGTH:
        problems.append(
            "LAB_ONLY.md:{} `{}` - 사유가 {}자다. 최소 {}자. 어느 단계에서 "
            "어떤 게임 기능이 이 헤더를 쓰게 되는지 적어라.".format(
                entry.line, entry.header, len(entry.reason),
                MIN_REASON_LENGTH))
    for phrase in BANNED_REASON_PHRASES:
        if phrase in entry.reason:
            problems.append(
                "LAB_ONLY.md:{} `{}` - 사유에 '{}' 가 있다. 그것은 이유가 "
                "아니다.".format(entry.line, entry.header, phrase))
    if entry.stage == NO_STAGE and "계획 없음" not in entry.reason:
        # ... 도입 계획이 없으면 그 능력이 어디 있는지 적게 한다
    return problems
```

**금지 문구 목록**이 이 검사의 핵심이다.

```python
BANNED_REASON_PHRASES = [
    "아직 안 씀", "아직 안 쓴다", "아직 쓰지", "아직 사용하지",
    "나중에", "추후", "미정", "TODO", "이유 없음",
]
```

"아직 안 씀"은 이유가 아니라 **사실의 재진술**이다. 등재부가 그런 문장으로 채워지면 등재는 형식만 남고, 형식만 남은 게이트는 통과 도장이 된다. 최소 30자 요구도 같은 목적이다 — 짧은 사유는 대개 재진술이다.

도입 단계가 "없음"이면 사유에 "계획 없음"을 명시하고 **그 능력이 실제로 어디에 있는지** 적게 한다. 제품에 들어갈 계획이 없는 코드가 저장소에 있는 것 자체는 문제가 아니지만, 그 사실이 명시되지 않으면 다음 사람이 그것을 통합 대상으로 착각한다.

세 종류의 위반이 더 있다.

- **고아** — 도달 불가인데 등재도 없음. 제3의 상태.
- **유령 등재** — 등재부에 있는데 그런 파일이 없음. 삭제·이름 변경의 흔적.
- **낡은 등재** — 이제 제품에 도달하는데 랩 전용이라 적혀 있음.

셋째가 이 게이트를 **양방향**으로 만든다. 헤더를 제품에 편입할 때 등재를 지우지 않으면 빌드가 실패한다. 그래서 등재부가 자연스럽게 줄어든다.

## 10. 게이트가 조용히 꺼지는 것을 막는 장치

게이트의 가장 위험한 실패는 초록으로 잘못 도는 것이다. 셋을 막아 뒀다.

**의존성 출력이 없으면 실행 불가(종료 코드 2).** 제품 타깃을 빌드하지 않은 상태에서 게이트를 돌리면 "통과"가 아니라 "게이트를 실행할 수 없다"로 끝난다. 통과와 미실행이 같은 결과를 내면 게이트가 없는 것과 같다.

**도달 0개면 실행 불가.**

```python
if not reachable:
    print("도달성 게이트: 제품 도달 헤더가 0개다. 의존성 출력이 include/ 를 "
          "가리키지 않는다 - 게이트가 무력화된 상태이므로 통과시키지 않는다.")
    return 2
```

도달 0개는 거짓 음성일 가능성이 압도적으로 높다(경로 비교 실패 등). 그 상태에서는 모든 헤더가 "도달 불가"가 되므로 등재부만 완전하면 **통과한다.** 그 통과를 막는다.

**제품 타깃이 하나도 없으면 구성 실패.**

```cmake
get_property(MEGA_PRODUCT_DEPFILES GLOBAL PROPERTY MEGA_PRODUCT_DEPFILES)
if(NOT MEGA_PRODUCT_DEPFILES)
    message(FATAL_ERROR
        "도달성 게이트: 의존성 출력을 남기는 제품 타깃이 하나도 없다. "
        "제품 없이 계산한 도달성은 아무 의미가 없으므로 통과시키지 않는다.")
endif()
```

## 11. 리포트는 생성물이다

<!-- snippet: CMakeLists.txt@reach-gate -->
```cmake
add_custom_target(mega_reachability ALL
    COMMAND ${MEGA_REACHABILITY_COMMAND}
    COMMENT "도달성 게이트: include/ 의 모든 헤더가 제품 도달 또는 LAB_ONLY 등재인가"
    VERBATIM)
add_dependencies(mega_reachability mega_defense mega_defense_server)

add_test(NAME reachability_gate COMMAND ${MEGA_REACHABILITY_COMMAND})
```

게이트가 통과하면 `docs/PRODUCT_LAYERS.md` 를 쓴다. 계층별 도달 현황과 헤더 단위 표가 들어 있고, 맨 위에 이렇게 적혀 있다.

```text
**생성 파일이다. 직접 수정하지 마라.** `scripts/reachability.py` 가 빌드마다 다시 쓴다.
```

감사가 지적한 문제 중 하나는 "미통합 목록이 문서에서 축소돼 있다"였다. 손으로 관리하는 목록은 줄어든다 — 악의가 아니라, 항목을 지우는 것은 쉽고 추가하는 것은 잊히기 때문이다. **생성물은 줄일 수 없다.**

`write_if_changed` 로 내용이 같으면 mtime 을 건드리지 않는다. 매 빌드마다 파일이 갱신되면 불필요한 재빌드와 git 잡음이 생긴다.

같은 게이트가 곁다리로 하나 더 본다 — 저장소 최상위 README 의 코드 구조 트리가 실제 `include/` 하위 디렉터리와 같은 집합인가. 감사가 그 트리에서 실재하는 디렉터리 6개가 빠졌고 그중 하나(`serialize/`)가 제품 도달 계층인 상태를 찾았다. 트리를 보고 소스를 찾는 독자에게 그 계층은 존재하지 않는 것과 같다.

이 검사가 여기 사는 이유는 **입력이 같기 때문**이다 — `include/` 아래에 무엇이 있는가. 손으로 맞춰 두면 다음 디렉터리가 생길 때 다시 어긋난다.

## 12. 실패도 부재도 아닌 세 번째 결과

GPU 표시 경로에는 다른 종류의 문제가 있다. `--gpu-selftest` 는 실제 Vulkan 장치와 스왑체인을 요구한다.

두 선택지 모두 나쁘다.

- **ctest 에 등록하지 않는다** — 문서는 "자동 검증한다"고 적는데 ctest 에는 없는 상태로 돌아간다.
- **등록하고 실패시킨다** — GPU 없는 CI 러너에서 게이트가 상시 빨간불이 되고, 상시 빨간 게이트는 곧 무시당한다.

세 번째 선택지가 있다.

<!-- snippet: CMakeLists.txt@gpu-skip -->
```cmake
add_test(NAME mega_defense_gpu_selftest COMMAND mega_defense --gpu-selftest)
set_tests_properties(mega_defense_gpu_selftest PROPERTIES
                     TIMEOUT 120
                     SKIP_RETURN_CODE 77)
```

`if(Vulkan_FOUND)` 안에 넣지 않는다. 그러면 **테스트 목록 자체가 머신마다 달라져** "ctest 56개"라는 서술이 어느 머신에서는 틀리고, 무엇보다 GPU 가 없다는 사실이 결과에 아무 흔적도 남지 않는다.

대신 항상 등록하고, 실행할 수 없는 환경에서는 실행 파일이 77로 끝난다.

<!-- snippet: include/game/defense_app.hpp#DefenseGpuSelftestUnavailable -->
```cpp
inline int DefenseGpuSelftestUnavailable(const char* reason) {
    std::fprintf(stderr, "[gpu-selftest] SKIP: %s\n", reason);
    std::fflush(stderr);
    return kDefenseSkipExitCode;
}
```

77은 GNU automake 의 관례값이라 다른 도구도 같은 뜻으로 읽는다. 부재의 이유는 셋으로 나뉜다 — 이 빌드에 Vulkan 지원이 없다, 이 환경에 창 백엔드가 없다, 장치·스왑체인·셰이더를 준비하지 못했다.

**실패는 오직 "장치도 창도 있는데 present 가 깨졌을 때"뿐이다.**

부재 판정의 순서도 결정이다.

```cpp
#ifndef MEGA_HAS_VULKAN
    // 창을 열기 전에 답이 나오는 조건이다. 창부터 만들면 GPU 가 없는 환경에서
    // "창 생성 실패"라는 엉뚱한 이유가 먼저 보고된다.
    if (options.vulkan) { ... }
#endif
```

컴파일 시간에 답이 나오는 조건을 먼저 본다.

## 13. 실행과 관측

**구성.** 두 게이트가 구성 단계에서 이미 말을 한다.

```powershell
cmake -S . -B build
```

```text
-- 계층 규칙 OK: games/ 에 OS API 직접 호출 없음, 진입점 ≤ 150줄
-- mega_core: 헤더 99개(생성 TU 동수) + 구현 4개
-- mega_defense Vulkan presenter enabled
-- Configuring done (7.2s)
-- Generating done (4.5s)
```

`헤더 99개(생성 TU 동수) + 구현 4개` 가 8절의 상태다. 헤더온리 99개가 전부 컴파일되고, 실제 `.cpp` 는 넷뿐이다.

**계층 규칙.**

```powershell
python scripts\check_layers.py
```

```text
검사 TU 2개 / ERROR 0 / WARN 0
```

TU 가 둘이다 — `main.cpp` 29줄, `server_main.cpp` 18줄. 감사 시점의 `main.cpp` 는 익명 네임스페이스만 460줄 남짓이었고 전체는 700줄을 넘었다.

검사기가 실제로 무엇을 잡는지는 위반을 만들어 보면 된다. 진입점 사본에 `<winsock2.h>` 와 `<fstream>` 을 넣고 `socket()` 을 한 번 부른 뒤 그 디렉터리를 검사기에 물리면 이렇게 나온다.

```powershell
python scripts\check_layers.py --games-dir <위반 사본 디렉터리>
```

```text
ERROR	games/defense/main.cpp:19	OS 헤더 <winsock2.h> 를 직접 인클루드했다. 그 권한은 net/udp_transport.hpp 에만 있다
ERROR	games/defense/main.cpp:20	OS 헤더 <fstream> 를 직접 인클루드했다. 그 권한은 io/read_file.hpp 에만 있다
ERROR	games/defense/main.cpp:30	OS 타입 SOCKET 을 직접 썼다. include/ 계층의 중립 타입을 써라
ERROR	games/defense/main.cpp:30	OS 함수 socket() 를 직접 호출했다. net/ · platform/ · io/ 뒤로 옮겨라

검사 TU 2개 / ERROR 4 / WARN 0
games/ 는 OS API 를 직접 부를 수 없고 소스는 150줄을 넘을 수 없다(재설계안 A3).
```

줄 하나에 위반 둘(타입과 호출)이 걸린 것을 각각 보고한다. 그리고 각 메시지가 **그 권한을 가진 계층**을 함께 말한다.

**도달성.**

```powershell
python -X utf8 scripts\reachability.py --include-dir include --lab-only docs\LAB_ONLY.md `
  --dep mega_defense=build\reachability\Release\mega_defense.json `
  --dep mega_defense_server=build\reachability\Release\mega_defense_server.json `
  --readme-tree README.md
```

```text
도달성 게이트 - include/ 헤더 99개
  제품 mega_defense           도달 37개
  제품 mega_defense_server    도달 23개
  제품 도달 합계 41개 / 랩 전용 등재 58개 / 근거 없는 미도달 0개
  README 코드 구조 트리 대조: include/ 하위 디렉터리 21개
  게이트 통과: 제3의 상태 없음
```

감사 시점과 나란히 두면 이렇다.

| | 감사 시점 | 현재 |
|---|---|---|
| `include/` 헤더 | 89 | 99 |
| 제품 도달 | 25 | **41** |
| 근거 없는 미도달 | **64** | **0** |
| 등재된 랩 전용 | 0(등재부 없음) | 58 |

37 + 23 = 60 이 41 로 줄어드는 것은 두 제품이 공유하는 헤더(게임 규칙·와이어 포맷·콘텐츠 로더·수학·물리·io)가 19개이기 때문이다.

여기서 중요한 것은 **64 → 0 이 "게임이 부르게 만들었다"는 뜻이 아니라는 점**이다. 미도달 58개는 여전히 미도달이고, 달라진 것은 그 58개 전부에 도입 단계와 사유가 붙었다는 것이다. 게이트가 강제한 것은 통합이 아니라 **근거**다.

등재를 하나 지워 보면 게이트가 무엇을 하는지 보인다.

```text
  제품 도달 합계 41개 / 랩 전용 등재 57개 / 근거 없는 미도달 1개

도달성 게이트 실패 - 규칙 위반 1건 (재설계안 A2)
  `ecs/world.h` - 제품에서 도달 불가인데 docs/LAB_ONLY.md 에 등재되지 않았다. 제품이 호출하게 만들거나, 왜 제품에 없는지 등재하라.

규칙: include/ 아래 모든 헤더는 제품에서 도달 가능하거나 docs/LAB_ONLY.md 에 이유와 함께 등재돼야 한다.
      제3의 상태 - 도달 불가인데 근거도 없음 - 는 빌드 실패다.
```

**전수 검사.**

```powershell
ctest --test-dir build -C Release
```

```text
100% tests passed out of 56

Total Test time (real) =  12.76 sec
```

56의 구성은 이렇다.

```text
 1        layer_rules              계층 규칙 검사기
 2 ~ 53   test_*.cpp 52개          엔진 단위 · 게임 통합 · 프로세스 통합
54        mega_defense_selftest    제품의 진짜 프레임 루프를 헤드리스로
55        mega_defense_gpu_selftest  GPU present. 장치가 없으면 SKIP(77)
56        reachability_gate        도달성 + 등재부 + README 트리
```

게이트 셋이 테스트 목록 안에 있다는 것이 요점이다. **구조 검사와 동작 검사가 같은 형태로 보고된다.**

`-V` 로 돌리면 각 검사가 실제로 무엇을 찍는지 보인다.

```powershell
ctest --test-dir build -C Release -R "layer_rules|reachability_gate|mega_defense_selftest|mega_defense_gpu_selftest" -V
```

```text
1: 검사 TU 2개 / ERROR 0 / WARN 0
54: [selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external
55: [gpu-selftest] Vulkan present 5 frames: OK
56: 도달성 게이트 - include/ 헤더 99개
56:   게이트 통과: 제3의 상태 없음
100% tests passed out of 4
```

55번은 이 기계에 Vulkan SDK 와 GPU 가 있어 실제로 5프레임을 present 했다. 없는 기계에서는 같은 목록에 SKIP 으로 나타나며, 그 사실이 결과에 흔적을 남긴다.

**타임아웃도 검사의 일부다.** 몇 테스트에는 명시적 상한이 걸려 있는데, 그중 둘은 이유가 특별하다.

```cmake
set_tests_properties(test_tools_script PROPERTIES TIMEOUT 30)
set_tests_properties(test_defense_content PROPERTIES TIMEOUT 30)
```

이 둘은 "끝나지 않는 스크립트"를 실제로 인터프리터에 먹인다. 방어가 무너지면 이 테스트는 실패하는 게 아니라 **끝나지 않는다** — 그 상태를 실패로 바꿔 주는 것은 타임아웃뿐이다.

---

## 결론

이 단계에서 세운 것.

1. **계층 규칙 검사기** — 여섯 줄(OS 헤더·화이트리스트·OS 타입·OS 함수·150줄·헤더 금지). 화이트리스트가 목록을 닫고, 위반 메시지가 대안 계층을 함께 말한다.
2. **오탐 방지** — 주석·리터럴을 두 벌로 마스킹하고, 멤버 호출과 전역 호출을 가른다. 출력 인코딩까지 콘솔에 맞춘다. 읽히지 않거나 틀리는 게이트는 무시당한다.
3. **세 지점 실행** — 구성 시(즉시 실패) · 매 빌드(본체) · ctest(보고). 구성 시에만 돌면 재구성 전까지 위반이 통과한다.
4. **컴파일러 의존성 출력** — 텍스트 근사가 아니라 컴파일러가 실제로 연 파일 목록. 게이트가 보는 집합과 실행 파일이 링크하는 코드가 어긋날 수 없다.
5. **헤더당 생성 TU** — 헤더를 두 번 인클루드해 자족성과 가드 멱등성을 강제한다. 그러나 도달성은 그 타깃이 아니라 **제품 타깃**에서 잰다. 두 층의 질문이 다르다.
6. **제3의 상태는 빌드 실패** — 도달 또는 등재. 사유 최소 30자, 금지 문구 목록, 유령 등재와 낡은 등재까지 양방향으로 검사한다. 게이트가 강제한 것은 통합이 아니라 근거다.
7. **거짓 초록 방어 셋** — 의존성 출력 부재, 도달 0개, 제품 타깃 부재. 셋 다 통과가 아니라 "실행 불가"로 끝난다.
8. **생성 리포트** — 손으로 줄일 수 없는 목록. 같은 입력으로 README 코드 구조 트리도 대조한다.
9. **SKIP(77)** — 실패도 부재도 아닌 세 번째 결과. 테스트 목록은 머신과 무관하게 같고, GPU 부재가 결과에 흔적을 남긴다.

이 시리즈는 여기까지가 "지금 있는 게임이 어떻게 만들어졌는가"다. 그리고 여기까지 세운 것들이 앞으로 매번 되돌아올 기준선이다.

**현재 기준선.** 빌드 에러 0. `ctest` 56 전수 통과. `layer_rules` 통과(검사 TU 2개, ERROR 0). `reachability_gate` 통과(헤더 99, 제품 도달 41, 등재 58, 근거 없는 미도달 0). `mega_defense --selftest` = `CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external`. 시뮬은 결정적이라 이 수들은 같은 소스에서 다시 나온다.

현재 상태의 한계도 분명하다. 도달성 게이트는 헤더가 **제품에서 도달 가능한가**만 강제한다. 도달 가능한 코드가 실제로 **실행되는가**는 묻지 않는다 — 인클루드는 되지만 아무도 부르지 않는 함수, 부르지만 결과를 버리는 호출은 게이트를 그대로 통과한다. 그 구멍을 메우는 것은 게이트가 아니라 실행 경로를 지나는 테스트이고, 그것이 `test_defense_client_loop` 과 `test_defense_coop_process` 가 신설된 이유다. 그리고 등재부에 남은 58개는 여전히 게임이 부르지 않는 코드다.
