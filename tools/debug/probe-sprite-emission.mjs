#!/usr/bin/env node
// Sonda de emisión de sprites EN UNA SOLA EJECUCIÓN: lee la copperlist (via COP1LC) y la
// DECODIFICA, extrae de ella la config de los 8 canales (SPRxPOS/CTL/PT), lee la DATA en
// esas direcciones y los registros de paleta/DMA. Cruzar todo en el mismo instante es lo
// que permite separar «el canal no dibuja por la DATA» de «no dibuja por el DMA».
//
// Uso:  bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/054_sprite_allocator
//       node tools/debug/probe-sprite-emission.mjs 054_sprite_allocator [CONFIG_NAME]
//
// Puertos: WINUAE_GDB_PORT (2345). Ver docs/build/BUILD_AND_RUN.md.
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMO = process.argv[2] || '054_sprite_allocator';
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const conn = new WinUAEConnection({
  winuaePath: 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32',
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10),
});
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
await sleep(6000);
const hw = (a, l) => p.readMemory(a, l);
const hex = (v, n = 4) => v.toString(16).padStart(n, '0');

// --- registros de control del display -------------------------------------------------
const dmacon = (await hw(0xdff096, 2)).readUInt16BE(0);
const bplcon0 = (await hw(0xdff100, 2)).readUInt16BE(0);
const diwstrt = (await hw(0xdff08e, 2)).readUInt16BE(0);
const diwstop = (await hw(0xdff090, 2)).readUInt16BE(0);
console.log(`DMACON=0x${hex(dmacon)} BPLCON0=0x${hex(bplcon0)}` +
  ` DIWSTRT=0x${hex(diwstrt)} DIWSTOP=0x${hex(diwstop)}`);

const pal = await hw(0xdff180, 64);
let s = 'COLOR16..31:';
for (let i = 0; i < 16; ++i) s += ' ' + hex(pal.readUInt16BE(32 + i * 2), 3);
console.log(s);

// --- decodifica la lista y se queda con la ULTIMA config por canal --------------------
const lc = await hw(0xdff080, 4);
const listAddr = (lc.readUInt16BE(0) << 16) | lc.readUInt16BE(2);
const words = await hw(listAddr, 900);
const cfg = Array.from({ length: 8 }, () => ({ pos: null, ctl: null, pt: null }));
let waits = 0;
for (let w = 0; w + 1 < 450; ++w) {
  const w1 = words.readUInt16BE(w * 2);
  const w2 = words.readUInt16BE((w + 1) * 2);
  if (w1 === 0xffff && w2 === 0xfffe) break; // STOP
  if (w1 & 1) { ++waits; ++w; continue; }    // WAIT
  const reg = w1 & 0x1fe;
  for (let c = 0; c < 8; ++c) {
    if (reg === 0x140 + c * 8) cfg[c].pos = w2;
    if (reg === 0x142 + c * 8) cfg[c].ctl = w2;
    if (reg === 0x120 + c * 4) cfg[c].ptHi = w2;
    if (reg === 0x122 + c * 4) cfg[c].ptLo = w2;
  }
  ++w;
}
console.log(`lista @0x${hex(listAddr)}  WAITs=${waits}  (config = ultima escritura por canal)`);

// --- DATA en las direcciones que dice la PROPIA lista (misma ejecución) ---------------
for (let c = 0; c < 8; ++c) {
  const { pos, ctl } = cfg[c];
  const pt = (cfg[c].ptHi << 16) | cfg[c].ptLo;
  const vstart = pos === null ? -1 : (((ctl >> 3) & 1) << 8) | (pos >> 8);
  const vstop = ctl === null ? -1 : (((ctl >> 2) & 1) << 8) | (ctl >> 8);
  const hstart = pos === null ? -1 : (((ctl >> 1) & 1) << 8) | ((pos & 0xff) << 1);
  if (!pt) { console.log(`SPR${c}: sin PT en la lista`); continue; }
  const d = await hw(pt, 72);
  let head = '';
  for (let i = 0; i < 4; ++i) head += ' ' + hex(d.readUInt16BE(i * 2));
  const tail = hex(d.readUInt16BE(64)) + ':' + hex(d.readUInt16BE(66));
  console.log(`SPR${c}: PT=0x${hex(pt)} vstart=${vstart} vstop=${vstop} hstart=${hstart}` +
    ` DATA=${head} term=${tail}`);
}

await conn.disconnect(true);
process.exit(0);
