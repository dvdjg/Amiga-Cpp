#!/usr/bin/env node
// ---------------------------------------------------------------------------
// Verificador determinista del test L0-020 math_scalars.
//
// 1) compila el test; 2) lanza `run-demo.sh` (que espera READY por el canal lateral);
// 3) resuelve `g_math_report` desde el .map + su direccion runtime y lo lee por el canal
// lateral; 4) imprime la tabla de casos y exige `failed_count == 0`.
//
// El test no usa float: el veredicto sale de comparar enteros (max_err vs tol, en
// unidades de 1/4096). Este script solo formatea y agrega.
//
// Uso: tests/amiga/l0_bare_metal/020_math_scalars/verify-math.sh [--demo <ruta>] [--skip-build]
//      [--port N] [--wait-ms N] [--keep]
// ---------------------------------------------------------------------------
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { spawn } from 'child_process';
import { repoRoot } from '../../../../tools/lib/paths.js';
const root = repoRoot(import.meta.url);
function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}
function argValue(name, fallback) {
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
// --- Resolucion de simbolos desde el .map (mismo patron que tools/debug) ----
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
            sections.push({ name: match[1], start, end: start + size });
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
// --- Cliente del canal lateral ---------------------------------------------
class SideChannelClient {
    constructor(port) {
        this.socket = null;
        this.pending = '';
        this.lines = [];
        this.waiters = [];
        this.port = port;
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
                resolve();
            });
            socket.once('error', (err) => {
                clearTimeout(timer);
                reject(err);
            });
        });
        await this.readLine(timeoutMs); // saludo del servidor
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
            const waiter = this.waiters.shift();
            if (waiter) {
                waiter(line);
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
            const timer = setTimeout(() => reject(new Error('timeout reading side channel line')), timeoutMs);
            this.waiters.push((line) => {
                clearTimeout(timer);
                resolve(line);
            });
        });
    }
    async command(cmd, timeoutMs = 2000) {
        this.socket?.write(`${cmd}\n`);
        const line = await this.readLine(timeoutMs);
        return JSON.parse(line);
    }
    close() {
        try {
            this.socket?.end();
        }
        catch {
            /* ignora */
        }
    }
}
// --- Build + runner --------------------------------------------------------
async function runBuild(demo) {
    if (hasArg('--skip-build')) {
        return;
    }
    await new Promise((resolve, reject) => {
        const child = spawn('bash', ['tools/build/build-demo.sh', demo, '--debug'], {
            cwd: root,
            windowsHide: true,
            stdio: 'inherit',
        });
        child.on('exit', (code) => (code === 0 ? resolve() : reject(new Error(`build fallo (${code})`))));
        child.on('error', reject);
    });
}
// --- Decodificacion del reporte --------------------------------------------
const HDR = 20;
const CASE_SIZE = 32;
const MATH_MAGIC = 0x4d415448;
function parseHexBytes(hex) {
    const clean = String(hex || '').replace(/[^0-9a-fA-F]/g, '');
    return Buffer.from(clean.length % 2 === 0 ? clean : clean.slice(0, -1), 'hex');
}
function decodeReport(buf) {
    const magic = buf.readUInt32BE(0);
    const version = buf.readUInt32BE(4);
    const count = buf.readUInt32BE(8);
    const failed = buf.readUInt32BE(12);
    const checks = buf.readUInt32BE(16);
    const cases = [];
    for (let i = 0; i < count; i++) {
        const offset = HDR + i * CASE_SIZE;
        let name = '';
        for (let k = 0; k < 20; k++) {
            const code = buf[offset + k];
            if (code === 0) {
                break;
            }
            name += String.fromCharCode(code);
        }
        cases.push({
            name,
            pass: buf.readInt16BE(offset + 20),
            kind: buf.readInt16BE(offset + 22),
            maxErr: buf.readInt32BE(offset + 24),
            tol: buf.readInt32BE(offset + 28),
        });
    }
    return { magic, version, count, failed, checks, cases };
}
const KIND_NAMES = ['MF  ', 'q12 ', 'q8  ', 'q0/int', 'mix '];
async function main() {
    const demo = argValue('--demo', 'tests\\l0_bare_metal\\020_math_scalars');
    const demoName = path.basename(demo);
    const port = parseInt(argValue('--port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
    const readyTimeoutMs = parseInt(argValue('--ready-timeout-ms', '30000'), 10);
    const waitMs = parseInt(argValue('--wait-ms', '12000'), 10);
    const builtMap = path.join(root, 'out/demos', demoName, 'A500_debug', `${demoName}.A500_debug.map`);
    await runBuild(demo);
    const reportSymbol = findMapSymbol(builtMap, 'g_math_report');
    const runStatusSymbol = findMapSymbol(builtMap, 'g_eng_run_status');
    const mapSections = findMapAllocSections(builtMap);
    assertOk(reportSymbol !== null, 'No se encontro g_math_report en el map');
    assertOk(runStatusSymbol !== null, 'No se encontro g_eng_run_status en el map');
    const runner = spawn('bash', ['tools/run/run-demo.sh', demo, '--wait-ms', String(waitMs), '--settle-ms', '1200', '--side-channel-timeout-ms', '6000'], { cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdoutText = '';
    let stderrText = '';
    runner.stdout.on('data', (chunk) => (stdoutText += chunk.toString()));
    runner.stderr.on('data', (chunk) => (stderrText += chunk.toString()));
    const deadline = Date.now() + readyTimeoutMs;
    while (!stdoutText.includes('side-channel READY') && Date.now() <= deadline) {
        if (runner.exitCode !== null) {
            throw new Error(`runner salio antes de READY (${runner.exitCode})\n${stdoutText}\n${stderrText}`);
        }
        await sleep(100);
    }
    assertOk(stdoutText.includes('side-channel READY'), 'runner no alcanzo side-channel READY');
    const client = new SideChannelClient(port);
    let failed = true;
    try {
        await client.connect();
        const state = await client.command('state');
        assertOk(state.ok, `state no responde: ${JSON.stringify(state)}`);
        const runtime = resolveRuntimeSymbolAddress(reportSymbol, mapSections, state.sections);
        assertOk(runtime !== null && runtime > 0, 'No se pudo resolver g_math_report en runtime');
        const headerHex = await client.command(`mem ${runtime.toString(16)} ${HDR}`, 3000);
        assertOk(headerHex.ok, `no se pudo leer la cabecera: ${JSON.stringify(headerHex)}`);
        const header = parseHexBytes(headerHex.data);
        const count = header.readUInt32BE(8);
        assertOk(count > 0 && count <= 100, `case_count inesperado: ${count}`);
        const casesHex = await client.command(`mem ${(runtime + HDR).toString(16)} ${count * CASE_SIZE}`, 5000);
        assertOk(casesHex.ok, `no se pudieron leer los casos: ${JSON.stringify(casesHex)}`);
        const report = decodeReport(Buffer.concat([header, parseHexBytes(casesHex.data)]));
        assertOk(report.magic === MATH_MAGIC, `magic inesperado: 0x${report.magic.toString(16)}`);
        console.log(`\n[math] g_math_report v${report.version}: ${report.count} casos, ${report.checks} comprobaciones`);
        let printed = 0;
        for (const c of report.cases) {
            const tag = c.pass ? 'OK  ' : 'FAIL';
            const line = `  ${tag} ${KIND_NAMES[c.kind] ?? '?   '} ${c.name.padEnd(30)} err=${String(c.maxErr).padStart(7)} tol=${String(c.tol).padStart(6)}`;
            if (!c.pass || hasArg('--verbose')) {
                console.log(line);
                printed++;
            }
        }
        if (printed === 0) {
            console.log('  (todos los casos dentro de tolerancia)');
        }
        console.log(`[math] fallos: ${report.failed}/${report.count}`);
        failed = report.failed !== 0;
    }
    finally {
        client.close();
        if (!hasArg('--keep')) {
            try {
                runner.kill();
            }
            catch {
                /* ignora */
            }
        }
    }
    if (failed) {
        throw new Error('LA BATERIA DE MATEMATICAS TIENE FALLOS (ver tabla)');
    }
    console.log('[math] OK: todos los casos (MF/q12/q8/q0 + inter-tipo) dentro de tolerancia.');
}
main().catch((error) => {
    console.error(`[math] FAIL: ${error.message}`);
    process.exit(1);
});
