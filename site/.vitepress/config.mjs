import { defineConfig } from 'vitepress';
import { readFileSync } from 'node:fs';

const sidebar = JSON.parse(readFileSync(new URL('../content/navigation.json', import.meta.url), 'utf8'));
const escapeAttr = value => value.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
// Match the original GitHub/VeLog TOCs: keep Korean NFC and numeric prefixes.
const sourceSlug = text => text.normalize('NFC').trim().toLowerCase()
  .replace(/[^\p{L}\p{N}_\-\s]/gu, '').replace(/\s/g, '-');

export default defineConfig({
  title: '게임 개발자의 CS',
  description: '게임의 상태와 비용, 실패를 이해하는 컴퓨터 과학 자습 커리큘럼',
  lang: 'ko-KR',
  base: '/game-dev-curriculum/',
  srcExclude: ['README.md'],
  cleanUrls: false,
  lastUpdated: false,
  markdown: {
    math: true,
    anchor: { slugify: sourceSlug },
    headers: { slugify: sourceSlug },
    toc: { slugify: sourceSlug },
    config(md) {
      const fence = md.renderer.rules.fence;
      md.renderer.rules.fence = (tokens, idx, options, env, self) => {
        if (tokens[idx].info.trim() === 'mermaid') {
          return `<MermaidDiagram :source="${escapeAttr(JSON.stringify(tokens[idx].content))}" />\n`;
        }
        return fence(tokens, idx, options, env, self);
      };
    },
  },
  themeConfig: {
    logo: undefined,
    siteTitle: '게임 개발자의 CS',
    nav: [
      { text: '학습 시작', link: '/content/base/README' },
      { text: '커리큘럼', link: '/content/README' },
      { text: '실습', link: '/content/projects/mini/README' },
    ],
    sidebar,
    search: { provider: 'local', options: {
      // Keep code searchable as plain text, without running Shiki a second time.
      _render(src, env, md) {
        const prose = src
          .replace(/^(`{3,}|~{3,})[^\n]*\n([\s\S]*?)^\1[ \t]*$/gm,
            (_, fence, code) => `<pre><code>${escapeAttr([...new Set(code.match(/[\p{L}_][\p{L}\p{N}_]{2,}/gu) || [])].join(' '))}</code></pre>`)
          .replace(/\$\$[\s\S]*?\$\$/g, '')
          .replace(/\$[^$\n]+\$/g, '');
        return md.render(prose, env);
      },
      miniSearch: {
        searchOptions: { fuzzy: false, prefix: true },
        // Index H2 sections together with their subheadings to keep download and
        // browser parsing bounded while retaining chapter/section destinations.
        *_splitIntoSections(file, html) {
          const headings = [...html.matchAll(/<h([12])\b[^>]*\bid="([^"]*)"[^>]*>([\s\S]*?)<\/h\1>/g)];
          let chapter = '';
          const textOnly = value => value.replace(/<[^>]*>/g, ' ').replace(/&[a-z]+;|&#\d+;/gi, ' ').replace(/\s+/g, ' ').trim();
          for (let i = 0; i < headings.length; i++) {
            const heading = headings[i];
            const title = textOnly(heading[3]);
            if (heading[1] === '1') chapter = title;
            const text = textOnly(html.slice(heading.index + heading[0].length, headings[i + 1]?.index));
            yield { anchor: heading[2], titles: heading[1] === '1' ? [title] : [chapter, title].filter(Boolean), text };
          }
        },
      },
      locales: { root: { translations: { button: { buttonText: '검색', buttonAriaLabel: '문서 검색' }, modal: { noResultsText: '검색 결과가 없습니다', resetButtonTitle: '검색어 지우기', footer: { selectText: '선택', navigateText: '이동', closeText: '닫기' } } } } } } },
    outline: { label: '이 글에서', level: [2, 3] },
    docFooter: { prev: '이전 글', next: '다음 글' },
    sidebarMenuLabel: '학습 목차',
    returnToTopLabel: '맨 위로',
    darkModeSwitchLabel: '화면 테마',
    socialLinks: [{ icon: 'github', link: 'https://github.com/Rein-ArXiv/game-dev-curriculum' }],
    editLink: { pattern: ({ frontmatter }) => `https://github.com/Rein-ArXiv/game-dev-curriculum/edit/master/${frontmatter.sourcePath || 'site/index.md'}`, text: '원문에서 수정 제안' },
  },
});
