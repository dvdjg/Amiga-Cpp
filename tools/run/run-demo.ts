#!/usr/bin/env node
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';
import { repoRoot } from '../lib/paths.js';

const root = repoRoot(import.meta.url);
// El conector de WinUAE (repo hermano mcp-winuae-emu) vive junto al repositorio.
// Se importa dinamicamente desde la raiz para que la ruta valga tanto en el
// fuente como en dist/ (compilar anade un nivel de directorio).
const mcpWinuae = await import(
  pathToFileURL(path.join(path.dirname(root), 'mcp-winuae-emu', 'dist', 'winuae-connection.js')).href
);
const { WinUAEConnection } = mcpWinuae as { WinUAEConnection: any };


function argValue(name, fallback = undefined) {
  const index = process.argv.indexOf(name);
  if (index >= 0 && index + 1 < process.argv.length) {
    return process.argv[index + 1];
  }
  return fallback;
}

function hasArg(name) {
  return process.argv.includes(name);
}

// --protect <target>,<block|set:VALUE>[,size]  (repetible)
// target: simbolo del .map o direccion hex 0x... . El simbolo se resuelve a
// direccion runtime tras READY (relocacion por canal lateral). size: 8|16|32.
// Tambien lee ENG_PROTECT_SPECS (espacios entre specs) para propagarse desde
// tools/test-regression.sh a los analyze-sequence.sh sin tocar sus args.
function parseProtectSpecs() {
  const specs = [];
  const envSpecs = (process.env.ENG_PROTECT_SPECS || '').trim();
  if (envSpecs) {
    for (const part of envSpecs.split(/\s+/)) specs.push(part);
  }
  process.argv.forEach((value, index) => {
    if (value !== '--protect') return;
    const spec = process.argv[index + 1];
    if (spec) specs.push(spec);
  });
  const parsed = [];
  for (const spec of specs) {
    const parts = spec.split(',').map((part) => part.trim());
    const target = parts[0];
    const mode = parts[1] || 'block';
    const size = parts.length > 2 ? parseInt(parts[2], 10) : 16;
    if (!target) throw new Error(`--protect target vacio: ${spec}`);
    if (mode !== 'block' && !/^set:0x[0-9a-fA-F]+$/.test(mode)) {
      throw new Error(`--protect mode debe ser 'block' o 'set:0xVALUE': ${spec}`);
    }
    if (![8, 16, 32].includes(size)) throw new Error(`--protect size debe ser 8|16|32: ${spec}`);
    parsed.push({ target, mode, size });
  }
  return parsed;
}

function sleep(ms) {
  return new Promise<any>((resolve) => setTimeout(resolve, ms));
}

function parsePoint(text, name) {
  const match = String(text ?? '').match(/^\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*$/);
  if (!match) {
    throw new Error(`${name} debe tener formato x,y. Ejemplo: ${name} 32,32`);
  }
  return { x: Number(match[1]), y: Number(match[2]) };
}

function clampInt(value, min, max) {
  return Math.max(min, Math.min(max, Math.round(value)));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function curvePoint(from, control, to, t) {
  if (!control) {
    return { x: lerp(from.x, to.x, t), y: lerp(from.y, to.y, t) };
  }
  const ab = { x: lerp(from.x, control.x, t), y: lerp(from.y, control.y, t) };
  const bc = { x: lerp(control.x, to.x, t), y: lerp(control.y, to.y, t) };
  return { x: lerp(ab.x, bc.x, t), y: lerp(ab.y, bc.y, t) };
}

function buildMousePathFromArgs() {
  const fromText = argValue('--mouse-from');
  const toText = argValue('--mouse-to');
  if (!fromText && !toText) {
    return [];
  }
  if (!fromText || !toText) {
    throw new Error('Para automatizar raton hacen falta --mouse-from y --mouse-to.');
  }

  const from = parsePoint(fromText, '--mouse-from');
  const to = parsePoint(toText, '--mouse-to');
  const control = argValue('--mouse-control') ? parsePoint(argValue('--mouse-control'), '--mouse-control') : null;
  const steps = Math.max(1, parseInt(argValue('--mouse-steps', '48'), 10));
  const maxX = parseInt(argValue('--mouse-max-x', '319'), 10);
  const maxY = parseInt(argValue('--mouse-max-y', '255'), 10);
  const points = [];

  // These are Amiga display coordinates consumed by WinUAE monitor commands.
  // The Windows cursor is never moved: the emulated mouse receives synthetic
  // absolute positions through the debugger channel while the host pointer is
  // free to remain wherever the user left it.
  for (let i = 0; i <= steps; i++) {
    const p = curvePoint(from, control, to, i / steps);
    points.push({
      x: clampInt(p.x, 0, maxX),
      y: clampInt(p.y, 0, maxY),
    });
  }

  return points;
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function setConfigValue(configText, key, value) {
  const line = `${key}=${value}`;
  const re = new RegExp(`^${escapeRegExp(key)}=.*$`, 'm');
  if (re.test(configText)) {
    return configText.replace(re, line);
  }
  return `${configText.replace(/\s*$/, '')}\r\n${line}\r\n`;
}

function findExtensionRoot() {
  if (process.env.AMIGA_DEBUG_EXT && fs.existsSync(process.env.AMIGA_DEBUG_EXT)) {
    return path.resolve(process.env.AMIGA_DEBUG_EXT);
  }

  // Version-agnostic: pick the highest bartmanabyss.amiga-debug-* installed.
  // Do NOT hardcode a version; it changed between machines (1.7.9, 1.8.1, ...).
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
  if (bestDir) return bestDir;

  throw new Error('No se encontro la extension bartmanabyss.amiga-debug-*.');
}

function patchConfig(configText, extensionRoot, stagedOutDir, warpEnabled, immediateBlits) {
  const dh0 = path.join(extensionRoot, 'bin/dh0');
  const normalizedDh0 = dh0.replace(/\//g, '\\');
  const normalizedOut = stagedOutDir.replace(/\//g, '\\');

  let out = configText;
  out = out.replace(/^filesystem=rw,dh0:.*$/m, `filesystem=rw,dh0:${normalizedDh0}`);

  if (/^filesystem2=rw,dh1:.*$/m.test(out)) {
    out = out.replace(/^filesystem2=rw,dh1:.*$/m, `filesystem2=rw,dh1:dh1:${normalizedOut},-128`);
  } else {
    out += `\r\nfilesystem2=rw,dh1:dh1:${normalizedOut},-128\r\n`;
  }

  if (/^debugging_trigger=.*$/m.test(out)) {
    out = out.replace(/^debugging_trigger=.*$/m, 'debugging_trigger=:a.exe');
  } else {
    out += '\r\ndebugging_trigger=:a.exe\r\n';
  }

  // Automated tests must never trap the host pointer.  WinUAE-DBG has a
  // separate target option (`win32.absolute_mouse`) that disables the classic
  // relative mouse capture/warping path, while the Amiga-side tablet/mousehack
  // option (`absolute_mouse=mousehack`) remains disabled because that path is
  // still documented as problematic in WinUAE-DBG/doc/MOUSE-ABSOLUTE-TODO.md.
  out = setConfigValue(out, 'win32.start_not_captured', 'yes');
  out = setConfigValue(out, 'win32.active_capture_automatically', 'no');
  out = setConfigValue(out, 'win32.absolute_mouse', 'yes');
  out = setConfigValue(out, 'absolute_mouse', 'none');
  // The engine demos use `wait_vblank()` as their frame boundary.  Leaving
  // WinUAE in warp mode makes those VBlank waits complete faster than real time,
  // which is useful for bulk throughput but terrible for judging smooth motion.
  // Keep real-time pacing by default; callers can opt into warp explicitly.
  out = setConfigValue(out, 'warp', warpEnabled ? 'true' : 'false');
  // Blitter instantáneo (sin robar ciclos al 68000): mide el COSTE lado-A500 sin
  // el overhead de emular cada blit. Útil para el gate de fps del harness.
  if (immediateBlits) {
    out = setConfigValue(out, 'immediate_blits', 'true');
  }

  return out;
}

function decodeMonitorReply(hexReply) {
  if (!hexReply || !/^[0-9a-fA-F]+$/.test(hexReply)) {
    return '';
  }
  return Buffer.from(hexReply, 'hex').toString('utf8').trim();
}

async function captureScreenshot(protocol, imagePath, timeoutMs = 30000) {
  const winPath = imagePath.replace(/\//g, '\\');
  const reply = await protocol.sendMonitorCommand(`screenshot ${winPath}`, timeoutMs);
  return {
    path: imagePath,
    reply,
    replyText: decodeMonitorReply(reply),
    exists: fs.existsSync(imagePath),
  };
}

async function captureFrameSequence(protocol, sequenceDir, frameCount, intervalMs, sideChannel = null, onSample = null) {
  // Each sequence must be self-contained. Leaving older frame_XXX.png files in
  // place made analyzers and contact sheets mix different runs, which is exactly
  // the kind of false confidence these tools are meant to prevent.
  fs.rmSync(sequenceDir, { recursive: true, force: true });
  fs.mkdirSync(sequenceDir, { recursive: true });
  const frames = [];
  for (let i = 0; i < frameCount; ++i) {
    const framePath = path.join(sequenceDir, `frame_${String(i).padStart(3, '0')}.png`);
    const capturedAt = Date.now();
    let runStatusBefore = null;
    if (sideChannel?.runtimeAddress) {
      try {
        runStatusBefore = await readSideChannelRunStatusOnce(
          sideChannel.port,
          sideChannel.runtimeAddress,
          sideChannel.timeoutMs ?? 1000
        );
      } catch (err) {
        runStatusBefore = { ok: false, error: err.message };
      }
    }
    const frame: Record<string, any> = await captureScreenshot(protocol, framePath);
    frame.capturedAtMs = capturedAt;
    frame.runStatusBefore = runStatusBefore;
    if (sideChannel?.runtimeAddress) {
      try {
        frame.runStatusAfter = await readSideChannelRunStatusOnce(
          sideChannel.port,
          sideChannel.runtimeAddress,
          sideChannel.timeoutMs ?? 1000
        );
      } catch (err) {
        frame.runStatusAfter = { ok: false, error: err.message };
      }
      frame.runStatus = runStatusBefore?.ok ? runStatusBefore : frame.runStatusAfter;
    }
    frames.push(frame);
    if (intervalMs > 0 && i + 1 < frameCount) {
      await sleep(intervalMs);
    }
    if (onSample) {
      await onSample(i, frame, frames);
    }
  }
  return {
    directory: sequenceDir,
    frames,
    frameCount,
    intervalMs,
  };
}

/**
 * Ejecuta `fn` bajo un lock explícito del canal lateral. `input` requiere lock
 * `assist` y `poke`/`rollback` requieren `takeover` (replican la forma de uso
 * del CLI: `lock acquire owner MODE` -> acción -> `lock release owner`); sin
 * lock, el stub responde `lock_required` y por qRcmd además congelaba la
 * emulación (nunca se liberaba).
 */
async function withSideChannelLock(port: number, mode: 'assist' | 'takeover', owner: string, fn: () => Promise<void>): Promise<void> {
  const acquire = await sendSideChannelCommand(port, `lock acquire ${owner} ${mode}`);
  if (!String(acquire).includes('"ok":true')) {
    throw new Error(`lock acquire ${mode} rechazado: ${acquire}`);
  }
  try {
    await fn();
  } finally {
    await sendSideChannelCommand(port, `lock release ${owner}`);
  }
}

/**
 * Envía una orden por el socket TCP del canal lateral (2346). Protocolo:
 * conectar -> saludar -> `<orden>\n` -> una línea de respuesta JSON (3s).
 */
function sendSideChannelCommand(port: number, command: string, timeoutMs = 3000): Promise<string> {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: '127.0.0.1', port });
    socket.setEncoding('utf8');
    let pending = '';
    let consumedGreeting = false;
    let done = false;
    const timer = setTimeout(() => {
      if (!done) {
        socket.destroy();
        reject(new Error(`timeout del canal lateral (${command})`));
      }
    }, timeoutMs);
    socket.on('data', (chunk: string) => {
      pending += chunk;
      for (;;) {
        const eol = pending.indexOf('\n');
        if (eol < 0) break;
        const line = pending.slice(0, eol).trim();
        pending = pending.slice(eol + 1);
        if (!consumedGreeting) {
          consumedGreeting = true;
          socket.write(`${command}\n`);
          continue;
        }
        done = true;
        clearTimeout(timer);
        socket.end();
        resolve(line);
        return;
      }
    });
    socket.on('error', (err: Error) => {
      clearTimeout(timer);
      done = true;
      socket.destroy();
      reject(err);
    });
  });
}

function decodeCameraFineX(runStatus) {
  if (!runStatus?.ok) {
    return null;
  }
  const detail = typeof runStatus.detail === 'number'
    ? runStatus.detail
    : parseHexNumber(String(runStatus.detail ?? '0'));
  const cameraX = (detail >>> 16) & 0xff;
  return cameraX & 15;
}

function decodeCameraX(runStatus) {
  if (!runStatus?.ok) {
    return null;
  }
  const detail = typeof runStatus.detail === 'number'
    ? runStatus.detail
    : parseHexNumber(String(runStatus.detail ?? '0'));
  return (detail >>> 16) & 0xff;
}

async function captureFrameSequenceByRunStatusTarget(protocol, sequenceDir, targets, sideChannel, selector, label) {
  if (!sideChannel?.runtimeAddress) {
    throw new Error(`--sequence-${label} requiere canal lateral run status activo.`);
  }

  fs.rmSync(sequenceDir, { recursive: true, force: true });
  fs.mkdirSync(sequenceDir, { recursive: true });
  const frames = [];
  let targetIndex = 0;
  let lastCapturedFrame = -1;
  const deadline = Date.now() + Math.max(2500, targets.length * 1500);

  while (targetIndex < targets.length && Date.now() <= deadline) {
    const runStatus = await readSideChannelRunStatusOnce(
      sideChannel.port,
      sideChannel.runtimeAddress,
      sideChannel.timeoutMs ?? 1000
    );
    const value = selector(runStatus);
    const frameNumber = Number(runStatus?.frame ?? -1);
    if (value === targets[targetIndex] && frameNumber !== lastCapturedFrame) {
      const framePath = path.join(sequenceDir, `frame_${String(frames.length).padStart(3, '0')}_${label}${String(value).padStart(2, '0')}.png`);
      const capturedAt = Date.now();
      // Captura frame-exacta: congelar la CPU, leer el run status en ese estado
      // congelado y reanudar. La lectura previa (polling) va 1-2 frames por
      // delante del display; sin congelar, el label de camara no coincide con el
      // frame capturado y la deteccion de saltos de contenido pierde fiabilidad.
      let frozen = runStatus;
      try {
        await protocol.pause();
        await sleep(120);
        frozen = await readSideChannelRunStatusOnce(
          sideChannel.port,
          sideChannel.runtimeAddress,
          sideChannel.timeoutMs ?? 1000
        );
      } catch (err) {
        frozen = { ok: false, error: err.message };
      }
      const frame: Record<string, any> = await captureScreenshot(protocol, framePath);
      try { await protocol.continue(); } catch (err) { /* la captura ya quedo */ }
      frame.capturedAtMs = capturedAt;
      frame.runStatusBefore = runStatus;
      frame.runStatus = frozen.ok ? frozen : runStatus;
      frame.frozenFrame = Number(frozen?.frame ?? runStatus?.frame ?? -1);
      frame.target = targets[targetIndex];
      frame[`target${label[0].toUpperCase()}${label.slice(1)}`] = targets[targetIndex];
      frames.push(frame);
      lastCapturedFrame = Number(frozen?.frame ?? frameNumber);
      ++targetIndex;
    }
    await sleep(8);
  }

  if (targetIndex < targets.length) {
    throw new Error(`No se pudo capturar la secuencia ${label} completa. Capturados ${targetIndex}/${targets.length}.`);
  }

  return {
    directory: sequenceDir,
    frames,
    frameCount: frames.length,
    targets,
    targetKind: label,
  };
}

function findMapSymbol(mapPath, symbolName) {
  if (!fs.existsSync(mapPath)) {
    return null;
  }

  const lines = fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g);
  const re = new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${symbolName}\\b`);
  for (const line of lines) {
    const match = line.match(re);
    if (match) {
      return parseInt(match[1], 16);
    }
  }
  return null;
}

function findMapAllocSections(mapPath) {
  if (!fs.existsSync(mapPath)) {
    return [];
  }

  const wanted = new Set(['.text', '.rodata', '.data', '.bss']);
  const sections = [];
  const lines = fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g);
  for (const line of lines) {
    const match = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!match || !wanted.has(match[1])) {
      continue;
    }
    const start = parseInt(match[2], 16);
    const size = parseInt(match[3], 16);
    if (size === 0) {
      continue;
    }
    sections.push({ name: match[1], start, size, end: start + size });
  }
  return sections;
}

function resolveRuntimeSymbolAddress(linkedSymbol, mapSections, runtimeSections) {
  if (!Array.isArray(runtimeSections) || runtimeSections.length === 0) {
    return null;
  }

  const candidates = mapSections.filter((section) => linkedSymbol >= section.start && linkedSymbol < section.end);
  if (candidates.length === 0) {
    return runtimeSections.length > 0 ? parseHexNumber(runtimeSections[0]) + (linkedSymbol - 0x400) : null;
  }

  const section = candidates[0];
  const hunkIndex = mapSections.indexOf(section);
  if (runtimeSections.length <= hunkIndex) {
    return null;
  }

  return parseHexNumber(runtimeSections[hunkIndex]) + (linkedSymbol - section.start);
}

function parseTextOffset(reply) {
  const match = String(reply || '').match(/(?:^|;)Text=([0-9a-fA-F]+)/);
  return match ? parseInt(match[1], 16) : 0;
}

function parseHexNumber(value) {
  if (typeof value === 'number') {
    return value;
  }
  if (typeof value !== 'string') {
    return 0;
  }
  const trimmed = value.trim();
  return parseInt(trimmed.startsWith('0x') || trimmed.startsWith('0X') ? trimmed.slice(2) : trimmed, 16);
}

function decodeRunStatus(buffer) {
  if (!buffer || buffer.length < 16) {
    return null;
  }

  return {
    magic: buffer.readUInt32BE(0),
    version: buffer.readUInt16BE(4),
    state: buffer.readUInt16BE(6),
    frame: buffer.readUInt32BE(8),
    detail: buffer.readUInt32BE(12),
  };
}

async function waitForRunStatus(protocol, address, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last = null;

  while (Date.now() <= deadline) {
    const bytes = await protocol.readMemory(address, 16);
    const status = decodeRunStatus(bytes);
    last = status;

    if (status && status.magic === 0x454e4752 && status.version === 1) {
      if (status.state === 3) {
        return { status: 'ready', value: status };
      }
      if (status.state === 0xffff) {
        return { status: 'failed', value: status };
      }
    }

    await sleep(100);
  }

  return { status: 'timeout', value: last };
}

async function waitForStatusFile(filePath, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() <= deadline) {
    if (fs.existsSync(filePath)) {
      const text = fs.readFileSync(filePath, 'utf8').trim();
      if (text.startsWith('READY')) {
        return { status: 'ready', text };
      }
      if (text.startsWith('LAUNCHING')) {
        return { status: 'launching', text };
      }
      if (text.startsWith('FAILED')) {
        return { status: 'failed', text };
      }
      return { status: 'unexpected', text };
    }
    await sleep(100);
  }
  return { status: 'timeout', text: '' };
}

class SideChannelClient {
  // Campos de estado del cliente (socket, buffer de lectura y cola de esperas).
  port: number;
  socket: any;
  pending: string;
  lines: string[];
  waiters: any[];
  constructor(port) {
    this.port = port;
    this.socket = null;
    this.pending = '';
    this.lines = [];
    this.waiters = [];
  }

  async connect(timeoutMs) {
    await new Promise<any>((resolve, reject) => {
      const socket = net.createConnection({ host: '127.0.0.1', port: this.port });
      const timer = setTimeout(() => {
        socket.destroy();
        reject(new Error(`timeout connecting to side channel port ${this.port}`));
      }, timeoutMs);

      socket.once('connect', () => {
        clearTimeout(timer);
        this.socket = socket;
        socket.setEncoding('utf8');
        socket.on('data', (chunk) => this.onData(chunk));
        socket.on('error', () => {});
        resolve(undefined);
      });
      socket.once('error', (err) => {
        clearTimeout(timer);
        reject(err);
      });
    });

    // The server sends one greeting line immediately. Consume it so command
    // replies remain one request -> one JSON object.
    await this.readJsonLine(timeoutMs);
  }

  onData(chunk) {
    this.pending += chunk;
    for (;;) {
      const eol = this.pending.indexOf('\n');
      if (eol < 0) {
        break;
      }
      const line = this.pending.slice(0, eol).trim();
      this.pending = this.pending.slice(eol + 1);
      if (this.waiters.length > 0) {
        this.waiters.shift().resolve(line);
      } else {
        this.lines.push(line);
      }
    }
  }

  readLine(timeoutMs) {
    if (this.lines.length > 0) {
      return Promise.resolve(this.lines.shift());
    }
    return new Promise<any>((resolve, reject) => {
      const waiter = { resolve, reject, timer: null };
      waiter.timer = setTimeout(() => {
        const index = this.waiters.indexOf(waiter);
        if (index >= 0) {
          this.waiters.splice(index, 1);
        }
        reject(new Error('side channel reply timeout'));
      }, timeoutMs);
      waiter.resolve = (line) => {
        clearTimeout(waiter.timer);
        resolve(line);
      };
      this.waiters.push(waiter);
    });
  }

  async readJsonLine(timeoutMs) {
    const line = await this.readLine(timeoutMs);
    return JSON.parse(line);
  }

  async command(command, timeoutMs) {
    if (!this.socket) {
      throw new Error('side channel is not connected');
    }
    this.socket.write(`${command}\n`);
    return this.readJsonLine(timeoutMs);
  }

  close() {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
  }
}

async function waitForSideChannelRunStatus({ linkedSymbol, mapSections, timeoutMs, pollMs, port }) {
  const deadline = Date.now() + timeoutMs;
  const client = new SideChannelClient(port);
  // El listener 2346 puede tardar en arrancar (el emulador lo levanta tras el
  // boot): reconectamos en CADA poll (igual que `readSideChannelRunStatusOnce`
  // de la captura), en vez de conectar una sola vez al inicio.
  await client.connect(Math.min(1500, timeoutMs)).catch(() => {});
  try {
    let runtimeAddress = null;
    let last = null;
    while (Date.now() <= deadline) {
      let state = null;
      try {
        state = await client.command('state', 1500);
      } catch {
        state = null;
      }
      if (state && state.ok && runtimeAddress === null) {
        runtimeAddress = resolveRuntimeSymbolAddress(linkedSymbol, mapSections, state.sections);
      }
      if (runtimeAddress !== null && runtimeAddress > 0) {
        const status = await client.command(`runstatus ${runtimeAddress.toString(16)}`, 1500).catch(() => null);
        last = { state, status, runtimeAddress };
        if (status && status.ok && status.magic === '0x454e4752' && status.version === 1) {
          if (status.state === 3) {
            return { status: 'ready', value: status, runtimeAddress, state };
          }
          if (status.state === 0xffff) {
            return { status: 'failed', value: status, runtimeAddress, state };
          }
        }
      } else {
        last = { state, status: null, runtimeAddress: null };
      }
      // Reanudar la conexión si se cayó (el emulador reinicia el listener).
      try {
        await client.close();
      } catch { /* ignore */ }
      if (Date.now() <= deadline) {
        await client.connect(Math.min(1500, deadline - Date.now())).catch(() => {});
      }
      await sleep(pollMs);
    }
    return { status: 'timeout', value: last?.status ?? null, runtimeAddress, state: last?.state ?? null };
  } finally {
    client.close();
  }
}

async function readSideChannelRunStatusOnce(port, runtimeAddress, timeoutMs) {
  const client = new SideChannelClient(port);
  await client.connect(timeoutMs);
  try {
    return await client.command(`runstatus ${runtimeAddress.toString(16)}`, timeoutMs);
  } finally {
    client.close();
  }
}

const demoArg = process.argv[2];
if (!demoArg || demoArg.startsWith('--')) {
  console.error('Uso: node tools/run/run-demo.mjs demos/000_toolchain_cpp23 [--wait-ms 12000] [--screenshot file.png]');
  process.exit(2);
}

const demoPath = path.resolve(root, demoArg);
const demoName = path.basename(demoPath);
// El build nombra el ejecutable con un CONFIG_ID (MACHINE_flags_modo) y aísla
// los artefactos por configuración: out/demos/<demo>/<CONFIG_ID>/<demo>.<CONFIG_ID>.exe.
// Aquí seleccionamos la configuración más reciente (por mtime) de las presentes.
const demosRoot = path.join(root, 'out/demos', demoName);
let builtExe = path.join(demosRoot, `${demoName}.exe`);
let builtMap = path.join(demosRoot, `${demoName}.map`);
let configId = '';
// Prioridad de selección (de mejor a peor):
//   0  A500_debug            (default canónico: máquina + modo _debug, sin flags)
//   1  A500_o0               (sin flags pero depuración -O0: nunca sombrea el default)
//   2  A500_<flags>_debug    (debug con EXTRA_DEFINES)
//   3  A500_<flags>_o0
//   4  A500_release          (release sin flags)
//   5  A500_<flags>_release
// El desempate por mtime solo aplica dentro del mismo rango. Así la regresión
// (A500_debug) gana siempre y un `--o0` recién compilado no la sombrea.
function configRank(cfg: string): number {
  const noFlags = cfg.split('_').length <= 2; // sin token de EXTRA_DEFINES
  if (cfg.endsWith('_debug')) return noFlags ? 0 : 2;
  if (cfg.endsWith('_o0')) return noFlags ? 1 : 3;
  if (cfg.endsWith('_release')) return noFlags ? 4 : 5;
  return 6;
}
if (fs.existsSync(demosRoot)) {
  let best = { rank: 99, mtime: 0, exe: '', map: '', cfg: '' };
  for (const entry of fs.readdirSync(demosRoot)) {
    const cfgDir = path.join(demosRoot, entry);
    const st = fs.statSync(cfgDir);
    if (!st.isDirectory()) continue;
    const exe = path.join(cfgDir, `${demoName}.${entry}.exe`);
    if (!fs.existsSync(exe)) continue;
    const rank = configRank(entry);
    const mt = fs.statSync(exe).mtimeMs;
    if (rank < best.rank || (rank === best.rank && mt > best.mtime)) {
      best = { rank, mtime: mt, exe, map: path.join(cfgDir, `${demoName}.${entry}.map`), cfg: entry };
    }
  }
  if (best.exe) { builtExe = best.exe; builtMap = best.map; configId = best.cfg; }
}
// `--config <id>` fuerza una config concreta (p. ej. A500_release) sin pasar por
// la prioridad, útil para validar un perfil concreto (regresión de release).
const forcedConfig = argValue('--config', '');
if (forcedConfig) {
  const forcedDir = path.join(demosRoot, forcedConfig);
  const forcedExe = path.join(forcedDir, `${demoName}.${forcedConfig}.exe`);
  if (!fs.existsSync(forcedExe)) {
    throw new Error(`--config "${forcedConfig}": no existe ${forcedExe}. Compila esa config antes.`);
  }
  builtExe = forcedExe;
  builtMap = path.join(forcedDir, `${demoName}.${forcedConfig}.map`);
  configId = forcedConfig;
}
const builtMapSections = findMapAllocSections(builtMap);
if (!fs.existsSync(builtExe)) {
  throw new Error(`No existe ${builtExe}. Compila la demo antes de ejecutarla.`);
}

const waitMs = parseInt(argValue('--wait-ms', process.env.ENG_RUN_WAIT_MS || '18000'), 10);
const readyTimeoutMs = parseInt(argValue('--ready-timeout-ms', process.env.ENG_READY_TIMEOUT_MS || '4000'), 10);
const loadTimeoutMs = parseInt(argValue('--load-timeout-ms', process.env.ENG_LOAD_TIMEOUT_MS || '20000'), 10);
const settleMs = parseInt(argValue('--settle-ms', process.env.ENG_SETTLE_MS || '500'), 10);
const sideChannelPort = parseInt(argValue('--side-channel-port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
const sideChannelTimeoutMs = parseInt(argValue('--side-channel-timeout-ms', process.env.ENG_SIDE_CHANNEL_TIMEOUT_MS || '40000'), 10);
const sideChannelPollMs = parseInt(argValue('--side-channel-poll-ms', process.env.ENG_SIDE_CHANNEL_POLL_MS || '50'), 10);
const sequenceFrames = Math.max(0, parseInt(argValue('--sequence-frames', '0'), 10));
const sequenceIntervalMs = Math.max(0, parseInt(argValue('--sequence-interval-ms', '100'), 10));
const injectCommandsArg = argValue('--inject-commands', '');
const injectCommands = injectCommandsArg === ''
  ? []
  : injectCommandsArg.split('|').map((value) => value.trim()).filter((value) => value !== '');
const injectSample = Math.max(0, parseInt(argValue('--inject-sample', '4'), 10));
const automationKeyArg = argValue('--automation-key', '');
const sequenceFineXArg = argValue('--sequence-fine-x', '');
const sequenceFineX = sequenceFineXArg === ''
  ? []
  : sequenceFineXArg.split(',').map((value) => parseInt(value.trim(), 10)).filter((value) => Number.isInteger(value) && value >= 0 && value <= 15);
const sequenceCameraXArg = argValue('--sequence-camera-x', '');
const sequenceCameraX = sequenceCameraXArg === ''
  ? []
  : sequenceCameraXArg.split(',').map((value) => parseInt(value.trim(), 10)).filter((value) => Number.isInteger(value) && value >= 0 && value <= 255);
const telemetrySamples = Math.max(0, parseInt(argValue('--telemetry-samples', '0'), 10));
const telemetryIntervalMs = Math.max(10, parseInt(argValue('--telemetry-interval-ms', '120'), 10));
const warpEnabled = hasArg('--warp');
  const immediateBlits = hasArg('--immediate-blits');
const mousePath = buildMousePathFromArgs();
const mouseDelayMs = Math.max(0, parseInt(argValue('--mouse-duration-ms', '800'), 10)) / Math.max(1, mousePath.length - 1);
const mouseButton = Math.max(0, parseInt(argValue('--mouse-button', '0'), 10));
const stopEmulator = !hasArg('--keep-running');
const protectSpecs = parseProtectSpecs();
const outputDir = configId
  ? path.join(root, 'out/run', demoName, configId)
  : path.join(root, 'out/run', demoName);
const stagedDir = path.join(outputDir, 'dh1');
fs.mkdirSync(stagedDir, { recursive: true });
fs.copyFileSync(builtExe, path.join(stagedDir, 'a.exe'));
const statusFilePath = path.join(stagedDir, 'eng-run-status.txt');
if (fs.existsSync(statusFilePath)) {
  fs.unlinkSync(statusFilePath);
}

const screenshotPath = path.resolve(argValue('--screenshot', path.join(outputDir, 'screenshot.png')));
fs.mkdirSync(path.dirname(screenshotPath), { recursive: true });

const extensionRoot = findExtensionRoot();
const winuaePath = path.join(extensionRoot, 'bin/win32');
const dh0 = path.join(extensionRoot, 'bin/dh0');
const startupPath = path.join(dh0, 's/startup-sequence');
fs.mkdirSync(path.dirname(startupPath), { recursive: true });

const previousStartup = fs.existsSync(startupPath) ? fs.readFileSync(startupPath, 'utf8') : null;
fs.writeFileSync(startupPath, 'cd dh1:\n:a.exe\n', 'utf8');

const baseConfigPath = path.join(root, 'config/mcp-amiga-c-debug.uae');
const runnerConfigPath = path.join(outputDir, 'runner.uae');
const configText = fs.readFileSync(baseConfigPath, 'utf8');
fs.writeFileSync(runnerConfigPath, patchConfig(configText, extensionRoot, stagedDir, warpEnabled, immediateBlits), 'utf8');

const config = {
  winuaePath,
  configFile: runnerConfigPath,
  gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10),
};

const conn = new WinUAEConnection(config);
const report: Record<string, any> = {
  demo: demoName,
  executable: builtExe,
  map: builtMap,
  stagedExecutable: path.join(stagedDir, 'a.exe'),
  screenshot: screenshotPath,
  statusFile: statusFilePath,
  waitMs,
  readyTimeoutMs,
  sideChannelPort,
  sideChannelTimeoutMs,
  sequenceFrames,
  sequenceIntervalMs,
  warpEnabled,
  loadTimeoutMs,
  settleMs,
  status: 'started',
};

try {
  if (hasArg('--reset-emulator')) {
    // Limpieza previa opcional: evita que un WinUAE zombie de un run anterior
    // tenga tomados los puertos 2345/2346 (causa de "GDB connect timeout" y de
    // listeners 2346 ausentes). Solo matamos los binarios homónimos del depurador.
    try {
      const { spawnSync } = await import('node:child_process');
      for (const exe of ['winuae-gdb.exe', 'm68k-amiga-elf-gdb.exe', 'amiga-gdb.exe']) {
        const k = spawnSync('taskkill', ['/IM', exe, '/F', '/T'], { encoding: 'utf8' });
        if (k.status !== 0) { /* simplemente no estaba corriendo */ }
      }
      await new Promise((r) => setTimeout(r, 800));
      console.log('[run-demo] --reset-emulator: procesos stale finalizados');
    } catch { /* la limpieza nunca debe bloquear el arranque */ }
  }
  console.log(`[run-demo] launching ${demoName}`);
  await conn.connect({ forceBreak: false, initializeStopped: true });
  const protocol = conn.getProtocol();
  const runStatusSymbol = findMapSymbol(builtMap, 'g_eng_run_status');

  if (runStatusSymbol !== null) {
    report.runStatus = {
      symbol: `0x${runStatusSymbol.toString(16)}`,
      readyProbeSymbol: findMapSymbol(builtMap, 'eng_debug_ready_probe') !== null ? `0x${findMapSymbol(builtMap, 'eng_debug_ready_probe').toString(16)}` : null,
      statusFile: statusFilePath,
    };
  }

  const readyProbeSymbol = findMapSymbol(builtMap, 'eng_debug_ready_probe');
  if (hasArg('--require-ready') && readyProbeSymbol !== null) {
    console.log(`[run-demo] ready probe breakpoint at linked 0x${readyProbeSymbol.toString(16)}`);
    await protocol.setBreakpoint(readyProbeSymbol);
  }

  await protocol.continue();
  if (hasArg('--require-launch-marker')) {
    console.log(`[run-demo] waiting for startup launch marker (${loadTimeoutMs} ms)`);
    const launch = await waitForStatusFile(statusFilePath, loadTimeoutMs);
    report.launchMarker = launch;
    if (launch.status === 'timeout') {
      report.status = 'launch_marker_timeout';
      fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
      throw new Error('AmigaDOS no llego a lanzar la demo antes del timeout.');
    }
  }

  if (!hasArg('--no-side-channel') && runStatusSymbol !== null && !hasArg('--require-ready')) {
    console.log(`[run-demo] waiting for side-channel run status (${sideChannelTimeoutMs} ms)`);
    try {
      const side = await waitForSideChannelRunStatus({
        linkedSymbol: runStatusSymbol,
        mapSections: builtMapSections,
        timeoutMs: sideChannelTimeoutMs,
        pollMs: sideChannelPollMs,
        port: sideChannelPort,
      });
      report.sideChannel = side;
      if (side.status === 'ready') {
        if (settleMs > 0) {
          console.log(`[run-demo] side-channel READY; settling ${settleMs} ms`);
          await sleep(settleMs);
        }
      } else if (side.status === 'failed') {
        report.status = 'side_channel_failed';
        fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
        throw new Error(`La demo informo FAILED por canal lateral: detail=${side.value?.detail ?? '?'}`);
      } else {
        report.status = `side_channel_${side.status}`;
        fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
        if (!hasArg('--allow-timeout-fallback')) {
          throw new Error(`La demo no alcanzo READY por canal lateral en ${sideChannelTimeoutMs} ms: ${side.status}`);
        }
        console.log(`[run-demo] side-channel ${side.status}; explicit fallback wait ${waitMs} ms`);
        await sleep(waitMs);
      }
    } catch (err) {
      report.sideChannel = { status: 'unavailable', error: err.message };
      report.status = 'side_channel_unavailable';
      fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
      if (!hasArg('--allow-timeout-fallback')) {
        throw err;
      }
      console.log(`[run-demo] side-channel unavailable (${err.message}); explicit fallback wait ${waitMs} ms`);
      await sleep(waitMs);
    }
  } else if (hasArg('--require-ready') && readyProbeSymbol !== null) {
    console.log(`[run-demo] waiting for ready probe breakpoint (${readyTimeoutMs} ms)`);
    const stopReply = await protocol.waitForStop(readyTimeoutMs);
    await protocol.clearBreakpoint(readyProbeSymbol);
    report.runStatus = { ...report.runStatus, result: { status: 'ready', stopReply } };
    await protocol.continue();
    if (settleMs > 0) {
      console.log(`[run-demo] READY; settling ${settleMs} ms`);
      await sleep(settleMs);
    }
  } else if (hasArg('--require-ready') && runStatusSymbol !== null) {
    console.log(`[run-demo] waiting for debug status file (${readyTimeoutMs} ms)`);
    const ready = await waitForStatusFile(statusFilePath, readyTimeoutMs);
    report.runStatus = { ...report.runStatus, result: ready };
    if (ready.status !== 'ready') {
      report.status = `run_status_${ready.status}`;
      fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
      throw new Error(`La demo no alcanzo READY: ${ready.status}`);
    }
    if (settleMs > 0) {
      console.log(`[run-demo] READY; settling ${settleMs} ms`);
      await sleep(settleMs);
    }
  } else {
    console.log(`[run-demo] fallback wait ${waitMs} ms`);
    await sleep(waitMs);
  }

  // WinUAE-DBG v2.1: aplicar reglas protect (block/set) tras READY
  if (protectSpecs.length > 0) {
    report.protects = report.protects || [];
    const runtimeSections = report.sideChannel?.state?.sections ?? null;
    for (const spec of protectSpecs) {
      let addr = null;
      if (/^0x[0-9a-fA-F]+$/.test(spec.target)) {
        addr = parseHexNumber(spec.target);
      } else {
        const symbol = findMapSymbol(builtMap, spec.target);
        if (symbol === null) {
          console.log(`[run-demo] protect: simbolo '${spec.target}' no encontrado en .map; se omite`);
          continue;
        }
        if (!runtimeSections) {
          console.log(`[run-demo] protect: sin secciones runtime (canal lateral) para relocar '${spec.target}'; se omite`);
          continue;
        }
        addr = resolveRuntimeSymbolAddress(symbol, builtMapSections, runtimeSections);
      }
      if (addr === null || addr <= 0) {
        console.log(`[run-demo] protect: no se pudo resolver '${spec.target}'; se omite`);
        continue;
      }
      const modeCmd = spec.mode === 'block' ? 'block' : `set=0x${parseHexNumber(spec.mode.slice(4)).toString(16)}`;
      const cmd = `protect ${addr.toString(16)} ${modeCmd} size=${spec.size} src=cpudw`;
      console.log(`[run-demo] protect ${cmd}`);
      const reply = await protocol.sendMonitorCommand(cmd, 10000);
      report.protects.push({
        target: spec.target,
        runtimeAddr: `0x${addr.toString(16)}`,
        size: spec.size,
        mode: spec.mode,
        reply: Buffer.from(reply, 'hex').toString('utf8').trim(),
      });
    }
  }

  // Conmutación de técnica por `g_tech_new` (1..kTechCount), el hook directo que
  // el update de la demo ya consume (sin lógica de make/break): un único poke con
  // lock `takeover` en la fase pre-secuencia selecciona la técnica concreta. Es la
  // vía robusta y simple de "simular una tecla" en WinUAE (el port serie de CIAA
  // `input key` provocaba una excepción en la demo; ver input_poll.hpp).
  let automationKeyAddr: number | null = null;
  let automationKeyValue = 0;
  if (automationKeyArg !== '') {
    const parsed = parseInt(automationKeyArg, 10);
    automationKeyValue = Number.isInteger(parsed) && parsed >= 1 && parsed <= 9 ? parsed : 0;
    // Dirección runtime de `g_tech_new` anclada al símbolo PROBADO
    // `g_eng_run_status` (report.sideChannel.runtimeAddress): ambas viven en el
    // mismo hunk de datos y `resolveRuntimeSymbolAddress` cae en su fallback
    // (incorrecto) para símbolos tempranos, así que se aplica el delta del .map
    // sobre la base ya verificada.
    const mapKey = findMapSymbol(builtMap, 'g_tech_new');
    const kbdRuntimeSections = report.sideChannel?.state?.sections ?? null;
    if (mapKey !== null && kbdRuntimeSections) {
      automationKeyAddr = resolveRuntimeSymbolAddress(mapKey, builtMapSections, kbdRuntimeSections);
    }
    if (automationKeyAddr === null) {
      console.log('[run-demo] aviso: no se pudo resolver g_tech_new; se ignora --automation-key');
    } else {
      // Se inyecta ANTES de la secuencia (fase ratón/protect); la secuencia captura
      // el estado POST-conmutación.
      console.log(`[run-demo] automation key (tecnica) ${automationKeyValue} -> 0x${automationKeyAddr.toString(16)} (pre-secuencia)`);
      const valueHex = String(automationKeyValue).padStart(2, '0');
      await withSideChannelLock(sideChannelPort, 'takeover', 'run-demo', async () => {
        await sendSideChannelCommand(sideChannelPort, `poke ${automationKeyAddr.toString(16)} ${valueHex}`);
      });
      const readback = await sendSideChannelCommand(sideChannelPort, `mem ${automationKeyAddr.toString(16)} 1`).catch((err) => String(err));
      report.automationKey = {
        sample: 'pre-sequence',
        technique: automationKeyValue,
        address: `0x${automationKeyAddr.toString(16)}`,
        readback,
      };
    }
  }

  if (mousePath.length > 0) {
    console.log(`[run-demo] injecting mouse path with ${mousePath.length} points`);
    if (hasArg('--mouse-drag')) {
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 1`, 5000);
    }
    for (const [index, point] of mousePath.entries()) {
      await protocol.sendMonitorCommand(`input mouse abs ${point.x} ${point.y}`, 5000);
      if (mouseDelayMs > 0 && index + 1 < mousePath.length) {
        await sleep(mouseDelayMs);
      }
    }
    if (hasArg('--mouse-drag')) {
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 0`, 5000);
    }
    if (hasArg('--mouse-click')) {
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 1`, 5000);
      await sleep(Math.max(20, parseInt(argValue('--mouse-click-ms', '80'), 10)));
      await protocol.sendMonitorCommand(`input mouse button ${mouseButton} 0`, 5000);
    }
    report.mouse = {
      points: mousePath.length,
      first: mousePath[0],
      last: mousePath[mousePath.length - 1],
      button: mouseButton,
      clicked: hasArg('--mouse-click'),
      dragged: hasArg('--mouse-drag'),
    };
  }

  if (sequenceCameraX.length > 0) {
    console.log(`[run-demo] capturing sequence by cameraX targets ${sequenceCameraX.join(',')}`);
    report.sequence = await captureFrameSequenceByRunStatusTarget(
      protocol,
      path.join(outputDir, 'sequence'),
      sequenceCameraX,
      {
        port: sideChannelPort,
        runtimeAddress: report.sideChannel?.runtimeAddress,
        timeoutMs: 1000,
      },
      decodeCameraX,
      'cameraX'
    );
  } else if (sequenceFineX.length > 0) {
    console.log(`[run-demo] capturing sequence by fineX targets ${sequenceFineX.join(',')}`);
    report.sequence = await captureFrameSequenceByRunStatusTarget(
      protocol,
      path.join(outputDir, 'sequence'),
      sequenceFineX,
      {
        port: sideChannelPort,
        runtimeAddress: report.sideChannel?.runtimeAddress,
        timeoutMs: 1000,
      },
      decodeCameraFineX,
      'fineX'
    );
  } else if (sequenceFrames > 0) {
    console.log(`[run-demo] capturing ${sequenceFrames} sequence frames every ${sequenceIntervalMs} ms`);
    const injectionEvents: string[] = [];
    const onSample = (injectCommands.length === 0)
      ? null
      : async (index: number, frame: Record<string, any>, frames: unknown[]) => {
if (index !== injectSample - 1) return;
          console.log(`[run-demo] injecting ${injectCommands.length} monitor commands after sample ${index}`);
          // `input`/`profile` requieren lock `assist`: se mantiene durante el
          // lote (la emulación sigue corriendo bajo assist) y se libera al final.
          await withSideChannelLock(sideChannelPort, 'assist', 'run-demo', async () => {
            for (const cmd of injectCommands) {
              const sleepMatch = cmd.match(/^sleep:(\d+)$/);
              if (sleepMatch) {
                const ms = parseInt(sleepMatch[1], 10);
                await sleep(ms);
                continue;
              }
              await sendSideChannelCommand(sideChannelPort, cmd);
              injectionEvents.push(cmd);
            }
          });
          frame.injection = { sampleAfter: index, commands: [...injectionEvents] };
        };
    report.sequence = await captureFrameSequence(
      protocol,
      path.join(outputDir, 'sequence'),
      sequenceFrames,
      sequenceIntervalMs,
      report.sideChannel?.runtimeAddress ? {
        port: sideChannelPort,
        runtimeAddress: report.sideChannel.runtimeAddress,
        timeoutMs: 1000,
      } : null,
      onSample
    );
    report.inputInjection = injectionEvents.length > 0 ? {
      sample: injectSample,
      commands: injectionEvents,
    } : undefined;
  }

  // Telemetría por frame (opcional): lee `g_eng_frame_telemetry` (u32 frame,
  // u16 blit_jobs, u16 blit_words, u16 copper_words, u16 fillup_extra) por GDB
  // durante `telemetrySamples` muestras → permite medir el coste real de jobs
  // del scrolling (skip-on-equal, fusión, etc.).
  const telemetrySymbol = findMapSymbol(builtMap, 'g_eng_frame_telemetry');
  // Las secciones runtime las devuelve el comando `state` del canal lateral y
  // se guardan en report.sideChannel.state.sections (no en .value.state).
  const sideSections = report.sideChannel?.state?.sections;
  if (telemetrySamples > 0 && telemetrySymbol !== null && Array.isArray(sideSections) && sideSections.length > 0) {
    const telAddr = resolveRuntimeSymbolAddress(telemetrySymbol, builtMapSections, sideSections);
    if (telAddr !== null) {
      report.telemetry = [];
      for (let t = 0; t < telemetrySamples; ++t) {
        try {
          const b = await protocol.readMemory(telAddr, 12);
          // m68k es big-endian: leer los campos del struct en BE.
          report.telemetry.push({
            frame: b.readUInt32BE(0),
            blit_jobs: b.readUInt16BE(4),
            blit_words: b.readUInt16BE(6),
            copper_words: b.readUInt16BE(8),
            fillup_extra: b.readUInt16BE(10),
          });
        } catch (err) {
          report.telemetry.push({ error: err.message });
        }
        await sleep(telemetryIntervalMs);
      }
    }
  }

  const screenshot = await captureScreenshot(protocol, screenshotPath);
  report.screenshotReply = screenshot.reply;
  report.screenshotReplyText = screenshot.replyText;
  if (report.sideChannel?.runtimeAddress) {
    try {
      report.finalSideChannel = await readSideChannelRunStatusOnce(sideChannelPort, report.sideChannel.runtimeAddress, 1000);
    } catch (err) {
      report.finalSideChannel = { ok: false, error: err.message };
    }
  }
  report.status = fs.existsSync(screenshotPath) ? 'ok' : 'missing_screenshot';

  fs.writeFileSync(path.join(outputDir, 'run-report.json'), JSON.stringify(report, null, 2), 'utf8');
  console.log(`[run-demo] screenshot ${screenshotPath}`);
  console.log(`[run-demo] ${report.status}`);
} finally {
  try {
    await conn.disconnect(stopEmulator);
    if (!stopEmulator && (conn as any).process && typeof (conn as any).process.unref === 'function') {
      (conn as any).process.unref();
    }
  } catch {
    // Best effort cleanup: a failed disconnect should not hide the run result.
  }

  if (previousStartup !== null) {
    fs.writeFileSync(startupPath, previousStartup, 'utf8');
  }
}

if (report.status !== 'ok') {
  process.exit(1);
}
