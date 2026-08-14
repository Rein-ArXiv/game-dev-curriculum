# languages/ 시리즈 검수 보고서 (2026-08)

- 대상: languages/ 8편 (5,018줄)
- 기준: **확정 가능한 사실오류·산술오류·컴파일 불가 코드·서지오류·내부모순**만. 문체·구성 취향은 대상 아님.
- 방법: 파일별 전문 정독 → 수치 재계산 → 서지 대조 → 코드 검증 → 문서 내부 정의와 사용처 대조 → **적대적 반박 검증**
- ~~이 보고서는 지적만 한다. 본문은 수정하지 않았다.~~ → **더 이상 사실이 아니다.** 검수는 **2026-08**에 수행됐고, 여기 실린 지적은 **2026-08-13**에 본문에 반영됐다. 항목별 반영 여부는 각 항목 제목의 `[반영됨]`/`[기각·본문 유지]` 표시와 문서 끝 `## 반영 상태 (2026-08-13)` 절을 보라.
- **아래 인용문과 행번호(L###)는 모두 수정 전(2026-08) 본문 기준이다.** 반영 후 행이 밀렸으므로 현재 파일과는 어긋난다. 원 지적의 근거를 보존하기 위해 그대로 둔다.

> 발견 41건 중 **32건 확정 / 9건 기각**. 검증자 8/8 정상 동작.
> 일부 항목은 검증자가 **실제로 코드를 컴파일·실행해** 판정했다.

---

## languages/part01_frontend_semantics.md — 확정 3건

### 1. L348-350 (6절 211행과 대조) — 내부모순 · **[반영됨 2026-08-13]**

> target: EntityHandle
>     10: Int literal -> Float conversion inserted
>   result: DamageResult

같은 러닝 예제 함수 `damage`의 시그니처가 6절과 9절에서 서로 다르다. 6절 211행은 `damage: (EntityId, Int) -> Void`로 선언했는데, 9절의 typed IR 예시는 (1) 첫 인자 타입을 `EntityHandle`로, (2) 둘째 인자 리터럴 `10`에 `Float` 변환을 삽입했다고(=둘째 매개변수가 Float라는 뜻) 적고, (3) 결과 타입을 `DamageResult`로 적는다. 선언이 `Int` 매개변수인데 Int 리터럴에 Float 변환이 삽입될 이유가 없고, 반환형 `Void`와 `DamageResult`도 양립하지 않는다. 호출 표현식 문자열 `"damage(target, 10)"`이 1절·6절과 동일하므로 다른 함수로 읽을 수도 없다.

**정정**: 9절 예시를 6절 선언과 일치시킨다. 예: `target: EntityId` / `10: Int literal` (변환 없음) / `result: Void`. 혹은 암묵적 변환을 보여주는 것이 목적이라면 6절의 선언 자체를 `damage: (EntityHandle, Float) -> DamageResult`처럼 바꿔 두 곳을 일치시켜야 한다.

<sub>검증: 인용 확인됨. 211행에 `damage: (EntityId, Int) -> Void`가 실재하고, 345행의 호출 표현식 문자열 `"damage(target, 10)"`은 31행(1절)의 러닝 예제와 동일하다. 348-350행은 (1) 첫 인자 `EntityHandle`, (2) `10: Int literal -> Float conversion inserted`, (3) `result: DamageResult`로 211행 선언과 세 지점 모두 어긋난다. 특히 매개변수가 Int인데 Int 리터럴에 Float 변환을 삽입할 이유가 없고, Void와 DamageResult는 양립 불가다. 방어 논거로 342행의 'overload 선택' 언급과 346행 `FunctionId(17)`을 근거로 '다른 오버로드로 해석'을 검토했으나, 문서는 damage의 오버로드 집합을 어디에도 선언하지 않았고 211행이 유일한 명시적 시그니처다. 문서 내부의 명시적 정의와 충돌하므로 인정한다. 정정은 9절을 211행에 맞추거나, 암묵적 변환 시연이 목적이면 211행 선언을 `(EntityHandle, Float) -> DamageResult`로 통일하면 된다.</sub>

### 2. L129-137 — 사실오류 · **[반영됨 2026-08-13]**

> stack:  expression + term
> lookahead: *

제시된 상태는 shift/reduce conflict의 예가 되지 못한다. 예시가 암묵적으로 쓰는 문법(expression → expression "+" term, term → term "*" factor)에서 `*`는 FOLLOW(expression) = {"+", ")", $}에 속하지 않으므로, lookahead가 `*`일 때 `expression + term -> expression` reduce는 애초에 유효한 동작이 아니다. 즉 SLR(1)/LR(1) 테이블에서 이 칸은 shift 하나로 결정되며 충돌이 없다. 이것은 오히려 lookahead가 LR(0) 항목 집합의 겉보기 충돌을 해소하는 표준 교과서 예제(Dragon Book)다. 덧붙여 이 예시의 생성 규칙(`expression + term`, `term * factor`)은 3절 67-75행에 제시된 이 글 자체의 문법(`+`는 term 규칙, `*`는 factor 규칙에 등장)과도 어긋난다.

**정정**: 실제 충돌 예로 바꾸어야 한다. 예를 들어 모호한 문법 `expression = expression, "+", expression | expression, "*", expression | NUMBER`에서 stack이 `expression + expression`이고 lookahead가 `*`인 상태를 쓰면 reduce와 shift가 모두 유효하여 진짜 shift/reduce conflict가 된다(dangling else도 표준 예). 혹은 현재 예시를 "lookahead가 충돌을 해소하는 경우"로 설명을 고쳐야 한다.

<sub>검증: 인용 확인됨(129행 `stack:  expression + term`, 130행 `lookahead: *`). 재검증 결과 검수자가 옳다. 이 예시가 쓰는 표준 문법(E→E+T|T, T→T*F|F, F→"("E")"|id)에서 FOLLOW(E)={"+", ")", $}이며 `*`는 FOLLOW(E)에 없다. 따라서 lookahead가 `*`일 때 `expression + term -> expression` reduce는 SLR(1)/LR(1) 액션 테이블에 존재하지 않고 shift 하나로 결정된다(bison에 이 층위 문법을 넣으면 conflict 0건). 이는 오히려 lookahead가 LR(0) 항목집합의 겉보기 충돌을 해소하는 Dragon Book의 표준 예제다. 그런데 137행은 '두 선택이 모두 가능한 상태가 shift/reduce conflict다'라고 이 예시에 직접 이어 단정하므로 잘못된 예시다. 실제 충돌 예(모호 문법 E→E+E|E*E에서 stack `expression + expression`, lookahead `*`; 또는 dangling else)로 바꾸거나, 설명을 'lookahead가 충돌을 해소하는 경우'로 고쳐야 한다. 다만 부차 지적인 '67-75행 문법과 규칙 이름이 다르다'는 별개 예시 문법 사용으로 볼 여지가 있어 단독 오류로는 약하다(67-75행 자체는 Crafting Interpreters의 term=가감/factor=승제 명명과 일치해 문제없음).</sub>

### 3. L105 — 내부모순 · **[반영됨 2026-08-13]**

> statement   = IDENT, ident_tail ;

96-99행의 원래 `statement` 규칙은 `"if" ...`, `"while" ...` 두 대안을 포함하는데, left factoring 결과라고 제시된 104-108행의 `statement`는 `IDENT, ident_tail` 하나만 남아 if/while 문을 더 이상 유도하지 못한다. left factoring은 언어를 보존하는 변환이므로, 변환 전후 문법이 같은 언어를 생성해야 한다는 이 절의 주장(공통 접두사만 묶는다)과 어긋난다.

**정정**: if/while 대안을 유지해야 한다. 예: `statement = "if", expression, statement | "while", expression, statement | IDENT, ident_tail ;` 와 `ident_tail = "=", expression, ";" | "(", arguments, ")", ";" ;`

<sub>검증: 인용 확인됨. 96-99행 `statement` 규칙에 `"if", expression, statement`와 `"while", expression, statement` 두 대안이 실재하는데, 104-108행은 `statement   = IDENT, ident_tail ;`로 종결자 `;`까지 붙인 완전한 규칙 재정의이며 생략 표기(`| ...`)가 없다. 따라서 변환 후 문법은 if/while 문을 더 이상 유도하지 못한다. left factoring은 언어를 보존하는 변환이고 102행도 '공통 접두사를 묶는' 것이라고 명시했으므로, 제시된 결과 문법은 그 서술과 모순된다. '관련 부분만 발췌한 것'이라는 방어를 검토했으나 발췌 표기가 전혀 없어 문법 정밀성을 다루는 절에서는 오해를 유발한다. `statement = "if", expression, statement | "while", expression, statement | IDENT, ident_tail ;`로 두 대안을 유지하는 정정이 타당하다.</sub>

---

## languages/part03_garbage_collection.md — 확정 9건

### 1. L955-956 — 코드오류 · **[반영됨 2026-08-13]**

> GC: 5 objects before, 2 after, 3 collected, next at 4
> GC: 6 objects before, 0 after, 6 collected, next at 8

7절 "완전한 구현" 코드를 그대로 실행하면 나올 수 없는 출력이다. (a) main()은 첫 gc(vm) 전에 정수 1,2 + pair, 정수 3,4 + pair 로 총 6개를 만들므로 "5 objects before"가 불가능하다. 첫 GC 시점 stack에는 (1,2) pair가 남아 있어 pair+int1+int2 = 3개가 생존하므로 "2 after"도 틀리다. (b) 875행 `if (vm->max_objects < 8) vm->max_objects = 8;` 때문에 max_objects는 8 미만이 될 수 없어 "next at 4"는 코드상 도달 불가능하다. (c) 마지막 GC 직전 stack에는 (1,2) pair가 계속 남아 있으므로 "0 after"도 불가능하다. (d) 882행의 자동 트리거(`num_objects >= max_objects`)로 두 번째 push_pair 도중 GC가 한 번 더 발생하는데 이 출력 줄이 통째로 빠져 있다. 코드를 그대로 시뮬레이션해 확인함.

**정정**: 실제 출력은 3줄이다:
GC: 6 objects before, 3 after, 3 collected, next at 8
GC: 8 objects before, 8 after, 0 collected, next at 16
GC: 9 objects before, 3 after, 6 collected, next at 8

<sub>검증: 인정. 780-950행 코드를 그대로 추출해 MSVC(cl)로 컴파일·실행한 결과 실제 출력은 3줄이며 검수자 주장과 정확히 일치했다: 'GC: 6 objects before, 3 after, 3 collected, next at 8' / 'GC: 8 objects before, 8 after, 0 collected, next at 16' / 'GC: 9 objects before, 3 after, 6 collected, next at 8'. 문서의 955-956행 2줄 출력은 전부 틀렸다. 근거를 하나씩 확인함: (a) 817행 `vm->max_objects = 8`, 882행 `if (vm->num_objects >= vm->max_objects) gc(vm)` → 첫 gc(vm) 호출 전 main이 만드는 객체는 int1,int2,pair1,int3,int4,pair2 = 6개이므로 'before 5' 불가. (b) 첫 GC 시점 stack=[pair1]이라 pair1+int1+int2 3개 생존 → 'after 2' 불가. (c) 875행 `if (vm->max_objects < 8) vm->max_objects = 8;`이 하한을 8로 고정하므로 'next at 4'는 코드상 도달 불가능. (d) 두 번째 push_pair 진입 시 num_objects=8 == max_objects=8이라 자동 GC가 한 번 더 발생하는데(중간 줄 '8 → 8, 0 collected, next at 16') 문서에 그 줄이 없다. 재현 스크립트: C:\Users\REIN\AppData\Local\Temp\claude</sub>

### 2. L1195 — 내부모순 · **[반영됨 2026-08-13]**

> // 더 좋음 (할당 없음)

바로 아래 코드는 `String[] cache = new String[1000000];`로 100만 원소 배열을 힙에 할당하고, 루프 첫 통과에서 `cache[i]`가 전부 null이므로 `cache[i] = "Item " + i;`가 100만 번 실행되어 String 100만 개를 새로 할당한다. 즉 "할당 없음"이 아니며, 게다가 그 100만 개를 배열이 강하게 붙잡아 전부 live set에 남기므로 앞의 StringBuilder 버전보다 GC 압력(특히 old 승격과 heap 점유)이 오히려 크다. 이 절의 주제(High GC Overhead 해결)와 정반대 예시다.

**정정**: 루프가 한 번만 도는 이 코드는 배열 + String 100만 개를 할당하고 전부 보존한다. "할당 없음"이라는 주석은 성립하지 않으며, 캐시가 이득이 되려면 같은 인덱스를 여러 번 재조회하는 경우여야 한다는 전제를 명시해야 한다.

<sub>검증: 인정. 1195-1202행을 직접 확인했다. 주석은 '// 더 좋음 (할당 없음)'인데 바로 아래 코드가 `String[] cache = new String[1000000];`로 100만 원소 배열을 할당하고, 루프가 인덱스 0..999999를 한 번씩만 도는 구조라 `cache[i]`는 매 반복 항상 null → `cache[i] = "Item " + i;`가 100만 번 실행된다. 즉 '할당 없음'은 코드가 하는 일과 정면으로 어긋난다. 게다가 배열이 100만 String을 강하게 참조해 live set에 전부 남으므로, 1188-1193행 StringBuilder 버전(생성 즉시 죽는 short-lived garbage)보다 old 승격·heap 점유 측면에서 GC 압력이 더 크다. 1164행 '시나리오 2: High GC Overhead' 해결책이라는 절 주제와도 반대다. 캐시가 이득이 되려면 같은 인덱스를 반복 조회하는 전제가 있어야 한다는 지적도 타당.</sub>

### 3. L439 — 사실오류 · **[반영됨 2026-08-13]**

> G1 GC (Java 7+)는 카드 테이블 대신 remembered set 사용:

G1은 카드 테이블을 대체한 것이 아니라 카드 테이블 '위에' remembered set을 얹은 구조다. G1의 write barrier는 대상 카드를 dirty로 표시해 dirty card queue에 넣고, refinement 스레드가 그 카드를 목적지 region의 RSet에 등록한다. 따라서 450행의 "정확한 포인터 (카드 전체 스캔 불필요)"도 틀리다 — G1의 RSet은 포인터가 아니라 카드 인덱스를 sparse/fine/coarse 세 단계로 저장하므로, Scan RS 단계에서 결국 해당 카드를 스캔한다(1072행 로그의 `[Update RS]`, `[Scan RS]` 항목이 바로 그 증거다).

**정정**: "G1 GC는 카드 테이블을 write barrier의 기록 수단으로 그대로 쓰면서, 그 카드들을 region별 remembered set으로 정리해 회수 대상 region만 독립적으로 수집할 수 있게 한다" 정도로. RSet 항목은 정확한 포인터가 아니라 카드 단위이므로 "카드 전체 스캔 불필요"도 "참조를 담은 카드만 골라 스캔"으로 고쳐야 한다.

<sub>검증: 인정. 439행 'G1 GC (Java 7+)는 카드 테이블 대신 remembered set 사용:'과 449-450행 '장점: - 정확한 포인터 (카드 전체 스캔 불필요)'를 확인했다. G1은 카드 테이블을 대체하지 않는다 — post-write barrier가 대상 카드를 dirty로 찍어 dirty card queue에 넣고 refinement 스레드가 그것을 목적지 region의 RSet에 등록하는, 카드 테이블 '위'의 구조다. '정확한 포인터'도 틀리다: G1 RSet 엔트리는 sparse(카드 인덱스 해시)/fine(region별 카드 비트맵)/coarse(region 단위)의 카드·region 입도이며 Scan RS 단계에서 결국 그 카드를 스캔한다. 문서 스스로 442-446행 예시를 'Region 1: {Region 5, Region 12}' 즉 region 입도 리스트로 그려놓고 450행에서 '정확한 포인터'라 한 것은 문서 내부 불일치이기도 하다. 1071-1072행 로그의 [Update RS]/[Scan RS]가 이 메커니즘의 증거라는 지적도 맞다. 다만 'Java 7+' 표기와 451행 'Region 단위 독립적 GC 가능'은 문제 없다.</sub>

### 4. L1261 — 개정잔재 · **[반영됨 2026-08-13]**

> JVM GC 파라미터 완전 가이드

이 하위 설명이 붙은 항목은 바로 위 1260행의 Microsoft, "Fundamentals of garbage collection"(learn.microsoft.com의 .NET 문서)이다. .NET CLR GC 문서에 "JVM GC 파라미터 완전 가이드"라는 설명이 달려 있다. 원래는 1258행의 Oracle "HotSpot Virtual Machine Garbage Collection Tuning Guide"에 붙어야 할 설명이 항목 삽입 과정에서 두 칸 아래로 밀린 것으로 보인다. 게다가 1262-1263행에 이미 ".NET Garbage Collection / CLR GC 내부 구조" 항목이 따로 있어 설명이 중복·엇갈린다.

**정정**: "JVM GC 파라미터 완전 가이드" 설명을 1258행 HotSpot Tuning Guide 항목 아래로 옮기고, 1260행 Microsoft "Fundamentals of garbage collection"에는 .NET 관련 설명을 달아야 한다.

<sub>검증: 인정. 1256-1263행을 직접 확인했다. 1258행 Oracle HotSpot GC Tuning Guide에는 설명이 없고, 1260행 Microsoft 'Fundamentals of garbage collection'(learn.microsoft.com의 .NET 문서) 아래에 ' - JVM GC 파라미터 완전 가이드'(1261행)가 붙어 있다. .NET CLR GC 문서에 JVM 파라미터 가이드 설명이 달린 명백한 항목-설명 어긋남이다. 게다가 1262-1263행에 '.NET Garbage Collection / CLR GC 내부 구조'가 별도로 있어 .NET 설명이 중복된다. 설명을 1258행 HotSpot 항목으로 옮기라는 정정도 합리적이다.</sub>

### 5. L300 — 서지오류 · **[반영됨 2026-08-13]**

> > "The Weak Generational Hypothesis: Most objects die young."

298행에서 '1984년 David Ungar의 "Generation Scavenging"은 핵심 통찰을 제시했다'라고 한 뒤 인용 블록으로 제시했으나, "weak generational hypothesis"라는 용어 자체는 Ungar(1984) 논문의 표현이 아니다. Ungar의 논문은 "most objects die young"이라는 관찰을 서술할 뿐이고, weak/strong generational hypothesis라는 명명은 이후 문헌(대표적으로 Jones 계열 GC 문헌)에서 정착한 것이다. 원문에 없는 문구를 원논문 직접 인용 형식으로 제시했다.

**정정**: 인용은 "most objects die young" 부분만 Ungar에 귀속시키고, "weak generational hypothesis"는 후대에 붙은 명칭임을 구분해 표기해야 한다(예: Ungar의 관찰을 오늘날 'weak generational hypothesis'라 부른다).

<sub>검증: 인정(경미). 298행 '1984년 David Ungar의 "Generation Scavenging"은 핵심 통찰을 제시했다:' 직후 300행에 영문 따옴표 + 인용 블록으로 'The Weak Generational Hypothesis: Most objects die young.'을 제시해 원논문 직접 인용 형식을 취했다. 그러나 'weak generational hypothesis'라는 명명(weak/strong 구분)은 Ungar(1984) 본문의 표현이 아니라 이후 문헌(Jones & Lins 계열 GC 문헌)에서 정착한 용어다. Ungar 논문은 'most objects die young'이라는 관찰만 서술한다. 실질 귀속(세대 가설의 통찰 = Ungar 1984)은 맞으므로 오류 자체는 가벼우나, 원전에 없는 문구를 인용부호로 원논문에 귀속시킨 것은 서지 표기 오류가 맞다. 'Ungar의 관찰을 오늘날 weak generational hypothesis라 부른다'로 분리 표기하라는 정정이 적절하다.</sub>

### 6. L1238 — 서지오류 · **[반영됨 2026-08-13]**

> - Christine H. Flood et al., ["Shenandoah: An Open-Source Concurrent Compacting Garbage Collector for OpenJDK"](https://developers.redhat.com/blog/2019/06/27/shenandoah-gc-in-jdk-13-part-1-load-reference-barriers), PPPJ 2016

링크 제목과 출처(PPPJ 2016 논문)가 실제 URL과 일치하지 않는다. 연결된 곳은 2019년 6월 Red Hat Developers 블로그의 "Shenandoah GC in JDK 13, part 1: Load reference barriers" 글이며, Flood 외의 PPPJ '16 논문이 아니다. 저자·연도·게재지 표기(2016, PPPJ)와 링크 대상(2019, 블로그)이 어긋난다.

**정정**: PPPJ 2016 논문 자체(ACM DL: 10.1145/2972206.2972210)를 링크하거나, 링크를 유지하려면 제목/저자/연도를 해당 블로그 글(Roman Kennke, Red Hat Developers, 2019)에 맞게 고쳐야 한다.

<sub>검증: 인정. 1238행 원문을 확인했다 — 링크 텍스트는 Flood 외의 PPPJ 2016 논문 제목 'Shenandoah: An Open-Source Concurrent Compacting Garbage Collector for OpenJDK'이고 저자·게재지를 'Christine H. Flood et al., ..., PPPJ 2016'으로 명기했는데, 실제 URL은 https://developers.redhat.com/blog/2019/06/27/shenandoah-gc-in-jdk-13-part-1-load-reference-barriers 즉 2019년 Red Hat Developers 블로그 'Shenandoah GC in JDK 13, part 1: Load reference barriers'(Roman Kennke)다. 저자·연도·게재지와 링크 대상이 전부 어긋난다. 논문 원문(ACM DL 10.1145/2972206.2972210)을 링크하거나 항목 표기를 블로그 글에 맞게 고쳐야 한다는 정정이 맞다.</sub>

### 7. L1240 — 서지오류 · **[반영됨 2026-08-13]**

> - Per Liden, Stefan Karlsson, ["ZGC: A Scalable Low-Latency Garbage Collector"](https://malloc.se/blog/zgc-jdk15), 2018

제목·저자·연도(2018년 Lidén & Karlsson의 발표)와 링크 대상이 불일치한다. malloc.se/blog/zgc-jdk15는 Per Lidén 개인 블로그의 "ZGC in JDK 15" 글(2020년, JDK 15 관련)로, 2018년 발표 자료가 아니며 공동 저자 표기와도 맞지 않는다.

**정정**: 2018년 발표 슬라이드/영상을 직접 링크하거나, 링크를 유지한다면 항목을 "Per Lidén, ZGC in JDK 15, 2020"으로 수정해야 한다.

<sub>검증: 인정. 1240행 원문 확인 — 'Per Liden, Stefan Karlsson, ["ZGC: A Scalable Low-Latency Garbage Collector"](https://malloc.se/blog/zgc-jdk15), 2018'. 링크 대상 malloc.se/blog/zgc-jdk15는 Per Lidén 개인 블로그의 'ZGC in JDK 15' 글(JDK 15 시점, 2020)로 제목·연도가 다르고 공저자 Stefan Karlsson 표기와도 맞지 않는다. 제목/저자/연도로 표기한 2018년 발표 자료와 링크가 불일치하는 서지 오류다. 8·10번과 같은 유형.</sub>

### 8. L1222 — 서지오류 · **[반영됨 2026-08-13]**

> - David F. Bacon, Perry Cheng, V.T. Rajan, ["A Unified Theory of Garbage Collection"](https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/unified-theory-gc/), OOPSLA 2004

링크 제목은 Bacon/Cheng/Rajan의 OOPSLA 2004 논문인데, URL은 Cornell CS6120(2019 가을) 수업의 학생 블로그 리뷰 글이다. 논문 원문이 아니라 제3자의 요약/리뷰를 원논문 서지 정보로 제시했다.

**정정**: ACM DL의 원논문(10.1145/1028976.1028982) 또는 저자 배포본을 링크하고, 리뷰 글은 별도 항목으로 분리해야 한다.

<sub>검증: 인정. 1222행 원문 확인 — 'David F. Bacon, Perry Cheng, V.T. Rajan, ["A Unified Theory of Garbage Collection"](https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/unified-theory-gc/), OOPSLA 2004'. URL 경로(courses/cs6120/2019fa/blog/)가 보여주듯 대상은 Cornell CS6120(2019 가을) 수업의 학생 리뷰 블로그 글이지 OOPSLA 2004 논문 원문이 아니다. 저자·게재지·연도를 원논문으로 표기하고 제3자 요약을 링크한 서지 불일치. ACM DL 원문(10.1145/1028976.1028982)이나 저자 배포본으로 교체하라는 정정이 맞다. 참고로 같은 절 1224·1226·1231·1233행은 dl.acm.org / 저자 배포본을 제대로 링크하고 있어 이 항목만 예외적으로 어긋난다.</sub>

### 9. L1292 — 서지오류 · **[반영됨 2026-08-13]**

> JVM 최적화와 GC 상호작용

바로 위 1291행의 참고문헌은 Aleksey Shipilev의 "The Black Magic of Java Method Dispatch"로, 가상/인터페이스 메서드 디스패치, JIT 인라이닝, 콜사이트 다형성을 다루는 글이다. GC와의 상호작용은 이 글의 주제가 아니며 본문에서 다뤄지지도 않는다. 또한 이 항목이 속한 절 제목이 "벤치마크 및 성능 분석"인데 설명만 GC를 언급해 다른 항목의 설명이 남은 것으로 보인다.

**정정**: 설명을 "메서드 디스패치와 JIT 인라이닝의 실측 분석" 등 실제 내용에 맞게 고치거나, GC 관련 참고문헌이 필요하면 별도 항목으로 교체해야 한다.

<sub>검증: 인정. 1291-1292행 확인 — 참고문헌은 Aleksey Shipilev, 'The Black Magic of Java Method Dispatch'(shipilev.net/blog/2015/black-magic-method-dispatch/)이고 설명이 'JVM 최적화와 GC 상호작용'이다. 해당 글은 가상/인터페이스 메서드 디스패치, 콜사이트 다형성(mono/bi/megamorphic), JIT 인라이닝을 다루며 GC와의 상호작용은 주제가 아니다. 항목이 속한 절도 1287행 '벤치마크 및 성능 분석'이라 GC 언급이 더 어색하다. 4번과 동일하게 항목-설명이 어긋난 사례다. 설명을 '메서드 디스패치와 JIT 인라이닝의 실측 분석' 등으로 고치라는 정정이 적절하다.</sub>

---

## languages/part05_jit_compilation.md — 확정 11건

### 1. L557, 564 — 코드오류 · **[반영됨 2026-08-13]**

> void compute(int n) {

Java 코드 블록인데 반환형이 `void`인 메서드 본문에서 564행 `return sum;`으로 int 값을 반환한다. Java에서 void 메서드는 값을 반환할 수 없으므로 컴파일 에러("incompatible types: unexpected return value")다. 바로 아래 Lua 대응 예제(581~589행)는 값을 반환하는 함수로 되어 있어 의도상 int 반환이 맞다.

**정정**: int compute(int n) {

<sub>검증: 파일 확인 결과 556행 펜스가 ```java이고 557행이 정확히 `void compute(int n) {`, 564행이 `    return sum;`이다. Java에서 void 메서드의 `return <expr>;`는 컴파일 에러(incompatible types: unexpected return value)로, 의사 코드가 아니라 명시적으로 java로 표기된 블록이다. 580~589행의 대응 Lua 예제가 `return sum`으로 값을 반환하고 본문 설명도 동일 함수를 전제하므로 의도는 int 반환이 맞다. 반박 불가.</sub>

### 2. L449, 454, 459, 461, 462, 467, 469, 470 — 코드오류 · **[반영됨 2026-08-13]**

> emit_mov_eax_imm(*code, node->value);

479~511행에서 emit_* 함수들은 모두 `unsigned char** code`를 첫 인자로 받도록 정의되어 있는데(예: 479행 `void emit_mov_eax_imm(unsigned char** code, int imm)`, 485행 `void emit_mov_eax_edi(unsigned char** code)`), codegen 본문은 한 번 역참조한 `*code`(= unsigned char*)를 넘긴다. 포인터 레벨이 하나 어긋나 C 제약 위반(incompatible pointer type)으로 컴파일되지 않는다. 반대로 527행 `emit_ret(&ptr)`과 526행 `codegen(ast, &ptr)`은 올바른 레벨을 넘기고 있어 내부적으로도 일관되지 않는다. 또한 `*code`를 넘기면 각 emit이 호출자의 포인터를 전진시키지 못해 코드가 같은 위치에 겹쳐 쓰인다.

**정정**: emit_mov_eax_imm(code, node->value); / emit_mov_eax_edi(code); / emit_push_eax(code); / emit_pop_ecx(code); / emit_imul_eax_ecx(code); / emit_add_eax_ecx(code); (모두 역참조 없이 code 전달)

<sub>검증: 479~511행 정의를 끝까지 확인했다. 6개 emit_* 함수 모두 첫 인자가 `unsigned char** code`이며(479 `void emit_mov_eax_imm(unsigned char** code, int imm)`, 485/490/494/498/504행도 동일) 본문에서 `*(*code)++` 로 이중 역참조를 쓴다. 반면 codegen 본문(449,454,459,461,462,467,469,470)은 `*code`(unsigned char*)를 넘긴다. 포인터 레벨 불일치로 C에서 제약 위반이고, 같은 파일 526~527행 `codegen(ast, &ptr)`/`emit_ret(&ptr)`은 올바른 레벨을 넘겨 내부적으로도 모순된다. 게다가 값 전달이므로 호출자 포인터가 전진하지 않아 동일 주소에 덮어쓴다. 확정.</sub>

### 3. L1084, 1085 — 코드오류 · **[반영됨 2026-08-13]**

> add eax, 16
>     cmp eax, [rdx+8]

eax를 바이트 오프셋으로 쓰면서(1083행 `[rdx+rax+16]`, 한 반복에 16바이트 = int 4개 전진) 원소 개수인 배열 길이 `[rdx+8]`과 직접 비교한다. 단위가 바이트 대 원소로 뒤섞여 루프가 Length/16회만 돌고, 배열의 1/4만 더한 뒤 종료한다. 같은 문서의 Tier 0 예제(1069, 1072행)는 `cmp ecx, [rdx+8]` / `[rdx+rcx*4+16]`로 원소 인덱스와 길이를 올바르게 비교하고 있어 두 예제가 서로 모순된다.

**정정**: 바이트 오프셋과 원소 개수의 단위를 맞춰야 한다. 예: `cmp eax, [rdx+8]`을 원소 인덱스 기준으로 바꾸거나(`add eax, 4` + 4배 스케일 주소), 길이를 미리 바이트 수로 변환해 비교(`mov ecx,[rdx+8]; shl ecx,2; cmp eax,ecx`).

<sub>검증: 1080~1086행을 직접 확인: `xor eax, eax` → `vpaddd xmm0, xmm0, [rdx+rax+16]` (주석 '4개 동시 덧셈') → `add eax, 16` → `cmp eax, [rdx+8]`. rax는 스케일 없는 바이트 오프셋이고 [rdx+8]은 .NET 배열의 원소 개수(Length)다. 반복당 16바이트 전진하므로 루프는 ceil(Length/16)회만 돌고 4*(Length/16)=Length/4 원소만 누산한다. 재계산해도 단위 불일치가 확정되며, 같은 문서 Tier 0 예제(1069 `cmp ecx,[rdx+8]`, 1071 `[rdx+rcx*4+16]`)는 원소 인덱스 기준으로 올바르게 비교해 두 예제가 서로 모순된다. 말미 `...`(1090)은 잔여 처리 생략을 뜻할 뿐 비교 피연산자 단위 오류를 변호하지 못한다.</sub>

### 4. L1088 — 코드오류 · **[반영됨 2026-08-13]**

> vextracti128 xmm1, xmm0, 1

VEXTRACTI128의 소스 피연산자는 256비트 ymm 레지스터여야 한다(형식: VEXTRACTI128 xmm1/m128, ymm2, imm8). 이 예제는 1080·1083·1089행에서 줄곧 128비트 xmm0만 누산기로 쓰므로, xmm0에서 상위 128비트 레인(imm8=1)을 뽑는 것은 인코딩 자체가 불가능하다. 128비트 누산기의 수평 합에는 레인 추출이 아니라 셔플이 필요하다.

**정정**: 누산기를 ymm0으로 바꾸고 `vextracti128 xmm1, ymm0, 1`을 쓰거나, 128비트를 유지한다면 `vpshufd xmm1, xmm0, 0x4E` 같은 셔플 기반 수평 합으로 대체

<sub>검증: 1088행 `vextracti128 xmm1, xmm0, 1`을 확인했다. VEXTRACTI128의 형식은 `xmm1/m128, ymm2, imm8`으로 소스는 반드시 256비트 ymm이어야 한다. 이 예제는 1080행 `vxorps xmm0, xmm0, xmm0`, 1083행 `vpaddd xmm0, xmm0, [rdx+rax+16]`(주석 '4개 동시 덧셈' = 128비트), 1084행 `add eax, 16`(=16바이트 스텝)까지 일관되게 128비트 누산기만 쓰므로 xmm0에서 상위 레인(imm8=1)을 추출하는 것은 인코딩 불가다. 128비트 수평 합에는 vpshufd/vphaddd 계열 셔플이 필요하다. 어셈블 자체가 실패하는 확정 오류.</sub>

### 5. L822, 823, 826, 827 — 코드오류 · **[반영됨 2026-08-13]**

> ; Guard: a가 SMI(Small Integer)인가?
> test eax, 1
> jz deopt_label

V8의 Smi 태그는 0이고 HeapObject 태그가 1이다(kSmiTag = 0, kHeapObjectTag = 1). 따라서 `test eax, 1`은 값이 Smi일 때 결과가 0이 되어 ZF=1이 되고, `jz deopt_label`은 Smi일 때 오히려 디옵티마이즈로 점프한다. 가드 극성이 반대라 정수 인자가 들어와도 830행의 빠른 정수 덧셈에 절대 도달하지 못한다(항상 deopt). 실제 V8이 내보내는 Smi 검사는 `testb rax, 1; jnz <not_smi>` 형태다.

**정정**: test eax, 1
jnz deopt_label  (b에 대한 826~827행도 동일하게 jnz)

<sub>검증: 821~830행을 확인: `; Guard: a가 SMI(Small Integer)인가?` / `test eax, 1` / `jz deopt_label` (b도 826~827 동일), 이어 830행 `add eax, edx`. 문서가 사용한 'SMI'는 V8 고유 용어이고 1018행은 아예 'TurboFan'으로 귀속하는데, V8의 규약은 kSmiTag=0 / kHeapObjectTag=1이다. 따라서 Smi일 때 test 결과가 0 → ZF=1 → `jz deopt_label`이 성립해 정수 인자에서 항상 디옵트하고 830행 빠른 경로에 도달하지 못한다. V8이 실제 내보내는 검사는 `testb ...,1` 후 non-zero일 때 not-smi로 분기하는 형태다. 문서를 구제하려면 태그비트=1(2n+1) 규약을 전제해야 하는데, 그러면 830행의 무보정 `add`가 틀리게 되므로 어느 독법에서도 이 두 예제가 동시에 옳을 수 없다. V8 귀속을 명시한 이상 가드 극성이 오류다.</sub>

### 6. L1028 — 내부모순 · **[반영됨 2026-08-13]**

> sub rax, 1        ; SMI tagging adjustment

동일한 Smi + Smi 덧셈을 다루는 830행의 예제는 `add eax, edx` 뒤에 아무 태그 보정도 하지 않는데, 여기서는 `sub rax, 1`로 보정한다. 두 서술이 서로 다른 태그 규약을 전제한다. V8 실제 규약(Smi 태그 비트 = 0, 값은 좌측 1비트 시프트)에서는 Smi 두 개를 더하면 결과도 그대로 올바른 Smi이므로 보정이 필요 없고, `sub rax, 1`은 값을 잘못 만든다(태그 비트를 1로 만들어 HeapObject 포인터로 오인시킨다). 반대로 `sub 1`이 필요한 것은 태그 비트가 1인 규약(2n+1)인데, 그 규약이면 830행 예제가 틀리게 된다.

**정정**: V8 Smi 규약에서는 보정 불필요 — `sub rax, 1` 행을 삭제(두 예제 모두 `add`만으로 완결)

<sub>검증: 1019~1029행(TurboFan 예제)의 `add rax, rsi` 뒤 `sub rax, 1        ; SMI tagging adjustment`와, 동일한 Smi+Smi를 다루는 830행 `add eax, edx`(보정 없음)를 대조 확인했다. 두 예제는 양립 불가능한 태그 규약을 전제한다: 태그비트=0(V8 실제, 값=2n)이면 덧셈 결과가 그대로 유효한 Smi여서 `sub 1`이 값을 망가뜨리고(최하위 비트를 1로 만들어 HeapObject 포인터로 오인), 태그비트=1(2n+1)이면 `sub 1`이 필요한 대신 830행이 틀린다. 문서가 V8/TurboFan을 명시했으므로 1028행이 잘못된 쪽이다. 내부모순 확정.</sub>

### 7. L775~782 — 코드오류 · **[반영됨 2026-08-13]**

> ; 직접 호출
> call Circle_getArea
> 
> slow_path:
>     call inline_cache_miss

monomorphic 빠른 경로가 `call Circle_getArea` 후 아무 분기 없이 slow_path 레이블로 흘러들어가, 캐시가 적중한 경우에도 `inline_cache_miss`가 항상 호출된다. 인라인 캐시의 요점(적중 시 O(1) 종료)과 정반대로 동작한다. 바로 다음 polymorphic 예제(793~798행)는 `jmp done`으로 fall-through를 막고 있어 두 예제의 처리가 불일치한다.

**정정**: call Circle_getArea 다음에 `ret`(또는 `jmp done`)을 넣어 slow_path로 흘러들지 않게 한다

<sub>검증: 773~783행을 확인: `cmp [obj], CircleType` / `jne slow_path` / `call Circle_getArea` 다음에 아무 분기 없이 `slow_path:` 레이블과 `call inline_cache_miss`가 이어진다. 가드가 적중해 fast path로 간 경우에도 fall-through로 miss 핸들러가 반드시 호출되어, inline cache의 요지(적중 시 즉시 종료)와 정반대가 된다. 저자 스스로 바로 다음 polymorphic 예제(793~798행)에서 `jmp done`으로 fall-through를 차단하고 있어 동일 문서 내 처리 방식이 불일치한다. 단순 축약이 아니라 제어 흐름 오류다.</sub>

### 8. L596~604 — 내부모순 · **[반영됨(부분) 2026-08-13 — 잔여 있음, 끝 절 참조]**

> ; 분기 제거됨 (항상 true였으므로)

바로 위 Recording phase 로그(596행)는 `i=1: is_prime(1) → false → skip`으로 분기가 false였던 반복을 기록해 놓았는데, 컴파일 단계 설명은 그 분기가 "항상 true였으므로" 제거되었다고 말한다. 기록된 트레이스와 그에 대한 설명이 어긋난다(추가로 594행은 "루프를 한 번 실행하며" 기록한다고 해놓고 로그에는 i=1,2,3 세 번의 반복이 나열되어 있다).

**정정**: false였던 i=1 줄을 로그에서 빼거나(트레이스는 true 경로가 잡힌 반복부터 기록됨), 설명을 "기록된 경로에서는 true였으므로 그 경로만 남기고 guard를 삽입"으로 수정

<sub>검증: 594~608행 확인: Recording 로그 596행이 `i=1: is_prime(1) → false → skip`으로 분기가 false였던 반복을 명시해 놓았는데, 604행 주석은 `; 분기 제거됨 (항상 true였으므로)`라고 서술한다. 기록된 트레이스와 그에 대한 설명이 직접 충돌한다. 더구나 610~613행이 `guard (is_prime(i) == true)`로 분기 가정을 guard로 대체한다고 설명하므로, '항상 true였다'는 근거 자체가 문서의 다음 단계와도 어긋난다(guard가 필요하다는 것은 항상 true가 아니라는 뜻). 594행 '루프를 한 번 실행하며'와 i=1,2,3 세 반복 로그의 불일치도 부수적으로 성립한다.</sub>

### 9. L351 — 서지오류 · **[반영됨 2026-08-13]**

> Intel 문서에 따르면 수백~수천 사이클 소요.

`mprotect`는 POSIX 운영체제 시스템 콜이며 Intel SDM 등 Intel 아키텍처 문서에는 이 API도, 그 비용(사이클 수)도 기술되어 있지 않다. Intel 문서가 다루는 것은 INVLPG·CR3 재적재 같은 명령의 동작이지 OS 시스템 콜의 실측 비용이 아니다. 근거 없는 출처 귀속이다.

**정정**: 출처 귀속을 제거하고 "시스템 콜 진입과 TLB 무효화(멀티코어에서는 TLB shootdown IPI 포함) 때문에 수백~수천 사이클 규모의 비용이 든다" 정도로 서술하거나, 실제 측정 문헌을 인용

<sub>검증: 351행은 `**성능 비용**: \`mprotect\`는 시스템 콜이며, TLB(Translation Lookaside Buffer) 플러시를 유발한다. Intel 문서에 따르면 수백~수천 사이클 소요.`다. mprotect는 POSIX OS 시스템 콜로 Intel SDM/최적화 매뉴얼의 서술 대상이 아니며, Intel 문서는 INVLPG·CR3 재적재 등 명령의 아키텍처 동작을 규정할 뿐 OS 시스템 콜의 실측 사이클 비용을 제시하지 않는다(해당 수치대는 통상 Agner Fog 계열 실측표나 커널 벤치마크 출처다). 수치 자체는 그럴듯하나 출처 귀속이 근거 없다. 서지오류로 인정.</sub>

### 10. L346 — 내부모순 · **[반영됨 2026-08-13]**

> 페이지 테이블 엔트리: [물리 주소 | R | W | X | NX]

X 비트와 NX 비트를 한 엔트리에 동시에 나열했는데 둘은 서로의 부정이라 공존할 수 없다. 325행에서 문서 스스로 "페이지 테이블의 각 엔트리에 1비트 추가"(NX 한 비트)라고 설명한 것과도 모순된다. 실제 x86-64 PTE에는 별도의 실행 허용(X) 비트가 없고 실행 권한은 오직 NX(XD, 비트 63)로만 제어되며, 읽기 전용 비트도 따로 없이 Present 비트가 읽기를 함의한다.

**정정**: [물리 주소 | Present | R/W | U/S | ... | NX] 처럼 X를 빼고 NX 하나만 실행 제어 비트로 표기

<sub>검증: 344~349행 확인: `페이지 테이블 엔트리: [물리 주소 | R | W | X | NX]`와 그 아래 네 비트를 가리키는 화살표 + 'mprotect가 이 비트들을 변경'. X와 NX는 서로의 부정이라 독립 비트로 한 엔트리에 공존할 수 없다. 또 이 절은 x86-64 NX/DEP를 다루면서 326행에서 문서 스스로 '페이지 테이블의 각 엔트리에 1비트 추가'(=NX 한 비트)라고 명시해 놓았으므로 내부모순이다. 실제 x86-64 PTE에는 별도 X 비트가 없고 실행 권한은 XD(비트 63) 하나로만 제어되며 읽기는 Present가 함의한다. 일반화된 개념도라 해도(RISC-V처럼 R/W/X를 두는 아키텍처가 있어도) X와 NX를 동시에 나열한 시점에서 변호되지 않는다.</sub>

### 11. L18 — 개정잔재 · **[반영됨 2026-08-13]**

> 8. [실전: LuaJIT, V8,.NET의 구현](#8-실전-luajit-v8-net의-구현)

실제 제목은 911행 `## 8. 실전: LuaJIT, V8,.NET의 구현`으로 "V8,.NET" 사이에 공백이 없다. 마크다운 앵커 생성 규칙상 구두점(`,` `.`)이 제거되고 공백만 하이픈이 되므로 실제 앵커는 `#8-실전-luajit-v8net의-구현`이 되어, 목차의 `v8-net`과 일치하지 않아 링크가 끊긴다. 목차 1~7번 항목은 모두 규칙대로 일치하므로 8번만 어긋난 상태다.

**정정**: 8. [실전: LuaJIT, V8,.NET의 구현](#8-실전-luajit-v8net의-구현)

<sub>검증: 목차 18행 `8. [실전: LuaJIT, V8,.NET의 구현](#8-실전-luajit-v8-net의-구현)`과 실제 제목 911행 `## 8. 실전: LuaJIT, V8,.NET의 구현`을 대조 확인했다. 'V8,' 과 '.NET' 사이에 공백이 없다. GitHub 앵커 규칙(소문자화 → 구두점 제거 → 공백을 하이픈)으로 계산하면 `8-실전-luajit-v8net의-구현`이 되어 목차의 `v8-net`과 불일치, 링크가 끊긴다. 대조군으로 1~7번을 직접 계산해 보니(예: 23행 → #1-인터프리터-vs-컴파일러-그-사이의-jit, 308행 'W^X 보호와 mprotect' → #3-wx-보호와-mprotect, 803행 → #7-디옵티마이제이션-추측이-틀렸을-때) 모두 목차와 정확히 일치한다. 8번만 어긋난 것이 맞다.</sub>

---

## languages/part07_reflection_metadata.md — 확정 9건

### 1. L53 — 사실오류 · **[반영됨 2026-08-13]**

> UPROPERTY(EditAnywhere, Category="Stats", ClampMin=0, ClampMax=100)

ClampMin/ClampMax는 Unreal의 UPROPERTY 메타데이터 지정자이므로 반드시 meta=(...) 안에 들어가야 하며, 최상위 지정자로 쓰면 UHT가 인식하지 못한다. 같은 문서 280행은 동일한 지정자를 `meta=(ClampMin=0, ClampMax=100)` 형태로 올바르게 쓰고 있어 내부적으로도 서로 어긋난다.

**정정**: UPROPERTY(EditAnywhere, Category="Stats", meta=(ClampMin=0, ClampMax=100)) 로 표기 (280행과 동일한 형태)

<sub>검증: 인용문은 53행에 그대로 존재(확인). Unreal의 UPROPERTY 최상위 지정자 목록에 ClampMin/ClampMax는 없으며 UHT는 미지의 지정자에 대해 'Unknown variable specifier' 에러를 낸다. 반박을 시도했으나(이 블록은 1장의 개념 설명이고 56행 prop->GetMetadata<float>(...)는 Unreal 실제 API가 아닌 범용 의사 API다) 방어가 성립하지 않는다. 같은 문서 280행이 동일한 지정자를 meta=(ClampMin=0, ClampMax=100)로 정확히 표기하고 있어 문서 내부에서 직접 충돌한다. 저자가 올바른 형태를 알고 있음이 280행에서 드러나므로 53행은 단순 오기다. 인정.</sub>

### 2. L214 — 코드오류 · **[반영됨 2026-08-13 — 잔여 있음, 끝 절 참조]**

> else if (prop->size == sizeof(int)) {

주류 플랫폼에서 sizeof(float)==sizeof(int)==4이므로 210행의 첫 조건이 항상 먼저 참이 되어 이 else-if 분기는 도달 불가능한 죽은 코드다. 실제로 178~181행 예제가 등록한 `int Ammo`(크기 4)는 float로 잘못 해석되어 SliderFloat로 표시되고 비트 패턴을 부동소수점으로 읽는 잘못된 값이 나온다. 크기로 타입을 판별하는 로직 자체가 성립하지 않는다.

**정정**: PropertyInfo에 타입 태그(예: enum PropertyType type)를 두고 `prop->type == PropertyType::Float / Int`로 분기해야 한다

<sub>검증: 210·214행 확인. 주류 플랫폼에서 sizeof(float)==sizeof(int)==4이므로 210행이 항상 먼저 참이 되어 214행 분기는 도달 불가능하다. 이는 추상적 지적이 아니라 문서 자신의 예제로 재현된다: 156~162행 Character는 float Health와 int Ammo를 갖고 180행이 REGISTER_PROPERTY(Character, Ammo, ...)로 sizeof=4를 등록하므로, 204행 ShowProperties는 Ammo를 SliderFloat로 그리고 정수 비트 패턴을 float로 읽는다. 의사 코드라는 방어는 통하지 않는다 — 서술된 메커니즘(크기로 타입 판별) 자체가 성립하지 않고, 문서가 제시한 입력에 대해 잘못된 동작을 낳는 논리 오류다. 233~245행 단점 목록에도 이 한계는 언급되지 않아 의도된 반례 제시도 아니다. 인정.</sub>

### 3. L655 — 코드오류 · **[반영됨 2026-08-13]**

> ImGui::BeginDisabled();

ImGui::BeginDisabled() 호출에 대응하는 ImGui::EndDisabled()가 루프 안 어디에도 없어 Begin/End 스택이 어긋난다(ImGui는 프레임 종료 시 어서션으로 잡는다). 더구나 EditCondition 검사가 해당 프로퍼티 위젯을 이미 그린 뒤(607~647행 다음)에 수행되므로, 설령 짝을 맞춘다 해도 현재 프로퍼티에는 비활성화가 적용되지 않고 다음 프로퍼티에 적용된다.

**정정**: EditCondition 검사를 위젯을 그리기 전으로 옮기고, 그린 뒤에 `if (disabled) ImGui::EndDisabled();`로 짝을 맞춰야 한다

<sub>검증: 655행 ImGui::BeginDisabled() 확인, EndDisabled는 파일 전체에 존재하지 않음(grep 확인). ImGui의 BeginDisabled/EndDisabled는 스택 짝 맞춤이 강제되며 프레임 종료 시 어서션으로 걸린다. 게다가 649~657행의 EditCondition 검사는 607~647행에서 해당 프로퍼티 위젯을 이미 그린 뒤 for 루프 말미에 위치하므로, 짝을 맞춘다 해도 비활성화는 현재 프로퍼티가 아니라 다음 반복의 프로퍼티에 적용된다. 두 결함 모두 문서에 적힌 코드만으로 확인되는 순서·짝 오류다. 인정.</sub>

### 4. L630 — 내부모순 · **[반영됨 2026-08-13]**

> if (ImGui::BeginCombo(prop->GetName(), enumProp->GetValueName(*value))) {

같은 함수 GetValueName을 두 가지 다른 의미의 인자로 호출한다. 631~633행에서는 `for (int i = 0; i < enumProp->GetValueCount(); i++)`의 인덱스 i를 넘기고 `*value = enumProp->GetValue(i)`로 인덱스→값 변환을 하므로 GetValueName은 인덱스를 받는 함수인데, 630행에서는 인덱스가 아니라 열거형의 실제 값 `*value`를 넘긴다. 값과 인덱스가 다른 열거형(예: 값이 1,2,4인 플래그)에서는 잘못된 이름이 표시되거나 범위를 벗어난다.

**정정**: 630행은 `enumProp->GetValueName(enumProp->FindIndexByValue(*value))`처럼 값→인덱스 변환을 거치거나, GetValueName을 값 기준으로 통일해야 한다

<sub>검증: 630~634행 확인. 632행이 루프 인덱스 i를 GetValueName에 넘기고 633행이 GetValue(i)로 인덱스→값 변환을 하므로, 이 문서가 정의한 API에서 GetValueName의 인자는 인덱스다. 그런데 630행은 열거형의 실제 값 *value를 넘긴다. '값과 인덱스가 같으니 무해하다'는 방어를 검토했으나, 값==인덱스라면 633행의 GetValue(i) 변환 자체가 존재할 이유가 없다 — API가 둘을 구분한다는 것이 코드로 증명된다. 626~637행은 특정 enum이 아니라 모든 EnumProperty에 대한 범용 코드이므로 477행 State가 0,1이라는 점도 방어가 되지 못한다. 동일 함수의 인자 의미가 4줄 안에서 어긋나는 내부 모순. 인정.</sub>

### 5. L480 — 코드오류 · **[반영됨 2026-08-13]**

> ("Idle", State::Idle)

RTTR의 열거형 등록 문법이 아니다. rttr::registration::enumeration의 operator()는 rttr::value(...)로 감싼 enum_data 목록을 한 번에 받는다. 문자열과 열거 상수를 그대로 두 개의 인자로 넘기고 operator()를 두 번 체이닝하는 이 코드는 컴파일되지 않는다.

**정정**: rttr::registration::enumeration<State>("State")( rttr::value("Idle", State::Idle), rttr::value("Running", State::Running) ); 형태로 작성

<sub>검증: 478~482행 확인. 438행에서 <rttr/registration.h>를 include하고 444행에서 (rttr::metadata("min", 0.0f), rttr::metadata("max", 100.0f))를 정확한 RTTR 문법으로 쓰고 있어 이 절은 의사 코드가 아니라 실제 라이브러리 사용 예다. RTTR의 enumeration 바인딩은 registration::enumeration<E>("E")( rttr::value("Idle", E::Idle), rttr::value("Running", E::Running) ) 형태로 rttr::value()가 만든 enum_data 목록을 단일 operator() 호출로 받는다. 문자열과 enum 상수를 날것으로 넘기고 operator()를 두 번 체이닝하는 480~481행은 어떤 RTTR 버전 관례로도 성립하지 않는다(구버전 0.9.x도 value() 래핑을 요구). 같은 문서가 property의 metadata는 올바르게 썼다는 점에서 내부적으로도 일관되지 않는다. 인정.</sub>

### 6. L325 — 내부모순 · **[반영됨 2026-08-13]**

> Class->SetMetaData(TEXT("ClampMin"), TEXT("0"));

280행에서 ClampMin/ClampMax는 `int Ammo` 프로퍼티에 붙은 meta인데, 생성 코드는 이를 프로퍼티가 아니라 UClass에 등록하고 있다. 문서의 다른 모든 곳(56행 prop->GetMetadata<float>("ClampMin"), 609·612행 prop->GetMetadataFloat/HasMetadata)은 메타데이터를 프로퍼티에서 조회하므로 등록 주체와 조회 주체가 어긋난다. 실제 Unreal도 meta는 해당 필드(FProperty/UField)에 저장한다.

**정정**: 생성 코드가 Ammo 프로퍼티 객체에 대해 SetMetaData를 호출하도록(예: AmmoProp->SetMetaData(TEXT("ClampMin"), TEXT("0"))) 표기

<sub>검증: 280행에서 ClampMin/ClampMax는 int Ammo 프로퍼티의 meta=()로 선언되는데, 325~326행 생성 코드는 이를 Class->SetMetaData로 UClass에 등록한다. 실제 Unreal에서 프로퍼티 meta는 해당 필드(FProperty/UField)에 저장되며 생성 코드도 프로퍼티별 MetaDataArray로 내보낸다 — 클래스 레벨 meta와 프로퍼티 레벨 meta는 별개 저장소다. 더구나 이 문서의 소비 측 코드(56행 prop->GetMetadata<float>("ClampMin"), 609·610행 prop->GetMetadataFloat, 612행 prop->HasMetadata)는 전부 프로퍼티에서 조회하므로 등록 주체와 조회 주체가 문서 안에서 어긋난다. 단순화된 예시라는 방어를 검토했으나, 이는 표기 축약이 아니라 '프로퍼티에 붙인 meta가 클래스에 등록된다'는 잘못된 모델을 가르치는 의미 차이다. 인정(중간).</sub>

### 7. L347 — 사실오류 · **[반영됨 2026-08-13]**

> UHT는 완전한 C++ 파서가 아니다. 정규표현식과 간단한 파서로 매크로만 찾는다.

UHT가 완전한 C++ 파서가 아니라는 앞 문장은 맞지만, 구현이 정규표현식 기반이라는 서술은 사실이 아니다. UHT는 구 C++ 버전(BaseParser/HeaderParser)이든 UE5의 C# 재작성 버전(UhtTokenizer)이든 손으로 작성한 토크나이저·재귀 하강 파서를 사용하며 정규표현식으로 매크로를 스캔하지 않는다. 같은 이유로 372행의 "때로 주석 안의 매크로도 파싱한다"도 성립하지 않는다(토크나이저 단계에서 주석을 분리한다).

**정정**: "UHT는 완전한 C++ 파서가 아니다. 손으로 작성한 토크나이저와 제한적인 재귀 하강 파서로 UCLASS/UPROPERTY 같은 매크로 선언만 인식한다" 정도로 수정(아래 regex 예제는 '개념 설명용 의사코드'임을 명시)

<sub>검증: 347행 확인. '완전한 C++ 파서가 아니다'는 맞지만 '정규표현식과 간단한 파서로 매크로만 찾는다'는 구현 서술이 사실과 다르다. UHT는 UE3~UE4의 C++ 구현(FBaseParser/FHeaderParser의 GetToken 기반 손수 작성 토크나이저 + 재귀 하강)이든 UE5의 C# 재작성(EpicGames.UHT의 렉서/토크나이저)이든 정규표현식으로 매크로를 스캔한 적이 없다. '버전에 따라 문서가 맞을 수 있다'는 방어를 검토했으나 regex 기반이었던 UHT 버전은 존재하지 않는다. 349~366행 코드 블록이 '(단순화)'로 표기된 것은 코드에 대한 면책일 뿐, 347행 산문이 도구의 실제 구현을 단정하는 서술이라는 점을 바꾸지 못한다(제안된 정정도 이 블록을 '개념용 의사코드'로 명시하라는 취지로 타당). 다만 372행 '주석 안의 매크로도 파싱한다'에 대한 부수 주장은 근거가 약하고(#if 조건부 컴파일 한계와의 혼동 가능성) 판정에 영향을 주지 않는다. 인정.</sub>

### 8. L871 — 서지오류 · **[반영됨 2026-08-13]**

> Jason Gregory, "Game Engine Architecture", 3rd Edition - Chapter 15.4: Reflection and Type Info

3판의 Chapter 15는 "Introduction to Gameplay Systems"이고 15.4는 "The Game World Editor"다. "Reflection and Type Info"라는 제목의 절은 존재하지 않으며, 런타임 객체 모델·타입 스키마 관련 내용은 Chapter 16(Runtime Gameplay Foundation Systems)에 있다.

**정정**: 장절 표기를 실제 목차에 맞게 수정(예: Chapter 15.3 Data-Driven Game Engines / Chapter 16 Runtime Gameplay Foundation Systems)

<sub>검증: 871행 확인. Game Engine Architecture 3판에서 Chapter 15는 'Introduction to Gameplay Systems'이고 하위 절은 15.1 Anatomy of a Game World, 15.2 Implementing Dynamic Elements: Game Objects, 15.3 Data-Driven Game Engines, 15.4 The Game World Editor다. 'Reflection and Type Info'라는 제목의 절은 3판 어디에도 없다. 런타임 객체 모델·타입 스키마 논의는 Chapter 16 Runtime Gameplay Foundation Systems(특히 16.2 Runtime Object Model Architectures)에 있다. 판본 혼동 가능성(2판은 14/15장)도 검토했으나 어느 판에서도 해당 절 제목은 존재하지 않는다. 서지 오류 인정.</sub>

### 9. L872 — 서지오류 · **[반영됨 2026-08-13]**

> Martin Reddy, "API Design for C++" - Chapter 6: Reflection

이 책의 Chapter 6은 "C++ Usage"이며, Reflection이라는 제목의 장은 존재하지 않는다. 스크립트 바인딩·플러그인 등 관련 내용은 Chapter 11(Scripting)과 Chapter 12(Extensibility)에 있다.

**정정**: Chapter 11: Scripting 또는 Chapter 12: Extensibility로 장 표기 수정

<sub>검증: 872행 확인. Martin Reddy, API Design for C++의 Chapter 6은 'C++ Usage'이며(1 Introduction, 2 Qualities, 3 Patterns, 4 Design, 5 Styles, 6 C++ Usage, 7 Performance, 8 Versioning, 9 Documentation, 10 Testing, 11 Scripting, 12 Extensibility), 'Reflection'이라는 제목의 장은 없다. 2판에서도 장 구성은 동일해 판본 차이로 방어되지 않는다. 이 책에서 리플렉션에 인접한 논의는 Chapter 11 Scripting과 Chapter 12 Extensibility다. 서지 오류 인정.</sub>

---

## 검증에서 기각된 것 (참고) — 9건 전부 **[기각 · 본문 유지 확인됨 2026-08-13]**

> 2026-08-13 반영 작업에서 이 9건은 **의도적으로 손대지 않았다**. diff로 확인함:
> `part02_execution_runtime.md`, `part04_compiler_optimization_abi.md`, `part06_engine_scripting.md`는 변경 0건이고,
> `part03`의 118·187행 주변과 `part05`의 481행, `part07`의 192행도 변경 hunk 밖이다.
> **이 항목들은 고치면 안 되는 것이다.** 다음 검수에서 다시 지적으로 올리지 마라.

- languages/part02_execution_runtime.md 79: 인용문은 79행에 실재하나(줄 드리프트 없음) 주장은 성립하지 않는다. (1) 문서는 ReadOp/ReadTarget/Push/Pop/Peek 중 어느 것도 정의하지 않는다(파일 전체 grep 결과 이 식별자들은 74-83행 코드 블록에만 등장). ip 전진 규약이 전적으로 미정의 헬퍼에 위임되어 있으므로 '문서 내부 정의와 어긋난다'는 판정 근거가 존재하
- languages/part02_execution_runtime.md 78: 인용문은 78행에 실재하나 결함이 아니다. (1) 평가 순서가 unspecified인 것은 맞으나 Add는 가환이며 두 호출 모두 부작용이 '스택 포인터 2회 감소'로 동일하다. 어느 순서든 스택에서 2개를 pop하고 합을 push하므로 최종 스택 상태와 결과 값이 동일하다. 관찰 가능한 차이가 없다. UB도 아니다(C++17에서 두 함수 호출은 indet
- languages/part03_garbage_collection.md 118: 기각. 118행은 '1960년 McCarthy의 원조 알고리즘:'이고 125-131행은 mark 단계를 white/gray/black 작업 큐로 서술하지만, 문서 어디에서도 'McCarthy가 tri-color를 고안했다'고 말하지 않는다. 143행은 'Dijkstra et al. (1978)에서 제안'이라며 3색 표기의 출처를 정확히 밝히고 있어 서지 사
- languages/part03_garbage_collection.md 187: 기각(스타일 제안). 사실 확인은 검수자 말이 맞다 — 103-104행의 `obj->ref_count`는 들어오는 참조 카운트, 187행 struct의 `ref_count`는 205행 `for (size_t i = 0; i < obj->ref_count; i++) mark(obj->refs[i]);`에서 보듯 refs[] 배열의 길이다. 그러나 이는 서로 
- languages/part04_compiler_optimization_abi.md 59: 인용문은 실재한다(44행, 59행 모두 원문 그대로 확인). 그러나 '오류'가 아니라 편집 취향 제안이므로 범위 밖이다.

1) 중복이 아니라 개요→본론 구조다. 44행은 2절 도입부에서 CFG의 용도를 열거하는 문장('CFG는 도달 가능성, 루프, 지배 관계, 예외 경로를 분석하는 기반이다')에 딸린 한 줄 각주성 설명이고, 59행은 15줄 뒤 별도 하위
- languages/part05_jit_compilation.md 481: 481행 `*(int*)(*code) = imm;`은 실재한다. 그러나 277~278행의 memcpy 주석은 문서 전체에 적용되는 규범 선언이 아니라, 지역 배열 `template`의 오프셋 +3(구조상 4바이트 정렬이 보장되지 않는 위치)에 imm32를 넣는 '그 스니펫'의 이유 설명이다. 게다가 278행은 'x86-64 호스트를 전제하며'라고 플랫폼을 
- languages/part06_engine_scripting.md 156: 인용문은 156행에 실재하지만 '뒤 서술과 어긋난다'는 주장은 문서를 잘못 읽은 것이다. (1) 같은 156행의 바로 앞 절이 '다중 VM은 서로를 모르게 격리해 안전하고, 서로 다른 스레드에 배치하기 좋지만(아래)'라고 명시하며 2절 멀티스레딩 소절로 전방참조까지 걸어둔다. 문제의 문장은 스레드 배치 가능성을 이미 전제한 같은 문장의 후반부이며, VM 분
- languages/part06_engine_scripting.md 393: 393/399/407행의 package.loaded와 require, 456행의 'io, os, package, debug 등은 등록하지 않음', 118행의 OpenSafeLibraries 호출은 모두 실재한다. 그러나 '두 절이 모순'이라는 결론은 6절이 스스로 세운 전제를 무시한 것이다. (1) 6절은 코드를 보이기 전에 437~441행 표로 신뢰 수준
- languages/part07_reflection_metadata.md 192: AddProperty가 파일 전체에서 192행에만 등장하고 125~132행 TypeInfo 정의에 선언이 없다는 사실 자체는 맞다(grep 확인). 그러나 이 블록은 '### 기본 구조'의 스케치이며 완결된 정의를 표방하지 않는다. 결정적으로 같은 절이 사용하는 PropertyInfo는 문서 어디에도 정의가 없는데(129·207·226·562행에서 ->of

## 확정 오류 0건인 파일

`README.md`, `part02_execution_runtime.md`, `part04_compiler_optimization_abi.md`, `part06_engine_scripting.md`

---

## 반영 상태 (2026-08-13)

**검수 시점: 2026-08 / 반영 시점: 2026-08-13.**
이 절은 위 지적들이 실제로 본문에 반영됐는지를 기록한다. 판정 근거는 추측이 아니라 아래 diff다:

```
diff -ru --strip-trailing-cr VeLog_backup_2026-08-11/languages VeLog/languages \
     -x "REVIEW_*" -x "REDESIGN_*" -x diagrams -x build
```

(백업 `C:/Users/REIN/Documents/VeLog_backup_2026-08-11/languages/`는 수정 전 원본이다. `diagrams/`는 백업이 텍스트만 담은 탓에 비교 대상에서 제외했다.)

### 집계

| 분류 | 건수 | 비고 |
|---|---|---|
| **반영됨** | **32** | 확정 32건 전부. diff에 대응 변경이 있음 |
| **기반영** | **0** | 보고서 작성 후 먼저 고쳐져 있던 항목 없음 — 이 보고서는 낡지 않았다 |
| **기각** | **9** | `## 검증에서 기각된 것` 절. 본문 무변경 확인. **고치면 안 되는 것** |
| **이월** | **0건(전건)** / **2건(부분 잔여)** | 통째로 미반영된 지적은 없다. 아래 "잔여 사항" 참조 |
| 합계 | 41 | 보고서 머리말의 "발견 41건"과 일치 |

파일별 반영 건수 (diff 기준):

| 파일 | 확정 | 반영됨 | 변경 hunk |
|---|---|---|---|
| `part01_frontend_semantics.md` | 3 | 3 | 3 |
| `part03_garbage_collection.md` | 9 | 9 | 9 |
| `part05_jit_compilation.md` | 11 | 11 | 10 (1028행 항목은 1019~1029 hunk에 통합) |
| `part07_reflection_metadata.md` | 9 | 9 | 9 |
| `README.md`, `part02`, `part04`, `part06` | 0 | 0 | **0 (무변경)** |

### 잔여 사항 — 다음에 이 저장소를 여는 사람이 볼 것

전건 이월은 없지만, **주 정정만 반영되고 부수 지적이 남은 항목이 2건** 있다. 남은 것은 이게 전부다.

1. **`part05_jit_compilation.md` — 5절 Tracing JIT 로그 (L594~604 — 앞 hunk들이 행수 중립이라 현재도 같은 행)**
   - 반영된 것: 컴파일 단계 주석이 `; 분기 제거됨 (항상 true였으므로)` → `; 분기 제거됨 (기록된 경로에서는 true였으므로 그 경로만 남기고 guard 삽입)`으로 수정됨(정정 2안 채택).
   - **남은 것**: Recording 로그에 `i=1: is_prime(1) → false → skip` 줄이 그대로 있어, "기록된 경로에서는 true였으므로"라는 새 설명과 여전히 어긋난다. 또한 같은 절 서두의 "**루프를 한 번 실행하며** 실제로 실행된 경로를 기록"이라는 서술과 i=1,2,3 **세 번**의 반복 로그가 맞지 않는 채로 남아 있다.
   - 해야 할 일: 로그에서 false였던 i=1 줄을 빼고 "true 경로가 잡힌 반복부터 기록된다"로 맞추거나, 서두의 "한 번 실행하며"를 "몇 회 실행하며"로 고쳐 로그 길이와 일치시킨다. (본문 재구성이 필요해 이번 배치에서 손대지 않았다.)

2. **`part07_reflection_metadata.md` — 크기 기반 타입 판별 (구 L210·L214, 현 동일 위치)**
   - 반영된 것: 소비 측이 `prop->size == sizeof(float/int)` → `prop->type == PropertyType::Float / Int`로 바뀌어 도달 불가 분기가 해소됨.
   - **남은 것**: 타입 태그를 **등록하는 쪽이 그대로다.** `REGISTER_PROPERTY` 매크로(L191~197)는 여전히 `#Member, offsetof(...), sizeof(Class::Member), Description`만 넘기고, `PropertyType` 열거형도 `PropertyInfo`의 `type` 필드도 문서 어디에도 정의가 없다(grep 확인). 즉 소비하는 필드를 아무도 채우지 않는다.
   - 해야 할 일: `enum class PropertyType { Float, Int, Bool, ... }` 정의를 `### 기본 구조` 블록(L120~148)에 추가하고, `REGISTER_PROPERTY`가 `sizeof` 대신(또는 그와 함께) 타입 태그를 등록하도록 매크로를 고친다.
   - 참고: 이 절의 `PropertyInfo`/`AddProperty`에 정의가 없다는 점은 **기각 항목 9번**에서 "스케치 수준이므로 오류가 아니다"로 이미 판정됐다. 위 작업은 그 판정을 뒤집자는 게 아니라, 새로 도입한 `type` 필드만이라도 등록 경로를 갖추자는 뜻이다.

### 보고서 범위를 넘어선 추가 변경 (1건)

- `part05_jit_compilation.md` 8절 TurboFan 예제(구 L1021·L1023)의 `jz deopt` 2줄도 `jnz deopt`로 함께 고쳐졌다. 보고서 **5번 항목**(L822·823·826·827의 가드 극성)이 지적한 것과 **동일한 결함**이지만 해당 행번호는 보고서에 열거돼 있지 않았다. 같은 결함의 일관 적용이므로 회귀가 아니다.

### 반영 방식이 정정안과 다른 항목 (1건 — 오류 아님)

- `part01` **1번 항목**(damage 시그니처 불일치)은 정정안의 **2안**이 채택됐다. 9절 typed IR을 6절에 맞춘 게 아니라, 6절 선언을 `damage: (EntityHandle, Float) -> DamageResult`로 바꿔 9절과 맞췄다. 현재 본문에서 두 곳이 일치하므로 내부모순은 해소됐다(1절 31행의 호출 표현식 서술과도 충돌 없음).

### 이 절을 읽는 사람에게

- **위 본문의 인용문과 행번호(L###)는 수정 전(2026-08) 기준이다.** 반영으로 행이 밀렸으므로 현재 파일에서 같은 행번호를 찾으면 다른 내용이 나온다. 원 지적의 근거를 보존하려고 일부러 그대로 두었다.
- **`## 검증에서 기각된 것` 9건은 오탐이다.** 본문이 그대로인 것이 정상이며, 다시 "미반영"으로 집계하지 마라.
- 확인하지 못한 것: 없음. 41건 전부 diff와 현재 본문 대조로 판정했다.
