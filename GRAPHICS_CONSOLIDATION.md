# graphics 통합 기록

기준일: 2026-06-16. 목표는 렌더링 이론을 stage0가 한 번 깊게 소유하고, OpenGL·DirectX·Vulkan 문서는 API 고유의 객체·자원·동기화·제출 모델에 집중하게 하는 것이다.

## 현재 구조

- stage0 공통 핵심: 기초 9편 + 심화 4편
- OpenGL 적용 트랙: 10편
- DirectX 적용 트랙: 9편
- Vulkan 적용 트랙: 8편

API 중립 GPU 아키텍처 문서는 Vulkan 기초에서 `stage0_adv_01_gpu_architecture.md`로 이동했다.

## 개념 소유권

| 개념 | 소유 문서 |
|---|---|
| 파이프라인 단계 | `stage0_fund_01_pipeline_overview` |
| 좌표·변환·투영·NDC·법선 변환 | `stage0_fund_02_coordinate_transform` |
| 래스터화·보간 | `stage0_fund_03_rasterization` |
| 색·감마·HDR·톤매핑 | `stage0_fund_04_color_gamma` |
| 반사 모델·광원 모델·거리 감쇠·PBR·고급 재질 | `stage0_fund_05_shading_materials` |
| 깊이·가시성·컬링·AA | `stage0_fund_06_depth_visibility` |
| 텍스처링·필터링·밉맵 | `stage0_fund_07_texturing_sampling` |
| 프레임버퍼·멀티패스·G-buffer·그림자 골격 | `stage0_fund_08_framebuffer_multipass` |
| 메시·인덱스·접선 공간·LOD·테셀레이션·메시 셰이더 | `stage0_fund_09_geometry_mesh` |
| SIMT·메모리 계층·점유율·발산 | `stage0_adv_01_gpu_architecture` |
| 컴퓨트 실행 모델·동기화·병렬 패턴 | `stage0_adv_02_compute_gpgpu` |
| GI·IBL·BVH·레이트레이싱·디노이징 | `stage0_adv_03_global_illumination_raytracing` |
| GPU 자원 배치·상주·전송·동기화·스트리밍 | `stage0_adv_04_gpu_memory_system` |

## 완료된 통합

- PBR과 고급 재질을 하나의 셰이딩·머티리얼 장으로 통합
- GI와 레이트레이싱을 하나의 가시성·광전달 심화 장으로 통합
- 테셀레이션·메시 셰이더를 지오메트리 장으로 통합
- 애니메이션·스키닝은 엔진 애니메이션 장이 소유
- API별 문서에서 좌표·샘플링·PBR·그림자·톤매핑·컴퓨트 이론의 중복을 줄임
- API 중립 GPU 아키텍처를 Vulkan 트랙에서 stage0로 이동

## 남은 검수

- 짧은 stage0 장이 모델·불변식·실패 조건·검증 방법을 충분히 설명하는지 보강
- API별 문서가 개념을 재유도하지 않고 API 고유 계약에 집중하는지 지속 확인
- 적용 문서의 전제 문장이 존재하지 않는 후속 문서를 예고하지 않는지 확인
