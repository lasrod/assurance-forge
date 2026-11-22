// Verifies the real application, with DATUM, and writes a compact report: what
// was checked, against what, and what it showed. Public CI cannot do this,
// because DATUM never enters it; the report is what goes in a pull request.
//
//   pnpm --dir ui verify [--quick] [--compare <earlier report folder>] [--out <folder>] [--trace]
//
//   --quick     skip the browser tests in three browsers (the captures still run)
//   --compare   put this run's images beside an earlier report's, in compare.html
//   --out       where the report goes; default ui/verification/<commit>[-dirty]
//   --trace     record a Playwright trace of every capture, not only of failures
//
// Steps: the WebAssembly core is built again from the working tree (a stale
// one would verify old code), then `pnpm check`, `pnpm e2e`, the captures of
// the application (e2e/capture), and af-layout's gallery with DATUM's
// measured faces. The report is report.md, report.json and index.html.

import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import { basename, dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ui = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const root = resolve(ui, '..');
const args = process.argv.slice(2);
const flag = (name) => args.includes(name);
const option = (name) => {
  const at = args.indexOf(name);
  return at >= 0 ? args[at + 1] : undefined;
};
const windows = process.platform === 'win32';

function run(command, commandArgs, { cwd = ui, env = {}, log } = {}) {
  const started = Date.now();
  const result = spawnSync(command, commandArgs, {
    cwd,
    encoding: 'utf8',
    shell: windows,
    env: { ...process.env, ...env, FORCE_COLOR: '0' },
    maxBuffer: 256 * 1024 * 1024,
  });
  const output = `${result.stdout ?? ''}${result.stderr ?? ''}`;
  if (log) writeFileSync(log, output);
  return { ok: result.status === 0, status: result.status, output, seconds: Math.round((Date.now() - started) / 100) / 10 };
}

const text = (command, commandArgs, cwd = root) => run(command, commandArgs, { cwd }).output.trim();

// ===== Where the report goes =====

const commit = text('git', ['rev-parse', 'HEAD']);
const changed = text('git', ['status', '--porcelain']).split('\n').filter(Boolean);
const out = resolve(option('--out') ?? join(ui, 'verification', `${commit.slice(0, 8)}${changed.length ? '-dirty' : ''}`));
rmSync(out, { recursive: true, force: true });
mkdirSync(join(out, 'logs'), { recursive: true });

// ===== What is verified, and on what =====

function datum() {
  const vendor = join(ui, 'vendor', 'datum');
  const manifestPath = join(vendor, 'manifest.json');
  if (!existsSync(manifestPath)) return { present: false };
  const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  const recorded = JSON.parse(readFileSync(join(ui, 'datum.json'), 'utf8'));
  const lock = readFileSync(join(ui, 'pnpm-lock.yaml'), 'utf8');
  const packages = manifest.packages.map((entry) => {
    const bytes = readFileSync(join(vendor, entry.file));
    const sha256 = createHash('sha256').update(bytes).digest('hex');
    const integrity = `sha512-${createHash('sha512').update(bytes).digest('base64')}`;
    const locked = new RegExp(`'${entry.name.replace('/', '\\/')}@file:vendor/datum/${entry.file}':\\s*\\n\\s*resolution: \\{integrity: ([^,]+),`).exec(lock)?.[1];
    let installed = null;
    try {
      installed = JSON.parse(readFileSync(join(ui, 'node_modules', ...entry.name.split('/'), 'package.json'), 'utf8')).version;
    } catch {
      installed = null;
    }
    return {
      name: entry.name,
      version: installed,
      tarballMatchesManifest: sha256 === entry.sha256,
      tarballMatchesLockfile: locked === integrity,
    };
  });
  return {
    present: true,
    version: manifest.version,
    commit: manifest.commit,
    dirty: manifest.dirty,
    recordedCommit: recorded.commit,
    sameCommitAsRecorded: manifest.commit === recorded.commit,
    packages,
  };
}

async function browsers() {
  try {
    const { chromium, firefox, webkit } = await import('@playwright/test');
    const versions = {};
    for (const [name, type] of Object.entries({ chromium, firefox, webkit })) {
      try {
        const browser = await type.launch();
        versions[name] = browser.version();
        await browser.close();
      } catch {
        versions[name] = 'not installed';
      }
    }
    return versions;
  } catch {
    return {};
  }
}

const environment = {
  date: new Date().toISOString(),
  assuranceForge: {
    commit,
    branch: text('git', ['rev-parse', '--abbrev-ref', 'HEAD']),
    workingTree: changed.length ? `${changed.length} changed or untracked path(s)` : 'clean',
    changed,
  },
  datum: datum(),
  os: `${os.type()} ${os.release()} ${os.arch()}`,
  node: process.version,
  pnpm: text('pnpm', ['--version'], ui),
  rustc: text('rustc', ['--version']),
  browsers: await browsers(),
};

// ===== The checks =====

const checks = [];
function check(name, command, commandArgs, options = {}) {
  process.stdout.write(`${name} ... `);
  const log = join(out, 'logs', `${name.replace(/[^\w]+/g, '-')}.log`);
  const result = run(command, commandArgs, { ...options, log });
  const summary = options.summary?.(result.output) ?? '';
  checks.push({ name, command: `${command} ${commandArgs.join(' ')}`, ok: result.ok, seconds: result.seconds, summary, log: relative(out, log) });
  console.log(`${result.ok ? 'passed' : 'FAILED'} (${result.seconds} s)${summary ? ` ${summary}` : ''}`);
  return result.ok;
}
const lastLine = (pattern) => (output) => output.split('\n').filter((line) => pattern.test(line)).map((line) => line.trim()).join('; ');

const haveDatum = environment.datum.present && existsSync(join(ui, 'node_modules', '@datum-ui', 'react'));
if (haveDatum) {
  check('WebAssembly core', 'pnpm', ['wasm']);
  check('Interface: types, lint, unit tests', 'pnpm', ['check'], { summary: lastLine(/Tests\s+\d+/) });
  if (!flag('--quick')) {
    check('Browser tests (Chromium, Firefox, WebKit)', 'pnpm', ['e2e'], { summary: lastLine(/^\s*\d+ (passed|failed|skipped|flaky)/) });
  } else {
    check('Production build for the browser', 'pnpm', ['build:e2e']);
  }
  check('Captures of the application', 'pnpm', ['exec', 'playwright', 'test', '-c', 'playwright.capture.config.ts', ...(flag('--trace') ? ['--trace', 'on'] : [])], {
    env: { AF_CAPTURE_DIR: join(out, 'screens') },
    summary: lastLine(/^\s*\d+ (passed|failed)/),
  });
}
const faces = join(ui, 'node_modules', '@datum-ui', 'fonts', 'dist', 'measured');
check('Layout gallery', 'cargo', ['run', '--release', '-q', '-p', 'af-layout', '--example', 'gallery', '--', join(out, 'gallery'), ...(existsSync(faces) ? ['--fonts', faces] : [])], {
  cwd: root,
  summary: lastLine(/invalid geometry/),
});
if (existsSync(join(out, 'gallery'))) check('Gallery images', 'node', ['scripts/gallery-png.mjs', join(out, 'gallery')]);

// Traces of anything that failed in a browser.
const traces = [];
for (const folder of [join(ui, 'test-results')]) {
  if (!existsSync(folder)) continue;
  for (const entry of readdirSync(folder, { recursive: true })) {
    if (String(entry).endsWith('trace.zip')) {
      const target = join(out, 'traces', String(entry).replace(/[\\/]/g, '__'));
      mkdirSync(dirname(target), { recursive: true });
      copyFileSync(join(folder, String(entry)), target);
      traces.push(relative(out, target));
    }
  }
}

// ===== The report =====

const read = (path, fallback) => (existsSync(path) ? JSON.parse(readFileSync(path, 'utf8')) : fallback);
const captures = read(join(out, 'screens', 'captures-chromium.json'), {});
const timing = read(join(out, 'screens', 'timing.json'), null);
const gallery = read(join(out, 'gallery', 'gallery.json'), []);

const limitations = [];
if (!haveDatum) limitations.push('DATUM is not installed (`pnpm --dir ui datum` and `pnpm --dir ui install`), so nothing of the interface was verified: only the layout gallery ran.');
if (haveDatum && !environment.datum.sameCommitAsRecorded) limitations.push(`DATUM was packed from ${environment.datum.commit.slice(0, 7)}, but ui/datum.json records ${environment.datum.recordedCommit.slice(0, 7)}.`);
if (haveDatum && environment.datum.dirty) limitations.push('DATUM was packed from a tree with uncommitted changes.');
if (haveDatum && environment.datum.packages.some((entry) => !entry.tarballMatchesLockfile)) {
  limitations.push(`The DATUM tarballs differ byte for byte from those pnpm-lock.yaml was made with (${environment.datum.packages.filter((entry) => !entry.tarballMatchesLockfile).map((entry) => entry.name).join(', ')}): packing the same commit again does not give the same bytes, so the lockfile's integrity cannot be met. What was tested is the tarball packed here.`);
}
if (flag('--quick')) limitations.push('--quick: the browser tests in three browsers did not run.');
limitations.push('The desktop application (Tauri) is not driven by these checks; the browser build stands in for it. CI builds it on Windows and opens a sample over a placeholder page.');

const report = { environment, checks, gallery, captures, timing, traces, limitations };
writeFileSync(join(out, 'report.json'), `${JSON.stringify(report, null, 2)}\n`);

const ok = checks.every((entry) => entry.ok);
const md = [];
md.push(`# Verification ${ok ? 'passed' : 'FAILED'}`, '');
md.push(`${environment.date} · Assurance Forge \`${commit.slice(0, 8)}\` on \`${environment.assuranceForge.branch}\`, working tree ${environment.assuranceForge.workingTree}`, '');
md.push('## Environment', '', '| | |', '|---|---|');
if (environment.datum.present) {
  md.push(`| DATUM | ${environment.datum.version} at \`${environment.datum.commit.slice(0, 8)}\`${environment.datum.dirty ? ' (dirty)' : ''}; ui/datum.json records \`${environment.datum.recordedCommit.slice(0, 8)}\` |`);
  md.push(`| DATUM packages | ${environment.datum.packages.map((entry) => `${entry.name} ${entry.version ?? 'not installed'}${entry.tarballMatchesManifest ? '' : ' (tarball ≠ manifest)'}${entry.tarballMatchesLockfile ? '' : ' (tarball ≠ lockfile)'}`).join('<br>')} |`);
} else md.push('| DATUM | not present |');
md.push(`| System | ${environment.os}, Node ${environment.node}, pnpm ${environment.pnpm}, ${environment.rustc} |`);
md.push(`| Browsers | ${Object.entries(environment.browsers).map(([name, version]) => `${name} ${version}`).join(', ')} |`, '');
md.push('## Checks', '', '| Check | Result | Time | Detail |', '|---|---|--:|---|');
for (const entry of checks) md.push(`| ${entry.name} | ${entry.ok ? 'passed' : '**failed**'} | ${entry.seconds} s | ${entry.summary} [log](${entry.log.replace(/\\/g, '/')}) |`);
md.push('');
if (gallery.length) {
  md.push('## Layout gallery', '', "Measured by `af_layout::check` on the layout made with DATUM's measured faces. A line through a node, an overlap or text outside its shape is a defect; edge crossings are counted, not refused.", '');
  md.push('| Fixture | Nodes | Edges | Overlaps | Lines through nodes | Text outside | Crossings | Unplaced edges | Size (px) | Layout (ms) |', '|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|');
  for (const entry of gallery) {
    const name = entry.fixture.replace(/\.sacm\.xmi$/, '');
    md.push(`| [${name}](gallery/${name}.png) | ${entry.nodes} | ${entry.edges} | ${entry.nodeOverlaps} | ${entry.edgeNodeIntersections.length} | ${entry.textEscapes} | ${entry.edgeCrossings} | ${entry.unplacedEdges} | ${entry.width} × ${entry.height} | ${entry.layoutColdMs} |`);
  }
  md.push('');
}
if (timing) {
  md.push('## Responsiveness in Chromium', '', '| Measure | Value |', '|---|--:|');
  for (const [name, value] of Object.entries(timing)) {
    if (value.openToFirstPaintMs !== undefined) md.push(`| ${name}: open to first paint, median of 3 | ${value.openToFirstPaintMs} ms |`);
    else md.push(`| ${name}: paint interval, mean (worst) | ${value.meanMs} ms (${value.worstMs} ms) at a display interval of ${value.displayMs} ms |`);
  }
  md.push('');
}
if (Object.keys(captures).length) {
  md.push('## Screenshots', '', '| Image | Shows | Window | Zoom |', '|---|---|---|--:|');
  for (const [name, entry] of Object.entries(captures)) md.push(`| [${name}](screens/${entry.file}) | ${entry.what} | ${entry.window?.width} × ${entry.window?.height} | ${entry.zoom ?? ''}% |`);
  md.push('');
}
if (traces.length) md.push('## Traces', '', ...traces.map((trace) => `- [${basename(trace)}](${trace.replace(/\\/g, '/')}) (open with \`pnpm exec playwright show-trace\`)`), '');
md.push('## Limitations', '', ...limitations.map((line) => `- ${line}`), '');
writeFileSync(join(out, 'report.md'), md.join('\n'));

// index.html: the images, and beside them an earlier report's when --compare names one.
const earlier = option('--compare') ? resolve(option('--compare')) : null;
if (earlier && existsSync(earlier)) cpSync(earlier, join(out, 'before'), { recursive: true });
const images = [
  ...Object.values(captures).map((entry) => `screens/${entry.file}`),
  ...gallery.flatMap((entry) => {
    const name = entry.fixture.replace(/\.sacm\.xmi$/, '');
    return [`gallery/${name}.png`, `gallery/${name}-actual.png`];
  }),
].filter((path) => existsSync(join(out, path)));
const rows = images
  .map((path) => {
    const before = earlier && existsSync(join(out, 'before', path)) ? `<figure><figcaption>before</figcaption><img src="before/${path}"></figure>` : '';
    return `<section><h2>${path}</h2><div class="pair">${before}<figure><figcaption>${before ? 'after' : ''}</figcaption><img src="${path}"></figure></div></section>`;
  })
  .join('\n');
writeFileSync(
  join(out, 'index.html'),
  `<!doctype html><meta charset="utf-8"><title>Verification ${commit.slice(0, 8)}</title>
<style>body{font:14px system-ui;margin:16px;background:#fafaf7}h2{font-size:14px;margin:24px 0 4px}.pair{display:flex;gap:12px;align-items:flex-start}figure{margin:0;flex:1;min-width:0}img{max-width:100%;border:1px solid #ccc;background:#fff}figcaption{color:#666}</style>
<h1>Verification ${ok ? 'passed' : 'FAILED'}: ${commit.slice(0, 8)}${changed.length ? ' (working tree changed)' : ''}</h1>
<p><a href="report.md">report.md</a>${earlier ? ` · compared with ${earlier}` : ''}</p>
${rows}`,
);

console.log(`\nReport: ${join(out, 'report.md')}\nImages: ${join(out, 'index.html')}`);
process.exit(ok ? 0 : 1);
