# 검수 백로그

## 원칙

문서 수 증가는 목표가 아니다. 기존 핵심 장에 흡수할 수 없고 독립적인 사고 모델을
전공서적 수준으로 설명할 수 있을 때만 새 글을 만든다. 프로젝트도 단계별 소스 복사본을
만들지 않고 최종 코드 하나와 과정 문서를 유지한다.

## 문서 정확성

- `base/`: 독립 심화 구현을 다시 축약하지 않고 상태·불변식·비용·실패를 연결하는
  진입 계층으로 유지. 제품 버전·가격·환경 없는 성능 수치를 새로 넣지 않음
- `parts/`, `system/`: CPU·ABI·커널·컴파일러 버전에 의존하는 보장과 경험적 성능 수치를
  정기적으로 원 명세와 재대조
- graphics API별 장: API가 보장하는 동작, validation이 검출하는 오류, 벤더별 성능
  휴리스틱을 분리
- `network/`, `db/`, `live_service/`: 확률·장애 모델의 가정과 운영 보장을 과장하지
  않는지 재검증
- 외부 Reference WARN: 봇 차단과 일시 오류를 수동 확인하고, 실제 이전된 문서만
  안정적인 원문 URL로 교체

자동 스타일 검사와 링크 생존 검사는 완료 조건의 일부일 뿐 기술적 정확성의 증명이 아니다.

## 미니 프로젝트

- graphics GL/DX/Vulkan lab: 상태 전이·자원 수명 계약 테스트는 완료. 실제 GPU 렌더
  캡처 회귀와 드라이버 validation 자동화는 미완료
- physics lab: broad phase, SAT, GJK/EPA, PBD 계약 테스트는 완료. 다수 접촉의
  장기 수렴·에너지 드리프트 계측은 선택적 확대 항목
- AI lab: flow field, steering, boids, influence map 경계 테스트는 완료. 고정 seed
  replay checksum은 선택적 확대 항목
- game network lab: Windows IOCP 실제 TCP·UDP process smoke와 protocol 경계 검증 완료.
  Linux epoll backend의 실제 빌드·실행은 Linux 환경에서 확인 필요
- live service lab: token·매칭·fleet·멱등 결과 core 및 HTTP smoke 완료. durable store와
  자동 fleet agent는 선택적 확대 항목
- system kernel lab: 실제 Linux/WSL2에서 configure, build, CTest, io_uring 지원·미지원
  경로 검증

## Mega Defense

현재 완료한 검증:

- 별도 호스트·클라이언트 OS 프로세스의 Hello → 실제 UDP 명령 → 권위 snapshot
- 전용 서버와 두 UDP 클라이언트의 HMAC token admission → 공유 권위 snapshot
- 결정적 loopback의 35% 손실·고정 지연·재정렬에서 게임 명령 누적 ACK 수렴
- 24회 반복 매치의 유한 상태, 엔티티 상한·ID 유일성, wire 상한·복호화
- WAL 재오픈과 잘린 미커밋 꼬리 복구
- 외부 맵·웨이브·스프라이트의 검증과 전체 폴백

선택적 제품 확장:

- 설정·접근성 UI와 더 풍부한 플레이 피드백
- 타워·적 수치까지 외부 데이터로 옮기는 콘텐츠 파이프라인
- WAL compaction과 파일 성장·디스크 고갈·쓰기 실패를 포함한 장기 운영 soak
- 2명을 넘는 다중 클라이언트, session/reconnect, delta baseline, AOI, rate limiting
- packet AEAD, NAT traversal, relay, 자동 fleet register·heartbeat·result agent

선택적 확장을 현재 학습 제품의 완료 기능처럼 문서화하지 않는다. 특히 런타임 DLC registry,
인터넷 공개 서비스, 대규모 서버를 구현하지 않은 상태에서 OCP나 “풀 스택 멀티플레이”라는
표현으로 대체하지 않는다.
