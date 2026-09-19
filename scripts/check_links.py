# check_links.py — VeLog 문서의 외부 링크 생존(link rot) 검사.
#
# 사용법:
#   python scripts/check_links.py            # 문서 시리즈 전체 검사
#   python scripts/check_links.py parts ai   # 지정 폴더만 검사
#
# 표준 라이브러리만 사용한다(의존성 0). HEAD 요청을 먼저 보내고,
# HEAD 를 거부하는 서버(405 등)는 GET 으로 재시도한다.
# 결과는 표준 출력 + scripts/link_report.txt 에 기록한다.

import concurrent.futures
import argparse
from collections import Counter
import re
import urllib.request
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC_DIRS = [
    "base", "algorithms", "languages", "parts", "system", "network", "math",
    "physics", "ai", "graphics", "db", "engine",
    "live_service", "platform", "tools",
]
ROOT_FILES = ["README.md", "git.md", "STYLE_GUIDE.md"]

URL_RE = re.compile(r'https?://[^\s<>"`]+')
HEADERS = {"User-Agent": "Mozilla/5.0 (link-checker; VeLog docs)"}
TIMEOUT = 15

# 봇 차단(403) 등으로 자동 검사가 무의미한 도메인 — 수동 확인 대상으로만 보고.
SKIP_DOMAINS = ("youtube.com", "youtu.be", "dl.acm.org", "ieeexplore.ieee.org")


def extract_urls(line):
    for match in URL_RE.finditer(line):
        candidate = match.group(0)
        depth = 0
        end = len(candidate)
        for index, char in enumerate(candidate):
            if char == "(":
                depth += 1
            elif char == ")":
                if depth == 0:
                    end = index
                    break
                depth -= 1
        url = candidate[:end].rstrip(".,;:'\"`>]")
        yield url


def is_article(path):
    return not path.name.startswith(("REVIEW_", "REDESIGN_"))


def collect_urls(dirs):
    urls = {}  # url -> [file:line]
    files = []
    for d in dirs:
        p = ROOT / d
        if p.is_dir():
            files += sorted(f for f in p.glob("*.md") if is_article(f))
        elif p.is_file():
            files.append(p)
    for f in files:
        text = f.read_text(encoding="utf-8", errors="replace")
        in_fence = False
        for i, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            for u in extract_urls(line):
                urls.setdefault(u, []).append(f"{f.relative_to(ROOT)}:{i}")
    return urls


def check(url):
    hostname = (urllib.parse.urlsplit(url).hostname or "").lower()
    if any(hostname == d or hostname.endswith("." + d) for d in SKIP_DOMAINS):
        return url, "SKIP", "봇 차단 도메인 — 수동 확인"
    for method in ("HEAD", "GET"):
        try:
            req = urllib.request.Request(url, headers=HEADERS, method=method)
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                return url, "OK", str(r.status)
        except urllib.error.HTTPError as e:
            # 일부 정상 사이트(RFC Editor 포함)는 HEAD에 404/403을 반환한다.
            # HEAD 결과만으로 죽은 링크를 판정하지 않고 GET으로 확인한다.
            if method == "HEAD":
                continue
            return url, "DEAD" if e.code in (404, 410) else "WARN", f"HTTP {e.code}"
        except Exception as e:
            if method == "HEAD":
                continue
            return url, "WARN", type(e).__name__
    return url, "WARN", "unreachable"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--projects", action="store_true",
                        help="Include handwritten project README, article and design documents")
    parser.add_argument("--report", type=Path, default=ROOT / "scripts" / "link_report.txt")
    args = parser.parse_args()
    dirs = args.paths or (DOC_DIRS + ROOT_FILES)
    if args.projects:
        dirs = list(dirs)
        for project in sorted((ROOT / "projects" / "mini").iterdir()):
            if project.is_dir():
                dirs.extend([project / "README.md", project / "articles"])
        dirs.extend([ROOT / "projects" / "mega" / "README.md",
                     ROOT / "projects" / "mega" / "articles",
                     ROOT / "projects" / "mega" / "docs"])
    urls = collect_urls(dirs)
    print(f"검사 대상: 고유 URL {len(urls)}개")
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
        for url, status, detail in ex.map(check, sorted(urls)):
            results.append((status, url, detail))
            if status in ("DEAD", "WARN"):
                print(f"  [{status}] {url} ({detail}) ← {urls[url][0]}")
    report = args.report.resolve()
    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open("w", encoding="utf-8") as f:
        for status, url, detail in sorted(results):
            f.write(f"{status}\t{detail}\t{url}\t{'; '.join(urls[url])}\n")
    counts = Counter(status for status, _, _ in results)
    print(f"\n요약: OK {counts['OK']} / WARN {counts['WARN']} / DEAD {counts['DEAD']} / SKIP {counts['SKIP']}")
    print(f"전체 결과: {report}")
    print("주의: OK는 HTTP 응답 확인만 뜻한다. 저자·제목·주장 근거는 별도 검증해야 한다. WARN/SKIP은 미확인이다.")


if __name__ == "__main__":
    main()
