# 게임 AI 시리즈 지도

`ai/`는 25편 전체가 하나의 공통 필수 과목은 아니다. 모든 게임 개발자가 공유해야 할 핵심 모델과, AI 담당자나 특정 장르에서 깊게 들어가는 고급 선택 주제를 분리한다.

## 공통 핵심 경로

| 문서 | 이유 |
|---|---|
| `part01_fsm_hfsm.md` | 상태와 전이, 행동의 가장 기본적인 모델 |
| `part02_behavior_tree.md` | 상용 게임 AI의 대표적 합성 모델 |
| `part03_utility_ai.md` | 연속 점수 기반 의사결정과 튜닝 모델 |
| `part06_pathfinding_basics.md` | 그래프 최단 경로와 A*의 게임 적용 |
| `part08_navmesh.md` | 3D 이동 공간 표현의 표준 모델 |
| `part11_steering_behaviors.md` | 경로를 실제 움직임으로 바꾸는 로컬 제어 |
| `part13_perception_systems.md` | 시야·청각·기억 등 AI 입력 모델 |
| `part20_ai_debugging.md` | AI의 결정 과정을 관측·재현하는 방법 |
| `part23_ai_architecture_action_layer.md` | 의사결정과 행동 실행 계층의 분리 |
| `part24_blackboard_world_model.md` | 공유 월드 상태와 지식 표현 |

이 10편이 “모든 게임 개발자가 알아야 할 AI CS”에 해당한다. 목적은 AI 시스템을 직접 전부 구현하는 것이 아니라, 상태·탐색·지각·행동 실행·디버깅의 공통 언어를 갖는 것이다.

## 고급 선택 경로

### 계획과 질의

- `part04_goap.md`
- `part05_htn.md`
- `part25_environment_query_system.md`

목표와 전제조건, 작업 분해, 환경 질의가 중요한 전술 AI나 대규모 NPC 시스템에서 필요하다.

### 고급 이동과 군중

- `part07_advanced_pathfinding.md`
- `part10_flow_field.md`
- `part21_local_avoidance.md`
- `part12_flocking_boids.md`

대규모 유닛, 동적 장애물, 군중 이동, 로컬 회피가 게임플레이 핵심일 때 필요하다.

### 전술·연출 AI

- `part09_influence_map.md`
- `part14_squad_coordination.md`
- `part15_attack_slot_system.md`
- `part16_boss_directors.md`
- `part22_adversarial_search.md`

전투 전술, 보스 연출, 보드게임형 탐색, 다수 에이전트 조율이 필요한 장르의 선택 과목이다.

### 학습 기반 AI

- `part17_rl_intro.md`
- `part18_imitation_learning.md`
- `part19_neural_npc.md`

강화학습과 신경망 NPC는 일반 게임 AI의 기본 도구라기보다 연구·도구·특수 시스템에 가까운 선택 과목이다.

## 유지 기준

공통 핵심 문서는 상태 모델, 비용, 실패 조건, 디버깅 방법을 명확히 설명해야 한다. 고급 선택 문서는 공통 핵심을 반복하지 않고, 해당 기법이 왜 필요하고 어떤 전제에서 실패하는지에 집중한다.
