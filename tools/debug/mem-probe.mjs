#!/usr/bin/env node
// Sonda de reservas de memoria: lee `eng::debug::g_mem_probe` (ver
// engine/include/eng/debug/mem_probe.hpp) de una demo en marcha y muestra el estado de los bancos
// Chip/Slow/Fast más el último fallo de reserva de cada uno.
//
// Uso:
//   bash ./tools/run/run-demo.sh demos/amiga/<demo>    # deja el runner.uae listo
//   node tools/debug/mem-probe.mjs <demo> [CONFIG]
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
  console.error('Uso: node tools/debug/mem-probe.mjs <demo> [CONFIG]');
  process.exit(1);
}
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const MAP = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.map`;
const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
const SIDE_PORT = parseInt(process.env.WINUAE_SIDE_CHANNEL_PORT || '2346', 10);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const MEM_MAGIC = 0x4d454d00;

function findMapSymbol(name) {
  if (!fs.existsSync(MAP)) return null;
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
await conn.getProtocol().continue();
await sleep(9000);

const readMem = async (a, len) => {
  try {
    const r = await sideChannelCommand('mem 0x' + a.toString(16) + ' ' + len, SIDE_PORT, 3000);
    const hex = r && r.reply && typeof r.reply.data === 'string' ? r.reply.data : null;
    return hex ? Buffer.from(hex, 'hex') : null;
  } catch { return null; }
};

const linked = findMapSymbol('g_mem_probe');
const ms = mapSections();
const st = await sideChannelCommand('state', SIDE_PORT, 5000);
const rs = st && st.reply && st.reply.sections;

// Resolución: (1) offset por sección; (2) escaneo por magic en cada sección runtime.
let addr = null;
if (Array.isArray(rs)) {
  const cand = ms.find((s) => linked >= s.start && linked < s.end);
  const off = cand ? linked - cand.start : linked - 0x400;
  for (const sec of rs) {
    const a = parseInt(sec, 16) + off;
    const b = await readMem(a, 4);
    if (b && b.readUInt32BE(0) === MEM_MAGIC) { addr = a; break; }
  }
}
if (addr === null && Array.isArray(rs)) {
  for (let round = 0; round < 20 && addr === null; ++round) {
    for (const sec of rs) {
      const base = parseInt(sec, 16);
      if (!base) continue;
      for (let read = 0; read < 0x40000; read += 4096) {
        const b = await readMem(base + read, 4096);
        if (!b || b.length < 4) break;
        for (let i = 0; i + 4 <= b.length; i += 4) {
          if (b.readUInt32BE(i) === MEM_MAGIC) { addr = base + read + i; break; }
        }
        if (addr !== null) break;
      }
      if (addr !== null) break;
    }
  }
}
if (addr === null) {
  console.error('[mem] no se pudo resolver g_mem_probe (¿map? ¿sections?)');
  await conn.disconnect(true);
  process.exit(1);
}

// Layout: magic(4) generation(4) bank_count(1) failures_valid(1) pad(2) | banks[3] (24 B) | failures[3] (16 B)
const BLOCK = 4 + 4 + 4 + 3 * 24 + 3 * 16;
const b = await readMem(addr, BLOCK);
if (!b || b.readUInt32BE(0) !== MEM_MAGIC) {
  console.error('[mem] bloque con magic inesperado');
  await conn.disconnect(true);
  process.exit(1);
}
const generation = b.readUInt32BE(4);
const failuresValid = b.readUInt8(9);
const NAMES = ['Chip', 'Slow', 'Fast'];
const STATUS = ['Ok', 'BankAbsent', 'NoSpace', 'Fragmented'];
console.log(`[mem] ${DEMO}/${CONFIG_NAME} | generación=${generation} | fallos=${failuresValid ? 'sí' : 'no'}`);
for (let i = 0; i < 3; ++i) {
  const o = 12 + i * 24;
  const capacity = b.readUInt32BE(o);
  const used = b.readUInt32BE(o + 4);
  const free = b.readUInt32BE(o + 8);
  const peak = b.readUInt32BE(o + 12);
  const blocks = b.readUInt32BE(o + 16);
  const status = b.readUInt32BE(o + 20);
  console.log(`  ${NAMES[i].padEnd(5)} status=${STATUS[status] ?? status} usados=${used} libres=${free} pico=${peak} slots=${blocks} capacidad=${capacity}`);
}
for (let i = 0; i < 3; ++i) {
  const o = 12 + 3 * 24 + i * 16;
  const requested = b.readUInt32BE(o);
  const status = b.readUInt32BE(o + 4);
  if (requested === 0) continue;
  console.log(`  fallo ${NAMES[i]}: pedidos=${requested} status=${STATUS[status] ?? status}`);
}
await conn.disconnect(true);
process.exit(0);
