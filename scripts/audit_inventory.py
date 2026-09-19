"""Inventory publication documents and syntax-check Python fences without executing them.

This is coverage evidence, not a technical-correctness certificate. Compilation failures
are review candidates: a fence may intentionally be pseudocode or a partial function.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from check_style import DOC_DIRS
from check_links import extract_urls, is_article

ROOT = Path(__file__).resolve().parent.parent


def documents():
    found = set()
    for directory in DOC_DIRS:
        found.update(p for p in (ROOT / directory).glob("*.md") if is_article(p))
    for name in ("README.md", "CURRICULUM_MAP.md", "CONTENT_OWNERSHIP.md",
                 "PROJECTS_PLAN.md", "git.md"):
        found.add(ROOT / name)
    for project in (ROOT / "projects" / "mini").iterdir():
        if project.is_dir():
            found.add(project / "README.md")
            found.update(project.glob("articles/*.md"))
    mega = ROOT / "projects" / "mega"
    found.add(mega / "README.md")
    found.update(mega.glob("articles/*.md"))
    found.update(mega.glob("docs/*.md"))
    return sorted(p for p in found if p.is_file() and is_article(p))


def inspect(path):
    data = path.read_bytes()
    lines = data.decode("utf-8-sig").splitlines()
    fences = []
    references = []
    links = []
    in_references = False
    active = None
    body = []
    for number, line in enumerate(lines, 1):
        fence = re.match(r"^\s*(`{3,}|~{3,})(.*)$", line)
        if active is not None:
            if fence and fence[1][0] == active["marker"][0] and len(fence[1]) >= len(active["marker"]) and not fence[2].strip():
                item = {k: v for k, v in active.items() if k != "marker"}
                item["lines"] = len(body)
                item["anchored"] = active["line"] > 1 and "<!-- snippet:" in lines[active["line"] - 2]
                if item["language"] in ("python", "py"):
                    try:
                        compile("\n".join(body), f"{path}:{item['line']}", "exec")
                        item["syntax"] = "pass; not executed"
                    except SyntaxError as error:
                        item["syntax"] = "needs-context-review"
                        item["error"] = f"line {item['line'] + (error.lineno or 1)}: {error.msg}"
                fences.append(item)
                active = None
                body = []
            else:
                body.append(line)
            continue
        if fence:
            active = {"line": number, "language": fence[2].strip().split(" ")[0], "marker": fence[1]}
            continue
        if line.startswith("## "):
            in_references = bool(re.search(r"더 읽을거리|참고|references?", line, re.I))
        if in_references and re.match(r"^\s*[-*]\s", line):
            references.append({"line": number, "text": line})
        links.extend({"line": number, "url": url} for url in extract_urls(line))
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(data).hexdigest(),
            "lines": len(lines), "fences": fences, "unclosed_fence": active,
            "references": references, "urls": links}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "temp/audit_2026_09_19/inventory.json")
    args = parser.parse_args()
    rows = [inspect(path) for path in documents()]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Documents: {len(rows)}; lines: {sum(row['lines'] for row in rows)}")
    languages = Counter(fence["language"] for row in rows for fence in row["fences"])
    print(f"Fences: {sum(languages.values())}; languages: {dict(languages)}")
    print(f"Bibliography entries: {sum(len(row['references']) for row in rows)}")
    for row in rows:
        if row["unclosed_fence"]:
            print(f"UNCLOSED {row['path']}:{row['unclosed_fence']['line']}")
        for fence in row["fences"]:
            if fence.get("syntax") == "needs-context-review":
                print(f"PYTHON-CONTEXT {row['path']}:{fence['line']} {fence['error']}")
    print(f"Inventory: {args.output}")


if __name__ == "__main__":
    main()
