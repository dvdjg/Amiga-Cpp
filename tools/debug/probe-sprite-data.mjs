#!/usr/bin/env node
// Sonda ampliada: DATA de los 8 sprites (via SPRxPT), paleta COLOR00..31, DMACON y display.
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
const dmacon = (await hw(0xdff096, 2)).readUInt16BE(0);
const bplcon0 = (await hw(0xdff100, 2)).readUInt16BE(0);
const diwstrt = (await hw(0xdff08e, 2)).readUInt16BE(0);
const diwstop = (await hw(0xdff090, 2)).readUInt16BE(0);
console.log(`DMACON=0x${dmacon.toString(16)} BPLCON0=0x${bplcon0.toString(16)}` +
  ` DIWSTRT=0x${diwstrt.toString(16)} DIWSTOP=0x${diwstop.toString(16)}`);

const pal = await hw(0xdff180, 64);
let palLine = 'COLOR00..15:';
for (let i = 0; i < 16; ++i) palLine += ' ' + pal.readUInt16BE(i * 2).toString(16).padStart(3, '0');
console.log(palLine);
palLine = 'COLOR16..31:';
for (let i = 0; i < 16; ++i) palLine += ' ' + pal.readUInt16BE(32 + i * 2).toString(16).padStart(3, '0');
console.log(palLine);

const pt = await hw(0xdff120, 32);
for (let c = 0; c < 8; ++c) {
  const addr = (pt.readUInt16BE(c * 4) << 16) | pt.readUInt16BE(c * 4 + 2);
  if (addr === 0) { console.log(`SPR${c}: PT=0`); continue; }
  const d = await hw(addr, 72); // 36 words: 16 lineas + 2 de terminador
  let first = '';
  for (let i = 0; i < 6; ++i) first += ' ' + d.readUInt16BE(i * 2).toString(16).padStart(4, '0');
  const last2 = d.readUInt16BE(68).toString(16) + ':' + d.readUInt16BE(70).toString(16);
  console.log(`SPR${c}: PT=0x${addr.toString(16)} datos[0..5]=${first} words[32..35](terminador)=${last2}`);
}

await conn.disconnect(true);
process.exit(0);
