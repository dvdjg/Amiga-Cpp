#!/usr/bin/env node
// Perfil por SECCIONES de una demo en marcha: lee `eng::debug::g_eng_prof` (contadores de
// ciclos por seccion, ver engine/include/eng/debug/prof.hpp) y lo convierte a ciclos/frame
// y % del frame. Complementa a measure-fps.mjs, que da el total entre frames; la
// diferencia total - suma(secciones) es la espera de VBlank + render + bucle.
//
// Uso:
//   bash ./tools/run/run-demo.sh demos/amiga/<demo>      # deja el runner.uae listo
//   node tools/debug/profile.mjs <demo> [CONFIG] [segundos]
//
// Puertos: WINUAE_GDB_PORT (2345) y WINUAE_SIDE_CHANNEL_PORT (2346).
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import { sideChannelCommand } from '../../../mcp-winuae-emu/dist/side-channel.js';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMO = process.argv[2];
if (!DEMO) {
  console.error('Uso: node tools/debug/profile.mjs <demo> [config] [segundos]');
  process.exit(1);
}
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const SECONDS = parseFloat(process.argv[4] || '5');
const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
const SIDE_PORT = parseInt(process.env.WINUAE_SIDE_CHANNEL_PORT || '2346', 10);
const MAP = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.map`;
const CPU_HZ = 7093790;
const FIELD = CPU_HZ / 50;
const PROF_MAGIC = 0x50524f46;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function findMapSymbol(name) {
  if (!fs.existsSync(MAP)) return null;
  // Tolerante al manglado: `g_eng_prof` puede aparecer como `_ZN3eng6debug10g_eng_profE`.
  const re = new RegExp('^\\s*0x([0-9a-fA-F]+)\\s+\\S*' + name + '\\S*\\s*$');
  for (const line of fs.readFileSync(MAP, 'utf8').split(/\r?\n/g)) {
    const m = line.match(re);
    if (m) return parseInt(m[1], 16);
  }
  return null;
}
function mapSections() {
  const wanted = new Set(['.text', '.rodata', '.eh_frame', '.data', '.bss']);
  const out = [];
  for (const line of fs.readFileSync(MAP, 'utf8').split(/\r?\n/g)) {
    const m = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!m || !wanted.has(m[1])) continue;
    const start = parseInt(m[2], 16), size = parseInt(m[3], 16);
    if (size === 0) continue;
    out.push({ start, size, end: start + size });
  }
  return out;
}
function runtimeAddr(linked, ms, rs) {
  if (!Array.isArray(rs) || rs.length === 0) return null;
  const cand = ms.find((s) => linked >= s.start && linked < s.end);
  if (!cand) return parseInt(rs[0], 16) + (linked - 0x400);
  const idx = ms.indexOf(cand);
  if (rs.length <= idx) return null;
  return parseInt(rs[idx], 16) + (linked - cand.start);
}
const u32delta = (a, b) => { let d = b - a; if (d < 0) d += 4294967296; return d; };

const SECTION_NAMES = ['actors', 'blits', 'copper', 'static', 'sky', 'objcopper', 'materialize', 'sort_lines', 'sort_prio', 'emit', 'calib', 'loop'];
// Los nombres de seccion son por demo. `--names a,b,c` los fija (indice 0..); si no, se usa
// el mapa generico de arriba y, para indices sin nombre, `seccionN`.
const namesArgIdx = process.argv.indexOf('--names');
const CUSTOM_NAMES = (namesArgIdx >= 0 && process.argv[namesArgIdx + 1])
  ? process.argv[namesArgIdx + 1].split(',')
  : null;

// WinUAE de la extension Bartman: autodetecta la version MAS NUEVA instalada (fijar una antigua
// colgaba el arranque de algunas demos y con ello fallaban las lecturas GDB).
const EXT_BIN = (() => {
  const base = `${process.env.USERPROFILE || process.env.HOME || ''}/.vscode/extensions`;
  try {
    const dirs = fs.readdirSync(base).filter((d) => d.startsWith('bartmanabyss.amiga-debug-')).sort();
    if (dirs.length) return `${base}/${dirs[dirs.length - 1]}/bin`;
  } catch { /* fallback */ }
  return `${base}/bartmanabyss.amiga-debug-1.8.2/bin`;
})();
const conn = new WinUAEConnection({
  winuaePath: `${EXT_BIN}/win32`,
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: GDB_PORT,
});
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
await sleep(3000);

const st = await sideChannelCommand('state', SIDE_PORT, 5000);
const linked = findMapSymbol('g_eng_prof');
// Lectura por el **canal lateral** (`mem`): el `readMemory` GDB da timeouts en este entorno.
// Devuelve un Buffer con los bytes (los enteros son big-endian en memoria Amiga).
const readMem = async (a, len) => {
  try {
    const r = await sideChannelCommand('mem 0x' + a.toString(16) + ' ' + len, SIDE_PORT, 2500);
    const hex = (r && r.reply && typeof r.reply.data === 'string') ? r.reply.data : null;
    return hex ? Buffer.from(hex, 'hex') : null;
  } catch { return null; }
};
const profOk = async (a) => {
  if (!a) return false;
  const b = await readMem(a, 4);
  return !!b && b.length >= 4 && b.readUInt32BE(0) === PROF_MAGIC;
};
// Resolucion robusta: (1) MISMO offset en cada seccion runtime (los hunks runtime no casan
// con el `.map`); (2) inicio de cada seccion; (3) ESCANEO por el magic `PROF` dentro de cada
// seccion (necesario cuando el simbolo cae en `.gnu.linkonce.b`, que el `.map` lista aparte de
// `.bss` y por tanto los offsets por indice no casan). Mismo criterio que `measure-fps.mjs`.
const ms = mapSections();
const rs = st.reply.sections;
const cand = (linked !== null && Array.isArray(ms)) ? ms.find((s) => linked >= s.start && linked < s.end) : null;
const off = cand ? (linked - cand.start) : (linked !== null ? (linked - 0x400) : 0);
let addr = null;
if (Array.isArray(rs)) {
  for (const sec of rs) {
    const a = parseInt(sec, 16) + off;
    if (await profOk(a)) { addr = a; break; }
  }
  if (addr === null) {
    for (const sec of rs) {
      const a = parseInt(sec, 16);
      if (a && await profOk(a)) { addr = a; break; }
    }
  }
  // Escaneo por magic: lee bloques grandes de cada seccion y busca 'PROF' alineado a 4.
  if (addr === null) {
    const CHUNK = 4096;
    for (const sec of rs) {
      const base = parseInt(sec, 16);
      if (!base) continue;
      for (let read = 0; read < 0x20000; read += CHUNK) {
        const b = await readMem(base + read, CHUNK);
        if (!b || b.length < 4) break;
        for (let i = 0; i + 4 <= b.length; i += 4) {
          if (b.readUInt32BE(i) === PROF_MAGIC) { addr = base + read + i; break; }
        }
        if (addr !== null) break;
      }
      if (addr !== null) break;
    }
  }
}
if (addr === null) {
  console.error('[prof] no se pudo resolver g_eng_prof (¿map? ¿sections?)');
  await conn.disconnect(true);
  process.exit(1);
}

// magic(4)+sections(1)+pad(3)+frames(4) + cycles[16] + calls[16] + min[16] + max[16]
const BLOCK = 12 + 16 * 4 * 4;
async function sample() {
  const b = await readMem(addr, BLOCK);
  if (!b || b.length < BLOCK || b.readUInt32BE(0) !== PROF_MAGIC) return null;
  const sections = b.readUInt8(4);
  const frames = b.readUInt32BE(8);
  const cycles = [], calls = [], minc = [], maxc = [];
  const base = 12;
  for (let i = 0; i < 16; ++i) {
    cycles.push(b.readUInt32BE(base + i * 4));
    calls.push(b.readUInt32BE(base + 64 + i * 4));
    minc.push(b.readUInt32BE(base + 128 + i * 4));
    maxc.push(b.readUInt32BE(base + 192 + i * 4));
  }
  // Contador de ciclos = periferico de depuracion (0xB7E928). Si el canal lateral no lo lee,
  // se intenta por GDB y si no queda 0 (se pierde el `%` del frame, no la tabla por seccion).
  let clock = 0;
  const cm = await readMem(0xb7e928, 4);
  if (cm && cm.length >= 4) {
    clock = cm.readUInt32BE(0);
  } else {
    try { clock = (await p.readMemory(0xb7e928, 4)).readUInt32BE(0); } catch { clock = 0; }
  }
  return { sections, frames, cycles, calls, minc, maxc, clock };
}

const a = await sample();
await sleep(SECONDS * 1000);
const z = await sample();
if (!a || !z) {
  console.error('[prof] bloque no valido (¿falta ENG_PROF_INIT?)');
  await conn.disconnect(true);
  process.exit(1);
}

const frames = Math.max(1, u32delta(a.frames, z.frames));
const total = u32delta(a.clock, z.clock) / frames;
console.log(`[prof] ${DEMO}/${CONFIG_NAME} | ${frames} frames | total ${total.toFixed(0)} ciclos/frame (${(total / FIELD).toFixed(2)} campos)`);
console.log('  seccion   ciclos/frame      %   llam/frame   min/llam   max/llam');
let sum = 0;
for (let i = 0; i < Math.max(a.sections, 10); ++i) {
  const perFrame = u32delta(a.cycles[i], z.cycles[i]) / frames;
  const calls = u32delta(a.calls[i], z.calls[i]) / frames;
  // min/max del bloque `z` (ultimo estado) ya son por-llamada; si la seccion no se
  // ejecuto, son 0.
  const minc = z.minc[i];
  const maxc = z.maxc[i];
  sum += perFrame;
  const name = (CUSTOM_NAMES && CUSTOM_NAMES[i]) || SECTION_NAMES[i] || `seccion${i}`;
  console.log(`  ${name.padEnd(8)} ${perFrame.toFixed(0).padStart(8)} ${(100 * perFrame / total).toFixed(1).padStart(6)}% ${calls.toFixed(1).padStart(10)} ${String(minc).padStart(11)} ${String(maxc).padStart(11)}`);
}
const rest = total - sum;
console.log(`  ${'(resto)'.padEnd(8)} ${rest.toFixed(0).padStart(8)} ${(100 * rest / total).toFixed(1).padStart(6)}%  (espera VBlank + render + bucle)`);
await conn.disconnect(true);
process.exit(0);
