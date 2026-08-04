#!/usr/bin/env node
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { spawn } from 'child_process';
import { repoRoot } from '../lib/paths.js';
const root = repoRoot(import.meta.url);
function sleep(ms) { return new Promise((resolve) => setTimeout(resolve, ms)); }
function argValue(name, fallback = undefined) {
    const index = process.argv.indexOf(name);
    return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : fallback;
}
function hasArg(name) { return process.argv.includes(name); }
function assertOk(condition, message) { if (!condition)
    throw new Error(message); }
function findMapSymbol(mapPath, symbolName) {
    const re = new RegExp(`^\\s*0x([0-9a-fA-F]+)\\s+${symbolName}\\b`);
    for (const line of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g)) {
        const match = line.match(re);
        if (match)
            return parseInt(match[1], 16);
    }
    return null;
}
function findMapAllocSections(mapPath) {
    const wanted = new Set(['.text', '.rodata', '.data', '.bss']);
    const sections = [];
    for (const line of fs.readFileSync(mapPath, 'utf8').split(/\r?\n/g)) {
        const match = line.match(/^(\.[A-Za-z0-9_.]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)/);
        if (!match || !wanted.has(match[1]))
            continue;
        const start = parseInt(match[2], 16);
        const size = parseInt(match[3], 16);
        if (size > 0)
            sections.push({ name: match[1], start, size, end: start + size });
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
    return runtimeSections.length > hunkIndex ? parseHexNumber(runtimeSections[hunkIndex]) + (linkedSymbol - section.start) : null;
}
function hex32(value) { return `0x${(value >>> 0).toString(16).padStart(8, '0')}`; }
async function runBuild(demo) {
    if (hasArg('--skip-build'))
        return;
    await new Promise((resolve, reject) => {
        const child = spawn('bash', ['tools/build/build-demo.sh', demo, '--debug'], {
            cwd: root, windowsHide: true, stdio: 'inherit',
        });
        child.once('exit', (code) => code === 0 ? resolve(undefined) : reject(new Error(`build-demo failed with exit code ${code}`)));
    });
}
class SideChannelClient {
    constructor(port) {
        this.port = port;
        this.socket = null;
        this.pending = '';
        this.lines = [];
        this.waiters = [];
    }
    async connect(timeoutMs = 2000) {
        await new Promise((resolve, reject) => {
            const socket = net.createConnection({ host: '127.0.0.1', port: this.port });
            const timer = setTimeout(() => { socket.destroy(); reject(new Error(`timeout connecting to side channel port ${this.port}`)); }, timeoutMs);
            socket.once('connect', () => {
                clearTimeout(timer);
                this.socket = socket;
                socket.setEncoding('utf8');
                socket.on('data', (chunk) => this.onData(chunk));
                socket.on('error', () => { });
                resolve(undefined);
            });
            socket.once('error', (err) => { clearTimeout(timer); reject(err); });
        });
        await this.readJsonLine(timeoutMs);
    }
    onData(chunk) {
        this.pending += chunk;
        for (;;) {
            const eol = this.pending.indexOf('\n');
            if (eol < 0)
                break;
            const line = this.pending.slice(0, eol).trim();
            this.pending = this.pending.slice(eol + 1);
            if (this.waiters.length > 0)
                this.waiters.shift().resolve(line);
            else
                this.lines.push(line);
        }
    }
    readLine(timeoutMs) {
        if (this.lines.length > 0)
            return Promise.resolve(this.lines.shift());
        return new Promise((resolve, reject) => {
            const waiter = { resolve, reject, timer: null };
            waiter.timer = setTimeout(() => {
                const index = this.waiters.indexOf(waiter);
                if (index >= 0)
                    this.waiters.splice(index, 1);
                reject(new Error('side channel reply timeout'));
            }, timeoutMs);
            waiter.resolve = (line) => { clearTimeout(waiter.timer); resolve(line); };
            this.waiters.push(waiter);
        });
    }
    async readJsonLine(timeoutMs) { return JSON.parse(await this.readLine(timeoutMs)); }
    async command(command, timeoutMs = 2000) {
        this.socket.write(`${command}\n`);
        return this.readJsonLine(timeoutMs);
    }
    close() {
        if (this.socket)
            this.socket.destroy();
        this.socket = null;
    }
}
async function waitForAction(client, id, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let last = null;
    while (Date.now() <= deadline) {
        last = await client.command(`action status ${id}`, 2000);
        if (last.ok && last.done)
            return last;
        await sleep(100);
    }
    throw new Error(`action ${id} did not finish; last=${JSON.stringify(last)}`);
}
async function waitForState(client, predicate, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let last = null;
    while (Date.now() <= deadline) {
        last = await client.command('state', 2000);
        if (predicate(last))
            return last;
        await sleep(100);
    }
    throw new Error(`state did not match; last=${JSON.stringify(last)}`);
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
    const stdoutLog = path.join(root, 'out', 'run', 'side-channel-pause-resume.stdout.log');
    const stderrLog = path.join(root, 'out', 'run', 'side-channel-pause-resume.stderr.log');
    fs.mkdirSync(path.dirname(stdoutLog), { recursive: true });
    fs.rmSync(stdoutLog, { force: true });
    fs.rmSync(stderrLog, { force: true });
    const runner = spawn('bash', ['tools/run/run-demo.sh', demo, '--side-channel-timeout-ms', '6000', '-SettleMs', String(settleMs)], {
        cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
    });
    let stdoutText = '';
    let stderrText = '';
    runner.stdout.on('data', (chunk) => { stdoutText += chunk.toString(); fs.appendFileSync(stdoutLog, chunk); });
    runner.stderr.on('data', (chunk) => { stderrText += chunk.toString(); fs.appendFileSync(stderrLog, chunk); });
    const readyDeadline = Date.now() + readyTimeoutMs;
    while (!stdoutText.includes('side-channel READY') && Date.now() <= readyDeadline) {
        if (runner.exitCode !== null)
            throw new Error(`runner exited before READY (${runner.exitCode})\n${stdoutText}\n${stderrText}`);
        await sleep(100);
    }
    assertOk(stdoutText.includes('side-channel READY'), 'runner did not reach side-channel READY');
    const client = new SideChannelClient(port);
    await client.connect();
    const report = {};
    try {
        report.stateBefore = await client.command('state');
        assertOk(report.stateBefore.ok && report.stateBefore.gdbConnected, 'state must report live GDB');
        const runStatusRuntime = resolveRuntimeSymbolAddress(runStatusSymbol, mapSections, report.stateBefore.sections);
        assertOk(runStatusRuntime !== null && runStatusRuntime > 0, 'No se pudo resolver g_eng_run_status runtime');
        report.runStatusRuntime = hex32(runStatusRuntime);
        report.pauseWithoutLock = await client.command('pause');
        assertOk(report.pauseWithoutLock.error === 'lock_required', 'pause debe requerir takeover lock');
        report.lockAcquire = await client.command('lock acquire codex takeover');
        assertOk(report.lockAcquire.ok && report.lockAcquire.mode === 'takeover', 'No se pudo tomar takeover lock');
        report.pauseQueued = await client.command('pause');
        assertOk(report.pauseQueued.ok && report.pauseQueued.status === 'queued', 'pause no se encolo');
        report.pauseDone = await waitForAction(client, report.pauseQueued.id, 3000);
        assertOk(report.pauseDone.result?.ok, `pause fallo: ${JSON.stringify(report.pauseDone)}`);
        report.statePaused = await waitForState(client, (state) => state.ok && state.debuggerState === 2, 3000);
        report.memWhilePaused = await client.command(`mem ${runStatusRuntime.toString(16)} 16`);
        assertOk(report.memWhilePaused.ok && report.memWhilePaused.data.startsWith('414d4752'), 'mem debe funcionar mientras esta pausado');
        report.resumeDone = await client.command('resume');
        assertOk(report.resumeDone.ok && (report.resumeDone.status === 'running' || report.resumeDone.status === 'already_running'), `resume fallo: ${JSON.stringify(report.resumeDone)}`);
        report.stateRunning = await waitForState(client, (state) => state.ok && state.debuggerState === 1, 3000);
        report.lockRelease = await client.command('lock release codex');
        assertOk(report.lockRelease.ok && !report.lockRelease.locked, 'No se pudo liberar takeover lock');
        console.log(JSON.stringify(report, null, 2));
    }
    finally {
        client.close();
    }
    const exitCode = await new Promise((resolve) => runner.once('exit', (code) => resolve(code)));
    assertOk(exitCode === 0, `runner exited with ${exitCode}\n${stdoutText}\n${stderrText}`);
}
main().catch((err) => {
    console.error(err.stack || err.message);
    process.exit(1);
});
