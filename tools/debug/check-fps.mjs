#!/usr/bin/env node
// Gate de deriva de fps: mide cada demo de la tabla trazable de
// `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` y avisa/falla si baja del umbral
// respecto al valor registrado. Pensado como check opt-in (lanza WinUAE por fila).
//
// Uso:
//   node tools/debug/check-fps.mjs [--demo <substr>] [--threshold 0.9] [--json]
//                                  [--warn-only] [--help]
//
//   --demo <substr>   solo las filas cuyo nombre contenga <substr>
//   --threshold <n>   fraccion minima respecto al valor registrado (def. 0.9 = -10 %)
//   --json            imprime el resultado como JSON
//   --warn-only       no falla (exit 0) aunque haya deriva
//
// El fps depende de la fase del recorrido (`detail`), asi que:
//   - si la fase medida coincide con la registrada y se baja del umbral -> FALLO
//   - si la fase difiere -> AVISO (no comparable de forma estricta)
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
  console.log(`Uso: node tools/debug/check-fps.mjs [--demo <substr>] [--threshold 0.9] [--json] [--warn-only]

Mide las demos de la tabla de BITACORA_SCROLL_TILES.md y detecta deriva de fps.
Falla si una demo medida en la MISMA fase (detail) baja del umbral; si la fase
difiere, solo avisa. Requiere el emulador (opt-in).`);
  process.exit(0);
}
const filter = (() => { const i = ARGV.indexOf('--demo'); return i >= 0 ? ARGV[i + 1] : null; })();
const threshold = (() => { const i = ARGV.indexOf('--threshold'); return i >= 0 ? Number(ARGV[i + 1]) : 0.9; })();
const warnOnly = ARGV.includes('--warn-only');
const json = ARGV.includes('--json');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function measure(demo, config) {
  for (let attempt = 1; attempt <= 3; attempt++) {
    const run = spawnSync(process.execPath, [MEASURE, demo, config, '--json'], { cwd: ROOT, encoding: 'utf8' });
    const out = run.stdout || '';
    const line = out.split(/\r?\n/).reverse().find((l) => l.trim().startsWith('{'));
    if (run.status === 0 && line) return JSON.parse(line);
    if (attempt < 3) { console.error(`[check-fps] ${demo}: intento ${attempt}/3 fallido; reintento...`); }
  }
  return null;
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
  const res = measure(row.demo, row.config);
  if (!res) { results.push({ ...row, status: 'error' }); continue; }
  const samePhase = (res.detail || '').toLowerCase() === row.detail;
  const limit = row.fps * threshold;
  const status = res.emulatedFps >= limit ? 'ok' : (samePhase ? 'fail' : 'warn');
  results.push({
    demo: row.demo, config: row.config,
    baselineFps: row.fps, currentFps: res.emulatedFps,
    baselineDetail: row.detail, currentDetail: res.detail, samePhase, status,
  });
  if (!json) console.log(`[check-fps] ${row.demo}: ${res.emulatedFps} fps (baseline ${row.fps}, umbral ${(threshold * 100).toFixed(0)} %) -> ${status.toUpperCase()}${samePhase ? '' : ' (fase distinta)'}`);
}

const failed = results.filter((r) => r.status === 'fail' || r.status === 'error');
if (json) console.log(JSON.stringify({ threshold, results, failed: failed.length }, null, 2));
else if (failed.length === 0) console.log(`[check-fps] OK: ${results.length} demo(s), sin deriva por encima del umbral.`);
else console.error(`[check-fps] ${failed.length} demo(s) con deriva (${failed.map((r) => r.demo).join(', ')}).`);

process.exit(failed.length === 0 || warnOnly ? 0 : 1);
