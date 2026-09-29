#!/usr/bin/env node
// Auto-test de `screendump-diff.mjs` **sin emulador**: arranca un servidor TCP local que responde
// a las órdenes `mem <addr> <len>` con buffers sintéticos (dos snapshots de un framebuffer 6bpp
// conocidos) y comprueba que el diff detecta el cambio esperado (píxeles y planos correctos).
//
// Uso: node tools/vision-review/selftest-screendump.mjs [--require-ok]
// Salida: out/vision-review/selftest-screendump.md. Código: 0 ok, 4 fallo informativo, 1 con --require-ok.

import * as fs from 'node:fs';
import * as net from 'node:net';
import * as path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const has = (n) => process.argv.includes(n);

const W = 32, H = 16, PLANES = 4, ROW = 4; // 32 px = 4 bytes/fila
const PLANE = ROW * H; // 64 bytes por plano
const BASE = 0x20000;

// Snapshot A y B: A es color 0/1 (patrón), B cambia el píxel (0,0) de color 1 a color 2 (plano 1).
function makeSnapshots() {
  const a = Buffer.alloc(PLANES * PLANE);
  const b = Buffer.alloc(PLANES * PLANE);
  // A: plano 0 con un patrón (columna 0 a 1 en todas las filas), resto 0.
  for (let y = 0; y < H; y++) a[y * ROW] = 0x80; // bit de columna 0
  b.set(a);
  // B: añade el bit de plano 1 en el píxel (0,0) → color pasa de 1 a 3.
  b[0 + 1 * PLANE] = 0x80;
  return { a, b };
}

const { a: snapA, b: snapB } = makeSnapshots();
let reads = 0;

// Servidor que responde `mem <addr> <len>` en hex; la 1ª lectura da A, la 2ª da B.
const server = net.createServer((sock) => {
  sock.setEncoding('utf8');
  sock.write('READY\n'); // saludo
  let pending = '';
  sock.on('data', (chunk) => {
    pending += chunk;
    let eol;
    while ((eol = pending.indexOf('\n')) >= 0) {
      const line = pending.slice(0, eol).trim();
      pending = pending.slice(eol + 1);
      const m = line.match(/^mem ([0-9a-fA-F]+) (\d+)$/);
      if (!m) { sock.write(JSON.stringify({ ok: false, error: 'cmd' }) + '\n'); continue; }
      const addr = parseInt(m[1], 16);
      const len = parseInt(m[2], 10);
      // Registros de display activos: `--from-copper` los consulta primero.
      if (addr === 0xdff0e0) { sock.write(JSON.stringify({ ok: true, data: BASE.toString(16).padStart(8, '0') }) + '\n'); continue; }
      if (addr === 0xdff108) { sock.write(JSON.stringify({ ok: true, data: '0000' }) + '\n'); continue; }
      const snap = reads++ === 0 ? snapA : snapB;
      const off = addr - BASE;
      const slice = snap.subarray(Math.max(0, off), Math.max(0, off) + len);
      sock.write(JSON.stringify({ ok: true, data: slice.toString('hex') }) + '\n');
    }
  });
  sock.on('error', () => {});
});

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const port = 2500 + Math.floor(Math.random() * 100);
await new Promise((r) => server.listen(port, '127.0.0.1', r));

try {
  // `spawn` (async): el servidor fake vive en ESTE proceso, así que no puede bloquearse mientras
  // el cliente corre en un proceso hijo.
  const args = [
    path.join(ROOT, 'tools/vision-review/screendump-diff.mjs'),
    '--addr', BASE.toString(16), '--planes', String(PLANES), '--row-bytes', String(ROW),
    '--width', String(W), '--height', String(H), '--gap-ms', '0', '--side-port', String(port),
    '--json',
  ];
  const out = await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, args, { encoding: 'utf8' });
    let stdout = '', stderr = '';
    child.stdout.on('data', (d) => { stdout += d; });
    child.stderr.on('data', (d) => { stderr += d; });
    child.on('error', reject);
    child.on('close', (code) => (code === 0 ? resolve(stdout) : reject(new Error(stderr || `exit ${code}`))));
  });
  const j = JSON.parse(out);
  check(j.changed === 1, `detecta 1 pixel cambiado (fue ${j.changed})`);
  check(j.perPlane && j.perPlane[1] === 1, `cambio en el plano 1 (fue [${j.perPlane}])`);
  check(j.bbox && j.bbox[0] === 0 && j.bbox[1] === 0, `bbox en (0,0) (fue ${JSON.stringify(j.bbox)})`);
  check(j.total === W * H, `total de pixeles correcto (${j.total})`);

  // Segunda pasada con `--from-copper`: debe deducir la base del registro BPL1PT activo.
  const args2 = [
    path.join(ROOT, 'tools/vision-review/screendump-diff.mjs'),
    '--from-copper', '--planes', String(PLANES),
    '--width', String(W), '--height', String(H), '--gap-ms', '0', '--side-port', String(port),
    '--json',
  ];
  const out2 = await new Promise((resolve, reject) => {
    const child = spawn(process.execPath, args2, { encoding: 'utf8' });
    let stdout = '', stderr = '';
    child.stdout.on('data', (d) => { stdout += d; });
    child.stderr.on('data', (d) => { stderr += d; });
    child.on('error', reject);
    child.on('close', (code) => (code === 0 ? resolve(stdout) : reject(new Error(stderr || `exit ${code}`))));
  });
  const j2 = JSON.parse(out2);
  check(j2.addr === '0x' + BASE.toString(16), `--from-copper deduce la base del registro (fue ${j2.addr})`);
  check(j2.rowBytes === ROW, `--from-copper deduce row_bytes (fue ${j2.rowBytes})`);
} catch (e) {
  check(false, `ejecucion falló: ${(e.stderr || e.message || '').toString().split('\n')[0]}`);
}
server.close();

const md = `# Auto-test screendump-diff\n\nResultado: ${failures === 0 ? 'OK' : failures + ' fallo(s)'}\n`;
fs.mkdirSync(path.join(ROOT, 'out/vision-review'), { recursive: true });
fs.writeFileSync(path.join(ROOT, 'out/vision-review/selftest-screendump.md'), md, 'utf8');

if (failures === 0) { console.log('OK: screendump-diff (auto-test) validado.'); process.exit(0); }
console.log(`FALLO: ${failures} comprobacion(es).`);
process.exit(has('--require-ok') ? 1 : 4);
