#!/usr/bin/env node
/**
 * gdb-probe-octamed.mjs (fuente TS) — Diagnostico del cuelgue de A1 (OctaMED) con GDB.
 *
 * Lanza WinUAE con la demo 274, pide las SECCIONES RUNTIME por el canal lateral, resuelve
 * los simbolos del playroutine a su direccion runtime, pone breakpoints y reporta PC/
 * registros en cada parada. Sirve para ver donde se atasca, en vez de parchear a ciegas.
 *
 * Uso (con el emulador lanzado por el propio probe):
 *   node dist/tools/debug/gdb-probe-octamed.js \
 *       [--config <id>] [--bps _startmusic,_endmusic] [--hold-ms 12000] [--side-port 2421]
 */
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';
import { repoRoot } from '../lib/paths.js';

const root = repoRoot(import.meta.url);
const mcpWinuae = await import(
  pathToFileURL(path.join(path.dirname(root), 'mcp-winuae-emu', 'dist', 'winuae-connection.js')).href
);
const { WinUAEConnection } = mcpWinuae as { WinUAEConnection: any };

function argValue(name: string, fallback: string | undefined = undefined): string | undefined {
  const i = process.argv.indexOf(name);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}
function sleep(ms: number) { return new Promise((r) => setTimeout(r, ms)); }
function parseHex(v: string): number { return parseInt(String(v).replace(/^0x/i, ''), 16); }
function setConfigValue(t: string, k: string, v: string): string {
  const re = new RegExp(`^${k.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}=.*$`, 'm');
  const line = `${k}=${v}`;
  return re.test(t) ? t.replace(re, line) : `${t.replace(/\s*$/, '')}\r\n${line}\r\n`;
}
function findExtensionRoot(): string {
  if (process.env.AMIGA_DEBUG_EXT && fs.existsSync(process.env.AMIGA_DEBUG_EXT)) return path.resolve(process.env.AMIGA_DEBUG_EXT);
  for (const base of [path.join(process.env.USERPROFILE || '', '.cursor/extensions'), path.join(process.env.USERPROFILE || '', '.vscode/extensions')]) {
    if (!fs.existsSync(base)) continue;
    let best = '';
    for (const e of fs.readdirSync(base)) {
      if (!/^bartmanabyss\.amiga-debug-/.test(e)) continue;
      if (!fs.existsSync(path.join(base, e, 'bin/win32/winuae-gdb.exe'))) continue;
      if (!best || e > best) best = e;
    }
    if (best) return path.join(base, best);
  }
  throw new Error('No se encontro la extension bartmanabyss.amiga-debug-*.');
}
function findMapSymbol(mapPath: string, name: string): number | null {
  if (!fs.existsSync(mapPath)) return null;
  const re = new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`, 'm');
  const m = re.exec(fs.readFileSync(mapPath, 'utf8'));
  return m ? parseInt(m[1], 16) : null;
}
function findMapAllocSections(mapPath: string) {
  const wanted = new Set(['.text', '.rodata', '.eh_frame', '.data', '.bss']);
  const out: Array<{ start: number; size: number; end: number }> = [];
  for (const line of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g)) {
    const m = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!m || !wanted.has(m[1])) continue;
    const start = parseInt(m[2], 16); const size = parseInt(m[3], 16);
    if (size === 0) continue;
    out.push({ start, size, end: start + size });
  }
  return out;
}
function resolveRuntime(linked: number, mapSections: any[], runtime: string[]): number | null {
  if (!Array.isArray(runtime) || runtime.length === 0) return null;
  const cand = mapSections.filter((s) => linked >= s.start && linked < s.end);
  if (cand.length === 0) return parseHex(runtime[0]) + (linked - 0x400);
  const idx = mapSections.indexOf(cand[0]);
  if (runtime.length <= idx) return null;
  return parseHex(runtime[idx]) + (linked - cand[0].start);
}

/// Cliente del canal lateral (2346/2421): conectar -> saludar -> `<ord>\n` -> JSON.
function sideCommand(port: number, command: string, timeoutMs = 2500): Promise<any> {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: '127.0.0.1', port });
    socket.setEncoding('utf8');
    let pending = ''; let greeting = false; let done = false;
    const timer = setTimeout(() => { if (!done) { socket.destroy(); reject(new Error(`timeout canal lateral (${command})`)); } }, timeoutMs);
    socket.on('data', (chunk: string) => {
      pending += chunk;
      for (;;) {
        const eol = pending.indexOf('\n');
        if (eol < 0) break;
        const line = pending.slice(0, eol).trim(); pending = pending.slice(eol + 1);
        if (!greeting) { greeting = true; socket.write(`${command}\n`); continue; }
        done = true; clearTimeout(timer); socket.end();
        try { resolve(JSON.parse(line)); } catch { resolve(line); }
        return;
      }
    });
    socket.on('error', (e: Error) => { clearTimeout(timer); done = true; socket.destroy(); reject(e); });
  });
}

const demoName = path.basename(String(argValue('--demo', 'demos/techniques/amiga/audio/274_octamed_probe')).replace(/\\/g, '/'));
const demoDir = path.join(root, 'out/demos', demoName);
const forced = String(argValue('--config', ''));
let cfg = forced, exe = '', map = '';
if (forced) { exe = path.join(demoDir, forced, `${demoName}.${forced}.exe`); map = path.join(demoDir, forced, `${demoName}.${forced}.map`); }
else {
  let best: any = null;
  for (const e of fs.readdirSync(demoDir)) {
    const dir = path.join(demoDir, e); if (!fs.statSync(dir).isDirectory()) continue;
    const x = path.join(dir, `${demoName}.${e}.exe`); if (!fs.existsSync(x)) continue;
    const mt = fs.statSync(x).mtimeMs; if (!best || mt > best.mt) best = { mt, cfg: e, exe: x, map: path.join(dir, `${demoName}.${e}.map`) };
  }
  if (!best) throw new Error(`Sin builds en ${demoDir}`);
  cfg = best.cfg; exe = best.exe; map = best.map;
}
const bpNames = String(argValue('--bps', '_startmusic,_endmusic')).split(',').map((s) => s.trim()).filter(Boolean);
const holdMs = parseInt(String(argValue('--hold-ms', '12000')), 10);
const sidePort = parseInt(String(argValue('--side-port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2421')), 10);
console.log(`[gdb-probe] demo=${demoName} config=${cfg} side=${sidePort}`);
console.log(`[gdb-probe] exe=${exe}`);

const extensionRoot = findExtensionRoot();
const outputDir = path.join(root, 'out/run', demoName, cfg);
const stagedDir = path.join(outputDir, 'dh1'); fs.mkdirSync(stagedDir, { recursive: true });
fs.copyFileSync(exe, path.join(stagedDir, 'a.exe'));
const dh0 = path.join(extensionRoot, 'bin/dh0');
const startupPath = path.join(dh0, 's/startup-sequence'); fs.mkdirSync(path.dirname(startupPath), { recursive: true });
const prevStartup = fs.existsSync(startupPath) ? fs.readFileSync(startupPath, 'utf8') : null;
fs.writeFileSync(startupPath, 'cd dh1:\n:a.exe\n', 'utf8');
const cfgPath = path.join(outputDir, 'gdb-probe-uae.uae');
let configText = fs.readFileSync(path.join(root, 'config/mcp-amiga-c-debug.uae'), 'utf8');
configText = configText.replace(/^filesystem=rw,dh0:.*$/m, `filesystem=rw,dh0:${dh0.replace(/\//g, '\\')}`);
configText = setConfigValue(configText, 'filesystem2', `rw,dh1:dh1:${stagedDir.replace(/\//g, '\\')},-128`);
configText = setConfigValue(configText, 'debugging_trigger', ':a.exe');
configText = setConfigValue(configText, 'warp', 'false');
fs.writeFileSync(cfgPath, configText, 'utf8');

const conn = new WinUAEConnection({ winuaePath: path.join(extensionRoot, 'bin/win32'), configFile: cfgPath, gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10) });
const report: any = { demo: demoName, config: cfg, breakpoints: [], stops: [] };
try {
  await conn.connect({ forceBreak: false, initializeStopped: true });
  const proto = conn.getProtocol();
  // Secciones runtime por el canal lateral (el probe no lo "posee": solo lee `state`).
  let runtimeSections: string[] = [];
  for (let i = 0; i < 20 && runtimeSections.length === 0; i++) {
    try { const st = await sideCommand(sidePort, 'state'); if (st && st.ok && Array.isArray(st.sections)) runtimeSections = st.sections; } catch { /* aun no */ }
    if (runtimeSections.length === 0) await sleep(500);
  }
  report.runtimeSections = runtimeSections;
  const mapSections = findMapAllocSections(map);
  for (const name of bpNames) {
    const linked = findMapSymbol(map, name);
    const addr = linked !== null && runtimeSections.length ? resolveRuntime(linked, mapSections, runtimeSections) : null;
    const entry: any = { name, linked: linked === null ? null : `0x${linked.toString(16)}`, runtime: addr === null ? null : `0x${addr.toString(16)}` };
    if (addr !== null && addr > 0) {
      try { await proto.setBreakpoint('*0x' + addr.toString(16)); entry.set = true; } catch (e: any) { entry.error = String(e); }
    }
    report.breakpoints.push(entry);
  }
  // Tras el arranque, step-by-step N instrucciones desde `_startmusic` para ver si RETORNA
  // y a donde salta (si se queda girando dentro, lo delata).
  const stepCount = parseInt(String(argValue('--steps-after', '20')), 10);
  const startBp = report.breakpoints.find((b: any) => b.name === '_startmusic');
  await proto.continue();
  let stopped = false;
  const start = Date.now();
  while (Date.now() - start < holdMs + 6000 && !stopped) {
    let stop: any;
    try { stop = await proto.waitForStop(1500); } catch { continue; }
    if (/^W|^X/.test(stop)) { report.stops.push({ kind: 'exit', reply: stop }); break; }
    stopped = true;
    const regs0 = await proto.readRegisters().catch(() => null);
    const pc0 = regs0 ? (regs0.PC >>> 0) : null;
    report.stops.push({ pc: pc0 === null ? null : `0x${pc0.toString(16)}`, at: startBp && pc0 !== null && Math.abs(pc0 - parseHex(startBp.runtime)) <= 24 ? '_startmusic' : null });
    // Stepping: registra el PC de cada instruccion; si pasa de la direccion de retorno (vuelve
    // a `main`/a territorio del engine) o se estanca, se ve.
    const trace: string[] = [];
    let prevPc = pc0;
    let stuck = 0;
    for (let i = 0; i < stepCount; i++) {
      const r = await proto.step().catch(() => null);
      if (!r) break;
      const rr = await proto.readRegisters().catch(() => null);
      const pc = rr ? (rr.PC >>> 0) : null;
      trace.push(pc === null ? '?' : '0x' + pc.toString(16));
      if (pc !== null && pc === prevPc) { stuck++; } else { stuck = 0; }
      prevPc = pc;
      if (stuck > 3) { trace.push('(PC estancado)'); break; }
    }
    report.stops[report.stops.length - 1].stepTrace = trace;
    break;
  }
  console.log(JSON.stringify(report, null, 2));
} finally {
  try { await conn.disconnect(true); } catch { /* no ocultar */ }
  if (prevStartup !== null) fs.writeFileSync(startupPath, prevStartup, 'utf8');
}
