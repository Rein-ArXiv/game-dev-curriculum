#!/usr/bin/env python3
"""velog 업로드용 원고를 조립한다.

velog 는 Markdown 안의 mermaid 펜스를 렌더하지 않는다. 그래서 본문에 인라인으로
들어 있는 다이어그램은 업로드하면 코드 덩어리로 보인다. 이 스크립트는 저장소의
본문(.md)을 읽어 mermaid 펜스를 미리 렌더해 둔 PNG 참조로 바꾸고, 저장소 안에서만
의미가 있는 표시(스니펫 앵커 주석, 문서 간 상대 링크)를 걷어낸 사본을
`velog_export/` 아래에 만든다. 원본은 건드리지 않는다.

사용법:
    python -X utf8 scripts/export_velog.py

변환 규칙:
    (a) k 번째 mermaid 펜스 전체를
        `![{stem} diagram {k:02d}](diagrams/{stem}_diagram_{k:02d}.png)` 한 줄로 바꾼다.
        해당 PNG 가 없으면 경고를 내고 펜스를 그대로 둔다(원고가 조용히 비지 않는다).
    (b) `<!-- snippet: ... -->` / `<!-- snippet-end: ... -->` 주석 줄을 지운다.
    (c) 내부 .md 상대 링크는 라벨만 남긴다. http/https 외부 링크와 이미지 참조는 둔다.
    (d) 그 밖의 본문은 그대로. 출력은 UTF-8 무 BOM, LF.

출력은 원본 상대 경로를 그대로 미러링하고, 각 출력 폴더 옆에 그 문서들이 실제로
참조하는 PNG 만 `diagrams/` 로 복사한다. 폴더 하나만 들고 가도 업로드가 된다.
"""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "velog_export"

# 이론 15 개 시리즈. check_style.py 의 DOC_DIRS 와 같은 목록이다.
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

# 루트 문서 중 diagrams 내보내기가 있는 것들 — 루트 diagrams/ 를 쓴다.
ROOT_FILES = ["README.md", "STYLE_GUIDE.md", "git.md"]

# 프로젝트 글. 랩별 articles/ 와 mega articles/ 가 각자 diagrams/ 를 가진다.
PROJECT_GLOBS = [
    "projects/mini/*/articles/*.md",
    "projects/mega/articles/*.md",
]

# 검수 보고서는 원고가 아니다.
REPORT_PREFIXES = ("REVIEW_", "REDESIGN_")

# 펜스 판정은 PNG 를 만든 렌더러(scripts/export_mermaid_png.ps1)와 **글자 하나까지**
# 같은 규칙이어야 한다. 규칙이 어긋나면 펜스 순번과 PNG 파일명의 번호가 어긋나고,
# 그러면 글에 엉뚱한 그림이 붙는다. 그쪽 규칙은 이렇다.
#
#   - 여는 줄: 열 0 에서 정확히 `` ```mermaid `` (앞뒤 공백 없음)
#   - 닫는 줄: 열 0 에서 정확히 `` ``` ``
#   - 다른 코드 펜스 안이어도 세다 — 렌더러는 mermaid 블록만 추적한다.
#
# 마지막 항목이 중요하다. STYLE_GUIDE.md 는 "언어 태그" 절에서 4-백틱 블록 안에
# ```mermaid 예시를 보여 주는데, 렌더러는 그것도 2 번 펜스로 세어 PNG 를 만들었다.
# 여기서 코드 블록 안이라고 건너뛰면 그 뒤 진짜 도식이 02 번을 가져가 그림이 밀린다.
MERMAID_OPEN = "```mermaid"
FENCE_CLOSE = "```"
FENCE_RE = re.compile(r"^```")

SNIPPET_RE = re.compile(r"^\s*<!--\s*snippet(?:-end)?:.*?-->\s*$")

# 내부 .md 상대 링크(#앵커 포함) — 라벨만 남긴다.
# 앞의 부정 후방탐색이 이미지 참조(`![...](...)`)를 건드리지 않게 막는다.
INTERNAL_LINK_RE = re.compile(
    r"(?<!!)\[([^\]]*)\]\((?!https?://|#)[^)]*\.md(?:#[^)]*)?\)"
)


def is_report(path: Path) -> bool:
    return path.name.startswith(REPORT_PREFIXES)


def documents() -> list[Path]:
    """내보낼 문서를 저장소 순서대로 모은다."""
    result: list[Path] = []
    for name in DOC_DIRS:
        directory = ROOT / name
        if not directory.is_dir():
            continue
        result.extend(sorted(p for p in directory.glob("*.md") if not is_report(p)))
    for name in ROOT_FILES:
        path = ROOT / name
        if path.is_file():
            result.append(path)
    for pattern in PROJECT_GLOBS:
        result.extend(sorted(p for p in ROOT.glob(pattern) if not is_report(p)))
    return result


def convert(path: Path, warnings: list[str]) -> tuple[list[str], int, list[Path]]:
    """문서 한 편을 업로드용 본문으로 바꾼다.

    반환값은 (출력 줄, 교체한 펜스 수, 참조한 PNG 경로들)이다.
    """
    stem = path.stem
    diagrams = path.parent / "diagrams"
    text = path.read_text(encoding="utf-8")
    lines = text.split("\n")

    out: list[str] = []
    used: list[Path] = []
    replaced = 0
    fence_index = 0
    inside_code = False  # ```mermaid 이외의 코드 펜스 안인가
    inside_mermaid = False
    block: list[str] = []
    block_line = 0

    for number, line in enumerate(lines, 1):
        if inside_mermaid:
            if line == FENCE_CLOSE:
                inside_mermaid = False
                png = diagrams / f"{stem}_diagram_{fence_index:02d}.png"
                if png.is_file():
                    out.append(
                        f"![{stem} diagram {fence_index:02d}]"
                        f"(diagrams/{stem}_diagram_{fence_index:02d}.png)"
                    )
                    used.append(png)
                    replaced += 1
                else:
                    # PNG 가 없으면 펜스를 지우지 않는다. 지우면 원고에서 그림이
                    # 통째로 사라지고, 그 사실이 눈에 띄지 않는다.
                    warnings.append(
                        f"{path.relative_to(ROOT).as_posix()}:{block_line} "
                        f"mermaid 펜스 #{fence_index} 의 PNG 가 없다 — "
                        f"{png.relative_to(ROOT).as_posix()}"
                    )
                    out.append(MERMAID_OPEN)
                    out.extend(block)
                    out.append(line)
                block = []
                continue
            block.append(line)
            continue

        # mermaid 판정이 코드 블록 추적보다 먼저다 — 렌더러와 같은 순서다.
        if line == MERMAID_OPEN:
            inside_mermaid = True
            fence_index += 1
            block = []
            block_line = number
            continue

        # 코드 블록 추적은 (b)·(c) 를 코드 안에서 적용하지 않기 위한 것뿐이다.
        # 펜스 번호에는 관여하지 않는다.
        if FENCE_RE.match(line):
            inside_code = not inside_code
            out.append(line)
            continue

        if inside_code:
            out.append(line)
            continue

        if SNIPPET_RE.match(line):
            continue

        out.append(INTERNAL_LINK_RE.sub(r"\1", line))

    if inside_mermaid:
        raise SystemExit(f"닫히지 않은 mermaid 펜스: {path} (줄 {block_line})")

    return out, replaced, used


def main() -> int:
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)

    warnings: list[str] = []
    copied: set[Path] = set()
    total_replaced = 0
    files = documents()

    for path in files:
        lines, replaced, used = convert(path, warnings)
        total_replaced += replaced

        destination = OUTPUT / path.relative_to(ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        # newline="" 로 써서 파이썬이 \n 을 \r\n 으로 바꾸지 않게 한다.
        with destination.open("w", encoding="utf-8", newline="") as handle:
            handle.write("\n".join(lines))

        for png in used:
            target = destination.parent / "diagrams" / png.name
            if target in copied:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(png, target)
            copied.add(target)

    for warning in warnings:
        print(f"WARN\t{warning}", file=sys.stderr)

    print(f"문서 {len(files)}편")
    print(f"교체한 mermaid 펜스 {total_replaced}개")
    print(f"복사한 PNG {len(copied)}장")
    print(f"누락 PNG 경고 {len(warnings)}건")
    print(f"출력 위치 {OUTPUT.relative_to(ROOT).as_posix()}/")
    return 1 if warnings else 0


if __name__ == "__main__":
    raise SystemExit(main())
