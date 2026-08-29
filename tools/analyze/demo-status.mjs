#!/usr/bin/env node
/**
 * Lanza una demo (estilo run-demo) y lee su run status: estado (Ready/Failed),
 * contador de frames y detalle. Util para saber si una demo marca FAILED en
 * runtime (p. ej. por presupuesto de blits o fallo del backend) sin abrir la
 * UI. Todo local, sin tokens de nube.
 *
 * Uso:
 *   node tools/analyze/demo-status.mjs <demo> [--samples N] [--interval-ms M]
 */
import * as path from 'path';
import * as fs from 'fs';
import { fileURLToPath, pathToFileURL } from 'url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const MCP_WINUAE = path.resolve(path.join(HERE, '..', '..', '..', 'mcp-winuae-emu'));
const LAUNCH = pathToFileURL(path.join(HERE, '..', 'profile', 'launch-winuae.mjs')).href;
const { prepareDemo, detectWinUAE, resolveRuntimeSymbolAddress } = await import(LAUNCH);
const { WinUAEConnection } = await import(pathToFileURL(path.join(MCP_WINUAE, 'dist', 'winuae-connection.js')).href);
const { sideChannelCommand } = await import(pathToFileURL(path.join(MCP_WINUAE, 'dist', 'side-channel.js')).href);

const demo = process.argv[2];
if (!demo) { console.error('Uso: node tools/analyze/demo-status.mjs <demo> [--samples N] [--interval-ms M]'); process.exit(2); }
const samples = parseInt((process.argv.find((a, i) => a === '--samples' && process.argv[i + 1]) ?? ['', '8'])[1], 10) || 8;
const interval = parseInt((process.argv.find((a, i) => a === '--interval-ms' && process.argv[i + 1]) ?? ['', '1000'])[1], 10) || 1000;

const { configPath, runStatusSymbol, mapSections } = prepareDemo(demo);
const { winuaePath } = detectWinUAE();
const extRoot = path.dirname(path.dirname(winuaePath));
fs.writeFileSync(path.join(extRoot, 'bin/dh0/s/startup-sequence'), 'cd dh1:\n:a.exe\n', 'utf8');

const conn = new WinUAEConnection({ winuaePath, configFile: configPath, gdbPort: 2345 });
await conn.connect({ forceBreak: false, initializeStopped: true });
try { await conn.getProtocol().continue(); } catch { /* noop */ }

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const read = async (cmd) => {
  const r = await sideChannelCommand(cmd, 2346, 2000);
  return r.ok ? r.reply : null;
};

let rt = 0;
for (let i = 0; i < 40 && rt === 0; ++i) {
  await sleep(500);
  const st = await read('state');
  if (st) rt = resolveRuntimeSymbolAddress(runStatusSymbol, mapSections, st.sections) || 0;
}
if (rt === 0) { console.log('NO READY'); try { await conn.disconnect(true); } catch {} process.exit(1); }

const names = { 0: 'Cold', 1: 'Booted', 2: 'InitStarted', 3: 'Ready', 0xffff: 'Failed' };
let prevFrame = -1;
for (let i = 0; i < samples; ++i) {
  try {
    const st = await read(`runstatus ${rt.toString(16)}`);
    if (st) {
      const state = parseInt(st.state ?? '0', 10);
      const frame = parseInt(st.frame ?? '0', 10);
      const detail = typeof st.detail === 'number' ? st.detail : parseInt(String(st.detail ?? '0'), 10);
      const px = (detail >>> 16) & 0xff;
      const py = (detail >>> 8) & 0xff;
      console.log(`[${i}] state=${names[state] ?? state} frame=${frame} detail=0x${detail.toString(16)} px=${px} py=${py}${i > 0 ? ` fps~${(frame - prevFrame) * 1000 / interval}` : ''}`);
      prevFrame = frame;
    }
  } catch (e) { /* noop */ }
  await sleep(interval);
}
try { await conn.disconnect(true); } catch { /* noop */ }
process.exit(0);
