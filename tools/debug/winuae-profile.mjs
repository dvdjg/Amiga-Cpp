#!/usr/bin/env node
// Invoca el frame profiler de WinUAE (el mismo que usa el MCP `winuae_profile` y el
// vscode-amiga-debug) y resume el binario con `parseProfile`: **ciclos de CPU ocupada y
// libre por frame** y **DMA por tipo** (bitplane/copper/blitter/sprite). Es la herramienta
// para rutas negras: dice si el frame lo consume la CPU o lo espera, y reparte el bus.
//
// Uso:
//   bash ./tools/run/run-demo.sh <demo> [--config <config>]
//   node tools/debug/winuae-profile.mjs <demo> [CONFIG] [frames] [--settle-ms MS]
import { WinUAEConnection } from '../../../mcp-winuae-emu/dist/winuae-connection.js';
import { parseProfile } from '../../../mcp-winuae-emu/dist/profile-parse.js';
import { sideChannelCommand } from '../../../mcp-winuae-emu/dist/side-channel.js';
import * as fs from 'fs';
import * as path from 'path';
import * as childProcess from 'child_process';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMO = process.argv[2];
if (!DEMO) {
  console.error('Uso: node tools/debug/winuae-profile.mjs <demo> [config] [frames] [--settle-ms MS]');
  process.exit(1);
}
const CONFIG_NAME = process.argv[3] || 'A500_debug';
const FRAMES = Math.max(1, Math.min(100, parseInt(process.argv[4] || '10', 10)));
const settleArg = process.argv.indexOf('--settle-ms');
const SETTLE_MS = settleArg >= 0 ? Math.max(0, parseInt(process.argv[settleArg + 1] || '2500', 10)) : 2500;
const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
const SIDE_PORT = parseInt(process.env.WINUAE_SIDE_CHANNEL_PORT || '2346', 10);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// El perfil debe medir la **demo**, no la pantalla de arranque de Kickstart. El
// runner monta DH1 y lanza `a.exe`; hasta que `g_eng_run_status` no pasa a
// `Ready`, la CPU está en el boot y todas las demos darían el mismo perfil (el
// sintoma que tenía este script: 142102 ciclos ocupados en cualquier demo).
//
// `run-demo` escribe la startup-sequence de DH0 solo durante su ejecución y la
// restaura al salir; el `runner.uae` queda apuntando a la startup vieja. Aquí se
// prepara la misma startup (`cd dh1:` + `a.exe`) y se restaura al terminar.
const RUNNER_PATH = `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`;
let startupRestore = null;
function prepareRunnerStartup() {
  if (!fs.existsSync(RUNNER_PATH)) return;
  const text = fs.readFileSync(RUNNER_PATH, 'utf8');
  const match = text.match(/^filesystem=rw,dh0:(.+)$/m);
  if (!match) return;
  const dh0 = match[1].trim().replace(/\//g, '\\');
  const startup = path.join(dh0, 's', 'startup-sequence');
  fs.mkdirSync(path.dirname(startup), { recursive: true });
  startupRestore = {
    file: startup,
    content: fs.existsSync(startup) ? fs.readFileSync(startup, 'utf8') : null,
  };
  fs.writeFileSync(startup, 'stack 131072\ncd dh1:\n:a.exe\n', 'utf8');
}
function restoreRunnerStartup() {
  if (startupRestore === null) return;
  if (startupRestore.content === null) {
    fs.rmSync(startupRestore.file, { force: true });
  } else {
    fs.writeFileSync(startupRestore.file, startupRestore.content, 'utf8');
  }
  startupRestore = null;
}
prepareRunnerStartup();
process.on('exit', restoreRunnerStartup);

/// Espera a que el **programa** esté cargado (el canal lateral publica `baseText`
/// al dispararse `debugging_trigger=:a.exe`). Es el criterio robusto para el
/// perfil: el canal lateral puede no estar listo durante el boot y la resolución
/// del run-status por índice de sección no es fiable entre hunks.
async function waitForProgramLoaded(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last = null;
  while (Date.now() < deadline) {
    const state = await sideChannelCommand('state', SIDE_PORT, 1500);
    if (state.ok && state.reply) {
      last = state.reply;
      const base = parseInt(String(state.reply.baseText ?? '0x0').replace(/^0x/i, ''), 16);
      if (base !== 0) return base;
    }
    await sleep(250);
  }
  throw new Error(`el programa no se cargo (baseText=0) en ${timeoutMs} ms; ultimo=${JSON.stringify(last)}`);
}

// Se perfila el `runner.uae` de este build; usa `-f` para no reescribir
// `WinUAE-DBG/bin/default.uae` ni tomar por accidente otra demo A500.
// Un override explícito permite depurar la ruta extension-style si se necesita.
if (process.env.WINUAE_USE_LEGACY_LAUNCH === undefined) {
  process.env.WINUAE_USE_LEGACY_LAUNCH = '1';
}
const conn = new WinUAEConnection({
  winuaePath: (process.env.AMIGA_WINUAE_PATH ||
    'C:/Users/dvdjg/Documents/programa/AI/Amiga/WinUAE-DBG/bin').replace(/\\/g, '/'),
  configFile: `${ROOT}/out/run/${DEMO}/${CONFIG_NAME}/runner.uae`,
  gdbPort: GDB_PORT,
});
await conn.connect({ forceBreak: false, initializeStopped: true });
const p = conn.getProtocol();
await p.continue();
console.log('[wprof] esperando a que el programa cargue (baseText != 0)');
const baseText = await waitForProgramLoaded(30000);
console.log(`[wprof] programa cargado (baseText=0x${baseText.toString(16)})`);
if (SETTLE_MS > 0) {
  console.log(`[wprof] settle ${SETTLE_MS} ms antes de muestrear`);
  await sleep(SETTLE_MS);
}

// Construye la tabla de UNWIND que WinUAE necesita para MUESTREAR la CPU (sin ella el
// binario del perfil sale con 0 muestras: solo ciclos/DMA/recursos). Portado de
// `class UnwindTable` del plugin (bartmanabyss.amiga-debug, dist/debugAdapter.js):
// lee las CFI de DWARF con `objdump --dwarf=frames-interp` y escribe, por cada 2 bytes de
// .text, { (cfaReg<<12)|cfaOfs, r13, ra } como 3 int16.
function buildUnwind(objdumpPath, elfPath, textSize) {
  const invalid = { cfaOfs: -1, cfaReg: -1, r13: -1, ra: -1 };
  const table = new Array(textSize).fill(invalid);
  const dump = childProcess.spawnSync(objdumpPath, ['--dwarf=frames-interp', elfPath], { maxBuffer: 32 * 1024 * 1024 });
  if (dump.status !== 0) throw new Error(String(dump.error || dump.stderr || 'objdump fallo'));
  const lines = dump.stdout.toString().replace(/\r/g, '').split('\n');
  let line = 0, cfaIndex = -1, raIndex = -1, r13Index = -1;
  const cieMap = new Map();
  const header = () => {
    if (lines[line] === '') return;
    const el = lines[line++].trim().split(/\s+/g);
    cfaIndex = el.indexOf('CFA'); r13Index = el.indexOf('r13'); raIndex = el.indexOf('ra');
  };
  const row = () => {
    const el = lines[line++].split(/\s+/g);
    const loc = parseInt(el[0], 16);
    const c = el[cfaIndex].match(/r([0-9]+)\+([0-9]+)/);
    const r13s = el[r13Index];
    return {
      loc,
      unwind: {
        cfaReg: parseInt(c[1]), cfaOfs: parseInt(c[2]),
        r13: r13s && r13s.startsWith('c-') ? parseInt(r13s.substr(1)) : -1,
        ra: parseInt(el[raIndex].substr(1)),
      },
    };
  };
  const put = (pc, uw) => { if (table[pc >> 1] === invalid) table[pc >> 1] = uw; };
  while (line < lines.length) {
    if (/[0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8} CIE/.test(lines[line])) {
      const addr = parseInt(lines[line++].substr(0, 8), 16);
      header();
      cieMap.set(addr, row().unwind);
    } else if (/[0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8} FDE/.test(lines[line])) {
      const m = lines[line++].match(/ FDE cie=([0-9a-f]{8}) pc=([0-9a-f]{8})\.\.([0-9a-f]{8})/);
      const pcStart = parseInt(m[2], 16), pcEnd = parseInt(m[3], 16);
      header();
      let unw = cieMap.get(parseInt(m[1], 16));
      if (!unw) continue;
      let pc = pcStart;
      while (line < lines.length && lines[line] !== '') {
        const next = row();
        while (pc < next.loc) { put(pc, unw); pc += 2; }
        pc = next.loc; unw = next.unwind;
      }
      while (pc < pcEnd) { put(pc, unw); pc += 2; }
    } else line++;
  }
  const dflt = { cfaOfs: 4, cfaReg: 15, r13: -1, ra: -4 };
  const ser = new Int16Array(textSize * 3);
  let i = 0;
  for (const u of table) {
    const v = u === invalid ? dflt : u;
    ser[i++] = (v.cfaReg << 12) | (v.cfaOfs);
    ser[i++] = v.r13;
    ser[i++] = v.ra;
  }
  return Buffer.from(ser.buffer);
}

const outFile = path.join(ROOT, 'out', 'tmp', `wprof-${DEMO}-${CONFIG_NAME}.bin`);
let unwindFile = '';
try {
  const binDir = process.env.AMIGA_BIN_PATH ||
    'C:/Users/dvdjg/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32';
  const objdump = `${binDir.replace(/\\/g, '/')}/opt/bin/m68k-amiga-elf-objdump.exe`;
  const elf = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.elf`;
  const mapFile = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.map`;
  const text = fs.readFileSync(mapFile, 'utf8').match(/^\.text\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)/m);
  if (!text) throw new Error('sin .text en el .map');
  unwindFile = path.join(ROOT, 'out', 'tmp', `wprof-${DEMO}-${CONFIG_NAME}.unwind`);
  fs.writeFileSync(unwindFile, buildUnwind(objdump, elf, parseInt(text[1], 16)));
  console.log(`[wprof] unwind: ${unwindFile} (.text ${parseInt(text[1], 16)} bytes)`);
} catch (e) {
  unwindFile = '';
  console.log(`[wprof] sin unwind (${e.message}); el perfil no tendra muestras de CPU`);
}
console.log(`[wprof] capturando ${FRAMES} frame(s) a ${outFile}`);
await p.sendMonitorCommand(`profile ${FRAMES} "${unwindFile}" "${outFile}"`, 60000 + FRAMES * 3000);

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
restoreRunnerStartup();
await conn.disconnect(true);
process.exit(0);
