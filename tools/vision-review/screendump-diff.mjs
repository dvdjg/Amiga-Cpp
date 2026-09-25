#!/usr/bin/env node
// Diff de **buffers gráficos** (framebuffer real, por planos) leídos por el canal lateral de
// WinUAE-DBG (`mem <addr> <len>`), sin depender del PNG escalado. Es la comparación "buffer a
// buffer" para análisis diferencial de pantalla: lee la base de bitplanes y la geometría, decodifica
// cada plano y compara dos momentos (A vs B) por píxel, con resumen por plano y por bloques.
//
// Uso:
//   node tools/vision-review/screendump-diff.mjs \
//     --addr <hex-base> --planes 6 --row-bytes 40 --plane-bytes 10240 \
//     --width 320 --height 256 [--plane-stride <bytes=plane-bytes>] [--gap-ms 500]
//     [--side-port 2346] [--thresh 1] [--json]
//
// Salida: `out/vision-review/screendump-diff/{json,md}` + por pantalla. Código: 0 ok, 2 uso, 1 error.
//
// El canal lateral debe estar activo (demo corriendo con `run-demo`). Requiere `pngjs` no: el diff
// es por bits de plano (no hay imagen). Ver docs/emulation/WINUAE_SIDE_CHANNEL_DEBUG.md.

import * as fs from 'node:fs';
import * as path from 'node:path';
import * as net from 'node:net';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const arg = (n, fb) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fb; };
const has = (n) => process.argv.includes(n);

const addr = parseInt(arg('--addr', ''), 16);
const planes = parseInt(arg('--planes', '0'), 10);
const rowBytes = parseInt(arg('--row-bytes', '0'), 10);
const width = parseInt(arg('--width', '0'), 10);
const height = parseInt(arg('--height', '0'), 10);
const planeBytes = parseInt(arg('--plane-bytes', String(rowBytes * height)), 10);
const planeStride = parseInt(arg('--plane-stride', String(planeBytes)), 10);
const gapMs = parseInt(arg('--gap-ms', '500'), 10);
const sidePort = parseInt(arg('--side-port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
const thresh = parseInt(arg('--thresh', '1'), 10);

if (!Number.isFinite(addr) || planes <= 0 || rowBytes <= 0 || width <= 0 || height <= 0) {
  console.error('Uso: screendump-diff.mjs --addr <hex> --planes N --row-bytes N --width N --height N [--plane-bytes N] [--gap-ms N] [--side-port N]');
  process.exit(2);
}

// --- Cliente mínimo del canal lateral (una orden, una respuesta JSON) ---
function sideCommand(cmd, port) {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection({ host: '127.0.0.1', port });
    sock.setEncoding('utf8');
    let pending = '';
    let greeting = false;
    const timer = setTimeout(() => { sock.destroy(); reject(new Error('timeout del canal lateral')); }, 5000);
    sock.on('data', (chunk) => {
      pending += chunk;
      let eol;
      while ((eol = pending.indexOf('\n')) >= 0) {
        const line = pending.slice(0, eol).trim();
        pending = pending.slice(eol + 1);
        if (!line) continue;
        if (!greeting) { greeting = true; sock.write(cmd + '\n'); continue; } // consume saludo y envía
        clearTimeout(timer);
        sock.end();
        try { resolve(JSON.parse(line)); } catch (e) { reject(new Error('respuesta no-JSON: ' + line.slice(0, 120))); }
        return;
      }
    });
    sock.on('connect', () => {
      // Si el servidor no envía saludo en 200 ms, envía la orden igualmente.
      setTimeout(() => { if (!greeting) { greeting = true; sock.write(cmd + '\n'); } }, 200);
    });
    sock.on('error', (e) => { clearTimeout(timer); reject(e); });
  });
}

/// Lee `len` bytes desde `addr` y devuelve un Buffer (el canal devuelve hex; se lee por tramos).
async function readMem(addr, len, port) {
  const out = Buffer.alloc(len);
  const chunk = 1024; // tramos para no exceder límites del servidor
  for (let off = 0; off < len; off += chunk) {
    const n = Math.min(chunk, len - off);
    const resp = await sideCommand(`mem ${(addr + off).toString(16)} ${n}`, port);
    const hex = (resp && resp.data) ? String(resp.data) : '';
    if (!hex || !/^[0-9a-fA-F]+$/.test(hex)) {
      throw new Error(`mem devolvió datos no-hex en 0x${(addr + off).toString(16)}: ${hex.slice(0, 40)}`);
    }
    const buf = Buffer.from(hex, 'hex');
    if (buf.length < n) throw new Error(`mem devolvió ${buf.length} < ${n} bytes`);
    buf.copy(out, off, 0, n);
  }
  return out;
}

/// Decodifica el color de un píxel desde los planos (contiguo: plano p en base + p*planeStride).
function pixel(mem, baseOff, x, y) {
  let c = 0;
  const byteIdx = baseOff + y * rowBytes + (x >> 3);
  const bit = 7 - (x & 7);
  for (let p = 0; p < planes; p++) {
    const b = mem[byteIdx + p * planeStride];
    c |= ((b >> bit) & 1) << p;
  }
  return c;
}

const baseA = await readMem(addr, planes * planeStride, sidePort).catch((e) => { console.error(`[screendump-diff] lectura A falló: ${e.message}`); process.exit(1); });
await new Promise((r) => setTimeout(r, gapMs));
const baseB = await readMem(addr, planes * planeStride, sidePort).catch((e) => { console.error(`[screendump-diff] lectura B falló: ${e.message}`); process.exit(1); });

// Diff por píxel (Buffer es memoria contigua; la decodificación planar es simple pero rápida).
let changed = 0;
const perPlane = new Array(planes).fill(0);
const block = 16;
const bw = Math.ceil(width / block), bh = Math.ceil(height / block);
const blocks = new Array(bw * bh).fill(0);
let minx = width, miny = height, maxx = -1, maxy = -1;
for (let y = 0; y < height; y++) {
  for (let x = 0; x < width; x++) {
    const ca = pixel(baseA, 0, x, y);
    const cb = pixel(baseB, 0, x, y);
    const d = Math.abs(ca - cb);
    if (d > thresh) {
      changed++;
      blocks[(y / block | 0) * bw + (x / block | 0)]++;
      if (x < minx) minx = x; if (x > maxx) maxx = x;
      if (y < miny) miny = y; if (y > maxy) maxy = y;
      for (let p = 0; p < planes; p++) {
        const ba = baseA[y * rowBytes + (x >> 3) + p * planeStride];
        const bb = baseB[y * rowBytes + (x >> 3) + p * planeStride];
        if ((((ba >> (7 - (x & 7))) & 1) !== ((bb >> (7 - (x & 7))) & 1))) perPlane[p]++;
      }
    }
  }
}
const total = width * height;
const hotBlocks = blocks.map((v, i) => ({ v, x: (i % bw) * block, y: (i / bw | 0) * block }))
  .filter((b) => b.v > 0).sort((a, b) => b.v - a.v).slice(0, 8);

const outDir = path.join(ROOT, 'out/vision-review/screendump-diff');
fs.mkdirSync(outDir, { recursive: true });
const report = {
  addr: '0x' + addr.toString(16), planes, width, height, rowBytes, planeStride, gapMs, thresh,
  changed, total, pct: round(changed / total * 100), bbox: maxx >= 0 ? [minx, miny, maxx, maxy] : null,
  perPlane, hotBlocks,
};
fs.writeFileSync(path.join(outDir, 'screendump-diff.json'), JSON.stringify(report, null, 2), 'utf8');
const md = [
  '# Diff de buffer gráfico (canal lateral)',
  '',
  `Base 0x${addr.toString(16)} · ${planes} planos · ${width}×${height} · row_bytes ${rowBytes} · gap ${gapMs} ms`,
  '',
  `Píxeles cambiados: **${changed}/${total}** (${report.pct}%)${report.bbox ? ` · bbox ${report.bbox.join(',')}` : ''}`,
  '',
  '| plano | píxeles cambiados |',
  '|---|---|',
  ...perPlane.map((v, p) => `| ${p} | ${v} |`),
  '',
  'Bloques calientes (x,y): ' + hotBlocks.map((b) => `(${b.x},${b.y})=${b.v}`).join(' ') || '—',
].join('\n');
fs.writeFileSync(path.join(outDir, 'screendump-diff.md'), md, 'utf8');

function round(v) { return Math.round(v * 100) / 100; }
if (has('--json')) {
  console.log(JSON.stringify(report, null, 2));
} else {
  console.log(`[screendump-diff] 0x${addr.toString(16)}: ${changed}/${total} px cambiados (${report.pct}%)`);
  console.log(`  por plano: ${perPlane.join(' ')}`);
  console.log(`  informe: out/vision-review/screendump-diff/screendump-diff.md`);
}
process.exit(0);
