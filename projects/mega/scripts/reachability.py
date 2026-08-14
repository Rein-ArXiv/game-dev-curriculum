#!/usr/bin/env python3
# reachability.py — 도달성 게이트(재설계안 A2).
#
#   include/ 아래 모든 헤더는 정확히 둘 중 하나여야 한다.
#     (1) 제품 실행 파일에서 전이적으로 **도달 가능**하거나
#     (2) docs/LAB_ONLY.md 에 **이유와 함께 등재**되거나.
#   제3의 상태 — 도달 불가인데 근거도 없는 헤더 — 는 빌드 실패다.
#
# 왜 필요한가. 감사(REVIEW_MEGA_2026-08.md §1)는 89개 헤더 중 64개가 제품에서
# 도달조차 하지 않는데도 테스트 51개가 전부 통과하는 상태를 찾았다. 그 상태에서는
# "이 코드는 아직 안 쓴다"와 "이 코드는 죽었다"를 아무도 구분할 수 없다. 구분을
# 사람의 규율이 아니라 빌드 실패로 강제하는 것이 이 스크립트다.
#
# 도달성은 **컴파일러가 실제로 연 파일 목록**에서만 계산한다. 텍스트로 #include 를
# 훑는 근사는 전처리기 조건부(#if)를 무시해 ±2개씩 어긋난다. 입력은 제품 타깃이
# 컴파일될 때 컴파일러가 직접 남긴 의존성 출력이다.
#
#   MSVC        : /sourceDependencies <파일>  → JSON (Data.Includes)
#   GCC / Clang : -MD -MF <파일>              → Makefile 의존성 규칙
#
# 중요 — mega_core 의 의존성으로 계산하면 안 된다. mega_core 는 헤더 하나당 생성
# TU 를 만들어 87개 헤더를 **전부** 컴파일하므로(CMakeLists.txt 참조) 거기서 재면
# "전부 도달 가능"이라는 거짓 green 이 나온다. 제품 타깃(mega_defense ·
# mega_defense_server)은 진입점 TU 하나씩만 컴파일하므로 그 둘의 의존성 출력이
# 곧 제품 도달 집합이다.
#
# 곁다리로 하나 더 본다 — README 의 코드 구조 트리가 실제 include/ 와 같은가
# (--readme-tree). 감사 #27 은 그 트리가 실재하는 디렉터리 6개를 빠뜨렸고 그중
# serialize/ 는 제품 도달 계층이라, 트리를 보고 소스를 찾는 독자가 제품 코드를
# 찾지 못하는 상태를 지적했다. 손으로 맞춰 두면 다음 디렉터리가 생길 때 다시
# 어긋난다 — 어긋날 수 없게 만드는 것은 대조뿐이다. 이 검사가 여기 사는 이유는
# 입력이 같기 때문이다: include/ 아래에 무엇이 있는가.
#
# 사용:
#   python scripts/reachability.py
#       --include-dir <저장소>/include
#       --lab-only    <저장소>/docs/LAB_ONLY.md
#       --dep mega_defense=<빌드>/reachability/Release/mega_defense.json
#       --dep mega_defense_server=<빌드>/reachability/Release/mega_defense_server.json
#       [--report      <저장소>/docs/PRODUCT_LAYERS.md]
#       [--json        <빌드>/reachability.json]
#       [--readme-tree <저장소>/README.md]
#
# 종료 코드: 0 = 게이트 통과, 1 = 규칙 위반, 2 = 게이트를 실행할 수 없음(입력 없음).

import argparse
import json
import os
import re
import sys


def _use_console_encoding():
    """빌드 로그에서 한글이 깨지지 않게 콘솔 코드페이지에 맞춰 출력한다.

    MSBuild 는 자식 프로세스의 stdout 을 콘솔 코드페이지로 디코딩한다. 파이썬이
    UTF-8 로 쓰면 그 지점에서 되돌릴 수 없이 깨진다. 읽을 수 없는 실패 메시지는
    무시당하고, 무시당하는 게이트는 게이트가 아니다.
    """
    if sys.platform != "win32":
        return
    try:
        import ctypes
        code_page = ctypes.windll.kernel32.GetConsoleOutputCP()
    except Exception:
        return
    if not code_page:
        return
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="cp{}".format(code_page),
                               errors="replace")
        except Exception:
            pass


_use_console_encoding()

# ── LAB_ONLY.md 등재 규칙 ────────────────────────────────────────────
# 도입 단계는 REDESIGN_MEGA_2026-08.md 의 단계 이름이어야 한다. "없음" 은 제품에
# 들어갈 계획이 없다는 뜻이고, 그때는 사유에 "계획 없음" 이 명시돼야 한다.
VALID_STAGES = {"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "P9", "없음"}
NO_STAGE = "없음"
MIN_REASON_LENGTH = 30

# "아직 안 씀"은 이유가 아니다. 어느 단계에서 어떤 게임 기능이 이 헤더를 쓰게
# 되는지, 또는 제품에 들어갈 계획이 없는지를 적어야 한다.
BANNED_REASON_PHRASES = [
    "아직 안 씀",
    "아직 안 쓴다",
    "아직 쓰지",
    "아직 사용하지",
    "나중에",
    "추후",
    "미정",
    "TODO",
    "이유 없음",
]

HEADER_SUFFIXES = (".h", ".hpp", ".hh", ".hxx")


def key(path):
    """대소문자·구분자·상대경로 차이를 흡수한 비교용 키.

    MSVC 는 도구마다 경로 대소문자를 다르게 남긴다(tlog 는 전부 대문자,
    /sourceDependencies 는 원래 표기). 대소문자 구분 비교를 하면 "도달 0개"라는
    거짓 음성이 조용히 나온다 - 이 게이트에서 가장 위험한 실패 모드다.
    """
    return os.path.normcase(os.path.realpath(path))


# ── include/ 헤더 목록 ───────────────────────────────────────────────
def scan_headers(include_dir):
    """include/ 아래 모든 헤더를 {비교키: posix 상대경로} 로 돌려준다."""
    found = {}
    for root, dirs, files in os.walk(include_dir):
        dirs.sort()
        for name in sorted(files):
            if not name.endswith(HEADER_SUFFIXES):
                continue
            absolute = os.path.join(root, name)
            relative = os.path.relpath(absolute, include_dir)
            found[key(absolute)] = relative.replace(os.sep, "/")
    return found


# ── 컴파일러 의존성 출력 파싱 ────────────────────────────────────────
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


def parse_make_depfile(text):
    """GCC/Clang -MD -MF 출력. `target: a b \\<개행> c` 형태."""
    text = text.replace("\\\n", " ").replace("\\\r\n", " ")
    paths = []
    for line in text.splitlines():
        if ":" in line:
            # 드라이브 문자(C:)를 규칙 구분자로 오인하지 않도록 뒤에서 찾는다.
            head, _, tail = line.partition(":")
            if len(head) == 1 and tail[:1] in ("\\", "/"):
                _, _, tail = tail.partition(":")
            line = tail
        # 이스케이프된 공백을 되살린 뒤 토큰으로 자른다.
        line = line.replace("\\ ", "\x00")
        for token in line.split():
            token = token.replace("\x00", " ").strip()
            if token and token != "\\":
                paths.append(token)
    return paths


def read_dependency_file(path):
    with open(path, "r", encoding="utf-8-sig", errors="replace") as handle:
        text = handle.read()
    stripped = text.lstrip()
    if stripped.startswith("{"):
        return parse_msvc_source_dependencies(text)
    return parse_make_depfile(text)


# ── docs/LAB_ONLY.md 파싱 ────────────────────────────────────────────
class Entry:
    def __init__(self, header, stage, reason, line):
        self.header = header
        self.stage = stage
        self.reason = reason
        self.line = line


SEPARATOR_CELL = re.compile(r"^:?-{2,}:?$")
BACKTICKED = re.compile(r"^`([^`]+)`$")


def parse_lab_only(path):
    """표에서 (헤더, 도입 단계, 사유) 행만 뽑는다. 산문 줄은 무시한다."""
    entries = []
    problems = []
    if not os.path.exists(path):
        problems.append(
            "{}: 파일이 없다. 도달 불가 헤더를 등재할 곳이 없으면 게이트를 "
            "실행할 수 없다.".format(path))
        return entries, problems

    with open(path, "r", encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    for number, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 3:
            continue
        if all(SEPARATOR_CELL.match(c or "-") for c in cells):
            continue
        matched = BACKTICKED.match(cells[0])
        if not matched:
            continue  # 표 머리글("헤더" 등)은 백틱이 없으므로 여기서 걸러진다.
        entries.append(Entry(matched.group(1).strip().replace("\\", "/"),
                             cells[1], cells[2], number))
    return entries, problems


def validate_entry(entry):
    """등재 항목 자체의 형식 검증. 반환값은 위반 메시지 목록."""
    problems = []
    if entry.stage not in VALID_STAGES:
        problems.append(
            "LAB_ONLY.md:{} `{}` - 도입 단계 '{}' 는 허용되지 않는다. "
            "{} 중 하나여야 한다.".format(
                entry.line, entry.header, entry.stage,
                " · ".join(sorted(VALID_STAGES))))
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
        problems.append(
            "LAB_ONLY.md:{} `{}` - 도입 단계가 '{}' 이면 사유에 '계획 없음' 을 "
            "명시하고 그 능력이 실제로 어디에 있는지 적어라.".format(
                entry.line, entry.header, NO_STAGE))
    return problems


# ── README 코드 구조 트리 대조(감사 #27) ─────────────────────────────
TREE_DIRECTORY = re.compile(r"([A-Za-z0-9_+.-]+)/")


def include_subdirectories(include_dir):
    return {name for name in os.listdir(include_dir)
            if os.path.isdir(os.path.join(include_dir, name))}


def readme_tree_directories(readme_path):
    """README 트리에서 `include/` 바로 아래로 적힌 디렉터리 이름들.

    트리는 들여쓰기가 구조다. `include/` 줄의 들여쓰기보다 깊은 줄이 그 자식이고,
    같거나 얕은 줄이 나오면 끝난다. 한 줄에 `db/ live/` 처럼 여러 개를 묶어 적을
    수 있으므로 줄 단위가 아니라 이름 단위로 뽑는다.

    반환: (이름 집합, 문제 메시지 목록). `include/` 줄을 못 찾으면 문제로 본다 —
    빈 집합을 조용히 통과시키면 트리를 통째로 지운 README 가 게이트를 통과한다.
    """
    if not os.path.exists(readme_path):
        return set(), ["{}: 파일이 없다. 대조할 코드 구조 트리가 없다.".format(
            readme_path)]

    with open(readme_path, "r", encoding="utf-8") as handle:
        lines = handle.read().splitlines()

    names = set()
    found = False
    index = 0
    while index < len(lines):
        stripped = lines[index].strip()
        if stripped == "include/":
            found = True
            base = len(lines[index]) - len(lines[index].lstrip())
            index += 1
            while index < len(lines):
                line = lines[index]
                if not line.strip():
                    break
                indent = len(line) - len(line.lstrip())
                if indent <= base:
                    break
                names.update(TREE_DIRECTORY.findall(line.strip()))
                index += 1
            break
        index += 1

    if not found:
        return set(), [
            "{}: 코드 구조 트리에서 `include/` 줄을 찾지 못했다. 트리가 없으면 "
            "실제 디렉터리와 대조할 대상이 없다.".format(readme_path)]
    return names, []


def check_readme_tree(readme_path, include_dir):
    """README 트리와 실제 include/ 하위 디렉터리를 양방향으로 대조한다."""
    documented, problems = readme_tree_directories(readme_path)
    if problems:
        return problems
    actual = include_subdirectories(include_dir)
    label = os.path.basename(readme_path)
    violations = []
    for name in sorted(actual - documented):
        violations.append(
            "{} 코드 구조 트리 - `include/{}/` 가 실재하는데 트리에 없다. "
            "트리를 보고 소스를 찾는 독자에게 이 계층은 존재하지 않는 것과 "
            "같다.".format(label, name))
    for name in sorted(documented - actual):
        violations.append(
            "{} 코드 구조 트리 - `include/{}/` 가 트리에 적혀 있는데 실재하지 "
            "않는다. 삭제·이름 변경된 디렉터리의 서술이 남아 있다.".format(
                label, name))
    return violations


# ── 리포트 생성(재설계안 A4) ─────────────────────────────────────────
def top_level(header):
    return header.split("/", 1)[0] if "/" in header else "(루트)"


def build_report(include_dir, headers, per_target, reachable, registry):
    all_headers = sorted(headers.values())
    lab_only = {e.header: e for e in registry}
    targets = sorted(per_target)

    groups = {}
    for header in all_headers:
        groups.setdefault(top_level(header), []).append(header)

    out = []
    out.append("# 제품이 호출하는 계층")
    out.append("")
    out.append("**생성 파일이다. 직접 수정하지 마라.** `scripts/reachability.py` 가"
               " 빌드마다 다시 쓴다.")
    out.append("손으로 고쳐도 다음 빌드에 덮어써지고, 내용이 현실과 어긋나면"
               " 그 전에 도달성 게이트가 빌드를 실패시킨다.")
    out.append("")
    out.append("출처는 제품 타깃이 컴파일될 때 컴파일러가 남긴 의존성 출력"
               "(MSVC `/sourceDependencies`, GCC/Clang `-MD`)이다.")
    out.append("텍스트로 `#include` 를 훑은 추정치가 아니라 컴파일러가 실제로 연"
               " 파일 목록이다.")
    out.append("")
    out.append("## 요약")
    out.append("")
    out.append("| 항목 | 수 |")
    out.append("|---|---|")
    out.append("| `include/` 헤더 전체 | {} |".format(len(all_headers)))
    out.append("| 제품 도달 | {} |".format(len(reachable)))
    out.append("| 랩 전용(`docs/LAB_ONLY.md` 등재) | {} |".format(len(lab_only)))
    out.append("| 근거 없는 미도달 | 0 (있으면 빌드가 실패한다) |")
    out.append("")
    out.append("제품 진입점별 도달 수:")
    out.append("")
    out.append("| 제품 타깃 | 도달 헤더 |")
    out.append("|---|---|")
    for target in targets:
        out.append("| `{}` | {} |".format(target, len(per_target[target])))
    out.append("")

    out.append("## 계층별 도달 현황")
    out.append("")
    out.append("| 계층 | 도달 / 전체 |")
    out.append("|---|---|")
    for group in sorted(groups):
        members = groups[group]
        hit = sum(1 for h in members if h in reachable)
        out.append("| `{}/` | {} / {} |".format(group, hit, len(members)))
    out.append("")

    out.append("## 제품 도달 헤더")
    out.append("")
    header_row = "| 헤더 | " + " | ".join("`{}`".format(t) for t in targets) + " |"
    out.append(header_row)
    out.append("|---|" + "---|" * len(targets))
    for header in all_headers:
        if header not in reachable:
            continue
        marks = ["O" if header in per_target[t] else "-" for t in targets]
        out.append("| `{}` | {} |".format(header, " | ".join(marks)))
    out.append("")

    out.append("## 랩 전용 헤더 (제품 미도달, 등재됨)")
    out.append("")
    out.append("전체 목록과 각 항목의 사유는 [`LAB_ONLY.md`](LAB_ONLY.md) 에 있다."
               " 여기서는 도입 단계별 집계만 둔다.")
    out.append("")
    stages = {}
    for entry in registry:
        stages.setdefault(entry.stage, []).append(entry.header)
    out.append("| 도입 단계 | 헤더 수 | 헤더 |")
    out.append("|---|---|---|")
    for stage in sorted(stages, key=lambda s: (s == NO_STAGE, s)):
        members = sorted(stages[stage])
        out.append("| {} | {} | {} |".format(
            stage, len(members),
            " ".join("`{}`".format(m) for m in members)))
    out.append("")
    return "\n".join(out) + "\n"


def write_if_changed(path, text):
    """내용이 같으면 mtime 을 건드리지 않는다(불필요한 재빌드·git 잡음 방지)."""
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", newline="") as handle:
            if handle.read() == text:
                return False
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return True


# ── 본체 ─────────────────────────────────────────────────────────────
def main(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--include-dir", required=True)
    parser.add_argument("--lab-only", required=True)
    parser.add_argument("--dep", action="append", default=[],
                        metavar="TARGET=FILE",
                        help="제품 타깃이 남긴 컴파일러 의존성 출력")
    parser.add_argument("--report", default=None,
                        help="도달성 리포트를 쓸 경로(재설계안 A4)")
    parser.add_argument("--json", default=None,
                        help="기계 판독용 결과를 쓸 경로")
    parser.add_argument("--readme-tree", default=None,
                        help="코드 구조 트리를 실제 include/ 와 대조할 README 경로")
    args = parser.parse_args(argv)

    include_dir = os.path.abspath(args.include_dir)
    if not os.path.isdir(include_dir):
        print("도달성 게이트: include 디렉터리가 없다: {}".format(include_dir))
        return 2

    headers = scan_headers(include_dir)
    if not headers:
        print("도달성 게이트: {} 아래에서 헤더를 하나도 찾지 못했다.".format(
            include_dir))
        return 2

    # 1. 제품 타깃의 의존성 출력에서 도달 집합을 계산한다.
    per_target = {}
    for spec in args.dep:
        target, _, dep_path = spec.partition("=")
        if not dep_path:
            print("도달성 게이트: --dep 는 TARGET=FILE 형식이어야 한다: "
                  "{}".format(spec))
            return 2
        if not os.path.exists(dep_path):
            print("도달성 게이트를 실행할 수 없다 - 제품 타깃 '{}' 의 의존성 "
                  "출력이 없다:\n  {}\n제품 타깃을 먼저 빌드해야 한다."
                  .format(target, dep_path))
            return 2
        hits = set()
        for raw in read_dependency_file(dep_path):
            candidate = key(os.path.join(os.path.dirname(dep_path), raw))
            if candidate in headers:
                hits.add(headers[candidate])
        per_target[target] = hits

    if not per_target:
        print("도달성 게이트를 실행할 수 없다 - --dep 가 하나도 주어지지 않았다. "
              "제품 타깃 없이 도달성을 계산하면 의미가 없다.")
        return 2

    reachable = set()
    for hits in per_target.values():
        reachable |= hits
    unreachable = set(headers.values()) - reachable

    # 도달 0개는 거짓 음성일 가능성이 압도적으로 높다(경로 비교 실패 등).
    # 그 상태로 통과시키면 게이트가 조용히 무력화된다.
    if not reachable:
        print("도달성 게이트: 제품 도달 헤더가 0개다. 의존성 출력이 include/ 를 "
              "가리키지 않는다 - 게이트가 무력화된 상태이므로 통과시키지 않는다.")
        return 2

    # 2. 등재부를 읽는다.
    registry, problems = parse_lab_only(os.path.abspath(args.lab_only))
    if problems:
        for message in problems:
            print("  " + message)
        return 2

    seen = {}
    violations = []
    for entry in registry:
        violations.extend(validate_entry(entry))
        if entry.header in seen:
            violations.append(
                "LAB_ONLY.md:{} `{}` - {}행에 이미 등재돼 있다(중복)."
                .format(entry.line, entry.header, seen[entry.header]))
        seen[entry.header] = entry.line

    known = set(headers.values())
    registered = set(seen)

    # 3-a. 제3의 상태: 도달 불가인데 등재도 없음.
    orphans = sorted(unreachable - registered)
    # 3-b. 유령 등재: 존재하지 않는 파일.
    ghosts = sorted(registered - known)
    # 3-c. 낡은 등재: 이미 제품에 도달하는데 랩 전용이라 적혀 있음.
    stale = sorted(registered & reachable)

    for header in orphans:
        violations.append(
            "`{}` - 제품에서 도달 불가인데 docs/LAB_ONLY.md 에 등재되지 않았다. "
            "제품이 호출하게 만들거나, 왜 제품에 없는지 등재하라.".format(header))
    for header in ghosts:
        violations.append(
            "LAB_ONLY.md:{} `{}` - include/ 아래에 그런 파일이 없다. 삭제·이름 "
            "변경된 헤더의 등재가 남아 있다.".format(seen[header], header))
    for header in stale:
        violations.append(
            "LAB_ONLY.md:{} `{}` - 이제 제품에서 도달 가능하다. 랩 전용 등재를 "
            "지워라.".format(seen[header], header))

    # 3-d. README 코드 구조 트리 ↔ 실제 include/ 하위 디렉터리(감사 #27).
    tree_directories = 0
    if args.readme_tree:
        readme_path = os.path.abspath(args.readme_tree)
        violations.extend(check_readme_tree(readme_path, include_dir))
        tree_directories = len(include_subdirectories(include_dir))

    # 4. 결과 출력.
    print("도달성 게이트 - include/ 헤더 {}개".format(len(known)))
    for target in sorted(per_target):
        print("  제품 {:<22} 도달 {}개".format(target, len(per_target[target])))
    print("  제품 도달 합계 {}개 / 랩 전용 등재 {}개 / 근거 없는 미도달 {}개"
          .format(len(reachable), len(registered & known), len(orphans)))
    if args.readme_tree:
        print("  README 코드 구조 트리 대조: include/ 하위 디렉터리 {}개"
              .format(tree_directories))

    if violations:
        print("")
        print("도달성 게이트 실패 - 규칙 위반 {}건 (재설계안 A2)".format(
            len(violations)))
        for message in violations:
            print("  " + message)
        print("")
        print("규칙: include/ 아래 모든 헤더는 제품에서 도달 가능하거나 "
              "docs/LAB_ONLY.md 에 이유와 함께 등재돼야 한다.")
        print("      제3의 상태 - 도달 불가인데 근거도 없음 - 는 빌드 실패다.")
        print("      README 코드 구조 트리는 실제 include/ 하위 디렉터리와 "
              "정확히 같은 집합이어야 한다.")
        return 1

    if args.report:
        report = build_report(include_dir, headers, per_target, reachable,
                              registry)
        if write_if_changed(os.path.abspath(args.report), report):
            print("  리포트 갱신: {}".format(args.report))

    if args.json:
        payload = {
            "headers": sorted(known),
            "reachable": sorted(reachable),
            "labOnly": {e.header: {"stage": e.stage, "reason": e.reason}
                        for e in registry},
            "perTarget": {t: sorted(h) for t, h in per_target.items()},
        }
        write_if_changed(
            os.path.abspath(args.json),
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n")

    print("  게이트 통과: 제3의 상태 없음")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
