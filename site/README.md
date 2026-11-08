# 사이트 운영

기존 교육용 Markdown이 원본이다. `site/content/`는 `npm run site:generate`로 매번 생성하며 직접 편집하거나 커밋하지 않는다. 제목·탐색 메뉴·웹 전용 실험은 사이트 계층에서 덧붙여 VeLog 원고를 유지한다.

## 로컬 실행

저장소 루트에서 `npm ci` 후 `npm run site:dev`를 실행한다. 공개 전 검증은 `npm run site:build`, 생성 결과 확인은 `npm run site:preview`다. 결과는 `site/.vitepress/dist/`에 생긴다.

## 게시 범위

생성 스크립트는 과목별 루트의 README, part·stage 교육 문서, git 원고, 선택한 프로젝트 소개 README만 허용한다. 프로젝트 구현·게임 빌드·바이너리·임시 파일·검수/감사 보고서·ZIP은 웹 출력에 복사하지 않는다. 교육 내용에서 이를 참조하면 저장소 원문 링크로 연결한다. 링크된 원본이 실제로 없는 경우 생성을 실패시킨다.

원본 목록과 복사한 이미지 목록은 생성된 `site/content/manifest.json`에서 확인할 수 있다. 사이드바도 같은 원본 목록으로 생성한다. 과목을 추가하려면 `scripts/site-generate.mjs`의 허용 목록을 검토한다.

## GitHub Pages

배포 경로는 `/game-dev-curriculum/`다. 워크플로는 문서 생성·정적 빌드와 Pages 아티팩트 업로드를 수행한다. 실제 배포는 저장소 Actions 변수 `PAGES_ENABLED`가 `true`일 때만 실행한다. 웹 서버는 필요하지 않으며 인터랙션은 브라우저에서 실행한다. 기본 문서 빌드가 성공해도 원격 게시 성공을 뜻하지는 않는다.

저장소는 사용자 결정에 따라 공개 상태로 전환했고, Pages의 게시 원본은 GitHub Actions이며 저장소 Actions 변수 `PAGES_ENABLED=true`를 설정했다. 첫 소스 푸시 뒤 CI에서 생성·빌드·완성 HTML 링크 검사를 통과하면 `https://rein-arxiv.github.io/game-dev-curriculum/`에 배포한다. Pages 설정 완료와 실제 배포 성공은 구분하며 워크플로 결과로 확인한다.

## 수식과 도식

수식은 MathJax로 정적 변환한다. Mermaid 코드 블록은 원문을 유지한 채 웹 렌더러에서 전용 컴포넌트로 연결한다. Amdahl 실험도 원고를 수정하지 않고 기초 비용 모델 문서의 해당 절에 삽입한다.
