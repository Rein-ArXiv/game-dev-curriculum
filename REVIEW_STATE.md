# 현재 검수 상태

## 기준 시점

- 최종 갱신: 2026-07-25
- 이론 본문: 진입 계층 20편 + 심화 문서 218편 = 238편
- Git 단편: 1편
- 프로젝트: 미니 lab 11개, 단일 mega 프로젝트 1개

세부 수정 이력과 과거 경로는 `REVIEW_LOG.md`가 소유한다. 이 문서는 현재 사실과 검증
결과만 기록한다.

## 커리큘럼 구조

- 진입 계층: `base/` 20편. 계산 비용과 수 표현에서 게임 도메인·엔진·운영까지
  전체 지형과 공통 질문을 제공하며 독립 개념 소유권은 갖지 않음
- 공통 핵심: algorithms, languages, parts, system, network, math, graphics stage0,
  AI 핵심 경로, engine
- 전문 선택: physics, multiplayer, DB, live service, graphics API별 장, AI 고급 경로
- 실무 참고: platform, tools, Git
- distributed, verification, audio, netcode의 독립 축은 해체하고 실제 소유 문서에 통합
- network는 6개의 깊은 장, DB는 5개의 깊은 장으로 통합
- 이론 문서는 후속 글과 lab을 예고하지 않고 현재 논증 안에서 자기완결

## 문서 검증

2026-07-25 실행 결과:

- `python scripts/check_style.py`
  - 검사 문서 232개
  - ERROR 0
  - WARN 0
- 외부 Reference 검사
  - 고유 URL 736개
  - DEAD 0
  - WARN 49
  - WARN은 403, 429, 일시적 연결 실패처럼 자동 판정할 수 없는 응답이며
    `scripts/link_report.txt`에 기록
- `base/`만 별도 검사
  - 본문 20편, 스타일 ERROR 0 / WARN 0
  - 고유 URL 24개, OK 24 / WARN 0 / DEAD 0
- 상대 Markdown 링크
  - 실제 깨진 링크 0
  - `REVIEW_LOG.md`의 리터럴 문구를 정규식이 링크로 오인한 1건만 제외

형식 검사는 기술적 정확성을 대신하지 않는다. 플랫폼·버전 의존 단정과 명세 인용은
`REVIEW_BACKLOG.md`의 지속 검수 대상으로 남긴다.

## 미니 프로젝트

모든 lab은 최종 `src/` 코드 하나와 `articles/step*.md` 과정 문서를 가진다. 단계별 소스
디렉터리는 없다. 검증 과정에서 생성되는 `build/`는 재생성 가능한 캐시이며 권위 있는
학습 단계나 소스 스냅샷으로 간주하지 않는다.

2026-07-25 Windows Release 검증:

- 기존 캐시와 분리한 빌드 루트에서 clean configure/build 통과:
  parts, languages, math, physics, AI, OpenGL, DirectX, Vulkan, network, DB lab
- CTest 통과: Windows 대상 10개 lab 모두
- 그래픽스 3개 lab: draw 제출 수, 자원 수명 정책, barrier·mip 계산의 headless
  계약 테스트 통과. GPU 렌더 캡처 회귀는 아직 없음
- physics lab: 적분기·임펄스 외에 AABB/PBD와 SAT·GJK/EPA 교차 검증 통과
- AI·network lab: 경계 입력, 손실 후 재전송, 재정렬, reconciliation 수렴 검증 통과
- system kernel lab: Linux 수명 규칙을 정적 감사해 POSIX AIO와 io_uring 오류 경로를
  수정했지만, 현재 환경에는 WSL 배포판이 없어 configure/build/CTest는 실행하지 못함

## Mega Defense

`projects/mega/`만 권위 있는 최종 코드다. 단계별 CMake 프로젝트와 다른 장르 데모는
제거했고, 구현 과정은 `projects/mega/docs/development.md`에만 기록한다.

구현 범위:

- 사람이 조작하는 타일 기반 타워 디펜스
- 4종 타워, 5종 적, 5개 웨이브, 강화·판매·타게팅, 승패·재시작
- 외부 ASCII 레벨·웨이브와 교체 가능한 PPM 스프라이트, 원자적 전체 폴백
- 제한 스크립트로 계산한 밸런스와 구문·실행·범위 실패의 전체 폴백
- 액션 입력, 이동 표적 물리 충돌, 권위 상태와 분리된 파티클
- 싱글플레이 전술 일시정지와 일시정지 중 건설·강화
- 한 실행 파일의 solo, host, join 모드
- 실제 비차단 IPv4 UDP, 유효한 Hello 뒤 피어 승인, 호스트 권위 명령
- sequence, resend, cumulative ACK, duplicate suppression
- 20Hz 제한 크기 full snapshot과 안정 ID 기반 클라이언트 보간
- WAL 진행도, 운영 지표, GDI/Vulkan 표시

2026-07-25 검증:

- MSVC Release clean configure/build 성공
- CTest 49/49
- 실제 localhost UDP 명령 → 호스트 검증 → ACK snapshot 통과
- 테스트가 독립 호스트·클라이언트 OS 프로세스를 기동해 실제 UDP 세션 통과
- 결정적 35% 손실·지연·재정렬에서 세 명령의 누적 ACK 수렴 통과
- 24회 반복 매치에서 유한 상태, 안정 ID, 엔티티 상한, 스냅샷 상한·복호화 통과
- 임의 선행 데이터그램이 호스트 피어를 점유하지 못하는 테스트 통과
- 외부 웨이브의 정상 로드와 부분 혼합 없는 전체 폴백 통과
- 밸런스 스크립트 정상 적용과 구문·범위 실패의 전체 폴백 통과
- 외부 스프라이트 10/10 로드
- Vulkan swapchain 5프레임 present 통과
- 실행 파일: `projects/mega/build/bin/Release/mega_defense.exe`

현재 네트워크는 로컬호스트·LAN용 1호스트+1원격 클라이언트 학습 제품이다. 다중
클라이언트, 재접속, 인증, NAT traversal, relay, delta snapshot, AOI를 구현한 것으로
간주하지 않는다.

## 완료 판정

커리큘럼 구조 통합, 20편의 진입 계층, 이론 형식 검증, 프로젝트 단일화, 재현 가능한
Windows 빌드, 실제 2프로세스 UDP 협동의 최소 제품 경계는 완료됐다. 남은 항목은
문서의 지속적인 명세 검증과 선택적 제품 확장이며, 현재 구현하지 않은 기능은 완료로
표시하지 않는다.
