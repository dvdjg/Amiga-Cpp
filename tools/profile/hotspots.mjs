#!/usr/bin/env node
/**
 * Sampler profiler de hotspots (e9k-style) para WinUAE-DBG.
 *
 * DEPRECADO para el reparto por rutina. El canal lateral NO refresca el PC: el campo
 * `pc` de `state` (puerto 2346) devuelve el PC de cuando se entro en `observe`, asi que
 * este sampler da siempre el mismo simbolo (y con `-f <runner.uae>` el `baseText` puede
 * venir a 0). Para "donde se va el CPU" usar el perfil NATIVO de WinUAE + informe:
 *
 *   1) node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames]      (captura + .unwind)
 *   2) node tools/analyze/profile-report.mjs  <perfil.amigaprofile>      (top por rutina)
 *      o  node tools/analyze/profile-samples.mjs <perfil.bin> <demo>     (PCs -> .map)
 *
 * Ver docs/guides/optimization/METODOLOGIA_PROFILING.md §5 y docs/tools/PROFILING_FROM_AGENT.md.
 * Se conserva por su resolucion de simbolos desde el `.map`; si el canal lateral empieza a
 * refrescar el PC, vuelve a ser util.
 *
 * Uso: node tools/profile/hotspots.mjs [demos/<plataforma>/<demo>] [--seconds 5] [--config <id>] [--map x.map] [--out hotspots.md]
 * Env: WINUAE_PATH, WINUAE_CONFIG (defaults abajo).
 *
 * Nota: la demo debe estar compilada (out/demos/<demo>/<CONFIG>/<demo>.<CONFIG>.exe) y el
 * .map junto a ella. La captura usa el canal lateral de forma NO intrusiva.
 */
import { WinUAEConnection } from 'file:///C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/winuae-connection.js';
import { sideChannelCommand } from 'file:///C:/Users/dvdjg/Documents/programa/AI/Amiga/mcp-winuae-emu/dist/side-channel.js';
import fs from 'fs';
import path from 'path';

const ROOT = path.resolve(process.argv[1] ? path.dirname(process.argv[1]) : '.', '..', '..');
const args = process.argv.slice(2);
const demoArg = args.find(a => a.startsWith('demos/') || a.includes('_driver')) || 'demos/amiga/101_ehb_tile_scroll_driver';
const secondsIdx = args.indexOf('--seconds');
const seconds = parseInt(secondsIdx >= 0 ? (args[secondsIdx + 1] ?? '5') : '5', 10) || 5;
const outIdx = args.indexOf('--out');
const outPath = outIdx >= 0 ? (args[outIdx + 1] ?? null) : null;
const sleep = (ms) => new Promise(r => setTimeout(r, ms));

// Acepta `demos/amiga/<demo>`, `amiga/<demo>` o `<demo>`; la plataforma se descarta porque
// los artefactos viven en out/<...>/<demo>/ (ver docs/STRUCTURE.md y build-demo.sh).
const DEMO_RAW = demoArg.replace(/^demos\//, '').replace(/\/$/, '');
const DEMO = path.basename(DEMO_RAW);
const DEMO_DIR = path.join(ROOT, 'demos', DEMO_RAW);
const OUT_DIR = path.join(ROOT, 'out', 'run', DEMO);
const STAGE = path.join(OUT_DIR, 'dh1');
// El binario y el .map viven en out/demos/<demo>/<CONFIG>/ (sin plataforma; ver build-demo.sh).
// Se toma la primera config que tenga .exe y .map juntos, y se acepta --config para forzarla.
const CONFIG_IDX = args.indexOf('--config');
const CONFIG_ARG = CONFIG_IDX >= 0 ? args[CONFIG_IDX + 1] : null;
const DEMO_BASE = path.join(ROOT, 'out', 'demos', DEMO);
function findBuild(configName) {
  const candidates = [];
  if (configName) candidates.push(path.join(DEMO_BASE, configName));
  if (fs.existsSync(DEMO_BASE)) {
    for (const e of fs.readdirSync(DEMO_BASE)) candidates.push(path.join(DEMO_BASE, e));
  }
  for (const dir of candidates) {
    const id = path.basename(dir);
    const exe = path.join(dir, `${DEMO}.${id}.exe`);
    const map = path.join(dir, `${DEMO}.${id}.map`);
    if (fs.existsSync(exe) && fs.existsSync(map)) return { dir, exe, map };
  }
  return null;
}
const build = findBuild(CONFIG_ARG);
const EXE = build ? build.exe : path.join(DEMO_BASE, `${DEMO}.exe`);
const MAP = build ? build.map : path.join(DEMO_BASE, `${DEMO}.map`);

if (!fs.existsSync(EXE)) {
  console.error(`No existe ${EXE}. Compila la demo antes (bash ./tools/build/build-demo.sh demos/amiga/<demo>).`);
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
  // `initializeStopped` es IMPRESCINDIBLE: su `qOffsets` hace que WinUAE-DBG calcule
  // `baseText`, sin el cual los PCs de runtime no se pueden rebasar al direccionamiento
  // enlazado del .map (todas las muestras caerian en "otra-region").
  await conn.connect({ forceBreak: false, initializeStopped: true });
  await sleep(9000);
  const p = conn.getProtocol();
  await p.continue();
  // esperar a que la demo pase el init (~8s) y muestrear durante `seconds`
  console.log(`Esperando init de ${DEMO}...`);
  await sleep(8000);
  console.log(`Muestreando PC durante ${seconds}s...`);
  const tally = new Map();
  let baseText = 0;
  let lastPc = null;
  let distinctPc = 0;
  // El canal lateral lee el PC SIN detener la CPU, pero en las pruebas el campo `pc` de
  // `state` no se refresca entre consultas: si no cambia en toda la captura se avisa y el
  // informe queda inservible (el reparto real se obtiene con el perfil nativo, ver cabecera).
  const t0 = Date.now();
  while (Date.now() - t0 < seconds * 1000) {
    const r = await sideChannelCommand('state', 2346, 500).catch(() => null);
    if (r && r.ok && r.reply) {
      if (r.reply.baseText) baseText = parseInt(r.reply.baseText, 16) || baseText;
      const pc = r.reply.pc ? parseInt(r.reply.pc, 16) : NaN;
      if (!isNaN(pc)) {
        tally.set(pc, (tally.get(pc) || 0) + 1);
        if (pc !== lastPc) { ++distinctPc; lastPc = pc; }
      }
    }
    await sleep(5);
  }
  const samples = [...tally.values()].reduce((a, b) => a + b, 0);
  const syms = loadSymbols(MAP);
  if (distinctPc <= 1 && samples > 1) {
    console.error(`[hotspots] AVISO: el canal lateral devolvio el mismo PC en las ${samples} muestras`);
    console.error('[hotspots] El reparto por rutina NO es fiable por esta via. Usa el perfil nativo:');
    console.error(`[hotspots]   node tools/debug/winuae-profile.mjs ${DEMO} [CONFIG] [frames]`);
    console.error('[hotspots]   node tools/analyze/profile-report.mjs <perfil.amigaprofile>');
  }
  if (process.env.HOTSPOTS_DEBUG) {
    console.log(`DEBUG baseText=0x${baseText.toString(16)} PCs distintos=${distinctPc} primera=0x${[...tally.keys()][0]?.toString(16)}`);
  }

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
