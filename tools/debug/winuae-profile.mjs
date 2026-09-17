#!/usr/bin/env node
// Invoca el frame profiler de WinUAE (el mismo que usa el MCP `winuae_profile` y el
// vscode-amiga-debug) y resume el binario con `parseProfile`: **ciclos de CPU ocupada y
// libre por frame** y **DMA por tipo** (bitplane/copper/blitter/sprite). Es la herramienta
// para rutas negras: dice si el frame lo consume la CPU o lo espera, y reparte el bus.
//
// Uso:
//   bash ./tools/run/run-demo.sh <demo> [--config <config>]
//   node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames]
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import { parseProfile } from '../../../mcp-winuae-emu/dist/profile-parse.js';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMO = process.argv[2];
if (!DEMO) {
  console.error('Uso: node tools/debug/winuae-profile.mjs <demo> [config] [frames]');
  process.exit(1);
}
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const FRAMES = Math.max(1, Math.min(100, parseInt(process.argv[4] || '10', 10)));
const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const conn = new WinUAEConnection({
  winuaePath: 'C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32',
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: GDB_PORT,
});
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
await sleep(2500);

const outFile = path.join(ROOT, 'out', 'tmp', `wprof-${DEMO}-${CONFIG_NAME}.bin`);
console.log(`[wprof] capturando ${FRAMES} frame(s) a ${outFile}`);
await p.sendMonitorCommand(`profile ${FRAMES} "" "${outFile}"`, 60000 + FRAMES * 3000);

const parsed = parseProfile(fs.readFileSync(outFile));
console.log(`[wprof] ${DEMO}/${CONFIG_NAME} | baseClock=${parsed.baseClock} cpuCycleUnit=${parsed.cpuCycleUnit} frames=${parsed.frames.length}`);
const dmaTotals = {};
let sumBusy = 0, sumAll = 0;
for (let i = 0; i < parsed.frames.length; ++i) {
  const f = parsed.frames[i];
  const busy = f.profileCycles || 0;
  const idle = f.idleCycles || 0;
  const all = busy + idle;
  sumBusy += busy; sumAll += all;
  for (const [k, v] of Object.entries(f.dmaSummary?.byType || {})) {
    dmaTotals[k] = (dmaTotals[k] || 0) + v;
  }
  console.log(`  frame ${i}: cpu ocupada=${busy} libre=${idle} (${all ? (100 * idle / all).toFixed(0) : 0}% libre) | dma=${JSON.stringify(f.dmaSummary?.byType || {})} | gfxRes=${f.gfxResources?.length ?? 0}`);
}
const n = Math.max(1, parsed.frames.length);
console.log(`[wprof] media: cpu ocupada ${(sumBusy / n).toFixed(0)} ciclos/frame, libre ${((sumAll - sumBusy) / n).toFixed(0)} (${sumAll ? (100 * (sumAll - sumBusy) / sumAll).toFixed(0) : 0}% libre)`);
console.log(`[wprof] DMA por frame: ${JSON.stringify(Object.fromEntries(Object.entries(dmaTotals).map(([k, v]) => [k, Math.round(v / n)])))}`);
await conn.disconnect(true);
process.exit(0);
