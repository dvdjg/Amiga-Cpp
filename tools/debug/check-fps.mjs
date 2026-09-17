#!/usr/bin/env node
// Gate de deriva de fps: mide cada demo de la tabla trazable de
// `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` y avisa/falla si baja del umbral
// respecto al valor registrado. Pensado como check opt-in (lanza WinUAE por fila).
//
// Uso:
//   node tools/debug/check-fps.mjs [--demo <substr>] [--threshold 0.9] [--samples N]
//                                  [--json] [--warn-only] [--help]
//
//   --demo <substr>   solo las filas cuyo nombre contenga <substr>
//   --threshold <n>   fraccion minima respecto al valor registrado (def. 0.9 = -10 %)
//   --samples N       mediciones por demo; se compara la de mayor fps (def. 2)
//   --json            imprime el resultado como JSON
//   --warn-only       no falla (exit 0) aunque haya deriva
//
// El fps depende de la fase del recorrido (`detail`). Para no depender de medir la
// misma fase, se toman varias muestras y se compara la MEJOR contra el baseline (que
// tambien se registra como mejor de N en record-fps.mjs). Si aun asi baja del umbral,
// es FALLO (la fase observada se reporta como informacion).
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
  console.log(`Uso: node tools/debug/check-fps.mjs [--demo <substr>] [--threshold 0.9] [--samples N] [--json] [--warn-only]

Mide las demos de la tabla de BITACORA_SCROLL_TILES.md y detecta deriva de fps.
Toma varias muestras por demo y compara la mejor contra el baseline; si baja del
umbral, falla. Requiere el emulador (opt-in).`);
  process.exit(0);
}
const filter = (() => { const i = ARGV.indexOf('--demo'); return i >= 0 ? ARGV[i + 1] : null; })();
const threshold = (() => { const i = ARGV.indexOf('--threshold'); return i >= 0 ? Number(ARGV[i + 1]) : 0.9; })();
const samples = (() => { const i = ARGV.indexOf('--samples'); const n = i >= 0 ? parseInt(ARGV[i + 1], 10) : 2; return Number.isFinite(n) && n > 0 ? n : 2; })();
const warnOnly = ARGV.includes('--warn-only');
const json = ARGV.includes('--json');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function measureOnce(demo, config) {
  for (let attempt = 1; attempt <= 3; attempt++) {
    const run = spawnSync(process.execPath, [MEASURE, demo, config, '--json'], { cwd: ROOT, encoding: 'utf8' });
    const out = run.stdout || '';
    const line = out.split(/\r?\n/).reverse().find((l) => l.trim().startsWith('{'));
    if (run.status === 0 && line) return JSON.parse(line);
    if (attempt < 3) {
      console.error(`[check-fps] ${demo}: intento ${attempt}/3 fallido; reintento...`);
      await sleep(2000 * attempt);
    }
  }
  return null;
}

async function measureBest(demo, config, n) {
  let best = null;
  for (let i = 0; i < n; i++) {
    const r = await measureOnce(demo, config);
    if (r && (!best || r.emulatedFps > best.emulatedFps)) best = r;
  }
  return best;
}

function readRows() {
  const lines = fs.readFileSync(BITACORA, 'utf8').split(/\r?\n/);
  const headerIdx = lines.findIndex((l) => /^\|\s*Demo\s*\|/.test(l));
  if (headerIdx < 0) { console.error(`[check-fps] no se encontro la tabla de fps en ${BITACORA}.`); process.exit(1); }
  let sepIdx = headerIdx + 1;
  while (sepIdx < lines.length && !/^\|[\s|:-]+\|$/.test(lines[sepIdx])) sepIdx++;
  let endIdx = sepIdx + 1;
  while (endIdx < lines.length && lines[endIdx].startsWith('|')) endIdx++;
  const rows = [];
  for (let i = sepIdx + 1; i < endIdx; i++) {
    const cells = lines[i].split('|').slice(1, -1).map((c) => c.trim());
    const demo = (cells[0] || '').replace(/`/g, '');
    const config = (cells[1] || '').replace(/`/g, '');
    const fps = Number((cells[2] || '').replace(',', '.'));
    const detail = (cells[4] || '').toLowerCase();
    if (demo && config && Number.isFinite(fps)) rows.push({ demo, config, fps, detail });
  }
  return rows;
}

const rows = readRows().filter((r) => !filter || r.demo.includes(filter));
if (rows.length === 0) { console.error('[check-fps] no hay filas que medir.'); process.exit(1); }

const results = [];
for (const row of rows) {
  const res = await measureBest(row.demo, row.config, samples);
  if (!res) { results.push({ ...row, status: 'error' }); continue; }
  const samePhase = (res.detail || '').toLowerCase() === row.detail;
  const limit = row.fps * threshold;
  const status = res.emulatedFps >= limit ? 'ok' : 'fail';
  results.push({
    demo: row.demo, config: row.config,
    baselineFps: row.fps, currentFps: res.emulatedFps,
    baselineDetail: row.detail, currentDetail: res.detail, samePhase, samples, status,
  });
  if (!json) console.log(`[check-fps] ${row.demo}: ${res.emulatedFps} fps de ${samples} (baseline ${row.fps}, umbral ${(threshold * 100).toFixed(0)} %) -> ${status.toUpperCase()}${samePhase ? '' : ' (fase distinta)'}`);
}

const failed = results.filter((r) => r.status === 'fail' || r.status === 'error');
if (json) console.log(JSON.stringify({ threshold, samples, results, failed: failed.length }, null, 2));
else if (failed.length === 0) console.log(`[check-fps] OK: ${results.length} demo(s), sin deriva por encima del umbral.`);
else console.error(`[check-fps] ${failed.length} demo(s) con deriva (${failed.map((r) => r.demo).join(', ')}).`);

process.exit(failed.length === 0 || warnOnly ? 0 : 1);
