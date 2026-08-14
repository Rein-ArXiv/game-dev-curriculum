#!/usr/bin/env python3
"""Validate mini-project step documents without theory-only restrictions."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = ROOT / "projects" / "mini"

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
    r"(?:다음|후속)[^.]{0,30}(?:주제|토픽|토대|출발점)",
    re.IGNORECASE,
)
OPENING_FENCE_RE = re.compile(r"^```(.*)$")
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


def validate(path: Path) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    rel = relative(path)

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

    if in_fence:
        errors.append(f"{rel}: 닫히지 않은 코드 블록이 있습니다.")
    if not has_implementation_code:
        warnings.append(f"{rel}: 구현 언어 코드 블록이 없습니다.")

    return errors, warnings


def main() -> int:
    documents = sorted(PROJECT_ROOT.glob("*/articles/*.md"))
    all_errors: list[str] = []
    all_warnings: list[str] = []

    for document in documents:
        errors, warnings = validate(document)
        all_errors.extend(errors)
        all_warnings.extend(warnings)

    for message in all_errors:
        print(f"ERROR: {message}")
    for message in all_warnings:
        print(f"WARN: {message}")

    print(
        f"\nChecked {len(documents)} project documents: "
        f"{len(all_errors)} error(s), {len(all_warnings)} warning(s)."
    )
    return 1 if all_errors else 0


if __name__ == "__main__":
    sys.exit(main())
