# 33. 알고리즘이 게임 안에서 무엇을 바꿨는가

## 들어가며

이 저장소에는 알고리즘을 설명하는 이론 문서가 여러 편 있다. 유도가 있고 불변식이 있고 반례가 있다. 없는 것은 하나뿐이었다 — **그 알고리즘이 실제로 돌아가는 자리.**

이 편이 여섯 개를 게임 안으로 들여온다. 조건은 하나다: **게임에 진짜 이유가 있어야 한다.** 이유를 못 만드는 알고리즘은 넣지 않았다. 그래서 이 여섯은 "알고리즘을 써 봤다"가 아니라, 여섯 개의 임시 구현·놓친 정책·없던 기능을 각각 대체한다.

| 알고리즘 | 대체하거나 새로 만든 것 |
|---|---|
| canonical Huffman | 리플레이 파일의 무압축 고정폭 프레이밍 |
| 서로소 집합 + Kruskal | 절차 맵 골격 생성기의 두 번째 팔(형질이 다른 미로) |
| low-link DFS | 배치 판정을 칸 질의에서 **격자 전수 판정**으로 |
| Kahn 위상 정렬 + SCC | 해금 선행 DAG — 상점 순서와 지갑 폐포 검사 |
| Aho-Corasick | 입장 토큰의 여섯 번 훑기, 그리고 **중복 필드의 조용한 채택** |
| quickselect | 스냅샷 64자리를 저장 행 순서가 아니라 relevancy 로 |

도달점의 수치.

- 리플레이 파일이 **44,088바이트에서 35,394바이트로** 줄었다. 그런데 체인 값은 `0xecfa0daff65fe392` 그대로다 — 압축은 파일 계층이고 결정성은 그 위를 지나가지 않는다.
- 그 80.3%라는 수에는 **천장이 있다.** 본문의 정확히 50%가 해시 출력이라 구성상 압축되지 않는다. 나머지 절반은 54.6%로 접힌다.
- 배치 판정 두 팔이 **44,376칸 전부에서 일치**하고, 두 팔로 400틱을 돌린 세계의 상태 해시가 `0x98978851724926e0` 로 비트 동일하다. 무작위 위상 격자 1,643개의 **57,916칸**을 칸마다 실제로 지워 BFS 를 돌린 결과와도 전부 일치한다.
- 격자 전체를 한 번 묻는 비용이 **19.26ms 에서 0.010ms 로**(1,963배). 그 질의가 싸졌기 때문에 화면이 격자 전체를 칠할 수 있다.
- 미로 두 팔의 형질이 갈린다. 8시드 합계로 분기 셀 **325 대 850**, 막다른 길 **344 대 986**.
- 입장 토큰의 필드 중복이 **거부**로 바뀌었다. 서명이 깨진 입력에서 페이로드 훑기는 **0회**, 정상 토큰은 **1회**(예전에는 6회).
- 스냅샷이 코어에 가장 가까운 적을 놓치지 않는다. 900마리 중 64를 고를 때 저장 행 순서 팔은 최근접 적을 **빠뜨렸고**, 실린 적의 평균 코어 거리가 **14.0칸 대 11.0칸**이다.
- 무력화 변이 20건 중 18건이 죽었다. 처음 심었을 때는 넷이 살아남았고, 그중 **둘은 검사의 구멍**이었다 — 그 둘을 메우는 것이 이 편에서 가장 값이 나간 30분이다.
- CTest 121 → 127. `include/` 헤더 121개 전부가 제품에서 도달한다(랩 전용 등재 0행 유지).

---

## 목차
1. [흡수의 조건 — 이유를 못 만들면 넣지 않는다](#1-흡수의-조건--이유를-못-만들면-넣지-않는다)
2. [코드 길이표 하나가 복호기 전체를 정한다](#2-코드-길이표-하나가-복호기-전체를-정한다)
3. [압축을 파일 계층에 두면 결정성이 지나가지 않는다](#3-압축을-파일-계층에-두면-결정성이-지나가지-않는다)
4. [천장을 아는 것과 모르는 것](#4-천장을-아는-것과-모르는-것)
5. [신장 트리 둘, 형질 둘](#5-신장-트리-둘-형질-둘)
6. [칸 하나의 답과 격자 전체의 답](#6-칸-하나의-답과-격자-전체의-답)
7. [두 구현이 같은 버그를 공유하지 않는다](#7-두-구현이-같은-버그를-공유하지-않는다)
8. [상점에 순서가 생겼다](#8-상점에-순서가-생겼다)
9. [첫 일치가 아니라 모든 일치](#9-첫-일치가-아니라-모든-일치)
10. [어느 64를 싣는가](#10-어느-64를-싣는가)
11. [변이 — 스무 개를 심고 무엇이 죽었는가](#11-변이--스무-개를-심고-무엇이-죽었는가)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 흡수의 조건 — 이유를 못 만들면 넣지 않는다

알고리즘을 제품에 넣는 가장 쉬운 방법은 쓸 자리를 지어내는 것이다. 그렇게 들어간 코드는 컴파일되고 테스트도 통과하지만 아무것도 지키지 않는다. 이 저장소는 그 상태를 이미 한 번 겪었다 — 89개 헤더 중 64개가 제품에서 도달조차 하지 않는데 테스트가 전부 초록이던 때다. 그때 배운 것이 도달성 게이트였다.

이번에는 게이트가 하나 더 필요했다. 도달 가능한 코드가 실제로 **무언가를 바꾸는가**.

그래서 흡수마다 세 줄을 요구했다.

**(a) 임시 구현을 대체하거나 실제 기능을 추가한다.** "여기에도 쓸 수 있다"는 이유가 아니다. 지금 있는 코드가 무엇을 못 하고 있는지를 먼저 적어야 한다.

**(b) 실행하는 테스트가 있다.** 라이브러리 경계 안에서 함수를 부르는 것으로는 부족하다. 게임이 그것을 부르는 자리까지 가야 한다.

**(c) 무력화 변이가 그 테스트를 죽인다.** 한 줄을 지웠는데 아무것도 실패하지 않으면, 그 줄은 검사되지 않는 것이고 따라서 없는 것이다.

여섯 중 몇은 (a)에서 걸러졌다. 원안에 있던 "서버 이름 필터"는 이 게임에 채팅도 닉네임도 없어 성립하지 않았고, 대신 입장 토큰의 페이로드가 그 자리를 대신했다 — 실제로 존재하는, 실제로 여섯 번 훑고 있던 다중 패턴 스캔이다.

새 헤더는 `include/algo/` 아래에 산다. 여섯 개 전부가 제품 실행 파일에서 전이적으로 도달하고, 그 사실을 도달성 게이트가 빌드마다 확인한다.

## 2. 코드 길이표 하나가 복호기 전체를 정한다

리플레이 파일은 한 틱에 32바이트를 쓴다. 프레임 조작 8, 아바타 자취 12, 골든 마크 12. 헤드리스 한 판이 수십 KB고, 창 실행을 기록하면 훨씬 커진다.

그런데 그 32바이트는 성질이 정반대인 두 덩어리다. 조작 쪽은 극단적으로 치우쳐 있다 — 17개 액션 키 비트는 대부분 0이고, 마우스 좌표는 프레임 사이 근접 반복이며, 전진 플래그는 사실상 상수다. 해시 쪽은 반대다. 아바타 체크섬과 체인 값은 해시 출력이라 **구성상 압축되지 않는다**(압축된다면 그것은 나쁜 해시다).

Huffman 을 고른 이유는 그 첫 덩어리다. 그리고 canonical Huffman 을 고른 이유는 **파일에 트리를 적지 않아도 되기 때문**이다.

일반 Huffman 코드를 복원하려면 트리 구조를 적어야 한다. canonical 코드는 심볼별 **코드 길이**만 적으면 코드가 유일하게 복원된다. 규칙은 두 줄이다: 길이가 짧은 것이 먼저, 같은 길이는 심볼 번호 순.

<!-- snippet: include/algo/huffman.hpp@huffman-canonical -->
```cpp
inline bool HuffmanAssignCanonical(const uint8_t* lengths, HuffmanCodes& out) {
    if (!HuffmanKraftComplete(lengths)) return false;
    uint32_t count[kHuffmanMaxLength + 1]{};
    for (size_t i = 0; i < kHuffmanSymbols; ++i) ++count[lengths[i]];
    count[0] = 0;

    uint32_t next[kHuffmanMaxLength + 2]{};
    uint32_t code = 0;
    for (uint8_t bits = 1; bits <= kHuffmanMaxLength; ++bits) {
        code = (code + count[bits - 1]) << 1;
        next[bits] = code;
    }
```

`<< 1` 이 canonical 의 전부다. 길이가 하나 오르면 자리도 하나 는다는 것 — 그 한 줄이 접두 자유성을 만든다. 지우면 길이가 다른 코드들이 서로의 접두가 되고, 복호가 조용히 다른 심볼을 낸다. 그래서 256바이트의 길이표가 트리 전체를 대신한다.

길이 상한은 15로 묶었다. 상한이 없으면 병적인 분포에서 깊이가 심볼 수까지 자라고, 그러면 길이표에 8비트로 적을 수 없다. 넘으면 빈도를 절반으로 접어 다시 짓는다 — 전부 1이 되면 깊이는 8 이하이므로 그 되풀이는 반드시 끝난다.

복호는 9비트 lookup 표와 그 표에 들어가지 못하는 긴 코드용 fallback 둘로 나뉜다.

<!-- snippet: include/algo/huffman.hpp@huffman-decoder -->
```cpp
class HuffmanDecoder {
public:
    bool Build(const uint8_t* lengths) {
        if (!HuffmanKraftComplete(lengths)) return false;
        HuffmanCodes codes;
        if (!HuffmanAssignCanonical(lengths, codes)) return false;
```

fallback 이 실제로 도는지를 우연에 맡기지 않았다. 검사 하나가 기하 분포의 파일을 일부러 만든다 — `(i+1)` 의 후행 0 개수를 심볼로 쓰면 0이 절반, 1이 4분의 1, 2가 8분의 1인 분포가 되고, 기하 분포의 Huffman 깊이는 심볼 번호를 따라가므로 9비트를 반드시 넘는다. 실측으로 **최장 15비트**, 그 파일은 800,080바이트에서 112,862바이트(14.1%)로 접힌다.

## 3. 압축을 파일 계층에 두면 결정성이 지나가지 않는다

이 저장소에서 리플레이는 압축을 위해 있는 것이 아니다. 프로세스 경계를 넘는 결정성 게이트를 위해 있다. 한 실행이 조작과 체인을 파일로 적고, **다른 프로세스**가 그 조작만으로 같은 체인을 다시 만든다.

그래서 압축을 들일 때 지켜야 할 것이 하나 있었다. **시뮬이 압축을 몰라야 한다.**

방법은 본문을 통째로 한 덩어리로 만들어 그 덩어리만 접는 것이다. 프레임·아바타 자취·마크·체인 꼬리를 예전과 **한 바이트도 다르지 않게** 이어 붙이고, 그 결과를 압축한다.

<!-- snippet: include/game/replay.hpp@replay-body -->
```cpp
inline std::vector<uint8_t> EncodeReplayBody(const Replay& replay) {
    std::vector<uint8_t> out;
    platform::AppendLE32(out, static_cast<uint32_t>(replay.frames.size()));
    for (const ReplayFrame& frame : replay.frames) {
        platform::AppendLE8(out, static_cast<uint8_t>(frame.keys));
        platform::AppendLE8(out, static_cast<uint8_t>(frame.keys >> 8));
        platform::AppendLE8(out, static_cast<uint8_t>(frame.keys >> 16));
        platform::AppendLE16(out, static_cast<uint16_t>(frame.mouseX));
        platform::AppendLE16(out, static_cast<uint16_t>(frame.mouseY));
        platform::AppendLE8(out, frame.flags);
    }
```

디코더 쪽에서 이 결정이 눈에 보인다. 커서가 도는 대상이 둘이 되는데 — 먼저 파일, 압축을 푼 뒤에는 본문 — 읽기 헬퍼는 한 벌만 둔다. 가리키는 곳만 바꾼다. 두 벌이 되면 한쪽에만 경계 검사를 고치는 날이 온다.

압축 블록의 신뢰 경계 검사는 셋이다. 코드 길이표가 Kraft 등식을 만족하는가, 선언된 본문 크기가 예산 안인가, 복호 결과가 그 크기와 정확히 같은가.

<!-- snippet: include/algo/huffman.hpp@huffman-kraft -->
```cpp
inline bool HuffmanKraftComplete(const uint8_t* lengths) {
    uint32_t total = 0;
    const uint32_t full = 1u << kHuffmanMaxLength;
    bool any = false;
    for (size_t i = 0; i < kHuffmanSymbols; ++i) {
        if (lengths[i] == 0) continue;
        if (lengths[i] > kHuffmanMaxLength) return false;
        any = true;
        total += full >> lengths[i];
        if (total > full) return false;
    }
    return any && total == full;
}
```

**정확히 1** 만 받는다. 합이 1보다 크면 코드가 겹치고, 1보다 작으면 어떤 비트열에서도 심볼을 못 찾는 구멍이 생긴다. 부동소수로 재지 않는 것도 이유가 있다 — `2^15` 로 스케일해 정수로 세면 같은 표가 컴파일러에 따라 통과하기도 실패하기도 하는 일이 없다.

이 검사가 막는 상태는 크래시가 아니다. **조용히 다른 바이트를 내는 디코더**다. 그것이 신뢰 경계에 있으면, 재생기는 손상된 입력으로 시뮬을 돌려 "결정성이 깨졌다"고 보고한다. 거짓 실패를 내는 게이트는 며칠 안에 꺼진다.

같은 이유로 원본 본문의 해시 하나를 파일에 함께 적는다. 페이로드의 비트 하나가 뒤집히면 복호 결과가 통째로 달라지는데, 그 달라진 바이트가 **우연히 문법에 맞는 본문**일 수 있기 때문이다.

파일 전 범위에서 표본 200곳을 잘라 봤고 200곳 전부 거절됐다. 비트 하나를 뒤집은 표본 200곳도 전부 거절됐다.

## 4. 천장을 아는 것과 모르는 것

실측이다. 제품의 `--record` 가 낸 한 판.

```text
[record] wave=6 towers=10 bytes=35394 raw_body=44088 ratio=80.3%
```

80.3%. 압축치고는 초라한 수다. 그 수를 보고 "압축이 고장났다"고 판단하지 않으려면 **천장을 알아야 한다.**

본문의 정확히 절반이 해시 출력이다. 틱마다 아바타 체크섬 8바이트와 체인 값 8바이트, 합쳐 16바이트가 나머지 16바이트와 나란히 들어간다. 그 절반은 어떤 무손실 코더로도 줄지 않는다.

검사가 그 천장을 함께 적는다.

```text
  리플레이 압축 1920085 -> 1483897 바이트 (77.3%, 프레임 60000)
  그중 압축 불가(해시) 960000바이트 = 본문의 50.0% — 이론 하한 50.0%
```

압축 가능한 절반만 보면 960,085바이트가 523,897바이트, **54.6%** 다. 그리고 검사에는 `bytes.size() > hashBytes` 라는 단언이 있다 — 해시 아래로 내려간 파일은 무손실이 아니라는 뜻이므로, 그 단언이 죽으면 압축이 아니라 손실이 일어난 것이다.

천장을 적어 두지 않았다면 이 자리에 남는 것은 "80.3%" 하나뿐이었고, 그 수는 다음 사람에게 "왜 이것밖에 안 줄었지"라는 잘못된 질문을 시킨다.

## 5. 신장 트리 둘, 형질 둘

절차 맵의 골격은 재귀 백트래커 하나가 팠다. 시드를 바꾸면 다른 미로가 나오지만, **형질은 언제나 같았다.** DFS 미로는 구조적으로 긴 단일 회랑이다 — 한 방향으로 끝까지 파고 들어갔다 되돌아오므로 분기점이 적고 막다른 길이 길다.

메이즈 디펜스에서 "어디를 막을까"의 선택지 수는 곧 골격의 분기 수다. 시드 스위치가 있어도 그 손잡이는 없었다.

Kruskal 이 두 번째 팔이다. 셀 사이 간선에 시드 기반 가중치를 주고 오름차순으로 훑으며, 서로소 집합으로 사이클을 막는다.

<!-- snippet: include/tools/procgen.hpp@maze-kruskal -->
```cpp
    static Level GenerateKruskal(int cw, int ch, uint32_t seed) {
        if (cw < 1) cw = 1; if (ch < 1) ch = 1;
        int W = 2 * cw + 1, H = 2 * ch + 1;
        Level lvl;
        lvl.Init(W, H, '#');
        for (int cy = 0; cy < ch; ++cy)
            for (int cx = 0; cx < cw; ++cx)
                lvl.Set(2 * cx + 1, 2 * cy + 1, '.');   // 셀 타일은 전부 열려 있다
```

정확성을 지키는 것은 `dsu.Union(a, b)` 가 돌려주는 불리언 **하나**다. 그것을 무시하고 무조건 벽을 뚫으면 채택 간선이 셀 수 − 1을 넘고 사이클이 생긴다 — 그 순간 이것은 미로가 아니라 그냥 뚫린 방이다.

그리고 여기에 이 자료구조의 재미있는 사실이 있다. **경로 압축과 크기 합치기는 정확성에 기여하지 않는다.**

<!-- snippet: include/algo/dsu.hpp@dsu-core -->
```cpp
class DisjointSet {
public:
    DisjointSet() = default;
    explicit DisjointSet(size_t count) { Reset(count); }
```

둘 중 어느 것을 지워도 답은 같고 속도만 변한다. 그래서 변이 실험에서 이 둘은 **어떤 검사도 죽이지 못한다** — 그 사실을 숨기지 않고 그대로 문서에 적는다(11절).

두 팔이 지켜야 하는 것은 같다. 채택 간선 수가 셀 수 − 1이고, 서로소 집합으로 센 개방 칸의 연결 성분이 1개이며, `tools::Level` 의 flood fill 도 같은 답을 낸다. 8시드 × 2팔 × 400셀에서 전부 성립한다.

다른 것은 형질이다. 8시드 합계로:

| 팔 | 분기 셀(개방 이웃 3 이상) | 막다른 길 |
|---|---|---|
| dfs | 325 | 344 |
| kruskal | 850 | 986 |

Kruskal 쪽이 분기가 2.6배, 막다른 길이 2.9배다. 막다른 길이 **많다**는 것은 각각이 **짧다**는 뜻이다 — 총 셀 수가 같으므로 긴 회랑 하나가 짧은 가지 여럿으로 흩어진 것이다. 그것이 이 스위치가 밸런스 손잡이인 이유다.

## 6. 칸 하나의 답과 격자 전체의 답

배치 규칙은 오랫동안 칸 하나짜리 질문이었다. "여기에 타워를 세워도 스폰에서 코어까지 갈 수 있는가."

그 질문에 답하는 가장 짧은 코드는 후보 격자를 만들어 flood fill 을 한 번 도는 것이고, 실제로 그렇게 해 왔다. 41×41 char 레벨을 통째로 새로 짓고(1,681칸 할당 + 복사) 4방향 flood fill 을 한 번.

답은 옳다. 문제는 **한 칸에 대해서만** 옳다는 것이다.

그래서 화면이 말할 수 있는 것도 한 칸뿐이었다 — 커서 아래가 되는지 안 되는지. 격자 전체에서 막으면 안 되는 칸이 어디인지 물으려면 1,681번 돌아야 한다.

low-link DFS 는 한 번 돌아 전부 표시한다. 스폰을 뿌리로 DFS 를 돌면 각 정점에 발견 시각 `tin` 과 자기 부분트리에서 역방향 간선으로 닿을 수 있는 가장 이른 시각 `low` 가 붙고, 정점 v 가 스폰과 코어를 가르는 조건은 정확히 둘이다 — v 가 트리에서 스폰→코어 경로 위에 있고, 그 경로의 자식 c 에 대해 `low[c] >= tin[v]` 다.

<!-- snippet: include/algo/articulation.hpp@artic-build -->
```cpp
    bool Build(const uint8_t* passable, int width, int height,
               int spawnX, int spawnY, int coreX, int coreY) {
        width_ = width;
        height_ = height;
```

트리 경로 위만 보면 되는 이유는 간단하다. 가르는 정점은 **모든** 스폰-코어 경로 위에 있어야 하고, DFS 트리 경로도 그중 하나다.

역방향 간선을 만났을 때 `low_[next]` 가 아니라 `tin_[next]` 를 쓰는 것이 이 코드에서 가장 조용한 함정이다. 무방향 DFS 에서 이미 방문된 이웃의 `low` 는 자기 부분트리 바깥의 정보를 담고 있어, 그것을 끌어오면 실제로는 가르는 정점이 안 가르는 것으로 보고된다. 역간선이 주는 정보는 그 정점의 **발견 시각**뿐이다.

그리고 표가 생기는 순간 새 기능이 생긴다.

<!-- snippet: include/game/defense_map.hpp@choke-mask -->
```cpp
    const std::vector<uint8_t>& ChokeMask() const {
        EnsureChoke();
        return chokeMask_;
    }
```

화면이 커서 아래 한 칸의 가부 대신 **격자 전체를 칠한다.** 타일 색 계산에 한 줄이 붙었고, 그 한 줄은 전수 판정을 가진 팔에서만 무언가를 칠한다 — 칸마다 flood fill 을 도는 팔에서 이 색을 채우려면 한 프레임에 격자 수만큼 검증을 돌아야 하기 때문이다.

표의 무효화 기준은 이미 있었다. 통행 격자가 바뀔 때마다 오르는 버전 하나 — 항법 격자가 쓰던 것과 같은 기준이다.

## 7. 두 구현이 같은 버그를 공유하지 않는다

판정이 둘이 되면 즉시 질문이 하나 생긴다. **두 팔이 같은 답을 내는가.** 다르면 그것은 최적화가 아니라 규칙 변경이다.

이 대조가 진짜 오라클인 이유는 두 구현의 **구조가 다르기 때문**이다. 한쪽은 도달성을 직접 시뮬레이션하고, 다른 쪽은 DFS 트리의 low-link 부등식으로 판정한다. 공통 원인이 없으므로 같은 버그를 공유할 수 없다.

대조는 전 칸에서 한다. 격자 밖 한 칸까지 포함해서 — 예외 규칙의 경계가 정확히 거기 있기 때문이다.

<!-- snippet: include/game/defense_map.hpp@artic-connected -->
```cpp
    bool ArticConnected(int blockX, int blockY) const {
        EnsureChoke();
        if (!articConnected_) return false;
        if (!InBounds(blockX, blockY)) return true;
        if ((blockX == spawn_.x && blockY == spawn_.y) ||
            (blockX == core_.x && blockY == core_.y)) return true;
        if (!Passable(blockX, blockY)) return true;
        return chokeMask_[static_cast<size_t>(blockY * columns_ + blockX)] == 0;
    }
```

flood fill 팔이 암묵적으로 갖고 있던 네 규칙을 여기서 명시했다. 격자 밖을 막으면 후보 격자가 그대로다. 스폰·코어를 막으면 후보 격자가 그 두 칸을 되살린다. 이미 못 가는 칸을 다시 막아도 아무것도 바뀌지 않는다. 그 밖의 통행 칸만 분리 정점 여부를 묻는다.

실측: 8시드 × 3가지 밀도 임계에서 **44,376칸 전부 일치.** 발파로 바위 24칸을 열고 잔해로 24칸을 막은 뒤에도 전 칸 일치. 그 격자들에 표시된 봉쇄 칸은 합계 4,373개다 — 0이었다면 두 팔은 "전부 배치 가능"에 대해서만 합의한 것이고, 그때 이 대조는 아무것도 검사하지 않는다.

그런데 이 대조에는 구멍이 하나 있었다. **절차 맵은 밀도장이 만든 특정한 형질만 갖는다.** 고리에 매달린 막다른 가지처럼 그 형질 밖의 위상에서만 드러나는 결함이 있고, low-link 의 역간선 처리가 정확히 그 부류다(11절에서 실제로 그 변이가 살아남았다).

그래서 세 번째 오라클을 붙였다. 7×7 무작위 벽 배치 격자를 4,000개 만들고, **칸을 실제로 지워 BFS 를 돌린다.** 이쪽에는 DFS 도 `low` 도 없다 — 분리 정점의 정의를 그대로 실행한다.

```text
  무작위 격자 1643개(연결됨) · 칸 57916개 전수 대조, 분리 정점 6078개
```

그리고 마지막 단언이 이 절의 결론이다. 두 팔로 400틱을 돌린 세계의 **상태 해시가 비트 동일**하다.

```text
  두 팔 상태 해시 0x98978851724926e0 동일 (배치 16 · 거절 384)
```

배치도 거절도 실제로 일어나야 이 대조가 의미를 갖는다. 전부 거절이면 경로가 한 번도 바뀌지 않아 두 팔이 같은 정적 격자만 보고 답한 셈이고, 전부 성공이면 연결성 게이트가 한 번도 답하지 않은 셈이다. 그래서 밀도 임계를 두 극단 사이에 둔다.

비용은 (a) 다음에 온다. 이 저장소의 A/B 규약이 두 단계인 이유가 그것이다 — 결과가 같다는 단언 없이 재는 수치는 빨라진 쪽이 다른 답을 냈을 가능성을 배제하지 못한다.

```text
  격자 전수 질의(41x41 = 1681칸) 1회 — floodfill 19.26ms, artic 0.010ms, 1963x
```

1,963배는 놀라운 수가 아니다. 한쪽은 칸마다 1,681칸짜리 레벨을 새로 짓고 flood fill 을 돌아 `O(n^2)` 이고, 다른 쪽은 격자당 한 번 DFS 를 돌고 표를 읽어 `O(n)` 이다. 놀라운 것은 그 앞의 문장이다 — **그렇게 다른 두 계산이 44,376칸에서 한 칸도 다르지 않다.**

그리고 19.26ms 라는 수가 이 흡수의 이유를 설명한다. 60Hz 프레임 예산이 16.7ms 다. 칸 질의 팔에서 격자 전체를 칠하는 것은 느린 정도가 아니라 **불가능**했다.

## 8. 상점에 순서가 생겼다

영구 강화는 가격표 하나짜리 목록이었다. 값만 있으면 아무 순서로나 해금됐고, "이것을 먼저 열어야 저것이 열린다"는 개념이 없었다. 변조된 프로필이 잎 노드만 들고 있어도 통과했다.

`assets/unlocks.txt` 가 그 관계를 저작한다.

```text
unlock 0
unlock 1 requires=0
unlock 2 requires=0
unlock 3 requires=1,2
```

Kahn 위상 정렬이 두 가지를 한 번에 준다. 상점의 표시 순서와, 순환 파일의 거부.

<!-- snippet: include/algo/topo_sort.hpp@topo-kahn -->
```cpp
inline TopoResult TopologicalOrder(size_t count, const std::vector<Edge>& edges) {
    TopoResult result;
    std::vector<uint32_t> indegree(count, 0);
    std::vector<std::vector<uint32_t>> next(count);
    for (const Edge& edge : edges) {
        if (edge.from >= count || edge.to >= count) return result;   // 범위 밖 = 거부
        next[edge.from].push_back(edge.to);
        ++indegree[edge.to];
    }
```

동점을 무엇으로 가르는지가 이 코드에서 유일하게 비자명한 선택이다. 진입 차수가 동시에 0이 되는 항목이 여럿이면 Kahn 은 아무것이나 먼저 꺼내도 **옳다.** 옳지만 재현되지 않는다. 삽입 순서로 꺼내면 파일의 줄 순서가 상점 순서가 되고, 줄 하나를 옮겨 적은 사람은 자기가 순서를 바꿨다는 것을 모른다. 스크린샷도 검사도 그 순간 갈린다.

그래서 꺼내는 자리를 **종류 번호 오름차순 최소 힙**으로 고정한다. 검사가 그것을 확인하는 방법은 같은 그래프를 줄 순서만 다르게 적어 두 파일이 같은 한 줄을 내는지 보는 것이다.

순환은 "순환이 있다"로 끝내지 않는다. 그 문장은 저작자에게 아무것도 알려 주지 않는다.

<!-- snippet: include/algo/topo_sort.hpp@topo-scc -->
```cpp
inline std::vector<std::vector<uint32_t>> CyclicComponents(
    size_t count, const std::vector<Edge>& edges) {
    std::vector<std::vector<uint32_t>> next(count);
    std::vector<uint8_t> selfLoop(count, 0);
```

Tarjan 의 강결합 요소가 **누가 서로를 물고 있는지**를 지목한다.

```text
  순환 진단: unlocks: 선행 관계에 순환이 있다 [1,2,3]
```

그 파일은 통째로 거부된다. 순환에 걸리지 않은 항목이 함께 있어도 마찬가지다 — 이벤트 창·기능 플래그와 같은 규율이고 이유도 같다. 절반만 적용된 선행 그래프는 상점에 영원히 회색인 항목을 남긴다.

그래프는 두 자리에서 소비된다. 하나는 구매다.

<!-- snippet: include/live/economy.hpp@economy-unlock -->
```cpp
    bool UnlockTower(uint32_t account, uint32_t towerType) {
        if (HasUnlock(account, towerType)) return false;
        const Entry* existing = Find(account);
        static const std::vector<uint32_t> kNone;
        if (!unlocks_.Satisfied(towerType, existing ? existing->unlocks : kNone)) return false;
        if (!Spend(account, TowerUnlockCost(towerType), Sink::TowerUnlock)) return false;
```

선행 검사가 **소각보다 먼저** 온다. 순서가 뒤집히면 거절된 해금이 스크랩을 태우고, 그때 보존 불변식은 여전히 참이면서 플레이어는 돈만 잃는다. 검사가 그것을 함께 단언한다 — 거절 뒤에 `Sunk()` 가 0인가.

다른 하나는 지갑 로드다.

<!-- snippet: include/live/unlock_graph.hpp@unlock-satisfied -->
```cpp
    bool Satisfied(uint32_t type, const std::vector<uint32_t>& unlocks) const {
        if (nodes_.empty()) return true;
        if (!Declared(type)) return false;   // 저작되지 않은 종류는 상점에 없다
        for (uint32_t need : Prerequisites(type))
            if (!std::binary_search(unlocks.begin(), unlocks.end(), need)) return false;
        return true;
    }
```

저장 파일에서 온 지갑의 해금 집합이 **선행 폐포**인지 묻는다. 잎만 들고 있는 지갑은 이 게임에서 도달할 수 없는 상태이므로 손상이거나 변조다.

그리고 파일이 없으면 그래프가 비어 있고, 그때 `Satisfied` 는 언제나 참이다. **기본값이 스위치 이전의 동작이어야 한다**는 규약이 저작 파일에도 그대로 적용된다.

제품 안에서 도는 자리는 `--unlock N` 이다. 상점 순서를 따라 사므로 선행이 언제나 먼저 결제되고 — 그래서 그 반복문에는 "선행을 먼저 사라"는 분기가 없다. 순서가 그 규칙을 이미 담고 있다.

```text
[unlocks] source=file nodes=4 order=0>1>2>3 owned=0
[meta] unlock tower=0 cost=300 balance=228
```

## 9. 첫 일치가 아니라 모든 일치

입장 토큰의 페이로드에서 여섯 개의 필드를 뽑을 때, 예전에는 필드마다 `json.find(prefix)` 를 한 번씩 — 같은 비신뢰 문자열을 처음부터 여섯 번 — 훑었다.

속도가 문제였던 적은 없다. 페이로드는 수백 바이트다.

문제는 여섯 모두가 **첫 일치만 채택한다**는 것이었다. 같은 최상위 필드가 두 번 적힌 페이로드에서 앞의 것이 조용히 이긴다. 서명이 유효한 토큰 안에서 필드가 두 번 나오는 상태는 발급기의 결함이거나 주입이고, 둘 다 승인 대상이 아니다.

다중 패턴 자동자는 첫 일치가 아니라 **모든 일치**를 낸다. 그래서 세는 것이 가능해지고, 세는 것이 가능해지면 조용한 정책을 명시적 거부로 바꿀 수 있다.

<!-- snippet: include/live/admission_token.hpp@admission-scan -->
```cpp
    const detail::AdmissionFields fields = detail::ScanAdmissionFields(json);
    for (uint32_t field = 0; field < detail::kFieldCount; ++field) {
        // 없으면 불완전한 토큰이고, 둘 이상이면 발급기 결함이거나 주입이다.
        // 예전 코드는 후자를 통과시켰다 — 앞의 값을 조용히 골랐기 때문이다.
        if (fields.count[field] != 1) return false;
    }
```

자동자는 세 개의 링크로 산다. `goto` 는 트라이의 간선이고, `failure` 는 goto 가 없을 때 떨어질 곳이며, `output` 은 한 위치에서 끝나는 다른 패턴들의 목록이다.

<!-- snippet: include/algo/aho_corasick.hpp@aho-build -->
```cpp
    void Build() {
        if (nodes_.empty()) NewNode();
        std::vector<uint32_t> queue;
        queue.reserve(nodes_.size());
        for (int c = 0; c < 256; ++c) {
            const uint32_t child = nodes_[0].next[c];
            if (child == kNone) {
                nodes_[0].next[c] = 0;    // 뿌리에서 모르는 문자는 제자리
                continue;
            }
```

failure link 가 실제로 일하는 자리가 이 패턴 집합에 있다. 여섯 패턴이 전부 `"` 로 시작하고, 문자열 필드의 패턴은 `"Kind":"` 처럼 중간에도 `"` 를 갖는다. 그래서 `"Kind"` 라는 상태의 실패 링크는 뿌리가 아니라 `"` 상태다.

무슨 차이가 나는가. 페이로드 `{"Kind"PlayerId":"bob"}` 을 생각해 보자. `"Kind"` 를 읽고 다음 문자 `P` 에서 goto 가 없다. 올바른 실패 링크를 따르면 `"` 상태에서 `P` 로 가 `"PlayerId":"` 의 중간에 서고, 이어지는 `layerId":"` 가 그 패턴을 완성한다. 실패 링크를 전부 뿌리로 만들면 그 일치를 **놓친다.**

차분 오라클이 그것을 잡는다. 무작위 페이로드에 패턴 조각을 섞어 400회 만들고, 자동자가 낸 일치 multiset 을 패턴별 naive 검색 multiset 과 비교한다. 실측 2,705건이 완전히 일치했다.

그리고 이 파일에서 바뀌지 않아야 하는 것이 하나 있다. **서명 검증이 필드 스캔보다 먼저 온다.** 코드를 읽으면 확인되지만 검사로는 확인되지 않던 성질이었다. 훑기 계수기 하나가 그것을 관측 가능하게 만든다.

```text
  서명 실패 시 훑기 0회, 정상 토큰 훑기 1회
```

예전에는 정상 토큰 하나에 여섯 번이었다.

## 10. 어느 64를 싣는가

스냅샷 한 장에 실을 수 있는 적은 64자리다. 규모 팔에서 살아 있는 적은 2048까지 오른다.

인코더는 이렇게 채우고 있었다.

```cpp
size_t enemyCount = std::min(aliveEnemies, kSnapshotMaxEnemies);
```

**저장 행 순서 앞에서부터 64마리.** 코어 앞에 도착한 적이 뒷 행이면 화면에 나타나지 않는다. 그리고 그 행 순서는 고정된 것도 아니다 — 죽은 행은 swap-remove 로 메워지므로, 몇 마리가 어디서 죽었느냐가 그 순서를 흔든다.

relevance 팔은 전순서를 정하고 상위 64를 quickselect 로 고른다.

<!-- snippet: include/game/defense_protocol.hpp@snapshot-relevance -->
```cpp
struct EnemyPick {
    uint32_t hidden = 0;     // 0 = 노출됨
    uint32_t distance = 0;   // 코어까지 격자 맨해튼 거리
    uint32_t strength = 0;   // 인코드된 체력의 보수 — 작을수록 강하다
    uint32_t id = 0;
    size_t   row = 0;
};

inline bool MoreRelevant(const EnemyPick& a, const EnemyPick& b) {
    if (a.hidden != b.hidden) return a.hidden < b.hidden;
    if (a.distance != b.distance) return a.distance < b.distance;
    if (a.strength != b.strength) return a.strength < b.strength;
    return a.id < b.id;    // 전순서를 닫는 겹. id 는 월드 안에서 유일하다.
}
```

마지막 겹이 이 구조체의 존재 이유다. **동점이 하나도 없어야 한다.** 동점이 있으면 분할이 그 둘을 어느 쪽에 두는지가 입력 배열 순서에 좌우되고, 그러면 같은 월드가 두 번 다른 패킷을 낸다.

<!-- snippet: include/algo/quickselect.hpp@quickselect-topk -->
```cpp
template <class T, class Less>
inline void SelectTopK(T* data, size_t count, size_t k, Less less) {
    if (k >= count || k == 0) return;
    size_t lo = 0, hi = count - 1;
    while (lo < hi) {
        const size_t pivotAt = detail::MedianOfThree(data, lo, hi, less);
```

피벗도 결정적이다. 무작위 피벗은 기대 O(n) 을 주지만 이 제품에서는 쓸 수 없다 — 난수원을 시뮬의 다른 소비자와 공유하면 게임이 갈린다. 세 자리 중앙값은 난수 없이 정렬된 입력의 최악을 피한다.

고른 뒤 그 64개를 다시 정렬하는 한 줄이 있는데, 이유가 비용이 아니라 **정합성**이다. quickselect 가 남기는 앞 k 개의 내부 순서는 입력 배열 순서에 좌우되므로, 정렬하지 않으면 행 순서를 섞은 같은 월드가 다른 바이트를 낸다. 정렬하면 패킷이 집합의 함수가 된다. 비용은 `n + k log k` 대 `n log n` — 전수 정렬과 같은 답을 훨씬 싸게 얻는 것이 quickselect 를 쓴 이유다.

실측. 적 900마리가 살아 있는 월드에서:

```text
  적 900마리 중 상위 64를 전수 정렬 오라클과 동일하게 골랐다
  치환 12회: relevance 집합 불변, rows 집합은 12회 모두 달라졌다
  코어 최근접 적 id=814 — rows 팔에 빠짐 / relevance 만 싣는 적 64마리
  실린 적의 평균 코어 거리 — rows 14.0칸, relevance 11.0칸
```

두 번째 줄에 음성 대조가 들어 있다. 같은 치환에서 "앞 64행" 정책은 12회 모두 다른 집합을 냈다 — 그러지 않았다면 첫 줄의 불변 단언은 아무것도 구분하지 못한다.

세 번째 줄이 이 흡수가 고치는 결함이다. 코어에 **가장 가까운** 적이 기본 팔의 스냅샷에서 빠져 있다.

이것은 프로토콜 변경이다. 프레이밍 바이트는 하나도 바뀌지 않았지만 같은 월드가 다른 64를 실을 수 있으므로, 버전을 올리지 않으면 두 빌드가 "같은 프로토콜"이라고 주장하면서 서로 다른 세계를 그린다. `wire::kVersion` 이 6에서 7로 올랐고, 기본 팔은 그대로 `rows` 다.

## 11. 변이 — 스무 개를 심고 무엇이 죽었는가

흡수의 세 번째 조건이 이 절이다. 한 줄을 지웠는데 아무것도 실패하지 않으면 그 줄은 검사되지 않는 것이다.

변이 스무 개를 하나씩 심고, 그 변이가 겨냥한 테스트를 빌드해 돌렸다.

| 변이 | 대상 | 1차 | 최종 |
|---|---|---|---|
| canonical 배정의 `<< 1` 제거 | `test_replay_golden` | 죽음 | 죽음 |
| Kraft 완전성 검사를 통과로 | `test_replay_golden` | 죽음 | 죽음 |
| 긴 코드 fallback 제거 | `test_replay_golden` | 죽음 | 죽음 |
| 본문 해시 대조 제거 | `test_replay_golden` | 죽음 | 죽음 |
| Kruskal 의 `Union` 반환 무시 | `test_map_procgen` | 죽음 | 죽음 |
| union by size 제거 | `test_map_procgen` | **생존** | **생존** |
| `low >= tin` 을 `>` 로 | `test_build_connectivity` | 죽음 | 죽음 |
| 역간선 갱신에 `low` 사용 | `test_build_connectivity` | **생존** | 죽음 |
| 판정 캐시의 버전 무효화 제거 | `test_build_connectivity` | 죽음 | 죽음 |
| `--indegree[v] == 0` 을 `indegree[v] == 0` 으로 | `test_meta_economy` | 죽음 | 죽음 |
| 위상 정렬 동점을 삽입 순서로 | `test_meta_economy` | 죽음 | 죽음 |
| 지갑 폐포 검사 제거 | `test_meta_economy` | 죽음 | 죽음 |
| 해금 선행 검사 제거 | `test_meta_economy` | 죽음 | 죽음 |
| failure link 를 전부 root 로 | `test_admission` | 죽음 | 죽음 |
| output link 병합 제거 | `test_admission` | **생존** | **생존** |
| 필드 중복 카운트 제거 | `test_admission` | 죽음 | 죽음 |
| 서명 검증을 스캔 뒤로 | `test_admission` | 죽음 | 죽음 |
| 분할의 교환 제거 | `test_defense_protocol` | 죽음 | 죽음 |
| 전순서의 id 겹 제거 | `test_defense_protocol` | 죽음 | 죽음 |
| 선택 뒤 재정렬 제거 | `test_defense_protocol` | **생존** | 죽음 |

1차에서 넷이 살아남았다. 그중 **둘은 검사의 구멍**이었고, 그 둘을 메우는 것이 이 절의 실제 내용이다.

**역간선 갱신에 `low` 사용.** 무방향 DFS 에서 이미 방문된 이웃의 `low` 를 끌어오면 "이 부분트리가 실제보다 위로 갈 수 있다"고 잘못 계산되고, 그러면 진짜 분리 정점을 놓친다. 그런데 그것이 드러나려면 고리에 매달린 막다른 가지 같은 특정 위상이 **스폰→코어 트리 경로 위에** 있어야 한다. 절차 맵 24장에는 그 배치가 없었다.

메운 방법은 오라클을 하나 더 붙이는 것이다. 7×7 무작위 벽 격자 4,000개에서, 칸마다 실제로 지우고 BFS 를 돌린 결과와 대조한다. 그 오라클에는 DFS 도 `low` 도 없다. 다시 심으니 **1,446번째 격자의 (4,3) 칸**에서 죽었다.

**선택 뒤 재정렬 제거.** quickselect 가 남기는 앞 64개의 내부 순서는 입력 배열 순서에 좌우된다. 그런데 내 검사는 선택된 **집합**만 비교하고 있었고(정렬해서), 같은 월드를 두 번 인코드하는 검사는 입력 순서가 같으니 당연히 같은 바이트를 냈다. 순서를 검사하는 것이 하나도 없었다.

메운 방법은 실린 순서가 전순서를 따르는지 직접 보는 것이다. 음성 대조도 함께 둔다 — 기본 팔이 싣는 순서에는 역전이 28건 있다. 그러지 않았다면 이 단언은 두 팔을 구분하지 못한다.

**나머지 둘은 왜 살아남는지가 알려진 둘이다.**

**union by size.** 경로 압축과 함께 이 자료구조의 성능을 만들지만, 정확성에는 기여하지 않는다. 지워도 신장 트리는 그대로 신장 트리다. 이 변이를 잡으려면 성능 단언이 필요하고, 성능 단언은 게임이 요구하는 것이 아니다 — 그 측정은 통제된 합성 워크로드에서만 값을 낸다.

**output link 병합.** 이 패턴 집합에서는 어떤 패턴도 다른 패턴의 접미사가 아니다(`"Kind":"`, `"PlayerId":"`, … 여섯 개 전부). 그래서 한 위치에서 두 패턴이 동시에 끝나는 일이 **구성상 일어나지 않고**, output link 는 제품에서 한 번도 쓰이지 않는다. 잡히지 않는 것이 검사의 구멍이 아니라 그 사실의 반영이다.

이 둘을 표에서 지우고 "18개 심어 18개 잡았다"고 적을 수도 있었다. 그러지 않는 이유는 잡히지 않은 변이가 알려 주는 것이 잡힌 변이보다 많기 때문이다 — 어디까지가 이 게임이 요구하는 성질이고 어디부터가 아닌지의 경계가 정확히 그 둘에 있다.

그리고 1차의 넷 중 둘이 구멍이었다는 사실 자체가 이 절차의 값이다. **변이를 심지 않았다면 그 두 검사는 초록이면서 아무것도 지키지 않는 상태로 남았을 것이다.**

**union by size.** 경로 압축과 함께 이 자료구조의 성능을 만들지만, 정확성에는 기여하지 않는다. 지워도 신장 트리는 그대로 신장 트리다. 이 변이를 잡으려면 성능 단언이 필요하고, 성능 단언은 게임이 요구하는 것이 아니다 — 그 측정은 통제된 합성 워크로드에서만 값을 낸다.

**output link 병합.** 이 패턴 집합에서는 어떤 패턴도 다른 패턴의 접미사가 아니다(`"Kind":"`, `"PlayerId":"`, … 여섯 개 전부). 그래서 한 위치에서 두 패턴이 동시에 끝나는 일이 **구성상 일어나지 않고**, output link 는 제품에서 한 번도 쓰이지 않는다. 잡히지 않는 것이 검사의 구멍이 아니라 그 사실의 반영이다.

이 둘을 표에서 지우고 "18개 심어 18개 잡았다"고 적을 수도 있었다. 그러지 않는 이유는 잡히지 않은 변이가 알려 주는 것이 잡힌 변이보다 많기 때문이다 — 어디까지가 이 게임이 요구하는 성질이고 어디부터가 아닌지의 경계가 정확히 그 둘에 있다.

## 12. 실행과 관측

### 빌드와 전수 검사

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -j 4
```

```text
100% tests passed out of 127
```

121에서 127로 늘었다. 새로 선 여섯은 `test_build_connectivity` 하나, 셀프테스트 셋(`_maze` · `_unlock_warm` · `_unlock`), 그리고 기록·재생 짝 하나(`record_relevance` / `replay_relevance`)다.

### 도달성 게이트

```text
도달성 게이트 - include/ 헤더 121개
  제품 mega_defense           도달 116개
  제품 mega_defense_server    도달 91개
  제품 도달 합계 121개 / 랩 전용 등재 0개 / 근거 없는 미도달 0개
  README 코드 구조 트리 대조: include/ 하위 디렉터리 22개
  게이트 통과: 제3의 상태 없음
```

`include/algo/` 의 여섯 헤더와 `include/live/unlock_graph.hpp` 가 전부 제품에서 도달한다. 등재부는 여전히 0행이다.

### 리플레이 압축

```powershell
.\build\bin\Release\mega_defense.exe --selftest 4000 --record solo.mgrp --match-unix 1767312000
```

```text
[record] path=solo.mgrp frames=1377 ticks=1377 marks=1377 stride=1
         chain=0xecfa0daff65fe392 avatar_frames=1377 mode=0 players=1 predict=0
[record] wave=6 towers=10 bytes=35394 raw_body=44088 ratio=80.3%
```

그리고 그 파일을 다른 프로세스가 읽는다.

```text
[replay] result=MATCH ticks=1377/1377 frames=1377
         chain=0xecfa0daff65fe392/0xecfa0daff65fe392 stride=1
[replay] lockstep=OK frames=1377 stalls=0 fault=none tick=0
```

체인 값이 압축 전과 같다. 압축은 파일 계층이고, 결정성 판정은 그 위를 지나가지 않는다.

### 압축 블록의 성질

```text
  리플레이 압축 1920085 -> 1483897 바이트 (77.3%, 프레임 60000)
  그중 압축 불가(해시) 960000바이트 = 본문의 50.0% — 이론 하한 50.0%
  코드 길이표 최장 9비트 (lookup 폭 9비트)
  긴 코드 파일 최장 15비트, 800080 -> 112862 바이트 (14.1%)
  절단 표본 200곳 전부 거절
  비트 손상 표본 200곳 전부 거절
  코드 길이표·방식·크기 거부 5종 확인
```

### 골격 두 팔

```text
  골격 형질 8시드 합계 — dfs 분기 325 막다른길 344 / kruskal 분기 850 막다른길 986
```

제품 안에서 도는 자리:

```powershell
.\build\bin\Release\mega_defense.exe --selftest 25000 --match-unix 1767312000 `
  --ab map=procgen --ab map_maze=kruskal --ab build_check=artic --ab pathing=flowfield
```

```text
[terrain] seed=7 41x41 maze=799 carved=314 rock=568 veins=142 route=77 bvh_prims=568/nodes=367
[selftest] result=CLEAR ticks=2744 towers=7 wave=6 packet=140 protocol=OK render=OK
```

### 배치 판정 두 팔

```text
  무작위 격자 1643개(연결됨) · 칸 57916개 전수 대조, 분리 정점 6078개
  전 칸 대조 44376칸 일치, 표시된 봉쇄 칸 합계 4373
  발파 24칸 · 잔해 24칸 뒤에도 전 칸 일치
  두 팔 상태 해시 0x98978851724926e0 동일 (배치 16 · 거절 384)
  격자 전수 질의(41x41 = 1681칸) 1회 — floodfill 19.26ms, artic 0.010ms, 1963x
```

### 해금 선행 그래프

```text
  순환 진단: unlocks: 선행 관계에 순환이 있다 [1,2,3]
  상점 순서 0>1>2>3 (줄 순서와 무관)
  제품 상점 순서: 0 1 2 3 (파일 4노드)
```

```text
[unlocks] source=file nodes=4 order=0>1>2>3 owned=1
[meta] unlock tower=0 cost=300 balance=228
[meta] settle scrap=+205 balance=433 supply=433 sourced=733 sunk=300 tier=0
```

`sunk=300` 이 이 배선의 증거다 — 매치 밖 소비가 실제로 일어났고 보존 불변식이 그것을 셌다.

### 입장 토큰

```text
  차분 오라클 400회, 일치 2705건이 naive 검색과 동일
  서명 실패 시 훑기 0회, 정상 토큰 훑기 1회
```

### 스냅샷 선택

```text
  적 900마리 중 상위 64를 전수 정렬 오라클과 동일하게 골랐다
  치환 12회: relevance 집합 불변, rows 집합은 12회 모두 달라졌다
  실린 순서가 전순서를 따른다 (기본 팔의 역전 28건)
  코어 최근접 적 id=814 — rows 팔에 빠짐 / relevance 만 싣는 적 64마리
  실린 적의 평균 코어 거리 — rows 14.0칸, relevance 11.0칸
```

기록·재생 짝의 네 번째 팔이 이 선택 정책을 지나는 한 판을 통째로 돌린다.

```powershell
ctest --test-dir build -C Release -R "relevance"
```

```text
Test #122: mega_defense_record_relevance ....   Passed
Test #123: mega_defense_replay_relevance ....   Passed
```

선택 정책이 시뮬을 건드렸다면 여기서 갈린다.

---

## 결론

여섯 개를 들여왔고, 여섯 개 모두 이미 있던 무언가를 대체했다. 그것이 이 편에서 가장 중요한 문장이다.

측정에서 나온 것 넷.

**변이를 심기 전까지 검사는 초록이면서 아무것도 지키지 않을 수 있다.** 1차에서 살아남은 넷 중 둘이 그 상태였다. 하나는 절차 맵에 없는 위상에서만 드러나는 결함이었고(오라클을 하나 더 붙여 메웠다), 다른 하나는 집합만 비교하고 순서를 아무도 보지 않던 자리였다. 두 검사 모두 그 전에 통과하고 있었다.

**천장을 모르면 수치가 거짓말을 한다.** 리플레이 압축의 80.3%는 나쁜 수가 아니라 **가능한 최선에 가까운 수**다. 본문의 절반이 해시 출력이라 구성상 줄지 않기 때문이다. 그 사실을 검사가 함께 적지 않았다면 다음 사람은 "왜 이것밖에 안 줄었지"를 며칠 쫓았을 것이다.

**같은 답을 내는 두 구현은 그 자체가 검사다.** 배치 판정 두 팔은 구조가 달라 같은 버그를 공유하지 않는다. 44,376칸 전부에서 일치한다는 사실은 어느 한쪽만 있을 때는 존재할 수 없던 종류의 증거다.

**성능이 흡수의 이유였던 적이 한 번도 없다.** low-link 는 전수 판정을 위해, Aho-Corasick 은 모든 일치를 세기 위해, quickselect 는 결정적 선택을 위해 들어왔다. 빨라진 것은 전부 부산물이고, 만약 그것이 유일한 이유였다면 여섯 중 넷은 넣지 않았을 것이다.

현재 상태의 한계 넷.

**압축이 0차 모델뿐이다.** 골든 마크의 틱 번호는 1, 2, 3, … 으로 이어지는데 그 구조를 바이트 단위 Huffman 은 볼 수 없다. 차분을 취한 뒤 접으면 그 4바이트가 거의 사라지지만, 그것은 이 편이 흡수한 알고리즘이 아니다.

**relevancy 가 적에만 붙었다.** 타워 16자리와 발사체 18자리도 같은 형태로 잘리고, 같은 결함을 갖는다. 타워는 게임플레이 상한이 프로토콜 상한과 같아 지금은 잘리지 않고, 발사체는 순수 표시용이라 우선순위를 정할 근거가 약하다 — 근거를 못 만드는 자리에 알고리즘을 넣지 않는다는 것이 이 편의 규칙이었다.

**해금 그래프가 타워 종류에만 걸린다.** 시작 골드 티어는 여전히 선형 사슬이고 그래프에 들어가지 않는다. 사슬은 DAG 의 특수한 경우이므로 표현할 수는 있지만, 그것으로 얻는 것이 없다.

**Kruskal 팔의 밸런스 영향이 측정되지 않았다.** 분기가 2.6배라는 것은 지형의 성질이지 난이도의 성질이 아니다. 두 팔에서 같은 스크립트가 몇 웨이브를 버티는지는 재지 않았고, 그 수 없이는 "밸런스 손잡이"라는 말이 형태의 서술에 머문다.
