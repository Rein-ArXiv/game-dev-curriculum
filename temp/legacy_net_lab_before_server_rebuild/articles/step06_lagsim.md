## 들어가며

다른 플레이어의 입력은 알 수 없으므로 클라이언트는 수신한 snapshot으로 움직임을 복원한다. Interpolation은 과거 두 표본 사이를 그려 안정성을 얻고, Extrapolation은 미래를 추정해 지연을 줄이는 대신 틀릴 위험을 진다.

---

## 목차
1. [패킷 전달 모델](#1-패킷-전달-모델)
2. [보간 버퍼](#2-보간-버퍼)
3. [외삽](#3-외삽)
4. [오차와 손실 지표](#4-오차와-손실-지표)
5. [검증 실험](#5-검증-실험)

---

## 1. 패킷 전달 모델

Scene은 참 위치를 일정한 함수 `TruePos(t)`로 만들고, send rate에 맞춰 snapshot을 생성한다. 각 패킷에는 지연과 jitter를 더한 `arriveTime`이 붙고, loss 확률에 따라 버려진다.

도착 시각 순서가 송신 순서와 다르면 재정렬로 기록한다. 렌더링은 도착 순서가 아니라 snapshot timestamp 순서로 버퍼를 관리해야 한다.

## 2. 보간 버퍼

Interpolation은 현재보다 `interpDelay`만큼 과거인 render time을 고른다. 그 시각을 둘러싼 두 snapshot 사이를 보간한다.

```cpp
const double renderTime = time_ - interpDelay_;
for (size_t i = received_.size(); i-- > 0;) {
    if (received_[i].sendTime > renderTime) continue;
    if (i + 1 == received_.size()) return received_[i].pos;

    const Snapshot& a = received_[i];
    const Snapshot& b = received_[i + 1];
    const double span = b.sendTime - a.sendTime;
    const float t = span > 1e-6
        ? std::clamp((float)((renderTime - a.sendTime) / span), 0.0f, 1.0f)
        : 0.0f;
    return Lerp(a.pos, b.pos, t);
}
```

버퍼가 충분하면 부드럽고 정확하지만, 표시가 항상 과거에 머문다. interpDelay가 jitter보다 작으면 필요한 미래 표본이 없어 버퍼가 고갈된다.

## 3. 외삽

Extrapolation은 가장 최근 위치와 속도로 현재를 추정한다.

$$
\hat{\mathbf{x}}(t)=\mathbf{x}_{last}+\mathbf{v}_{last}(t-t_{last})
$$

직선 운동에서는 잘 맞지만 방향 전환과 충돌에서 크게 틀린다. 외삽 시간에 상한을 두고, 넘으면 멈추거나 마지막 상태를 유지해야 폭주를 막을 수 있다.

## 4. 오차와 손실 지표

흰색 참 위치, 파란색 보간, 초록색 외삽을 함께 그리고 각각의 평균 위치 오차를 누적한다. 손실 수와 재정렬 수도 표시한다.

오차 평균만으로 순간적인 큰 튐을 놓칠 수 있으므로 실제 구현에서는 최대·P95 오차도 함께 본다.

## 5. 검증 실험

1. jitter 0에서 interpDelay를 한 송신 간격 이상 두고 오차를 본다.
2. jitter를 키워 버퍼 고갈이 생기는 경계를 찾는다.
3. loss를 올려 보간과 외삽 오차를 비교한다.
4. send rate를 낮춰 snapshot 간격의 영향을 본다.
5. interpDelay를 키워 부드러움과 표시 지연을 교환한다.

이 단계로 `net_lab`은 넷코드의 핵심 거래를 한 화면에서 보여 준다. *즉시성, 정확성, 계산 비용, 대역폭 중 무엇을 어디에 지불하는가* 가 모든 Scene의 공통 질문이다.

---

## 결론

지연, 지터, 손실, 재정렬을 시드 기반 전달 큐로 모사하고 참 위치, 보간 위치, 외삽 위치의 오차를 동시에 측정한다. 송신 주기와 보간 지연을 바꾸며 부드러움이 무료가 아니라 표시 지연과 오차 예산을 소비한다는 사실을 확인할 수 있다.

현재 snapshot은 단순 운동 상태이며 delta baseline, 양자화, 신뢰 채널, 대역폭 제한을 포함하지 않는다. 평균 오차만으로 순간 튐을 판단할 수도 없으므로 실제 적용에는 상위 백분위와 버퍼 고갈 횟수가 필요하다.
