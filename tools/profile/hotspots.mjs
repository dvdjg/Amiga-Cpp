#!/usr/bin/env node
/**
 * Sampler profiler de hotspots (e9k-style) para WinUAE-DBG.
 *
 * Muestrea el PC del 68000 por el canal lateral (2346) durante N segundos,
 * agrupa las muestras por direccion, resuelve a simbolos con el .map de la
 * demo y emite un informe de "donde se va el tiempo del CPU".
 *
 * Uso: node tools/profile/hotspots.mjs [demos/<demo>] [--seconds 5] [--map x.map] [--out hotspots.md]
 * Env: WINUAE_PATH, WINUAE_CONFIG (defaults abajo).
 *
 * Nota: la demo debe estar compilada (out/demos/<demo>/<demo>.exe) y el .map
 * junto a ella. La captura usa el canal lateral de forma NO intrusiva.
 */
import { WinUAEConnection } from 'file:///C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/winuae-connection.js';
import { sideChannelCommand } from 'file:///C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/side-channel.js';
import fs from 'fs';
import path from 'path';

const ROOT = path.resolve(process.argv[1] ? path.dirname(process.argv[1]) : '.', '..', '..');
const args = process.argv.slice(2);
const demoArg = args.find(a => a.startsWith('demos/') || a.includes('_driver')) || 'demos/101_ehb_tile_scroll_driver';
const secondsIdx = args.indexOf('--seconds');
const seconds = parseInt(secondsIdx >= 0 ? (args[secondsIdx + 1] ?? '5') : '5', 10) || 5;
const outIdx = args.indexOf('--out');
const outPath = outIdx >= 0 ? (args[outIdx + 1] ?? null) : null;
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

const DEMO = demoArg.replace(/^demos\//, '').replace(/\/$/, '');
const DEMO_DIR = path.join(ROOT, 'demos', DEMO);
const OUT_DIR = path.join(ROOT, 'out', 'run', DEMO);
const STAGE = path.join(OUT_DIR, 'dh1');
const EXE = path.join(ROOT, 'out', 'demos', DEMO, `${DEMO}.exe`);
const MAP = path.join(ROOT, 'out', 'demos', DEMO, `${DEMO}.map`);

if (!fs.existsSync(EXE)) {
  console.error(`No existe ${EXE}. Compila la demo antes.`);
  process.exit(1);
}
if (process.env.HOTSPOTS_DEBUG) {
  console.log('DEBUG ROOT=', ROOT, 'argv1=', process.argv[1]);
  console.log('DEBUG EXE=', EXE, 'STAGE=', STAGE);
}

// --- resolucion de simbolos desde el .map (linked 0x400..) ---
function loadSymbols(mapPath) {
  if (!fs.existsSync(mapPath)) return [];
  const syms = [];
  for (const raw of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g)) {
    const m = /^\s*0x([0-9a-fA-F]{4,8})\s{2,}([A-Za-z_][A-Za-z0-9_]*)/.exec(raw);
    if (!m) continue;
    const addr = parseInt(m[1], 16);
    if (addr >= 0x400 && addr < 0x10000) syms.push({ addr, name: m[2] });
  }
  syms.sort((a, b) => a.addr - b.addr);
  return syms;
}
function symbolFor(syms, linked) {
  let best = null;
  for (const s of syms) if (linked >= s.addr) best = s;
  return best;
}

// --- staging (replica run-demo) ---
process.env.WINUAE_EXE = 'winuae-gdb.exe';
process.env.WINUAE_HEADLESS = '1';
process.env.WINUAE_USE_LEGACY_LAUNCH = '1';
process.env.WINUAE_GDB_INITIAL_DELAY_MS = process.env.WINUAE_GDB_INITIAL_DELAY_MS || '9000';
const bases = [path.join(process.env.USERPROFILE, '.cursor/extensions'), path.join(process.env.USERPROFILE, '.vscode/extensions')];
let extRoot = null, best = '';
for (const base of bases) {
  if (!fs.existsSync(base)) continue;
  for (const e of fs.readdirSync(base)) {
    const m = /^bartmanabyss\.amiga-debug-(.+)$/.exec(e);
    if (!m) continue;
    const c = path.join(base, e);
    if (!fs.existsSync(path.join(c, 'bin/win32/winuae-gdb.exe'))) continue;
    if (m[1] > best) { best = m[1]; extRoot = c; }
  }
}
fs.mkdirSync(STAGE, { recursive: true });
fs.copyFileSync(EXE, path.join(STAGE, 'a.exe'));
const dh0 = path.join(extRoot, 'bin/dh0');
fs.mkdirSync(path.join(dh0, 's'), { recursive: true });
fs.writeFileSync(path.join(dh0, 's/startup-sequence'), 'cd dh1:\n:a.exe\n', 'utf8');
const cfgPath = path.join(ROOT, 'config', 'mcp-amiga-c-debug.uae');
let cfg = fs.readFileSync(cfgPath, 'utf8');
cfg = cfg.replace(/^filesystem=rw,dh0:.*$/m, `filesystem=rw,dh0:${dh0.replace(/\//g, '\\')}`);
cfg = cfg.replace(/^filesystem2=rw,dh1:.*$/m, `filesystem2=rw,dh1:dh1:${STAGE.replace(/\//g, '\\')},-128`);
cfg = cfg.replace(/^warp=.*$/m, 'warp=false');
fs.mkdirSync(OUT_DIR, { recursive: true });
fs.writeFileSync(path.join(OUT_DIR, 'hotspots-cap.uae'), cfg);

const conn = new WinUAEConnection({
  winuaePath: path.join(extRoot, 'bin/win32'),
  configFile: path.join(OUT_DIR, 'hotspots-cap.uae'),
  gdbPort: 2345,
});

try {
  await conn.connect();
  await sleep(9000);
  const p = conn.getProtocol();
  await p.continue();
  // esperar a que la demo pase el init (~8s) y muestrear durante `seconds`
  console.log(`Esperando init de ${DEMO}...`);
  await sleep(8000);
  console.log(`Muestreando PC durante ${seconds}s...`);
  const tally = new Map();
  let baseText = 0;
  const t0 = Date.now();
  while (Date.now() - t0 < seconds * 1000) {
    const r = await sideChannelCommand('state', 2346, 400).catch(() => null);
    if (r && r.ok && r.reply) {
      const pc = parseInt(r.reply.pc, 16);
      if (!isNaN(pc)) tally.set(pc, (tally.get(pc) || 0) + 1);
      if (r.reply.baseText) baseText = parseInt(r.reply.baseText, 16) || 0;
    }
    await sleep(5);
  }
  const samples = [...tally.values()].reduce((a, b) => a + b, 0);
  const syms = loadSymbols(MAP);

  // resolver cada PC muestreado a su simbolo
  const bySymbol = new Map();
  let textHits = 0, romHits = 0, otherHits = 0;
  for (const [pc, n] of tally) {
    if (baseText && pc >= baseText && pc < baseText + 0x10000) {
      const linked = pc - baseText + 0x400;
      const s = symbolFor(syms, linked);
      const key = s ? s.name : `text+0x${linked.toString(16)}`;
      textHits += n;
      bySymbol.set(key, (bySymbol.get(key) || 0) + n);
    } else if (pc >= 0xfc000000) {
      romHits += n;
      bySymbol.set('KICKSTART_ROM', (bySymbol.get('KICKSTART_ROM') || 0) + n);
    } else {
      otherHits += n;
      bySymbol.set('otra-region', (bySymbol.get('otra-region') || 0) + n);
    }
  }

  const rows = [...bySymbol.entries()].sort((a, b) => b[1] - a[1]);
  const lines = [];
  lines.push(`# Hotspots de CPU - ${DEMO}`);
  lines.push('');
  lines.push(`- Muestras: ${samples} en ${seconds}s`);
  lines.push(`- text=${textHits} (${(textHits / samples * 100).toFixed(1)}%) · ROM=${romHits} (${(romHits / samples * 100).toFixed(1)}%) · otras=${otherHits} (${(otherHits / samples * 100).toFixed(1)}%)`);
  lines.push('');
  lines.push('| # | Símbolo | muestras | % |');
  lines.push('|---|---|---:|---:|');
  rows.slice(0, 20).forEach(([name, n], i) => {
    lines.push(`| ${i + 1} | ${name} | ${n} | ${(n / samples * 100).toFixed(1)}% |`);
  });
  const report = lines.join('\n') + '\n';

  if (outPath) {
    fs.writeFileSync(outPath, report, 'utf8');
    console.log(`Informe guardado en ${outPath}`);
  } else {
    console.log(report);
  }
} catch (e) {
  console.error('FAIL:', e.message.slice(0, 200));
  process.exit(1);
} finally {
  try { await conn.disconnect(true); } catch { /* noop */ }
}
