#pragma once

namespace skl {

// 워커가 *어떤 실행 단위* 위에서 도는지 결정하는 토글.
//
// Pthread:   N개 OS 스레드가 진짜 병렬 실행. 컨텍스트 스위치는 커널 스케줄러 경계.
// Coroutine: 한 OS 스레드 위에 N개 stackful 코루틴이 협력적 round-robin.
//            컨텍스트 스위치는 *유저 공간* 에서 register save/restore (ucontext_t).
//
// 측정 의도: 두 모델의 *컨텍스트 스위치 비용 차이* 와, *한 스레드 직렬 대 다중 코어
// 병렬* 의 처리량 차이. 이 lab은 동기 IO 워크로드라 코루틴이 IO 대기 중 yield 할
// 기회가 적어, 코루틴 모드는 *진짜 병렬을 잃은 시리얼 실행* 에 가까운 결과를 낸다.
// 그게 *옳은 결과* — 코루틴은 비동기 IO와 결합할 때 빛난다.
enum class CtxType : int {
    Pthread   = 0,
    Coroutine = 1,
};

constexpr int kCtxTypeCount = 2;

inline constexpr const char* kCtxTypeLabels[kCtxTypeCount] = {
    "pthread", "coroutine",
};

}  // namespace skl
