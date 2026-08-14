## 들어가며

Rollback과 delay-based 동기화는 같은 불확실성을 다른 위치에 둔다. Delay 방식은 원격 입력이 올 때까지 기다려 틀리지 않지만 반응이 늦다. Rollback은 입력을 예측해 즉시 진행하고, 틀리면 과거로 돌아가 재시뮬레이션한다.

---

## 목차
1. [공통 결정론 시뮬레이션](#1-공통-결정론-시뮬레이션)
2. [입력 예측과 히스토리](#2-입력-예측과-히스토리)
3. [불일치와 재시뮬레이션](#3-불일치와-재시뮬레이션)
4. [Delay 모드](#4-delay-모드)
5. [검증 실험](#5-검증-실험)

---

## 1. 공통 결정론 시뮬레이션

두 모드가 같은 입력에서 같은 최종 상태를 내려면 `StepSim(tick)`이 결정론적이어야 한다. 현재 상태와 해당 tick의 두 플레이어 입력만으로 다음 상태가 정해져야 한다.

렌더링, 사운드, 난수 호출 같은 부수 효과는 재시뮬레이션 경로에 넣지 않는다. rollback 중 같은 이펙트가 여러 번 발화하면 화면 결과가 시뮬레이션 상태와 달라진다.

## 2. 입력 예측과 히스토리

원격 입력이 아직 없으면 마지막 확정 입력을 반복하는 것이 가장 단순한 예측이다.

```text
confirmed:  +1 +1 +1
missing:             ?
predicted:           +1
```

매 tick마다 상태 스냅샷과 사용한 입력을 짧은 링 버퍼에 보관한다. 최대 네트워크 지연보다 긴 히스토리가 있어야 늦게 도착한 입력까지 되감을 수 있다.

## 3. 불일치와 재시뮬레이션

도착한 실제 입력이 예측과 다르면:

1. 해당 tick 직전 상태를 복원한다.
2. 확정된 실제 입력으로 그 tick을 다시 실행한다.
3. 현재 tick까지 저장된 입력을 재적용한다.
4. rollback 횟수와 재시뮬 깊이를 기록한다.

```cpp
if (mode_ == 0 && wasPredicted && mismatch) {
    ++rollbacksWin_;
    ++mispredictWin_;
    resimDepthWin_ += curTick_ - inputTick;

    int prediction = LastConfirmedRemoteInputBefore(inputTick);
    for (int tick = inputTick; tick < curTick_; ++tick) {
        if (HasConfirmedRemoteInput(tick))
            prediction = inB_[tick % kHistory];
        else
            SetRemoteInput(tick, prediction, false);
        StepSim(tick);
    }
}
```

평균 깊이만 보지 말고 최대 깊이도 봐야 한다. 한 번의 깊은 rollback이 프레임 예산을 넘기면 체감 끊김이 된다.

## 4. Delay 모드

Delay 방식은 입력을 몇 tick 미리 모아 둔 뒤 실행한다. 예측 실패와 rollback 비용은 없지만 입력 반응이 늦어진다. 지연을 늘리면 안정성은 올라가고 조작감은 나빠진다.

## 5. 검증 실험

1. latency 0에서 두 모드 결과가 같고 rollback이 0인지 본다.
2. latency를 올려 Delay 입력 지연과 Rollback 재시뮬 비용을 비교한다.
3. jitter를 올려 rollback 깊이 분포를 본다.
4. 원격 입력 패턴이 바뀌는 구간에서 mispredict가 늘어나는지 확인한다.
5. 같은 입력 로그로 두 모드의 최종 상태가 일치하는지 확인한다.

Rollback은 지연을 제거하지 않는다. *입력 지연을 계산 비용과 수정 가능성으로 교환* 한다.

---

## 결론

결정론적 tick 시뮬레이션, 입력 히스토리, 원격 입력 예측, 상태 복원과 재시뮬레이션을 연결해 Delay 방식과 Rollback 방식의 비용을 같은 조건에서 비교한다. 평균뿐 아니라 최대 재시뮬 깊이와 입력 지연을 측정해 체감과 프레임 예산의 교환을 드러낸다.

현재 모델은 작은 상태를 같은 프로세스 안에서 복제하며 실제 패킷 직렬화, 손실 복구, 상태 메모리 예산을 포함하지 않는다. 결정론이 깨지는 플랫폼 차이도 별도 통제 없이는 이 결과로 해결되지 않는다.
