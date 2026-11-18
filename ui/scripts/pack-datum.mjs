// Packs DATUM from a local checkout into ui/vendor/datum/, which git ignores.
//
//   node scripts/pack-datum.mjs [path-to-datum-checkout]
//
// DATUM is private and stays out of this repository: its tarballs are never
// committed, published or fetched in CI. The checkout defaults to ../datum-ui
// beside this repository, or DATUM_DIR. The tarballs get names without a
// version, so package.json does not change when DATUM does; manifest.json
// records the DATUM commit they came from, and whether its tree was dirty.

import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ui = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const datum = resolve(process.argv[2] ?? process.env.DATUM_DIR ?? resolve(ui, '../../datum-ui'));
const out = resolve(ui, 'vendor/datum');
const NAMES = ['tokens', 'quantity', 'css', 'charts', 'fonts', 'react'];

// On Windows pnpm is a .cmd script, which runs only through the shell; the
// shell joins arguments without quoting them, so a path with a space is quoted here.
const windows = process.platform === 'win32';
const quote = (arg) => (windows && /[\s"]/.test(arg) ? `"${arg.replaceAll('"', '\\"')}"` : arg);

function run(command, args, cwd) {
  const result = spawnSync(command, args.map(quote), { cwd, encoding: 'utf8', shell: windows });
  if (result.status !== 0) {
    console.error(`${command} ${args.join(' ')} failed in ${cwd}\n${result.stderr || result.stdout}`);
    process.exit(1);
  }
  return result.stdout.trim();
}

if (!existsSync(resolve(datum, 'packages/react/package.json'))) {
  console.error(`No DATUM checkout at ${datum}. Pass its path, or set DATUM_DIR.`);
  process.exit(1);
}

const commit = run('git', ['rev-parse', 'HEAD'], datum);
// Builds pkg/ (not committed in DATUM) for each package.
run('node', ['scripts/build-packages.mjs'], datum);
const dirty = run('git', ['status', '--porcelain'], datum) !== '';

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
const packages = NAMES.map((name) => {
  const dir = resolve(datum, 'packages', name);
  run('pnpm', ['pack', '--pack-destination', out], dir);
  const packed = readdirSync(out).find((file) => file.startsWith(`datum-ui-${name}-`));
  const file = `datum-ui-${name}.tgz`;
  renameSync(resolve(out, packed), resolve(out, file));
  const sha256 = createHash('sha256').update(readFileSync(resolve(out, file))).digest('hex');
  return { name: `@datum-ui/${name}`, packed, file, sha256 };
});
const version = JSON.parse(readFileSync(resolve(datum, 'package.json'), 'utf8')).version;
writeFileSync(resolve(out, 'manifest.json'), `${JSON.stringify({ version, commit, dirty, packages }, null, 2)}\n`);
console.log(`Packed DATUM ${version} at ${commit.slice(0, 7)}${dirty ? ' (dirty)' : ''} into ${out}`);
if (dirty) console.warn('DATUM\'s tree is dirty, so these tarballs are not exactly that commit.');
