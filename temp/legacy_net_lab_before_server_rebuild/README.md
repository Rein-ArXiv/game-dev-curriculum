# net_lab — multiplayer 시리즈의 원리 측정·시각화

## 컨셉

이 lab은 **멀티플레이어 넷코드의 트레이드오프를 인터랙티브하게 관찰·측정**하는 도구다. 인공 지연·지터·손실을 슬라이더로 주고, 롤백/예측/재시뮬, 결정론 디싱크, 매치메이킹 widening, 보간 vs 외삽이 *실제로 어떻게 갈라지는지*를 수치와 그림으로 본다. multiplayer 시리즈 글의 도식·의사 코드를 살아 있는 형태로 만든다.

## 짝 시리즈

- **multiplayer** — 롤백 넷코드, 매치메이킹, 리플레이, 음성, 안티치트, 대규모 확장을 다루는 시리즈. 이 lab은 그중 *실시간 동기화* 쪽 토픽(롤백·결정론·매칭·지연 보상)을 실행 가능하게 만든다.

## Scene와 토글

상단 탭으로 Scene을 전환한다. 각 Scene의 우측 패널에 raygui 슬라이더/콤보/버튼이 있다.

| Scene | 토글 / 슬라이더 | 측정·관찰 | 참조 |
|---|---|---|---|
| **Rollback** | Mode(Rollback/Delay), latency, jitter, input delay | 롤백/s, 평균 재시뮬 깊이, mispredict율 (Rollback) / 입력 지연 (Delay) | part01 |
| **Prediction** | Mode(예측+보정/예측없음), latency, jitter, server wall | 입력 지연(예측~0 vs RTT), pending 입력 수, 평균 보정량 | part07 |
| **Lockstep** | particles, 디싱크 주입, 일시정지 | 두 클라 상태 해시 일치/갈라짐, 첫 디싱크 tick | part01 |
| **Matchmaking** | spawn rate, widening slope, base window | queue size, matches/min, 평균 대기, 평균 MMR diff(품질) | part02 |
| **LagSim** | latency, jitter, loss%, send rate, interp delay | 보간/외삽 평균 오차, 손실·재정렬 수 | part01/04 |

## 조작

- **Rollback** — 화살표 키로 Local 플레이어 직접 조작(안 누르면 자동 패턴). Mode를 Rollback↔Delay로 바꿔 입력 지연 vs 롤백 비용을 비교.
- **Lockstep** — "디싱크 주입" 체크 → 한쪽 시뮬에 미세 오차 누적 → 해시 갈라짐 관찰. Reset으로 재동기화.
- **Matchmaking** — widening slope를 키우면 대기는 짧아지되 MMR diff(불공정)가 커지는 트레이드오프를 본다.
- **LagSim** — latency/jitter/loss를 키우고 보간(파랑)과 외삽(초록)이 원본(흰)에서 어떻게 어긋나는지 본다.

## 측정 항목

- Rollback: 초당 롤백 횟수, 평균 재시뮬 깊이, mispredict율, Delay 모드의 입력 지연(프레임/ms).
- Lockstep: 상태 해시(FNV-1a) 일치 여부, 첫 디싱크 tick.
- Matchmaking: 큐 크기, 분당 매치 수, 평균 대기 시간, 평균 MMR 차이(매치 품질).
- LagSim: 보간/외삽의 원본 대비 평균 위치 오차, 패킷 손실·재정렬 횟수.

## 기술 스택

- C++20.
- **raylib 5.0** — 윈도우/2D 드로잉/입력 (FetchContent로 자동 취득).
- **raygui 4.0** — 패널 컨트롤 (단일 헤더).

> 같은 mini 시리즈의 `math_lab` / `physics_lab` / `ai_lab` 과 동일한 raylib + raygui Scene 아키텍처.

## 디렉토리 구조

```text
net_lab/
  CMakeLists.txt
  src/
    main.cpp                  Scene 등록 + 메인 루프
    measure/frame_timer.*     ms/fps EMA 측정
    ui/
      scene_chrome.*          탭 바 / 푸터 / 레이아웃
      raygui_impl.cpp         raygui 단일 정의 TU
    scenes/
      iscene.h                Scene 인터페이스
      welcome_scene.*
      prediction_scene.*      클라이언트 예측·서버 보정
      rollback_scene.*        결정론 2인 시뮬 + 예측·롤백·재시뮬
      lockstep_scene.*        두 시뮬 해시 비교 + 디싱크 주입
      matchmaking_scene.*     MMR 큐 + Progressive Widening
      lagsim_scene.*          패킷 지연/손실 + 보간/외삽
```

## 빌드와 실행

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/Release/net_lab        # Windows (MSVC): build/Release/net_lab.exe
```

raylib·raygui는 CMake FetchContent로 자동으로 가져온다. 인터넷 연결과 C++20 컴파일러만 있으면 된다.

## 구현 범위 메모

- 시리즈의 *실시간 동기화* 토픽(롤백·결정론·매칭·지연 보상)을 다룬다. 리플레이(part03)·음성(part04)·안티치트(part05)·대규모 확장(part06)은 시각화보다 인프라/운영 토픽이라 이 lab의 첫 범위에서 제외하고, 시리즈 글에서 다룬다.
- Rollback Scene의 "원격 입력"은 결정론적 패턴으로 생성해, 예측 적중/실패와 롤백을 재현 가능하게 한다. 실제 P2P/서버 왕복은 인공 지연/지터 모델로 추상화했다.
- 이동 모델·수치는 *원리 시연*에 맞춘 단순화다. 실제 프로덕션 넷코드의 직렬화·보안·대역폭 세부는 시리즈 글을 참조.
