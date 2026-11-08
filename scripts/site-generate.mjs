import { readFile, writeFile, mkdir, readdir, stat, rm } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.join(root, 'site/content');
const github = 'https://github.com/Rein-ArXiv/game-dev-curriculum/blob/master/';
export const subjects = [
  ['base', '기초 CS'], ['algorithms', '알고리즘'], ['languages', '언어와 런타임'],
  ['parts', '메모리와 C++'], ['system', '운영체제'], ['network', '네트워크'],
  ['math', '수학'], ['graphics', '그래픽스'], ['engine', '엔진'], ['ai', '게임 AI'],
  ['physics', '물리'], ['db', '데이터베이스'], ['live_service', '라이브 서비스'],
  ['platform', '플랫폼'], ['tools', '제작 도구'],
];
const files = ['README.md', 'git.md'];
for (const [dir] of subjects) {
  const names = await readdir(path.join(root, dir));
  files.push(...names.filter(n => /^(README|part\d[^/]*|stage\d[^/]*)\.md$/.test(n)).sort().map(n => `${dir}/${n}`));
}
// Only project introductions are published; source, assets, builds and audit reports stay out.
files.push('projects/mini/README.md');
for (const entry of await readdir(path.join(root, 'projects/mini'), { withFileTypes: true })) {
  if (entry.isDirectory() && entry.name.endsWith('_lab')) {
    const name = `projects/mini/${entry.name}/README.md`;
    if (await stat(path.join(root, name)).then(s => s.isFile()).catch(() => false)) files.push(name);
  }
}
if (await stat(path.join(root, 'projects/mega/README.md')).catch(() => false)) files.push('projects/mega/README.md');
const allowed = new Set(files);
const sources = new Map(await Promise.all(files.map(async f => [f, (await readFile(path.join(root, f), 'utf8')).replace(/^\uFEFF/, '').replace(/\r\n/g, '\n')])));
const titles = new Map();
function proseOnly(text) {
  let fence = null;
  return text.split('\n').map(line => {
    const marker = line.match(/^\s{0,3}(`{3,}|~{3,})/);
    if (marker) {
      if (!fence) fence = marker[1];
      else if (marker[1][0] === fence[0] && marker[1].length >= fence.length) fence = null;
      return '';
    }
    return fence ? '' : line;
  }).join('\n');
}
for (const [file, text] of sources) {
  const h1 = proseOnly(text).match(/^# (.+)$/m);
  if (h1) titles.set(file, h1[1]);
  // Most source READMEs already give chapter names; use those rather than filenames.
  for (const match of text.matchAll(/\[([^\]\n]+)\]\(([^)\s]+\.md)(?:#[^)]*)?\)/g)) {
    const target = path.posix.normalize(path.posix.join(path.posix.dirname(file), match[2]));
    const label = match[1].replace(/`/g, '');
    if (allowed.has(target) && !titles.has(target) && !/\.md$/.test(label)) titles.set(target, label);
  }
}
// base's public-order table has semantic titles without file links.
for (const match of sources.get('base/README.md').matchAll(/^\|\s*(\d+)\s*\|\s*([^|]+)\|/gm)) {
  const target = files.find(f => f.startsWith(`base/part${match[1].padStart(2, '0')}_`));
  if (target) titles.set(target, match[2].trim());
}
// Some series maps use filenames plus a question instead of Markdown links.
for (const [dir] of subjects) {
  const map = sources.get(`${dir}/README.md`) || '';
  for (const match of map.matchAll(/^\|[^\n]*?`((?:part|stage)[^`]+\.md)`\s*\|\s*([^|]+)\|/gm)) {
    const target = `${dir}/${match[1]}`;
    if (allowed.has(target) && !titles.has(target)) titles.set(target, match[2].trim().replace(/`/g, ''));
  }
}
for (const file of files) {
  if (!titles.has(file)) {
    const firstHeading = proseOnly(sources.get(file)).match(/^##? (?!들어가며|목차)(.+)$/m)?.[1];
    const stem = path.posix.basename(file, '.md').replace(/^(part\d+|stage\d+)_/, '').replace(/_/g, ' ');
    titles.set(file, firstHeading && !/^\d+[.)]/.test(firstHeading) ? firstHeading : stem);
  }
}
const namedTitles = {
  'git.md': 'Git과 버전 관리',
  'platform/part01_mobile.md': '모바일 플랫폼',
  'platform/part02_console.md': '콘솔 플랫폼',
  'platform/part03_pc.md': 'PC 플랫폼',
  'platform/part04_web.md': '웹 플랫폼',
  'platform/part05_crossplatform.md': '크로스 플랫폼',
  'platform/part06_cloud.md': '클라우드 게임',
  'platform/part07_windows_text.md': 'Windows 텍스트 경계',
  'tools/part01_leveleditor.md': '레벨 에디터',
  'tools/part03_procedural.md': '절차적 콘텐츠 생성',
  'tools/part04_debug.md': '디버깅과 관측',
  'tools/part05_asset.md': '에셋 파이프라인',
  'tools/part06_build.md': '빌드 시스템',
};
for (const [file, title] of Object.entries(namedTitles)) titles.set(file, title);
const assets = new Map();
const errors = [];
// Preserve four existing hand-written TOC spellings that differ from heading slugs.
// These are aliases beside the real heading, not edits to the reusable originals.
const anchorAliases = {
  'system/part15_ipc.md': ['3-named-pipefifo', '## 3. Named Pipe (FIFO)'],
  'system/part19_virtualization.md': ['6-중첩-가상화nested-virtualization', '## 6. 중첩 가상화 (Nested Virtualization)'],
  'math/part06_coordinate_systems.md': ['1-좌표계-위계-localworldviewclip', '## 1. 좌표계 위계: Local → World → View → Clip'],
  'graphics/stage1_gl_fund_04_texture.md': ['3-데이터-업로드-gltexImage2d와-gltexstorage2d', '## 3. 데이터 업로드: glTexImage2D와 glTexStorage2D'],
};
function rewriteUrl(url, from, image = false) {
  if (/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(url)) return url;
  const [pathname, suffix = ''] = url.split(/(?=[?#])/s, 2);
  let decoded;
  try { decoded = decodeURIComponent(pathname); } catch { errors.push(`${from}: malformed URL ${url}`); return url; }
  const target = path.posix.normalize(path.posix.join(path.posix.dirname(from), decoded));
  if (target.startsWith('../') || path.posix.isAbsolute(target)) { errors.push(`${from}: path escapes repository: ${url}`); return url; }
  if (allowed.has(target)) return path.posix.relative(path.posix.dirname(from), target) + suffix;
  if (image && /\.(png|jpe?g|svg|webp|gif)$/i.test(target)) {
    assets.set(target, path.join(root, target));
    return path.posix.relative(path.posix.dirname(from), target) + suffix;
  }
  // Nonpublished documentation and project code remain accessible at their source.
  return github + target.split('/').map(encodeURIComponent).join('/') + suffix;
}
// Remove only this generated directory, after resolving its workspace boundary.
if (out !== path.join(root, 'site', 'content')) throw new Error('Unexpected output directory');
await rm(out, { recursive: true, force: true });
await mkdir(out, { recursive: true });
const localTargets = [];
for (const [file, original] of sources) {
  let inFence = false;
  const text = original.split('\n').map(line => {
    if (/^\s*(`{3,}|~{3,})/.test(line)) inFence = !inFence;
    if (inFence) return line;
    return line.replace(/(!?)\[([^\]]*)\]\((<?)([^\s)>]+)>?(\s+"[^"]*")?\)/g,
      (all, bang, label, angle, url, title = '') => {
        if (!/^(?:[a-z][a-z\d+.-]*:|\/\/|#)/i.test(url)) localTargets.push([file, url]);
        return `${bang}[${label}](${rewriteUrl(url, file, !!bang)}${title})`;
      });
  }).join('\n');
  let body = text;
  if (anchorAliases[file]) {
    const [id, heading] = anchorAliases[file];
    if (!body.includes(heading)) throw new Error(`Anchor alias heading changed: ${file}`);
    body = body.replace(heading, `<a id="${id}"></a>\n\n${heading}`);
  }
  if (file === 'base/part01_cost_models.md') {
    const marker = '최적화 순서는 다음 질문으로 정리할 수 있다.';
    if (!body.includes(marker)) throw new Error('Amdahl insertion point changed');
    body = body.replace(marker, `<AmdahlLab />\n\n${marker}`);
  }
  const title = titles.get(file);
  const frontmatter = `---\ntitle: ${JSON.stringify(title)}\nsourcePath: ${JSON.stringify(file)}\neditLink: true\n---\n\n`;
  if (!/^# /m.test(proseOnly(body))) body = `# ${title}\n\n${body}`;
  const dest = path.join(out, file);
  await mkdir(path.dirname(dest), { recursive: true });
  await writeFile(dest, frontmatter + body);
}
for (const [from, url] of localTargets) {
  const raw = url.split(/[?#]/)[0];
  const target = path.resolve(root, path.posix.dirname(from), decodeURIComponent(raw));
  if (!await stat(target).catch(() => false)) errors.push(`${from}: missing target ${url}`);
}
for (const [relative, source] of assets) {
  const target = path.join(out, relative);
  await mkdir(path.dirname(target), { recursive: true });
  await writeFile(target, await readFile(source));
}
if (errors.length) throw new Error(`Content validation failed:\n${errors.join('\n')}`);
const route = file => `/content/${file.replace(/\.md$/, '')}`;
const groups = subjects.map(([dir, text]) => ({ text, collapsed: dir !== 'base', items: files.filter(f => f.startsWith(`${dir}/`)).map(f => ({ text: titles.get(f), link: route(f) })) }));
groups.push({ text: '실습 프로젝트', collapsed: true, items: files.filter(f => f.startsWith('projects/')).map(f => ({ text: titles.get(f), link: route(f) })) });
groups.push({ text: '참고', collapsed: true, items: [{ text: titles.get('git.md'), link: route('git.md') }] });
await writeFile(path.join(out, 'navigation.json'), JSON.stringify(groups, null, 2));
await writeFile(path.join(out, 'manifest.json'), JSON.stringify({ sources: files, assets: [...assets.keys()] }, null, 2));
console.log(`Generated educational content and navigation; verified ${localTargets.length} local source links. Originals unchanged.`);
