#!/usr/bin/env node
// Auto-test del detector temporal determinista (`temporal-detect.py`). Genera secuencias sintéticas
// con artefactos conocidos (`make-synth-seq.mjs`), ejecuta el detector y comprueba que:
//
//   1. `flicker`    : la secuencia con parpadeo se detecta como `flicker`.
//   2. `corruption` : la corrupción que aparece y no vuelve se detecta como `corruption`.
//   3. `motion`     : la banda que se desplaza NO se marca (ningún candidato).
//
// Sin OpenCV/Python disponible se **omite** (exit 3). Con `--require-ok`, un fallo es exit 1.
//
// Uso: node tools/vision-review/selftest-temporal.mjs [--require-ok] [--keep]
// Salida: out/vision-review/selftest-temporal.md

import * as fs from 'node:fs';
import * as path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const has = (n) => process.argv.includes(n);
const tmp = path.join(ROOT, 'out/tmp/selftest-temporal');
const reportDir = path.join(ROOT, 'out/vision-review');

let failures = 0;
const rows = [];

function run(cmd, args) {
  return execFileSync(cmd, args, { stdio: ['ignore', 'pipe', 'pipe'] }).toString('utf8');
}

// ¿Está disponible el detector?
try {
  run(process.env.PYTHON || 'python', ['-c', 'import cv2, numpy']);
} catch {
  console.log('[selftest-temporal] OpenCV/Python no disponible; se omite.');
  process.exit(3);
}

function detect(kind) {
  const seq = path.join(tmp, kind);
  const out = path.join(tmp, kind + '-out');
  try {
    run(process.env.PYTHON || 'python', [
      path.join(ROOT, 'tools/vision-review/temporal-detect.py'), '--sequence', seq, '--out', out,
    ]);
  } catch (e) {
    // El detector devuelve 4 cuando hay candidatos (informativo, no error).
    if (!(e && (e.status === 4 || e.status === 0))) throw e;
  }
  const jf = path.join(out, 'temporal-detect.json');
  return JSON.parse(fs.readFileSync(jf, 'utf8'));
}

const cases = [
  { kind: 'flicker', want: 'flicker' },
  { kind: 'mix', want: 'corruption' }, // en `mix` conviven flicker+corruption
  { kind: 'motion', want: null },      // no debe haber candidatos
];

for (const c of cases) {
  const seq = path.join(tmp, c.kind);
  run(process.execPath, [path.join(ROOT, 'tools/vision-review/make-synth-seq.mjs'),
    '--out', seq, '--kind', c.kind, '--frames', '8']);
  const rep = detect(c.kind);
  const types = new Set();
  for (const cand of rep.candidates) for (const t of cand.type) types.add(t);
  let ok;
  if (c.want === null) ok = rep.candidates.length === 0;
  else ok = types.has(c.want);
  rows.push({ kind: c.kind, candidates: rep.candidates.length, types: [...types].sort().join(',') || '-', want: c.want ?? '(ninguno)', ok });
  if (!ok) failures++;
}

const md = [
  '# Auto-test del detector temporal (secuencias sintéticas)',
  '',
  '| caso | candidatos | tipos vistos | esperado | resultado |',
  '|---|---|---|---|---|',
  ...rows.map((r) => `| ${r.kind} | ${r.candidates} | ${r.types} | ${r.want} | ${r.ok ? 'OK' : 'FALLO'} |`),
  '',
].join('\n');
fs.mkdirSync(reportDir, { recursive: true });
fs.writeFileSync(path.join(reportDir, 'selftest-temporal.md'), md, 'utf8');

for (const r of rows) console.log(`  ${r.ok ? 'OK  ' : 'FALLO'} ${r.kind}: candidatos=${r.candidates} tipos=[${r.types}] (esperado ${r.want})`);

if (!has('--keep')) fs.rmSync(tmp, { recursive: true, force: true });

if (failures === 0) {
  console.log('OK: detector temporal (auto-test) validado.');
  process.exit(0);
}
console.log(`FALLO: ${failures} caso(s). Informe: out/vision-review/selftest-temporal.md`);
process.exit(has('--require-ok') ? 1 : 4);
