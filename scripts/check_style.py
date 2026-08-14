"""VeLog 본문이 STYLE_GUIDE.md의 기계 검증 가능한 규칙을 지키는지 검사한다."""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DOC_DIRS = [
    "base",
    "algorithms",
    "languages",
    "parts",
    "system",
    "network",
    "math",
    "physics",
    "ai",
    "graphics",
    "db",
    "engine",
    "live_service",
    "platform",
    "tools",
]

INTERNAL_LINK_RE = re.compile(r"\]\((?!https?://|#)[^)]+\.md(?:#[^)]+)?\)")
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
AMBIGUOUS_FORWARD_RE = re.compile(
    r"(?:다음|후속)\s*단계(?:는|가|를|의|인|에서)|"
    r"(?:다음|추후|후속)[^.]{0,40}(?:다룬다|살펴본다|설명한다|구현한다|증명한다|소개한다|몫이다)|"
    r"\b\d+\s*절(?:에서|의)[^.]{0,50}"
    r"(?:다룬다|살펴본다|설명한다|구현한다|증명한다|소개한다|맡는다|몫이다|토대다|자세히)|"
    r"(?:다음|후속)[^.]{0,30}(?:주제|토픽|토대|출발점)",
    re.IGNORECASE,
)
BROKEN_PROSE_RE = re.compile(
    r"이 글(?:는|가|를|와)|이 설명의 \(|이 범위를 확장하면 다룬다|이와 이어지는 주제는|"
    r"^들이\s|^다룬\s|^심화 렌더링 영역\s"
)
NUMBERED_H3_RE = re.compile(r"^###\s+\d+\.")
THEORY_LAB_RE = re.compile(r"\b[a-z0-9]+_lab\b", re.IGNORECASE)


@dataclass
class Finding:
    level: str
    path: Path
    line: int
    message: str


# 검수 보고서는 기사가 아니다 — 들어가며·목차·더 읽을거리 절이 없고, H3 에 번호를 쓰고,
# 다른 시리즈의 파일명을 인용하는 것이 이 문서들의 정상 형태다. 기사 규격으로 검사하면
# 상시 ERROR 상태가 되어 진짜 스타일 위반이 그 안에 묻힌다.
REPORT_PREFIXES = ("REVIEW_", "REDESIGN_")


def is_report(path: Path) -> bool:
    return path.name.startswith(REPORT_PREFIXES)


def documents(args: list[str]) -> list[Path]:
    targets = args or DOC_DIRS
    result: list[Path] = []
    for target in targets:
        path = ROOT / target
        if path.is_dir():
            result.extend(
                sorted(
                    p for p in path.glob("*.md")
                    if p.name != "README.md" and not is_report(p)
                )
            )
        elif path.is_file() and path.suffix == ".md":
            # 명시적으로 지정하면 보고서도 검사한다 (의도적 확인용)
            result.append(path)
    return result


def check_file(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    rel = path.relative_to(ROOT)

    required = {"## 들어가며": 0, "## 목차": 0, "## 결론": 0}
    hr_count = 0
    in_fence = False
    fence_start = 0
    display_math_tokens = 0
    first_display_math_line = 0

    for number, line in enumerate(lines, 1):
        if line in required:
            required[line] += 1
        if re.fullmatch(r"---\s*", line):
            hr_count += 1

        fence = re.match(r"^```(.*)$", line)
        if fence:
            if not in_fence:
                in_fence = True
                fence_start = number
                if not fence.group(1).strip():
                    findings.append(Finding("ERROR", rel, number, "코드 블록 시작에 언어가 없다"))
            else:
                in_fence = False
            continue

        if in_fence:
            continue

        # Markdown emphasis가 "다음 *절*" 같은 참조를 숨기지 않게 한다.
        reference_line = line.replace("*", "").replace("~", "")

        math_tokens_on_line = line.count("$$")
        if math_tokens_on_line:
            if first_display_math_line == 0:
                first_display_math_line = number
            display_math_tokens += math_tokens_on_line

        if INTERNAL_LINK_RE.search(line):
            findings.append(Finding("ERROR", rel, number, "다른 Markdown 문서로 직접 링크한다"))
        if ARTICLE_REFERENCE_RE.search(reference_line):
            findings.append(Finding("ERROR", rel, number, "다른 회차나 글을 자연어로 참조한다"))
        if SERIES_PATH_RE.search(line):
            findings.append(Finding("ERROR", rel, number, "다른 시리즈의 디렉터리 이름을 직접 참조한다"))
        if AMBIGUOUS_FORWARD_RE.search(reference_line):
            findings.append(Finding("WARN", rel, number, "후속 글 예고인지 같은 글의 처리 단계인지 문맥 확인이 필요하다"))
        if BROKEN_PROSE_RE.search(line):
            findings.append(Finding("ERROR", rel, number, "자동 정규화로 손상됐을 가능성이 높은 문장이다"))
        if path.parent.name not in {"math", "physics"} and NUMBERED_H3_RE.match(line):
            findings.append(Finding("ERROR", rel, number, "math/physics 밖에서 H3에 번호를 붙였다"))
        if THEORY_LAB_RE.search(line):
            findings.append(Finding("ERROR", rel, number, "이론 본문에서 lab을 언급한다"))

    if in_fence:
        findings.append(Finding("ERROR", rel, fence_start, "닫히지 않은 코드 블록이다"))
    if display_math_tokens % 2 != 0:
        findings.append(
            Finding(
                "ERROR",
                rel,
                first_display_math_line,
                f"display math 구분자($$)가 {display_math_tokens}개로 닫히지 않았다",
            )
        )
    if hr_count != 3:
        findings.append(Finding("ERROR", rel, 1, f"가로줄이 {hr_count}개다(필수 3개)"))
    for heading, count in required.items():
        if count != 1:
            findings.append(Finding("ERROR", rel, 1, f"{heading} 헤딩이 {count}개다(필수 1개)"))

    return findings


def main() -> int:
    files = documents(sys.argv[1:])
    findings = [finding for path in files for finding in check_file(path)]

    for finding in findings:
        print(f"{finding.level}\t{finding.path}:{finding.line}\t{finding.message}")

    errors = sum(f.level == "ERROR" for f in findings)
    warnings = sum(f.level == "WARN" for f in findings)
    print(f"\n검사 문서 {len(files)}개 / ERROR {errors} / WARN {warnings}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
