#!/usr/bin/env node
// Analiza un perfil BINARIO de WinUAE/vscode-amiga-debug (el que produce el comando GDB
// `profile N "" out.bin`, p. ej. via tools/debug/winuae-profile.mjs) y saca **las rutinas
// que mas tiempo de CPU consumen**, resolviendo los PCs de las muestras con el `.map` del
// build. Es el equivalente a lo que hace el plugin en VSCode, sin depender de la UI.
//
// Formato (portado del parser del plugin, dist/debugAdapter.js):
//   numFrames(u32) sectionCount(u32) sections[] systemStack stack kickstart[..] chip[..]
//   bogo[..] baseClock(u32) cpuCycleUnit(u32)
//   por frame: customRegs[256] agaColors[256] dma[..] resources[..] profileCycles idle
//              profileCount profileArray[profileCount](u32 = PC) screenshot
//
// Uso:
//   node tools/analyze/profile-samples.mjs <perfil.bin> <demo_dir_name> [CONFIG] [--top N] [--json]
//
// Nota: el `parseProfile` del MCP NO lee el profileArray (se lo salta), por eso esta
// herramienta hace su propio parser.

import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

const args = process.argv.slice(2);
const pos = args.filter((a) => !a.startsWith('--'));
const flag = (name, dflt) => {
  const i = args.indexOf(name);
  return i >= 0 ? parseInt(args[i + 1] || String(dflt), 10) : dflt;
};
const BIN = pos[0];
const DEMO = pos[1];
const CONFIG_NAME = pos[2] || 'A500_debug';
const TOP = flag('--top', 20);
const AS_JSON = args.includes('--json');
if (!BIN || !DEMO) {
  console.error('Uso: node tools/analyze/profile-samples.mjs <perfil.bin> <demo> [config] [--top N] [--json]');
  process.exit(1);
}
const MAP = `${ROOT}/out/demos/${DEMO}/${CONFIG_NAME}/${DEMO}.${CONFIG_NAME}.map`;

// --- parser del perfil binario --------------------------------------------------------
// Unico cambio respecto al parser del plugin/MCP: `profileArray` con los PCs de las
// muestras (el MCP lo descartaba con `o += profileCount * 4`); hoy el MCP tambien lo lee.
function parseProfileWithSamples(buf) {
  let o = 0;
  const numFrames = buf.readUInt32LE(o); o += 4;
  const sectionCount = buf.readUInt32LE(o); o += 4;
  const sectionBases = [];
  for (let i = 0; i < sectionCount; i++) { sectionBases.push(buf.readUInt32LE(o)); o += 4; }
  o += 4 * 4; // systemStackLower/Upper, stackLower/Upper
  const kick = buf.readUInt32LE(o); o += 4 + kick;
  const chip = buf.readUInt32LE(o); o += 4 + chip;
  const bogo = buf.readUInt32LE(o); o += 4 + bogo;
  const baseClock = buf.readUInt32LE(o); o += 4;
  const cpuCycleUnit = buf.readUInt32LE(o); o += 4;
  const frames = [];
  for (let f = 0; f < numFrames; f++) {
    const customLen = buf.readUInt32LE(o); const customStart = o + 4;
    o = customStart + customLen;
    const agaLen = buf.readUInt32LE(o); o += 4 + agaLen;
    const dmaLen = buf.readUInt32LE(o); o += 4;
    const dmaCount = buf.readUInt32LE(o); o += 4;
    o += dmaLen * dmaCount;
    const resLen = buf.readUInt32LE(o); o += 4;
    const resCount = buf.readUInt32LE(o); o += 4;
    o += resLen * resCount;
    const profileCycles = buf.readUInt32LE(o); o += 4;
    const idleCycles = buf.readUInt32LE(o); o += 4;
    const profileCount = buf.readUInt32LE(o); o += 4;
    const samples = new Uint32Array(profileCount);
    for (let i = 0; i < profileCount; i++) { samples[i] = buf.readUInt32LE(o); o += 4; }
    const shotSize = buf.readUInt32LE(o); o += 4 + 4 + shotSize;
    frames.push({ profileCycles, idleCycles, samples });
  }
  return { sectionBases, baseClock, cpuCycleUnit, frames };
}

// --- simbolos del .map ----------------------------------------------------------------
const WANTED = new Set(['.text', '.rodata', '.data', '.bss', '.eh_frame']);
function mapSections() {
  const out = [];
  for (const line of fs.readFileSync(MAP, 'utf8').split(/\r?\n/g)) {
    const m = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!m || !WANTED.has(m[1])) continue;
    const start = parseInt(m[2], 16), size = parseInt(m[3], 16);
    if (size === 0) continue;
    out.push({ start, size, end: start + size });
  }
  return out;
}
function mapSymbols() {
  const out = [];
  for (const line of fs.readFileSync(MAP, 'utf8').split(/\r?\n/g)) {
    const m = line.match(/^\s*0x([0-9a-fA-F]+)\s+(\S+)\s*$/);
    if (m) out.push({ addr: parseInt(m[1], 16), name: m[2] });
  }
  out.sort((a, b) => a.addr - b.addr);
  return out;
}
function makeResolver(rtSections) {
  const ms = mapSections();
  const syms = mapSymbols();
  // Simbolo con direccion de RUNTIME: linked -> rt usando el indice de seccion.
  const rt = syms.map((s) => {
    for (let i = 0; i < ms.length && i < rtSections.length; ++i) {
      if (s.addr >= ms[i].start && s.addr < ms[i].end) {
        return { addr: rtSections[i] + (s.addr - ms[i].start), name: s.name };
      }
    }
    return null;
  }).filter(Boolean).sort((a, b) => a.addr - b.addr);
  return (pc) => {
    let lo = 0, hi = rt.length - 1, best = null;
    while (lo <= hi) {
      const mid = (lo + hi) >> 1;
      if (rt[mid].addr <= pc) { best = rt[mid]; lo = mid + 1; } else { hi = mid - 1; }
    }
    return best && pc - best.addr < 0x4000 ? best.name : `0x${pc.toString(16)}`;
  };
}

// --- informe ---------------------------------------------------------------------------
const parsed = parseProfileWithSamples(fs.readFileSync(BIN));
if (!fs.existsSync(MAP)) {
  console.error(`[samples] falta el .map: ${MAP} (¿config correcta?)`);
  process.exit(1);
}
const resolve = makeResolver(parsed.sectionBases);
const byRoutine = new Map();
let total = 0, busy = 0, idle = 0;
for (const f of parsed.frames) {
  busy += f.profileCycles || 0;
  idle += f.idleCycles || 0;
  for (const pc of f.samples) {
    const name = resolve(pc);
    byRoutine.set(name, (byRoutine.get(name) || 0) + 1);
    ++total;
  }
}
const pct = (v) => (total ? (100 * v / total).toFixed(1) : '0.0');
const top = [...byRoutine.entries()].sort((a, b) => b[1] - a[1]).slice(0, TOP);
if (AS_JSON) {
  console.log(JSON.stringify({
    frames: parsed.frames.length,
    samples: total,
    cpuBusyCycles: busy,
    cpuIdleCycles: idle,
    top: top.map(([routine, samples]) => ({ routine, samples, pct: Number(pct(samples)) })),
  }, null, 1));
} else {
  console.log(`[samples] ${path.basename(BIN)} | ${parsed.frames.length} frame(s) | ${total} muestras | CPU ocupada ${busy} ciclos, libre ${idle}`);
  for (const [name, n] of top) {
    console.log(`  ${String(n).padStart(6)}  ${pct(n).padStart(5)}%  ${name}`);
  }
}
