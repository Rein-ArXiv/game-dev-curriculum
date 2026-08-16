#!/usr/bin/env python3
"""Validate mini-project step documents without theory-only restrictions."""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_snippets  # noqa: E402  문서 스니펫 ↔ 소스 대조(PROJECTS_PLAN 완료 기준 7).


ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = ROOT / "projects" / "mini"
# 완료 기준 5의 정당한 예외 등재부. 없으면 등재가 하나도 없는 것으로 본다
# (파일을 지우면 예외가 조용히 사라지는 게 아니라 미달이 한꺼번에 드러난다).
VERIFY_EXEMPT_PATH = PROJECT_ROOT / "VERIFY_EXEMPT.md"

ARTICLE_REFERENCE_RE = re.compile(
    r"회차|(?:다음|이전|앞선|추후|후속|별도)\s*"
    r"(?:(?:글|편|파트|part|문서|주제|토픽)\b|"
    r"(?:장|절)(?:들)?(?=\s|에서|은|의|을|이|로|[.,):]|$))|"
    r"별도\s*주제로\s*(?:이어진다|분리된다)",
    re.IGNORECASE,
)
SERIES_PATH_RE = re.compile(
    r"`(?:base|algorithms|languages|parts|system|network|math|physics|ai|graphics|"
    r"multiplayer|db|engine|live_service|platform|tools)/"
    r"(?:`|(?:part\d+|stage\d+|README)[^`]*`)",
    re.IGNORECASE,
)
INTERNAL_MARKDOWN_LINK_RE = re.compile(r"\]\((?!https?://|#)[^)]+\.md(?:#[^)]+)?\)")
FORWARD_PROMISE_RE = re.compile(
    r"(?:다음|후속)\s*단계(?:는|가|를|의|인|에서)|"
    r"(?:다음|추후|후속)[^.]{0,40}(?:다룬다|살펴본다|설명한다|구현한다|증명한다|소개한다|몫이다)|"
    r"\b\d+\s*절(?:에서|의)[^.]{0,50}"
    r"(?:다룬다|살펴본다|설명한다|구현한다|증명한다|소개한다|맡는다|몫이다|토대다|자세히)|"
    r"(?:다음|후속)[^.]{0,30}(?:주제|토픽|토대|출발점)|"
    # `이후 단계는`, `이후 barrier, queue 단계는` — 조사를 요구해 회고적 표현과 가른다.
    # `이후 BVH 빌드 단계 시각화` 처럼 조사 없는 명사 수식은 예고가 아니므로 일부러 뺀다.
    r"(?:이후|추후)\s*(?:[^.\n]{0,25}?\s)?단계(?:는|가|를|의|에서|에|까지|부터)|"
    # `다음 스텝` 류. 본문의 `600 스텝`, `매 스텝` 같은 시뮬레이션 타임스텝은
    # 숫자·관형어가 앞에 오므로 걸리지 않는다.
    r"(?:다음|이후|후속|추후)\s*(?:스텝|step)\b",
    re.IGNORECASE,
)

# `Step 4 의 토픽`(예고)과 `Step 2 에서 만든 것`(회고)은 문법이 아니라 *번호* 로 갈린다.
# 자기 step 번호보다 큰 숫자를 가리키면 그것은 반드시 아직 없는 글이다.
#
# 뒤의 부정 전방탐색은 `main step 4 번 모이면` 같은 *세는 말* 을 걷어낸다.
# 그것은 문서 참조가 아니라 시뮬레이션 스텝의 횟수다.
STEP_NUMBER_RE = re.compile(
    r"(?:\bstep|스텝|단계)\s*(\d+)\s*(?:[~\-–]\s*(\d+))?"
    r"(?!\s*(?:번|회|개|차례|차|줄|배|초|분|시간|프레임|픽셀|바이트|ms|Hz))",
    re.IGNORECASE,
)
OWN_STEP_RE = re.compile(r"^step(\d+)", re.IGNORECASE)
OPENING_FENCE_RE = re.compile(r"^```(.*)$")

# ── PROJECTS_PLAN 완료 기준 5 ────────────────────────────────────────────────
# "step 종료 시 실행 명령과 관측 가능한 결과가 있다."
#
# 두 조각을 따로 본다. (a) 자동 검증을 *실행하는* 명령이 있는가. (b) 그 명령이 무엇을
# 내는지가 있는가. 명령만 있고 기대 출력이 없으면 독자는 자기 결과가 맞는지 모른다.

# 명령이 있을 수 있는 펜스. `text` 가 들어 있는 이유는 프롬프트를 붙인 콘솔 기록을
# `text` 로 적는 문서가 있기 때문이다.
SHELL_FENCE_LANGS = {
    "bash",
    "sh",
    "shell",
    "console",
    "powershell",
    "pwsh",
    "cmd",
    "bat",
    "text",
    "txt",
    "output",
    "log",
}
# 관측 출력을 담는 펜스. 명령 펜스(`bash`·`powershell`)는 여기 없다 — 명령 뒤에
# 명령이 또 나오는 것은 출력이 아니다.
OUTPUT_FENCE_LANGS = {"text", "txt", "console", "output", "log"}

# `$ ctest ...` 의 프롬프트. `$` 뒤 공백을 요구해 PowerShell 변수 `$server = ...` 와 가른다.
PROMPT_RE = re.compile(r"^\s*(?:\$|PS[^>\n]*>)\s+")
# 줄 끝 계속 문자. PowerShell 은 백틱, POSIX 셸은 역슬래시.
CONTINUATION_RE = re.compile(r"(?:`|\\)$")
# 경로로 직접 실행하는 테스트 실행 파일. 경로 구분자를 *요구하는* 것이 핵심이다.
# 그래야 출력 줄 `db_tests: all passed` 나 트리 그림의 `tests/` 가 명령으로 읽히지 않는다.
TEST_BINARY_RE = re.compile(
    r"^(?:\./|\.\\|[A-Za-z]:[/\\])?[\w.\-]*[/\\][\w.\-/\\]*test[\w.\-]*(?:\.exe)?$",
    re.IGNORECASE,
)
# `dotnet` 이 테스트를 도는 형태. `dotnet build Api.csproj` 와 `dotnet run --project Api.csproj`
# 는 제품을 띄우는 것이지 검증이 아니므로 제외된다.
DOTNET_TEST_RE = re.compile(
    r"(?:^|\s)test\b|[\w.]*tests?\.csproj|[/\\]tests?[/\\]", re.IGNORECASE
)
# 산문으로 기대 출력을 적은 형태("기대 출력은 `... Passed` 다")를 인정하기 위한 표지.
# 표지만으로는 부족하다 — 인용된 것이 *검증의 출력* 이어야 한다. 그래서 백틱 안의 내용까지
# 본다. 이게 없으면 `패널의 `draw submissions` 줄에도 출력된다` 같은 GUI 설명이 통과한다.
OUTPUT_PROSE_RE = re.compile(r"출력|찍힌다|찍는다|stdout|콘솔")
INLINE_CODE_RE = re.compile(r"`([^`]+)`")
OUTPUT_QUOTE_RE = re.compile(r"passed|failed|tests?\b|checks?\b", re.IGNORECASE)
# 명령 뒤 관측 출력을 찾는 범위의 상한. 절 경계(`## ` 제목, `---`)가 먼저 오면 거기서 끊는다.
OUTPUT_WINDOW_LINES = 60

EXEMPT_ROW_RE = re.compile(r"^\|(.+)\|$")
# mega 의 `docs/LAB_ONLY.md` 와 같은 목록. `아직` 을 통째로 막으면
# "판정 대상이 아직 없다" 같은 *구조적 이유* 까지 걸린다 — 막을 것은 `아직 안 썼다` 쪽이다.
EXEMPT_BANNED_RE = re.compile(r"나중에|추후|미정|TODO|아직\s*안", re.IGNORECASE)
EXEMPT_MIN_REASON = 30

IMPLEMENTATION_LANGS = {
    "c",
    "cpp",
    "c++",
    "cmake",
    "csharp",
    "cs",
    "glsl",
    "hlsl",
    "java",
    "javascript",
    "js",
    "python",
    "rust",
    "sh",
    "bash",
    "powershell",
}


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def own_step(path: Path) -> int | None:
    matched = OWN_STEP_RE.match(path.name)
    return int(matched.group(1)) if matched else None


def forward_step_reference(line: str, current: int | None) -> int | None:
    """자기 step 번호보다 뒤를 가리키는 숫자가 있으면 그 번호를 돌려준다."""
    if current is None:
        return None
    for matched in STEP_NUMBER_RE.finditer(line):
        numbers = [int(group) for group in matched.groups() if group]
        if numbers and max(numbers) > current:
            return max(numbers)
    return None


def fence_language(line: str) -> str:
    """` ```powershell ` 같은 여는 줄에서 언어 이름만 뽑는다."""
    fence = OPENING_FENCE_RE.match(line)
    if not fence:
        return ""
    info = fence.group(1).strip().lower()
    return info.split(maxsplit=1)[0] if info else ""


def iter_fences(lines: list[str]):
    """코드 펜스마다 (여는 줄, 닫는 줄, 언어)를 1-based 로 돌려준다."""
    start = 0
    language = ""
    in_fence = False
    for number, line in enumerate(lines, start=1):
        if not OPENING_FENCE_RE.match(line):
            continue
        if not in_fence:
            in_fence = True
            start = number
            language = fence_language(line)
        else:
            in_fence = False
            yield start, number, language
    # 닫히지 않은 펜스는 validate() 가 따로 error 로 만든다.


def logical_lines(lines: list[str], start: int, end: int) -> list[tuple[int, str]]:
    """펜스 본문을 계속 문자(``\\`` · `` ` ``)로 이어 붙인 논리 줄로 만든다."""
    merged: list[tuple[int, str]] = []
    buffer = ""
    anchor = 0
    for number in range(start + 1, end):
        raw = lines[number - 1]
        text = raw.rstrip()
        if not buffer:
            anchor = number
        if CONTINUATION_RE.search(text):
            buffer += CONTINUATION_RE.sub("", text).rstrip() + " "
            continue
        merged.append((anchor, (buffer + text).strip()))
        buffer = ""
    if buffer:
        merged.append((anchor, buffer.strip()))
    return merged


def automatic_verification(command: str) -> bool:
    """이 명령 줄이 통과/실패로 갈리는 검증을 실제로 돌리는가."""
    stripped = PROMPT_RE.sub("", command).strip()
    if not stripped:
        return False
    head = stripped.split()[0]
    if head.lower() == "ctest":
        return True
    if head.lower() == "dotnet":
        return bool(DOTNET_TEST_RE.search(stripped))
    # 벤치마크는 수치를 뱉지만 통과·실패로 갈리지 않는다. 이름에 test 가 있어도 제외한다.
    if "bench" in head.lower():
        return False
    return bool(TEST_BINARY_RE.match(head))


def has_inline_output(entries: list[tuple[int, str]], index: int) -> bool:
    """같은 펜스 안에서 명령 뒤에 출력이 이어지는가(프롬프트 있는 콘솔 기록)."""
    if not any(PROMPT_RE.match(text) for _, text in entries):
        return False  # 명령만 나열한 펜스다. 뒤 줄도 전부 명령이다.
    return any(
        text.strip() and not PROMPT_RE.match(text) for _, text in entries[index + 1 :]
    )


def has_following_output(lines: list[str], fence_end: int) -> bool:
    """명령 펜스 뒤 같은 절 안에 관측 출력이 있는가."""
    number = fence_end + 1
    limit = min(len(lines), fence_end + OUTPUT_WINDOW_LINES)
    while number <= limit:
        line = lines[number - 1]
        stripped = line.strip()
        if stripped.startswith("## ") or stripped == "---":
            return False  # 절이 끝났다.
        if OPENING_FENCE_RE.match(line):
            if fence_language(line) in OUTPUT_FENCE_LANGS:
                return True
            closing = next(
                (n for n in range(number + 1, len(lines) + 1)
                 if OPENING_FENCE_RE.match(lines[n - 1])),
                len(lines),
            )
            number = closing + 1  # 명령·구현 펜스는 건너뛴다.
            continue
        # 산문으로 기대 출력을 인용한 형태. 표지 + 검증 출력으로 보이는 인용을 함께 요구한다.
        if OUTPUT_PROSE_RE.search(stripped) and any(
            OUTPUT_QUOTE_RE.search(quoted) for quoted in INLINE_CODE_RE.findall(stripped)
        ):
            return True
        number += 1
    return False


def criterion5(path: Path, lines: list[str]) -> tuple[bool, list[str]]:
    """완료 기준 5를 검사한다. (충족 여부, error 목록)."""
    rel = relative(path)
    commands: list[tuple[int, str, bool]] = []
    for start, end, language in iter_fences(lines):
        if language not in SHELL_FENCE_LANGS:
            continue
        entries = logical_lines(lines, start, end)
        for index, (number, text) in enumerate(entries):
            if not automatic_verification(text):
                continue
            observed = has_inline_output(entries, index) or has_following_output(lines, end)
            commands.append((number, PROMPT_RE.sub("", text).strip(), observed))

    if not commands:
        return False, [
            f"{rel}: 완료 기준 5 — 자동 검증을 실행하는 명령이 없습니다. "
            f"`ctest`·테스트 실행 파일·`dotnet` 테스트 실행 중 하나를 코드 펜스에 두거나 "
            f"`{relative(VERIFY_EXEMPT_PATH)}` 에 이유와 함께 등재해야 합니다."
        ]
    if any(observed for _, _, observed in commands):
        return True, []

    number, text, _ = commands[-1]
    return False, [
        f"{rel}:{number}: 완료 기준 5 — `{text}` 뒤에 관측 출력이 없습니다. "
        f"무엇이 나와야 하는지를 출력 펜스나 기대 출력 인용으로 적거나 "
        f"`{relative(VERIFY_EXEMPT_PATH)}` 에 이유와 함께 등재해야 합니다."
    ]


def load_exemptions() -> tuple[dict[str, int], list[str]]:
    """예외 등재부를 읽는다. {문서 상대경로: 등재 줄번호} 와 등재부 자체의 error."""
    entries: dict[str, int] = {}
    errors: list[str] = []
    if not VERIFY_EXEMPT_PATH.exists():
        return entries, errors

    rel = relative(VERIFY_EXEMPT_PATH)
    in_fence = False
    for number, line in enumerate(
        VERIFY_EXEMPT_PATH.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if OPENING_FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        matched = EXEMPT_ROW_RE.match(line.strip())
        if not matched:
            continue
        cells = [cell.strip() for cell in matched.group(1).split("|")]
        if len(cells) != 3 or set(cells[0]) <= {"-", ":"}:
            continue  # 표 구분선이나 규칙 설명의 표다.
        document, reason, manual = cells
        path_match = re.fullmatch(r"`([^`]+)`", document)
        if not path_match:
            continue  # 등재부 앞부분의 설명 표. 문서 칸은 반드시 백틱 경로다.
        target = path_match.group(1)

        if target in entries:
            errors.append(f"{rel}:{number}: `{target}` 이 두 번 등재됐습니다.")
            continue
        entries[target] = number
        if not (PROJECT_ROOT / target).is_file():
            errors.append(
                f"{rel}:{number}: 등재된 `{target}` 이 없습니다. "
                f"경로는 `projects/mini` 기준입니다."
            )
        if len(reason) < EXEMPT_MIN_REASON:
            errors.append(
                f"{rel}:{number}: `{target}` 의 사유가 {len(reason)}자입니다. "
                f"{EXEMPT_MIN_REASON}자 이상으로 왜 이 step 에서 통과·실패가 갈리는 검증을 "
                f"만들 수 없는지 적어야 합니다."
            )
        if EXEMPT_BANNED_RE.search(reason):
            errors.append(
                f"{rel}:{number}: `{target}` 의 사유가 미준수의 서술입니다. "
                f"`나중에`·`추후`·`미정`·`TODO`·`아직 안` 류는 이유가 아닙니다."
            )
        if not manual:
            errors.append(
                f"{rel}:{number}: `{target}` 에 사람이 확인하는 방법이 비어 있습니다."
            )
    return entries, errors


def validate(path: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    rel = relative(path)
    current_step = own_step(path)

    for heading in ("## 들어가며", "## 목차", "## 결론"):
        count = sum(line.strip() == heading for line in lines)
        if count != 1:
            errors.append(f"{rel}: `{heading}`가 {count}개입니다. 정확히 1개여야 합니다.")

    horizontal_rules = sum(line.strip() == "---" for line in lines)
    if horizontal_rules != 3:
        errors.append(f"{rel}: `---`가 {horizontal_rules}개입니다. 정확히 3개여야 합니다.")

    first_content = next((line.strip() for line in lines if line.strip()), "")
    if first_content != "## 들어가며":
        errors.append(f"{rel}: 첫 본문 제목은 `## 들어가며`여야 합니다.")

    in_fence = False
    has_implementation_code = False
    for line_number, line in enumerate(lines, start=1):
        fence = OPENING_FENCE_RE.match(line)
        if fence:
            if not in_fence:
                language = fence.group(1).strip().lower()
                if not language:
                    errors.append(f"{rel}:{line_number}: 코드 블록 언어가 없습니다.")
                if language.split(maxsplit=1)[0] in IMPLEMENTATION_LANGS:
                    has_implementation_code = True
                in_fence = True
            else:
                in_fence = False
            continue

        if in_fence:
            continue

        if line.startswith("# "):
            errors.append(f"{rel}:{line_number}: H1은 사용하지 않습니다.")
        reference_line = line.replace("*", "").replace("~", "")
        # `~` 는 강조 제거용으로 지우지만, step 번호 검사에서는 `Step 1~7` 의
        # 범위 구분자라 지우면 `Step 17` 로 읽힌다. 그래서 따로 둔다.
        step_line = line.replace("*", "")
        if ARTICLE_REFERENCE_RE.search(reference_line):
            errors.append(
                f"{rel}:{line_number}: 특정 글·회차·단계를 가리키지 말고 현재 코드 상태를 직접 설명해야 합니다."
            )
        if SERIES_PATH_RE.search(line) or INTERNAL_MARKDOWN_LINK_RE.search(line):
            errors.append(
                f"{rel}:{line_number}: 다른 문서의 파일명이나 경로를 직접 참조하지 않습니다."
            )
        if FORWARD_PROMISE_RE.search(reference_line):
            errors.append(
                f"{rel}:{line_number}: 후속 구현 예고 대신 현재 단계의 한계를 기술해야 합니다."
            )
        ahead = forward_step_reference(step_line, current_step)
        if ahead is not None:
            errors.append(
                f"{rel}:{line_number}: 아직 없는 step {ahead}을 가리킵니다"
                f"(이 문서는 step {current_step}). 지난 step 참조는 허용됩니다."
            )

    if in_fence:
        errors.append(f"{rel}: 닫히지 않은 코드 블록이 있습니다.")
    if not has_implementation_code:
        warnings.append(f"{rel}: 구현 언어 코드 블록이 없습니다.")

    return errors, warnings


def main() -> int:
    documents = sorted(PROJECT_ROOT.glob("*/articles/*.md"))
    all_errors: list[str] = []
    all_warnings: list[str] = []

    exemptions, exempt_errors = load_exemptions()
    all_errors.extend(exempt_errors)
    exempt_rel = relative(VERIFY_EXEMPT_PATH)

    snippet_anchors = 0
    unverified: list[str] = []
    stale_exemptions = 0
    for document in documents:
        errors, warnings = validate(document)
        all_errors.extend(errors)
        all_warnings.extend(warnings)

        key = document.relative_to(PROJECT_ROOT).as_posix()
        satisfied, criterion5_errors = criterion5(
            document, document.read_text(encoding="utf-8").splitlines()
        )
        if key in exemptions:
            if satisfied:
                # 낡은 등재. 문서가 기준을 만족하는데 예외로 남아 있으면 표가 현실보다 뒤처진다.
                stale_exemptions += 1
                all_errors.append(
                    f"{exempt_rel}:{exemptions[key]}: `{key}` 은 이제 완료 기준 5를 "
                    f"만족합니다. 등재를 지워야 합니다."
                )
        elif not satisfied:
            unverified.append(key)
            all_errors.extend(criterion5_errors)

        snippets = check_snippets.check_document_full(document)
        all_errors.extend(snippets.errors)
        all_warnings.extend(snippets.warnings)
        snippet_anchors += snippets.anchors

    for message in all_errors:
        print(f"ERROR: {message}")
    for message in all_warnings:
        print(f"WARN: {message}")

    if unverified:
        by_lab: dict[str, int] = {}
        for key in unverified:
            by_lab[key.split("/", 1)[0]] = by_lab.get(key.split("/", 1)[0], 0) + 1
        summary = ", ".join(f"{lab} {count}" for lab, count in sorted(by_lab.items()))
        print(f"\n완료 기준 5 미달 {len(unverified)}편 / {len(documents)}편 — {summary}")

    print(
        f"\nChecked {len(documents)} project documents "
        f"({snippet_anchors} snippet anchor(s), "
        f"{len(exemptions) - stale_exemptions} verification exemption(s)): "
        f"{len(all_errors)} error(s), {len(all_warnings)} warning(s)."
    )
    return 1 if all_errors else 0


if __name__ == "__main__":
    sys.exit(main())
