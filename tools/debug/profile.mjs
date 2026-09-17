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
  const re = new RegExp('^\\s*0x([0-9a-fA-F]+)\\s+' + name + '\\b');
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

const SECTION_NAMES = ['actors', 'blits', 'copper'];

const conn = new WinUAEConnection({
  winuaePath: 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32',
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: GDB_PORT,
});
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
await sleep(3000);

const st = await sideChannelCommand('state', SIDE_PORT, 5000);
const linked = findMapSymbol('g_eng_prof');
const addr = linked !== null ? runtimeAddr(linked, mapSections(), st.reply.sections) : null;
if (addr === null) {
  console.error('[prof] no se pudo resolver g_eng_prof (¿map? ¿sections?)');
  await conn.disconnect(true);
  process.exit(1);
}

const BLOCK = 12 + 16 * 4 + 16 * 4; // magic+sections+pad+frames + cycles[16] + calls[16]
async function sample() {
  const b = await p.readMemory(addr, BLOCK);
  if (b.readUInt32BE(0) !== PROF_MAGIC) return null;
  const sections = b.readUInt8(4);
  const frames = b.readUInt32BE(8);
  const cycles = [], calls = [];
  for (let i = 0; i < 16; ++i) {
    cycles.push(b.readUInt32BE(12 + i * 4));
    calls.push(b.readUInt32BE(12 + 64 + i * 4));
  }
  const clock = (await p.readMemory(0xb7e928, 4)).readUInt32BE(0);
  return { sections, frames, cycles, calls, clock };
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
let sum = 0;
for (let i = 0; i < a.sections; ++i) {
  const perFrame = u32delta(a.cycles[i], z.cycles[i]) / frames;
  const calls = u32delta(a.calls[i], z.calls[i]) / frames;
  sum += perFrame;
  const name = SECTION_NAMES[i] || `seccion${i}`;
  console.log(`  ${name.padEnd(8)} ${perFrame.toFixed(0).padStart(8)} ciclos/frame ${(100 * perFrame / total).toFixed(1).padStart(6)}%  ${calls.toFixed(1).padStart(6)} llamadas/frame`);
}
const rest = total - sum;
console.log(`  ${'(resto)'.padEnd(8)} ${rest.toFixed(0).padStart(8)} ciclos/frame ${(100 * rest / total).toFixed(1).padStart(6)}%  (espera VBlank + render + bucle)`);
await conn.disconnect(true);
process.exit(0);
