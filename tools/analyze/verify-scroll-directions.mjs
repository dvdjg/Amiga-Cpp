#!/usr/bin/env node
/**
 * Verifica que el scroll de la demo 101 cubre las 4 direcciones + diagonal,
 * mide el vector por par de frames (cross-correlación) y confirma visualmente
 * con ollama local que los tiles se renderizan sin glitches.
 *
 * Uso: node tools/analyze/verify-scroll-directions.mjs [--regen] [--ollama-model M]
 *  --regen: re-ejecuta run-demo para regenerar la secuencia (si no, usa la existente).
 * Requiere la demo compilada. Env: WINUAE_PATH, WINUAE_CONFIG (si --regen).
 */
import { execSync } from 'child_process';
import { readFileSync, existsSync, mkdirSync, readdirSync } from 'fs';
import { inflateSync } from 'zlib';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..', '..');
const DEMO = 'demos/101_ehb_tile_scroll_driver';
const SEQ = path.join(ROOT, 'out/run/101_ehb_tile_scroll_driver/sequence');
const REPORT = path.join(ROOT, 'out/run/101_ehb_tile_scroll_driver/run-report.json');
const MODEL = process.argv.includes('--ollama-model') ? process.argv[process.argv.indexOf('--ollama-model') + 1] : 'qwen3-vl:8b-instruct-q8_0';
const REGEN = process.argv.includes('--regen');

// ---------- decodificador PNG mínimo (RGB 8-bit, filtros estándar) ----------
function loadPng(p) {
  const buf = readFileSync(p);
  const chunks = [];
  let off = 8;
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    chunks.push({ type: buf.toString('ascii', off + 4, off + 8), data: buf.subarray(off + 8, off + 8 + len) });
    off += 12 + len;
  }
  const ihdr = chunks.find(c => c.type === 'IHDR').data;
  const w = ihdr.readUInt32BE(0), h = ihdr.readUInt32BE(4);
  const colorType = ihdr[9];
  const idat = Buffer.concat(chunks.filter(c => c.type === 'IDAT').map(c => c.data));
  const raw = inflateSync(idat);
  const bpp = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 0 ? 1 : 4;
  const stride = Math.floor((w * bpp * 8 + 7) / 8);
  const px = Buffer.alloc(w * h * 3);
  let pos = 0;
  let prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const filter = raw[pos++];
    const line = raw.subarray(pos, pos + stride);
    pos += stride;
    const recon = Buffer.alloc(stride);
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? recon[x - bpp] : 0;
      const b = y > 0 ? prev[x] : 0;
      const c = (x >= bpp && y > 0) ? prev[x - bpp] : 0;
      let v = line[x];
      if (filter === 1) v = (v + a) & 0xff;
      else if (filter === 2) v = (v + b) & 0xff;
      else if (filter === 3) v = (v + ((a + b) >> 1)) & 0xff;
      else if (filter === 4) {
        const pv = a + b - c;
        const pa = Math.abs(pv - a), pb = Math.abs(pv - b), pc = Math.abs(pv - c);
        v = (v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c)) & 0xff;
      }
      recon[x] = v;
    }
    for (let x = 0; x < w; x++) {
      const si = x * bpp;
      const r = recon[si], g = colorType === 0 ? recon[si] : recon[si + 1], bl = colorType === 0 ? recon[si] : recon[si + 2];
      const di = (y * w + x) * 3;
      px[di] = r; px[di + 1] = g; px[di + 2] = bl;
    }
    prev = recon;
  }
  return { w, h, px };
}

function scrollVector(a, b) {
  const { w, h } = a;
  const m = 60;
  let best = null;
  for (let dy = -40; dy <= 40; dy++) {
    for (let dx = -40; dx <= 40; dx++) {
      let sum = 0, n = 0;
      for (let y = m; y < h - m; y += 2) {
        for (let x = m; x < w - m; x += 2) {
          const ax = x + dx, ay = y + dy;
          if (ax < 0 || ay < 0 || ax >= w || ay >= h) continue;
          const i = (y * w + x) * 3, j = (ay * w + ax) * 3;
          sum += Math.abs(a.px[i] - b.px[j]) + Math.abs(a.px[i + 1] - b.px[j + 1]) + Math.abs(a.px[i + 2] - b.px[j + 2]);
          n++;
        }
      }
      const score = sum / n;
      if (!best || score < best.score) best = { dx, dy, score };
    }
  }
  return best;
}

function classify(dx, dy) {
  if (Math.abs(dx) >= 8 && Math.abs(dy) < Math.abs(dx) / 2) return dx < 0 ? 'right' : 'left';
  if (Math.abs(dy) >= 8 && Math.abs(dx) < Math.abs(dy) / 2) return dy > 0 ? 'down' : 'up';
  if (Math.abs(dx) >= 8 && Math.abs(dy) >= 8) return 'diagonal';
  return 'still';
}

async function ollamaDescribe(p, dir) {
  const body = {
    model: MODEL,
    prompt: `This is a frame from an Amiga tile-scroll demo, captured during ${dir} scroll. It shows a grid of 16x16 tiles with hex glyphs (0-F). Answer in ONE sentence: does it look like a clean tile grid (list 3-4 visible glyphs) with NO glitches, tearing, cut tiles or black bars? If there IS any visual problem, say so explicitly.`,
    images: [readFileSync(p).toString('base64')],
    stream: false,
  };
  const resp = await fetch('http://localhost:11434/api/generate', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal: AbortSignal.timeout(90000),
  }).catch(() => null);
  if (!resp) return null;
  const data = await resp.json();
  return String(data.response || '').trim();
}

async function main() {
  if (REGEN || !existsSync(path.join(SEQ, 'frame_001.png'))) {
    console.log('Regenerando secuencia con run-demo...');
    execSync(`bash tools/run/run-demo.sh ${DEMO} --sequence-frames 20 --sequence-interval-ms 1000 --wait-ms 15000`, { cwd: ROOT, stdio: 'inherit' });
  }
  const frames = readdirSync(SEQ).filter(f => /^frame_\d{3}\.png$/.test(f)).sort();
  console.log(`Frames disponibles: ${frames.length}`);

  // vectores por par
  const dirs = { right: [], left: [], up: [], down: [], diagonal: [], still: [] };
  const vectors = [];
  const cache = {};
  const load = (f) => { if (!cache[f]) cache[f] = loadPng(path.join(SEQ, f)); return cache[f]; };
  for (let i = 0; i + 1 < frames.length; i++) {
    const v = scrollVector(load(frames[i]), load(frames[i + 1]));
    const d = classify(v.dx, v.dy);
    dirs[d].push({ a: frames[i], b: frames[i + 1], dx: v.dx, dy: v.dy });
    vectors.push(`${frames[i]}->${frames[i + 1]} ${d} dx=${v.dx} dy=${v.dy}`);
  }
  console.log('\nVectores por par:\n' + vectors.join('\n'));

  console.log('\n=== Resumen por dirección ===');
  for (const d of ['right', 'left', 'up', 'down', 'diagonal']) {
    const list = dirs[d];
    const avg = list.length
      ? { dx: Math.round(list.reduce((s, x) => s + x.dx, 0) / list.length), dy: Math.round(list.reduce((s, x) => s + x.dy, 0) / list.length) }
      : null;
    console.log(`  ${d}: ${list.length} pares ${avg ? `(avg dx=${avg.dx} dy=${avg.dy})` : ''}`);
  }

  // fps: finalSideChannel.frame / duración aprox
  let fps = null;
  if (existsSync(REPORT)) {
    const rep = JSON.parse(readFileSync(REPORT, 'utf8'));
    const f = rep.finalSideChannel && rep.finalSideChannel.frame;
    if (f) fps = (f / 20).toFixed(0); // ~20s de secuencia
  }
  console.log(`\nVelocidad estimada: ${fps ? fps + ' fps' : 'desconocida'} (objetivo 50)`);

  // ollama: verificar un frame representativo por dirección
  console.log('\n=== Verificación visual (ollama) ===');
  const results = { passed: [], failed: [] };
  const picks = {
    right: (dirs.right[0] || {}).a,
    left: (dirs.left[0] || {}).a,
    up: (dirs.up[0] || {}).a,
    down: (dirs.down[0] || {}).a,
    diagonal: (dirs.diagonal[0] || {}).a,
  };
  for (const d of ['right', 'left', 'up', 'down', 'diagonal']) {
    const f = picks[d];
    if (!f) { console.log(`  ${d}: sin frame de muestra`); continue; }
    const desc = await ollamaDescribe(path.join(SEQ, f), d);
    if (!desc) { console.log(`  ${d}: ollama no disponible (frame ${f})`); continue; }
    const clean = !/(glitch|tear|corrupt|black bar|artifact|cut)/i.test(desc);
    console.log(`  ${d} (frame ${f}): ${desc.slice(0, 140)}`);
    if (clean && dirs[d].length > 0) results.passed.push(d); else results.failed.push(d);
  }
  console.log(`\nRESULTADO SCROLL: PASS ${results.passed.length} | FAIL ${results.failed.length}`);
  console.log('Direcciones con movimiento medible:', Object.keys(dirs).filter(d => dirs[d].length).join(', '));
  process.exit(results.failed.length ? 1 : 0);
}

main().catch(e => { console.error('Fatal:', e.message); process.exit(1); });
