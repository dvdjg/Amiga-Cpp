#!/usr/bin/env node
/**
 * launch-winuae.mjs — Lanza WinUAE-DBG con una demo del engine y espera READY
 * por el canal lateral, manteniendo a WinUAE como proceso hijo de ESTE proceso
 * (asi sobrevive mientras la IA captura el perfil por el canal 2346).
 *
 * Replica la logica minima del runner (tools/run/run-demo.ts) sin ejecutar su
 * flujo completo:
 *   - detecta la extension bartmanabyss.amiga-debug-* (para winuae-gdb.exe)
 *   - stagediza out/demos/<demo>/<demo>.exe en out/run/<demo>/dh1/a.exe
 *   - escribe out/run/<demo>/runner.uae (config parcheada)
 *   - lanza WinUAE via mcp-winuae-emu y espera READY por `runstatus`
 *
 * Uso:
 *   node tools/profile/launch-winuae.mjs <demo> [--timeout-ms N]
 */
import * as path from 'path';
import fs from 'fs';
import os from 'os';
import { fileURLToPath, pathToFileURL } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
// Repo hermano mcp-winuae-emu: vive junto a Amiga-Cpp (misma carpeta padre).
const MCP_WINUAE = path.join(path.dirname(ROOT), 'mcp-winuae-emu');
const SLEEP = (ms) => new Promise((r) => setTimeout(r, ms));

export function detectWinUAE() {
  if (process.env.WINUAE_PATH && fs.existsSync(process.env.WINUAE_PATH)) {
    return { winuaePath: path.resolve(process.env.WINUAE_PATH) };
  }
  const bases = [
    path.join(process.env.USERPROFILE || '', '.cursor/extensions'),
    path.join(process.env.USERPROFILE || '', '.vscode/extensions'),
  ];
  let best = '';
  let bestDir = null;
  for (const base of bases) {
    if (!fs.existsSync(base)) continue;
    for (const entry of fs.readdirSync(base)) {
      const m = /^bartmanabyss\.amiga-debug-(.+)$/.exec(entry);
      if (!m) continue;
      const candidate = path.join(base, entry);
      if (!fs.existsSync(path.join(candidate, 'bin/win32/winuae-gdb.exe'))) continue;
      if (m[1] > best) { best = m[1]; bestDir = candidate; }
    }
  }
  if (!bestDir) throw new Error('No se encontro la extension bartmanabyss.amiga-debug-* (o WINUAE_PATH).');
  return { winuaePath: path.join(bestDir, 'bin/win32') };
}

function setConfigValue(text, key, value) {
  const re = new RegExp(`^${key}=.*$`, 'm');
  if (re.test(text)) return text.replace(re, `${key}=${value}`);
  return `${text}\r\n${key}=${value}\r\n`;
}

export function findMapSymbol(mapPath, symbolName) {
  if (!fs.existsSync(mapPath)) return null;
  const lines = fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g);
  const re = new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${symbolName}\\b`);
  for (const line of lines) {
    const match = line.match(re);
    if (match) return parseInt(match[1], 16);
  }
  return null;
}

export function findMapAllocSections(mapPath) {
  if (!fs.existsSync(mapPath)) return [];
  const wanted = new Set(['.text', '.rodata', '.data', '.bss']);
  const sections = [];
  const lines = fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g);
  for (const line of lines) {
    const match = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!match || !wanted.has(match[1])) continue;
    const start = parseInt(match[2], 16);
    const size = parseInt(match[3], 16);
    if (size === 0) continue;
    sections.push({ name: match[1], start, size, end: start + size });
  }
  return sections;
}

function parseHexNumber(value) {
  if (typeof value === 'number') return value;
  if (typeof value !== 'string') return 0;
  const t = value.trim();
  return parseInt(t.startsWith('0x') || t.startsWith('0X') ? t.slice(2) : t, 16);
}

export function resolveRuntimeSymbolAddress(linkedSymbol, mapSections, runtimeSections) {
  if (!Array.isArray(runtimeSections) || runtimeSections.length === 0) return null;
  const candidates = mapSections.filter((s) => linkedSymbol >= s.start && linkedSymbol < s.end);
  if (candidates.length === 0) {
    return runtimeSections.length > 0 ? parseHexNumber(runtimeSections[0]) + (linkedSymbol - 0x400) : null;
  }
  const section = candidates[0];
  const hunkIndex = mapSections.indexOf(section);
  if (runtimeSections.length <= hunkIndex) return null;
  return parseHexNumber(runtimeSections[hunkIndex]) + (linkedSymbol - section.start);
}

/** Prepara el directorio dh1 y la config; devuelve rutas y simbolos. */
export function prepareDemo(demo) {
  const demoName = path.basename(path.resolve(demo));
  const builtExe = path.join(ROOT, 'out/demos', demoName, `${demoName}.exe`);
  const builtMap = path.join(ROOT, 'out/demos', demoName, `${demoName}.map`);
  if (!fs.existsSync(builtExe)) {
    throw new Error(`No existe ${builtExe}. Compila la demo antes (tools/build/build-demo.sh ${demo} --debug).`);
  }
  const mapSections = findMapAllocSections(builtMap);
  const runStatusSymbol = findMapSymbol(builtMap, 'g_eng_run_status');
  if (runStatusSymbol === null) {
    throw new Error(`La demo ${demoName} no exporta 'g_eng_run_status' en su .map.`);
  }

  const outputDir = path.join(ROOT, 'out/run', demoName);
  const stagedDir = path.join(outputDir, 'dh1');
  fs.mkdirSync(stagedDir, { recursive: true });
  fs.copyFileSync(builtExe, path.join(stagedDir, 'a.exe'));

  const configPath = path.join(outputDir, 'runner.uae');
  if (!fs.existsSync(configPath)) {
    const baseConfig = path.join(ROOT, 'config/mcp-amiga-c-debug.uae');
    let cfg = fs.existsSync(baseConfig) ? fs.readFileSync(baseConfig, 'utf8') : '';
    // dh0 -> bin/dh0 de la extension (la base config puede tener una version hardcodeada).
    const { winuaePath } = detectWinUAE();
    const extRoot = path.dirname(path.dirname(winuaePath)); // <ext>/bin/win32 -> <ext>
    cfg = setConfigValue(cfg, 'filesystem', `rw,dh0:${path.join(extRoot, 'bin/dh0').replace(/\//g, '\\')}`);
    cfg = setConfigValue(cfg, 'filesystem2', `rw,dh1:dh1:${outputDir.replace(/\//g, '\\')},-128`);
    cfg = setConfigValue(cfg, 'debugging_trigger', ':a.exe');
    cfg = setConfigValue(cfg, 'warp', 'false');
    fs.writeFileSync(configPath, cfg, 'utf8');
  }

  return { configPath, outputDir, stagedDir, runStatusSymbol, mapSections, demoName };
}

/** Espera READY por el canal lateral. Devuelve la direccion runtime del runstatus. */
export async function waitReady(port, runStatusSymbol, mapSections, timeoutMs = 40000) {
  const { sideChannelCommand } = await import(pathToFileURL(path.join(MCP_WINUAE, 'dist', 'side-channel.js')).href);
  const deadline = Date.now() + timeoutMs;
  let runtimeAddress = null;
  while (Date.now() <= deadline) {
    let state = null;
    try {
      const r = await sideChannelCommand('state', port, 2000);
      if (r.ok && r.reply) state = r.reply;
    } catch { /* reintenta */ }
    runtimeAddress = state ? resolveRuntimeSymbolAddress(runStatusSymbol, mapSections, state.sections) : null;
    if (runtimeAddress !== null && runtimeAddress > 0) {
      try {
        const st = await sideChannelCommand(`runstatus ${runtimeAddress.toString(16)}`, port, 2000);
        const v = st.reply;
        if (v && v.magic === '0x454e4752' && v.version === 1) {
          if (v.state === 3) return { runtimeAddress };
          if (v.state === 0xffff) throw new Error(`La demo informo FAILED por canal lateral: ${JSON.stringify(v)}`);
        }
      } catch (e) {
        if (e.message && e.message.includes('FAILED')) throw e;
      }
    }
    await SLEEP(100);
  }
  throw new Error(`La demo no alcanzo READY por el canal lateral en ${timeoutMs} ms.`);
}

/** Lanza WinUAE y espera READY. Devuelve { conn, runtimeAddress }. */
export async function launchDemoAndWaitReady({ demo, port = 2346, readyTimeoutMs = 40000, connectTimeoutMs = 60000 }) {
  // El canal lateral debe sobrevivir a la desconexión inicial de GDB y a la
  // conexión posterior del capturador de perfiles.
  process.env.WINUAE_GDB_PERSIST_LISTENER = '1';
  // Usar la configuración preparada para la demo; el lanzador extension-style
  // puede sustituirla por default.uae y arrancar sin el filesystem de dh1.
  process.env.WINUAE_USE_LEGACY_LAUNCH = '1';
  const { WinUAEConnection } = await import(pathToFileURL(path.join(MCP_WINUAE, 'dist', 'winuae-connection.js')).href);
  const { configPath, runStatusSymbol, mapSections } = prepareDemo(demo);
  const { winuaePath } = detectWinUAE();
  const conn = new WinUAEConnection({
    winuaePath,
    configFile: configPath,
    gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10),
  });
  await conn.connect({ forceBreak: false, initializeStopped: false });
  try {
    // WinUAE puede aceptar GDB antes de haber terminado de cargar Kickstart y
    // el startup-sequence. Continuar inmediatamente deja algunas sesiones
    // detenidas sin llegar a ejecutar la demo ni publicar READY.
    await SLEEP(parseInt(process.env.WINUAE_GDB_INITIAL_DELAY_MS || '9000', 10));
    await conn.getProtocol().continue();
  } catch { /* puede estar ya en marcha */ }
  const { runtimeAddress } = await waitReady(port, runStatusSymbol, mapSections, readyTimeoutMs);
  return { conn, runtimeAddress };
}

// CLI directo (para probar): lanza la demo y mantiene WinUAE viva N segundos.
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const demo = process.argv[2];
  const holdMs = parseInt(process.argv[3] || '20000', 10);
  if (!demo) {
    console.error('Uso: node tools/profile/launch-winuae.mjs <demo> [holdMs]');
    process.exit(2);
  }
  const { conn, runtimeAddress } = await launchDemoAndWaitReady({ demo });
  console.log(`READY runtimeAddress=0x${runtimeAddress.toString(16)}; WinUAE viva ${holdMs} ms`);
  await SLEEP(holdMs);
  try { await conn.disconnect(true); } catch { /* noop */ }
  console.log('cleanup done');
  process.exit(0);
}
