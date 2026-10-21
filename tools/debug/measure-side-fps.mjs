#!/usr/bin/env node
// Timestamped Amiga frame-rate measurement over WinUAE's side channel.
// Usage: node tools/debug/measure-side-fps.mjs <demo> [CONFIG] [options]
// Options: --samples N --interval-ms MS --port N --address 0xADDR --json
// The runner must have reached READY and remain attached/listening while this runs.
import * as fs from 'node:fs';
import * as net from 'node:net';
import * as path from 'node:path';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const args = process.argv.slice(2);
const option = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const demo = args[0];
if (!demo || demo.startsWith('--')) {
  console.error('Uso: node tools/debug/measure-side-fps.mjs <demo> [CONFIG] [--samples N] [--interval-ms MS] [--port N] [--address 0xADDR] [--json]');
  process.exit(2);
}

const config = args[1] && !args[1].startsWith('--') ? args[1] : 'A500_debug';
const sampleCount = Math.max(2, Math.min(10000, parseInt(option('--samples', '41'), 10)));
const intervalMs = Math.max(10, parseInt(option('--interval-ms', '250'), 10));
const jsonOutput = args.includes('--json');
const reportPath = path.join(ROOT, 'out/run', demo, config, 'run-report.json');
if (!fs.existsSync(reportPath)) {
  console.error(`[side-fps] falta ${reportPath}; ejecuta primero run-demo para generar el informe.`);
  process.exit(1);
}

const report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
function parseAddress(value) {
  if (typeof value === 'number' && Number.isFinite(value)) return value;
  if (typeof value !== 'string') return 0;
  const text = value.trim();
  if (/^0x/i.test(text)) return parseInt(text.slice(2), 16);
  return Number(text) || 0;
}

const runtimeAddress = parseAddress(option(
  '--address',
  report.sideChannel?.runtimeAddress ?? report.finalSideChannel?.address,
));
if (!runtimeAddress) {
  console.error(`[side-fps] ${reportPath} no tiene runtimeAddress de g_eng_run_status; usa --address 0xADDR.`);
  process.exit(1);
}
const port = parseInt(option(
  '--port',
  String(report.sideChannelPort || process.env.WINUAE_SIDE_CHANNEL_PORT || 2346),
), 10);

function findMapSymbol(mapPath, symbolName) {
  if (!mapPath || !fs.existsSync(mapPath)) return null;
  const re = new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${symbolName}\\b`);
  for (const line of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/)) {
    const match = line.match(re);
    if (match) return parseInt(match[1], 16);
  }
  return null;
}

function findMapSection(mapPath, linkedAddress) {
  if (!mapPath || !fs.existsSync(mapPath) || linkedAddress === null) return null;
  const wanted = new Set(['.text', '.rodata', '.eh_frame', '.data', '.bss']);
  for (const line of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/)) {
    const match = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
    if (!match || !wanted.has(match[1])) continue;
    const start = parseInt(match[2], 16);
    const size = parseInt(match[3], 16);
    if (size > 0 && linkedAddress >= start && linkedAddress < start + size) {
      return { name: match[1], start, end: start + size };
    }
  }
  return null;
}

// The DK update counter and run-status symbol share `.data`; use their linked offset
// from the run report's already validated runtime address rather than scanning RAM.
let updateCounterAddress = null;
const mapPath = report.map;
const linkedStatus = findMapSymbol(mapPath, 'g_eng_run_status');
const linkedCounter = findMapSymbol(mapPath, 'g_eng_frame_telemetry');
if (linkedStatus !== null && linkedCounter !== null &&
    findMapSection(mapPath, linkedStatus)?.name === '.data' &&
    findMapSection(mapPath, linkedCounter)?.name === '.data') {
  updateCounterAddress = runtimeAddress + linkedCounter - linkedStatus;
}

class SideChannelClient {
  // One persistent TCP connection for low-overhead, timestamped JSON-line samples.
  constructor(port) {
    this.port = port;
    this.socket = null;
    this.pending = '';
    this.lines = [];
    this.waiters = [];
  }

  async connect(timeoutMs = 4000) {
    await new Promise((resolve, reject) => {
      const socket = net.createConnection({ host: '127.0.0.1', port: this.port });
      const timer = setTimeout(() => {
        socket.destroy();
        reject(new Error(`timeout conectando al canal lateral ${this.port}`));
      }, timeoutMs);
      socket.once('connect', () => {
        clearTimeout(timer);
        this.socket = socket;
        socket.setEncoding('utf8');
        socket.on('data', (chunk) => this.onData(chunk));
        socket.on('error', () => {});
        resolve();
      });
      socket.once('error', (error) => {
        clearTimeout(timer);
        reject(error);
      });
    });
    await this.readLine(timeoutMs); // greeting before command/reply pairs
  }

  onData(chunk) {
    this.pending += chunk;
    for (;;) {
      const eol = this.pending.indexOf('\n');
      if (eol < 0) break;
      const line = this.pending.slice(0, eol).trim();
      this.pending = this.pending.slice(eol + 1);
      const waiter = this.waiters.shift();
      if (waiter) {
        clearTimeout(waiter.timer);
        waiter.resolve(line);
      } else {
        this.lines.push(line);
      }
    }
  }

  readLine(timeoutMs = 4000) {
    if (this.lines.length > 0) return Promise.resolve(this.lines.shift());
    return new Promise((resolve, reject) => {
      const waiter = {
        resolve,
        reject,
        timer: setTimeout(() => {
          const index = this.waiters.indexOf(waiter);
          if (index >= 0) this.waiters.splice(index, 1);
          reject(new Error('timeout esperando respuesta del canal lateral'));
        }, timeoutMs),
      };
      this.waiters.push(waiter);
    });
  }

  async command(command, timeoutMs = 4000) {
    if (!this.socket) throw new Error('canal lateral no conectado');
    this.socket.write(`${command}\n`);
    return JSON.parse(await this.readLine(timeoutMs));
  }

  close() {
    this.socket?.end();
    this.socket = null;
  }
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const sideAddress = (address) => `0x${(address >>> 0).toString(16)}`;
function u32delta(from, to) {
  return (to - from) >>> 0;
}

const client = new SideChannelClient(port);
try {
  await client.connect();
  const samples = [];
  for (let i = 0; i < sampleCount; ++i) {
    const status = await client.command(`runstatus ${sideAddress(runtimeAddress)}`);
    if (!status.ok || status.magic !== '0x454e4752' || status.version !== 1) {
      throw new Error(`runstatus inválido en ${sideAddress(runtimeAddress)}: ${JSON.stringify(status)}`);
    }
    if (status.state !== 3) {
      throw new Error(`la demo no está en READY durante la medida (state=${status.state}, frame=${status.frame})`);
    }

    let updateFrame = null;
    if (updateCounterAddress !== null) {
      const memory = await client.command(`mem ${sideAddress(updateCounterAddress)} 4`);
      const bytes = typeof memory.data === 'string' ? Buffer.from(memory.data, 'hex') : null;
      if (memory.ok && bytes && bytes.length >= 4) updateFrame = bytes.readUInt32BE(0);
    }
    samples.push({ atMs: performance.now(), frame: Number(status.frame), updateFrame });
    if (i + 1 < sampleCount) await sleep(intervalMs);
  }

  const first = samples[0];
  const last = samples[samples.length - 1];
  const elapsedMs = last.atMs - first.atMs;
  const frameDelta = u32delta(first.frame, last.frame);
  const windowRates = [];
  for (let i = 1; i < samples.length; ++i) {
    const dt = samples[i].atMs - samples[i - 1].atMs;
    windowRates.push(u32delta(samples[i - 1].frame, samples[i].frame) * 1000 / dt);
  }
  windowRates.sort((a, b) => a - b);

  const counterSamples = samples.filter((sample) => sample.updateFrame !== null);
  let updateCounter = null;
  if (counterSamples.length === samples.length) {
    const firstUpdate = counterSamples[0].updateFrame;
    const lastUpdate = counterSamples[counterSamples.length - 1].updateFrame;
    const updateDelta = u32delta(firstUpdate, lastUpdate);
    updateCounter = {
      first: firstUpdate,
      last: lastUpdate,
      delta: updateDelta,
      fps: Number((updateDelta * 1000 / elapsedMs).toFixed(2)),
    };
  }

  const result = {
    demo,
    config,
    port,
    runStatusAddress: sideAddress(runtimeAddress),
    samples: sampleCount,
    intervalMs,
    state: 3,
    firstFrame: first.frame,
    lastFrame: last.frame,
    deltaFrames: frameDelta,
    elapsedMs: Number(elapsedMs.toFixed(1)),
    fps: Number((frameDelta * 1000 / elapsedMs).toFixed(2)),
    windowMedianFps: Number(windowRates[Math.floor(windowRates.length / 2)].toFixed(2)),
    windowMinFps: Number(windowRates[0].toFixed(2)),
    windowMaxFps: Number(windowRates[windowRates.length - 1].toFixed(2)),
    updateCounter,
  };

  if (jsonOutput) {
    console.log(JSON.stringify(result, null, 2));
  } else {
    console.log(`[side-fps] ${demo}/${config}, state=3, ${sampleCount} muestras / ${result.elapsedMs} ms`);
    console.log(`[side-fps] runstatus.frame ${first.frame} -> ${last.frame}: ${result.fps} fps (ventanas ${result.windowMinFps}–${result.windowMedianFps}–${result.windowMaxFps})`);
    if (updateCounter) {
      console.log(`[side-fps] g_eng_frame_telemetry.frame ${updateCounter.first} -> ${updateCounter.last}: ${updateCounter.fps} updates/s`);
    }
  }
} catch (error) {
  console.error(`[side-fps] ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
} finally {
  client.close();
}
