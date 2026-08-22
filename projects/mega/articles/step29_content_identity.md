# 29. 콘텐츠에 이름이 없었다

## 들어가며

`balance.script` 를 고친 클라이언트가 서버에 붙는다. 시작 골드를 5000으로 적어 놓았다. 서버는 그것을 알아채지 못한다 — 권위 시뮬이 서버 쪽이라 골드 자체는 서버 값을 따르지만, 클라이언트가 그리는 화면·비용 표시·배치 프리뷰는 전부 자기 파일을 따른다. 두 사람이 다른 게임을 보고 있고, 아무 로그에도 그 사실이 남지 않는다.

원인은 검증이 없어서가 아니다. **물을 어휘가 없어서다.** 서버가 "네가 들고 있는 콘텐츠가 무엇인가"를 묻고 싶어도 콘텐츠에 이름이 없다. 파일 목록도, 버전도, 해시도 없다. 있는 것은 "실행 파일 옆의 `assets/` 디렉터리"라는 경로 하나뿐이다.

이 편은 그 이름을 만든다. 두 단계다.

**쿡.** 원본을 의존 관계와 함께 등록하고, 바뀐 것과 그 의존자만 다시 굽는다. 산출물 전체의 합성 해시가 나온다.
**발행.** 그 해시를 버전과 함께 매니페스트에 올린다. `(버전, 해시)` 쌍이 콘텐츠의 이름이고, 입장 승인이 그 이름을 대조한다.

| 헤더 | 하는 일 |
|---|---|
| `tools/asset_pipeline.hpp` | 유효 해시 · 증분 쿡 · 순환 거부 · 콘텐츠 버전 해시 |
| `live/patch.hpp` | 단조 버전 발행 · 롤백/롤포워드 · 입장 판정 세 상태 |

도달점의 수치.

- 10개 에셋 그래프에서 잎 하나(`skeleton.def`)를 고치면 **정확히 3개**가 다시 구워진다 — 자신, 그것을 참조하는 `boss.pose`, 그리고 배포 목록 `pack.index`.
- 아무것도 안 바꾸면 **0개**. 되돌리면 콘텐츠 버전 해시도 **정확히 되돌아온다**(경로 의존 없음).
- 같은 그래프를 **등록 순서를 뒤집어** 세워도 버전 해시가 같다: `0x266291cec4e68037`.
- 의존 결합을 뺀 변이에서 잎 변경이 **3개 → 1개**로 줄고, 빌드는 성공한다.
- 발행된 해시 `0x50e77ba5975445ee` 대 밸런스 한 줄 고친 클라이언트 `0x6d44d536d2b57bdf` → `mismatch`.

---

## 목차
1. [굽는 단계가 없던 이유와 생긴 이유](#1-굽는-단계가-없던-이유와-생긴-이유)
2. [유효 해시 — 의존이 위로 전파된다](#2-유효-해시--의존이-위로-전파된다)
3. [순환을 값으로 거부한다](#3-순환을-값으로-거부한다)
4. [의존은 집합이지 열이 아니다](#4-의존은-집합이지-열이-아니다)
5. [콘텐츠 버전 해시](#5-콘텐츠-버전-해시)
6. [세 상태 — 미발행 · 일치 · 불일치](#6-세-상태--미발행--일치--불일치)
7. [단조 버전이 롤백을 가능하게 한다](#7-단조-버전이-롤백을-가능하게-한다)
8. [롤백 중에 이미 붙어 있는 세션](#8-롤백-중에-이미-붙어-있는-세션)
9. [변이 하나](#9-변이-하나)
10. [실행과 관측](#10-실행과-관측)

---

## 1. 굽는 단계가 없던 이유와 생긴 이유

1편의 에셋은 PPM 열 장이었다. CMake 의 `POST_BUILD copy_directory` 가 실행 파일 옆으로 복사하면 끝이고, "무엇을 다시 만들어야 하는가"라는 질문 자체가 없었다. 등재부가 `tools/asset_pipeline.hpp` 를 두고 "현 에셋은 PPM 10장이라 굽는 단계 자체가 없다"고 적은 것이 정확한 진단이었다.

그 뒤로 종류가 늘었다.

| 원본 | 들어온 편 | 의존 |
|---|---|---|
| `level.txt` · `waves.txt` · `balance.script` | 3편 | — |
| PPM 10장 | 1편 | — |
| `boss.pose` | 18편 | 본 정의 |
| `elite.bt` | 22편 | 행동 이름 목록 |
| `events.txt` · `flags.txt` | 27편 | — |
| SPIR-V 셰이더 | 6편 | 공용 GLSL |

마지막 열이 이 편의 전부다. 포즈 파일은 본 정의를 참조하고, 행동 트리는 행동 이름 목록을 참조하고, 셰이더는 공용 인클루드를 참조한다. 참조가 생긴 순간 "이것만 다시 구우면 되는가"가 실재하는 질문이 된다.

## 2. 유효 해시 — 의존이 위로 전파된다

답은 하나다. **어떤 에셋의 유효 해시는 자기 콘텐츠 해시와 모든 의존 에셋의 유효 해시를 합친 값이다.** 마지막으로 구운 유효 해시와 다르면 다시 굽는다.

이 정의의 좋은 점은 전파가 공짜라는 것이다. `skeleton.def` 가 바뀌면 그 유효 해시가 바뀌고, 그것을 참조하는 `boss.pose` 의 유효 해시도 자동으로 바뀐다 — `boss.pose` 자체는 한 바이트도 안 바뀌었는데도.

<!-- snippet: include/tools/asset_pipeline.hpp@cook-incremental -->
```cpp
CookStatus Cook(int* rebuilt, std::vector<std::string>* names = nullptr,
                std::string* error = nullptr) {
    std::vector<uint64_t> effective;
    const CookStatus status = Evaluate(effective, error);
    if (status != CookStatus::Ok) return status;

    int count = 0;
    if (names) names->clear();
    for (size_t i = 0; i < assets_.size(); ++i) {
        Entry& entry = assets_[i];
        if (entry.cooked && entry.cookedHash == effective[i]) continue;
        entry.cookedHash = effective[i];
        entry.cooked = true;
        ++count;
        if (names) names->push_back(entry.name);
    }
    if (rebuilt) *rebuilt = count;
    if (error) error->clear();
    return CookStatus::Ok;
}
```

순회는 이름 오름차순이다. `assets_` 가 정렬된 벡터라 `names` 목록이 실행마다 같고, 그래서 "무엇이 다시 구워졌는가"를 로그로 비교할 수 있다. 해시 버킷 순서로 순회하면 그 목록이 매번 달라진다.

10개 그래프에서 실제로 재면 이렇다.

```
  skeleton.def 변경 → 재빌드 3개: boss.pose pack.index skeleton.def
```

## 3. 순환을 값으로 거부한다

유효 해시를 재귀로 계산하는 것이 자연스럽다. 이전 구현이 그랬다.

```cpp
uint64_t EffectiveHash(const std::string& name) const {
    // ...
    for (const std::string& d : it->second.deps) h = Combine(h, EffectiveHash(d));
    return h;
}
```

이 코드에는 순환 의존을 만나면 **스택이 터진다**는 성질이 있다. 그리고 에셋 그래프는 사람이 손으로 적는 데이터라 순환은 언제든 생긴다.

순환에서 스택 오버플로가 나면 두 가지가 나쁘다. 진단이 없고("어느 두 에셋이 서로를 참조하는가"를 말할 수 없다), 프로세스가 죽으므로 다른 에셋은 굽지도 못한다. 순환은 **값으로** 돌려줘야 한다.

<!-- snippet: include/tools/asset_pipeline.hpp@cook-evaluate -->
```cpp
CookStatus Evaluate(std::vector<uint64_t>& effective, std::string* error) const {
    const size_t total = assets_.size();
    effective.assign(total, 0);
    std::vector<uint8_t> color(total, 0);          // 0 흰, 1 회(방문 중), 2 검(완료)
    std::vector<std::pair<size_t, size_t>> stack;  // (에셋 인덱스, 다음 의존 커서)

    for (size_t root = 0; root < total; ++root) {
        if (color[root] != 0) continue;
        color[root] = 1;
        stack.push_back({root, 0});
        while (!stack.empty()) {
            const size_t index = stack.back().first;
            const Entry& entry = assets_[index];
            if (stack.back().second < entry.deps.size()) {
                const std::string& depName = entry.deps[stack.back().second];
                ++stack.back().second;
                const size_t dep = IndexOf(depName);
                if (dep == kNone) {
                    if (error) *error = entry.name + " → " + depName + ": 없는 의존";
                    return CookStatus::MissingDependency;
                }
                if (color[dep] == 1) {
                    if (error) *error = entry.name + " → " + depName + ": 순환 의존";
                    return CookStatus::Cycle;
                }
                if (color[dep] == 0) {
                    color[dep] = 1;
                    stack.push_back({dep, 0});
                }
                continue;
            }
            uint64_t hash = entry.contentHash;
            for (const std::string& dep : entry.deps)
                hash = Combine(hash, effective[IndexOf(dep)]);
            effective[index] = hash;
            color[index] = 2;
            stack.pop_back();
        }
    }
    return CookStatus::Ok;
}
```

명시적 스택으로 후위 순회하며 세 가지 색을 칠한다. **회색은 "지금 스택 위에 있다" = 조상**이라는 뜻이고, 회색 노드를 다시 만나는 것이 곧 순환이다. 검은색을 다시 만나는 것은 순환이 아니다 — 마름모(두 경로로 같은 잎에 도달)가 그 경우이고, 테스트가 그것을 별도로 확인한다.

순환이면 **아무것도 굽지 않는다.** `Cook` 이 `Evaluate` 의 실패를 그대로 돌려주고 한 에셋도 갱신하지 않는다. 절반만 구운 산출물 집합은 그 해시가 어느 원본에도 대응하지 않으므로 발행할 수 없다.

```
  순환 거부: b → a: 순환 의존
```

자기 참조는 등록 단계에서 막는다 — 가장 짧은 순환이고, 그것을 굳이 DFS 까지 보낼 이유가 없다.

## 4. 의존은 집합이지 열이 아니다

여기서 한 번 틀렸다.

`pack.index` 가 여섯 에셋에 의존한다. 그 여섯을 어떤 순서로 적든 의미는 같다 — 의존은 집합이다. 그런데 유효 해시는 `Combine` 으로 **접어서** 만드는 값이라 나열 순서에 좌우된다. 같은 그래프를 다른 순서로 적은 두 빌드가 다른 콘텐츠 버전을 내면, 그 해시는 정체성이 될 수 없다.

테스트가 그것을 먼저 잡았다. 같은 그래프를 등록 순서만 뒤집어 세운 두 파이프라인의 버전 해시가 달랐다.

고친 자리는 등록이다.

```cpp
// 의존은 **집합**이지 열이 아니다. 그런데 유효 해시는 접어 만드는 값이라
// 나열 순서에 좌우된다 — 같은 그래프를 다른 순서로 적은 두 빌드가 다른
// 콘텐츠 버전을 내면 그 해시는 정체성이 될 수 없다. 그래서 여기서 한 번
// 정렬하고, 중복은 실수로 보고 거부한다.
std::vector<std::string> sorted = deps;
std::sort(sorted.begin(), sorted.end());
for (size_t i = 1; i < sorted.size(); ++i)
    if (sorted[i] == sorted[i - 1]) return false;
```

정규화를 **읽는 쪽이 아니라 쓰는 쪽에** 두는 것이 요점이다. `Evaluate` 에서 매번 정렬하면 같은 일을 반복하고, 무엇보다 `Dependencies()` 로 조회한 목록과 해시가 본 목록이 달라진다.

## 5. 콘텐츠 버전 해시

산출물 전체를 하나의 수로 접는다.

<!-- snippet: include/tools/asset_pipeline.hpp@content-version -->
```cpp
uint64_t ContentVersionHash() const {
    uint64_t h = 0xcbf29ce484222325ull;
    for (const Entry& entry : assets_) {                 // 이름 오름차순
        if (!entry.cooked) continue;
        for (char c : entry.name) h = Combine(h, static_cast<uint64_t>(static_cast<uint8_t>(c)));
        h = Combine(h, entry.cookedHash);
    }
    return h;
}
```

**이름을 함께 섞는 이유.** 해시만 섞으면 두 에셋의 이름을 서로 바꿔도 같은 버전이 나온다. `boss.pose` 의 내용이 `elite.bt` 라는 이름으로 배포된 상태는 분명히 다른 콘텐츠인데, 해시만으로는 구분되지 않는다.

**`cooked` 가 아닌 것을 건너뛰는 이유.** 등록만 하고 굽지 않은 에셋은 배포되지 않는다. 버전 해시가 말하는 것은 "지금 배포되는 것이 무엇인가"이지 "등록된 것이 무엇인가"가 아니다.

경로 의존이 없다는 것도 검사한다. 밸런스를 고쳤다가 되돌리면 버전 해시가 **정확히 원래 값으로 돌아온다** — 굽는 순서나 횟수가 결과에 남지 않는다는 뜻이고, 그래야 "같은 원본 = 같은 버전"이 성립한다.

## 6. 세 상태 — 미발행 · 일치 · 불일치

입장 판정을 `bool` 로 줄이지 않는다.

<!-- snippet: include/live/patch.hpp@patch-verdict -->
```cpp
ContentVerdict Check(uint64_t clientHash) const {
    if (!Valid()) return ContentVerdict::Unpublished;   // 강제할 기준이 없다
    return clientHash == history_[currentIdx_].contentHash ? ContentVerdict::Accept
                                                           : ContentVerdict::Mismatch;
}
```

**미발행은 통과다.** 아직 아무것도 발행하지 않은 매니페스트가 모든 클라이언트를 거절하면, 이 기능을 켜는 그 순간 서비스가 죽는다. 강제할 기준이 없는데 거절부터 하는 것은 검사가 아니라 사고다.

**그런데 통과의 두 가지 이유를 구분해야 한다.** `Accept` 와 `Unpublished` 를 같은 `true` 로 뭉치면 로그에 "통과"만 남고, 매니페스트가 비어 있어서 통과한 것인지 해시가 맞아서 통과한 것인지 사후에 알 수 없다. 배포 사고의 절반은 "검사가 돌고 있는 줄 알았는데 꺼져 있었다"이고, 그 상태를 구분하는 것이 세 번째 열거 값의 존재 이유다.

`Accepts()` 는 편의 함수로 남긴다 — 판정이 필요한 곳은 세 상태를 보고, 통과 여부만 필요한 곳은 `bool` 을 본다.

## 7. 단조 버전이 롤백을 가능하게 한다

<!-- snippet: include/live/patch.hpp@patch-publish -->
```cpp
// 새 패치 배포. version 은 **발행 이력 전체에 대해** 단조 증가여야 한다 —
// 롤백 중이어도 기준은 마지막 발행분이지 현재 가리키는 것이 아니다.
bool Publish(uint32_t version, uint64_t contentHash) {
    if (version == 0) return false;                          // 0 은 "미발행"의 이름이다
    if (!history_.empty() && version <= history_.back().version) return false;
    const uint32_t previous = history_.empty() ? 0 : history_.back().version;
    history_.push_back({version, contentHash, previous});
    currentIdx_ = static_cast<int>(history_.size()) - 1;
    return true;
}
```

두 줄이 각각 하나씩 막는다.

**`version == 0` 거부.** 0 은 "미발행"의 이름이다. 0을 발행할 수 있으면 `Current() == 0` 이 두 가지 뜻을 갖게 되고, `Check` 의 세 상태 구분이 무너진다.

**`version <= history_.back().version` 거부.** 이것이 없으면 과거 버전을 다시 발행하는 것으로 현재가 뒤로 갈 수 있다. 그 순간 `history_` 는 더 이상 시간 순서가 아니고, `Rollback()` 이 가리키는 "직전"이 실제 직전이 아니게 된다.

비교 대상이 `Current()` 가 아니라 `history_.back()` 인 것도 의도다. 롤백 중에 핫픽스를 발행할 때 기준은 **마지막으로 발행한 것**이다. 테스트가 그 경우를 명시적으로 잡는다: 10과 20을 발행하고 10으로 롤백한 상태에서 15는 거부되고 21은 통과한다.

## 8. 롤백 중에 이미 붙어 있는 세션

잘못 배포된 밸런스를 되돌린다. `Rollback()` 이 현재를 102에서 101로 옮긴다. 그런데 지금 붙어 있는 클라이언트들은 102의 콘텐츠를 들고 있다.

규칙을 이렇게 정했다.

**진행 중인 매치는 건드리지 않는다.** 매치 중에 콘텐츠가 바뀌면 그 매치의 후반부가 전반부와 다른 규칙으로 돌고, 그 판의 기록은 어느 버전의 것도 아니게 된다.

**다음 입장부터 새 기준이 적용된다.** `Check` 는 언제나 `currentIdx_` 가 가리키는 해시를 본다. 롤백 직후 102 해시를 들고 재입장하는 클라이언트는 `mismatch` 로 거절되고, 리로드해서 101을 받아야 들어온다.

**발행 이력은 지우지 않는다.** `Latest()` 는 여전히 102를 가리키고 `RolledBack()` 이 참이다. 롤포워드가 가능한 것도, "지금 왜 101인가"를 로그로 답할 수 있는 것도 이력이 남아 있기 때문이다.

**배선한 것과 배선하지 않은 것을 여기서 갈라 적는다.**

배선한 것: 발행과 검사다. 제품은 시작할 때 원본을 굽고 그 해시로 **버전 1을 발행**하며, 입장 인사에 실려 온 클라이언트 해시를 그것과 대조한다.

```cpp
        if (patch_.Published() && !patch_.Accepts(detail.contentHash)) {
            ++rejectedPackets_;
            ++contentRejects_;
            std::printf("[admission] reject reason=content_mismatch player=%s "
                        "client=0x%016llX server=0x%016llX\n", ...);
            return;
        }
```

실측은 이렇다.

```
[admission] reject reason=content_mismatch player=alice client=0x51570A985EA2FBD6 server=0x51570A985EA2FBD7
[admission] player=alice endpoint=127.0.0.1:50460 players=2/2 content=accept grant=161B ...
```

같은 실행에서 한 비트 다른 해시는 거절되고, 같은 해시는 승인된다. 클라이언트 쪽은 승인 패킷을 받아 자기 것과 맞춰 본다 — `[admission] grant version=1 hash=0x… content=accept`.

배선하지 **않은** 것: 롤백이다. 지금 제품은 프로세스가 뜰 때 원본을 굽고 버전 1을 한 번 발행한다. 발행이 하나뿐인 매니페스트에는 되돌아갈 직전 버전이 없으므로 `Rollback()` 은 언제나 거짓을 돌려준다.

그래서 `[patch] rollback 102 -> 101` 같은 줄은 이 저장소의 제품 로그에 **아직 없다.** 그것이 나오려면 실행 중에 새 버전을 발행하는 경로 — 운영자가 콘텐츠를 밀어 넣는 채널 — 가 있어야 하고, 이 제품에는 그 채널이 없다. 위 §8 의 세 규칙은 `PatchManifest` 안에서 성립하고 `test_content_version` 이 그것을 검사하지만, **제품 안에서 관측되는 것은 발행과 대조까지다.**

없는 것을 있다고 적지 않는 편이 낫다. 이 줄이 채워지는 시점은 라이브 패치 피드가 생기는 편이다.

시작 로그에 콘텐츠 정체성 한 줄이 생겼다.

```
> mega_defense --selftest
[platform] Windows/x86-64/LE profile=PC workers=16 arena=1024KB voices=32 lod=0
[content] version=1 hash=0x51570A985EA2FBD7 cooked=17/18
```

`cooked=17/18` 의 두 수가 다르다. 등록된 것은 배포된 원본 17개 + 배포 목록(`pack.index`) 하나로 18이고, 이번 실행에서 실제로 구워진 것은 17이다. 배포 목록은 자기 자신을 포함하지 않으므로 그 차이가 언제나 1이다.

첫 실행에서 17이 전부 구워지는 것은 캐시가 비어 있기 때문이다. 파이프라인이 프로세스마다 새로 만들어지므로 이 수는 매번 같다 — 증분 쿡의 이득은 여기서 관측되지 않고, 그것을 재는 것은 `test_asset_cook` 이다.

```
> test_asset_cook.exe
  skeleton.def 변경 → 재빌드 3개: boss.pose pack.index skeleton.def
  content version hash = 0x266291cec4e68037 (등록 순서 무관)
  순환 거부: b → a: 순환 의존
```

원본 하나를 고치면 그 자산과 **의존자만** 다시 구워진다. 셋이라는 수가 그 사실이다.

## 9. 변이 하나

**유효 해시에서 의존 결합을 뺀다.**

```cpp
uint64_t hash = entry.contentHash;   // ← deps 결합을 뺀 변이
effective[index] = hash;
```

```
[test] asset_cook
  skeleton.def 변경 → 재빌드 1개: skeleton.def
  FAIL tests\test_asset_cook.cpp:76  CHECK_EQ(rebuilt, 3)
  FAIL tests\test_asset_cook.cpp:78  CHECK(Contains(names, "boss.pose"))
  FAIL tests\test_asset_cook.cpp:79  CHECK(Contains(names, "pack.index"))
  FAIL tests\test_asset_cook.cpp:86  CHECK_EQ(rebuilt, 2)
  FAIL tests\test_asset_cook.cpp:88  CHECK(Contains(names, "pack.index"))
  content version hash = 0x7608dac367611f54 (등록 순서 무관)
  FAIL tests\test_asset_cook.cpp:181  CHECK_EQ(rebuilt, 4)
[test] asset_cook: 117 checks, 6 failures
```

첫 줄이 이 변이의 증상 전부다. `3개` 가 `1개` 가 됐다. 그리고 **빌드는 성공한다.** 순환도 없고 크래시도 없고 에러 메시지도 없다. 본 정의를 고쳤는데 포즈가 옛 산출물을 그대로 들고 있고, 보스가 옛 본 길이로 그려진다.

증분 빌드의 결함이 언제나 이렇게 조용하다는 것이 이 테스트가 개수를 세는 이유다. "다시 구웠다"가 아니라 "정확히 몇 개를 다시 구웠는가"를 물어야 한다.

복원 뒤 117 검사에 실패 0.

## 10. 실행과 관측

**쿡.**

```
> build\bin\Release\test_asset_cook.exe
[test] asset_cook
  skeleton.def 변경 → 재빌드 3개: boss.pose pack.index skeleton.def
  content version hash = 0x266291cec4e68037 (등록 순서 무관)
  순환 거부: b → a: 순환 의존
[test] asset_cook: 117 checks, 0 failures
```

읽을 것 셋.

**목록이 이름 오름차순이다.** `boss.pose pack.index skeleton.def` — 의존 순서도 등록 순서도 아니다. 테스트가 `names[i-1] < names[i]` 로 그것을 확인한다.

**`pack.index` 가 언제나 포함된다.** 배포 목록이 모든 산출물에 의존하므로, 어느 하나가 바뀌어도 전체 목록이 다시 만들어진다. 이것이 콘텐츠 버전이 바뀌는 경로다.

**등록 순서를 뒤집어도 같은 해시.** `0x266291cec4e68037` 을 두 파이프라인이 독립적으로 낸다.

**발행과 입장 검사.**

```
> build\bin\Release\test_content_version.exe
[test] content_version
  shipped=0x50e77ba5975445ee  tampered=0x6d44d536d2b57bdf  verdict=mismatch
[test] content_version: 132 checks, 0 failures
```

`tampered` 는 `balance.script` 의 콘텐츠 해시 하나를 `^ 1` 한 것 — 한 비트 차이다. 그 한 비트가 `pack.index` 를 거쳐 버전 해시 전체를 바꾸고, 판정이 `mismatch` 가 된다.

그리고 본 정의만 고친 클라이언트도 같은 판정을 받는다. `skeleton.def` 는 배포되는 파일이지만 클라이언트가 직접 보는 것은 `boss.pose` 다 — 그 사이의 참조 관계 때문에 어느 쪽을 고쳐도 걸린다. 정체성이 의존 그래프 전체를 덮는다는 것의 실물이다.

**발행되는 값이 실제로 쿡 결과인가.**

```cpp
for (int change = 0; change < 4; ++change) {
    CHECK(pipeline.Register("waves.txt", 0xA002 + change));
    const uint64_t hash = CookAndHash(pipeline);
    CHECK(hash != previous);
    CHECK(manifest.Publish(version, hash));
    CHECK_EQ(manifest.CurrentHash(), hash);
    CHECK(manifest.Check(hash) == ContentVerdict::Accept);
    if (previous != 0) CHECK(manifest.Check(previous) == ContentVerdict::Mismatch);
    // ...
}
```

네 번 고치고 네 번 발행하면서, 매번 **직전 버전의 해시가 거절되는지**까지 확인한다. 이것이 없으면 `Check` 가 늘 `Accept` 를 내는 구현도 통과한다.

## 11. 제품 실측

**(1) 서버가 발행한다.**

```
> mega_defense_server --port 27155 --server-id probe-server --run-seconds 6
[platform] Windows/x86-64/LE profile=PC
[patch] version=1 hash=0x51570A985EA2FBD7 assets=18 cooked=17
[flags] source=file variants=5 toggles=3
```

같은 해시가 클라이언트 쪽 `[content] version=1 hash=0x51570A985EA2FBD7` 과 일치한다. 두 프로세스가 같은 바이트에서 같은 방식으로 계산했기 때문이고, 그 재현성이 이 편의 전제다 — 서버 로그를 베껴 쓰는 것으로는 검사할 수 없는 성질이라 `test_dedicated_process` 는 해시를 **다시 계산해서** 보낸다.

```cpp
    const std::filesystem::path assetRoot =
        std::filesystem::path(argv[0]).parent_path() / "assets";
    tools::AssetPipeline pipeline;
    const uint64_t contentHash = defense::CookAssets(assetRoot.string(), pipeline);
    CHECK(contentHash != 0);
```

**(2) 같은 해시는 들어온다.**

```
[admission] player=bob endpoint=127.0.0.1:50461 players=1/2 content=accept grant=161B ...
```

**(3) 한 비트 다른 해시는 거절되고 `rejected` 가 는다.**

```
[admission] reject reason=content_mismatch player=alice client=0x51570A985EA2FBD6 server=0x51570A985EA2FBD7
[server] stop tick=317 players=2 commands=0 rejected=1 ...
```

`rejected=1` 이 그 한 번이다. 프로세스 검사가 자동으로 밟는 것도 같은 경로다 — `test_dedicated_process` 는 올바른 `MatchId` 를 손에 쥔 채 해시만 한 비트 뒤집어 보내고, 승인 패킷도 스냅샷도 오지 않는 것을 400ms 동안 확인한다. 배선 전에는 그 클라이언트가 **들어왔다.**

```
> test_dedicated_process.exe
[test] dedicated_process: queue -> match id=m-0001-bd20bd80
[test] dedicated_process: 19 checks, 0 failures
```

CTest 는 111개 대상을 전수 통과한다. 이 편이 들여온 검사는 `test_asset_cook`(증분 쿡·순환 거부·등록 순서 무관)과 `test_content_version`(단조 발행·롤백/롤포워드·해시 대조)이다.

---

## 결론

"클라이언트가 조용히 접속할 수 있었다"는 상태는 보안 결함이라기보다 **어휘의 부재**였다. 검사를 안 한 것이 아니라 검사할 대상에 이름이 없었다.

이름을 만드는 데 필요한 것이 해시 하나가 아니라 둘이라는 것이 이 편의 구조다. 먼저 **유효 해시**가 의존 그래프를 접어 각 산출물의 정체성을 만들고, 그다음 **버전 해시**가 산출물 전체를 접어 배포의 정체성을 만든다. 중간 단계를 건너뛰고 파일 바이트를 전부 이어 해시하면 증분 쿡이 불가능해지고, 증분이 없으면 원본 한 줄 수정에 전체를 다시 굽게 된다.

측정에서 나온 것 셋.

**증분 빌드의 결함은 언제나 조용하다.** 의존 결합을 뺀 변이는 에러도 크래시도 없이 "덜 구웠다". 개수를 세지 않는 테스트는 이것을 못 잡는다.

**접어 만드는 해시는 입력 순서에 좌우된다.** 의존이 집합이라면 정규화를 쓰는 쪽에 둬야 하고, 그러지 않으면 같은 그래프가 두 개의 버전을 갖는다.

**"통과"에는 두 가지 이유가 있다.** 기준이 있어서 통과한 것과 기준이 없어서 통과한 것을 로그에서 구분할 수 없으면, 꺼진 검사가 켜진 검사처럼 보인다.

현재 상태의 한계 넷.

**콘텐츠 해시가 파일 내용에서 오지 않는다.** 지금 `Register` 는 해시를 인자로 받는다. 그것을 실제 파일 바이트에서 계산하는 것은 파일을 읽는 계층의 일이고, 이 헤더는 그래프 계산만 소유한다. 그 계산이 어디서 도는지는 배선이 정한다.

**롤백이 클라이언트에게 콘텐츠를 내려보내지 않는다.** 매니페스트는 "네 것이 다르다"까지만 말한다. 다른 것을 어디서 받아 오는지는 배포 채널의 문제이고 이 저장소에 그것이 없다.

**버전 번호를 누가 매기는지가 정해져 있지 않다.** `Publish(version, hash)` 의 `version` 은 호출자가 준다. 빌드 번호일 수도 날짜일 수도 있고, 그 규칙이 어디에도 적혀 있지 않다.

**`Overflow` 와 `BadName` 상태에 도달 경로가 없다.** `CookStatus` 열거에 자리는 있지만 `Register` 가 이미 그 둘을 `false` 로 막고 있어 `Cook` 이 그 값을 내지 않는다.
