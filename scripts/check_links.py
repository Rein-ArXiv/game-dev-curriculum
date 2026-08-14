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
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC_DIRS = [
    "base", "algorithms", "languages", "parts", "system", "network", "math",
    "physics", "ai", "graphics", "db", "engine",
    "live_service", "platform", "tools",
]
ROOT_FILES = ["README.md", "git.md", "STYLE_GUIDE.md"]

URL_RE = re.compile(r"https?://[a-zA-Z0-9./_%#?=&+~@-]+")
TRAILING = ").,;:'\"`>]"
HEADERS = {"User-Agent": "Mozilla/5.0 (link-checker; VeLog docs)"}
TIMEOUT = 15

# 봇 차단(403) 등으로 자동 검사가 무의미한 도메인 — 수동 확인 대상으로만 보고.
SKIP_DOMAINS = ("youtube.com", "youtu.be", "dl.acm.org", "ieeexplore.ieee.org")


def collect_urls(dirs):
    urls = {}  # url -> [file:line]
    files = []
    for d in dirs:
        p = ROOT / d
        if p.is_dir():
            files += sorted(p.glob("*.md"))
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
            for m in URL_RE.finditer(line):
                u = m.group(0).rstrip(TRAILING)
                urls.setdefault(u, []).append(f"{f.relative_to(ROOT)}:{i}")
    return urls


def check(url):
    if any(d in url for d in SKIP_DOMAINS):
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
    dirs = sys.argv[1:] or (DOC_DIRS + ROOT_FILES)
    urls = collect_urls(dirs)
    print(f"검사 대상: 고유 URL {len(urls)}개")
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
        for url, status, detail in ex.map(check, sorted(urls)):
            results.append((status, url, detail))
            if status in ("DEAD", "WARN"):
                print(f"  [{status}] {url} ({detail}) ← {urls[url][0]}")
    dead = [r for r in results if r[0] == "DEAD"]
    warn = [r for r in results if r[0] == "WARN"]
    report = ROOT / "scripts" / "link_report.txt"
    with report.open("w", encoding="utf-8") as f:
        for status, url, detail in sorted(results):
            f.write(f"{status}\t{detail}\t{url}\t{'; '.join(urls[url][:3])}\n")
    print(f"\n요약: OK {len(results)-len(dead)-len(warn)} / WARN {len(warn)} / DEAD {len(dead)}")
    print(f"전체 결과: {report.relative_to(ROOT)}")
    print("주의: WARN 은 일시 오류·봇 차단일 수 있으니 DEAD(404/410)만 우선 손보면 된다.")


if __name__ == "__main__":
    main()
