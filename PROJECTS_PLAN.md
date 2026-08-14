# 게임 개발자 CS 프로젝트 재설계 기준

이 문서는 `projects/`의 편집·구현 계획이다. 프로젝트의 목적은 이론을 장식하는 데모를 만드는 것이 아니라, 이론의 가정과 실패 조건을 코드와 측정으로 확인하고 게임이 실제로 요구하는 축을 하나의 플레이 가능한 제품으로 통합하는 것이다.

여기서 통합은 **모든 축을 한 실행 파일에서 호출한다는 뜻이 아니다.** 게임이 요구하지 않는 축을 이름만 부르려고 호출하면 측정 근거 없는 코드가 제품 경로에 들어간다. 어느 축이 제품에 들어갔고 무엇이 참조 구현으로 남았는지, 그리고 남은 이유는 `projects/mega/docs/AXIS_OWNERSHIP.md`가 소유하며, 제품에서 도달 불가인데 근거도 등재되지 않은 상태는 mega의 빌드가 실패시킨다.

## 프로젝트의 세 역할

| 경로 | 역할 | 완료 상태의 의미 |
|---|---|---|
| `projects/mini/` | 한 이론 축의 주장을 분리해 측정하는 실험실 | 계산 정확성 테스트와 재현 가능한 측정이 있다. |
| `projects/mega/` | 게임이 요구하는 축의 중복 없는 최종 소스와 실제 게임 | 사람이 플레이하고 두 프로세스가 실제 네트워크로 연결된다. |

개발 순서는 문서와 변경 이력으로 설명한다. 단계별 소스 복사본은 코드 드리프트와 검증
중복을 만들므로 보관하지 않으며, `projects/mega/`의 최종 코드만 권위 있는 원본으로 둔다.

## Mini lab 완료 기준

각 lab은 다음 추적 단위를 갖는다.

| 항목 | 질문 |
|---|---|
| 이론 주장 | 어떤 모델이나 불변식을 확인하는가 |
| 조작 변수 | 무엇을 켜고 끄거나 변경하는가 |
| 통제 변수 | 비교에서 동일하게 유지하는 조건은 무엇인가 |
| 관측값 | 시간, 메모리, 오차, 처리량 중 무엇을 측정하는가 |
| 기대 결과 | 어떤 범위와 추세가 나와야 하는가 |
| 실패 주입 | 어떤 입력이 잘못된 구현을 드러내는가 |
| 자동 테스트 | GUI 없이 어떤 정확성을 검증하는가 |

빌드 성공은 완료 조건이 아니다. 렌더링과 UI에서 계산 코어를 분리하고 최소한 다음 자동 검증을 둔다.

- `parts_perf_lab`: 할당 정렬·크기 클래스·해제, scalar/SIMD 허용 오차, 자료구조 결과 동등성
- `languages_lab`: 토큰 위치·우선순위, 바이트코드 제어 흐름·스택 검증, 실행 예산, 호스트 바인딩
- `system_kernel_lab`: I/O 결과 동등성, 부분 읽기·EINTR·취소, 동기화 불변식
- `math_lab`: 행렬·쿼터니언 항등식, 교차 판정, 퇴화 기하, 수치 오차
- `physics_lab`: 적분기 오차 차수, 운동량·에너지 범위, 접촉·마찰·제약 불변식
- `ai_lab`: 경로 비용, 도달 불가 상태, flow field, steering·boids 경계 조건
- graphics labs: 자원 수명과 동기화 상태, 변환 결과, 가능한 경우 렌더 캡처 회귀
- `game_network_lab`: 실제 TCP framing, IOCP/epoll 수명, UDP endpoint binding, 권위 tick과 snapshot
- `live_service_lab`: token audience·만료, 매치 할당, server drain, 멱등 결과와 payload 충돌
- `db_lab`: WAL 원자성·내구성, 절단 복구, 손상 프레임 이후 꼬리 폐기 (멱등성은 `live_service_lab`, 동시성은 `system_kernel_lab`·`game_network_lab`의 범위)

## Step 문서 완료 기준

1. step의 시작 상태와 종료 상태를 객관적으로 적는다.
2. 후속 step을 예고하지 않는다. — `scripts/check_project_docs.py`가 검사한다.
   자기 step 번호보다 큰 `Step N` 참조와 `이후 단계는`·`다음 스텝` 류 예고가 error다.
   지난 step을 되짚는 참조는 허용된다.
3. 현재 step에서 추가·변경한 코드가 식별된다.
4. 문서의 코드만 따라도 해당 상태를 빌드할 수 있다.
5. step 종료 시 실행 명령과 관측 가능한 결과가 있다.
6. 다음 step의 확장은 기존 공개 계약을 이유 없이 깨지 않는다.
7. 최종 코드와 문서 코드가 드리프트하지 않도록 스니펫 검증을 둔다. 단계별 소스 스냅숏은
   만들지 않는다. — `scripts/check_snippets.py`가 검사한다. 현재 소스와 일치한다고 주장하는
   코드 펜스 앞에 앵커를 달면 검사기가 실제 파일과 대조한다. 앵커 문법과 운용 규칙은
   `scripts/SNIPPETS.md`에 있다.

```powershell
python scripts/check_project_docs.py   # 2번 + 7번을 한 번에 검사한다
```

## Mega의 최종 게임

최종 목표는 2D 탑다운 협동 멀티플레이 디펜스다. 별도 장르 데모를 늘리지 않고 하나의
플레이 가능한 제품에서 엔진 경계와 네트워크 불변식을 검증한다.

### 플레이 가능한 게임의 최소 조건

- 타일맵과 명시적인 경로가 있다.
- 건설 가능, 경로, 장애물, spawn, core 셀이 구분된다.
- 같은 셀 중복 배치와 경로 위 배치를 거부한다.
- 타워 구매, 업그레이드, 판매, 타기팅 모드가 있다.
- 적과 웨이브가 데이터로 정의되고 난이도 곡선이 있다.
- 승리, 패배, 재시작과 명확한 상태 피드백이 있다.
- 실제 이미지와 레벨 에셋을 실행 파일 재컴파일 없이 교체할 수 있다.
- 입력, HUD, 피드백, 카메라가 사람이 반복 플레이할 수 있는 수준으로 연결된다.

### 콘텐츠 확장 경계

엄격한 OCP 구호보다 데이터와 명령의 경계를 먼저 만든다.

```text
assets + config
      |
      v
TowerDef / EnemyDef / WaveDef / LevelDef registries
      |
      v
validated GameCommand
      |
      v
authoritative deterministic simulation
      |
      v
render snapshot + audio events + network snapshot
```

- 레벨 파일이 tile, path, spawn, core, build rule을 소유한다.
- 웨이브 파일이 개수, 체력, 속도, 보상, 스폰 간격, 적 종류를 소유하며 행 하나라도
  잘못되면 기본 세트 전체로 폴백한다.
- 스프라이트 파일은 게임 규칙과 분리되어 같은 ID의 외형을 교체한다.
- simulation은 파일 경로나 GPU 객체를 알지 않는다.
- 상태 변경은 `PlaceTower`, `UpgradeTower`, `SellTower`, `SetTargetMode` 명령을 통과한다.
- 현재 타워 4종과 적 5종은 네트워크 wire format에 포함된 폐쇄 enum이다. 수치와 웨이브는
  각각 코드의 정의 테이블과 외부 웨이브 파일이 소유한다. 새 종류 추가는 호스트와
  클라이언트의 프로토콜 버전을 함께 올리는 변경이다. 런타임 DLC registry를 구현한 것처럼
  설명하지 않는다.

### 현재 완료한 멀티플레이 경계

Loopback 시뮬레이터는 프로토콜 테스트 백엔드로 유지하지만 실제 멀티플레이 완료 조건으로 세지 않는다.

- 실제 UDP socket을 사용하는 `ITransport` 구현
- 하나의 `mega_defense` 실행 파일을 `--host`와 `--join` 모드의 독립 프로세스로 실행
- 유효한 Hello를 검사한 뒤에만 호스트가 단일 원격 피어를 승인
- 서버 권위 tick과 클라이언트 입력 command
- 타워 배치·업그레이드·판매 명령의 reliable ordered 전달
- 적·발사체·웨이브 상태의 제한 크기 full snapshot 전달
- 안정 ID 기반 원격 객체 interpolation
- sequence·ack·resend·duplicate suppression
- 1200바이트 payload 상한과 위치·체력 quantization
- localhost 실제 소켓에서 클라이언트 명령→호스트 검증→ACK snapshot 통합 테스트
- 결정적 loopback에서 손실·고정 지연·재정렬 주입 테스트
- 독립 호스트·클라이언트 OS 프로세스의 Hello→명령→권위 snapshot 테스트
- 반복 매치에서 유한 상태, 안정 ID, 엔티티·wire 상한을 검사하는 soak

### 인터넷 서비스 확장 경계

Mega는 listen host 2인 세션과 token admission을 사용하는 2인 전용 서버를 지원한다. 재접속,
delta baseline, AOI/interest management, 악성 패킷 rate limiting, 패킷 AEAD, NAT traversal,
relay, 자동 fleet agent는 현재 제품 범위가 아니다. 구현된 실제 두 프로세스·세 끝점 연결과
권위 검증을 미구현 항목의 빈 interface로 대체하지 않는다.

별도 미니 프로젝트는 제품 경계와 다른 질문을 검증한다. `game_network_lab`은 Windows IOCP와
Linux epoll 위에 실제 headless server·client를 만들고, `live_service_lab`은 로그인·매치 큐·
server allocation·짧은 수명의 game token·멱등 result를 HTTP 제어 경로로 만든다. Mega 전용
서버는 같은 token 형식을 검증하며, process test는 token 두 개로 실제 UDP session과 snapshot을
연다. register·heartbeat·result HTTP 호출을 자동화하는 fleet agent는 배포 계층의 별도 경계다.

## 검증 단계

| 단계 | 검증 |
|---|---|
| 코어 | 단위 테스트, property test, sanitizer |
| 시뮬레이션 | 고정 seed replay, checksum, 장기 soak |
| 네트워크 | 두 프로세스 localhost, 손실·지연·재정렬 주입, ACK 수렴 |
| 렌더링 | 캡처 회귀, 자원 수명 validation, 프레임 계측 |
| 게임플레이 | 배치 규칙, 경제, 승패, 입력 흐름, 플레이 세션 |
| 통합 | clean configure/build/ctest와 실행 파일 smoke test |

README에는 구현된 기능, 시뮬레이션으로만 검증한 기능, 교체 지점만 있는 기능을 구분해 적는다. “풀 스택 멀티플레이” 같은 표현은 실제 socket 서버·클라이언트와 플레이 세션이 검증된 뒤에만 사용한다.
