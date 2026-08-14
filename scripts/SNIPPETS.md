# 스니펫 앵커 사용법

`PROJECTS_PLAN.md`의 Step 문서 완료 기준 7번 — *최종 코드와 문서 코드가 드리프트하지
않는다* — 을 기계가 검사하게 하는 장치다. 검사기는 `scripts/check_snippets.py`이고,
`scripts/check_project_docs.py`가 이것을 함께 돌리므로 별도로 기억할 필요는 없다.

```powershell
python scripts/check_project_docs.py          # 문서 규칙 + 스니펫 대조
python scripts/check_snippets.py              # 스니펫 대조만
python scripts/check_snippets.py projects/mini/db_lab
python scripts/check_snippets.py --stats      # 앵커 종류별 현황
python scripts/check_snippets.py --fix-ranges # 밀린 줄번호 앵커 자동 갱신
```

## 앵커는 왜 opt-in인가

모든 코드 펜스를 소스와 대조하면 안 된다. step 문서의 스니펫 상당수는 *그 step
시점의 스냅숏*이고, 디스크의 코드는 이후 step에서 더 진행된 상태이기 때문이다.
`parts_perf_lab/articles/step04_simd.md`의 `AoSSimulation::Update`가 그 예다.
문서에는 인자가 `bool /*branchless*/`이고 소스에는 `bool branchless`인데, 이는
드리프트가 아니라 step05가 그 인자를 살렸기 때문이다.

그래서 앵커는 **저자가 "이 펜스는 지금 소스와 같다"고 주장할 때만** 단다. 앵커가
없는 펜스는 검사되지 않는다. 앵커를 다는 순간 그 주장은 CI가 지킨다.

## 문법

코드 펜스 **바로 앞 줄**에 HTML 주석으로 둔다.

```markdown
<!-- snippet: src/platform.h -->
```cpp
...
```
```

경로는 문서가 속한 lab 루트 기준이다(`projects/mini/parts_perf_lab/articles/x.md`
→ `projects/mini/parts_perf_lab/`). 못 찾으면 상위 디렉터리와 저장소 루트를 차례로
본다. `..`는 쓸 수 없다.

### 대상 지정 네 가지

| 문법 | 의미 | 낡음에 대한 내성 |
|---|---|---|
| `src/platform.h` | 파일 전체 | 파일이 사라지면 error |
| `src/sim/soa.cpp@sse-hot-loop` | 소스의 명시적 region 마커 | **가장 강함.** 마커가 코드와 함께 움직인다 |
| `src/sim/soa.cpp#UpdateSSE` | 심볼(함수·클래스·매크로) 정의 블록 | 줄 이동에 면역. 이름이 바뀌면 error |
| `src/sim/soa.cpp:107-146` | 줄 범위 | **가장 약함.** 항상 WARN이 붙는다 |

region 마커는 소스 쪽에 주석으로 둔다. 주석 기호는 무엇이든 된다.

```cpp
// snippet-begin: sse-hot-loop
for (; i + 4 <= count_; i += 4) {
    ...
}
// snippet-end: sse-hot-loop
```

심볼은 `#UpdateSSE`처럼 짧게 써도 `SoASimulation::UpdateSSE`를 찾고, 모호하면
`#SoASimulation::UpdateSSE`로 한정한다. 검사기는 *정의*만 찾는다 — 같은 이름의
호출부(`UpdateSSE(dt);`)나 선언(`void UpdateSSE(float);`)에는 걸리지 않는다.

### 플래그

```markdown
<!-- snippet: src/foo.py#parse mode=exact comments=keep indent=strict -->
```

| 플래그 | 기본값 | 의미 |
|---|---|---|
| `mode` | `partial` | `partial`은 발췌·생략 허용. `exact`는 영역 전체가 그대로 일치해야 한다 |
| `comments` | `ignore` | `ignore`는 주석을 양쪽에서 지우고 비교. `keep`은 주석까지 일치 요구 |
| `indent` | `ignore` | `ignore`는 들여쓰기·공백을 무시. `strict`는 상대 들여쓰기를 유지해 비교(Python용) |

## 의도된 차이를 어떻게 다루는가

문서가 소스를 그대로 붙여넣는 일은 드물다. 세 가지 차이는 **기본으로 허용**한다.

**1. 생략.** 문서는 가운데를 자른다. 생략 표기를 넣으면 그 자리에서 블록이 끊긴다.

```cpp
void ParseHeader(const char* buf) {
    if (len < 4) return -1;
    // ...
    return magic;
}
```

`// ...`, `# ...`, `-- ...`, 맨몸 `...`, `…`, `/* ... */`를 모두 인식한다.
`// ... 중략` 처럼 뒤에 설명을 붙여도 된다. 검사기는 생략으로 나뉜 각 블록이
*소스 안에 연속으로, 순서대로* 나타나는지만 본다.

**2. 주석 번역·재작성.** 기본값 `comments=ignore`가 양쪽 주석을 지우고 비교하므로,
문서가 주석을 한국어로 풀어 써도 통과한다. 실제로 `step04_simd.md`의 `platform.h`
스니펫은 소스와 주석이 전혀 다르지만 코드가 같아서 통과한다.

**3. 들여쓰기.** 기본값 `indent=ignore`가 공백을 정규화하므로, 함수 몸통만 떼어
와서 4칸 왼쪽으로 당겨도 통과한다. Python처럼 들여쓰기가 문법인 경우만
`indent=strict`를 쓴다.

허용하지 **않는** 것은 식별자·시그니처·리터럴·제어흐름의 차이다. 이것이 검수에서
반복 지적된 "문서가 존재하지 않는 식별자를 쓴다" 부류를 잡는 지점이다.

## 낡음(staleness) 대응

앵커 자체가 낡는 문제에 대한 답은 세 층이다.

**조용히 죽지 않는다.** 파일이 없거나, 심볼 이름이 바뀌었거나, region 마커 짝이
사라졌거나, 줄 범위가 파일 밖이면 **error**다. 검사를 건너뛰지 않는다. 검사기가
아무 말 없이 통과하는 앵커는 없다.

```
ERROR: .../step04_simd.md:285: `.../soa.cpp` 에서 심볼 `SoASimulation::UpdateSSE` 의
       정의를 찾지 못했습니다. 이름이 바뀌었거나 옮겨졌으면 앵커를 갱신하십시오.
```

**줄번호는 스스로 고친다.** 줄번호 앵커는 항상 WARN을 달고, 내용이 파일 안 다른
위치에서 발견되면 올바른 범위를 알려 준다. `--fix-ranges`가 그 자리에서 갱신한다.
단순히 줄이 밀린 경우만 고치고, 내용이 실제로 달라진 경우는 고치지 않고 error로 남긴다.

```
ERROR: ...:10: 스니펫이 `demo.c` 3-12 과 다릅니다.
    문서: return magic;
    소스: int magic = (buf[0] << 8) | buf[1];
    힌트: 같은 내용이 6-17 에 있습니다. --fix-ranges 로 앵커를 갱신할 수 있습니다.
```

**강한 앵커를 권한다.** 줄번호 앵커에는 매번 "심볼이나 region을 쓰라"는 WARN이
붙는다. 새 앵커는 `@region` 또는 `#심볼`로 다는 것을 기본으로 한다.

## 앵커를 다는 순서

1. 문서에서 *현재 소스와 같다고 주장하는* 펜스를 고른다. step 시점 스냅숏은 건너뛴다.
2. 대상을 정한다. 함수 하나면 `#심볼`, 함수 경계와 안 맞으면 소스에 region 마커를
   넣고 `@이름`, 파일 전체면 경로만.
3. 펜스 앞 줄에 앵커를 넣고 `python scripts/check_snippets.py <lab 경로>`를 돌린다.
4. 실패하면 둘 중 하나다 — 문서가 낡았거나, 앵커 범위가 펜스와 안 맞거나. 검사기가
   출력하는 `문서:` / `소스:` 두 줄이 어느 쪽인지 알려 준다.
