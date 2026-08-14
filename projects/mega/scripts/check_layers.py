"""games/ 가 계층 규칙(재설계안 A3)을 지키는지 검사한다.

규칙은 여섯 줄이다.

1. games/ 의 어떤 TU 도 OS 헤더를 직접 인클루드할 수 없다.
2. games/ 가 인클루드할 수 있는 시스템 헤더는 화이트리스트뿐이다.
3. games/ 는 OS 타입 이름을 쓸 수 없다(전이 인클루드로 손에 닿기 때문이다).
4. games/ 는 OS 함수를 직접 호출할 수 없다.
5. games/ 의 소스는 150줄을 넘을 수 없다.
6. games/ 아래에는 헤더 파일을 둘 수 없다.

왜 필요한가. 감사가 찾은 최악의 결함은 제품이 `main.cpp` 익명 네임스페이스에서
`ReplicaBuffer` 를 재구현했고 정작 `include/net/` 는 테스트만 링크했다는 것이다.
재구현이 가능했던 이유는 `main.cpp` 가 <winsock2.h> 를 들이마실 수 있었기 때문이다 —
원재료가 손에 닿는 곳에 있으면 누구든 그 자리에서 다시 만든다. 인클루드를 막으면
그 물리적 경로가 사라진다. 규율은 깨지지만 검사기는 깨지지 않는다.

2번(화이트리스트)이 1번(블랙리스트)보다 중요하다. 블랙리스트는 열려 있어서 아직
적지 않은 OS 헤더 하나면 뚫린다. games/ 가 필요로 하는 시스템 헤더는 실제로 몇
개뿐이므로 화이트리스트가 유지 가능하고, 그래야 규칙이 닫힌다.

사용:
    python scripts/check_layers.py
    python scripts/check_layers.py --games-dir <경로> --entry-line-limit 150
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GAMES_DIR = ROOT / "games"
ENTRY_LINE_LIMIT = 150

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl"}

# ── 규칙 1. 직접 인클루드가 금지된 OS 헤더 ───────────────────────────
# 소켓 · 창 · GPU · 오디오 장치 · 파일 · 프로세스 신호의 원재료. 이 권한은
# net/ · platform/ · io/ 만 갖는다.
FORBIDDEN_INCLUDES = {
    # 창 · OS 코어
    "windows.h": "platform/window.hpp",
    "windowsx.h": "platform/window.hpp",
    "shellapi.h": "platform/window.hpp",
    "commctrl.h": "platform/window.hpp",
    "libloaderapi.h": "platform/paths.hpp",
    "X11/Xlib.h": "platform/window.hpp",
    # 소켓
    "winsock.h": "net/udp_transport.hpp",
    "winsock2.h": "net/udp_transport.hpp",
    "ws2tcpip.h": "net/udp_transport.hpp",
    "mswsock.h": "net/udp_transport.hpp",
    "sys/socket.h": "net/udp_transport.hpp",
    "netinet/in.h": "net/udp_transport.hpp",
    "netinet/tcp.h": "net/udp_transport.hpp",
    "arpa/inet.h": "net/udp_transport.hpp",
    "netdb.h": "net/udp_transport.hpp",
    # 오디오 장치
    "mmsystem.h": "platform/audio_device.hpp",
    "mmeapi.h": "platform/audio_device.hpp",
    "audioclient.h": "platform/audio_device.hpp",
    "xaudio2.h": "platform/audio_device.hpp",
    "alsa/asoundlib.h": "platform/audio_device.hpp",
    # GPU · 표시
    "vulkan/vulkan.h": "platform/vk_present.hpp",
    "vulkan/vulkan_win32.h": "platform/vk_present.hpp",
    "d3d11.h": "platform/vk_present.hpp",
    "d3d12.h": "platform/vk_present.hpp",
    "dxgi.h": "platform/vk_present.hpp",
    "GL/gl.h": "platform/vk_present.hpp",
    "GL/glew.h": "platform/vk_present.hpp",
    # 파일 · 경로
    "fstream": "io/read_file.hpp",
    "filesystem": "platform/paths.hpp",
    "experimental/filesystem": "platform/paths.hpp",
    "io.h": "io/file.h",
    "direct.h": "platform/paths.hpp",
    "sys/stat.h": "io/file.h",
    "dirent.h": "platform/vfs.hpp",
    "fcntl.h": "io/file.h",
    # 프로세스 · 신호 · 스레드 원시 API
    "csignal": "platform/process.hpp",
    "signal.h": "platform/process.hpp",
    "unistd.h": "platform/process.hpp",
    "sys/wait.h": "platform/process.hpp",
    "sys/mman.h": "memory/arena.h",
    "pthread.h": "jobs/scheduler.h",
    "process.h": "platform/process.hpp",
    "dlfcn.h": "platform/paths.hpp",
}

# ── 규칙 2. games/ 가 인클루드해도 되는 시스템 헤더 ──────────────────
# 순수 계산과 진단만 남긴다. <cstdlib>(getenv·system)·<cstdio> 이외의 IO·
# <thread>·<chrono> 는 없다 — 시간·스레드·환경은 전부 engine/ 과 platform/ 소유다.
# <cstdio> 만 예외로 두는 이유는 진입점이 사용법을 stderr 로 찍어야 하기 때문이며,
# 규칙 4가 같은 헤더의 fopen·freopen 을 따로 막는다.
ALLOWED_SYSTEM_INCLUDES = {
    "algorithm",
    "array",
    "cstddef",
    "cstdint",
    "cstdio",
    "cstring",
    "initializer_list",
    "limits",
    "optional",
    "span",
    "string",
    "string_view",
    "tuple",
    "type_traits",
    "utility",
    "vector",
}

# ── 규칙 3. games/ 에서 쓸 수 없는 OS 타입 ───────────────────────────
# 인클루드 금지만으로는 이 구멍이 막히지 않는다. games/ TU 는
# game/defense_app.hpp → platform/window.hpp 를 통해 <windows.h> 를 **전이적으로**
# 보므로 HWND 같은 이름이 여전히 손에 닿는다. 이름 사용을 직접 막는다.
FORBIDDEN_TYPES = (
    "HWND", "HDC", "HINSTANCE", "HMODULE", "HANDLE", "HWAVEOUT",
    "WAVEHDR", "WAVEFORMATEX", "WNDCLASSW", "WNDCLASSA", "MSG",
    "LRESULT", "WPARAM", "LPARAM", "BITMAPINFO",
    "DWORD", "LPSTR", "LPWSTR", "LPCWSTR", "BOOL", "UINT", "HRESULT",
    "SOCKET", "WSADATA", "sockaddr", "sockaddr_in", "socklen_t", "fd_set",
    "VkInstance", "VkDevice", "VkSurfaceKHR", "VkSwapchainKHR", "VkResult",
)

# ── 규칙 4. games/ 에서 직접 부를 수 없는 OS 함수 ────────────────────
FORBIDDEN_CALLS = (
    "getenv", "putenv", "_dupenv_s", "system",
    "GetModuleFileNameA", "GetModuleFileNameW",
    "GetModuleHandleA", "GetModuleHandleW",
    "CreateWindowExA", "CreateWindowExW", "RegisterClassA", "RegisterClassW",
    "GetMessageA", "GetMessageW", "PeekMessageA", "PeekMessageW",
    "CreateFileA", "CreateFileW", "fopen", "fopen_s", "freopen", "freopen_s",
    "socket", "bind", "listen", "accept", "connect",
    "send", "sendto", "recv", "recvfrom", "select", "closesocket",
    "WSAStartup", "WSACleanup",
    "signal", "raise", "SetConsoleOutputCP",
    "waveOutOpen", "waveOutWrite",
    "vkCreateInstance", "vkCreateDevice",
)

FORBIDDEN_NAMESPACES = ("std::filesystem", "std::this_thread", "std::chrono")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
TYPE_RE = {
    name: re.compile(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])")
    for name in FORBIDDEN_TYPES
}
CALL_RE = {
    name: re.compile(rf"(?<![A-Za-z0-9_]){re.escape(name)}\s*\(")
    for name in FORBIDDEN_CALLS
}
NAMESPACE_RE = {
    name: re.compile(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])")
    for name in FORBIDDEN_NAMESPACES
}
QUALIFIER_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*::\s*$")
MEMBER_RE = re.compile(r"(?:\.|->)\s*$")


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


@dataclass
class Finding:
    level: str
    path: str
    line: int
    message: str


def blank_out(text: str) -> tuple[str, str]:
    """주석과 리터럴을 공백으로 덮되 줄·열 배치는 그대로 남긴다.

    두 벌을 돌려준다. 앞은 주석만 지운 것(#include 의 "헤더" 를 읽어야 하므로
    문자열을 남긴다), 뒤는 주석과 리터럴을 모두 지운 것(주석 속 'socket(' 이나
    사용법 문자열의 'HWND' 를 위반으로 오인하지 않기 위해서다).
    """
    without_comments: list[str] = []
    code_only: list[str] = []
    state = "code"
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        pair = text[index : index + 2]
        if state == "code":
            if pair == "//":
                state = "line_comment"
                without_comments.append("  ")
                code_only.append("  ")
                index += 2
                continue
            if pair == "/*":
                state = "block_comment"
                without_comments.append("  ")
                code_only.append("  ")
                index += 2
                continue
            if char == '"':
                state = "string"
                without_comments.append(char)
                code_only.append(" ")
                index += 1
                continue
            if char == "'":
                state = "char"
                without_comments.append(char)
                code_only.append(" ")
                index += 1
                continue
            without_comments.append(char)
            code_only.append(char)
            index += 1
            continue

        if state == "line_comment":
            if char == "\n":
                state = "code"
                without_comments.append(char)
                code_only.append(char)
            else:
                without_comments.append(" ")
                code_only.append(" ")
            index += 1
            continue

        if state == "block_comment":
            if pair == "*/":
                state = "code"
                without_comments.append("  ")
                code_only.append("  ")
                index += 2
                continue
            keep = char if char == "\n" else " "
            without_comments.append(keep)
            code_only.append(keep)
            index += 1
            continue

        # 문자열·문자 리터럴.
        terminator = '"' if state == "string" else "'"
        if char == "\\" and index + 1 < length:
            escaped = text[index + 1]
            without_comments.append(char + escaped)
            code_only.append(" " + (escaped if escaped == "\n" else " "))
            index += 2
            continue
        if char == terminator:
            state = "code"
            without_comments.append(char)
            code_only.append(" ")
            index += 1
            continue
        without_comments.append(char)
        code_only.append(char if char == "\n" else " ")
        index += 1

    return "".join(without_comments), "".join(code_only)


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
            findings.append(
                Finding(
                    "ERROR",
                    relative,
                    number,
                    f"OS 헤더 <{header}> 를 직접 인클루드했다. 그 권한은 {owner} 에만 있다",
                )
            )
        elif bracket == "<" and header not in ALLOWED_SYSTEM_INCLUDES:
            findings.append(
                Finding(
                    "ERROR",
                    relative,
                    number,
                    f"허용 목록에 없는 시스템 헤더 <{header}> 다. "
                    "games/ 는 순수 계산 헤더만 쓴다. 필요하면 include/ 의 계층 뒤로 옮겨라",
                )
            )

    for number, line in enumerate(code_lines, 1):
        for name, pattern in CALL_RE.items():
            if any(is_direct_call(line, m.start()) for m in pattern.finditer(line)):
                findings.append(
                    Finding(
                        "ERROR",
                        relative,
                        number,
                        f"OS 함수 {name}() 를 직접 호출했다. net/ · platform/ · io/ 뒤로 옮겨라",
                    )
                )
        for name, pattern in TYPE_RE.items():
            if pattern.search(line):
                findings.append(
                    Finding(
                        "ERROR",
                        relative,
                        number,
                        f"OS 타입 {name} 을 직접 썼다. include/ 계층의 중립 타입을 써라",
                    )
                )
        for name, pattern in NAMESPACE_RE.items():
            if pattern.search(line):
                findings.append(
                    Finding(
                        "ERROR",
                        relative,
                        number,
                        f"OS 네임스페이스 {name} 을 직접 썼다. include/ 계층 뒤로 옮겨라",
                    )
                )

    return findings


def collect(games_dir: Path, limit: int) -> tuple[list[Finding], int]:
    findings: list[Finding] = []
    sources: list[Path] = []
    for path in sorted(games_dir.rglob("*")):
        if not path.is_file():
            continue
        relative = f"games/{path.relative_to(games_dir).as_posix()}"
        if path.suffix in HEADER_SUFFIXES:
            findings.append(
                Finding(
                    "ERROR",
                    relative,
                    1,
                    "games/ 아래에 헤더가 있다. 재사용되는 코드는 include/ 가 소유한다",
                )
            )
            continue
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        sources.append(path)
        findings.extend(check_source(path, relative, limit))
    return findings, len(sources)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="mega 계층 규칙 검사(재설계안 A3)")
    parser.add_argument("--games-dir", default=str(GAMES_DIR))
    parser.add_argument("--entry-line-limit", type=int, default=ENTRY_LINE_LIMIT)
    args = parser.parse_args(argv)

    games_dir = Path(args.games_dir).resolve()
    if not games_dir.is_dir():
        print(f"ERROR\t{games_dir}:1\tgames 디렉터리를 찾지 못했다")
        return 1

    findings, source_count = collect(games_dir, args.entry_line_limit)
    findings.sort(key=lambda f: (f.path, f.line, f.message))

    for finding in findings:
        print(f"{finding.level}\t{finding.path}:{finding.line}\t{finding.message}")

    errors = sum(f.level == "ERROR" for f in findings)
    warnings = sum(f.level == "WARN" for f in findings)
    print(f"\n검사 TU {source_count}개 / ERROR {errors} / WARN {warnings}")
    if errors:
        print(
            "games/ 는 OS API 를 직접 부를 수 없고 소스는 "
            f"{args.entry_line_limit}줄을 넘을 수 없다(재설계안 A3)."
        )
    return 1 if errors else 0


if __name__ == "__main__":
    # 인코딩을 강제하지 않는다. UTF-8 로 고정하면 cp949 콘솔에서 MSBuild 가 받아
    # 적는 빌드 로그가 통째로 깨져 위반 메시지를 읽을 수 없다. 플랫폼 기본 인코딩을
    # 그대로 쓰되, 한글을 못 담는 로캘에서 죽지만 않게 errors 만 완화한다.
    sys.stdout.reconfigure(errors="replace")
    raise SystemExit(main())
