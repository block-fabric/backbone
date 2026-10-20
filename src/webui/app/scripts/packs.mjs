// The pages of the sidechains, built into the Chains app (doc/unified-app.md §13, src/webui/sidechains.json): each
// sidechain's interface taken from its repository at the pinned commit, built as a pack for /packs/<stem>/ (its own
// scripts/pack.mjs), checked, and put into the interface of the app with the shim that routes its bridge here.
//   node scripts/packs.mjs --config <sidechains.json> --repos <dir> --out <dist>/packs --work <dir>
// Fails (and so the build) when a commit is missing, a pack speaks another hostApi, or was made with other packages of
// the interface kit than this app's (package.json): a change of the kit makes the packs be bumped together.
import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const APP = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const arg = (name) => {
  const at = args.indexOf(name);
  if (at < 0 || !args[at + 1]) throw new Error(`packs: ${name} <value> is needed`);
  return args[at + 1];
};
const config = JSON.parse(readFileSync(arg('--config'), 'utf8'));
const repos = resolve(arg('--repos'));
const out = resolve(arg('--out'));
const work = resolve(arg('--work'));

const own = JSON.parse(readFileSync(join(APP, 'package.json'), 'utf8'));
const kit = (pkg) => JSON.stringify({ dependencies: pkg.dependencies ?? {}, devDependencies: pkg.devDependencies ?? {} });

/** Turns the pack's page into one that loads the shim first (a classic script: before its module). */
export function withShim(html) {
  if (html.includes('/packs/shim.js')) return html;
  // Before the page's own script (after its policy and its charset), as a classic script: it runs first.
  const at = html.indexOf('<script');
  if (at < 0) throw new Error('packs: the page of a pack has no script');
  return `${html.slice(0, at)}<script src="/packs/shim.js"></script>\n    ${html.slice(at)}`;
}

function run(cmd, argv, cwd, input) {
  return execFileSync(cmd, argv, { cwd, input, stdio: input === undefined ? ['ignore', 'pipe', 'inherit'] : ['pipe', 'pipe', 'inherit'], maxBuffer: 512 * 1024 * 1024 });
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  rmSync(out, { recursive: true, force: true });
  mkdirSync(out, { recursive: true });
  const index = [];
  for (const p of config.packs) {
    if (!/^[a-z0-9]+$/.test(p.stem) || !/^[0-9a-f]{40}$/.test(p.commit)) throw new Error(`packs: ${JSON.stringify(p)}: a stem of plain letters and a full commit id are needed`);
    const repo = join(repos, p.repo);
    try {
      run('git', ['-C', repo, 'cat-file', '-e', `${p.commit}^{commit}`]);
    } catch {
      throw new Error(`packs: ${p.stem}: commit ${p.commit} is not in ${repo}`);
    }
    // The interface of the sidechain at the commit, alone (never the work tree, never a branch).
    const src = join(work, p.stem);
    rmSync(src, { recursive: true, force: true });
    mkdirSync(src, { recursive: true });
    const tar = run('git', ['-C', repo, 'archive', '--format=tar', p.commit, 'src/webui/app', 'src/webui/locale']);
    run('tar', ['-x', '-C', src], undefined, tar);
    const app = join(src, 'src/webui/app');
    const pkg = JSON.parse(readFileSync(join(app, 'package.json'), 'utf8'));
    if (kit(pkg) !== kit(own)) throw new Error(`packs: ${p.stem} at ${p.commit.slice(0, 10)} was made with other packages than this app's interface: bump it to a commit with this kit`);
    if (!existsSync(join(app, 'scripts/pack.mjs'))) throw new Error(`packs: ${p.stem} at ${p.commit.slice(0, 10)} cannot be built as a pack (no scripts/pack.mjs): bump it`);
    // The same packages: this app's, not downloaded again.
    symlinkSync(join(APP, 'node_modules'), join(app, 'node_modules'), 'dir');
    run(process.execPath, ['scripts/i18n.mjs', '--if-present'], app);
    const dest = join(out, p.stem);
    run(process.execPath, ['scripts/pack.mjs', '--outDir', dest, '--commit', p.commit], app);
    const pack = JSON.parse(readFileSync(join(dest, 'pack.json'), 'utf8'));
    if (pack.stem !== p.stem) throw new Error(`packs: ${p.repo} at ${p.commit.slice(0, 10)} is ${pack.stem}, not ${p.stem}`);
    if (pack.hostApi !== config.hostApi) throw new Error(`packs: ${p.stem} speaks hostApi ${pack.hostApi}, this app ${config.hostApi}: bump it`);
    writeFileSync(join(dest, 'index.html'), withShim(readFileSync(join(dest, 'index.html'), 'utf8')));
    index.push({ stem: pack.stem, name: pack.name, hostApi: pack.hostApi, commit: pack.commit, brand: pack.brand, coin: pack.coin });
    console.log(`packs: ${pack.name} at ${p.commit.slice(0, 10)}`);
  }
  copyFileSync(join(APP, 'public/packs/shim.js'), join(out, 'shim.js'));
  writeFileSync(join(out, 'index.json'), `${JSON.stringify(index)}\n`);
}
