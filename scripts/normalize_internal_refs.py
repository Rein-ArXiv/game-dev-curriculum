"""이론 본문의 내부 문서 링크와 회차 의존 표현을 자기완결 문장으로 정규화한다."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from check_style import DOC_DIRS, ROOT, documents


INTERNAL_LINK_RE = re.compile(r"\[([^\]]+)\]\((?!https?://|#)[^)]+\.md(?:#[^)]+)?\)")

NEXT_PATTERNS = [
    (re.compile(r"다음\s*(?:회차|글|편|파트|part|문서)에서는?", re.IGNORECASE), "이 범위를 확장하면"),
    (re.compile(r"다음\s*(?:회차|글|편|파트|part|문서)는", re.IGNORECASE), "이와 이어지는 주제는"),
    (re.compile(r"다음\s*(?:회차|글|편|파트|part|문서)의", re.IGNORECASE), "후속 범위의"),
]

PREVIOUS_RE = re.compile(
    r"이전\s*(?:회차|글|편|파트|part|문서)(?:에서\s*다룬|에서|의|은|는|이|가|을|를|도|처럼|까지|부터)?\s*",
    re.IGNORECASE,
)
NUMBERED_REFERENCE_RE = re.compile(
    r"(?:\d+\s*회차|심화\s*\d+\s*편|adv_\d+|stage\d+(?:_[A-Za-z0-9]+)?)"
    r"(?:\s+\d+(?:[.·]\d+)*\s*절)?"
    r"(?:에서|의|은|는|이|가|을|를|도|처럼|까지|부터)?\s*",
    re.IGNORECASE,
)


def normalize_line(line: str) -> str:
    line = INTERNAL_LINK_RE.sub(lambda match: match.group(1), line)
    line = re.sub(r"다음\s*회차\s*\(([^)]+)\)", r"\1", line)
    for pattern, replacement in NEXT_PATTERNS:
        line = pattern.sub(replacement, line)
    line = PREVIOUS_RE.sub("", line)
    line = NUMBERED_REFERENCE_RE.sub("", line)
    line = re.sub(r"\(\s*\)", "", line)
    line = re.sub(r" {2,}", " ", line)
    return line


def normalize_file(path: Path) -> bool:
    source = path.read_text(encoding="utf-8")
    lines = source.splitlines(keepends=True)
    output: list[str] = []
    in_fence = False

    for line in lines:
        if line.startswith("```"):
            in_fence = not in_fence
            output.append(line)
        elif in_fence:
            output.append(line)
        else:
            ending = "\n" if line.endswith("\n") else ""
            body = line[:-1] if ending else line
            output.append(normalize_line(body) + ending)

    result = "".join(output)
    if result == source:
        return False
    path.write_text(result, encoding="utf-8", newline="")
    return True


def main() -> int:
    targets = documents(sys.argv[1:] or DOC_DIRS)
    changed = [path.relative_to(ROOT) for path in targets if normalize_file(path)]
    for path in changed:
        print(path)
    print(f"\n수정 문서 {len(changed)}개")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
