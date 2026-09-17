#!/usr/bin/env node
// Lee en caliente los registros de sprite del custom chip ($DFF120..$DFF17F) de una demo
// ya preparada en out/run/<demo>/<cfg>/runner.uae, para ver QUE configuro el engine.
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
await sleep(8000);

const pt = await p.readMemory(0xdff120, 32);   // 8 x (PTH, PTL)
const pos = await p.readMemory(0xdff140, 64);  // 8 x (POS, CTL, -, -)
const dmacon = (await p.readMemory(0xdff096, 2)).readUInt16BE(0);

console.log('DMACON = 0x' + dmacon.toString(16).padStart(4, '0'));
for (let c = 0; c < 8; ++c) {
  const pth = pt.readUInt16BE(c * 4);
  const ptl = pt.readUInt16BE(c * 4 + 2);
  const spos = pos.readUInt16BE(c * 8);
  const sctl = pos.readUInt16BE(c * 8 + 2);
  const vstart = ((sctl >> 3) & 1) << 8 | (spos >> 8);
  const vstop = ((sctl >> 2) & 1) << 8 | (sctl >> 8);
  const hstart = ((sctl >> 1) & 1) << 8 | ((spos & 0xff) << 1);
  console.log(
    `SPR${c}: PT=0x${pth.toString(16).padStart(4, '0')}${ptl.toString(16).padStart(4, '0')}` +
    ` POS=0x${spos.toString(16).padStart(4, '0')} CTL=0x${sctl.toString(16).padStart(4, '0')}` +
    ` -> hstart=${hstart} vstart=${vstart} vstop=${vstop}`
  );
}

await conn.disconnect(true);
process.exit(0);
