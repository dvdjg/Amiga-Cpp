#!/usr/bin/env node
/**
 * Smoke test de determinismo: ejecuta run-demo DOS veces para el mismo demo y
 * compara las secuencias frame a frame (emparejando por numero de frame del
 * run-status). Si la renderizacion es determinista, los frames del mismo numero
 * deben ser (casi) identicos.
 *
 * Uso: node tools/verify/verify-determinism.mjs [demos/<demo>] [--threshold 8]
 * Env: WINUAE_PATH/WINUAE_CONFIG opcionales; requiere la demo compilada.
 */
import { execSync } from 'child_process';
import { readFileSync, existsSync, mkdirSync, readdirSync, copyFileSync } from 'fs';
import { inflateSync } from 'zlib';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..', '..');
const DEMO_ARG = process.argv[2] || 'demos/101_ehb_tile_scroll_driver';
const DEMO = DEMO_ARG.replace(/^demos\//, '');
const THRESHOLD = parseFloat(process.argv.find(a => a.startsWith('--threshold'))?.split('=')[1] ?? '8') || 8;
const OUT = path.join(ROOT, 'out', 'run', DEMO);

function loadPng(p) {
  const buf = readFileSync(p);
  const chunks = []; let off = 8;
  while (off < buf.length) { const len = buf.readUInt32BE(off); chunks.push({ type: buf.toString('ascii', off + 4, off + 8), data: buf.subarray(off + 8, off + 8 + len) }); off += 12 + len; }
  const ihdr = chunks.find(c => c.type === 'IHDR').data;
  const w = ihdr.readUInt32BE(0), h = ihdr.readUInt32BE(4);
  const bitDepth = ihdr[8], colorType = ihdr[9];
  const idat = Buffer.concat(chunks.filter(c => c.type === 'IDAT').map(c => c.data));
  const raw = inflateSync(idat);
  const bpp = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 0 ? 1 : 4;
  const stride = Math.floor((w * bitDepth * (colorType === 0 ? 1 : bpp) + 7) / 8);
  const px = Buffer.alloc(w * h * 3);
  let pos = 0; let prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const filter = raw[pos++]; const line = raw.subarray(pos, pos + stride); pos += stride;
    const recon = Buffer.alloc(stride);
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? recon[x - bpp] : 0, b = y > 0 ? prev[x] : 0, c = (x >= bpp && y > 0) ? prev[x - bpp] : 0;
      let v = line[x];
      if (filter === 1) v = (v + a) & 0xff; else if (filter === 2) v = (v + b) & 0xff; else if (filter === 3) v = (v + ((a + b) >> 1)) & 0xff;
      else if (filter === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v = (v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c)) & 0xff; }
      recon[x] = v;
    }
    for (let x = 0; x < w; x++) {
      const si = x * bpp;
      const r = colorType === 0 ? recon[si] : recon[si], g = colorType === 0 ? recon[si] : colorType === 2 || colorType === 6 ? recon[si + 1] : recon[si], b = colorType === 0 ? recon[si] : colorType === 2 || colorType === 6 ? recon[si + 2] : recon[si];
      const di = (y * w + x) * 3;
      px[di] = r; px[di + 1] = g; px[di + 2] = b;
    }
    prev = recon;
  }
  return { w, h, px };
}

function meanAbsDiff(a, b) {
  let total = 0;
  for (let i = 0; i < a.px.length; i++) total += Math.abs(a.px[i] - b.px[i]);
  return total / a.px.length;
}

// busca el desplazamiento (dx,dy) que minimiza el error entre a y b (centro).
// Si el contenido es el mismo pero la captura cayo en una posicion distinta
// (lag del display), el minimo tras desplazar es bajo -> determinista.
function bestShift(a, b) {
  const w = Math.min(a.w, b.w), h = Math.min(a.h, b.h);
  let best = { dx: 0, dy: 0, err: Infinity };
  for (let dy = -3; dy <= 3; dy++) {
    for (let dx = -3; dx <= 3; dx++) {
      let total = 0, n = 0;
      for (let y = 80; y < h - 80; y += 2) {
        for (let x = 120; x < w - 120; x += 2) {
          const ax = x, bx = x + dx, ay = y, by = y + dy;
          if (bx < 0 || by < 0 || bx >= w || by >= h) continue;
          const i = (ay * w + ax) * 3, j = (by * w + bx) * 3;
          total += Math.abs(a.px[i] - b.px[j]) + Math.abs(a.px[i + 1] - b.px[j + 1]) + Math.abs(a.px[i + 2] - b.px[j + 2]);
          n++;
        }
      }
      const err = total / n;
      if (err < best.err) best = { dx, dy, err };
    }
  }
  return best;
}

function sequenceFrames(dir) {
  const files = readdirSync(dir).filter(n => /^frame_\d+_.*\.png$/.test(n));
  return files.sort();
}

// lee la telemetria frame->cam del run-report
function frameTelemetry(reportPath) {
  if (!existsSync(reportPath)) return new Map();
  try {
    const r = JSON.parse(readFileSync(reportPath, 'utf8'));
    const m = new Map();
    for (const f of (r.sequence?.frames || [])) {
      const fr = Number(f.runStatus?.frame ?? -1);
      if (fr >= 0) m.set(fr, { file: path.basename(f.path || ''), camX: (parseInt(f.runStatus?.detail ?? '0') >> 16) & 0xff });
    }
    return m;
  } catch { return new Map(); }
}

function runDemo() {
  const tag = `det-${Date.now()}`;
  execSync(`bash tools/run/run-demo.sh ${DEMO_ARG} --sequence-frames 12 --sequence-interval-ms 200 --settle-ms 500`, { cwd: ROOT, stdio: 'inherit' });
  // copiar la secuencia capturada a un directorio taggeado
  const src = path.join(OUT, 'sequence');
  const dst = path.join(OUT, `${tag}-seq`);
  if (existsSync(src)) {
    mkdirSync(dst, { recursive: true });
    for (const f of readdirSync(src)) {
      if (f.endsWith('.png')) copyFileSync(path.join(src, f), path.join(dst, f));
    }
  }
  return { dir: dst, report: path.join(OUT, 'run-report.json'), frames: sequenceFrames(dst) };
}

console.log(`Smoke determinismo ${DEMO} (threshold MAE ${THRESHOLD})`);
console.log('Ejecución 1...');
const run1 = runDemo();
console.log('Ejecución 2...');
const run2 = runDemo();

const t1 = frameTelemetry(run1.report), t2 = frameTelemetry(run2.report);
const seq1 = path.join(run1.dir), seq2 = path.join(run2.dir);

// emparejar frames por numero de frame del run-status
const pairs = [];
for (const [frame, info1] of t1) {
  const info2 = t2.get(frame);
  if (!info2) continue;
  const f1 = path.join(seq1, info1.file), f2 = path.join(seq2, info2.file);
  if (existsSync(f1) && existsSync(f2)) pairs.push({ frame, info1, info2, f1, f2 });
}

if (pairs.length === 0) {
  console.log('FAIL: no se pudieron emparejar frames entre las dos ejecuciones (mira la telemetria).');
  process.exit(1);
}

const diffs = [];
for (const p of pairs) {
  try {
    const s = bestShift(loadPng(p.f1), loadPng(p.f2));
    diffs.push({ frame: p.frame, camX1: p.info1.camX, camX2: p.info2.camX, diff: s.err, dx: s.dx, dy: s.dy });
  } catch { /* frame ilegible */ }
}

const maxDiff = diffs.reduce((m, d) => Math.max(m, d.diff), 0);
const identical = diffs.filter(d => d.diff < 0.01).length;
console.log(`\nPares comparados: ${diffs.length} (máx MAE tras shift=${maxDiff.toFixed(2)}, idénticos=${identical})`);
for (const d of diffs.slice(0, 8)) console.log(`  frame=${d.frame} camX=${d.camX1}/${d.camX2} mae=${d.diff.toFixed(2)} shift=(${d.dx},${d.dy})`);

if (diffs.length >= 2 && maxDiff <= THRESHOLD) {
  console.log(`\nSMOKE OK: la demo es determinista (MAE ${maxDiff.toFixed(2)} <= ${THRESHOLD}).`);
  process.exit(0);
} else {
  console.log(`\nSMOKE FAIL: render no determinista o frames desalineados (MAE ${maxDiff.toFixed(2)} > ${THRESHOLD}).`);
  process.exit(1);
}
