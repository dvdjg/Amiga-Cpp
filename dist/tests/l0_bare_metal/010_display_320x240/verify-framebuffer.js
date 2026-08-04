#!/usr/bin/env node
// Verificador determinista del test L0-010 display_320x240.
//
// Flujo:
//  1. Compila el test (a menos que --skip-build).
//  2. Lanza run-demo.sh (deja WinUAE viva) y espera READY por canal lateral.
//  3. Lee g_test_contract por el canal lateral y comprueba los puntos de la
//     copperlist/framebuffer que el test declaro.
//  4. Escribe figuras extra en el framebuffer con poke (lock takeover).
//  5. Captura el framebuffer con figuras (framebuffer.png).
//  6. Espera a que la demo restaure el sistema, captura (workbench.png) y
//     comprueba que es distinto (la demo volvio a Workbench).
//  7. Deja verify-report.json y termina con exit code 0/1.
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';
import { spawn } from 'child_process';
import { repoRoot } from '../../../tools/lib/paths.js';
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
// --- Cliente del canal lateral (mismo patron que tools/debug) ---------------
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
// --- Parseo del TestContract (big-endian 68000) -----------------------------
function parseContract(hex) {
    const buf = Buffer.from(hex, 'hex');
    const contract = {
        framebuffer_addr: buf.readUInt32BE(0),
        copper_addr: buf.readUInt32BE(4),
        width: buf.readUInt16BE(8),
        height: buf.readUInt16BE(10),
        planes: buf.readUInt8(12),
        check_count: buf.readUInt8(13),
        checks: [],
    };
    for (let i = 0; i < 16; ++i) {
        const xy = buf.readUInt32BE(16 + i * 4);
        contract.checks.push({ x: xy & 0xffff, y: (xy >>> 16) & 0xffff });
    }
    for (let i = 0; i < 16; ++i) {
        contract.checks[i].expected_color = buf.readUInt8(80 + i);
    }
    return contract;
}
// Lee el color planar de un pixel leyendo 1 byte por plano del framebuffer.
async function readPixelColor(client, framebuffer, x, y, planes, bytesPerRow) {
    let color = 0;
    for (let plane = 0; plane < planes; ++plane) {
        const byteIndex = plane * bytesPerRow * 240 + y * bytesPerRow + (x >> 3);
        const reply = await client.command(`mem ${(framebuffer + byteIndex).toString(16)} 1`, 2000);
        assertOk(reply.ok, `mem pixel fallo: ${JSON.stringify(reply)}`);
        const byte = parseInt(reply.data, 16);
        const bit = (byte >> (7 - (x & 7))) & 1;
        if (bit) {
            color |= 1 << plane;
        }
    }
    return color;
}
// Replica en host el patron que el test dibuja (misma logica planar/Bresenham)
// para comparar el framebuffer byte a byte en el diagnostico.
function buildExpectedFramebuffer(width, height, planes) {
    const bytesPerRow = width / 8;
    const planeBytes = bytesPerRow * height;
    const fb = Buffer.alloc(planeBytes * planes);
    function setPixel(x, y, color) {
        for (let plane = 0; plane < planes; ++plane) {
            const off = plane * planeBytes + y * bytesPerRow + (x >> 3);
            const mask = 0x80 >> (x & 7);
            if (color & (1 << plane)) {
                fb[off] |= mask;
            }
            else {
                fb[off] &= ~mask;
            }
        }
    }
    function drawLine(x0, y0, x1, y1, color) {
        let dx = x1 - x0;
        let dy = y1 - y0;
        const stepX = dx > 0 ? 1 : -1;
        const stepY = dy > 0 ? 1 : -1;
        dx = Math.abs(dx);
        dy = Math.abs(dy);
        let err = dx - dy;
        let x = x0;
        let y = y0;
        for (;;) {
            setPixel(x, y, color);
            if (x === x1 && y === y1) {
                break;
            }
            const e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += stepX;
            }
            if (e2 < dx) {
                err += dx;
                y += stepY;
            }
        }
    }
    drawLine(0, 0, 319, 0, 1);
    drawLine(0, 239, 319, 239, 1);
    drawLine(0, 0, 0, 239, 1);
    drawLine(319, 0, 319, 239, 1);
    drawLine(0, 32, 319, 32, 2);
    drawLine(64, 0, 64, 239, 3);
    drawLine(0, 0, 200, 200, 4);
    return fb;
}
function pokeBytes(planeRowBytes) {
    // El canal lateral limita poke a 256 bytes; aqui cada operacion es 1 byte.
    return planeRowBytes.map((b) => b.toString(16).padStart(2, '0')).join('');
}
async function compareImages(shotA, shotB, minDiffRatio, outDir) {
    const script = `
import sys
from PIL import Image
a = Image.open(r"${shotA}").convert("RGB")
b = Image.open(r"${shotB}").convert("RGB")
w, h = a.size
if b.size != (w, h):
    b = b.resize((w, h))
w = min(w, b.size[0]); h = min(h, b.size[1])
diff = 0
total = w * h
step = max(1, total // 100000)
for y in range(0, h, step):
    for x in range(0, w, step):
        pa = a.getpixel((x, y)); pb = b.getpixel((x, y))
        if abs(pa[0]-pb[0]) > 12 or abs(pa[1]-pb[1]) > 12 or abs(pa[2]-pb[2]) > 12:
            diff += 1
ratio = diff / (total / step / step)
print(f"ratio={ratio:.4f}")
sys.exit(0 if ratio >= ${minDiffRatio} else 1)
`;
    const py = path.join(outDir, 'compare-images.py');
    fs.writeFileSync(py, script);
    const result = await new Promise((resolve) => {
        const child = spawn('python3', [py], { cwd: root, windowsHide: true });
        let out = '';
        child.stdout.on('data', (d) => (out += d.toString()));
        child.once('exit', (code) => resolve({ code, out: out.trim() }));
    });
    fs.rmSync(py, { force: true });
    return result;
}
async function main() {
    const demo = argValue('--demo', 'tests\\l0_bare_metal\\010_display_320x240');
    const demoName = path.basename(demo);
    const port = parseInt(argValue('--port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
    const readyTimeoutMs = parseInt(argValue('--ready-timeout-ms', '30000'), 10);
    const waitMs = parseInt(argValue('--wait-ms', '16000'), 10);
    const builtMap = path.join(root, 'out/demos', demoName, `${demoName}.map`);
    await runBuild(demo);
    const contractSymbol = findMapSymbol(builtMap, 'g_test_contract');
    const runStatusSymbol = findMapSymbol(builtMap, 'g_eng_run_status');
    const mapSections = findMapAllocSections(builtMap);
    assertOk(contractSymbol !== null, 'No se encontro g_test_contract en el map');
    assertOk(runStatusSymbol !== null, 'No se encontro g_eng_run_status en el map');
    const runDir = path.join(root, 'out', 'run', demoName);
    fs.mkdirSync(runDir, { recursive: true });
    const stdoutLog = path.join(runDir, 'verify-framebuffer.stdout.log');
    const stderrLog = path.join(runDir, 'verify-framebuffer.stderr.log');
    fs.rmSync(stdoutLog, { force: true });
    fs.rmSync(stderrLog, { force: true });
    const runner = spawn('bash', [
        'tools/run/run-demo.sh',
        demo,
        '--wait-ms', String(waitMs),
        '--settle-ms', '1500',
        '--side-channel-timeout-ms', '6000',
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
    const report = { demo, demoName, sideChannelPort: port, steps: {} };
    const client = new SideChannelClient(port);
    await client.connect();
    try {
        report.state = await client.command('state');
        assertOk(report.state.ok && report.state.gdbConnected, 'state must report live GDB');
        const contractRuntime = resolveRuntimeSymbolAddress(contractSymbol, mapSections, report.state.sections);
        assertOk(contractRuntime !== null && contractRuntime > 0, 'No se pudo resolver g_test_contract runtime');
        // 1) Leer el contrato y verificar los puntos declarados.
        const contractHex = await client.command(`mem ${contractRuntime.toString(16)} 96`, 3000);
        assertOk(contractHex.ok, `No se pudo leer g_test_contract: ${JSON.stringify(contractHex)}`);
        const contract = parseContract(contractHex.data);
        report.contract = {
            framebuffer_addr: hex32(contract.framebuffer_addr),
            copper_addr: hex32(contract.copper_addr),
            width: contract.width,
            height: contract.height,
            planes: contract.planes,
        };
        assertOk(contract.framebuffer_addr > 0, 'El contrato no publico direccion de framebuffer');
        assertOk(contract.width === 320 && contract.height === 240 && contract.planes === 5, 'Geometria inesperada en el contrato');
        const bytesPerRow = contract.width / 8;
        const pointChecks = [];
        let pointsOk = true;
        for (let i = 0; i < contract.check_count; ++i) {
            const { x, y, expected_color: expected } = contract.checks[i];
            const color = await readPixelColor(client, contract.framebuffer_addr, x, y, contract.planes, bytesPerRow);
            const ok = color === expected;
            pointsOk = pointsOk && ok;
            pointChecks.push({ x, y, expected, actual: color, ok });
        }
        report.steps.contract_points = { ok: pointsOk, checks: pointChecks };
        // Diagnostico: cuando hay fallo, descargar el framebuffer completo en tramos
        // (el canal limita mem a 4096 bytes) y compararlo con el patron esperado.
        if (!pointsOk) {
            const totalBytes = bytesPerRow * contract.height * contract.planes;
            const hexParts = [];
            for (let off = 0; off < totalBytes; off += 4096) {
                const n = Math.min(4096, totalBytes - off);
                const part = await client.command(`mem ${(contract.framebuffer_addr + off).toString(16)} ${n}`, 5000);
                if (!part.ok) {
                    hexParts.push(null);
                    break;
                }
                hexParts.push(part.data);
            }
            if (hexParts.every((p) => p !== null)) {
                const fb = Buffer.from(hexParts.join(''), 'hex');
                const expected = buildExpectedFramebuffer(contract.width, contract.height, contract.planes);
                let diffs = 0;
                const first = [];
                for (let i = 0; i < fb.length; ++i) {
                    if (fb[i] !== expected[i]) {
                        ++diffs;
                        if (first.length < 24) {
                            const plane = Math.floor(i / (bytesPerRow * contract.height));
                            const row = Math.floor((i % (bytesPerRow * contract.height)) / bytesPerRow);
                            const col = i % bytesPerRow;
                            first.push({ plane, y: row, byte: col, actual: fb[i].toString(16).padStart(2, '0'), expected: expected[i].toString(16).padStart(2, '0') });
                        }
                    }
                }
                fs.writeFileSync(path.join(runDir, 'framebuffer.bin'), fb);
                console.error(`DIAG framebuffer compare: ${diffs}/${fb.length} bytes difieren; primeros=${JSON.stringify(first)}`);
            }
            else {
                console.error('DIAG descarga de framebuffer incompleta');
            }
        }
        assertOk(pointsOk, 'Fallo la verificacion de puntos del contrato');
        // 2) Escribir figuras extra por poke (lock takeover): dos franjas de 8 px.
        const figure = await client.command('lock acquire verify takeover');
        assertOk(figure.ok && figure.mode === 'takeover', 'No se pudo tomar takeover lock');
        const figByte = 'ff';
        const redPokes = [];
        for (let y = 120; y < 128; ++y) {
            // Franja roja (color 1 = bit0) en y=120..127.
            const addr = contract.framebuffer_addr + 0 * bytesPerRow * 240 + y * bytesPerRow + 0;
            const poke = await client.command(`poke ${addr.toString(16)} ${pokeBytes([0xff])} red-franja-y${y}`, 3000);
            assertOk(poke.ok && poke.status === 'queued', `poke fallo: ${JSON.stringify(poke)}`);
            const done = await waitForAction(client, poke.id, 3000);
            assertOk(done.result?.ok, `poke no aplico: ${JSON.stringify(done)}`);
            redPokes.push({ writeId: done.result.writeId, addr: hex32(addr) });
        }
        const greenPokes = [];
        for (let y = 136; y < 144; ++y) {
            // Franja verde (color 2 = bit1) en y=136..143.
            const addr = contract.framebuffer_addr + 1 * bytesPerRow * 240 + y * bytesPerRow + 0;
            const poke = await client.command(`poke ${addr.toString(16)} ${pokeBytes([0xff])} green-franja-y${y}`, 3000);
            assertOk(poke.ok && poke.status === 'queued', `poke fallo: ${JSON.stringify(poke)}`);
            const done = await waitForAction(client, poke.id, 3000);
            assertOk(done.result?.ok, `poke no aplico: ${JSON.stringify(done)}`);
            greenPokes.push({ writeId: done.result.writeId, addr: hex32(addr) });
        }
        const release = await client.command('lock release verify');
        assertOk(release.ok && !release.locked, 'No se pudo liberar takeover lock');
        report.steps.figures = { ok: true, redPokes: redPokes.length, greenPokes: greenPokes.length };
        // 3) Comprobar que una franja se escribio realmente (leer de vuelta).
        const probeAddr = contract.framebuffer_addr + 0 * bytesPerRow * 240 + 120 * bytesPerRow + 8;
        const probe = await client.command(`mem ${probeAddr.toString(16)} 1`, 2000);
        assertOk(probe.ok && probe.data === 'ff', `No se leyo la franja inyectada: ${JSON.stringify(probe)}`);
        report.steps.figure_probe = { ok: true, addr: hex32(probeAddr), data: probe.data };
        // 4) Captura del framebuffer con figuras (mientras la demo vive).
        const shotFb = path.join(runDir, 'framebuffer.png');
        fs.rmSync(shotFb, { force: true });
        const shotQueued = await client.command(`screenshot ${shotFb.replace(/\\/g, '/')}`, 3000);
        assertOk(shotQueued.ok && shotQueued.status === 'queued', `screenshot fallo: ${JSON.stringify(shotQueued)}`);
        const shotDone = await waitForAction(client, shotQueued.id, 4000);
        assertOk(shotDone.result?.ok, `screenshot no aplico: ${JSON.stringify(shotDone)}`);
        assertOk(fs.existsSync(shotFb), 'No se genero framebuffer.png');
        report.steps.framebuffer_shot = { ok: true, path: shotFb };
        // 5) Esperar a que la demo restaure el sistema (sale al frame 240 ~= 4,8 s)
        //    y capturar el Workbench.
        const exitWaitMs = parseInt(argValue('--exit-wait-ms', '7000'), 10);
        await sleep(exitWaitMs);
        const shotWb = path.join(runDir, 'workbench.png');
        fs.rmSync(shotWb, { force: true });
        const wbQueued = await client.command(`screenshot ${shotWb.replace(/\\/g, '/')}`, 3000);
        if (wbQueued.ok && wbQueued.status === 'queued') {
            const wbDone = await waitForAction(client, wbQueued.id, 4000);
            assertOk(wbDone.result?.ok, `screenshot workbench no aplico: ${JSON.stringify(wbDone)}`);
        }
        assertOk(fs.existsSync(shotWb), 'No se genero workbench.png');
        report.steps.workbench_shot = { ok: true, path: shotWb };
        // 6) Comparar: si la demo volvio a Workbench, la imagen cambio mucho.
        const compare = await compareImages(shotFb, shotWb, 0.5, runDir);
        assertOk(compare.code === 0, `workbench.png no parece el Workbench restaurado (${compare.out})`);
        report.steps.workbench_diff = { ok: true, compareOut: compare.out };
    }
    finally {
        client.close();
    }
    // 7) Dejar que el runner termine (captura final + cierre) y validar su salida.
    const exitCode = await new Promise((resolve) => {
        runner.once('exit', (code) => resolve(code));
    });
    assertOk(exitCode === 0, `runner exited with ${exitCode}\n${stdoutText}\n${stderrText}`);
    report.runner_exit = exitCode;
    report.status = 'ok';
    fs.writeFileSync(path.join(runDir, 'verify-report.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify(report, null, 2));
}
main().catch((err) => {
    console.error(err.stack || err.message);
    process.exit(1);
});
