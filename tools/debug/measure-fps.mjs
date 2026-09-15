#!/usr/bin/env node
// Mide fps y ciclos por frame en tiempo EMULADO (contador de ciclos del periférico
// de depuración 0xB7E928, 7.09379 MHz en A500), independiente del ancho de banda host.
//
// Uso:
//   node tools/debug/measure-fps.mjs <demo_dir_name> [CONFIG_NAME]
//
// Puertos: WINUAE_GDB_PORT (GDB, 2345) y WINUAE_SIDE_CHANNEL_PORT (lateral, 2346).
// Parametrizables para convivir con otras instancias de WinUAE (no pisar la ajena).
//
// La dirección runtime de `g_eng_run_status` se resuelve por el `.map` (símbolo +
// secciones) en vez de escanear el magic ENG: el escaneo daba falsos positivos.
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import { sideChannelCommand } from '../../../mcp-winuae-emu/dist/side-channel.js';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

const DEMO = process.argv[2];
if (!DEMO) { console.error('Uso: node tools/debug/measure-fps.mjs <demo_dir_name> [CONFIG_NAME]'); process.exit(1); }
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
const SIDE_PORT = parseInt(process.env.WINUAE_SIDE_CHANNEL_PORT || '2346', 10);
const MAP = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.map`;
const DH0 = 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/dh0';
fs.mkdirSync(DH0 + '/s', { recursive: true });
fs.writeFileSync(DH0 + '/s/startup-sequence', 'stack 131072\ncd dh1:\n:a.exe\n', 'utf8');

const CONFIG = {
  winuaePath: 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32',
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: GDB_PORT,
};
const CPU_HZ = 7093790;
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

const conn = new WinUAEConnection(CONFIG);
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
await sleep(10000);

const st = await sideChannelCommand('state', SIDE_PORT, 5000);
const linked = findMapSymbol('g_eng_run_status');
const magicAddr = linked !== null ? runtimeAddr(linked, mapSections(), st.reply.sections) : null;
console.log('[fps] map=' + MAP + ' linked=' + (linked ?? 'null') + ' runtime=0x' + (magicAddr ?? 0).toString(16));
if (magicAddr === null) {
  console.log('[fps] no se pudo resolver g_eng_run_status (¿map? ¿sections?)');
  await conn.disconnect(true);
  process.exit(1);
}
const state = async () => {
  const cy = (await p.readMemory(0xb7e928, 4)).readUInt32BE(0);
  const r = await sideChannelCommand('runstatus 0x' + magicAddr.toString(16), SIDE_PORT, 3000);
  return { cycles: cy, frame: r.reply.frame, detail: r.reply.detail };
};

await state();
const a = await state(); const t0 = Date.now();
await sleep(6000);
const b = await state(); const t1 = Date.now();
let dCycles = b.cycles - a.cycles;
if (dCycles < 0) dCycles += 4294967296;
const dFrame = b.frame - a.frame;
const emuFps = dFrame / (dCycles / CPU_HZ);
console.log('[fps] ' + DEMO + '/' + CONFIG_NAME +
  ' | emulado=' + emuFps.toFixed(2) + ' fps' +
  ' | host=' + ((dFrame) / ((t1 - t0) / 1000)).toFixed(2) + ' fps' +
  ' | ' + (dCycles / Math.max(1, dFrame)).toFixed(0) + ' ciclos/frame (' + (dCycles / Math.max(1, dFrame) / (CPU_HZ / 50)).toFixed(1) + ' lineas/frame)' +
  ' | detail=0x' + (b.detail >>> 0).toString(16));

await conn.disconnect(true);
process.exit(0);
