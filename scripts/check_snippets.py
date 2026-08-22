#!/usr/bin/env python3
"""문서의 코드 스니펫이 실제 소스와 드리프트했는지 검사한다.

PROJECTS_PLAN.md 의 "Step 문서 완료 기준" 7번을 기계로 강제한다.

사용법:
    python scripts/check_snippets.py                     # 기본 대상 전체
    python scripts/check_snippets.py projects/mini/db_lab # 지정 경로만
    python scripts/check_snippets.py --fix-ranges         # 낡은 줄번호 앵커 자동 갱신
    python scripts/check_snippets.py --stats              # 앵커 현황 요약

앵커 문법 — 코드 펜스 바로 앞 줄에 HTML 주석으로 둔다.

    <!-- snippet: src/platform.h -->                  파일 전체
    <!-- snippet: src/sim/soa.cpp#UpdateSSE -->       심볼(함수/클래스) 정의 블록
    <!-- snippet: src/sim/soa.cpp@sse-hot-loop -->    소스의 명시적 region 마커
    <!-- snippet: src/sim/soa.cpp:107-146 -->         줄 범위(가장 약함)

플래그(공백으로 구분해 뒤에 붙인다):

    mode=partial|exact       기본 partial. 발췌를 허용한다.
    comments=ignore|keep     기본 ignore. 주석 번역·재작성을 허용한다.
    indent=ignore|strict     기본 ignore. 들여쓰기 재정렬을 허용한다.

낡음(staleness) 대응 원칙:
    - 해석 불가능한 앵커(파일 없음, 심볼 없음, region 마커 없음, 범위 초과)는
      조용히 건너뛰지 않고 ERROR 로 터진다. 앵커는 썩으면 시끄럽게 썩는다.
    - 줄번호 앵커는 항상 WARN 을 내고, 내용이 파일 안 다른 위치에서 발견되면
      올바른 범위를 알려 준다. --fix-ranges 로 그 자리에서 갱신한다.
    - 심볼 앵커는 줄 이동에 면역이고, 이름이 바뀌면 ERROR 로 터진다(의도된 동작).
    - region 앵커는 마커가 코드와 함께 움직이므로 가장 강하다.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

DEFAULT_TARGETS = [
    "projects/mini/*/articles/*.md",
    "projects/mini/*/README.md",
    "projects/mini/README.md",
    "projects/mega/articles/*.md",
    "projects/mega/docs/*.md",
    "projects/mega/README.md",
]

# 서드파티 소스 트리는 문서 대상이 아니다.
EXCLUDED_PARTS = {"build", "build_avx2", "build_audit", "_deps", "node_modules", "temp"}

ANCHOR_RE = re.compile(r"^\s*<!--\s*snippet:\s*(?P<spec>[^>]+?)\s*-->\s*$")
FENCE_RE = re.compile(r"^\s*```(?P<info>.*)$")
TARGET_RE = re.compile(
    r"^(?P<path>[^@#\s]+?)"
    r"(?:@(?P<region>[\w.\-]+)"
    r"|\#(?P<symbol>[\w:~]+)"
    r"|:(?P<start>\d+)-(?P<end>\d+))?$"
)
REGION_BEGIN_RE = re.compile(r"snippet-begin\s*:\s*([\w.\-]+)")
REGION_END_RE = re.compile(r"snippet-end\s*:\s*([\w.\-]+)")

# 생략 표기. 주석이 제거되기 *전에* 판정해야 블록 경계가 살아남는다.
ELISION_RE = re.compile(
    r"^\s*(?:"
    r"(?:\.\.\.|…|⋮)"                      # 맨몸 ...
    r"|(?://|\#|--|;)\s*(?:\.\.\.|…|⋮).*"  # // ...  # ...  -- ...
    r"|/\*\s*(?:\.\.\.|…|⋮).*\*/"          # /* ... */
    r")\s*$"
)

C_LIKE = {
    ".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".cxx", ".hxx", ".inl", ".ipp",
    ".cs", ".java", ".js", ".ts", ".jsx", ".tsx", ".rs", ".go", ".m", ".mm",
    ".glsl", ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".hlsl", ".fx",
}
HASH_LIKE = {".py", ".cmake", ".sh", ".bash", ".ps1", ".psm1", ".yml", ".yaml", ".toml", ".rb"}
DASH_LIKE = {".sql"}

VALID_FLAGS = {
    "mode": {"partial", "exact"},
    "comments": {"ignore", "keep"},
    "indent": {"ignore", "strict"},
}


def comment_style(path: Path) -> str:
    if path.name in {"CMakeLists.txt", "Makefile", "Dockerfile"}:
        return "hash"
    suffix = path.suffix.lower()
    if suffix in C_LIKE:
        return "c"
    if suffix in HASH_LIKE:
        return "hash"
    if suffix in DASH_LIKE:
        return "dash"
    return "none"


def mask_noncode(text: str, in_block: bool, style: str) -> tuple[str, bool]:
    """문자열·주석 문자를 공백으로 바꾼 텍스트와 블록주석 상태를 돌려준다.

    중괄호 세기와 주석 제거가 문자열 리터럴 안의 `//`, `{` 에 속지 않게 한다.
    """
    if style == "none":
        return text, False

    out: list[str] = []
    i = 0
    n = len(text)
    quote = ""
    while i < n:
        ch = text[i]
        if in_block:
            if style == "c" and text.startswith("*/", i):
                out.append("  ")
                i += 2
                in_block = False
                continue
            out.append(" ")
            i += 1
            continue
        if quote:
            out.append(" ")
            if ch == "\\" and i + 1 < n:
                out.append(" ")
                i += 2
                continue
            if ch == quote:
                quote = ""
            i += 1
            continue
        if ch in "\"'":
            quote = ch
            out.append(" ")
            i += 1
            continue
        if style == "c" and text.startswith("//", i):
            out.append(" " * (n - i))
            break
        if style == "c" and text.startswith("/*", i):
            out.append("  ")
            i += 2
            in_block = True
            continue
        if style == "hash" and ch == "#":
            out.append(" " * (n - i))
            break
        if style == "dash" and text.startswith("--", i):
            out.append(" " * (n - i))
            break
        out.append(ch)
        i += 1
    return "".join(out), in_block


def strip_comments(lines: list[str], style: str) -> list[str]:
    """주석을 제거한 줄 목록. 코드 문자는 원본 그대로 유지한다."""
    if style == "none":
        return list(lines)
    result: list[str] = []
    in_block = False
    for line in lines:
        masked, in_block = mask_noncode(line, in_block, style)
        # 마스킹된 위치는 공백이므로, 코드가 남은 구간만 원본에서 되살린다.
        kept = "".join(
            original if mask != " " or original == " " else " "
            for original, mask in zip(line, masked)
        )
        result.append(kept)
    return result


def dedent(lines: list[str]) -> list[str]:
    widths = [len(line) - len(line.lstrip()) for line in lines if line.strip()]
    cut = min(widths) if widths else 0
    return [line[cut:] if line.strip() else "" for line in lines]


@dataclass
class Flags:
    mode: str = "partial"
    comments: str = "ignore"
    indent: str = "ignore"


@dataclass
class Anchor:
    line: int              # 앵커 주석이 있는 문서 줄 번호
    spec: str
    path: str
    region: str | None
    symbol: str | None
    start: int | None
    end: int | None
    flags: Flags
    fence_line: int = 0    # 여는 펜스 줄 번호
    body: list[str] = field(default_factory=list)
    body_start: int = 0    # 펜스 첫 코드 줄의 문서 줄 번호

    @property
    def kind(self) -> str:
        if self.region:
            return "region"
        if self.symbol:
            return "symbol"
        if self.start:
            return "range"
        return "file"


def parse_anchor(spec: str, line: int) -> tuple[Anchor | None, str | None]:
    parts = spec.split()
    if not parts:
        return None, "앵커에 대상이 없습니다."
    target = TARGET_RE.match(parts[0])
    if not target:
        return None, f"앵커 대상 `{parts[0]}` 을 해석할 수 없습니다."

    flags = Flags()
    for token in parts[1:]:
        if "=" not in token:
            return None, f"플래그 `{token}` 은 key=value 형태여야 합니다."
        key, value = token.split("=", 1)
        if key not in VALID_FLAGS:
            return None, f"알 수 없는 플래그 `{key}`. 가능: {', '.join(sorted(VALID_FLAGS))}"
        if value not in VALID_FLAGS[key]:
            return None, f"플래그 `{key}` 값 `{value}` 은 {sorted(VALID_FLAGS[key])} 중 하나여야 합니다."
        setattr(flags, key, value)

    return (
        Anchor(
            line=line,
            spec=spec,
            path=target.group("path"),
            region=target.group("region"),
            symbol=target.group("symbol"),
            start=int(target.group("start")) if target.group("start") else None,
            end=int(target.group("end")) if target.group("end") else None,
            flags=flags,
        ),
        None,
    )


def collect_anchors(lines: list[str]) -> tuple[list[Anchor], list[tuple[int, str]]]:
    """문서에서 앵커 + 뒤따르는 코드 펜스를 수집한다."""
    anchors: list[Anchor] = []
    problems: list[tuple[int, str]] = []

    index = 0
    in_fence = False
    while index < len(lines):
        line = lines[index]
        if FENCE_RE.match(line):
            in_fence = not in_fence
            index += 1
            continue
        if in_fence:
            index += 1
            continue

        matched = ANCHOR_RE.match(line)
        if not matched:
            index += 1
            continue

        anchor, error = parse_anchor(matched.group("spec"), index + 1)
        if error:
            problems.append((index + 1, error))
            index += 1
            continue
        assert anchor is not None

        cursor = index + 1
        while cursor < len(lines) and not lines[cursor].strip():
            cursor += 1
        if cursor >= len(lines) or not FENCE_RE.match(lines[cursor]):
            problems.append((index + 1, "앵커 다음에 코드 펜스가 없습니다."))
            index += 1
            continue

        anchor.fence_line = cursor + 1
        anchor.body_start = cursor + 2
        body: list[str] = []
        cursor += 1
        while cursor < len(lines) and not FENCE_RE.match(lines[cursor]):
            body.append(lines[cursor])
            cursor += 1
        if cursor >= len(lines):
            problems.append((anchor.fence_line, "앵커가 가리키는 코드 펜스가 닫히지 않았습니다."))
            index = cursor
            continue
        anchor.body = body
        anchors.append(anchor)
        index = cursor + 1

    return anchors, problems


# ---------------------------------------------------------------- 소스 영역 해석


def find_region(lines: list[str], name: str) -> tuple[int, int] | None:
    begin = end = None
    for number, line in enumerate(lines, start=1):
        matched = REGION_BEGIN_RE.search(line)
        if matched and matched.group(1) == name and begin is None:
            begin = number
            continue
        matched = REGION_END_RE.search(line)
        if matched and matched.group(1) == name and begin is not None:
            end = number
            break
    if begin is None or end is None:
        return None
    return begin + 1, end - 1


def _definition_patterns(symbol: str) -> list[tuple[str, re.Pattern[str]]]:
    escaped = re.escape(symbol)
    if "::" in symbol:
        qualified = escaped.replace(r"::", r"\s*::\s*")
    else:
        qualified = r"(?:[A-Za-z_]\w*\s*::\s*)*" + escaped
    bare = symbol.split("::")[-1]
    bare_escaped = re.escape(bare)
    return [
        ("python", re.compile(r"^\s*(?:async\s+)?def\s+" + bare_escaped + r"\s*\(")),
        # 파이썬 클래스는 정의 줄이 `:` 로 끝난다. `[(:]` 로만 보면
        # C++ 의 `class X : public Y {` 가 여기 먼저 걸려 들여쓰기 기반
        # _python_extent 가 정의 줄 한 줄만 범위로 잡는다(실측: 기반 클래스를 가진
        # 모든 C++ 클래스가 `#심볼` 앵커로 잡히지 않았다).
        ("python", re.compile(
            r"^\s*class\s+" + bare_escaped + r"\s*(?:\(.*\))?\s*:\s*$")),
        ("cmake", re.compile(r"^\s*(?:function|macro)\s*\(\s*" + bare_escaped + r"\b", re.I)),
        ("type", re.compile(
            r"\b(?:class|struct|union|enum\s+class|enum|namespace|interface|record)\s+"
            + bare_escaped + r"\b(?![\w:])")),
        ("keyword", re.compile(r"^\s*(?:function|fn|func|sub)\s+" + bare_escaped + r"\b")),
        ("define", re.compile(r"^\s*#\s*define\s+" + bare_escaped + r"\b")),
        ("call", re.compile(r"(?<![\w:])" + qualified + r"\s*\(")),
    ]


def _tail_after_signature(masked: list[str], start: int) -> str:
    """정의 줄의 `(` 부터 짝이 맞는 `)` 이후 텍스트를 이어붙여 돌려준다."""
    depth = 0
    seen = False
    for index in range(start, min(start + 12, len(masked))):
        line = masked[index]
        begin = 0
        if not seen:
            begin = line.find("(")
            if begin < 0:
                continue
        for position in range(begin, len(line)):
            character = line[position]
            if character == "(":
                depth += 1
                seen = True
            elif character == ")":
                depth -= 1
                if depth == 0:
                    rest = line[position + 1:]
                    return " ".join([rest] + masked[index + 1: index + 4])
    return ""


def find_symbol(lines: list[str], masked: list[str], symbol: str, style: str) -> tuple[int, int] | None:
    patterns = _definition_patterns(symbol)
    for index, line in enumerate(masked):
        for kind, pattern in patterns:
            if not pattern.search(line):
                continue
            if kind in {"call", "type", "keyword"}:
                tail = _tail_after_signature(masked, index) if kind == "call" else line
                if kind == "call":
                    # 선언(`;`)이나 호출은 정의가 아니다. `)` 뒤에 `{` 가 와야 한다.
                    cleaned = re.sub(
                        r"\b(?:const|noexcept|override|final|mutable|volatile)\b", " ", tail)
                    cleaned = re.sub(r"->[^{;]*", " ", cleaned)
                    brace = cleaned.find("{")
                    semicolon = cleaned.find(";")
                    if brace < 0 or (0 <= semicolon < brace):
                        continue
            if kind == "python":
                return _python_extent(lines, index)
            if kind == "cmake":
                return _cmake_extent(lines, index)
            if kind == "define":
                return _define_extent(lines, index)
            extent = _brace_extent(masked, index, style)
            if extent:
                return extent
    return None


def _brace_extent(masked: list[str], start: int, style: str) -> tuple[int, int] | None:
    depth = 0
    opened = False
    for index in range(start, len(masked)):
        line = masked[index]
        for character in line:
            if character == "{":
                depth += 1
                opened = True
            elif character == "}":
                depth -= 1
                if opened and depth == 0:
                    return start + 1, index + 1
        if not opened and ";" in line:
            return None
    return None


def _python_extent(lines: list[str], start: int) -> tuple[int, int]:
    base = len(lines[start]) - len(lines[start].lstrip())
    end = start
    for index in range(start + 1, len(lines)):
        line = lines[index]
        if not line.strip():
            continue
        if len(line) - len(line.lstrip()) <= base:
            break
        end = index
    return start + 1, end + 1


def _cmake_extent(lines: list[str], start: int) -> tuple[int, int]:
    for index in range(start + 1, len(lines)):
        if re.match(r"^\s*end(?:function|macro)\s*\(", lines[index], re.I):
            return start + 1, index + 1
    return start + 1, len(lines)


def _define_extent(lines: list[str], start: int) -> tuple[int, int]:
    end = start
    while end < len(lines) and lines[end].rstrip().endswith("\\"):
        end += 1
    return start + 1, end + 1


def resolve_source(doc: Path, anchor: Anchor) -> tuple[Path | None, str | None]:
    if ".." in Path(anchor.path).parts:
        return None, f"앵커 경로 `{anchor.path}` 에 `..` 은 쓸 수 없습니다."
    bases = [doc.parent, doc.parent.parent, doc.parent.parent.parent, ROOT]
    seen: set[Path] = set()
    for base in bases:
        candidate = (base / anchor.path)
        if candidate in seen:
            continue
        seen.add(candidate)
        if candidate.is_file():
            return candidate.resolve(), None
    return None, (
        f"앵커가 가리키는 소스 `{anchor.path}` 를 찾을 수 없습니다"
        f"(기준: {doc.parent.relative_to(ROOT).as_posix()} 및 상위 디렉터리)."
    )


# ------------------------------------------------------------------- 정규화·대조


ELISION = object()


def normalize(
    lines: list[str],
    offsets: list[int],
    style: str,
    flags: Flags,
    detect_elision: bool,
) -> list[tuple[int, object]]:
    """(원본 줄번호, 정규화 텍스트 또는 ELISION) 목록. 빈 줄은 버린다."""
    marks = [detect_elision and bool(ELISION_RE.match(line)) for line in lines]
    body = strip_comments(lines, style) if flags.comments == "ignore" else list(lines)
    if flags.indent == "strict":
        body = dedent(body)

    result: list[tuple[int, object]] = []
    for offset, line, is_elision in zip(offsets, body, marks):
        if is_elision:
            result.append((offset, ELISION))
            continue
        if flags.indent == "strict":
            text = line.rstrip()
        else:
            text = " ".join(line.split())
        if not text.strip():
            continue
        result.append((offset, text))
    return result


def split_blocks(entries: list[tuple[int, object]]) -> list[list[tuple[int, str]]]:
    blocks: list[list[tuple[int, str]]] = []
    current: list[tuple[int, str]] = []
    for offset, value in entries:
        if value is ELISION:
            if current:
                blocks.append(current)
                current = []
            continue
        current.append((offset, str(value)))
    if current:
        blocks.append(current)
    return blocks


def find_block(source: list[tuple[int, str]], block: list[tuple[int, str]], start: int) -> int:
    texts = [text for _, text in source]
    needle = [text for _, text in block]
    limit = len(texts) - len(needle)
    for position in range(start, limit + 1):
        if texts[position: position + len(needle)] == needle:
            return position
    return -1


def divergence(source: list[tuple[int, str]], block: list[tuple[int, str]], start: int) -> tuple[int, str]:
    """가장 길게 일치한 위치를 찾아 (블록 내 어긋난 인덱스, 소스 쪽 기대값)."""
    texts = [text for _, text in source]
    needle = [text for _, text in block]
    best_length = -1
    best_index = 0
    best_expected = ""
    for position in range(start, max(start + 1, len(texts))):
        length = 0
        while (
            length < len(needle)
            and position + length < len(texts)
            and texts[position + length] == needle[length]
        ):
            length += 1
        if length > best_length:
            best_length = length
            best_index = min(length, len(needle) - 1)
            actual = position + length
            best_expected = texts[actual] if actual < len(texts) else "(소스 영역 끝)"
    return best_index, best_expected


# ------------------------------------------------------------------------ 검사


@dataclass
class Result:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    anchors: int = 0
    kinds: dict[str, int] = field(default_factory=dict)
    fixes: list[tuple[int, str]] = field(default_factory=list)


def check_anchor(doc: Path, rel: str, anchor: Anchor, result: Result) -> None:
    source_path, error = resolve_source(doc, anchor)
    if error:
        result.errors.append(f"{rel}:{anchor.line}: {error}")
        return
    assert source_path is not None

    source_lines = source_path.read_text(encoding="utf-8", errors="replace").splitlines()
    style = comment_style(source_path)
    masked: list[str] = []
    in_block = False
    for line in source_lines:
        text, in_block = mask_noncode(line, in_block, style)
        masked.append(text)

    source_rel = source_path.relative_to(ROOT).as_posix()

    if anchor.region:
        span = find_region(source_lines, anchor.region)
        if span is None:
            result.errors.append(
                f"{rel}:{anchor.line}: `{source_rel}` 에 region 마커 "
                f"`snippet-begin: {anchor.region}` / `snippet-end: {anchor.region}` 짝이 없습니다."
            )
            return
    elif anchor.symbol:
        span = find_symbol(source_lines, masked, anchor.symbol, style)
        if span is None:
            result.errors.append(
                f"{rel}:{anchor.line}: `{source_rel}` 에서 심볼 `{anchor.symbol}` 의 정의를 "
                f"찾지 못했습니다. 이름이 바뀌었거나 옮겨졌으면 앵커를 갱신하십시오."
            )
            return
    elif anchor.start:
        assert anchor.end is not None
        if anchor.start > anchor.end:
            result.errors.append(f"{rel}:{anchor.line}: 줄 범위가 거꾸로입니다({anchor.start}-{anchor.end}).")
            return
        if anchor.end > len(source_lines):
            result.errors.append(
                f"{rel}:{anchor.line}: `{source_rel}` 은 {len(source_lines)}줄인데 앵커는 "
                f"{anchor.start}-{anchor.end} 를 가리킵니다."
            )
            return
        span = (anchor.start, anchor.end)
        result.warnings.append(
            f"{rel}:{anchor.line}: 줄번호 앵커는 소스가 움직이면 곧 낡습니다. "
            f"`{anchor.path}#심볼` 또는 `{anchor.path}@region` 을 권장합니다."
        )
    else:
        span = (1, len(source_lines))

    region_lines = source_lines[span[0] - 1: span[1]]
    region_offsets = list(range(span[0], span[1] + 1))
    source_entries = normalize(region_lines, region_offsets, style, anchor.flags, False)
    source_norm = [(offset, str(value)) for offset, value in source_entries]

    doc_offsets = list(range(anchor.body_start, anchor.body_start + len(anchor.body)))
    doc_entries = normalize(anchor.body, doc_offsets, style, anchor.flags, True)
    blocks = split_blocks(doc_entries)

    if not blocks:
        result.errors.append(f"{rel}:{anchor.line}: 앵커가 달린 코드 펜스가 비어 있습니다.")
        return

    if anchor.flags.mode == "exact":
        merged = [item for block in blocks for item in block]
        if len(blocks) > 1:
            result.errors.append(
                f"{rel}:{anchor.line}: mode=exact 에서는 생략 표기(`// ...`)를 쓸 수 없습니다."
            )
            return
        if [text for _, text in merged] != [text for _, text in source_norm]:
            index, expected = divergence(source_norm, merged, 0)
            doc_line = merged[index][0]
            result.errors.append(
                f"{rel}:{doc_line}: 스니펫이 `{source_rel}` "
                f"{span[0]}-{span[1]} 과 다릅니다(mode=exact).\n"
                f"    문서: {merged[index][1]}\n"
                f"    소스: {expected}"
            )
        return

    position = 0
    for block in blocks:
        found = find_block(source_norm, block, position)
        if found < 0:
            index, expected = divergence(source_norm, block, position)
            doc_line = block[index][0]
            message = (
                f"{rel}:{doc_line}: 스니펫이 `{source_rel}` "
                f"{span[0]}-{span[1]} 과 다릅니다.\n"
                f"    문서: {block[index][1]}\n"
                f"    소스: {expected}"
            )
            if anchor.kind == "range":
                suggestion = suggest_range(source_lines, style, anchor, blocks)
                if suggestion:
                    message += (
                        f"\n    힌트: 같은 내용이 {suggestion[0]}-{suggestion[1]} 에 있습니다. "
                        f"--fix-ranges 로 앵커를 갱신할 수 있습니다."
                    )
                    result.fixes.append(
                        (anchor.line, f"{anchor.path}:{suggestion[0]}-{suggestion[1]}")
                    )
            result.errors.append(message)
            return
        position = found + len(block)


def suggest_range(
    source_lines: list[str], style: str, anchor: Anchor, blocks: list[list[tuple[int, str]]]
) -> tuple[int, int] | None:
    """줄번호가 밀렸을 때 파일 전체에서 같은 내용을 찾아 올바른 범위를 제안한다."""
    offsets = list(range(1, len(source_lines) + 1))
    whole = normalize(source_lines, offsets, style, anchor.flags, False)
    whole_norm = [(offset, str(value)) for offset, value in whole]

    position = 0
    first = last = None
    for block in blocks:
        found = find_block(whole_norm, block, position)
        if found < 0:
            return None
        if first is None:
            first = whole_norm[found][0]
        last = whole_norm[found + len(block) - 1][0]
        position = found + len(block)
    if first is None or last is None:
        return None
    return first, last


def check_document(path: Path) -> tuple[list[str], list[str]]:
    """check_project_docs.py 가 그대로 쓰는 진입점. (errors, warnings)."""
    result = check_document_full(path)
    return result.errors, result.warnings


def check_document_full(path: Path) -> Result:
    result = Result()
    rel = path.relative_to(ROOT).as_posix()
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    anchors, problems = collect_anchors(lines)
    for line, message in problems:
        result.errors.append(f"{rel}:{line}: {message}")

    for anchor in anchors:
        result.anchors += 1
        result.kinds[anchor.kind] = result.kinds.get(anchor.kind, 0) + 1
        check_anchor(path, rel, anchor, result)
    return result


def apply_fixes(path: Path, fixes: list[tuple[int, str]]) -> None:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    for line_number, new_target in fixes:
        index = line_number - 1
        matched = ANCHOR_RE.match(lines[index])
        if not matched:
            continue
        spec = matched.group("spec")
        rest = spec.split()[1:]
        replacement = " ".join([new_target] + rest)
        lines[index] = re.sub(
            r"snippet:\s*[^>]+?\s*-->", f"snippet: {replacement} -->", lines[index]
        )
    path.write_text("".join(lines), encoding="utf-8")


# ------------------------------------------------------------------------- CLI


def excluded(path: Path) -> bool:
    return any(part in EXCLUDED_PARTS for part in path.parts)


def collect_documents(args: list[str]) -> list[Path]:
    if not args:
        found: list[Path] = []
        for pattern in DEFAULT_TARGETS:
            found.extend(ROOT.glob(pattern))
        return sorted({p for p in found if p.is_file() and not excluded(p)})

    found = []
    for argument in args:
        path = Path(argument)
        if not path.is_absolute():
            path = ROOT / argument
        if path.is_dir():
            found.extend(p for p in path.rglob("*.md") if not excluded(p))
        elif path.is_file():
            found.append(path)
    return sorted(set(found))


def main(argv: list[str]) -> int:
    fix_ranges = "--fix-ranges" in argv
    stats = "--stats" in argv
    args = [a for a in argv if not a.startswith("--")]

    documents = collect_documents(args)
    all_errors: list[str] = []
    all_warnings: list[str] = []
    total_anchors = 0
    kinds: dict[str, int] = {}
    fixed = 0

    for document in documents:
        result = check_document_full(document)
        if fix_ranges and result.fixes:
            apply_fixes(document, result.fixes)
            fixed += len(result.fixes)
            result = check_document_full(document)
        all_errors.extend(result.errors)
        all_warnings.extend(result.warnings)
        total_anchors += result.anchors
        for kind, count in result.kinds.items():
            kinds[kind] = kinds.get(kind, 0) + count

    for message in all_errors:
        print(f"ERROR: {message}")
    for message in all_warnings:
        print(f"WARN: {message}")

    if fixed:
        print(f"\n줄번호 앵커 {fixed}개를 갱신했습니다.")
    if stats or total_anchors:
        breakdown = ", ".join(f"{k} {v}" for k, v in sorted(kinds.items())) or "없음"
        print(f"\n앵커 종류: {breakdown}")

    print(
        f"\nChecked {len(documents)} documents, {total_anchors} snippet anchor(s): "
        f"{len(all_errors)} error(s), {len(all_warnings)} warning(s)."
    )
    if total_anchors == 0:
        print("앵커가 하나도 없습니다. 문서에 `<!-- snippet: ... -->` 를 달아야 검사가 일합니다.")
    return 1 if all_errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
