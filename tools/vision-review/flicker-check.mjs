#!/usr/bin/env node
// Detecta PARPADEO/GLITCH (flicker) en una secuencia de frames con un enfoque **híbrido**:
//
//   1. Capa **determinista** (OpenCV, `temporal-detect.py`): diferencia de frames + *optical flow*
//      (Farneback) + análisis por bloques → localiza candidatos (flicker/tearing/corrupción) y
//      descarta el movimiento coherente. Es la referencia fiable para parpadeo temporal.
//   2. Capa de **modelo de visión** (Ollama), **solo** sobre las regiones candidatas con frames de
//      referencia+contexto: el modelo confirma o descarta una sospecha ya localizada (menos
//      alucinaciones). Respuesta **estructurada** y con **regiones relativas** (sin píxeles).
//
// Base determinista también propia (rejilla de celdas, oscilación temporal de luminancia) por si
// OpenCV no está disponible.
//
// Uso: node tools/vision-review/flicker-check.mjs --demo <ruta> [--frames 6]
//        [--cells 16] [--top 4] [--model X] [--no-ollama] [--no-detect]
// Salida: out/vision-review/<demoId>/flicker-report.{json,md} (+ temporal-detect.{json,md})
import * as fs from 'node:fs';
import * as path from 'node:path';
import { execFileSync } from 'node:child_process';
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

// --- Capa 1: detección temporal determinista (OpenCV). Fiable para parpadeo/tearing. ---
// Devuelve candidatos {frame, region[px], relative, type, context_frames} o null si no aplica.
let detector = null;
let frameDiff = null;
const outDirEarly = path.join(ROOT, 'out/vision-review', demoId);

// Frame-diff determinista: cuántos píxeles cambian y en qué bbox entre frames consecutivos, más
// SSIM (cambio estructural). Es la referencia para separar movimiento (cambia la zona que se
// desplaza) de glitch (cambia una zona estable). Prevalece ante una respuesta dudosa del modelo.
// Se usa la versión Python (NumPy/OpenCV, SIMD) si está disponible; si no, la de Node.
{
  const pyFile = path.join(ROOT, 'tools/vision-review/frame-diff.py');
  let got = null;
  try {
    const out = execFileSync(process.env.PYTHON || 'python',
      [pyFile, '--sequence', seqDir, '--json'], { stdio: ['ignore', 'pipe', 'pipe'] });
    got = JSON.parse(out.toString('utf8'));
  } catch { got = null; }
  if (!got) {
    try {
      const fd = execFileSync(process.execPath, [path.join(ROOT, 'tools/vision-review/frame-diff.mjs'),
        '--sequence', seqDir, '--json'], { stdio: ['ignore', 'pipe', 'pipe'] });
      got = JSON.parse(fd.toString('utf8'));
    } catch { got = null; }
  }
  frameDiff = got;
}

if (!has('--no-detect')) {
  const py = process.env.PYTHON || 'python';
  const script = path.join(ROOT, 'tools/vision-review/temporal-detect.py');
  try {
    // El detector devuelve 4 cuando hay candidatos: no es un error, es informativo.
    execFileSync(py, [script, '--sequence', seqDir, '--out', outDirEarly], { stdio: ['ignore', 'ignore', 'pipe'] });
  } catch (e) {
    if (!(e && typeof e.status === 'number' && (e.status === 4 || e.status === 0))) {
      console.warn(`[flicker] detector determinista no disponible (${(e.stderr || e.message || '').toString().split('\n')[0]}); se usa la rejilla.`);
    }
  }
  const jf = path.join(outDirEarly, 'temporal-detect.json');
  if (fs.existsSync(jf)) {
    try { detector = JSON.parse(fs.readFileSync(jf, 'utf8')); } catch { detector = null; }
  }
}

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

// --- Capa 2: modelo de visión (Ollama) sobre la sospecha ya localizada ---
// Prompt estructurado (sí/no + tipo + zona RELATIVA + confianza) y, si hay candidatos del
// detector, se centra en su región con frames de referencia+contexto. Prohibido dar píxeles.
// Contrato de los prompts: `tools/vision-review/PROMPTS.md`.
const STRUCTURED = [
  'Responde SOLO en este formato (sin píxeles, sin coordenadas numéricas):',
  '- Anomalía: sí / no',
  '- Tipo: parpadeo / tearing / corrupción / desaparición / otro / ninguno',
  '- Zona relativa: (arriba-izquierda / centro / abajo-derecha / pantalla completa / …)',
  '- Confianza: alta / media / baja',
  '- Explicación (máximo 2 frases)',
].join('\n');

let modelText = '(sin análisis de modelo)';
const candidates = (detector && Array.isArray(detector.candidates)) ? detector.candidates : [];
if (!has('--no-ollama')) {
  const rawHost = process.env.OLLAMA_HOST || '';
  const BASE = rawHost.includes('://') ? rawHost.replace(/\/$/, '') : `http://${rawHost && rawHost !== '0.0.0.0' ? rawHost : '127.0.0.1:11434'}`;
  let ver = null;
  try { const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) }); if (r.ok) ver = await r.json(); } catch {}
  if (ver) {
    const model = arg('--model', process.env.OLLAMA_VL_MODEL || 'qwen3-vl:8b-instruct-q8_0');
    const lines = [];
    let images, focus;
    if (candidates.length) {
      const c = candidates[0];
      const idx = c.context_frames && c.context_frames.length ? c.context_frames : [c.frame];
      const idxInFiles = idx.filter((i) => i >= 0 && i < files.length);
      images = idxInFiles.map((i) => fs.readFileSync(path.join(seqDir, files[i])).toString('base64'));
      focus = c.relative;
      lines.push(
        `Estos son frames CONSECUTIVOS de la demo "${leaf}" (Amiga 500, lowres, paleta limitada).`,
        `Frame ${idxInFiles[0] === c.frame ? 'central' : 'A'} es la referencia (comportamiento esperado).`,
        `El detector determinista (diferencia + optical flow) señala una posible anomalía tipo ` +
        `"${c.type.join('/')}" en la zona relativa "${focus}"; el movimiento coherente NO está señalado.`,
        'Confirma o descarta ESA sospecha a partir de los frames.');
    } else {
      images = windowFiles.map((f) => fs.readFileSync(path.join(seqDir, f)).toString('base64'));
      focus = best ? '(sin candidato determinista; rejilla de luminancia)' : '(sin candidato)';
      lines.push(
        `Estos son ${images.length} frames CONSECUTIVOS de la demo "${leaf}" (Amiga 500, lowres, paleta limitada).`,
        `El primer frame es la referencia. ${best ? `La rejilla marca como zona más inestable la relativa a x≈${Math.round(best.x / W * 3) + 1}/3, y≈${Math.round(best.y / H * 3) + 1}/3.` : ''}`,
        'Compara contra la referencia.');
    }
    lines.push(
      '',
      '¿Hay parpadeo, flickering, tearing, corrupción de tiles, objetos que aparecen/desaparecen',
      'de forma no justificada por el movimiento legítimo, o cualquier artefacto no explicable?',
      '',
      STRUCTURED);
    const prompt = lines.filter(Boolean).join('\n');
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
const report = {
  demo: demoArg, seqDir, framesAnalyzed: files.length,
  grid: { cells: cw + 'x' + ch }, window: windowFiles, topZones: top,
  frameDiff: frameDiff ? frameDiff.pairs : null,
  detector: detector ? { candidates: candidates, anomalyBlocks: detector.anomaly_blocks } : null,
  modelText,
};
fs.writeFileSync(path.join(outDir, 'flicker-report.json'), JSON.stringify(report, null, 2), 'utf8');

const md = [
  `# Flicker / glitch — ${leaf}`,
  '',
  `Secuencia: \`${path.relative(ROOT, seqDir).replace(/\\/g, '/')}\` · ${files.length} frames · ventana analizada: ${windowFiles[0]}..${windowFiles[windowFiles.length - 1]}`,
  '',
];
if (detector) {
  if (frameDiff && frameDiff.pairs) {
    const frozen = frameDiff.pairs.filter((p) => p.changed === 0).length;
    const hasSsim = frameDiff.pairs.some((p) => typeof p.ssim === 'number');
    md.push(
      '## Frame-diff determinista (referencia)',
      '',
      `Pares de frames con cambio de píxeles (umbral 40): ${frameDiff.pairs.length - frozen}/${frameDiff.pairs.length} con cambio` +
      (frozen ? `, ${frozen} congelado(s).` : '.') + (hasSsim ? ' SSIM = cambio estructural (1.0 = idéntico).' : ''),
      '',
      hasSsim ? '| par | px cambiados | bbox | SSIM |' : '| par | px cambiados | bbox |',
      hasSsim ? '|---|---|---|---|' : '|---|---|---|',
      ...frameDiff.pairs.map((p) => {
        const bb = p.bbox ? `${p.bbox[0]},${p.bbox[1]}–${p.bbox[2]},${p.bbox[3]}` : '(sin cambio)';
        return hasSsim ? `| f${p.from}→f${p.to} | ${p.changed} | ${bb} | ${p.ssim ?? '—'} |`
                       : `| f${p.from}→f${p.to} | ${p.changed} | ${bb} |`;
      }),
      '');
  }
  md.push(
    '## Candidatos deterministas (OpenCV: frame-diff + optical flow)',
    '',
    `Bloques anómalos: ${detector.anomaly_blocks} → ${candidates.length} candidato(s). Zonas en movimiento coherente no se marcan.`,
    '');
  if (candidates.length) {
    md.push('| frame | región (px) | zona | tipo | score |', '|---|---|---|---|---|');
    for (const c of candidates) {
      const [x1, y1, x2, y2] = c.region;
      md.push(`| ${c.frame} | ${x1},${y1}–${x2},${y2} | ${c.relative} | ${c.type.join('/')} | ${c.score} |`);
    }
    md.push('', 'Detalle completo en `temporal-detect.md`.');
  } else {
    md.push('Sin anomalías deterministas por encima del umbral.');
  }
  md.push('');
} else {
  md.push('## Zonas con más oscilación temporal (rejilla de luminancia; detector no disponible)',
    '', '| x | y | w | h | oscilación |', '|---|---|---|---|---|',
    ...top.map((z) => `| ${z.x} | ${z.y} | ${z.w} | ${z.h} | ${z.score} |`), '');
}
md.push(
  '> Cautela: el modelo de visión **alucina** en glitches temporales finos; la referencia fiable es',
  '> la capa determinista. La respuesta del modelo es **apoyo**, no veredicto.',
  '',
  '## Respuesta del modelo (estructurada, sobre la sospecha localizada)',
  '',
  '```',
  modelText.trim(),
  '```',
  '',
  '## Cómo usarlo',
  '',
  'La región + tipo (`flicker`/`tearing`/`corruption`) y la respuesta del modelo señalan dónde mirar',
  '(copper/blitter/punteros de planos). Si el defecto viene del engine, se corrige en el engine.',
);
const mdText = md.join('\n');
fs.writeFileSync(path.join(outDir, 'flicker-report.md'), mdText, 'utf8');
const which = candidates.length ? `${candidates.length} candidato(s) determinista(s), top zona ${candidates[0].relative}` : `top rejilla x=${top[0]?.x} y=${top[0]?.y} osc=${top[0]?.score}`;
console.log(`[flicker] ${leaf}: ${which}; informe: ${path.relative(ROOT, outDir).replace(/\\/g, '/')}/flicker-report.md`);
process.exit(0);
