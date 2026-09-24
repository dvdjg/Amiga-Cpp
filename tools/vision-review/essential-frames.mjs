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

async function describe(point) {
  const idxs = point.frames || [point.index];
  const files = idxs.map((i) => frames[i]).filter(Boolean);
  if (files.length === 0) return { ok: false, text: `(no hay frame ${idxs.join(',')}: secuencia tiene ${frames.length})` };
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
  return { ok: !mismatch, text, files };
}

const results = [];
for (const p of points) {
  console.log(`[essential-frames] ${leaf}: ${p.name || 'punto'} (frames ${(p.frames || [p.index]).join(',')})...`);
  try {
    const r = await describe(p);
    results.push({ point: p, ...r });
    console.log(`  -> ${r.ok ? 'MATCH' : 'MISMATCH'}`);
  } catch (e) {
    results.push({ point: p, ok: false, error: e.message });
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
    `## ${r.point.name || 'punto'} (frames ${(r.point.frames || [r.point.index]).join(',')})`,
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
