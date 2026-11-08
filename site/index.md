---
layout: home
hero:
  name: 게임 개발자의 CS
  text: 게임을 만드는 원리를 이해하다
  tagline: 상태와 비용, 불변식과 실패 조건을 연결하는 컴퓨터 과학 자습 커리큘럼
  actions:
    - theme: brand
      text: 기초 CS부터 시작
      link: /content/base/README
    - theme: alt
      text: 전체 학습 지도
      link: /content/README
features:
  - title: 공통 기반을 먼저
    details: 자료구조와 언어, 메모리와 운영체제에서 게임 개발에 반복해서 등장하는 사고 모델을 익힌다.
    link: /content/base/README
  - title: 원리를 직접 조작
    details: 최적화할 구간과 가속 비율을 바꾸며 국소 개선이 전체 프레임에 미치는 영향을 확인한다.
    link: /content/base/part01_cost_models#6-최적화-판단
  - title: 구현으로 검증
    details: 작은 실습에서 관측값과 실패 조건을 확인하고, 원문 코드와 구현 설명으로 이어 간다.
    link: /content/projects/mini/README
---

## 어떤 경로로 읽을까

처음이라면 [기초 CS](/content/base/README)에서 전체 지형을 살핀다. 이미 다뤄 본 분야라면 왼쪽 과목 목차에서 필요한 원리로 바로 들어간다.

공통 핵심을 익힌 뒤에는 물리·렌더링 API·데이터와 운영 같은 선택 트랙을 필요에 맞게 고른다. [커리큘럼 지도](/content/README)는 각 분야의 역할과 의존 관계를 설명한다.

## 읽고, 예측하고, 확인하기

설명에서 다루는 상태와 전제를 먼저 확인한다. 결과를 예측한 뒤 예제나 실험을 실행하고, 관측 결과가 달라지면 깨진 전제를 찾는다. [비용 모델과 Amdahl 실험](/content/base/part01_cost_models#6-최적화-판단)에서 시작할 수 있다.
