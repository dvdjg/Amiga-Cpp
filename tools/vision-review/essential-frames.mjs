#!/usr/bin/env node
// Describe con un modelo de visión (Ollama) los **frames esenciales** de una demo y los
// compara con lo que la demo declara que debe verse. Pensado para `test-regression.sh`
// (solo actúa si Ollama está disponible) y para uso manual.
//
// Cada demo declara sus puntos en `<demo>/vision-points.json`:
//
//   {
//     "model": "qwen3-vl:8b-instruct-q8_0",        // opcional (env OLLAMA_VL_MODEL)
//     "points": [
//       { "name": "cruce coarse de tile",
//         "index": 48,                              // frame 0-based de la secuencia capturada
//         "expect": "entra una columna nueva por la derecha; sin banda negra vertical ni pop" }
//     ]
//   }
//
// La secuencia se busca en `out/run/<demoId>/<config>/sequence/` (config más reciente).
// Salida: `out/vision-review/<demoId>/essential-frames.{json,md}`.
//
// Uso: node tools/vision-review/essential-frames.mjs --demo <ruta> [--out <dir>]
//        [--model X] [--require-ok] [--no-start]
//
// Exit: 0 si Ollama no está disponible o todo coincide; 1 si hay MISMATCH y
// se pasó --require-ok (si no, solo informa).
import * as fs from 'node:fs';
import * as path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);

const ROOT = process.cwd();
const arg = (name, fb) => {
  const i = process.argv.indexOf(name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fb;
};
const has = (name) => process.argv.includes(name);

const demoArg = arg('--demo', '');
if (!demoArg) {
  console.error('Uso: node tools/vision-review/essential-frames.mjs --demo <ruta> [--out <dir>] [--model X] [--require-ok] [--no-start]');
  process.exit(2);
}

// Id de build/out (misma regla que build-demo.sh): features por ruta, resto por leaf.
const norm = demoArg.replace(/\\/g, '/');
const rel = norm.replace(/^demos\//, '').replace(/^tests\//, '');
const leaf = path.basename(norm);
const demoId = rel.startsWith('features/') ? rel.slice('features/'.length).replace(/\//g, '_') : leaf;

const demoDir = path.join(ROOT, demoArg);
const pointsFile = path.join(demoDir, 'vision-points.json');
if (!fs.existsSync(pointsFile)) {
  console.log(`[essential-frames] ${demoArg}: sin vision-points.json (se omite).`);
  process.exit(3);
}
const decl = JSON.parse(fs.readFileSync(pointsFile, 'utf8'));
const points = decl.points || [];
if (points.length === 0) {
  console.log(`[essential-frames] ${demoArg}: sin puntos declarados (se omite).`);
  process.exit(3);
}

// Localiza la secuencia (config más reciente con sequence/).
const runBase = path.join(ROOT, 'out/run', demoId);
function findSequence() {
  if (!fs.existsSync(runBase)) return null;
  const cands = [];
  for (const e of fs.readdirSync(runBase, { withFileTypes: true })) {
    if (!e.isDirectory()) continue;
    const seq = path.join(runBase, e.name, 'sequence');
    if (fs.existsSync(seq)) cands.push({ dir: seq, mt: fs.statSync(seq).mtimeMs });
  }
  cands.sort((a, b) => b.mt - a.mt);
  return cands.length ? cands[0].dir : null;
}
const seqDir = findSequence();
if (!seqDir) {
  console.log(`[essential-frames] ${demoArg}: sin secuencia capturada en out/run/${demoId}/*/sequence (se omite).`);
  process.exit(3);
}
const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d{3,}\.png$/.test(f)).sort();

// --- Diferencia entre frames (para localizar transiciones) ---
function meanDiff(a, b) {
  // Diferencia media por canal entre dos PNG (via pngjs). Sin pngjs, 0.
  let PNG;
  try { ({ PNG } = require('pngjs')); } catch { return 0; }
  const ia = PNG.sync.read(fs.readFileSync(a));
  const ib = PNG.sync.read(fs.readFileSync(b));
  if (ia.width !== ib.width || ia.height !== ib.height) return 999; // cambio de geometria (p. ej. mode switch)
  let sum = 0, n = 0;
  for (let i = 0; i < ia.data.length; i += 4) {
    sum += Math.abs(ia.data[i] - ib.data[i]) + Math.abs(ia.data[i + 1] - ib.data[i + 1]) + Math.abs(ia.data[i + 2] - ib.data[i + 2]);
    n += 3;
  }
  return n ? sum / n : 0;
}
function frameDiffs() {
  const d = [];
  for (let i = 1; i < frames.length; i++) {
    d.push({ i, diff: meanDiff(path.join(seqDir, frames[i - 1]), path.join(seqDir, frames[i])) });
  }
  return d;
}

// Modo sugerencia: propone frames de interes (picos de cambio + ultimo) para que el
// autor de la demo elija y los declare en vision-points.json.
if (has('--suggest')) {
  const d = frameDiffs();
  const peaks = [...d].sort((a, b) => b.diff - a.diff).slice(0, 6).filter((x) => x.diff > 0);
  console.log(`[essential-frames] ${leaf}: ${frames.length} frames en ${path.relative(ROOT, seqDir).replace(/\\/g, '/')}`);
  console.log('  ultimo:', frames.length - 1);
  console.log('  picos de cambio (frame, diff):', peaks.map((x) => `${x.i}(${x.diff.toFixed(1)})`).join(' ') || '(sin pngjs)');
  if (d.length) {
    const period = d.findIndex((x, k) => k > 2 && x.diff > 0 && d[0].diff > 0 && Math.abs(x.diff - d[0].diff) < d[0].diff * 0.3);
    if (period > 0) console.log('  posible cambio periodico cada ~', period, 'frames');
  }
  console.log('  -> declara los elegidos en vision-points.json (index/frames/last/every/max_diff).');
  process.exit(0);
}

// --- Ollama (health check + arranque opcional) ---
const rawHost = process.env.OLLAMA_HOST || '';
const BASE = rawHost.includes('://')
  ? rawHost.replace(/\/$/, '')
  : `http://${rawHost && rawHost !== '0.0.0.0' ? rawHost : '127.0.0.1:11434'}`;
async function health() {
  try {
    const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) });
    return r.ok ? await r.json() : null;
  } catch { return null; }
}
async function startOllama() {
  const { spawn } = await import('node:child_process');
  const exe = [
    'C:\\Users\\dvdjg\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
    (process.env.USERPROFILE || '') + '\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
    'C:\\Program Files\\Ollama\\ollama.exe',
  ].find((c) => fs.existsSync(c));
  if (!exe) return null;
  spawn(exe, ['serve'], { stdio: 'ignore', detached: true }).unref();
  for (let i = 0; i < 24; i++) { await new Promise((r) => setTimeout(r, 500)); const v = await health(); if (v) return v; }
  return null;
}
let ver = await health();
if (!ver && !has('--no-start')) {
  console.log('[essential-frames] Ollama no responde; intentando arrancarlo...');
  ver = await startOllama();
}
if (!ver) {
  console.log('[essential-frames] Ollama NO disponible: se omite la descripción de frames esenciales.');
  process.exit(3);
}
const model = arg('--model', decl.model || process.env.OLLAMA_VL_MODEL || 'qwen3-vl:8b-instruct-q8_0');

// Resuelve los indices de frame de un punto (selectores: index/frames/last/every/max_diff).
function resolveIndices(point) {
  if (Array.isArray(point.frames)) return point.frames;
  if (typeof point.index === 'number') return [point.index];
  if (point.last) return [frames.length - 1];
  if (point.every) { const out = []; for (let i = 0; i < frames.length; i += point.every) out.push(i); return out.slice(0, 8); }
  if (point.max_diff) { const d = frameDiffs().filter((x) => Number.isFinite(x.diff)); return d.length ? [d.reduce((a, b) => (b.diff > a.diff ? b : a)).i] : [0]; }
  return [0];
}

async function describe(point) {
  const idxs = resolveIndices(point);
  const files = idxs.map((i) => frames[i]).filter(Boolean);
  if (files.length === 0) return { ok: false, idxs, text: `(no hay frame ${idxs.join(',')}: secuencia tiene ${frames.length})` };
  const images = files.map((f) => fs.readFileSync(path.join(seqDir, f)).toString('base64'));
  const prompt = [
    `Captura(s) esencial(es) de la demo "${leaf}" (punto "${point.name || ''}").`,
    `Qué DEBE verse: ${point.expect || '(no declarado)'}.`,
    'Describe qué ves realmente en la imagen (bandas, columnas, tiles, bordes negros, repeticiones).',
    'Después responde si COINCIDE con lo esperado. Termina con una línea exactamente:',
    'VERDICT: MATCH  o  VERDICT: MISMATCH',
  ].join('\n');
  const res = await fetch(`${BASE}/api/chat`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ model, messages: [{ role: 'user', content: prompt, images }], stream: false }),
    signal: AbortSignal.timeout(180000),
  });
  if (!res.ok) return { ok: false, text: `HTTP ${res.status}: ${await res.text()}` };
  const j = await res.json();
  const text = j.message?.content ?? '';
  const mismatch = /VERDICT:\s*MISMATCH/i.test(text);
  return { ok: !mismatch, text, files, idxs };
}

const results = [];
for (const p of points) {
  console.log(`[essential-frames] ${leaf}: ${p.name || 'punto'} (frames ${resolveIndices(p).join(',')})...`);
  try {
    const r = await describe(p);
    results.push({ point: p, ...r });
    console.log(`  -> ${r.ok ? 'MATCH' : 'MISMATCH'}`);
  } catch (e) {
    results.push({ point: p, ok: false, error: e.message, idxs: resolveIndices(p) });
    console.log(`  -> error: ${e.message}`);
  }
}

const outDir = arg('--out', path.join(ROOT, 'out/vision-review', demoId));
fs.mkdirSync(outDir, { recursive: true });
fs.writeFileSync(path.join(outDir, 'essential-frames.json'), JSON.stringify({ demo: demoArg, model, seqDir, results }, null, 2), 'utf8');
const md = [
  `# Frames esenciales — ${leaf}`,
  '',
  `Modelo: \`${model}\` · secuencia: \`${path.relative(ROOT, seqDir).replace(/\\/g, '/')}\``,
  '',
  ...results.map((r) => [
    `## ${r.point.name || 'punto'} (frames ${(r.idxs || []).join(',')})`,
    '',
    `- Esperado: ${r.point.expect || '(no declarado)'}`,
    `- Veredicto: **${r.ok ? 'MATCH' : 'MISMATCH'}**`,
    '',
    '```',
    (r.text || r.error || '').trim(),
    '```',
    '',
  ].join('\n')),
].join('\n');
fs.writeFileSync(path.join(outDir, 'essential-frames.md'), md, 'utf8');
console.log(`[essential-frames] informe: ${path.relative(ROOT, outDir).replace(/\\/g, '/')}/essential-frames.md`);

const failed = results.filter((r) => !r.ok);
if (failed.length) {
  if (has('--require-ok')) {
    console.error(`[essential-frames] ${failed.length} punto(s) con MISMATCH y --require-ok.`);
    process.exit(1);
  }
  console.error(`[essential-frames] ${failed.length} punto(s) con MISMATCH (informativo; usa --require-ok para fallar).`);
  process.exit(4);
}
process.exit(0);
