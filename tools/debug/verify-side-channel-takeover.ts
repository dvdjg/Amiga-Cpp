#!/usr/bin/env node
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { spawn } from 'child_process';
import { fileURLToPath } from 'url';
import { repoRoot } from '../lib/paths.js';
const root = repoRoot(import.meta.url);
function sleep(ms) {
  return new Promise<any>((resolve) => setTimeout(resolve, ms));
}
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
function assertOk(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}
function findMapSymbol(mapPath, symbolName) {
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
    if (size > 0) {
      sections.push({ name: match[1], start, size, end: start + size });
    }
  }
  return sections;
}
function parseHexNumber(value) {
  const text = String(value || '').trim();
  return parseInt(text.startsWith('0x') || text.startsWith('0X') ? text.slice(2) : text, 16);
}
function resolveRuntimeSymbolAddress(linkedSymbol, mapSections, runtimeSections) {
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
function hex32(value) {
  return `0x${(value >>> 0).toString(16).padStart(8, '0')}`;
}
async function runBuild(demo) {
  if (hasArg('--skip-build')) {
    return;
  }
  await new Promise<any>((resolve, reject) => {
    const child = spawn('bash', [
      'tools/build/build-demo.sh',
      demo,
      '--debug',
    ], {
      cwd: root,
      windowsHide: true,
      stdio: 'inherit',
    });
    child.once('exit', (code) => {
      if (code === 0) {
        resolve(undefined);
      } else {
        reject(new Error(`build-demo failed with exit code ${code}`));
      }
    });
  });
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
  async connect(timeoutMs = 2000) {
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
    return JSON.parse(await this.readLine(timeoutMs));
  }
  async command(command, timeoutMs = 2000) {
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
async function waitForAction(client, id, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last = null;
  while (Date.now() <= deadline) {
    last = await client.command(`action status ${id}`, 2000);
    if (last.ok && last.done) {
      return last;
    }
    await sleep(100);
  }
  throw new Error(`action ${id} did not finish; last=${JSON.stringify(last)}`);
}
async function main() {
  const demo = argValue('--demo', 'demos\\030_ehb_palette_zones');
  const demoName = path.basename(demo);
  const port = parseInt(argValue('--port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
  const settleMs = parseInt(argValue('--settle-ms', '9000'), 10);
  const readyTimeoutMs = parseInt(argValue('--ready-timeout-ms', '20000'), 10);
  const builtMap = path.join(root, 'out/demos', demoName, `${demoName}.map`);
  await runBuild(demo);
  const runStatusSymbol = findMapSymbol(builtMap, 'g_eng_run_status');
  const mapSections = findMapAllocSections(builtMap);
  assertOk(runStatusSymbol !== null, 'No se encontro g_eng_run_status en el map');
  const stdoutLog = path.join(root, 'out', 'run', 'side-channel-takeover.stdout.log');
  const stderrLog = path.join(root, 'out', 'run', 'side-channel-takeover.stderr.log');
  fs.mkdirSync(path.dirname(stdoutLog), { recursive: true });
  fs.rmSync(stdoutLog, { force: true });
  fs.rmSync(stderrLog, { force: true });
  const runner = spawn('bash', [
    'tools/run/run-demo.sh',
    demo,
    '--side-channel-timeout-ms', '6000',
    '--settle-ms', String(settleMs),
  ], {
    cwd: root,
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let stdoutText = '';
  let stderrText = '';
  runner.stdout.on('data', (chunk) => {
    stdoutText += chunk.toString();
    fs.appendFileSync(stdoutLog, chunk);
  });
  runner.stderr.on('data', (chunk) => {
    stderrText += chunk.toString();
    fs.appendFileSync(stderrLog, chunk);
  });
  const readyDeadline = Date.now() + readyTimeoutMs;
  while (!stdoutText.includes('side-channel READY') && Date.now() <= readyDeadline) {
    if (runner.exitCode !== null) {
      throw new Error(`runner exited before READY (${runner.exitCode})\n${stdoutText}\n${stderrText}`);
    }
    await sleep(100);
  }
  assertOk(stdoutText.includes('side-channel READY'), 'runner did not reach side-channel READY');
  const client = new SideChannelClient(port);
  await client.connect();
  const report: Record<string, any> = {};
  try {
    report.state = await client.command('state');
    assertOk(report.state.ok && report.state.gdbConnected, 'state must report live GDB');
    const runStatusRuntime = resolveRuntimeSymbolAddress(runStatusSymbol, mapSections, report.state.sections);
    assertOk(runStatusRuntime !== null && runStatusRuntime > 0, 'No se pudo resolver g_eng_run_status runtime');
    const detailAddress = runStatusRuntime + 12;
    report.runStatusRuntime = hex32(runStatusRuntime);
    report.detailAddress = hex32(detailAddress);
    report.originalDetail = await client.command(`mem ${detailAddress.toString(16)} 4`);
    assertOk(report.originalDetail.ok && report.originalDetail.data.length === 8, 'No se pudo leer detail original');
    report.pokeWithoutLock = await client.command(`poke ${detailAddress.toString(16)} 12345678 takeover-test`);
    assertOk(report.pokeWithoutLock.error === 'lock_required', 'poke debe requerir takeover lock');
    report.lockAcquire = await client.command('lock acquire codex takeover');
    assertOk(report.lockAcquire.ok && report.lockAcquire.mode === 'takeover', 'No se pudo tomar takeover lock');
    report.pokeQueued = await client.command(`poke ${detailAddress.toString(16)} 12345678 takeover-test`);
    assertOk(report.pokeQueued.ok && report.pokeQueued.status === 'queued', 'poke no se encolo');
    report.pokeDone = await waitForAction(client, report.pokeQueued.id, 3000);
    assertOk(report.pokeDone.result?.ok && report.pokeDone.result?.writeId > 0, `poke fallo: ${JSON.stringify(report.pokeDone)}`);
    assertOk(report.pokeDone.result.before === report.originalDetail.data, 'La auditoria debe conservar el valor original');
    assertOk(report.pokeDone.result.after === '12345678', 'La auditoria debe conservar el valor nuevo');
    report.afterPoke = await client.command(`mem ${detailAddress.toString(16)} 4`);
    assertOk(report.afterPoke.data === '12345678', 'La memoria no contiene el valor escrito');
    report.audit = await client.command(`audit write ${report.pokeDone.result.writeId}`);
    assertOk(report.audit.ok && report.audit.writes.length === 1 && !report.audit.writes[0].rolledBack, 'La auditoria no registro la escritura');
    report.rollbackQueued = await client.command(`rollback ${report.pokeDone.result.writeId}`);
    assertOk(report.rollbackQueued.ok && report.rollbackQueued.status === 'queued', 'rollback no se encolo');
    report.rollbackDone = await waitForAction(client, report.rollbackQueued.id, 3000);
    assertOk(report.rollbackDone.result?.ok, `rollback fallo: ${JSON.stringify(report.rollbackDone)}`);
    report.afterRollback = await client.command(`mem ${detailAddress.toString(16)} 4`);
    assertOk(report.afterRollback.data === report.originalDetail.data, 'Rollback no restauro el valor original');
    report.auditAfterRollback = await client.command(`audit write ${report.pokeDone.result.writeId}`);
    assertOk(report.auditAfterRollback.writes[0].rolledBack === true, 'La auditoria no marco rollback');
    report.lockRelease = await client.command('lock release codex');
    assertOk(report.lockRelease.ok && !report.lockRelease.locked, 'No se pudo liberar takeover lock');
    console.log(JSON.stringify(report, null, 2));
  } finally {
    client.close();
  }
  const exitCode = await new Promise<any>((resolve) => {
    runner.once('exit', (code) => resolve(code));
  });
  assertOk(exitCode === 0, `runner exited with ${exitCode}\n${stdoutText}\n${stderrText}`);
}
main().catch((err) => {
  console.error(err.stack || err.message);
  process.exit(1);
});
