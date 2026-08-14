"""정규화 뒤 남은 전방 예고와 시리즈 메타 문장을 제거한다."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from check_style import DOC_DIRS, ROOT, documents


DROP_PREFIXES = (
    "이와 이어지는 주제는",
    "> 이 회차는 API 중립",
)

REPLACEMENTS = [
    ("이 글는", "이 글은"),
    ("이 글가", "이 글이"),
    ("이 글를", "이 글을"),
    ("이 글와", "이 글과"),
    ("다음 회차 주제", "고급 머티리얼 모델"),
    ("후속 범위의 ", ""),
    ("후속 범위의 몫이다", "이 글의 범위에서는 제외한다"),
    ("후속 범위로 남긴다", "이 글의 범위에서는 제외한다"),
    ("자연스러운 다음 주제다", "별도의 주제다"),
    ("다음 주제다", "별도의 주제다"),
    ("기초 트랙", "기본 래스터 파이프라인"),
    ("advanced 트랙", "심화 렌더링 영역"),
    ("심화 트랙(-adv)", "심화 렌더링 영역"),
    ("심화 트랙", "심화 렌더링 영역"),
    ("이 트랙", "이 설명"),
    ("이번 회차", "이 글"),
    ("이 회차", "이 글"),
    ("·*법선 매핑*", "*법선 매핑*"),
]


def cleanup_line(line: str) -> str:
    stripped = line.strip()
    if any(stripped.startswith(prefix) for prefix in DROP_PREFIXES):
        return ""

    for old, new in REPLACEMENTS:
        line = line.replace(old, new)

    line = re.sub(r"다음\s*(?:회차|글|편|파트|part|문서)", "별도 주제", line, flags=re.IGNORECASE)
    line = re.sub(r"\s+([,.])", r"\1", line)
    line = re.sub(r" {2,}", " ", line)
    return line


def cleanup_file(path: Path) -> bool:
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
            output.append(cleanup_line(body) + ending)

    result = "".join(output)
    if result == source:
        return False
    path.write_text(result, encoding="utf-8", newline="")
    return True


def main() -> int:
    targets = documents(sys.argv[1:] or DOC_DIRS)
    changed = [path.relative_to(ROOT) for path in targets if cleanup_file(path)]
    for path in changed:
        print(path)
    print(f"\n수정 문서 {len(changed)}개")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
