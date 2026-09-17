#!/usr/bin/env node
// Mide los fps de una demo con `measure-fps.mjs` y anexa (o actualiza) su fila en la
// tabla trazable de `docs/guides/roadmap/BITACORA_SCROLL_TILES.md`, con fecha, commit
// y CONFIG_ID. Deja cada medición reproducible y comparable.
//
// Uso:
//   node tools/debug/record-fps.mjs <demo_dir_name> [CONFIG_NAME] [--dry-run]
//
//   --dry-run  imprime la fila que se escribiría, sin tocar la bitácora.
//
// Requisitos: la demo compilada en la config a medir y un `runner.uae` de un
// `run-demo` previo (ver BITACORA_SCROLL_TILES.md, protocolo de medida).
import { spawnSync } from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const BITACORA = path.join(ROOT, 'docs/guides/roadmap/BITACORA_SCROLL_TILES.md');
const MEASURE = path.join(ROOT, 'tools/debug/measure-fps.mjs');

const ARGV = process.argv.slice(2);
if (ARGV.includes('--help') || ARGV.includes('-h')) {
  console.log(`Uso: node tools/debug/record-fps.mjs <demo_dir_name> [CONFIG_NAME] [--dry-run]

Mide fps con tools/debug/measure-fps.mjs --json y anexa/actualiza la fila de la demo
en la tabla de BITACORA_SCROLL_TILES.md (documento, config, fps, ciclos/frame, detail,
fecha y commit).`);
  process.exit(0);
}
const DRY_RUN = ARGV.includes('--dry-run');
const POSITIONAL = ARGV.filter((a) => !a.startsWith('-'));
const DEMO = POSITIONAL[0];
if (!DEMO) { console.error('Falta <demo_dir_name>. Uso: node tools/debug/record-fps.mjs <demo_dir_name> [CONFIG_NAME] [--dry-run]'); process.exit(1); }
const CONFIG_NAME = POSITIONAL[1] || 'A500_debug';

// El arranque de WinUAE + GDB ocasionalmente falla de forma transitoria (handshake,
// puerto aun en TIME_WAIT tras una medicion previa). Se reintenta con backoff antes
// de dar la medicion por perdida.
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const MAX_ATTEMPTS = 3;
let jsonLine = null;
let lastErr = '';
for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
  const run = spawnSync(process.execPath, [MEASURE, DEMO, CONFIG_NAME, '--json'], { cwd: ROOT, encoding: 'utf8' });
  const out = run.stdout || '';
  const line = out.split(/\r?\n/).reverse().find((l) => l.trim().startsWith('{'));
  if (run.status === 0 && line) { jsonLine = line; process.stdout.write(out); break; }
  lastErr = (run.stderr || '') + out;
  if (attempt < MAX_ATTEMPTS) {
    const delay = 2000 * attempt;
    console.error(`[record-fps] intento ${attempt}/${MAX_ATTEMPTS} fallido (exit ${run.status}); reintento en ${delay} ms.`);
    await sleep(delay);
  }
}
if (!jsonLine) {
  console.error(`[record-fps] la medicion fallo tras ${MAX_ATTEMPTS} intentos.`);
  process.stderr.write(lastErr);
  process.exit(1);
}
const res = JSON.parse(jsonLine);

const fmtFps = (n) => Number(n).toFixed(2).replace('.', ',');
const fmtCycles = (n) => String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
const commit = res.commit ? '`' + res.commit + '`' : '—';
const row = `| \`${res.demo}\` | \`${res.config}\` | ${fmtFps(res.emulatedFps)} | ${fmtCycles(res.cyclesPerFrame)} | ${res.detail} | ${res.date} | ${commit} |`;

if (DRY_RUN) {
  console.log('[record-fps] fila (dry-run): ' + row);
  process.exit(0);
}

const lines = fs.readFileSync(BITACORA, 'utf8').split(/\r?\n/);
const headerIdx = lines.findIndex((l) => /^\|\s*Demo\s*\|/.test(l));
if (headerIdx < 0) { console.error(`[record-fps] no se encontro la tabla de fps en ${BITACORA}.`); process.exit(1); }
let sepIdx = headerIdx + 1;
while (sepIdx < lines.length && !/^\|[\s|:-]+\|$/.test(lines[sepIdx])) sepIdx++;
let endIdx = sepIdx + 1;
while (endIdx < lines.length && lines[endIdx].startsWith('|')) endIdx++;

const keyOf = (l) => (l.split('|')[1] || '').trim().replace(/`/g, '');
const existing = lines.slice(sepIdx + 1, endIdx);
let replaced = false;
const newRows = existing.map((l) => {
  if (keyOf(l) === res.demo) { replaced = true; return row; }
  return l;
});
if (!replaced) newRows.push(row);

const out = [...lines.slice(0, sepIdx + 1), ...newRows, ...lines.slice(endIdx)];
fs.writeFileSync(BITACORA, out.join('\n'), 'utf8');
console.log(`[record-fps] ${replaced ? 'actualizada' : 'anadida'} la fila de ${res.demo} en ${path.relative(ROOT, BITACORA).split(path.sep).join('/')}.`);
