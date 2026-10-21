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
// Unico cambio respecto al parser del plugin/MCP: `profileArray` con las muestras (el MCP
// lo descartaba con `o += profileCount * 4`; hoy el MCP tambien lo lee).
//
// **Formato de las muestras (RLE, descifrado del plugin `debugAdapter`/`extension.js`):**
// el array NO es una lista de PCs: es un stream `[pc..., 0xFFFFFFFF-ciclos, r0..r16, ...]`
// — una muestra es el PRIMER valor desde el delimitador anterior (el PC), un delimitador
// que codifica los CICLOS gastados en ese PC (`0xFFFFFFFF - v`) y 17 registros
// (`Register._count` = D0-D7, A0-A7, SR). Leerlo como PCs daba basura (PCs 0x7fffffff…).
function decodeSamples(profileArray) {
	const out = [];
	let pc;
	for (let i = 0; i < profileArray.length; ++i) {
		const v = profileArray[i];
		if (v < 0xfff00000) {
			if (pc === undefined) pc = v;
			continue;
		}
		const cycles = (0xffffffff - v) >>> 0;
		if (pc === undefined) pc = 0xffffffff;
		i += 17; // registros de la muestra (Register._count)
		out.push({ pc, cycles });
		pc = undefined;
	}
	return out;
}

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
    out.push({ name: m[1], start, size, end: start + size });
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
	const textSection = ms.find((s) => s.name === '.text');
	const textSymbols = textSection
		? syms.filter((s) => s.addr >= textSection.start && s.addr < textSection.end)
		: [];
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
		if (best && pc - best.addr < 0x4000) return best.name;

		// WinUAE-DBG's `profileArray` may store a PC relative to the loaded text base
		// rather than an absolute runtime address. Relocate that offset to the link
		// `.text` base (`-Ttext=0x400`) before falling back to a raw hex PC.
		if (textSection) {
			const linkedPc = pc + textSection.start;
			if (linkedPc >= textSection.start && linkedPc < textSection.end) {
				let linkedLo = 0, linkedHi = textSymbols.length - 1, linkedBest = null;
				while (linkedLo <= linkedHi) {
					const mid = (linkedLo + linkedHi) >> 1;
					if (textSymbols[mid].addr <= linkedPc) {
						linkedBest = textSymbols[mid]; linkedLo = mid + 1;
					} else {
						linkedHi = mid - 1;
					}
				}
				if (linkedBest && linkedPc - linkedBest.addr < 0x4000) return linkedBest.name;
			}
		}
		return `0x${pc.toString(16)}`;
	};
}

// --- informe ---------------------------------------------------------------------------
const parsed = parseProfileWithSamples(fs.readFileSync(BIN));
if (!fs.existsSync(MAP)) {
  console.error(`[samples] falta el .map: ${MAP} (¿config correcta?)`);
  process.exit(1);
}
const resolve = makeResolver(parsed.sectionBases);

/// Agrega las muestras de un frame por rutina (ciclos y nº de muestras).
function frameRoutines(f) {
  const m = new Map();
  let total = 0;
  for (const s of decodeSamples(f.samples)) {
    const name = resolve(s.pc);
    const acc = m.get(name) || { samples: 0, cycles: 0 };
    acc.samples += 1;
    acc.cycles += s.cycles;
    m.set(name, acc);
    total += s.cycles;
  }
  return { m, total };
}

// --- modo por frame (`--per-frame [top]` o `--frame N`): atribuye frames lentos --------
const PER_FRAME = args.includes('--per-frame');
const FRAME_ARG = args.indexOf('--frame');
if (PER_FRAME) {
  const topN = flag('--per-frame', 2) || 2;
  console.log(`[samples] ${path.basename(BIN)} | por frame (top ${topN} rutinas) | ciclo muestreado = CPU ocupada`);
  for (let i = 0; i < parsed.frames.length; ++i) {
    const f = parsed.frames[i];
    const { m, total: t } = frameRoutines(f);
    const top = [...m.entries()].sort((a, b) => b[1].cycles - a[1].cycles).slice(0, topN);
    const list = top.map(([n, v]) => `${n}:${v.cycles}`).join(' ');
    console.log(`  f${String(i).padStart(3)}: ocupada=${String(f.profileCycles).padStart(7)} libre=${String(f.idleCycles).padStart(7)} muestreo=${String(t).padStart(7)} | ${list}`);
  }
  process.exit(0);
}
if (FRAME_ARG >= 0) {
  const idx = parseInt(args[FRAME_ARG + 1] || '0', 10);
  const f = parsed.frames[idx];
  if (!f) {
    console.error(`[samples] frame ${idx} fuera de rango (0..${parsed.frames.length - 1})`);
    process.exit(1);
  }
  const { m, total: t } = frameRoutines(f);
  console.log(`[samples] frame ${idx}: ocupada=${f.profileCycles} libre=${f.idleCycles} muestreo=${t}`);
  for (const [n, v] of [...m.entries()].sort((a, b) => b[1].cycles - a[1].cycles).slice(0, TOP)) {
    console.log(`  ${String(v.cycles).padStart(8)}  ${(100 * v.cycles / (t || 1)).toFixed(1).padStart(5)}%  ${n}  (${v.samples} muestras)`);
  }
  process.exit(0);
}

const byRoutine = new Map();
let total = 0, busy = 0, idle = 0;
for (const f of parsed.frames) {
	busy += f.profileCycles || 0;
	idle += f.idleCycles || 0;
	for (const s of decodeSamples(f.samples)) {
		const name = resolve(s.pc);
		const acc = byRoutine.get(name) || { samples: 0, cycles: 0 };
		acc.samples += 1;
		acc.cycles += s.cycles;
		byRoutine.set(name, acc);
		total += s.cycles;
	}
}
const pct = (v) => (total ? (100 * v / total).toFixed(1) : '0.0');
const top = [...byRoutine.entries()].sort((a, b) => b[1].cycles - a[1].cycles).slice(0, TOP);
if (AS_JSON) {
	console.log(JSON.stringify({
		frames: parsed.frames.length,
		sampledCycles: total,
		cpuBusyCycles: busy,
		cpuIdleCycles: idle,
		top: top.map(([routine, v]) => ({ routine, samples: v.samples, cycles: v.cycles, pct: Number(pct(v.cycles)) })),
	}, null, 1));
} else {
	console.log(`[samples] ${path.basename(BIN)} | ${parsed.frames.length} frame(s) | ${total} ciclos muestreados | CPU ocupada ${busy} ciclos, libre ${idle}`);
	for (const [name, v] of top) {
		console.log(`  ${String(v.cycles).padStart(8)}  ${pct(v.cycles).padStart(5)}%  ${name}  (${v.samples} muestras)`);
	}
}
