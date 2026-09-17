#!/usr/bin/env node
// Lee COP1LC ($DFF080) de la demo en marcha y DECODIFICA su copperlist (WAITs y MOVEs),
// para ver donde enlaza `end()` y donde caen los MOVEs de sprite respecto a la vstart.
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMO = process.argv[2] || '054_sprite_allocator';
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const N_WORDS = parseInt(process.argv[4] || '280', 10);
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

const lc = await p.readMemory(0xdff080, 4);
const addr = (lc.readUInt16BE(0) << 16) | lc.readUInt16BE(2);
console.log(`COP1LC = 0x${addr.toString(16)}  (leyendo ${N_WORDS} words)`);
const bytes = await p.readMemory(addr, N_WORDS * 2);

const SPR = (off) => off >= 0x120 && off <= 0x17e;
let pc = 0;
let lines = 0;
while (pc + 1 < N_WORDS && lines < 200) {
  const w1 = bytes.readUInt16BE(pc * 2);
  const w2 = bytes.readUInt16BE((pc + 1) * 2);
  if (w1 === 0xffff && w2 === 0xffff) { console.log(`${pc}: (relleno FFFF)`); break; }
  if (w1 & 1) {
    const vp = (w1 >> 8) & 0xff, hp = w1 & 0xfe;
    console.log(`${String(pc).padStart(4)}: WAIT  vpos=${vp} hpos=${hp} mask=0x${w2.toString(16)}`);
  } else {
    const reg = w1 & 0x1fe;
    const tag = SPR(reg) ? '  <-- SPRITE' : (reg === 0x088 ? '  <-- COPJMP1' : '');
    if (SPR(reg) || reg === 0x088 || reg === 0x08a || reg === 0x096) {
      console.log(`${String(pc).padStart(4)}: MOVE  reg=0x${reg.toString(16)} = 0x${w2.toString(16)}${tag}`);
    }
  }
  pc += 2;
  ++lines;
}
console.log(`(fin: ${pc} words recorridas)`);
await conn.disconnect(true);
process.exit(0);
