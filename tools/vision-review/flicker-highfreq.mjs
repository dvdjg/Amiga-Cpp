#!/usr/bin/env node
// Flicker de alta frecuencia: captura una secuencia **frame a frame** (no cada
// 250 ms) y aplica el detector determinista de bandas alternas + frame-diff.
// El objetivo es que una alternancia de un frame de cada dos no se pierda por
// submuestreo; no sustituye una captura dentro del barrido de pantalla, que
// requiere instrumentación de scanout.
//
// Uso:
//   node tools/vision-review/flicker-highfreq.mjs --demo <ruta> [--frames 12]
//        [--config A500_release] [--no-run] [--require-ok]
import * as fs from 'node:fs';
import * as path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';

const ROOT = process.cwd();
const arg = (name, fallback = null) => {
  const at = process.argv.indexOf(name);
  return at >= 0 && at + 1 < process.argv.length ? process.argv[at + 1] : fallback;
};
const has = (name) => process.argv.includes(name);

const demoArg = arg('--demo');
if (!demoArg) {
  console.error('Uso: node tools/vision-review/flicker-highfreq.mjs --demo <ruta> [--frames 12] [--config A500_release] [--no-run] [--require-ok]');
  process.exit(2);
}
const demo = demoArg.replace(/\\/g, '/');
const demoId = path.basename(demo);
const frames = Math.max(6, parseInt(arg('--frames', '12'), 10));
const config = arg('--config', 'A500_release');

if (!has('--no-run')) {
  const bash = process.env.BASH || (process.platform === 'win32'
    ? 'C:\\Program Files\\Git\\bin\\bash.exe'
    : 'bash');
  const run = spawnSync(bash, [
    'tools/run/run-demo.sh', demo, '--config', config,
    '--sequence-step-frames', String(frames),
  ], { cwd: ROOT, stdio: 'inherit' });
  if (run.status !== 0) {
    console.error(`[highfreq] run-demo falló (${run.status ?? 'signal'})`);
    process.exit(run.status ?? 1);
  }
}

const runBase = path.join(ROOT, 'out/run', demoId);
const candidates = fs.existsSync(runBase)
  ? fs.readdirSync(runBase, { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => ({ dir: path.join(runBase, e.name, 'sequence') }))
    .filter((e) => fs.existsSync(e.dir))
    .map((e) => ({ ...e, mtime: fs.statSync(e.dir).mtimeMs }))
    .sort((a, b) => b.mtime - a.mtime)
  : [];
if (candidates.length === 0) {
  console.error(`[highfreq] ${demo}: no hay secuencia en out/run/${demoId}/*/sequence`);
  process.exit(3);
}
const seqDir = candidates[0].dir;
const files = fs.readdirSync(seqDir).filter((f) => /^frame_\d{3,}_f\d+\.png$/.test(f)).sort();
if (files.length < 6) {
  console.error(`[highfreq] ${demo}: la secuencia no es frame-step (${files.length} frames con _fNNN)`);
  process.exit(3);
}

const bandsScript = path.join(ROOT, 'tools/analyze/check-alternating-bands.mjs');
let bands = { ok: true, text: '' };
try {
  bands.text = execFileSync(process.execPath, [bandsScript, seqDir], { encoding: 'utf8' });
} catch (err) {
  bands = { ok: false, text: `${err.stdout ?? ''}${err.stderr ?? ''}` };
}

const flickerArgs = [
  path.join(ROOT, 'tools/vision-review/flicker-check.mjs'),
  '--demo', demo, '--no-ollama', '--frames', String(files.length),
];
let flickerText = '';
try {
  flickerText = execFileSync(process.execPath, flickerArgs, { cwd: ROOT, encoding: 'utf8' });
} catch (err) {
  flickerText = `${err.stdout ?? ''}${err.stderr ?? ''}`;
}
const flickerPath = path.join(ROOT, 'out/vision-review', demoId, 'flicker-report.json');
let report = null;
try { report = JSON.parse(fs.readFileSync(flickerPath, 'utf8')); } catch { report = null; }
const flickerCandidates = report?.detector?.candidates ?? [];
const maxBlocksLow = Math.max(0, ...(report?.frameDiff ?? []).map((x) => Number(x.blocks_low) || 0));
const analysisUnavailable = flickerCandidates.some((c) => Array.isArray(c.type) && c.type.includes('analysis-unavailable'));
const ok = bands.ok && !analysisUnavailable && flickerCandidates.length === 0;

const outDir = path.join(ROOT, 'out/vision-review', demoId);
fs.mkdirSync(outDir, { recursive: true });
const result = {
  demo: demoId,
  sequence: path.relative(ROOT, seqDir).replace(/\\/g, '/'),
  frames: files.length,
  frameStep: true,
  bands: { ok: bands.ok, text: bands.text.trim() },
  flicker: {
    candidates: flickerCandidates.length,
    maxBlocksLow,
    analysisUnavailable,
    report: path.relative(ROOT, flickerPath).replace(/\\/g, '/'),
  },
  ok,
};
const jsonPath = path.join(outDir, 'flicker-highfreq.json');
const mdPath = path.join(outDir, 'flicker-highfreq.md');
fs.writeFileSync(jsonPath, `${JSON.stringify(result, null, 2)}\n`, 'utf8');
fs.writeFileSync(mdPath, [
  `# Flicker de alta frecuencia — ${demoId}`,
  '',
  `- Secuencia: \`${result.sequence}\` (${files.length} frames, paso 1)`,
  `- Bandas alternas: **${bands.ok ? 'OK' : 'FAIL'}**`,
  `- Candidatos deterministas: **${flickerCandidates.length}**`,
  `- Máximo \`blocks_low\`: **${maxBlocksLow}**`,
  `- Análisis de movimiento disponible: **${analysisUnavailable ? 'no' : 'sí'}**`,
  '',
  '## Bandas',
  '',
  '```text',
  bands.text.trim(),
  '```',
  '',
  '```text',
  flickerText.trim(),
  '```',
  '',
].join('\n'), 'utf8');

console.log(`[highfreq] ${demoId}: bands=${bands.ok ? 'ok' : 'fail'} candidates=${flickerCandidates.length} max_blocks_low=${maxBlocksLow}`);
console.log(`[highfreq] ${path.relative(ROOT, mdPath)}`);
if (!ok && has('--require-ok')) process.exit(1);
