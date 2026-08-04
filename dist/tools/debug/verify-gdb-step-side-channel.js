#!/usr/bin/env node
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { spawn } from 'child_process';
import { pathToFileURL } from 'url';
import { repoRoot } from '../lib/paths.js';
const root = repoRoot(import.meta.url);
// El conector de WinUAE (repo hermano mcp-winuae-emu) vive junto al repositorio.
// Se importa dinamicamente desde la raiz para que la ruta valga tanto en el
// fuente como en dist/ (compilar anade un nivel de directorio).
const mcpWinuae = await import(pathToFileURL(path.join(path.dirname(root), 'mcp-winuae-emu', 'dist', 'winuae-connection.js')).href);
const { WinUAEConnection } = mcpWinuae;
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
function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}
function assertOk(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
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
    const candidates = [
        path.join(process.env.USERPROFILE || '', '.cursor/extensions/bartmanabyss.amiga-debug-1.8.2'),
        path.join(process.env.USERPROFILE || '', '.vscode/extensions/bartmanabyss.amiga-debug-1.8.2'),
    ];
    for (const candidate of candidates) {
        if (fs.existsSync(path.join(candidate, 'bin/win32/winuae-gdb.exe'))) {
            return candidate;
        }
    }
    throw new Error('No se encontro la extension bartmanabyss.amiga-debug-1.8.2.');
}
function patchConfig(configText, extensionRoot, stagedOutDir) {
    const dh0 = path.join(extensionRoot, 'bin/dh0');
    const normalizedDh0 = dh0.replace(/\//g, '\\');
    const normalizedOut = stagedOutDir.replace(/\//g, '\\');
    let out = configText;
    out = out.replace(/^filesystem=rw,dh0:.*$/m, `filesystem=rw,dh0:${normalizedDh0}`);
    if (/^filesystem2=rw,dh1:.*$/m.test(out)) {
        out = out.replace(/^filesystem2=rw,dh1:.*$/m, `filesystem2=rw,dh1:dh1:${normalizedOut},-128`);
    }
    else {
        out += `\r\nfilesystem2=rw,dh1:dh1:${normalizedOut},-128\r\n`;
    }
    out = setConfigValue(out, 'debugging_trigger', ':a.exe');
    out = setConfigValue(out, 'win32.start_not_captured', 'yes');
    out = setConfigValue(out, 'win32.active_capture_automatically', 'no');
    out = setConfigValue(out, 'win32.absolute_mouse', 'yes');
    out = setConfigValue(out, 'absolute_mouse', 'none');
    out = setConfigValue(out, 'warp', 'true');
    return out;
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
    if (typeof value === 'number') {
        return value;
    }
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
    await new Promise((resolve, reject) => {
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
            }
            else {
                reject(new Error(`build-demo failed with exit code ${code}`));
            }
        });
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
async function pollSideWhile(waitPromise, side, samples, timeoutMs) {
    let settled = false;
    const wrapped = waitPromise.finally(() => {
        settled = true;
    });
    while (!settled) {
        samples.push(await side.command('state', timeoutMs));
        await sleep(100);
    }
    return wrapped;
}
async function main() {
    const demo = argValue('--demo', 'demos\\030_ehb_palette_zones');
    const sideChannelPort = parseInt(argValue('--side-channel-port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
    const stepCount = Math.max(1, parseInt(argValue('--steps', '3'), 10));
    const demoName = path.basename(demo);
    await runBuild(demo);
    const builtExe = path.join(root, 'out/demos', demoName, `${demoName}.exe`);
    const builtMap = path.join(root, 'out/demos', demoName, `${demoName}.map`);
    const mapSections = findMapAllocSections(builtMap);
    const readyProbeSymbol = findMapSymbol(builtMap, 'eng_debug_ready_probe');
    const runStatusSymbol = findMapSymbol(builtMap, 'g_eng_run_status');
    assertOk(fs.existsSync(builtExe), `No existe ${builtExe}`);
    assertOk(readyProbeSymbol !== null, 'No se encontro eng_debug_ready_probe en el map');
    assertOk(runStatusSymbol !== null, 'No se encontro g_eng_run_status en el map');
    const extensionRoot = findExtensionRoot();
    const outputDir = path.join(root, 'out/run', demoName);
    const stagedDir = path.join(outputDir, 'dh1');
    fs.mkdirSync(stagedDir, { recursive: true });
    fs.copyFileSync(builtExe, path.join(stagedDir, 'a.exe'));
    const dh0 = path.join(extensionRoot, 'bin/dh0');
    const startupPath = path.join(dh0, 's/startup-sequence');
    fs.mkdirSync(path.dirname(startupPath), { recursive: true });
    const previousStartup = fs.existsSync(startupPath) ? fs.readFileSync(startupPath, 'utf8') : null;
    fs.writeFileSync(startupPath, 'cd dh1:\n:a.exe\n', 'utf8');
    const runnerConfigPath = path.join(outputDir, 'gdb-step-side-channel.uae');
    const configText = fs.readFileSync(path.join(root, 'config/mcp-amiga-c-debug.uae'), 'utf8');
    fs.writeFileSync(runnerConfigPath, patchConfig(configText, extensionRoot, stagedDir), 'utf8');
    const conn = new WinUAEConnection({
        winuaePath: path.join(extensionRoot, 'bin/win32'),
        configFile: runnerConfigPath,
        gdbPort: parseInt(process.env.WINUAE_GDB_PORT || '2345', 10),
    });
    const side = new SideChannelClient(sideChannelPort);
    const report = {
        demo: demoName,
        breakpoint: {
            symbol: 'eng_debug_ready_probe',
            linked: hex32(readyProbeSymbol),
        },
        sideChannelPort,
        steps: [],
        sideSamplesWhileRunning: [],
    };
    try {
        await conn.connect({ forceBreak: false, initializeStopped: true });
        const protocol = conn.getProtocol();
        await side.connect();
        report.initialSideState = await side.command('state');
        assertOk(report.initialSideState.ok && report.initialSideState.gdbConnected, 'El canal lateral debe ver GDB conectado');
        const runtimeReadyProbe = resolveRuntimeSymbolAddress(readyProbeSymbol, mapSections, report.initialSideState.sections);
        const runtimeRunStatus = resolveRuntimeSymbolAddress(runStatusSymbol, mapSections, report.initialSideState.sections);
        assertOk(runtimeReadyProbe !== null && runtimeReadyProbe > 0, 'No se pudo resolver ready_probe runtime');
        assertOk(runtimeRunStatus !== null && runtimeRunStatus > 0, 'No se pudo resolver g_eng_run_status runtime');
        report.breakpoint.runtime = hex32(runtimeReadyProbe);
        report.runStatusRuntime = hex32(runtimeRunStatus);
        await protocol.setBreakpoint(readyProbeSymbol);
        report.breakpoint.set = true;
        await protocol.continue();
        const stopReply = await pollSideWhile(protocol.waitForStop(6000), side, report.sideSamplesWhileRunning, 1000);
        report.breakpoint.stopReply = stopReply;
        assertOk(/^S|^T/.test(stopReply), `Stop reply inesperado: ${stopReply}`);
        assertOk(report.sideSamplesWhileRunning.length > 0, 'Debe haber muestras laterales mientras GDB esperaba el breakpoint');
        assertOk(report.sideSamplesWhileRunning.every((sample) => sample.ok && sample.gdbConnected), 'Todas las muestras laterales deben ver GDB conectado');
        const regsAtBreak = await protocol.readRegisters();
        const sideRegsAtBreak = await side.command('regs');
        report.breakpoint.pc = hex32(regsAtBreak.PC);
        report.breakpoint.sidePc = sideRegsAtBreak.pc;
        assertOk(Math.abs(regsAtBreak.PC - runtimeReadyProbe) <= 8, `PC no esta cerca del breakpoint runtime: pc=${hex32(regsAtBreak.PC)} bp=${hex32(runtimeReadyProbe)}`);
        for (let i = 0; i < stepCount; i++) {
            const before = await protocol.readRegisters();
            const reply = await protocol.step();
            const after = await protocol.readRegisters();
            const sideState = await side.command('state');
            report.steps.push({
                index: i + 1,
                reply,
                beforePc: hex32(before.PC),
                afterPc: hex32(after.PC),
                sidePc: sideState.pc,
                sideDebuggerState: sideState.debuggerState,
            });
            assertOk(/^S|^T/.test(reply), `Step ${i + 1} no paro correctamente: ${reply}`);
            assertOk(after.PC !== before.PC, `Step ${i + 1} no avanzo PC: ${hex32(before.PC)}`);
            assertOk(sideState.ok && sideState.gdbConnected, `Canal lateral no responde tras step ${i + 1}`);
        }
        await protocol.clearBreakpoint(readyProbeSymbol);
        report.breakpoint.cleared = true;
        const status = await side.command(`runstatus ${runtimeRunStatus.toString(16)}`);
        report.runStatus = status;
        assertOk(status.ok && status.magic === '0x454e4752', 'runstatus lateral debe seguir siendo legible');
        console.log(JSON.stringify(report, null, 2));
    }
    finally {
        side.close();
        try {
            await conn.disconnect(true);
        }
        catch {
            // La salida de WinUAE no debe ocultar el diagnostico principal.
        }
        if (previousStartup !== null) {
            fs.writeFileSync(startupPath, previousStartup, 'utf8');
        }
    }
}
main().catch((err) => {
    console.error(err.stack || err.message);
    process.exit(1);
});
