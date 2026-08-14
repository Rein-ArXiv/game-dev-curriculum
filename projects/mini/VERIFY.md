# Mini Lab 검증 현황

기준일: 2026-08-02

이 문서는 mini lab의 **빌드 가능 여부**와 **동작 정확성 검증 여부**를 구분한다. 실행 파일이 만들어지는 것과 알고리즘이 옳게 작동하는 것은 같은 완료 조건이 아니다.

## 반복 검증

Windows에서 다음 명령으로 Windows 대상 lab 11개를 configure, Release build, CTest 순서로 검사한다.

```powershell
pwsh -File scripts/check_projects.ps1
```

기존 CMake 캐시와 분리한 디렉터리에서 모든 프로젝트를 다시 구성하려면
`-BuildRoot`를 지정한다. 컴파일러나 CMake를 갱신한 뒤 캐시 호환성까지 배제해야
하는 검수에는 이 형태를 사용한다.

```powershell
pwsh -File scripts/check_projects.ps1 -BuildRoot "$env:TEMP/velog-project-check"
```

이미 configure된 build 디렉터리를 그대로 검사하려면 다음을 사용한다.

```powershell
pwsh -File scripts/check_projects.ps1 -SkipConfigure
```

`system_kernel_lab`은 Linux 전용이므로 이 스크립트에서 제외한다. WSL 배포판 또는 별도 Linux 환경에서 README의 빌드 절차를 실행해야 한다.

## 현재 결과

| Lab | Release build | 자동 테스트 | 단계 문서 |
|---|---|---|---:|
| `parts_perf_lab` | 통과 | CTest 1개 | 8 |
| `languages_lab` | 통과 | CTest 1개 | 6 |
| `math_lab` | 통과 | CTest 1개 | 8 |
| `physics_lab` | 통과 | CTest 2개 | 8 |
| `ai_lab` | 통과 | CTest 2개 | 6 |
| `graphics_gl_lab` | 통과 | CTest 1개 | 7 |
| `graphics_dx_lab` | 통과 | CTest 1개 | 7 |
| `graphics_vk_lab` | 통과 | CTest 1개 | 7 |
| `game_network_lab` | 통과 | protocol·oversized UDP 회복 process CTest 2개 | 6 |
| `live_service_lab` | 통과 | core contract 테스트. 실패 주입 보강 필요, HTTP smoke는 mega 행 | 5 |
| `db_lab` | 통과 | CTest 2개 통과 | 4 |
| `system_kernel_lab` | 현재 환경에서 미검증 | CTest 1개 정의, Linux 실행 미검증 | 8 |

전체 스크립트의 마지막 검사는 C# Live Service의 실제 game token 두 개를 C++ Mega 전용 서버가
검증하고 서로 다른 UDP 끝점 두 곳에 snapshot을 보내는 교차 언어 process smoke다.

## 남은 완료 조건

GUI lab은 장면을 직접 실행해 화면과 상호작용을 확인해야 한다. 현재 headless
CTest는 다음 핵심 계약을 검사한다.

- `parts_perf_lab`: allocator 정렬·해제와 simulation 모드 계약
- `math_lab`: 행렬·쿼터니언·교차 판정·수치 안정성
- `physics_lab`: 적분기 차수, 임펄스, AABB/PBD, SAT·GJK/EPA
- `ai_lab`: 경로 비용, flow field, 반사 경계, boid 속도, influence 결합
- `graphics_gl_lab`: draw 제출 수, 바인딩 비용, mip 저장량 계산
- `graphics_dx_lab`: draw 제출 수, descriptor 수명 계획, barrier 전이 기술
- `graphics_vk_lab`: draw 제출 수 공식, barrier stage/access mask, timeline 값 증가 (자원 수명 정책은 자동 검증 없음)
- `game_network_lab`: TCP fragmentation·coalescing, sequence wrap, oversized UDP 오류 뒤 IOCP
  receive 복구, server/client handshake와 snapshot
- `live_service_lab`: access token 위조·만료, match allocation, drain, 중복 result 경로의 server
  소유권, 완료된 match 뒤 재큐잉

GPU 렌더 캡처 회귀, GUI 입력 자동화, Linux 전용 system 랩의 실제 실행은 별도
환경 검증이 필요하다. 자동 테스트가 있더라도 화면 출력과 드라이버별 validation을
대체하지는 않는다.
