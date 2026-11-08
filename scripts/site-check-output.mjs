import { readFile, readdir, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const dist = path.join(root, 'site/.vitepress/dist');
const base = '/game-dev-curriculum/';
async function htmlFiles(dir) {
  const files = [];
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    const filename = path.join(dir, entry.name);
    if (entry.isDirectory()) files.push(...await htmlFiles(filename));
    else if (entry.name.endsWith('.html')) files.push(filename);
  }
  return files;
}
const unescape = text => text.replace(/&amp;/g, '&').replace(/&quot;/g, '"').replace(/&#39;|&apos;/g, "'")
  .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(Number(n))).replace(/&#x([\da-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)));
const pages = new Map();
for (const filename of await htmlFiles(dist)) {
  const html = await readFile(filename, 'utf8');
  pages.set(filename, {
    ids: new Set([...html.matchAll(/\bid="([^"]*)"/g)].map(m => unescape(m[1]).normalize('NFC'))),
    links: [...html.matchAll(/<a\b[^>]*\bhref="([^"]*)"/g)].map(m => unescape(m[1])),
  });
}
const errors = [];
let checked = 0;
for (const [filename, page] of pages) {
  for (const href of page.links) {
    if (/^(?:[a-z][a-z\d+.-]*:|\/\/)/i.test(href) || !href.includes('#')) continue;
    const [pathname, hash] = href.split('#');
    if (!hash) continue;
    let target = filename;
    if (pathname) {
      const clean = decodeURIComponent(pathname.split('?')[0]);
      target = clean.startsWith(base) ? path.join(dist, clean.slice(base.length)) :
        clean.startsWith('/') ? path.join(dist, clean.slice(1)) : path.resolve(path.dirname(filename), clean);
      if (clean.endsWith('/')) target = path.join(target, 'index.html');
      if (!path.extname(target)) target += '.html';
    }
    checked++;
    const id = decodeURIComponent(hash).normalize('NFC');
    if (!pages.has(target) || !pages.get(target).ids.has(id)) {
      errors.push({ from: path.relative(dist, filename).replace(/\\/g, '/'), href, target: path.relative(dist, target).replace(/\\/g, '/') });
    }
  }
}
const cache = path.join(root, 'site/.vitepress/cache');
await mkdir(cache, { recursive: true });
await writeFile(path.join(cache, 'anchor-report.json'), JSON.stringify({ checked, errors }, null, 2));
if (errors.length) {
  console.error(`Broken rendered fragment links: ${errors.length}/${checked}`);
  console.error(errors.slice(0, 40));
  process.exitCode = 1;
} else console.log(`Verified ${checked} rendered internal fragment links across ${pages.size} HTML pages.`);
