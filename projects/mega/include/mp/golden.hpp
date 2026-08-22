// golden — 결정성 골든 해시 체인.
//
// 이 저장소에는 이미 두 개의 상태 요약이 있다. `GameWorld::StateHash()` 는 권위
// 시뮬 한 순간을 부동소수 **비트 패턴 그대로** 접고, `mp::DetState::Checksum()`
// 은 아바타 서브시뮬 한 순간을 정수로 접는다. 둘 다 "지금 이 상태"만 말한다.
//
// 그것으로는 부족하다. 한 판이 끝난 뒤 마지막 해시 하나만 비교하면 두 가지를
// 놓친다.
//
//   (1) **언제 갈렸는가를 말할 수 없다.** 18000틱을 돌고 마지막 수가 다르다는
//       사실만 남으면, 진단은 이분 탐색을 손으로 다시 도는 일이 된다.
//   (2) **갈렸다가 도로 붙은 것을 놓친다.** 두 시뮬이 300틱에서 갈렸다가 양쪽
//       다 벽에 눌려 같은 위치로 수렴하면 마지막 해시는 도로 일치한다. 그
//       순간 "desync 는 없었다"는 거짓 green 이 나온다.
//
// 체인이 둘 다 고친다. 틱 t 의 링크는 t-1 까지의 링크 전부를 접은 값 위에 이번
// 틱의 상태를 얹어 만든다 — 접두사에 민감하므로 한 번 갈리면 되돌아오지 않고,
// 마크(구간 끝)를 남겨 두면 처음 갈린 구간을 한 번의 순회로 짚는다.
//
// ── 무엇을 링크에 넣는가 ──────────────────────────────────────────
// 넷이다. 각각 넣는 이유가 다르다.
//
//   틱 번호        위치 민감성. 틱 하나가 빠지거나 끼면 그 뒤 전부가 갈린다.
//                  이것이 없으면 "같은 상태를 한 틱 늦게 지났다"가 같은 체인을 낸다.
//   권위 상태 해시  float 비트 패턴 그대로. 반올림한 값을 섞으면 갈라진 상태가
//                  같은 해시를 낼 수 있고, 그러면 이 체인은 아무것도 증명하지 못한다.
//   아바타 체크섬   정수 서브시뮬. 권위 해시와 별개로 넣는 이유는 소유자가 다르기
//                  때문이다 — 권위는 호스트가, 아바타는 양쪽이 각자 굴린다.
//   아바타 프레임   서브시뮬이 **몇 번 전진했는가**. 비예측 팔의 스톨은 상태를
//                  바꾸지 않으므로 체크섬만으로는 보이지 않는다. 스톨 횟수가
//                  달라진 두 실행은 다른 실행이다.
//
// 넣지 않는 것도 규약이다. 벽시계·프레임 시간·프로파일러 수치는 링크에 넣지
// 않는다. 그것들은 실행마다 다르고, 넣는 순간 이 체인은 언제나 갈린다 —
// 언제나 실패하는 게이트는 게이트가 아니다.
//
// ── 구간(stride)의 뜻 ─────────────────────────────────────────────
// stride = 1 이면 틱마다 마크를 남기므로 갈린 **틱**을 정확히 짚는다. stride = N
// 이면 N틱마다 남기므로 갈린 **구간** `(lastAgreeTick, suspectTick]` 만 나온다.
// 정밀도와 메모리의 맞바꿈이고, 어느 쪽인지는 `ChainDiff::exact` 가 말한다.
// 짐작으로 정확한 틱을 말하지 않는다 — 틀린 진단은 없는 진단보다 나쁘다.

#pragma once

#include <cstdint>
#include <vector>

namespace mega {
namespace mp {

// FNV-1a 64비트의 오프셋과 소수. `GameWorld::StateHash` · `DetState::Checksum`
// 과 같은 상수를 쓰는 것은 우연이 아니라 규율이다 — 세 자리가 같은 접기 규칙을
// 쓰면 어느 하나를 읽을 줄 아는 사람이 나머지 둘도 읽는다.
inline constexpr uint64_t kGoldenOffsetBasis = 1469598103934665603ull;
inline constexpr uint64_t kGoldenPrime = 1099511628211ull;

// 구간 끝. tick 은 **완료한 틱 수**(1부터 센다)이고 chain 은 그 틱까지 접은 값이다.
struct GoldenMark {
    uint32_t tick = 0;
    uint64_t chain = 0;
};

// snippet-begin: golden-append
class GoldenChain {
public:
    // stride 0 은 허용하지 않는다(마크가 하나도 안 남아 진단이 불가능해진다).
    void Reset(uint32_t stride) {
        stride_ = stride == 0 ? 1u : stride;
        chain_ = kGoldenOffsetBasis;
        ticks_ = 0;
        marks_.clear();
        sealed_ = false;
    }

    // 한 틱을 접는다. 순서가 계약이다 — 넷 중 하나라도 자리를 바꾸면 같은 상태가
    // 다른 체인을 내므로, 이 순서는 파일 포맷의 일부다.
    void Append(uint64_t worldHash, uint64_t avatarChecksum, uint32_t avatarFrame) {
        ++ticks_;
        Mix(static_cast<uint64_t>(ticks_));
        Mix(worldHash);
        Mix(avatarChecksum);
        Mix(static_cast<uint64_t>(avatarFrame));
        if (ticks_ % stride_ == 0) marks_.push_back(GoldenMark{ticks_, chain_});
    }

    // 마지막 마크가 마지막 틱과 어긋나 있으면(틱 수가 stride 의 배수가 아니면)
    // 끝을 하나 더 남긴다. 이것이 없으면 두 실행의 **꼬리**가 비교되지 않는다.
    void Seal() {
        if (marks_.empty() || marks_.back().tick != ticks_)
            marks_.push_back(GoldenMark{ticks_, chain_});
        sealed_ = true;
    }
    // snippet-end: golden-append

    uint32_t Stride() const { return stride_; }
    uint32_t Ticks() const { return ticks_; }
    uint64_t Chain() const { return chain_; }
    bool     Sealed() const { return sealed_; }
    const std::vector<GoldenMark>& Marks() const { return marks_; }
    std::vector<GoldenMark>& MutableMarks() { return marks_; }

    // 파일에서 되살릴 때 쓴다. 마크를 그대로 얹고 최종값을 마지막 마크에서 읽는다.
    void Adopt(uint32_t stride, uint32_t ticks, uint64_t chain,
               const std::vector<GoldenMark>& marks) {
        stride_ = stride == 0 ? 1u : stride;
        ticks_ = ticks;
        chain_ = chain;
        marks_ = marks;
        sealed_ = true;
    }

private:
    void Mix(uint64_t value) {
        chain_ ^= value;
        chain_ *= kGoldenPrime;
    }

    uint32_t                stride_ = 1;
    uint32_t                ticks_ = 0;
    uint64_t                chain_ = kGoldenOffsetBasis;
    bool                    sealed_ = false;
    std::vector<GoldenMark> marks_;
};

// 두 체인의 대조 결과. `diverged` 가 false 면 나머지 필드는 읽지 않는다.
struct ChainDiff {
    bool     diverged = false;
    // suspectTick 이 **정확한 틱**인가. 두 체인이 다 stride 1 일 때만 참이다.
    bool     exact = false;
    uint32_t lastAgreeTick = 0;   // 두 체인이 마지막으로 같았던 틱(0 = 처음부터 다름)
    uint32_t suspectTick = 0;     // 처음 갈린 마크의 틱 = 구간의 오른쪽 끝
    uint32_t goldenTicks = 0;
    uint32_t freshTicks = 0;
};

// 구간 체인 대조. 마크를 앞에서부터 훑어 처음 어긋나는 자리를 찾는다.
//
// 왜 이분 탐색이 아닌가. 체인은 접두사에 민감해서 "처음 갈린 뒤 전부 갈린다"가
// 성립하므로 이분 탐색이 가능하긴 하다. 그런데 마크는 이미 메모리에 있고 개수가
// 틱 수 / stride 라 선형 순회가 수백 마이크로초다. 이분 탐색은 코드를 늘리고
// 경계 실수를 부를 뿐 아무 것도 벌지 못한다.
// snippet-begin: golden-compare
inline ChainDiff CompareChains(const GoldenChain& golden, const GoldenChain& fresh) {
    ChainDiff diff;
    diff.goldenTicks = golden.Ticks();
    diff.freshTicks = fresh.Ticks();
    diff.exact = golden.Stride() == 1 && fresh.Stride() == 1;

    const std::vector<GoldenMark>& a = golden.Marks();
    const std::vector<GoldenMark>& b = fresh.Marks();
    const size_t common = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < common; ++i) {
        if (a[i].tick == b[i].tick && a[i].chain == b[i].chain) {
            diff.lastAgreeTick = a[i].tick;
            continue;
        }
        diff.diverged = true;
        // 마크의 틱 번호까지 어긋났다면(한쪽이 stride 를 다르게 잡았다면) 작은
        // 쪽이 의심 구간의 오른쪽 끝이다.
        diff.suspectTick = a[i].tick < b[i].tick ? a[i].tick : b[i].tick;
        return diff;
    }

    // 마크가 겹치는 데까지는 같은데 길이가 다르다 — 한쪽이 먼저 끝났다.
    // 이것도 갈림이다. 조기 종료는 상태가 아니라 **진행**의 차이라서 마지막
    // 해시 비교로는 잡히지 않는 부류다.
    if (a.size() != b.size() || golden.Ticks() != fresh.Ticks() ||
        golden.Chain() != fresh.Chain()) {
        diff.diverged = true;
        diff.suspectTick = diff.lastAgreeTick + 1;
    }
    return diff;
}
// snippet-end: golden-compare

}  // namespace mp
}  // namespace mega
