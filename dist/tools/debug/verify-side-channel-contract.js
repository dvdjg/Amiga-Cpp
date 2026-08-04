#!/usr/bin/env node
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { spawn } from 'child_process';
import { repoRoot } from '../lib/paths.js';
const root = repoRoot(import.meta.url);
function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}
function argValue(name, fallback = undefined) {
    const index = process.argv.indexOf(name);
    if (index >= 0 && index + 1 < process.argv.length) {
        return process.argv[index + 1];
    }
    return fallback;
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
            const timer = setTimeout(() => {
                socket.destroy();
                reject(new Error(`timeout connecting to side channel port ${this.port}`));
            }, timeoutMs);
            socket.once('connect', () => {
                clearTimeout(timer);
                this.socket = socket;
                socket.setEncoding('utf8');
                socket.on('data', (chunk) => this.onData(chunk));
                socket.on('error', () => { });
                resolve(undefined);
            });
            socket.once('error', (err) => {
                clearTimeout(timer);
                reject(err);
            });
        });
        // El servidor saluda al conectar. Lo consumimos para que cada comando
        // posterior tenga una unica respuesta JSON, facil de validar en scripts.
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
            }
            else {
                this.lines.push(line);
            }
        }
    }
    readLine(timeoutMs) {
        if (this.lines.length > 0) {
            return Promise.resolve(this.lines.shift());
        }
        return new Promise((resolve, reject) => {
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
function assertOk(condition, message) {
    if (!condition) {
        throw new Error(message);
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
async function waitForProfileIdle(client, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let last = null;
    while (Date.now() <= deadline) {
        last = await client.command('profile-status', 2000);
        if (last.ok && !last.active) {
            return last;
        }
        await sleep(100);
    }
    throw new Error(`profile did not finish; last=${JSON.stringify(last)}`);
}
async function main() {
    const demo = argValue('--demo', 'demos\\030_ehb_palette_zones');
    const port = parseInt(argValue('--port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
    const settleMs = parseInt(argValue('--settle-ms', '9000'), 10);
    const readyTimeoutMs = parseInt(argValue('--ready-timeout-ms', '20000'), 10);
    const runOutDir = path.join(root, 'out', 'run', path.basename(demo));
    const stdoutLog = path.join(root, 'out', 'run', 'side-channel-contract.stdout.log');
    const stderrLog = path.join(root, 'out', 'run', 'side-channel-contract.stderr.log');
    const sideShot = path.join(runOutDir, 'side-channel-shot.png');
    const profile = path.join(runOutDir, 'side-channel-profile.bin');
    fs.mkdirSync(path.dirname(stdoutLog), { recursive: true });
    fs.rmSync(stdoutLog, { force: true });
    fs.rmSync(stderrLog, { force: true });
    fs.rmSync(sideShot, { force: true });
    fs.rmSync(profile, { force: true });
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
    try {
        const report = {};
        report.state = await client.command('state');
        assertOk(report.state.ok && report.state.gdbConnected, 'state must report a live GDB session');
        report.inputWithoutLock = await client.command('input mouse move 1 1');
        assertOk(report.inputWithoutLock.error === 'lock_required', 'input must require assist/takeover lock');
        report.lockAcquire = await client.command('lock acquire codex assist');
        assertOk(report.lockAcquire.ok && report.lockAcquire.locked && report.lockAcquire.mode === 'assist', 'assist lock must be acquired');
        report.inputQueued = await client.command('input mouse abs 80 60');
        assertOk(report.inputQueued.ok && report.inputQueued.status === 'queued', 'input action must be queued');
        report.inputDone = await waitForAction(client, report.inputQueued.id, 3000);
        assertOk(report.inputDone.result?.ok, 'input action must finish ok');
        report.screenshotQueued = await client.command(`screenshot "${sideShot}"`);
        assertOk(report.screenshotQueued.ok && report.screenshotQueued.status === 'queued', 'screenshot action must be queued');
        report.screenshotDone = await waitForAction(client, report.screenshotQueued.id, 5000);
        assertOk(report.screenshotDone.result?.ok && fs.existsSync(sideShot), `screenshot action must write a PNG; result=${JSON.stringify(report.screenshotDone)} path=${sideShot}`);
        report.profileQueued = await client.command(`profile 1 "${profile}"`);
        assertOk(report.profileQueued.ok && report.profileQueued.status === 'queued', 'profile action must be queued');
        report.profileAction = await waitForAction(client, report.profileQueued.id, 3000);
        assertOk(report.profileAction.result?.ok, 'profile action must start ok');
        report.profileStatus = await waitForProfileIdle(client, 8000);
        assertOk(report.profileStatus.status === 'done' && fs.existsSync(profile), 'profile must finish and write an artifact');
        report.lockRelease = await client.command('lock release codex');
        assertOk(report.lockRelease.ok && !report.lockRelease.locked, 'lock must be released');
        report.artifacts = {
            screenshot: sideShot,
            screenshotBytes: fs.statSync(sideShot).size,
            profile,
            profileBytes: fs.statSync(profile).size,
        };
        console.log(JSON.stringify(report, null, 2));
    }
    finally {
        client.close();
    }
    const exitCode = await new Promise((resolve) => {
        runner.once('exit', (code) => resolve(code));
    });
    assertOk(exitCode === 0, `runner exited with ${exitCode}\n${stdoutText}\n${stderrText}`);
}
main().catch((err) => {
    console.error(err.stack || err.message);
    process.exit(1);
});
