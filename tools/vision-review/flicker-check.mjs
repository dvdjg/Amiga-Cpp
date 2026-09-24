#!/usr/bin/env node
// Detecta PARPADEO/GLITCH (flicker) en una secuencia de frames y, si Ollama está
// disponible, pide a un modelo de visión que describa las zonas señaladas para
// producir un informe accionable (celda → coordenadas → frames → descripción).
//
// Base determinista: rejilla de celdas; para cada celda se mide la **oscilación
// temporal** de luminancia (media de |L[f+1]-L[f]|). Las celdas con más oscilación
// son candidatas (una zona que debería ser estable y cambia cada frame = flicker).
// Luego el modelo mira los frames consecutivos de la peor zona y describe el defecto.
//
// Uso: node tools/vision-review/flicker-check.mjs --demo <ruta> [--frames 6]
//        [--cells 16] [--top 4] [--model X] [--no-ollama]
// Salida: out/vision-review/<demoId>/flicker-report.{json,md}
import * as fs from 'node:fs';
import * as path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);

const ROOT = process.cwd();
const arg = (n, fb) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fb; };
const has = (n) => process.argv.includes(n);

const demoArg = arg('--demo', '');
if (!demoArg) { console.error('Uso: node tools/vision-review/flicker-check.mjs --demo <ruta> [--frames 6] [--cells 16] [--top 4] [--no-ollama]'); process.exit(2); }

const norm = demoArg.replace(/\\/g, '/');
const rel = norm.replace(/^demos\//, '').replace(/^tests\//, '');
const leaf = path.basename(norm);
const demoId = rel.startsWith('features/') ? rel.slice('features/'.length).replace(/\//g, '_') : leaf;

const runBase = path.join(ROOT, 'out/run', demoId);
function findSequence() {
  if (!fs.existsSync(runBase)) return null;
  const c = [];
  for (const e of fs.readdirSync(runBase, { withFileTypes: true })) {
    if (!e.isDirectory()) continue;
    const seq = path.join(runBase, e.name, 'sequence');
    if (fs.existsSync(seq)) c.push({ dir: seq, mt: fs.statSync(seq).mtimeMs });
  }
  c.sort((a, b) => b.mt - a.mt);
  return c.length ? c[0].dir : null;
}
const seqDir = findSequence();
if (!seqDir) { console.log(`[flicker] ${demoArg}: sin secuencia en out/run/${demoId}/*/sequence (se omite).`); process.exit(3); }
const files = fs.readdirSync(seqDir).filter((f) => /^frame_\d{3,}\.png$/.test(f)).sort();
if (files.length < 3) { console.log(`[flicker] ${demoArg}: secuencia con <3 frames (se omite).`); process.exit(3); }

let PNG;
try { ({ PNG } = require('pngjs')); } catch { console.error('[flicker] pngjs no disponible.'); process.exit(2); }
const imgs = files.map((f) => PNG.sync.read(fs.readFileSync(path.join(seqDir, f))));
const W = imgs[0].width, H = imgs[0].height;
const lum = (img, x, y) => { const i = (y * img.width + x) * 4; return (img.data[i] + img.data[i + 1] + img.data[i + 2]) / 3; };

const cells = parseInt(arg('--cells', '16'), 10);
const cw = Math.max(1, Math.floor(W / cells)), ch = Math.max(1, Math.floor(H / cells));

// Luminancia media por celda y frame.
const L = []; // L[cellIndex][frame]
for (let cy = 0; cy < ch; cy++) for (let cx = 0; cx < cw; cx++) L.push([]);
for (let f = 0; f < imgs.length; f++) {
  const img = imgs[f];
  let k = 0;
  for (let cy = 0; cy < ch; cy++) {
    for (let cx = 0; cx < cw; cx++) {
      const x0 = cx * Math.floor(W / cw), y0 = cy * Math.floor(H / ch);
      const x1 = (cx === cw - 1) ? W : (cx + 1) * Math.floor(W / cw);
      const y1 = (cy === ch - 1) ? H : (cy + 1) * Math.floor(H / ch);
      let s = 0, n = 0;
      for (let y = y0; y < y1; y += 2) for (let x = x0; x < x1; x += 2) { s += lum(img, x, y); n++; }
      L[k++].push(n ? s / n : 0);
    }
  }
}
// Oscilación temporal por celda.
const zones = [];
for (let i = 0; i < L.length; i++) {
  const seq = L[i];
  let d = 0;
  for (let f = 1; f < seq.length; f++) d += Math.abs(seq[f] - seq[f - 1]);
  const score = d / Math.max(1, seq.length - 1);
  zones.push({ i, score });
}
zones.sort((a, b) => b.score - a.score);
const top = zones.slice(0, parseInt(arg('--top', '4'), 10)).map((z) => {
  const cx = z.i % cw, cy = Math.floor(z.i / cw);
  const x = cx * Math.floor(W / cw), y = cy * Math.floor(H / ch);
  const w = (cx === cw - 1 ? W : (cx + 1) * Math.floor(W / cw)) - x;
  const h = (cy === ch - 1 ? H : (cy + 1) * Math.floor(H / ch)) - y;
  return { rank: z.i, score: Math.round(z.score * 100) / 100, x, y, w, h };
});

// Ventana de frames para el modelo: donde la mejor zona tiene su mayor delta.
const best = top[0];
let win = 0, bestD = -1;
if (best) {
  const bi = best.rank;
  for (let f = 1; f < L[bi].length; f++) { const d = Math.abs(L[bi][f] - L[bi][f - 1]); if (d > bestD) { bestD = d; win = f; } }
}
const nFrames = parseInt(arg('--frames', '6'), 10);
const from = Math.max(0, Math.min(win - 1, files.length - nFrames));
const windowFiles = files.slice(from, from + nFrames);

// --- Ollama (opcional) ---
let modelText = '(sin análisis de modelo)';
if (!has('--no-ollama')) {
  const rawHost = process.env.OLLAMA_HOST || '';
  const BASE = rawHost.includes('://') ? rawHost.replace(/\/$/, '') : `http://${rawHost && rawHost !== '0.0.0.0' ? rawHost : '127.0.0.1:11434'}`;
  let ver = null;
  try { const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) }); if (r.ok) ver = await r.json(); } catch {}
  if (ver) {
    const model = arg('--model', process.env.OLLAMA_VL_MODEL || 'qwen3-vl:8b-instruct-q8_0');
    const images = windowFiles.map((f) => fs.readFileSync(path.join(seqDir, f)).toString('base64'));
    const prompt = [
      `Estos son ${images.length} frames CONSECUTIVOS de la demo "${leaf}" (Amiga), capturados ~20 ms aparte.`,
      best ? `La rejilla señala como zona más inestable: x=${best.x}..${best.x + best.w}, y=${best.y}..${best.y + best.h} (oscilación media ${best.score}).` : '',
      'Analiza si hay PARPADEO/GLITCH: zonas que cambian de brillo/color entre frames consecutivos',
      'sin que formen parte de una animación coherente (bandas que destellan, tiles que saltan,',
      'bordes que aparecen/desaparecen, ruido). Indica la zona aproximada (x,y) y el patrón.',
      'Termina con: VERDICT: FLICKER  o  VERDICT: OK',
    ].filter(Boolean).join('\n');
    try {
      const res = await fetch(`${BASE}/api/chat`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ model, messages: [{ role: 'user', content: prompt, images }], stream: false }), signal: AbortSignal.timeout(180000) });
      const j = await res.json();
      modelText = j.message?.content ?? '(sin respuesta)';
    } catch (e) { modelText = `(error de modelo: ${e.message})`; }
  } else {
    modelText = '(Ollama no disponible)';
  }
}

const outDir = arg('--out', path.join(ROOT, 'out/vision-review', demoId));
fs.mkdirSync(outDir, { recursive: true });
const report = { demo: demoArg, seqDir, framesAnalyzed: files.length, grid: { cells: cw + 'x' + ch }, window: windowFiles, topZones: top, modelText };
fs.writeFileSync(path.join(outDir, 'flicker-report.json'), JSON.stringify(report, null, 2), 'utf8');
const md = [
  `# Flicker / glitch — ${leaf}`,
  '',
  `Secuencia: \`${path.relative(ROOT, seqDir).replace(/\\/g, '/')}\` · ${files.length} frames · ventana analizada: ${windowFiles[0]}..${windowFiles[windowFiles.length - 1]}`,
  '',
  '## Zonas con más oscilación temporal (candidatas a parpadeo)',
  '',
  '| x | y | w | h | oscilación |',
  '|---|---|---|---|---|',
  ...top.map((z) => `| ${z.x} | ${z.y} | ${z.w} | ${z.h} | ${z.score} |`),
  '',
  '> Una zona **estable** que cambia cada frame es sospechosa; si además debería ser estática, es un defecto.',
  '',
  '## Descripción del modelo (frames consecutivos)',
  '',
  '```',
  modelText.trim(),
  '```',
  '',
  '## Cómo usarlo',
  '',
  'La zona (x,y,w,h) + la descripción señalan dónde mirar (copper/blitter/punteros de planos).',
  'Es evidencia para arreglar la demo y, si el defecto viene del engine, para corregirlo.',
].join('\n');
fs.writeFileSync(path.join(outDir, 'flicker-report.md'), md, 'utf8');
console.log(`[flicker] ${leaf}: top zona x=${top[0]?.x} y=${top[0]?.y} osc=${top[0]?.score}; informe: ${path.relative(ROOT, outDir).replace(/\\/g, '/')}/flicker-report.md`);
process.exit(0);
